/**
 * The Vulkan renderer's frames, swapchain and resources (engine/rendering/vulkan/renderer.h). The game draws into a back buffer of its
 * own resolution (render scale included); at the end of a frame that is scaled onto the swapchain image and presented.
 */
#include <string.h>
#include <xmmintrin.h>

#include "core/config.h"
#include "engine/resources/streamprofile.h"
#include "core/log.h"
#include "dev/profile.h"
#include "utils/simd.h"
#include "engine/rendering/vulkan/renderer.h"
#include "core/sdl.h"

extern "C" int TheGameFlowManager_CurrentGameFlowState;

namespace nfs::vk
{
Renderer *g_renderer;
int64_t g_profile[kProfileCount];
bool g_profileEnabled;

namespace
{
const uint32_t kFramesInFlight = 2;
const uint32_t kRingSize = 32u << 20;
const uint32_t kStagingSize = 64u << 20;
const uint32_t kMaxGarbage = 2048;
const DWORD kCaptureInterval = 4000;
const VkMemoryPropertyFlags kHostMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
/** Preferred for host memory the CPU reads: uncached (write-combined) memory reads at a few hundred MB/s, so a car skin's
 * 4 MB lock took 23 ms to upload from (2026-11-18). */
const VkMemoryPropertyFlags kReadMemory = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
/** -gpucrumbs: commands remembered for the report after a hang. */
const uint32_t kCrumbs = 65536;
/** -gpuprofile: timestamps a frame at most, and the keys past the pixel shaders' (pixel shader * 3 + target class): the
 * frame's start (work before its first draw) and its end. */
const uint32_t kShaderSpans = 4096;
const int kSpanTargets = 3;
const char *const kSpanTargetNames[kSpanTargets] = {"main", "cube", "other"};

bool Check(VkResult result, const char *what)
{
    if (result == VK_SUCCESS)
        return true;
    nfs::Log("Vulkan: %s failed (%s)", what, ResultName(result));
    return false;
}

uint32_t FullMipCount(uint32_t width, uint32_t height)
{
    uint32_t levels = 1;
    while ((width | height) > 1)
    {
        width >>= 1;
        height >>= 1;
        ++levels;
    }
    return levels;
}
/** Waits for a fence (the whole device without one) and logs where a wait failed or took a quarter of a second or more:
 * a GPU hang shows up as the wait that sat out the driver's timeout before the device was reported lost. */
VkResult WaitFor(const Device &device, const VkFence *fence, const char *where)
{
    int64_t start, end, frequency;
    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&start));
    const VkResult result = fence ? vkWaitForFences(device.device, 1, fence, VK_TRUE, UINT64_MAX)
                                  : vkDeviceWaitIdle(device.device);
    if (result == VK_ERROR_DEVICE_LOST && g_renderer)
        g_renderer->ReportCrumbs();
    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&end));
    QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER *>(&frequency));
    const int ms = static_cast<int>(static_cast<double>(end - start) * 1000.0 / static_cast<double>(frequency));
    if (result != VK_SUCCESS || ms >= 250)
    {
        static int logged;
        if (logged++ < 16)
            nfs::Log("Vulkan: waited %d ms for the GPU in %s (%s), tick %u", ms, where, ResultName(result),
                     GetTickCount());
    }
    return result;
}

HRESULT __stdcall ObjectQueryInterface(void *, const void *, void **object)
{
    if (object)
        *object = nullptr;
    return E_NOINTERFACE;
}

ULONG __stdcall ObjectAddRef(Object *object)
{
    return ++object->refs;
}

ULONG __stdcall ObjectRelease(Object *object)
{
    ULONG refs = object->refs - 1;
    if (g_renderer)
        g_renderer->Release(object);
    else
        object->refs = refs;
    return refs;
}
} // namespace

const void *const kObjectVtable[3] = {reinterpret_cast<const void *>(&ObjectQueryInterface),
                                      reinterpret_cast<const void *>(&ObjectAddRef),
                                      reinterpret_cast<const void *>(&ObjectRelease)};

struct Renderer::Frame
{
    VkCommandPool pool;
    VkCommandBuffer cmd;
    VkFence fence;
    VkSemaphore acquired;
    Buffer ring;
    uint32_t ringUsed;
    /** Set 0 of the pipeline layout, its constant buffers in `ring`. */
    VkDescriptorSet constants;
    /** Set 2, the scene: its constants and the shadow atlas and lamp volumes it shows (Renderer::SetSunShadowAtlas,
     * SetLampVolumes). */
    VkDescriptorSet scene;
    Buffer sceneConstants;
    Texture *sceneShadow;
    bool sceneShadowWritten;
    Texture *sceneHeadlightShadow;
    bool sceneHeadlightShadowWritten;
    Texture *sceneLamps[kLampVolumes];
    bool sceneLampsWritten;
    bool submitted;
    /** RequestReadback's copy for this frame: host-visible memory, valid once the frame's fence signals. */
    Buffer readback;
    bool readbackPending;
    /** Profiler timestamps written this frame (a bit per slot). */
    VkQueryPool timestamps;
    uint32_t timestampsWritten;
    /** -gpuprofile: a timestamp where each span starts, and the span's key and draws. */
    VkQueryPool spans;
    uint32_t spanCount;
    uint16_t spanKey[kShaderSpans];
    uint16_t spanDraws[kShaderSpans];
    uint32_t garbageCount;
    Object *garbage[kMaxGarbage];
};

/** Geometry copies waiting to be recorded together (FlushGeometryCopies). */
const uint32_t kMaxGeometryCopies = 4096;

struct Renderer::Upload
{
    VkCommandPool pool;
    VkCommandBuffer cmd;
    VkFence fence;
    Buffer staging;
    uint32_t used;
    bool recording;
    bool pending;
    VkBuffer copyTargets[kMaxGeometryCopies];
    VkBufferCopy copies[kMaxGeometryCopies];
    uint32_t copyCount;
};

bool HasArgument(const char *name)
{
    const char *line = GetCommandLineA();
    const int length = lstrlenA(name);
    for (const char *p = line; *p; ++p)
    {
        if ((p == line || p[-1] == ' ') && (p[length] == 0 || p[length] == ' '))
        {
            int i = 0;
            while (i < length && p[i] == name[i])
                ++i;
            if (i == length)
                return true;
        }
    }
    return false;
}

/** Vulkan is the game's renderer: the Direct3D 9 one is being taken out (docs/VULKAN.md, "Vulkan only"), and
 * [Display] Renderer and -vulkan no longer choose anything. */
bool Enabled()
{
    return true;
}

bool Renderer::Initialize(HWND window, uint32_t width, uint32_t height)
{
    window_ = window;
    DeviceOptions options = {};
    options.present = window != nullptr;
    char validation[16];
    nfs::config::String("Display", "VulkanValidation", "off", validation, sizeof(validation));
    options.validation = lstrcmpA(validation, "on") == 0;
    if (!CreateDevice(device_, options))
        return false;
    constantsAlignment_ = static_cast<uint32_t>(device_.properties.limits.minUniformBufferOffsetAlignment);
    if (device_.properties.limits.minStorageBufferOffsetAlignment > constantsAlignment_)
        constantsAlignment_ = static_cast<uint32_t>(device_.properties.limits.minStorageBufferOffsetAlignment);
    const VkMemoryPropertyFlags crumbMemory =
        kHostMemory | (device_.caps.uncachedMemory
                           ? VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD | VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD
                           : 0);
    if (device_.caps.bufferMarker &&
        CreateBuffer(device_, 64, VK_BUFFER_USAGE_TRANSFER_DST_BIT, crumbMemory, crumbBuffer_))
    {
        memset(crumbBuffer_.mapped, 0, 64);
        crumbs_ = static_cast<CrumbInfo *>(::operator new(sizeof(CrumbInfo) * kCrumbs));
        memset(crumbs_, 0, sizeof(CrumbInfo) * kCrumbs);
        nfs::Log("Vulkan: GPU crumbs on (VK_AMD_buffer_marker%s)",
                 device_.caps.uncachedMemory ? ", uncached memory" : "");
    }
    capture_ = HasArgument("-vkcapture");
    stats_ = HasArgument("-vkstats");
    g_profileEnabled = stats_;
    if (!device_.caps.pushDescriptor && !device_.caps.compat)
    {
        nfs::Log("Vulkan: %s has no VK_KHR_push_descriptor", device_.properties.deviceName);
        return false;
    }
    if (!CreatePipelineLayout(device_, layout_))
        return false;
    VkPipelineCacheCreateInfo cache = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    void *cacheData = nullptr;
    LoadPipelineCache(cache, cacheData);
    if (vkCreatePipelineCache(device_.device, &cache, nullptr, &pipelineCache_) != VK_SUCCESS && cacheData)
    {
        cache.initialDataSize = 0;
        cache.pInitialData = nullptr;
        vkCreatePipelineCache(device_.device, &cache, nullptr, &pipelineCache_);
    }
    ::operator delete(cacheData);

    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(device_.physical, VK_FORMAT_D24_UNORM_S8_UINT, &props);
    depthFormat_ = (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
                       ? VK_FORMAT_D24_UNORM_S8_UINT
                       : VK_FORMAT_D32_SFLOAT_S8_UINT;

    if (window)
    {
        // SDL has the game's window (core/sdl.h).
        if (!nfs::sdl::Window() || !SDL_Vulkan_CreateSurface(nfs::sdl::Window(), device_.instance, nullptr, &surface_))
        {
            nfs::Log("Vulkan: no surface for the game window (%s)", SDL_GetError());
            return false;
        }
        VkBool32 supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device_.physical, device_.queueFamily, surface_, &supported);
        if (!supported)
        {
            nfs::Log("Vulkan: the queue cannot present to the game window");
            return false;
        }
    }

    frames_ = static_cast<Frame *>(::operator new(sizeof(Frame) * kFramesInFlight));
    memset(frames_, 0, sizeof(Frame) * kFramesInFlight);
    for (uint32_t i = 0; i < kFramesInFlight; ++i)
    {
        Frame &f = frames_[i];
        VkCommandPoolCreateInfo pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool.queueFamilyIndex = device_.queueFamily;
        VkCommandBufferAllocateInfo alloc = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        VkFenceCreateInfo fence = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VkSemaphoreCreateInfo semaphore = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (!Check(vkCreateCommandPool(device_.device, &pool, nullptr, &f.pool), "vkCreateCommandPool"))
            return false;
        alloc.commandPool = f.pool;
        if (!Check(vkAllocateCommandBuffers(device_.device, &alloc, &f.cmd), "vkAllocateCommandBuffers") ||
            !Check(vkCreateFence(device_.device, &fence, nullptr, &f.fence), "vkCreateFence") ||
            !Check(vkCreateSemaphore(device_.device, &semaphore, nullptr, &f.acquired), "vkCreateSemaphore"))
            return false;
        if (!CreateBuffer(device_, kRingSize,
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                              VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          kHostMemory, f.ring, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            return false;
        if (!constantsPool_ && !(constantsPool_ = CreateConstantsPool(device_, kFramesInFlight)))
            return false;
        f.constants = AllocateConstantsSet(device_, layout_, constantsPool_, f.ring.buffer);
        if (!f.constants)
            return false;
        if (!CreateBuffer(device_, kSceneConstantsBytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, kHostMemory,
                          f.sceneConstants))
            return false;
        memset(f.sceneConstants.mapped, 0, kSceneConstantsBytes);
        f.scene = AllocateSceneSet(device_, layout_, constantsPool_, f.sceneConstants.buffer);
        if (!f.scene)
            return false;
        if (device_.properties.limits.timestampComputeAndGraphics)
        {
            VkQueryPoolCreateInfo queries = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            queries.queryType = VK_QUERY_TYPE_TIMESTAMP;
            queries.queryCount = kMaxTimestamps;
            timestamps_ = vkCreateQueryPool(device_.device, &queries, nullptr, &f.timestamps) == VK_SUCCESS;
            if (HasArgument("-gpuprofile"))
            {
                queries.queryCount = kShaderSpans;
                shaderTiming_ = vkCreateQueryPool(device_.device, &queries, nullptr, &f.spans) == VK_SUCCESS;
            }
        }
    }

    upload_ = static_cast<Upload *>(::operator new(sizeof(Upload)));
    memset(upload_, 0, sizeof(Upload));
    {
        VkCommandPoolCreateInfo pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool.queueFamilyIndex = device_.queueFamily;
        VkCommandBufferAllocateInfo alloc = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        VkFenceCreateInfo fence = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (!Check(vkCreateCommandPool(device_.device, &pool, nullptr, &upload_->pool), "vkCreateCommandPool"))
            return false;
        alloc.commandPool = upload_->pool;
        if (!Check(vkAllocateCommandBuffers(device_.device, &alloc, &upload_->cmd), "vkAllocateCommandBuffers") ||
            !Check(vkCreateFence(device_.device, &fence, nullptr, &upload_->fence), "vkCreateFence") ||
            !CreateBuffer(device_, kStagingSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, kHostMemory, upload_->staging))
            return false;
    }

    static const uint32_t kBlack = 0xFF000000;
    static const uint32_t kBlackCube[6] = {kBlack, kBlack, kBlack, kBlack, kBlack, kBlack};
    defaultTextures_[0] = CreateTexture(1, 1, 1, 21, false);
    defaultTextures_[1] = CreateTexture(1, 1, 1, 21, false, true);
    if (!defaultTextures_[0] || !defaultTextures_[1])
        return false;
    UploadTexture(defaultTextures_[0], 0, 0, &kBlack, 4);
    for (uint32_t face = 0; face < 6; ++face)
        UploadTexture(defaultTextures_[1], 0, face, &kBlackCube[face], 4);
    defaultTextures_[2] = CreateVolumeTexture(1, 1, 1, 1, 21);
    if (!defaultTextures_[2])
        return false;
    UploadTexture(defaultTextures_[2], 0, 0, &kBlack, 4);

    if ((surface_ && !CreateSwapchain()) || !CreateBackBuffer(width, height))
        return false;
    ResetState();
    PrewarmPipelines();
    savedPipelineCount_ = pipelineCount_;
    nfs::Log("Vulkan: renderer ready, back buffer %dx%d, swapchain %dx%d", width, height, swapchainExtent_.width,
             swapchainExtent_.height);
    return true;
}

void Renderer::Shutdown()
{
    if (!device_.device)
        return;
    vkDeviceWaitIdle(device_.device);
    FlushPipelineCache();
    for (uint32_t i = 0; frames_ && i < kFramesInFlight; ++i)
        CollectGarbage(frames_[i]);
    // Geometry buffers the game still holds in arenas go with them.
    for (uint32_t i = 0; i < arenaCount_; ++i)
    {
        DestroyBuffer(device_, arenas_[i].buffer);
        ::operator delete(arenas_[i].free);
        arenas_[i] = {};
    }
    arenaCount_ = 0;
    DestroyBackBuffer();
    DestroyBuffer(device_, captureBuffer_);
    if (defaultTextures_[0])
        DestroyTexture(defaultTextures_[0]);
    if (defaultTextures_[1])
        DestroyTexture(defaultTextures_[1]);
    if (defaultTextures_[2])
        DestroyTexture(defaultTextures_[2]);
    defaultTextures_[0] = defaultTextures_[1] = defaultTextures_[2] = nullptr;
    DestroySwapchain();
    if (upload_)
    {
        DestroyBuffer(device_, upload_->staging);
        if (upload_->fence)
            vkDestroyFence(device_.device, upload_->fence, nullptr);
        if (upload_->pool)
            vkDestroyCommandPool(device_.device, upload_->pool, nullptr);
        ::operator delete(upload_);
        upload_ = nullptr;
    }
    for (uint32_t i = 0; frames_ && i < kFramesInFlight; ++i)
    {
        Frame &f = frames_[i];
        DestroyBuffer(device_, f.ring);
        DestroyBuffer(device_, f.sceneConstants);
        DestroyBuffer(device_, f.readback);
        if (f.acquired)
            vkDestroySemaphore(device_.device, f.acquired, nullptr);
        if (f.fence)
            vkDestroyFence(device_.device, f.fence, nullptr);
        if (f.pool)
            vkDestroyCommandPool(device_.device, f.pool, nullptr);
        if (f.timestamps)
            vkDestroyQueryPool(device_.device, f.timestamps, nullptr);
        if (f.spans)
            vkDestroyQueryPool(device_.device, f.spans, nullptr);
    }
    ::operator delete(frames_);
    frames_ = nullptr;
    DestroyStateObjects();
    if (pipelineCache_)
        vkDestroyPipelineCache(device_.device, pipelineCache_, nullptr);
    if (surface_)
        vkDestroySurfaceKHR(device_.instance, surface_, nullptr);
    DestroyShaderModules(device_);
    if (constantsPool_)
        vkDestroyDescriptorPool(device_.device, constantsPool_, nullptr);
    constantsPool_ = VK_NULL_HANDLE;
    DestroyPipelineLayout(device_, layout_);
    DestroyDevice(device_);
}

bool Renderer::CreateSwapchain()
{
    VkSurfaceCapabilitiesKHR caps;
    if (!Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device_.physical, surface_, &caps),
               "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
        return false;
    VkSurfaceFormatKHR formats[32];
    uint32_t formatCount = 32;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device_.physical, surface_, &formatCount, formats);
    VkSurfaceFormatKHR chosen = formats[0];
    for (uint32_t i = 0; i < formatCount; ++i)
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM)
            chosen = formats[i];
    VkPresentModeKHR modes[16];
    uint32_t modeCount = 16;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device_.physical, surface_, &modeCount, modes);
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync_)
    {
        for (uint32_t i = 0; i < modeCount; ++i)
            if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
                mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        if (mode == VK_PRESENT_MODE_FIFO_KHR)
            for (uint32_t i = 0; i < modeCount; ++i)
                if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
                    mode = VK_PRESENT_MODE_MAILBOX_KHR;
    }
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFF)
    {
        RECT client;
        GetClientRect(window_, &client);
        extent.width = static_cast<uint32_t>(client.right - client.left);
        extent.height = static_cast<uint32_t>(client.bottom - client.top);
    }
    if (extent.width == 0 || extent.height == 0)
    {
        swapchainDirty_ = true;
        return true;
    }
    uint32_t images = caps.minImageCount + 1;
    if (caps.maxImageCount && images > caps.maxImageCount)
        images = caps.maxImageCount;

    VkSwapchainCreateInfoKHR info = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface_;
    info.minImageCount = images;
    info.imageFormat = chosen.format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = mode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = swapchain_;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkResult result = vkCreateSwapchainKHR(device_.device, &info, nullptr, &swapchain);
    if (swapchain_)
    {
        vkDestroySwapchainKHR(device_.device, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    if (!Check(result, "vkCreateSwapchainKHR"))
        return false;
    swapchain_ = swapchain;
    swapchainFormat_ = chosen.format;
    swapchainExtent_ = extent;
    swapchainImageCount_ = 8;
    vkGetSwapchainImagesKHR(device_.device, swapchain_, &swapchainImageCount_, swapchainImages_);
    for (uint32_t i = 0; i < swapchainImageCount_; ++i)
    {
        if (!presentReady_[i])
        {
            VkSemaphoreCreateInfo semaphore = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            vkCreateSemaphore(device_.device, &semaphore, nullptr, &presentReady_[i]);
        }
    }
    swapchainDirty_ = false;
    nfs::Log("Vulkan: swapchain %dx%d, %d images, present mode %d", extent.width, extent.height,
             swapchainImageCount_, mode);
    return true;
}

void Renderer::DestroySwapchain()
{
    if (swapchain_)
        vkDestroySwapchainKHR(device_.device, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
    for (VkSemaphore &s : presentReady_)
    {
        if (s)
            vkDestroySemaphore(device_.device, s, nullptr);
        s = VK_NULL_HANDLE;
    }
}

bool Renderer::CreateBackBuffer(uint32_t width, uint32_t height)
{
    backBuffer_ = CreateTexture(width, height, 1, 21, true);
    depthBuffer_ = CreateTexture(width, height, 1, 75, true);
    return backBuffer_ && depthBuffer_;
}

void Renderer::DestroyBackBuffer()
{
    if (backBuffer_)
        DestroyTexture(backBuffer_);
    if (depthBuffer_)
        DestroyTexture(depthBuffer_);
    backBuffer_ = depthBuffer_ = nullptr;
}

bool Renderer::Resize(uint32_t width, uint32_t height)
{
    if (frameActive_)
        EndFrame();
    WaitFor(device_, nullptr, "Resize");
    for (uint32_t i = 0; i < kFramesInFlight; ++i)
        CollectGarbage(frames_[i]);
    DestroyBackBuffer();
    DestroyBuffer(device_, captureBuffer_);
    swapchainDirty_ = true;
    if (colorTarget_ == backBuffer_ || depthTarget_ == depthBuffer_)
        colorTarget_ = depthTarget_ = nullptr;
    ++stateSerial_;
    return CreateBackBuffer(width, height);
}

void Renderer::SetVSync(bool on)
{
    if (vsync_ != on)
    {
        vsync_ = on;
        swapchainDirty_ = true;
    }
}

void Renderer::BeginFrame()
{
    if (frameActive_)
        return;
    if (device_.caps.compat)
        compat::BeginFrame();
    Frame &f = frames_[frameIndex_];
    if (f.submitted)
    {
        int64_t start, end;
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&start));
        WaitFor(device_, &f.fence, "BeginFrame");
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&end));
        cost_.fenceTicks += end - start;
        if (nfs::profile::g_enabled)
            nfs::profile::AddTime(nfs::profile::kTimerGpuWait, end - start);
        static const double msPerTick = [] {
            LARGE_INTEGER frequency;
            QueryPerformanceFrequency(&frequency);
            return 1000.0 / static_cast<double>(frequency.QuadPart);
        }();
        gpuWaitAverage_ += (static_cast<double>(end - start) * msPerTick - gpuWaitAverage_) * 0.02;
        gpuBound_ = gpuBound_ ? gpuWaitAverage_ > 0.05 : gpuWaitAverage_ > 0.4;
        f.submitted = false;
    }
    if (f.readbackPending)
    {
        memcpy(readback_, f.readback.mapped, kReadbackBytes);
        readbackValid_ = true;
        f.readbackPending = false;
    }
    if (f.timestampsWritten)
    {
        uint64_t results[kMaxTimestamps][2];
        vkGetQueryPoolResults(device_.device, f.timestamps, 0, kMaxTimestamps, sizeof(results), results,
                              sizeof(results[0]), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        const double period = static_cast<double>(device_.properties.limits.timestampPeriod);
        latestTimestampsWritten_ = 0;
        for (uint32_t i = 0; i < kMaxTimestamps; ++i)
            if ((f.timestampsWritten & (1u << i)) && results[i][1])
            {
                latestTimestamps_[i] =
                    static_cast<uint64_t>(static_cast<int64_t>(static_cast<double>(static_cast<int64_t>(results[i][0])) * period));
                latestTimestampsWritten_ |= 1u << i;
            }
        f.timestampsWritten = 0;
    }
    if (f.spanCount > 1 && shaderTiming_)
    {
        static uint64_t results[kShaderSpans][2];
        vkGetQueryPoolResults(device_.device, f.spans, 0, f.spanCount, f.spanCount * sizeof(results[0]), results,
                              sizeof(results[0]), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (!shaderTimes_)
        {
            const size_t bytes = sizeof(ShaderTime) * (kShaderCount * kSpanTargets + 2);
            shaderTimes_ = static_cast<ShaderTime *>(::operator new(bytes));
            memset(shaderTimes_, 0, bytes);
        }
        const double period = static_cast<double>(device_.properties.limits.timestampPeriod);
        for (uint32_t i = 0; i + 1 < f.spanCount; ++i)
            if (results[i][1] && results[i + 1][1] && results[i + 1][0] >= results[i][0])
            {
                ShaderTime &t = shaderTimes_[f.spanKey[i]];
                t.ns += static_cast<double>(static_cast<int64_t>(results[i + 1][0] - results[i][0])) * period;
                t.draws += f.spanDraws[i];
            }
        ++shaderTimeFrames_;
    }
    f.spanCount = 0;
    CollectGarbage(f);
    if (swapchainDirty_ && surface_)
    {
        WaitFor(device_, nullptr, "swapchain");
        CreateSwapchain();
    }
    f.ringUsed = 0;
    vkResetCommandPool(device_.device, f.pool, 0);
    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    stream::Track(f.cmd, f.pool);
    vkBeginCommandBuffer(f.cmd, &begin);
    cmd_ = f.cmd;
    frameConstants_ = f.constants;
    frameScene_ = f.scene;
    frameSceneConstants_ = f.sceneConstants.mapped;
    UpdateSceneShadow();
    if (f.timestamps)
        vkCmdResetQueryPool(f.cmd, f.timestamps, 0, kMaxTimestamps);
    if (shaderTiming_)
    {
        vkCmdResetQueryPool(f.cmd, f.spans, 0, kShaderSpans);
        spanKey_ = -1;
        StartSpan(static_cast<int>(kShaderCount) * kSpanTargets);
    }
    recorded_.valid = false;
    // Constants uploaded last frame live in that frame's ring, which is reused while this frame may still run.
    vsDirty_ = psDirty_ = true;
    frameActive_ = true;
    if (stats_)
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&cost_.beginTick));
    rendering_ = false;
    boundPipeline_ = VK_NULL_HANDLE;
    vsDirty_ = psDirty_ = true;
    vsConstantsCount_ = psConstantsCount_ = 0;
    if (!colorTarget_ && !depthTarget_)
        SetRenderTarget(backBuffer_, depthBuffer_);
}

void Renderer::StartSpan(int key)
{
    Frame &f = frames_[frameIndex_];
    if (f.spanCount >= kShaderSpans)
        return;
    vkCmdWriteTimestamp2(cmd_, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, f.spans, f.spanCount);
    f.spanKey[f.spanCount] = static_cast<uint16_t>(key);
    f.spanDraws[f.spanCount] = 0;
    ++f.spanCount;
    spanKey_ = key;
}

void Renderer::TimeDraw(int key)
{
    if (key != spanKey_)
        StartSpan(key);
    Frame &f = frames_[frameIndex_];
    if (f.spanCount)
        ++f.spanDraws[f.spanCount - 1];
}

void Renderer::LogShaderTimes()
{
    if (!shaderTiming_ || !shaderTimes_ || !shaderTimeFrames_)
        return;
    const int keys = static_cast<int>(kShaderCount) * kSpanTargets + 1;
    double total = 0.0;
    for (int k = 0; k < keys; ++k)
        total += shaderTimes_[k].ns;
    const double frames = shaderTimeFrames_;
    nfs::Log("gpu shaders: %d frames, %d.%02d ms a frame", static_cast<int>(frames),
             static_cast<int>(total / frames / 1e6), static_cast<int>(total / frames / 1e4) % 100);
    for (int shown = 0; shown < 48; ++shown)
    {
        int best = -1;
        for (int k = 0; k < keys; ++k)
            if (shaderTimes_[k].ns > 0.0 && (best < 0 || shaderTimes_[k].ns > shaderTimes_[best].ns))
                best = k;
        if (best < 0)
            break;
        const double us = shaderTimes_[best].ns / frames / 1000.0;
        if (best == keys - 1)
            nfs::Log("gpu shaders:   %5d us  (frame start)", static_cast<int>(us));
        else
            nfs::Log("gpu shaders:   %5d us  %-36s %-5s %4d draws", static_cast<int>(us),
                     kShaders[best / kSpanTargets].name, kSpanTargetNames[best % kSpanTargets],
                     static_cast<int>(shaderTimes_[best].draws / frames));
        shaderTimes_[best].ns = -shaderTimes_[best].ns;
    }
    memset(shaderTimes_, 0, sizeof(ShaderTime) * (keys + 1));
    shaderTimeFrames_ = 0;
}

void Renderer::WriteTimestamp(uint32_t slot)
{
    if (!timestamps_ || slot >= kMaxTimestamps)
        return;
    if (!frameActive_)
        BeginFrame();
    Frame &f = frames_[frameIndex_];
    vkCmdWriteTimestamp2(cmd_, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, f.timestamps, slot);
    f.timestampsWritten |= 1u << slot;
}

bool Renderer::LatestTimestamps(uint64_t (&nanoseconds)[kMaxTimestamps], uint32_t &written) const
{
    if (!timestamps_)
        return false;
    memcpy(nanoseconds, latestTimestamps_, sizeof(latestTimestamps_));
    written = latestTimestampsWritten_;
    return true;
}

void Renderer::EndFrame()
{
    ProfileScope profile(kProfileFrame);
    if (!frameActive_)
        BeginFrame();
    Frame &f = frames_[frameIndex_];
    EndRendering();
    if (shaderTiming_)
        StartSpan(static_cast<int>(kShaderCount) * kSpanTargets + 1);

    uint32_t imageIndex = ~0u;
    if (swapchain_ && !swapchainDirty_)
    {
        int64_t start, end;
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&start));
        VkResult result = vkAcquireNextImageKHR(device_.device, swapchain_, UINT64_MAX, f.acquired, VK_NULL_HANDLE,
                                                &imageIndex);
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&end));
        cost_.acquireTicks += end - start;
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            swapchainDirty_ = true;
            imageIndex = ~0u;
        }
        else if (result == VK_SUBOPTIMAL_KHR)
        {
            swapchainDirty_ = true;
        }
        else if (result != VK_SUCCESS)
        {
            imageIndex = ~0u;
        }
    }
    if (stats_ && GetTickCount() - lastStats_ >= kCaptureInterval)
    {
        lastStats_ = GetTickCount();
        nfs::Log("Vulkan stats: %d drawn; dropped: %d no shaders, %d no layout, %d no target, %d no pipeline, %d no "
                 "buffers; %d pipelines",
                 statCounts_[kStatDrawn], statCounts_[kStatNoShaders], statCounts_[kStatNoLayout],
                 statCounts_[kStatNoTarget], statCounts_[kStatNoPipeline], statCounts_[kStatNoBuffers], pipelineCount_);
        int64_t frequency;
        QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER *>(&frequency));
        const double ms = 1000.0 / static_cast<double>(frequency);
        nfs::Log("Vulkan time: %d frames; ms in effect passes %d, commits %d, draw setup %d, present %d",
                 static_cast<int>(frameNumber_ - statsFrame_),
                 static_cast<int>(static_cast<double>(g_profile[kProfileEffectPass]) * ms),
                 static_cast<int>(static_cast<double>(g_profile[kProfileEffectCommit]) * ms),
                 static_cast<int>(static_cast<double>(g_profile[kProfilePrepareDraw]) * ms),
                 static_cast<int>(static_cast<double>(g_profile[kProfileFrame]) * ms));
        memset(g_profile, 0, sizeof(g_profile));
        callprofile::Log(static_cast<uint32_t>(frameNumber_ - statsFrame_));
        nfs::Log("Vulkan pipeline binds changed: shaders %u, layout %u, topology %u, blending %u, colour writes %u, "
                 "targets %u",
                 bindReasons_[0], bindReasons_[1], bindReasons_[2], bindReasons_[3], bindReasons_[4], bindReasons_[5]);
        memset(bindReasons_, 0, sizeof(bindReasons_));
        LogBinds(static_cast<uint32_t>(frameNumber_ - statsFrame_));
        statsFrame_ = frameNumber_;
        memset(statCounts_, 0, sizeof(statCounts_));
    }
    const bool capture = (capture_ && GetTickCount() - lastCapture_ >= kCaptureInterval) || captureName_[0];
    if (capture)
    {
        lastCapture_ = GetTickCount();
        Transition(backBuffer_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        if (!captureBuffer_.buffer)
            CreateBuffer(device_, backBuffer_->width * backBuffer_->height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         kHostMemory, captureBuffer_, kReadMemory);
        VkBufferImageCopy copy = {};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {backBuffer_->width, backBuffer_->height, 1};
        if (captureBuffer_.buffer)
            vkCmdCopyImageToBuffer(cmd_, backBuffer_->image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   captureBuffer_.buffer, 1, &copy);
    }
    if (imageIndex != ~0u)
    {
        VkImage target = swapchainImages_[imageIndex];
        Transition(backBuffer_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        ImageBarrier(cmd_, target, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE,
                     VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkImageBlit blit = {};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.srcOffsets[1] = {static_cast<int32_t>(backBuffer_->width), static_cast<int32_t>(backBuffer_->height), 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.dstOffsets[1] = {static_cast<int32_t>(swapchainExtent_.width),
                              static_cast<int32_t>(swapchainExtent_.height), 1};
        vkCmdBlitImage(cmd_, backBuffer_->image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        ImageBarrier(cmd_, target, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
    }
    FlushUploads();
    vkEndCommandBuffer(cmd_);

    VkCommandBufferSubmitInfo cmdInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = cmd_;
    VkSemaphoreSubmitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = f.acquired;
    wait.stageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
    VkSemaphoreSubmitInfo signal = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = imageIndex != ~0u ? presentReady_[imageIndex] : VK_NULL_HANDLE;
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    if (imageIndex != ~0u)
    {
        submit.waitSemaphoreInfoCount = 1;
        submit.pWaitSemaphoreInfos = &wait;
        submit.signalSemaphoreInfoCount = 1;
        submit.pSignalSemaphoreInfos = &signal;
    }
    vkResetFences(device_.device, 1, &f.fence);
    // The constants and vertices went into the ring with streaming stores (utils/simd.h).
    _mm_sfence();
    const DWORD submitStart = GetTickCount();
    VkResult result = vkQueueSubmit2(device_.queue, 1, &submit, f.fence);
    if (crumbBuffer_.buffer && GetTickCount() - submitStart >= 250)
        nfs::Log("Vulkan: vkQueueSubmit2 took %u ms (frame %u)", GetTickCount() - submitStart,
                 static_cast<uint32_t>(frameNumber_));
    if (result != VK_SUCCESS)
        nfs::Log("Vulkan: vkQueueSubmit2 failed (%s)", ResultName(result));
    if (result == VK_ERROR_DEVICE_LOST)
        ReportCrumbs();
    f.submitted = result == VK_SUCCESS;
    if (capture && f.submitted && captureBuffer_.buffer)
    {
        WaitFor(device_, &f.fence, "capture");
        const bool dump = debugDumpName_[0] != 0;
        WriteCapture();
        if ((captureCount_ == 6 || dump) && debugTexture_)
            DumpDebugTexture();
        debugDumpName_[0] = 0;
    }

    if (imageIndex != ~0u)
    {
        VkPresentInfoKHR present = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &presentReady_[imageIndex];
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain_;
        present.pImageIndices = &imageIndex;
        result = vkQueuePresentKHR(device_.queue, &present);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            swapchainDirty_ = true;
    }
    frameActive_ = false;
    rendering_ = false;
    ++frameNumber_;
    {
        // Debug.ShaderLogFrame=N: logs the shader pairs every draw of frame N used.
        static const uint32_t logFrame = static_cast<uint32_t>(nfs::config::Int("Debug", "ShaderLogFrame", 0));
        if (logFrame && frameNumber_ == logFrame)
            SetShaderLog(true);
        else if (logFrame && frameNumber_ == logFrame + 1)
            SetShaderLog(false);
    }
    if (stats_)
    {
        int64_t now;
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&now));
        LogSpike(now);
    }
    // The list of pipelines (a few KB) is written once new ones stop appearing for a moment; the cache only at the end.
    if (pipelineCount_ != savedPipelineCount_ && GetTickCount() - lastPipelineCreated_ > 2000)
    {
        savedPipelineCount_ = pipelineCount_;
        SavePipelineList();
        cacheDirty_ = true;
    }
    frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;
}

void Renderer::DumpDebugTexture()
{
    Texture *t = debugTexture_;
    const bool depth = t->format.depth && (t->format.bytes == 2 || t->format.bytes == 4);
    if (!depth && (t->format.depth || t->format.compressed || t->format.bytes != 4))
        return;
    // Depth aspects copy as 16-bit words, 24-bit values in 32-bit words, or floats.
    const VkFormat format = t->image.desc.format;
    const bool depthFloat = format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D32_SFLOAT_S8_UINT;
    const uint32_t depthBytes = format == VK_FORMAT_D16_UNORM ? 2 : 4;
    const uint32_t layers = t->cube ? 6 : 1, texel = depth ? depthBytes : 4, layerBytes = t->width * t->height * texel;
    Buffer readback;
    if (!CreateBuffer(device_, layerBytes * layers, VK_BUFFER_USAGE_TRANSFER_DST_BIT, kHostMemory, readback, kReadMemory))
        return;
    const VkImageAspectFlags aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    OneShot shot(device_);
    shot.Begin();
    ImageBarrier(shot.cmd, t->image.image, aspect, t->layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                 VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferImageCopy copy = {};
    copy.imageSubresource = {aspect, 0, 0, layers};
    copy.imageExtent = {t->width, t->height, 1};
    vkCmdCopyImageToBuffer(shot.cmd, t->image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy);
    ImageBarrier(shot.cmd, t->image.image, aspect, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t->layout,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                 VK_ACCESS_2_MEMORY_READ_BIT);
    shot.Submit();
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/')
        --n;
    uint32_t *row = static_cast<uint32_t *>(::operator new(t->width * 4));
    for (uint32_t layer = 0; layer < layers; ++layer)
    {
        if (debugDumpName_[0])
            lstrcpyA(path + n, debugDumpName_);
        else if (depth)
            lstrcpyA(path + n, "vkshot_depth.bmp");
        else
            wsprintfA(path + n, "vkshot_debug_%u.bmp", layer);
        HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            continue;
        BITMAPFILEHEADER header = {};
        BITMAPINFOHEADER info = {};
        header.bfType = 0x4D42;
        header.bfOffBits = sizeof(header) + sizeof(info);
        header.bfSize = header.bfOffBits + t->width * t->height * 4;
        info.biSize = sizeof(info);
        info.biWidth = static_cast<LONG>(t->width);
        info.biHeight = -static_cast<LONG>(t->height);
        info.biPlanes = 1;
        info.biBitCount = 32;
        DWORD written;
        WriteFile(file, &header, sizeof(header), &written, nullptr);
        WriteFile(file, &info, sizeof(info), &written, nullptr);
        const uint8_t *base = static_cast<const uint8_t *>(readback.mapped) + layer * layerBytes;
        for (uint32_t y = 0; y < t->height; ++y)
        {
            for (uint32_t x = 0; x < t->width; ++x)
            {
                if (depth)
                {
                    uint32_t g;
                    if (depthBytes == 2)
                        g = reinterpret_cast<const uint16_t *>(base)[y * t->width + x] >> 8;
                    else if (depthFloat)
                    {
                        // 24 bits of the float depth whole in red, green and blue, as for 24-bit depth.
                        const float d = reinterpret_cast<const float *>(base)[y * t->width + x];
                        row[x] = 0xFF000000 | static_cast<uint32_t>((d < 0.0f ? 0.0f : d > 1.0f ? 1.0f : d) * 16777215.0f);
                        continue;
                    }
                    else
                    {
                        // 24-bit depth whole in red, green and blue (tools read it back exactly).
                        row[x] = 0xFF000000 | (reinterpret_cast<const uint32_t *>(base)[y * t->width + x] & 0xFFFFFF);
                        continue;
                    }
                    g = g > 255 ? 255 : g;
                    row[x] = 0xFF000000 | g << 16 | g << 8 | g;
                }
                else
                {
                    const uint32_t c = reinterpret_cast<const uint32_t *>(base)[y * t->width + x];
                    row[x] = (c | 0xFF000000) & (x < t->width / 2 ? 0xFFFFFFFF : 0);
                    if (x >= t->width / 2)
                    {
                        const uint32_t a = reinterpret_cast<const uint32_t *>(base)[y * t->width + x - t->width / 2] >> 24;
                        row[x] = 0xFF000000 | a << 16 | a << 8 | a;
                    }
                }
            }
            WriteFile(file, row, t->width * 4, &written, nullptr);
        }
        CloseHandle(file);
    }
    ::operator delete(row);
    DestroyBuffer(device_, readback);
}

/** Saves the captured back buffer as vkshot_N.bmp next to the exe, scaled down to at most 1920 pixels wide. */
void Renderer::SetSceneConstants(const void *data, uint32_t bytes)
{
    if (!frameActive_)
        BeginFrame();
    if (frameSceneConstants_)
        memcpy(frameSceneConstants_, data, bytes < kSceneConstantsBytes ? bytes : kSceneConstantsBytes);
}

void Renderer::SetSunShadowAtlas(Texture *atlas)
{
    if (sunShadowAtlas_ == atlas)
        return;
    sunShadowAtlas_ = atlas;
    if (frameActive_)
        UpdateSceneShadow();
}

void Renderer::SetHeadlightShadow(Texture *depth)
{
    if (headlightShadow_ == depth)
        return;
    headlightShadow_ = depth;
    if (frameActive_)
        UpdateSceneShadow();
}

void Renderer::SetLampVolumes(Texture *const *volumes)
{
    bool changed = false;
    for (uint32_t i = 0; i < kLampVolumes; ++i)
    {
        Texture *t = volumes ? volumes[i] : nullptr;
        changed |= lampVolumes_[i] != t;
        lampVolumes_[i] = t;
    }
    if (changed && frameActive_)
        UpdateSceneShadow();
}

void Renderer::SetShaderLog(bool on)
{
    if (shaderLog_ && !on)
        for (int i = 0; i < shaderPairCount_; ++i)
            nfs::Log("Vulkan: drew with %s + %s", kShaders[shaderPairs_[i][0]].name, kShaders[shaderPairs_[i][1]].name);
    shaderLog_ = on;
    shaderPairCount_ = 0;
}

void Renderer::NoteShaders(int vertexShader, int pixelShader)
{
    for (int i = 0; i < shaderPairCount_; ++i)
        if (shaderPairs_[i][0] == vertexShader && shaderPairs_[i][1] == pixelShader)
            return;
    if (shaderPairCount_ < 64)
    {
        shaderPairs_[shaderPairCount_][0] = static_cast<int16_t>(vertexShader);
        shaderPairs_[shaderPairCount_][1] = static_cast<int16_t>(pixelShader);
        ++shaderPairCount_;
    }
}

void Renderer::SetWorldMatrix(const float *m)
{
    __m128 r0 = _mm_loadu_ps(m), r1 = _mm_loadu_ps(m + 4), r2 = _mm_loadu_ps(m + 8), r3 = _mm_loadu_ps(m + 12);
    _MM_TRANSPOSE4_PS(r0, r1, r2, r3);
    float *w = world_[0];
    const __m128 w0 = _mm_loadu_ps(w), w1 = _mm_loadu_ps(w + 4), w2 = _mm_loadu_ps(w + 8);
    const int same = _mm_movemask_ps(_mm_cmpeq_ps(r0, w0)) & _mm_movemask_ps(_mm_cmpeq_ps(r1, w1)) &
                     _mm_movemask_ps(_mm_cmpeq_ps(r2, w2));
    if (same == 0xF)
        return;
    _mm_storeu_ps(w, r0);
    _mm_storeu_ps(w + 4, r1);
    _mm_storeu_ps(w + 8, r2);
    worldDirty_ = true;
}

/** Points the recording frame's scene set at the shadow atlas (or a white default) and the lamp volumes (or a black
 * one) if it shows others. The set is the frame's own, which the GPU has finished with once the frame began. */
void Renderer::UpdateSceneShadow()
{
    Frame &f = frames_[frameIndex_];
    Texture *lamps[kLampVolumes];
    bool lampsChanged = !f.sceneLampsWritten;
    for (uint32_t i = 0; i < kLampVolumes; ++i)
    {
        lamps[i] = lampVolumes_[i] ? lampVolumes_[i] : defaultTextures_[2];
        lampsChanged |= f.sceneLamps[i] != lamps[i];
    }
    if (lampsChanged && lamps[0])
    {
        if (!lampSampler_)
        {
            VkSamplerCreateInfo info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            info.magFilter = info.minFilter = VK_FILTER_LINEAR;
            info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            info.maxLod = 0.0f;
            vkCreateSampler(device_.device, &info, nullptr, &lampSampler_);
        }
        VkImageView views[kLampVolumes];
        for (uint32_t i = 0; i < kLampVolumes; ++i)
        {
            views[i] = lamps[i]->image.view;
            f.sceneLamps[i] = lamps[i];
        }
        WriteSceneLamps(device_, f.scene, views, lampSampler_);
        f.sceneLampsWritten = true;
    }
    Texture *shown = sunShadowAtlas_ ? sunShadowAtlas_ : defaultTextures_[0];
    Texture *beam = headlightShadow_ ? headlightShadow_ : defaultTextures_[0];
    if (!shown || !beam)
        return;
    const bool shadowChanged = !f.sceneShadowWritten || f.sceneShadow != shown;
    const bool beamChanged = !f.sceneHeadlightShadowWritten || f.sceneHeadlightShadow != beam;
    if (!shadowChanged && !beamChanged)
        return;
    if (!shadowCompareSampler_)
    {
        VkSamplerCreateInfo info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        info.magFilter = info.minFilter = VK_FILTER_LINEAR;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        info.compareEnable = VK_TRUE;
        info.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        info.maxLod = 0.0f;
        vkCreateSampler(device_.device, &info, nullptr, &shadowCompareSampler_);
        info.magFilter = info.minFilter = VK_FILTER_NEAREST;
        info.compareEnable = VK_FALSE;
        vkCreateSampler(device_.device, &info, nullptr, &shadowPointSampler_);
    }
    if (shadowChanged)
    {
        WriteSceneShadow(device_, f.scene, shown->image.view, shadowCompareSampler_, shadowPointSampler_);
        f.sceneShadow = shown;
        f.sceneShadowWritten = true;
    }
    if (beamChanged)
    {
        WriteSceneHeadlightShadow(device_, f.scene, beam->image.view, shadowCompareSampler_);
        f.sceneHeadlightShadow = beam;
        f.sceneHeadlightShadowWritten = true;
    }
}

void Renderer::WriteCrumb(uint32_t kind, uint32_t a, uint32_t b, uint32_t c)
{
    const uint32_t serial = ++crumbSerial_;
    crumbs_[serial & (kCrumbs - 1)] = {serial, static_cast<uint32_t>(frameNumber_), kind, a, b, c};
    vkCmdWriteBufferMarkerAMD(cmd_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, crumbBuffer_.buffer, 0, serial);
}

void Renderer::LogCrumbMark(const char *what)
{
    if (crumbBuffer_.buffer)
        nfs::Log("Vulkan: %s at frame %u, crumb %u, tick %u", what, static_cast<uint32_t>(frameNumber_), crumbSerial_,
                 GetTickCount());
}

void Renderer::FinishCrumb()
{
    vkCmdWriteBufferMarkerAMD(cmd_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, crumbBuffer_.buffer, 4, crumbSerial_);
}

void Renderer::ReportCrumbs()
{
    if (!crumbBuffer_.buffer || crumbsReported_)
        return;
    crumbsReported_ = true;
    static const char *const kKinds[] = {"?",    "draw", "draw indexed (UP)", "draw indexed", "clear",
                                         "copy", "blit", "barrier",           "begin rendering"};
    const volatile uint32_t *written = static_cast<const volatile uint32_t *>(crumbBuffer_.mapped);
    const uint32_t started = written[0], finished = written[1];
    nfs::Log("Vulkan: GPU crumbs: last started %u, last finished %u, last recorded %u (frame %u)", started, finished,
             crumbSerial_, static_cast<uint32_t>(frameNumber_));
    if (device_.caps.deviceFault && vkGetDeviceFaultInfoEXT)
    {
        VkDeviceFaultCountsEXT counts = {VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT};
        VkResult got = vkGetDeviceFaultInfoEXT(device_.device, &counts, nullptr);
        nfs::Log("Vulkan: device fault: %s, %u addresses, %u vendor records, %u binary bytes", ResultName(got),
                 counts.addressInfoCount, counts.vendorInfoCount, static_cast<uint32_t>(counts.vendorBinarySize));
        VkDeviceFaultAddressInfoEXT addresses[16] = {};
        VkDeviceFaultVendorInfoEXT vendors[16] = {};
        counts.addressInfoCount = counts.addressInfoCount < 16 ? counts.addressInfoCount : 16;
        counts.vendorInfoCount = counts.vendorInfoCount < 16 ? counts.vendorInfoCount : 16;
        counts.vendorBinarySize = 0;
        VkDeviceFaultInfoEXT info = {VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT};
        info.pAddressInfos = addresses;
        info.pVendorInfos = vendors;
        got = vkGetDeviceFaultInfoEXT(device_.device, &counts, &info);
        nfs::Log("Vulkan: device fault (%s): %s", ResultName(got), info.description);
        for (uint32_t i = 0; i < counts.addressInfoCount; ++i)
        {
            nfs::Log("Vulkan: fault address type %d at %08x%08x (precision %u)", addresses[i].addressType,
                     static_cast<uint32_t>(addresses[i].reportedAddress >> 32),
                     static_cast<uint32_t>(addresses[i].reportedAddress), static_cast<uint32_t>(addresses[i].addressPrecision));
            ReportAddressBindings(addresses[i].reportedAddress);
        }
        for (uint32_t i = 0; i < counts.vendorInfoCount; ++i)
            nfs::Log("Vulkan: fault vendor record: %s (code %08x, data %08x%08x)", vendors[i].description,
                     static_cast<uint32_t>(vendors[i].vendorFaultCode), static_cast<uint32_t>(vendors[i].vendorFaultData >> 32),
                     static_cast<uint32_t>(vendors[i].vendorFaultData));
    }
    // Every command remembered, to nfs_crumbs.txt next to the exe.
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    while (n && path[n - 1] != '\\')
        --n;
    lstrcpyA(path + n, "nfs_crumbs.txt");
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        const uint32_t oldest = crumbSerial_ > kCrumbs - 1 ? crumbSerial_ - (kCrumbs - 1) : 1;
        for (uint32_t s = oldest; s <= crumbSerial_; ++s)
        {
            const CrumbInfo &c = crumbs_[s & (kCrumbs - 1)];
            if (c.serial != s)
                continue;
            char line[128];
            const int length = wsprintfA(line, "%u%s %u %u %08x %08x %08x\r\n", s,
                                         s <= finished ? "" : s <= started ? " RUNNING" : " NOTDONE", c.frame, c.kind,
                                         c.a, c.b, c.c);
            DWORD written;
            WriteFile(file, line, static_cast<DWORD>(length), &written, nullptr);
        }
        CloseHandle(file);
    }
    // The start of the frame before the one that hung, then the commands around the hang.
    const uint32_t hungFrame = crumbs_[(finished + 1) & (kCrumbs - 1)].frame;
    uint32_t frameStart = finished;
    while (frameStart > 1 && crumbSerial_ - frameStart < kCrumbs - 1 &&
           crumbs_[(frameStart - 1) & (kCrumbs - 1)].frame + 1 >= hungFrame)
        --frameStart;
    const uint32_t first = finished > 24 ? finished - 24 : 1;
    for (uint32_t s = frameStart; s <= crumbSerial_ && s <= started + 8; ++s)
    {
        if (s >= frameStart + 40 && s < first)
            continue;
        const CrumbInfo &c = crumbs_[s & (kCrumbs - 1)];
        if (c.serial != s)
            continue;
        nfs::Log("Vulkan: crumb %u%s frame %u %s: %08x %08x %08x", s,
                 s <= finished ? " done" : s <= started ? " RUNNING" : "", c.frame, kKinds[c.kind < 9 ? c.kind : 0],
                 c.a, c.b, c.c);
    }
}

void Renderer::RequestCapture(const char *name, const char *debugName)
{
    lstrcpynA(captureName_, name, sizeof(captureName_));
    lstrcpynA(debugDumpName_, debugName ? debugName : "", sizeof(debugDumpName_));
}

void Renderer::WriteCapture()
{
    const uint32_t width = backBuffer_->width, height = backBuffer_->height;
    // At most 1920 pixels wide (every step-th pixel), unless -set Debug.CaptureMaxWidth asks for more.
    static const uint32_t maxWidth = static_cast<uint32_t>(nfs::config::Int("Debug", "CaptureMaxWidth", 1920));
    uint32_t step = 1;
    while (width / step > maxWidth)
        ++step;
    const uint32_t w = width / step, h = height / step;
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/')
        --n;
    if (captureName_[0])
    {
        lstrcpyA(path + n, captureName_);
        captureName_[0] = 0;
    }
    else
    {
        wsprintfA(path + n, "vkshot_%d.bmp", captureCount_++);
    }
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    BITMAPFILEHEADER header = {};
    BITMAPINFOHEADER info = {};
    header.bfType = 0x4D42;
    header.bfOffBits = sizeof(header) + sizeof(info);
    header.bfSize = header.bfOffBits + w * h * 4;
    info.biSize = sizeof(info);
    info.biWidth = static_cast<LONG>(w);
    info.biHeight = -static_cast<LONG>(h);
    info.biPlanes = 1;
    info.biBitCount = 32;
    DWORD written;
    WriteFile(file, &header, sizeof(header), &written, nullptr);
    WriteFile(file, &info, sizeof(info), &written, nullptr);
    uint32_t *row = static_cast<uint32_t *>(::operator new(w * 4));
    const uint32_t *pixels = static_cast<const uint32_t *>(captureBuffer_.mapped);
    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
            row[x] = pixels[(y * step) * width + x * step] | 0xFF000000;
        WriteFile(file, row, w * 4, &written, nullptr);
    }
    ::operator delete(row);
    CloseHandle(file);
}

bool Renderer::ReadPixels(Texture *texture, void *out)
{
    if (!texture || texture->format.bytes != 4 || texture->format.compressed || texture->format.depth)
        return false;
    if (!frameActive_)
        BeginFrame();
    Buffer readback;
    if (!CreateBuffer(device_, texture->width * texture->height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, kHostMemory,
                      readback, kReadMemory))
        return false;
    Transition(texture, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy copy = {};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {texture->width, texture->height, 1};
    vkCmdCopyImageToBuffer(cmd_, texture->image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1,
                           &copy);
    EndFrame();
    WaitFor(device_, nullptr, "readback");
    memcpy(out, readback.mapped, texture->width * texture->height * 4);
    DestroyBuffer(device_, readback);
    return true;
}

void Renderer::RequestReadback(Texture *texture)
{
    if (!texture || texture->format.compressed || texture->format.depth ||
        texture->width * texture->height * texture->format.bytes > kReadbackBytes)
        return;
    if (!frameActive_)
        BeginFrame();
    Frame &f = frames_[frameIndex_];
    if (!f.readback.buffer && !CreateBuffer(device_, kReadbackBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, kHostMemory,
                                            f.readback, kReadMemory))
        return;
    const VkImageLayout layout = texture->layout;
    Transition(texture, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy copy = {};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {texture->width, texture->height, 1};
    vkCmdCopyImageToBuffer(cmd_, texture->image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, f.readback.buffer, 1,
                           &copy);
    if (layout != VK_IMAGE_LAYOUT_UNDEFINED)
        Transition(texture, layout);
    f.readbackPending = true;
}

bool Renderer::ReadbackResult(void *out, uint32_t bytes) const
{
    if (!readbackValid_)
        return false;
    memcpy(out, readback_, bytes < kReadbackBytes ? bytes : kReadbackBytes);
    return true;
}

void *Renderer::AllocateRing(uint32_t bytes, uint32_t alignment, VkBuffer &buffer, VkDeviceSize &offset,
                              uint32_t span)
{
    Frame &f = frames_[frameIndex_];
    uint32_t start = (f.ringUsed + alignment - 1) & ~(alignment - 1);
    if (start + (bytes > span ? bytes : span) > kRingSize)
    {
        nfs::Log("Vulkan: frame ring buffer full");
        return nullptr;
    }
    f.ringUsed = start + bytes;
    buffer = f.ring.buffer;
    offset = start;
    return static_cast<char *>(f.ring.mapped) + start;
}

Texture *Renderer::CreateTexture(uint32_t width, uint32_t height, uint32_t levels, uint32_t d3dFormat,
                                 bool renderTarget, bool cube)
{
    FormatInfo format = TextureFormat(d3dFormat);
    if (format.format == VK_FORMAT_UNDEFINED)
    {
        nfs::Log("Vulkan: texture format %x has no Vulkan equivalent", d3dFormat);
        return nullptr;
    }
    if (format.depth && format.stencil)
        format.format = depthFormat_;
    if (!width)
        width = 1;
    if (!height)
        height = 1;
    if (levels == 0 || levels > FullMipCount(width, height))
        levels = FullMipCount(width, height);
    ImageDesc desc = {};
    desc.viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    desc.format = format.format;
    desc.width = width;
    desc.height = height;
    desc.mipLevels = levels;
    desc.layers = cube ? 6 : 1;
    desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (renderTarget)
        desc.usage |= format.depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    desc.aspect = format.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    desc.swizzle = format.swizzle;
    ++cost_.textures;
    Texture *t = static_cast<Texture *>(::operator new(sizeof(Texture)));
    memset(t, 0, sizeof(Texture));
    t->vtable = kObjectVtable;
    t->refs = 1;
    t->kind = kObjectTexture;
    if (!CreateImage(device_, desc, t->image))
    {
        ::operator delete(t);
        return nullptr;
    }
    if (cube && renderTarget)
    {
        for (uint32_t face = 0; face < 6; ++face)
        {
            VkImageViewCreateInfo view = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = t->image.image;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = format.format;
            view.subresourceRange = {desc.aspect, 0, 1, face, 1};
            vkCreateImageView(device_.device, &view, nullptr, &t->faceViews[face]);
        }
    }
    t->format = format;
    t->d3dFormat = d3dFormat;
    t->width = width;
    t->height = height;
    t->levels = levels;
    t->depth = 1;
    t->cube = cube;
    t->renderTarget = renderTarget;
    if (renderTarget && crumbBuffer_.buffer)
        nfs::Log("Vulkan: render target %08x made, %ux%u format %x, frame %u, memory %08x%08x +%u", reinterpret_cast<uint32_t>(t),
                 width, height, d3dFormat, static_cast<uint32_t>(frameNumber_),
                 static_cast<uint32_t>(t->image.allocation.address >> 32), static_cast<uint32_t>(t->image.allocation.address),
                 static_cast<uint32_t>(t->image.allocation.size));
    t->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    return t;
}

Texture *Renderer::CreateVolumeTexture(uint32_t width, uint32_t height, uint32_t depth, uint32_t levels,
                                       uint32_t d3dFormat)
{
    FormatInfo format = TextureFormat(d3dFormat);
    if (format.format == VK_FORMAT_UNDEFINED || format.depth || format.compressed)
    {
        nfs::Log("Vulkan: volume texture format %x is not supported", d3dFormat);
        return nullptr;
    }
    width = width ? width : 1;
    height = height ? height : 1;
    depth = depth ? depth : 1;
    uint32_t largest = width > height ? width : height;
    largest = largest > depth ? largest : depth;
    uint32_t full = 1;
    while (largest > 1)
    {
        largest >>= 1;
        ++full;
    }
    if (levels == 0 || levels > full)
        levels = full;
    ImageDesc desc = {};
    desc.viewType = VK_IMAGE_VIEW_TYPE_3D;
    desc.format = format.format;
    desc.width = width;
    desc.height = height;
    desc.depth = depth;
    desc.mipLevels = levels;
    desc.layers = 1;
    desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    desc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    desc.swizzle = format.swizzle;
    ++cost_.textures;
    Texture *t = static_cast<Texture *>(::operator new(sizeof(Texture)));
    memset(t, 0, sizeof(Texture));
    t->vtable = kObjectVtable;
    t->refs = 1;
    t->kind = kObjectTexture;
    if (!CreateImage(device_, desc, t->image))
    {
        ::operator delete(t);
        return nullptr;
    }
    t->format = format;
    t->d3dFormat = d3dFormat;
    t->width = width;
    t->height = height;
    t->depth = depth;
    t->levels = levels;
    t->volume = true;
    t->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    return t;
}

void Renderer::UploadTexture(Texture *texture, uint32_t level, uint32_t face, const void *data, uint32_t bytes)
{
    if (!texture || level >= texture->levels)
        return;
    uint32_t w = texture->width >> level, h = texture->height >> level, d = texture->depth >> level;
    w = w ? w : 1;
    h = h ? h : 1;
    d = d ? d : 1;
    // The game's data can be shorter than the level (its small mips of block-compressed and non-square textures); the
    // rest is zero, as in a fresh Direct3D 9 texture.
    const uint32_t size = LevelSize(texture->format, w, h) * d;
    if (bytes > size)
        bytes = size;
    VkDeviceSize offset;
    char *staging = static_cast<char *>(Stage(size, offset));
    if (!staging)
        return;
    memcpy(staging, data, bytes);
    if (bytes < size)
        memset(staging + bytes, 0, size - bytes);
    VkBufferImageCopy copy = {};
    copy.bufferOffset = offset;
    copy.imageSubresource = {texture->image.desc.aspect, level, face, 1};
    copy.imageExtent = {w, h, d};
    CopyStaged(texture, &copy, 1);
}

/** A streamed texture's levels in one go: each level took a staging piece, two barriers and a copy of its own, a few
 * milliseconds for a section with many textures (2026-11-14). */
void Renderer::UploadTextureLevels(Texture *texture, const void *data, uint32_t baseBytes, uint32_t levels)
{
    if (!texture || !levels)
        return;
    const uint32_t kMaxLevels = 16;
    levels = levels < texture->levels ? levels : texture->levels;
    levels = levels < kMaxLevels ? levels : kMaxLevels;
    VkBufferImageCopy copies[kMaxLevels];
    uint32_t sizes[kMaxLevels], given[kMaxLevels];
    uint32_t total = 0;
    for (uint32_t level = 0; level < levels; ++level)
    {
        uint32_t w = texture->width >> level, h = texture->height >> level;
        w = w ? w : 1;
        h = h ? h : 1;
        sizes[level] = LevelSize(texture->format, w, h);
        const uint32_t bytes = baseBytes >> (level * 2);
        given[level] = bytes < sizes[level] ? bytes : sizes[level];
        VkBufferImageCopy &copy = copies[level];
        copy = {};
        copy.bufferOffset = total;
        copy.imageSubresource = {texture->image.desc.aspect, level, 0, 1};
        copy.imageExtent = {w, h, 1};
        total += (sizes[level] + 15) & ~15u;
    }
    if (total > kStagingSize)
    {
        // Too large for one piece: level by level.
        const char *p = static_cast<const char *>(data);
        for (uint32_t level = 0; level < levels; ++level)
        {
            UploadTexture(texture, level, 0, p, given[level]);
            p += baseBytes >> (level * 2);
        }
        return;
    }
    VkDeviceSize offset;
    char *staging = static_cast<char *>(Stage(total, offset));
    if (!staging)
        return;
    const char *p = static_cast<const char *>(data);
    for (uint32_t level = 0; level < levels; ++level)
    {
        char *to = staging + copies[level].bufferOffset;
        memcpy(to, p, given[level]);
        if (given[level] < sizes[level])
            memset(to + given[level], 0, sizes[level] - given[level]);
        p += baseBytes >> (level * 2);
        copies[level].bufferOffset += offset;
    }
    CopyStaged(texture, copies, levels);
}

void Renderer::UploadVolumeBoxes(Texture *texture, const VolumeBox *boxes, uint32_t count)
{
    if (!texture || !texture->volume || !count)
        return;
    const uint32_t kMaxBoxes = 64;
    VkBufferImageCopy copies[kMaxBoxes];
    uint32_t size = 0, used = 0;
    for (uint32_t i = 0; i < count && used < kMaxBoxes; ++i)
    {
        const VolumeBox &b = boxes[i];
        if (b.at[0] + b.extent[0] > texture->width || b.at[1] + b.extent[1] > texture->height ||
            b.at[2] + b.extent[2] > texture->depth)
            continue;
        VkBufferImageCopy &copy = copies[used++];
        copy = {};
        copy.bufferOffset = size;
        copy.imageSubresource = {texture->image.desc.aspect, 0, 0, 1};
        copy.imageOffset = {static_cast<int32_t>(b.at[0]), static_cast<int32_t>(b.at[1]), static_cast<int32_t>(b.at[2])};
        copy.imageExtent = {b.extent[0], b.extent[1], b.extent[2]};
        size += (LevelSize(texture->format, b.extent[0], b.extent[1]) * b.extent[2] + 15) & ~15u;
    }
    VkDeviceSize offset;
    char *staging = static_cast<char *>(Stage(size, offset));
    if (!staging)
        return;
    for (uint32_t i = 0, k = 0; i < count && k < used; ++i)
    {
        const VolumeBox &b = boxes[i];
        if (b.at[0] + b.extent[0] > texture->width || b.at[1] + b.extent[1] > texture->height ||
            b.at[2] + b.extent[2] > texture->depth)
            continue;
        memcpy(staging + copies[k].bufferOffset, b.data, LevelSize(texture->format, b.extent[0], b.extent[1]) * b.extent[2]);
        copies[k++].bufferOffset += offset;
    }
    CopyStaged(texture, copies, used);
}

/** Records the copies of staged texels into one texture between the barriers that keep them from the GPU's other uses
 * of it. */
void Renderer::CopyStaged(Texture *texture, const VkBufferImageCopy *copies, uint32_t count)
{
    Upload &u = *upload_;
    VkImageMemoryBarrier2 barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.oldLayout = texture->layout;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture->image.image;
    VkDependencyInfo dependency = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    // The whole image moves together so its one tracked layout stays true.
    barrier.subresourceRange = {texture->image.desc.aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
    vkCmdPipelineBarrier2(u.cmd, &dependency);
    vkCmdCopyBufferToImage(u.cmd, u.staging.buffer, texture->image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, count,
                           copies);

    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier2(u.cmd, &dependency);
    texture->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void *Renderer::LockLevel(Texture *texture, uint32_t level, bool overwrite)
{
    if (!texture || level >= texture->levels || texture->format.depth)
        return nullptr;
    Lock *lock = nullptr;
    for (Lock &l : locks_)
    {
        if (l.texture == texture && l.level == level)
            return l.buffer.mapped;
        if (!l.texture && !lock)
            lock = &l;
    }
    if (!lock)
    {
        nfs::Log("Vulkan: too many locked textures");
        return nullptr;
    }
    uint32_t w = texture->width >> level, h = texture->height >> level;
    w = w ? w : 1;
    h = h ? h : 1;
    const uint32_t size = LevelSize(texture->format, w, h) * (texture->cube ? 6 : 1);
    if (!CreateBuffer(device_, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, kHostMemory, lock->buffer, kReadMemory))
        return nullptr;
    lock->texture = texture;
    lock->level = level;
    if (overwrite)
        return lock->buffer.mapped;
    memset(lock->buffer.mapped, 0, size);
    if (texture->layout == VK_IMAGE_LAYOUT_UNDEFINED)
        return lock->buffer.mapped;
    // Wait for the texture's data: earlier uploads, then this copy.
    FlushUploads();
    VkDeviceSize offset;
    Stage(0, offset);
    VkCommandBuffer cmd = upload_->cmd;
    const VkImageAspectFlags aspect = texture->image.desc.aspect;
    ImageBarrier(cmd, texture->image.image, aspect, texture->layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                 VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferImageCopy copy = {};
    copy.imageSubresource = {aspect, level, 0, texture->cube ? 6u : 1u};
    copy.imageExtent = {w, h, 1};
    vkCmdCopyImageToBuffer(cmd, texture->image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, lock->buffer.buffer, 1,
                           &copy);
    ImageBarrier(cmd, texture->image.image, aspect, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture->layout,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                 VK_ACCESS_2_MEMORY_READ_BIT);
    FlushUploads();
    WaitFor(device_, &upload_->fence, "LockLevel");
    return lock->buffer.mapped;
}

void Renderer::UploadBoxMips(Texture *texture, const uint8_t *top)
{
    uint32_t w = texture->width, h = texture->height;
    uint8_t *const work = static_cast<uint8_t *>(::operator new(static_cast<size_t>(w) * h * 2));
    const uint8_t *above = top;
    uint8_t *below = work;
    for (uint32_t level = 1; level < texture->levels; ++level)
    {
        const uint32_t nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
        for (uint32_t y = 0; y < nh; ++y)
        {
            const uint32_t y0 = h > 1 ? y * 2 : 0, y1 = h > 1 ? y0 + 1 : 0;
            for (uint32_t x = 0; x < nw; ++x)
            {
                const uint32_t x0 = w > 1 ? x * 2 : 0, x1 = w > 1 ? x0 + 1 : 0;
                for (uint32_t c = 0; c < 4; ++c)
                {
                    const uint32_t sum = above[(y0 * w + x0) * 4 + c] + above[(y0 * w + x1) * 4 + c] +
                                         above[(y1 * w + x0) * 4 + c] + above[(y1 * w + x1) * 4 + c];
                    below[(y * nw + x) * 4 + c] = static_cast<uint8_t>((sum + 2) / 4);
                }
            }
        }
        UploadTexture(texture, level, 0, below, nw * nh * 4);
        above = below;
        below = below == work ? work + static_cast<size_t>(w) * h : work;
        w = nw;
        h = nh;
    }
    ::operator delete(work);
}

void Renderer::UnlockLevel(Texture *texture, uint32_t level)
{
    for (Lock &l : locks_)
    {
        if (l.texture != texture || l.level != level)
            continue;
        uint32_t w = texture->width >> level, h = texture->height >> level;
        const uint32_t size = LevelSize(texture->format, w ? w : 1, h ? h : 1);
        for (uint32_t face = 0; face < (texture->cube ? 6u : 1u); ++face)
            UploadTexture(texture, level, face, static_cast<char *>(l.buffer.mapped) + face * size, size);
        // The game writes only the top level (the rims and skins composited into DUMMY_WHEEL/DUMMY_SKIN): the levels
        // under it would keep the old image, the blue placeholder showing on every painted rim from a few metres off.
        if (level == 0 && texture->levels > 1 && !texture->cube && !texture->format.compressed && texture->format.bytes == 4)
            UploadBoxMips(texture, static_cast<const uint8_t *>(l.buffer.mapped));
        DestroyBuffer(device_, l.buffer);
        l.texture = nullptr;
        return;
    }
}

void Renderer::CopyRect(Texture *source, uint32_t sourceX, uint32_t sourceY, Texture *target, uint32_t targetX,
                        uint32_t targetY, uint32_t width, uint32_t height)
{
    if (!source || !target)
        return;
    if (!frameActive_)
        BeginFrame();
    EndRendering();
    const VkImageLayout sourceLayout = source->layout, targetLayout = target->layout;
    const VkImageAspectFlags aspect = source->image.desc.aspect;
    ImageBarrier(cmd_, source->image.image, aspect, sourceLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                 VK_ACCESS_2_TRANSFER_READ_BIT);
    ImageBarrier(cmd_, target->image.image, aspect, targetLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkImageCopy copy = {};
    copy.srcSubresource = {aspect, 0, 0, 1};
    copy.srcOffset = {static_cast<int32_t>(sourceX), static_cast<int32_t>(sourceY), 0};
    copy.dstSubresource = {aspect, 0, 0, 1};
    copy.dstOffset = {static_cast<int32_t>(targetX), static_cast<int32_t>(targetY), 0};
    copy.extent = {width, height, 1};
    CrumbBegin(5, width << 16 | height, source->width << 16 | source->height, target->width << 16 | target->height);
    vkCmdCopyImage(cmd_, source->image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target->image.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    CrumbEnd();
    const VkImageLayout sourceAfter =
        sourceLayout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : sourceLayout;
    const VkImageLayout targetAfter =
        targetLayout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : targetLayout;
    ImageBarrier(cmd_, source->image.image, aspect, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, sourceAfter,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                 VK_ACCESS_2_MEMORY_READ_BIT);
    ImageBarrier(cmd_, target->image.image, aspect, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, targetAfter,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                 VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    source->layout = sourceAfter;
    target->layout = targetAfter;
}

void Renderer::CopyLevel(Texture *source, uint32_t sourceFace, uint32_t sourceLevel, Texture *target,
                         uint32_t targetFace, uint32_t targetLevel)
{
    if (!source || !target || sourceLevel >= source->levels || targetLevel >= target->levels)
        return;
    if (crumbBuffer_.buffer)
        nfs::Log("Vulkan: CopyLevel %08x %ux%u format %d (level %u face %u) to %08x %ux%u format %d (level %u face %u), "
                 "frame %u",
                 reinterpret_cast<uint32_t>(source), source->width, source->height, source->image.desc.format,
                 sourceLevel, sourceFace, reinterpret_cast<uint32_t>(target), target->width, target->height,
                 target->image.desc.format, targetLevel, targetFace, static_cast<uint32_t>(frameNumber_));
    // Inside a frame the copy goes with the frame's commands; between frames (loading) with the uploads.
    VkCommandBuffer cmd = cmd_;
    if (frameActive_)
    {
        EndRendering();
    }
    else
    {
        VkDeviceSize offset;
        Stage(0, offset);
        cmd = upload_->cmd;
    }
    const VkImageLayout sourceLayout = source->layout, targetLayout = target->layout;
    const VkImageAspectFlags aspect = source->image.desc.aspect;
    ImageBarrier(cmd, source->image.image, aspect, sourceLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                 VK_ACCESS_2_TRANSFER_READ_BIT);
    ImageBarrier(cmd, target->image.image, aspect, targetLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    uint32_t sw = source->width >> sourceLevel, sh = source->height >> sourceLevel;
    uint32_t tw = target->width >> targetLevel, th = target->height >> targetLevel;
    sw = sw ? sw : 1;
    sh = sh ? sh : 1;
    tw = tw ? tw : 1;
    th = th ? th : 1;
    if (source->image.desc.format == target->image.desc.format && sw == tw && sh == th)
    {
        VkImageCopy copy = {};
        copy.srcSubresource = {aspect, sourceLevel, sourceFace, 1};
        copy.dstSubresource = {aspect, targetLevel, targetFace, 1};
        copy.extent = {sw, sh, 1};
        vkCmdCopyImage(cmd, source->image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target->image.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    }
    else if (!target->format.compressed && !source->format.depth && !target->format.depth)
    {
        // Another format or size (the garage's face textures into the environment cube; D3DX converted and scaled):
        // a raw copy between them writes past the target, so the GPU converts and scales instead.
        VkImageBlit blit = {};
        blit.srcSubresource = {aspect, sourceLevel, sourceFace, 1};
        blit.srcOffsets[1] = {static_cast<int32_t>(sw), static_cast<int32_t>(sh), 1};
        blit.dstSubresource = {target->image.desc.aspect, targetLevel, targetFace, 1};
        blit.dstOffsets[1] = {static_cast<int32_t>(tw), static_cast<int32_t>(th), 1};
        vkCmdBlitImage(cmd, source->image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target->image.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
    }
    const VkImageLayout sourceAfter =
        sourceLayout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : sourceLayout;
    const VkImageLayout targetAfter =
        targetLayout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : targetLayout;
    ImageBarrier(cmd, source->image.image, aspect, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, sourceAfter,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                 VK_ACCESS_2_MEMORY_READ_BIT);
    ImageBarrier(cmd, target->image.image, aspect, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, targetAfter,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                 VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    source->layout = sourceAfter;
    target->layout = targetAfter;
}

void *Renderer::Stage(uint32_t bytes, VkDeviceSize &offset)
{
    Upload &u = *upload_;
    if (bytes > kStagingSize)
    {
        nfs::Log("Vulkan: an upload of %d bytes is larger than the staging buffer", bytes);
        return nullptr;
    }
    if (u.recording && ((u.used + 15) & ~15u) + bytes > kStagingSize)
        FlushUploads();
    cost_.uploadBytes += bytes;
    if (!u.recording)
    {
        int64_t start, end;
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&start));
        WaitFor(device_, &u.fence, "Stage");
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&end));
        cost_.uploadWaitTicks += end - start;
        vkResetCommandPool(device_.device, u.pool, 0);
        VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(u.cmd, &begin);
        u.recording = true;
        u.used = 0;
    }
    const uint32_t start = (u.used + 15) & ~15u;
    u.used = start + bytes;
    offset = start;
    return static_cast<char *>(u.staging.mapped) + start;
}

GeometryBuffer *Renderer::CreateDynamicGeometryBuffer(uint32_t bytes, bool index)
{
    if (!bytes)
        return nullptr;
    GeometryBuffer *b = static_cast<GeometryBuffer *>(::operator new(sizeof(GeometryBuffer)));
    memset(b, 0, sizeof(GeometryBuffer));
    b->vtable = kObjectVtable;
    b->refs = 1;
    b->kind = kObjectGeometryBuffer;
    b->bytes = bytes;
    const VkBufferUsageFlags usage =
        (index ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT : VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!CreateBuffer(device_, bytes, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, b->buffer))
    {
        nfs::Log("Vulkan: no memory for a buffer of %d bytes", bytes);
        ::operator delete(b);
        return nullptr;
    }
    b->cpu = ::operator new(bytes);
    memset(b->cpu, 0, bytes);
    return b;
}

void Renderer::UploadGeometryBuffer(GeometryBuffer *b, uint32_t bytes)
{
    if (!b || !b->cpu)
        return;
    if (bytes > b->bytes)
        bytes = b->bytes;
    // Large buffers go up in staging-sized pieces.
    for (uint32_t done = 0; done < bytes;)
    {
        uint32_t piece = bytes - done < kStagingSize ? bytes - done : kStagingSize;
        VkDeviceSize offset;
        void *staging = Stage(piece, offset);
        {
            nfs::streamprofile::Scope profile(nfs::streamprofile::kStagingCopy);
            memcpy(staging, static_cast<const char *>(b->cpu) + done, piece);
        }
        Upload &u = *upload_;
        if (u.copyCount == kMaxGeometryCopies)
            FlushGeometryCopies();
        u.copyTargets[u.copyCount] = BufferOf(b);
        u.copies[u.copyCount++] = {offset, b->arenaOffset + done, piece};
        done += piece;
    }
}

/**
 * Records the geometry copies gathered since the last time: one copy command for each run of copies into the same
 * buffer (an arena's), and one barrier after them all. A streamed section's few hundred solids each recorded a copy
 * and a barrier of their own, about 2 ms of its activation on the frame's thread (2026-11-14).
 */
void Renderer::FlushGeometryCopies()
{
    Upload &u = *upload_;
    if (!u.copyCount)
        return;
    for (uint32_t i = 0; i < u.copyCount;)
    {
        uint32_t n = 1;
        while (i + n < u.copyCount && u.copyTargets[i + n] == u.copyTargets[i])
            ++n;
        vkCmdCopyBuffer(u.cmd, u.staging.buffer, u.copyTargets[i], n, &u.copies[i]);
        i += n;
    }
    u.copyCount = 0;
    VkMemoryBarrier2 barrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.dstAccessMask =
        VK_ACCESS_2_INDEX_READ_BIT | VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT;
    VkDependencyInfo dependency = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(u.cmd, &dependency);
}

bool Renderer::ArenaAllocate(uint32_t stride, uint32_t bytes, uint32_t &arena, uint32_t &offset)
{
    nfs::streamprofile::Scope profile(nfs::streamprofile::kArenaAllocate);
    const uint32_t align = stride ? stride : 16;
    if (!bytes || bytes + align > kArenaBytes)
        return false;
    for (uint32_t pass = 0; pass < 2; ++pass)
    {
        for (uint32_t a = 0; a < arenaCount_; ++a)
        {
            Arena &ar = arenas_[a];
            if (ar.stride != stride)
                continue;
            // From where the last allocation was found: a long-used arena's free list is thousands of small ranges
            // that fit nothing, and searching them from the start for every buffer of a streamed section took
            // 1-3 ms of its activation (2026-11-14). New buffers mostly come from the large range at the end.
            const uint32_t from = ar.rover < ar.freeCount ? ar.rover : 0;
            for (uint32_t k = 0; k < ar.freeCount; ++k)
            {
                const uint32_t i = from + k < ar.freeCount ? from + k : from + k - ar.freeCount;
                const uint32_t start = ar.free[i * 2], end = start + ar.free[i * 2 + 1];
                const uint32_t at = (start + align - 1) / align * align;
                if (at + bytes > end)
                    continue;
                // The range splits into what is left before and after.
                const uint32_t before = at - start, after = end - (at + bytes);
                if (before && after)
                {
                    if (ar.freeCount == kArenaRanges)
                        continue;
                    memmove(&ar.free[(i + 2) * 2], &ar.free[(i + 1) * 2], (ar.freeCount - i - 1) * 8);
                    ++ar.freeCount;
                    ar.free[i * 2 + 1] = before;
                    ar.free[(i + 1) * 2] = at + bytes;
                    ar.free[(i + 1) * 2 + 1] = after;
                }
                else if (before)
                {
                    ar.free[i * 2 + 1] = before;
                }
                else if (after)
                {
                    ar.free[i * 2] = at + bytes;
                    ar.free[i * 2 + 1] = after;
                }
                else
                {
                    memmove(&ar.free[i * 2], &ar.free[(i + 1) * 2], (ar.freeCount - i - 1) * 8);
                    --ar.freeCount;
                }
                ar.rover = i;
                arena = a + 1;
                offset = at;
                return true;
            }
        }
        if (pass || arenaCount_ == kMaxArenas)
            return false;
        Arena &ar = arenas_[arenaCount_];
        const VkBufferUsageFlags usage = (stride ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT : VK_BUFFER_USAGE_INDEX_BUFFER_BIT) |
                                         VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (!CreateBuffer(device_, kArenaBytes, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ar.buffer))
            return false;
        ar.stride = stride;
        ar.free = static_cast<uint32_t *>(::operator new(kArenaRanges * 8));
        ar.free[0] = 0;
        ar.free[1] = kArenaBytes;
        ar.freeCount = 1;
        ++arenaCount_;
    }
    return false;
}

void Renderer::ArenaFree(uint32_t arena, uint32_t offset, uint32_t bytes)
{
    Arena &ar = arenas_[arena - 1];
    uint32_t i = 0;
    while (i < ar.freeCount && ar.free[i * 2] < offset)
        ++i;
    const bool joinBefore = i > 0 && ar.free[(i - 1) * 2] + ar.free[(i - 1) * 2 + 1] == offset;
    const bool joinAfter = i < ar.freeCount && offset + bytes == ar.free[i * 2];
    if (joinBefore && joinAfter)
    {
        ar.free[(i - 1) * 2 + 1] += bytes + ar.free[i * 2 + 1];
        memmove(&ar.free[i * 2], &ar.free[(i + 1) * 2], (ar.freeCount - i - 1) * 8);
        --ar.freeCount;
    }
    else if (joinBefore)
    {
        ar.free[(i - 1) * 2 + 1] += bytes;
    }
    else if (joinAfter)
    {
        ar.free[i * 2] = offset;
        ar.free[i * 2 + 1] += bytes;
    }
    else if (ar.freeCount < kArenaRanges)
    {
        memmove(&ar.free[(i + 1) * 2], &ar.free[i * 2], (ar.freeCount - i) * 8);
        ar.free[i * 2] = offset;
        ar.free[i * 2 + 1] = bytes;
        ++ar.freeCount;
    }
}

void Renderer::MoveToArena(GeometryBuffer *b, uint32_t stride)
{
    b->moveTried = true;
    uint32_t arena, offset;
    if (!ArenaAllocate(stride, b->bytes, arena, offset))
        return;
    VkDeviceSize unused;
    Stage(0, unused);
    // The buffer's own data may still be a gathered copy.
    FlushGeometryCopies();
    VkBufferMemoryBarrier2 before = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    before.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    before.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    before.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    before.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    before.srcQueueFamilyIndex = before.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before.buffer = b->buffer.buffer;
    before.size = VK_WHOLE_SIZE;
    VkDependencyInfo dependency = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &before;
    vkCmdPipelineBarrier2(upload_->cmd, &dependency);
    const VkBufferCopy copy = {0, offset, b->bytes};
    const VkBuffer target = arenas_[arena - 1].buffer.buffer;
    vkCmdCopyBuffer(upload_->cmd, b->buffer.buffer, target, 1, &copy);
    VkBufferMemoryBarrier2 after = before;
    after.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    after.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    after.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    after.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    after.buffer = target;
    after.offset = offset;
    after.size = b->bytes;
    dependency.pBufferMemoryBarriers = &after;
    vkCmdPipelineBarrier2(upload_->cmd, &dependency);
    // The frame's draws so far read the old buffer: it goes once they are done.
    GeometryBuffer *old = static_cast<GeometryBuffer *>(::operator new(sizeof(GeometryBuffer)));
    memset(old, 0, sizeof(GeometryBuffer));
    old->vtable = kObjectVtable;
    old->refs = 0;
    old->kind = kObjectGeometryBuffer;
    old->buffer = b->buffer;
    Retire(old);
    b->buffer = {};
    b->arena = arena;
    b->arenaOffset = offset;
    b->arenaVertex = offset / stride;
}

GeometryBuffer *Renderer::CreateGeometryBuffer(const void *data, uint32_t bytes, bool index)
{
    static const bool arenas = nfs::config::Int("Debug", "GeometryArenas", 1) != 0;
    uint32_t arena, offset;
    if (index && arenas && ArenaAllocate(0, bytes, arena, offset))
    {
        GeometryBuffer *b = static_cast<GeometryBuffer *>(::operator new(sizeof(GeometryBuffer)));
        memset(b, 0, sizeof(GeometryBuffer));
        b->vtable = kObjectVtable;
        b->refs = 1;
        b->kind = kObjectGeometryBuffer;
        b->bytes = bytes;
        b->arena = arena;
        b->arenaOffset = offset;
        b->cpu = const_cast<void *>(data);
        UploadGeometryBuffer(b, bytes);
        b->cpu = nullptr;
        return b;
    }
    GeometryBuffer *b = CreateDynamicGeometryBuffer(bytes, index);
    if (!b)
        return nullptr;
    b->moveTried = !arenas;
    memcpy(b->cpu, data, bytes);
    UploadGeometryBuffer(b, bytes);
    ::operator delete(b->cpu);
    b->cpu = nullptr;
    return b;
}

/**
 * A solid's vertices go straight into the arena of their stride: made the other way, each took a buffer and memory of
 * its own, a copy in system memory and, at its first draw, a copy into the arena and the old buffer's release - about
 * 2 ms of a streamed section's activation on the frame's thread, the most of it (2026-11-14, 460 km/h on the highway).
 */
GeometryBuffer *Renderer::CreateStaticVertexBuffer(const void *data, uint32_t bytes, uint32_t stride)
{
    static const bool arenas = nfs::config::Int("Debug", "GeometryArenas", 1) != 0;
    uint32_t arena, offset;
    if (!arenas || !stride || !ArenaAllocate(stride, bytes, arena, offset))
        return CreateGeometryBuffer(data, bytes, false);
    GeometryBuffer *b = static_cast<GeometryBuffer *>(::operator new(sizeof(GeometryBuffer)));
    memset(b, 0, sizeof(GeometryBuffer));
    b->vtable = kObjectVtable;
    b->refs = 1;
    b->kind = kObjectGeometryBuffer;
    b->bytes = bytes;
    b->arena = arena;
    b->arenaOffset = offset;
    b->arenaVertex = offset / stride;
    b->moveTried = true;
    b->cpu = const_cast<void *>(data);
    UploadGeometryBuffer(b, bytes);
    b->cpu = nullptr;
    return b;
}

void Renderer::ReleaseGeometryBuffer(GeometryBuffer *buffer)
{
    if (!buffer || --buffer->refs)
        return;
    for (GeometryBuffer *&stream : streams_)
        if (stream == buffer)
            stream = nullptr;
    if (indices_ == buffer)
        indices_ = nullptr;
    Retire(buffer);
}

Texture *Renderer::Resolve(void *object, uint32_t *face)
{
    Object *o = static_cast<Object *>(object);
    if (face)
        *face = 0;
    if (!o)
        return nullptr;
    if (o->kind == kObjectSurface)
    {
        if (face)
            *face = static_cast<Surface *>(o)->face;
        return static_cast<Surface *>(o)->texture;
    }
    return o->kind == kObjectTexture ? static_cast<Texture *>(o) : nullptr;
}

void Renderer::StretchRect(void *source, const RECT *sourceRect, void *target, const RECT *targetRect,
                           uint32_t filter)
{
    uint32_t sourceFace, targetFace;
    Texture *src = Resolve(source, &sourceFace), *dst = Resolve(target, &targetFace);
    if (!src || !dst || src == dst)
        return;
    if (!frameActive_)
        BeginFrame();
    EndRendering();
    Transition(src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Transition(dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageBlit blit = {};
    const VkImageAspectFlags aspect = src->image.desc.aspect;
    blit.srcSubresource = {aspect, 0, sourceFace, 1};
    blit.dstSubresource = {aspect, 0, targetFace, 1};
    if (sourceRect)
    {
        blit.srcOffsets[0] = {sourceRect->left, sourceRect->top, 0};
        blit.srcOffsets[1] = {sourceRect->right, sourceRect->bottom, 1};
    }
    else
    {
        blit.srcOffsets[1] = {static_cast<int32_t>(src->width), static_cast<int32_t>(src->height), 1};
    }
    if (targetRect)
    {
        blit.dstOffsets[0] = {targetRect->left, targetRect->top, 0};
        blit.dstOffsets[1] = {targetRect->right, targetRect->bottom, 1};
    }
    else
    {
        blit.dstOffsets[1] = {static_cast<int32_t>(dst->width), static_cast<int32_t>(dst->height), 1};
    }
    const bool scaled = blit.srcOffsets[1].x - blit.srcOffsets[0].x != blit.dstOffsets[1].x - blit.dstOffsets[0].x ||
                        blit.srcOffsets[1].y - blit.srcOffsets[0].y != blit.dstOffsets[1].y - blit.dstOffsets[0].y;
    CrumbBegin(6, src->width << 16 | src->height, dst->width << 16 | dst->height,
               src->d3dFormat << 16 | (dst->d3dFormat & 0xFFFF));
    vkCmdBlitImage(cmd_, src->image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->image.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   scaled && filter != 1 && !src->format.depth ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
    CrumbEnd();
}

void Renderer::FitRenderTarget(void *target, uint32_t width, uint32_t height)
{
    Texture *t = Resolve(target);
    if (!t || !t->renderTarget || t == backBuffer_ || t == depthBuffer_ || !width || !height)
        return;
    if (t->cube)
        height = width;
    if (t->width == width && t->height == height)
        return;
    Texture *fresh = CreateTexture(width, height, t->levels, t->d3dFormat, true, t->cube);
    if (!fresh)
        return;
    if (colorTarget_ == t || depthTarget_ == t)
        EndRendering();
    nfs::Log("Vulkan: render target %ux%u resized to %ux%u", t->width, t->height, width, height);
    Image image = t->image;
    t->image = fresh->image;
    fresh->image = image;
    for (uint32_t face = 0; face < 6; ++face)
    {
        VkImageView view = t->faceViews[face];
        t->faceViews[face] = fresh->faceViews[face];
        fresh->faceViews[face] = view;
    }
    t->width = width;
    t->height = height;
    t->levels = fresh->levels;
    t->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    // A bound target's size went into the recorded scissor.
    ++stateSerial_;
    Retire(fresh);
}

Surface *Renderer::CreateSurface(Texture *texture, uint32_t face)
{
    if (!texture)
        return nullptr;
    Surface *s = static_cast<Surface *>(::operator new(sizeof(Surface)));
    memset(s, 0, sizeof(Surface));
    s->vtable = kObjectVtable;
    s->refs = 1;
    s->kind = kObjectSurface;
    s->texture = texture;
    s->face = face;
    ++texture->refs;
    return s;
}

void Renderer::Release(Object *object)
{
    switch (object->kind)
    {
    case kObjectSurface:
        if (!--object->refs)
        {
            Texture *texture = static_cast<Surface *>(object)->texture;
            ::operator delete(object);
            ReleaseTexture(texture);
        }
        break;
    case kObjectTexture:
        ReleaseTexture(static_cast<Texture *>(object));
        break;
    case kObjectVertexLayout:
        ReleaseVertexLayout(static_cast<VertexLayout *>(object));
        break;
    case kObjectGeometryBuffer:
        ReleaseGeometryBuffer(static_cast<GeometryBuffer *>(object));
        break;
    }
}

void Renderer::FlushUploads()
{
    Upload &u = *upload_;
    if (!u.recording)
        return;
    FlushGeometryCopies();
    vkEndCommandBuffer(u.cmd);
    VkCommandBufferSubmitInfo cmdInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = u.cmd;
    VkSubmitInfo2 submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    vkResetFences(device_.device, 1, &u.fence);
    Check(vkQueueSubmit2(device_.queue, 1, &submit, u.fence), "vkQueueSubmit2 (uploads)");
    u.recording = false;
}

void Renderer::ReleaseTexture(Texture *texture)
{
    if (!texture || --texture->refs)
        return;
    for (uint32_t s = 0; s < kMaxSamplers; ++s)
        if (textures_[s] == texture)
            textures_[s] = nullptr;
    if (colorTarget_ == texture || depthTarget_ == texture)
    {
        EndRendering();
        ++stateSerial_;
        if (colorTarget_ == texture)
            colorTarget_ = nullptr;
        if (depthTarget_ == texture)
            depthTarget_ = nullptr;
    }
    Retire(texture);
}

/** Destroys the object once the frames that may use it (the one being recorded and the one in flight) are done. */
void Renderer::Retire(Object *object)
{
    if (object == backBuffer_ || object == depthBuffer_)
    {
        static int logged;
        if (logged++ < 8)
            nfs::Log("Vulkan: the game released the %s once too often (frame %u); kept",
                     object == backBuffer_ ? "back buffer" : "depth buffer", static_cast<uint32_t>(frameNumber_));
        object->refs = 1;
        return;
    }
    Frame &f = frames_[frameIndex_];
    if (f.garbageCount == kMaxGarbage)
    {
        FlushUploads();
        WaitFor(device_, nullptr, "Retire");
        for (uint32_t i = 0; i < kFramesInFlight; ++i)
            CollectGarbage(frames_[i]);
    }
    f.garbage[f.garbageCount++] = object;
}

void Renderer::DestroyTexture(Texture *texture)
{
    if (crumbBuffer_.buffer)
        nfs::Log("Vulkan: %s %08x destroyed, frame %u (%ux%u, image %08x%08x)",
                 texture->renderTarget ? "render target" : "texture", reinterpret_cast<uint32_t>(texture),
                 static_cast<uint32_t>(frameNumber_), texture->width, texture->height,
                 static_cast<uint32_t>(texture->image.allocation.address >> 32),
                 static_cast<uint32_t>(texture->image.allocation.address));
    for (VkImageView view : texture->faceViews)
        if (view)
            vkDestroyImageView(device_.device, view, nullptr);
    DestroyImage(device_, texture->image);
    texture->kind = kObjectFreed;
    ::operator delete(texture);
}

void Renderer::DestroyObject(Object *object)
{
    if (object->kind == kObjectTexture)
    {
        DestroyTexture(static_cast<Texture *>(object));
        return;
    }
    if (object->kind == kObjectGeometryBuffer)
    {
        GeometryBuffer *b = static_cast<GeometryBuffer *>(object);
        if (b->arena)
            ArenaFree(b->arena, b->arenaOffset, b->bytes);
        DestroyBuffer(device_, b->buffer);
        ::operator delete(b->cpu);
    }
    ::operator delete(object);
}

/** Logs the frame just finished when it took much longer than the running average, with what it spent time on. */
void Renderer::LogSpike(int64_t now)
{
    int64_t frequency;
    QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER *>(&frequency));
    const double ms = 1000.0 / static_cast<double>(frequency);
    const FrameCost cost = cost_;
    cost_ = {};
    const int64_t previous = lastFrameEnd_;
    lastFrameEnd_ = now;
    if (!previous)
        return;
    const double frame = static_cast<double>(now - previous) * ms;
    // Longer frames are loading screens, except in the world (a race, free roam), where they are hitches to find.
    if (frame > 250.0 && TheGameFlowManager_CurrentGameFlowState != 6)
        return;
    const double average = averageFrame_;
    averageFrame_ = average ? average * 0.95 + frame * 0.05 : frame;
    if (!average || frame < average * 2.0 || frame < average + 4.0 || spikesLogged_ >= 400)
        return;
    ++spikesLogged_;
    {
        // The frame's counter window, for tools/samples.py --spikes.
        static HANDLE windows = INVALID_HANDLE_VALUE;
        if (windows == INVALID_HANDLE_VALUE)
        {
            char path[MAX_PATH];
            DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
            while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/')
                --n;
            lstrcpyA(path + n, "nfs_spikes.txt");
            windows = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
        }
        if (windows != INVALID_HANDLE_VALUE)
        {
            char line[96];
            const int length = wsprintfA(line, "%08x%08x %08x%08x %d\r\n", static_cast<DWORD>(previous >> 32),
                                         static_cast<DWORD>(previous), static_cast<DWORD>(now >> 32),
                                         static_cast<DWORD>(now), static_cast<int>(frame));
            DWORD written;
            WriteFile(windows, line, static_cast<DWORD>(length), &written, nullptr);
        }
    }
    const int update = cost.beginTick > previous ? static_cast<int>((cost.beginTick - previous) * ms) : -1;
    const int drawing = cost.beginTick > previous ? static_cast<int>((now - cost.beginTick) * ms) : -1;
    nfs::Log("Vulkan spike: frame %d.%d ms (average %d.%d; update %d ms, drawing %d ms); %u pipelines in %d ms, %u "
             "textures, %u KB uploaded (%d ms waiting), GPU wait %d ms, acquire %d ms",
             static_cast<int>(frame), static_cast<int>(frame * 10) % 10, static_cast<int>(average),
             static_cast<int>(average * 10) % 10, update, drawing, cost.pipelines, static_cast<int>(cost.pipelineTicks * ms),
             cost.textures, cost.uploadBytes >> 10, static_cast<int>(cost.uploadWaitTicks * ms),
             static_cast<int>(cost.fenceTicks * ms), static_cast<int>(cost.acquireTicks * ms));
}

namespace
{
void PipelineCachePath(char *path)
{
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/')
        --n;
    lstrcpyA(path + n, "vkpipelines.bin");
}
} // namespace

void Renderer::LoadPipelineCache(VkPipelineCacheCreateInfo &info, void *&data)
{
    char path[MAX_PATH];
    PipelineCachePath(path);
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    const DWORD size = GetFileSize(file, nullptr);
    DWORD read = 0;
    if (size != INVALID_FILE_SIZE && size > 32 && size < (256u << 20))
    {
        data = ::operator new(size);
        if (ReadFile(file, data, size, &read, nullptr) && read == size)
        {
            info.initialDataSize = size;
            info.pInitialData = data;
            nfs::Log("Vulkan: pipeline cache loaded (%u KB)", size >> 10);
        }
    }
    CloseHandle(file);
}

namespace
{
/** Writes the pipeline cache to vkpipelines.bin. The driver hashes the whole cache (100 MB and more) when asked for its
 * data, about a third of a second for each of the two calls, and holds the cache's lock meanwhile: a pipeline made
 * while a writer thread did this in a race waited for it (a 1.1 s freeze in the bench drive, 2026-11-13; a 325 ms one
 * when it ran on the frame's thread). So it is written only when the game quits (FlushPipelineCache). */
void WritePipelineCache(VkDevice device, VkPipelineCache cache)
{
    size_t size = 0;
    if (vkGetPipelineCacheData(device, cache, &size, nullptr) != VK_SUCCESS || !size)
        return;
    void *data = ::operator new(size);
    if (vkGetPipelineCacheData(device, cache, &size, data) == VK_SUCCESS)
    {
        char path[MAX_PATH], temp[MAX_PATH];
        PipelineCachePath(path);
        lstrcpyA(temp, path);
        lstrcatA(temp, ".tmp");
        HANDLE file = CreateFileA(temp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            const BOOL ok = WriteFile(file, data, static_cast<DWORD>(size), &written, nullptr) && written == size;
            CloseHandle(file);
            if (ok)
                MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING);
        }
    }
    ::operator delete(data);
}
} // namespace

void Renderer::FlushPipelineCache()
{
    if (!device_.device)
        return;
    if (pipelineCount_ != savedPipelineCount_)
    {
        savedPipelineCount_ = pipelineCount_;
        SavePipelineList();
        cacheDirty_ = true;
    }
    if (!cacheDirty_ || !pipelineCache_)
        return;
    cacheDirty_ = false;
    int64_t start, end, frequency;
    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&start));
    WritePipelineCache(device_.device, pipelineCache_);
    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&end));
    QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER *>(&frequency));
    nfs::Log("Vulkan: pipeline cache saved in %d ms",
             static_cast<int>(static_cast<double>(end - start) * 1000.0 / static_cast<double>(frequency)));
}

void Renderer::CollectGarbage(Frame &frame)
{
    if (!frame.garbageCount)
        return;
    // Uploads into the objects may still be queued or running.
    FlushUploads();
    WaitFor(device_, &upload_->fence, "CollectGarbage");
    for (uint32_t i = 0; i < frame.garbageCount; ++i)
        DestroyObject(frame.garbage[i]);
    frame.garbageCount = 0;
}
} // namespace nfs::vk
