#include "core/config.h"
#include "core/log.h"
#include "engine/rendering/vulkan/device.h"
#include "engine/rendering/vulkan/renderer.h"
#include "engine/rendering/vulkan/resources.h"
#include "core/sdl.h"

namespace nfs::vk
{
namespace
{
const char kValidationLayer[] = "VK_LAYER_KHRONOS_validation";

/** -gpucrumbs: the last address bindings the driver reported. */
struct Binding
{
    uint64_t base;
    uint64_t size;
    uint64_t handle;
    uint32_t objectType;
    uint32_t flags;
    uint32_t type;
    uint32_t tick;
};
const uint32_t kBindings = 65536;
Binding *g_bindings;
volatile LONG g_bindingCount;

VKAPI_ATTR VkBool32 VKAPI_CALL BindingReport(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT type,
                                             const VkDebugUtilsMessengerCallbackDataEXT *data, void *)
{
    if (!(type & VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT) || !g_bindings)
        return VK_FALSE;
    for (const VkBaseInStructure *s = static_cast<const VkBaseInStructure *>(data->pNext); s; s = s->pNext)
    {
        if (s->sType != VK_STRUCTURE_TYPE_DEVICE_ADDRESS_BINDING_CALLBACK_DATA_EXT)
            continue;
        const VkDeviceAddressBindingCallbackDataEXT *b =
            reinterpret_cast<const VkDeviceAddressBindingCallbackDataEXT *>(s);
        const uint32_t i = static_cast<uint32_t>(InterlockedIncrement(&g_bindingCount) - 1) & (kBindings - 1);
        Binding &out = g_bindings[i];
        out.base = b->baseAddress;
        out.size = b->size;
        out.flags = b->flags;
        out.type = b->bindingType;
        out.objectType = data->objectCount ? data->pObjects[0].objectType : 0;
        out.handle = data->objectCount ? data->pObjects[0].objectHandle : 0;
        out.tick = GetTickCount();
    }
    return VK_FALSE;
}

struct ExtensionList
{
    VkExtensionProperties *items;
    uint32_t count;

    bool Has(const char *name) const
    {
        for (uint32_t i = 0; i < count; ++i)
            if (lstrcmpA(items[i].extensionName, name) == 0)
                return true;
        return false;
    }

    ~ExtensionList()
    {
        ::operator delete(items);
    }
};

void InstanceExtensions(ExtensionList &list)
{
    list.count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &list.count, nullptr);
    list.items = static_cast<VkExtensionProperties *>(::operator new(sizeof(VkExtensionProperties) * (list.count + 1)));
    vkEnumerateInstanceExtensionProperties(nullptr, &list.count, list.items);
}

void DeviceExtensions(VkPhysicalDevice physical, ExtensionList &list)
{
    list.count = 0;
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &list.count, nullptr);
    list.items = static_cast<VkExtensionProperties *>(::operator new(sizeof(VkExtensionProperties) * (list.count + 1)));
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &list.count, list.items);
}

bool HasLayer(const char *name)
{
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    VkLayerProperties *layers = static_cast<VkLayerProperties *>(::operator new(sizeof(VkLayerProperties) * (count + 1)));
    vkEnumerateInstanceLayerProperties(&count, layers);
    bool found = false;
    for (uint32_t i = 0; i < count && !found; ++i)
        found = lstrcmpA(layers[i].layerName, name) == 0;
    ::operator delete(layers);
    return found;
}

/** The first queue family with graphics (and presentation when asked for), or ~0u. */
uint32_t GraphicsQueueFamily(VkInstance instance, VkPhysicalDevice physical, bool present)
{
    VkQueueFamilyProperties families[16];
    uint32_t count = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families);
    for (uint32_t i = 0; i < count; ++i)
    {
        if (!(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            continue;
        if (present && !SDL_Vulkan_GetPresentationSupport(instance, physical, i))
            continue;
        return i;
    }
    return ~0u;
}

int TypeRank(VkPhysicalDeviceType type)
{
    switch (type)
    {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return 3;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return 2;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return 1;
    default:
        return 0;
    }
}

const char *TypeName(VkPhysicalDeviceType type)
{
    switch (type)
    {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return "cpu";
    default:
        return "other";
    }
}

bool CreateInstance(Device &out, const DeviceOptions &options)
{
    uint32_t loaderVersion = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion)
        vkEnumerateInstanceVersion(&loaderVersion);
    // A Vulkan 1.0 loader takes no other version; later ones take any. -vk10: as on a 1.0 card, for testing.
    out.instanceVersion = loaderVersion >= VK_API_VERSION_1_1 && !HasArgument("-vk10") ? VK_API_VERSION_1_3
                                                                                        : VK_API_VERSION_1_0;

    ExtensionList available;
    InstanceExtensions(available);
    const char *extensions[8];
    uint32_t extensionCount = 0;
    const bool reports = HasArgument("-gpucrumbs") && available.Has(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (reports)
        extensions[extensionCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    if (options.present)
    {
        // What SDL needs to make the window's surface (core/sdl.h).
        Uint32 count = 0;
        const char *const *needed = nfs::sdl::VulkanReady() ? SDL_Vulkan_GetInstanceExtensions(&count) : nullptr;
        if (!needed)
        {
            nfs::Log("Vulkan: SDL cannot present to windows (%s)", SDL_GetError());
            return false;
        }
        for (Uint32 i = 0; i < count && extensionCount < 8; ++i)
        {
            if (!available.Has(needed[i]))
            {
                nfs::Log("Vulkan: the loader cannot present to windows (no %s)", needed[i]);
                return false;
            }
            extensions[extensionCount++] = needed[i];
        }
    }
    const char *layers[1];
    uint32_t layerCount = 0;
    if (options.validation)
    {
        if (HasLayer(kValidationLayer))
            layers[layerCount++] = kValidationLayer;
        else
            nfs::Log("Vulkan: validation was asked for but %s is not installed", kValidationLayer);
    }

    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Need for Speed Most Wanted";
    app.applicationVersion = VK_MAKE_API_VERSION(0, 1, 3, 0);
    app.pEngineName = "Ecstasy";
    app.engineVersion = VK_MAKE_API_VERSION(0, 1, 3, 0);
    app.apiVersion = out.instanceVersion;

    VkInstanceCreateInfo info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = extensionCount;
    info.ppEnabledExtensionNames = extensions;
    info.enabledLayerCount = layerCount;
    info.ppEnabledLayerNames = layers;
    VkResult result = vkCreateInstance(&info, nullptr, &out.instance);
    if (result != VK_SUCCESS)
    {
        nfs::Log("Vulkan: vkCreateInstance failed (%s)", ResultName(result));
        return false;
    }
    LoadInstanceFunctions(out.instance);
    if (reports && vkCreateDebugUtilsMessengerEXT)
    {
        g_bindings = static_cast<Binding *>(::operator new(sizeof(Binding) * kBindings));
        memset(g_bindings, 0, sizeof(Binding) * kBindings);
        VkDebugUtilsMessengerCreateInfoEXT messenger = {VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        messenger.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT;
        messenger.pfnUserCallback = BindingReport;
        VkDebugUtilsMessengerEXT handle = VK_NULL_HANDLE;
        vkCreateDebugUtilsMessengerEXT(out.instance, &messenger, nullptr, &handle);
    }
    return true;
}

bool PickPhysicalDevice(Device &out, const DeviceOptions &options)
{
    VkPhysicalDevice devices[16];
    uint32_t count = 16;
    vkEnumeratePhysicalDevices(out.instance, &count, devices);
    int bestRank = -1;
    for (uint32_t i = 0; i < count; ++i)
    {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(devices[i], &properties);
        uint32_t family = GraphicsQueueFamily(out.instance, devices[i], options.present);
        bool usable = family != ~0u;
        nfs::Log("Vulkan: GPU %d: %s (%s, Vulkan %d.%d.%d, driver %x)%s", i, properties.deviceName,
                 TypeName(properties.deviceType), VK_API_VERSION_MAJOR(properties.apiVersion),
                 VK_API_VERSION_MINOR(properties.apiVersion), VK_API_VERSION_PATCH(properties.apiVersion),
                 properties.driverVersion, usable ? "" : " - not usable");
        if (!usable)
            continue;
        int rank = TypeRank(properties.deviceType);
        if (rank > bestRank)
        {
            bestRank = rank;
            out.physical = devices[i];
            out.properties = properties;
            out.queueFamily = family;
        }
    }
    if (bestRank < 0)
    {
        nfs::Log("Vulkan: no GPU with Vulkan%s", options.present ? " that can present to windows" : "");
        return false;
    }
    out.apiVersion = out.properties.apiVersion;
    vkGetPhysicalDeviceMemoryProperties(out.physical, &out.memory);
    return true;
}

/** The device on a card, driver or loader older than Vulkan 1.3: the calls of Vulkan 1.0, and source/engine/rendering/vulkan/Compat.cpp for
 * the rest. */
bool CreateCompatDevice(Device &out, const DeviceOptions &options)
{
    VkPhysicalDeviceFeatures supported = {};
    vkGetPhysicalDeviceFeatures(out.physical, &supported);
    if (!supported.textureCompressionBC)
    {
        nfs::Log("Vulkan: %s cannot sample DXT (BC) textures", out.properties.deviceName);
        return false;
    }
    ExtensionList available;
    DeviceExtensions(out.physical, available);
    const char *extensions[4];
    uint32_t extensionCount = 0;
    out.caps = {};
    out.caps.compat = true;
    out.caps.swapchain = options.present && available.Has(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    if (options.present)
    {
        if (!out.caps.swapchain)
        {
            nfs::Log("Vulkan: %s has no VK_KHR_swapchain", out.properties.deviceName);
            return false;
        }
        extensions[extensionCount++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    }
    // The renderer draws with y up as Direct3D has it (a viewport of negative height): Vulkan 1.1, or this extension.
    const bool version11 = out.instanceVersion >= VK_API_VERSION_1_1 && out.properties.apiVersion >= VK_API_VERSION_1_1;
    if (!version11)
    {
        if (!available.Has(VK_KHR_MAINTENANCE_1_EXTENSION_NAME))
        {
            nfs::Log("Vulkan: %s has neither Vulkan 1.1 nor VK_KHR_maintenance1", out.properties.deviceName);
            return false;
        }
        extensions[extensionCount++] = VK_KHR_MAINTENANCE_1_EXTENSION_NAME;
    }
    out.caps.pushDescriptor = version11 && available.Has(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    if (out.caps.pushDescriptor)
        extensions[extensionCount++] = VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME;

    VkPhysicalDeviceFeatures enabled = {};
    enabled.textureCompressionBC = VK_TRUE;
    enabled.samplerAnisotropy = supported.samplerAnisotropy;
    enabled.shaderClipDistance = supported.shaderClipDistance;
    enabled.fillModeNonSolid = supported.fillModeNonSolid;
    enabled.depthBiasClamp = supported.depthBiasClamp;
    out.caps.anisotropy = supported.samplerAnisotropy != VK_FALSE;
    out.caps.clipDistance = supported.shaderClipDistance != VK_FALSE;
    out.caps.maxAnisotropy = out.caps.anisotropy ? out.properties.limits.maxSamplerAnisotropy : 1.0f;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = out.queueFamily;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    VkDeviceCreateInfo info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue;
    info.enabledExtensionCount = extensionCount;
    info.ppEnabledExtensionNames = extensions;
    info.pEnabledFeatures = &enabled;
    VkResult result = vkCreateDevice(out.physical, &info, nullptr, &out.device);
    if (result != VK_SUCCESS)
    {
        nfs::Log("Vulkan: vkCreateDevice failed (%s)", ResultName(result));
        return false;
    }
    LoadDeviceFunctions(out.device);
    compat::Install(out, !out.caps.pushDescriptor);
    callprofile::Install();
    stream::Install();
    vkGetDeviceQueue(out.device, out.queueFamily, 0, &out.queue);
    return true;
}

bool CreateLogicalDevice(Device &out, const DeviceOptions &options)
{
    if (out.instanceVersion < VK_API_VERSION_1_3 || out.properties.apiVersion < VK_API_VERSION_1_3)
        return CreateCompatDevice(out, options);
    VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT supportedLibrary = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT};
    VkPhysicalDeviceVulkan13Features supported13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features supported12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                                                    &supported13};
    VkPhysicalDeviceFeatures2 supported = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &supported12};
    vkGetPhysicalDeviceFeatures2(out.physical, &supported);
    if (!supported13.dynamicRendering || !supported13.synchronization2)
        return CreateCompatDevice(out, options);
    if (!supported.features.textureCompressionBC)
    {
        nfs::Log("Vulkan: %s cannot sample DXT (BC) textures", out.properties.deviceName);
        return false;
    }

    ExtensionList available;
    DeviceExtensions(out.physical, available);
    const bool library = available.Has(VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME) &&
                         available.Has(VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME);
    if (library)
    {
        supported13.pNext = &supportedLibrary;
        vkGetPhysicalDeviceFeatures2(out.physical, &supported);
    }
    VkPhysicalDeviceRayQueryFeaturesKHR supportedRayQuery = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR supportedStructures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &supportedRayQuery};
    const bool rayExtensions = available.Has(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
                               available.Has(VK_KHR_RAY_QUERY_EXTENSION_NAME) &&
                               available.Has(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    if (rayExtensions)
    {
        VkPhysicalDeviceFeatures2 query = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &supportedStructures};
        vkGetPhysicalDeviceFeatures2(out.physical, &query);
    }
    char rayTracing[8];
    nfs::config::String("Display", "RayTracing", "on", rayTracing, sizeof(rayTracing));
    const bool rayWanted = lstrcmpA(rayTracing, "off") != 0 && !HasArgument("-noraytracing");
    const bool raySupported = rayExtensions && supportedStructures.accelerationStructure &&
                              supportedRayQuery.rayQuery && supported12.bufferDeviceAddress;
    out.caps.rayQuery = rayWanted && raySupported;
    nfs::Log("Vulkan: ray tracing support: acceleration structure ext %d, ray query ext %d, deferred host ops ext %d, "
             "accelerationStructure %d, rayQuery %d, bufferDeviceAddress %d",
             available.Has(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME), available.Has(VK_KHR_RAY_QUERY_EXTENSION_NAME),
             available.Has(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME), supportedStructures.accelerationStructure,
             supportedRayQuery.rayQuery, supported12.bufferDeviceAddress);
    nfs::Log("Vulkan: ray queries %s", out.caps.rayQuery ? "on" : raySupported ? "off (RayTracing=off)"
                                                                                : "not supported by this driver");

    const char *extensions[12];
    uint32_t extensionCount = 0;
    out.caps.swapchain = available.Has(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    if (options.present)
    {
        if (!out.caps.swapchain)
        {
            nfs::Log("Vulkan: %s has no VK_KHR_swapchain", out.properties.deviceName);
            return false;
        }
        extensions[extensionCount++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    }
    else
    {
        out.caps.swapchain = false;
    }
    // Off unless asked for (-vklibraries): the first try hung the GPU (driver timeout) on the RX 9070 XT.
    out.caps.pipelineLibrary = library && supportedLibrary.graphicsPipelineLibrary && HasArgument("-vklibraries");
    if (out.caps.pipelineLibrary)
    {
        extensions[extensionCount++] = VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME;
        extensions[extensionCount++] = VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME;
    }
    out.caps.pushDescriptor = available.Has(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    if (out.caps.pushDescriptor)
        extensions[extensionCount++] = VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME;
    out.caps.bufferMarker = available.Has(VK_AMD_BUFFER_MARKER_EXTENSION_NAME) && HasArgument("-gpucrumbs");
    if (out.caps.bufferMarker)
        extensions[extensionCount++] = VK_AMD_BUFFER_MARKER_EXTENSION_NAME;
    // The markers go to memory the GPU does not cache, so a hang cannot keep the last ones in its caches.
    out.caps.uncachedMemory = out.caps.bufferMarker && available.Has(VK_AMD_DEVICE_COHERENT_MEMORY_EXTENSION_NAME);
    if (out.caps.uncachedMemory)
        extensions[extensionCount++] = VK_AMD_DEVICE_COHERENT_MEMORY_EXTENSION_NAME;
    out.caps.deviceFault = out.caps.bufferMarker && available.Has(VK_EXT_DEVICE_FAULT_EXTENSION_NAME);
    if (out.caps.deviceFault)
        extensions[extensionCount++] = VK_EXT_DEVICE_FAULT_EXTENSION_NAME;
    out.caps.bindingReport =
        out.caps.bufferMarker && g_bindings && available.Has(VK_EXT_DEVICE_ADDRESS_BINDING_REPORT_EXTENSION_NAME);
    if (out.caps.bindingReport)
        extensions[extensionCount++] = VK_EXT_DEVICE_ADDRESS_BINDING_REPORT_EXTENSION_NAME;
    if (out.caps.bufferMarker)
        nfs::Log("Vulkan: crumbs extensions: coherent memory %d, device fault %d, binding report %d",
                 out.caps.uncachedMemory, out.caps.deviceFault, out.caps.bindingReport);
    if (out.caps.rayQuery)
    {
        extensions[extensionCount++] = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
        extensions[extensionCount++] = VK_KHR_RAY_QUERY_EXTENSION_NAME;
        extensions[extensionCount++] = VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME;
    }

    VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT enabledLibrary = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT};
    enabledLibrary.graphicsPipelineLibrary = VK_TRUE;
    VkPhysicalDeviceRayQueryFeaturesKHR enabledRayQuery = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    enabledRayQuery.rayQuery = VK_TRUE;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR enabledStructures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &enabledRayQuery};
    enabledStructures.accelerationStructure = VK_TRUE;
    VkPhysicalDeviceVulkan13Features enabled13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    if (out.caps.pipelineLibrary)
    {
        enabled13.pNext = &enabledLibrary;
        if (out.caps.rayQuery)
            enabledLibrary.pNext = &enabledStructures;
    }
    else if (out.caps.rayQuery)
    {
        enabled13.pNext = &enabledStructures;
    }
    enabled13.dynamicRendering = VK_TRUE;
    enabled13.synchronization2 = VK_TRUE;
    enabled13.maintenance4 = supported13.maintenance4;
    VkPhysicalDeviceVulkan12Features enabled12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &enabled13};
    enabled12.timelineSemaphore = supported12.timelineSemaphore;
    enabled12.bufferDeviceAddress = out.caps.rayQuery ? VK_TRUE : VK_FALSE;
    out.caps.addressLog = out.caps.bufferMarker && supported12.bufferDeviceAddress &&
                          supported12.bufferDeviceAddressCaptureReplay;
    if (out.caps.addressLog)
    {
        enabled12.bufferDeviceAddress = VK_TRUE;
        enabled12.bufferDeviceAddressCaptureReplay = VK_TRUE;
    }
    VkPhysicalDeviceAddressBindingReportFeaturesEXT enabledReport = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ADDRESS_BINDING_REPORT_FEATURES_EXT, &enabled12};
    enabledReport.reportAddressBinding = VK_TRUE;
    VkPhysicalDeviceFaultFeaturesEXT enabledFault = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT,
                                                     out.caps.bindingReport ? static_cast<void *>(&enabledReport)
                                                                            : &enabled12};
    enabledFault.deviceFault = VK_TRUE;
    void *afterCoherent = out.caps.deviceFault       ? static_cast<void *>(&enabledFault)
                          : out.caps.bindingReport ? static_cast<void *>(&enabledReport)
                                                   : &enabled12;
    VkPhysicalDeviceCoherentMemoryFeaturesAMD enabledCoherent = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COHERENT_MEMORY_FEATURES_AMD, afterCoherent};
    enabledCoherent.deviceCoherentMemory = VK_TRUE;
    VkPhysicalDeviceFeatures2 enabled = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                         out.caps.uncachedMemory ? static_cast<void *>(&enabledCoherent)
                                                                 : afterCoherent};
    enabled.features.textureCompressionBC = VK_TRUE;
    enabled.features.samplerAnisotropy = supported.features.samplerAnisotropy;
    enabled.features.shaderClipDistance = supported.features.shaderClipDistance;
    enabled.features.fillModeNonSolid = supported.features.fillModeNonSolid;
    enabled.features.depthBiasClamp = supported.features.depthBiasClamp;
    out.caps.anisotropy = supported.features.samplerAnisotropy != VK_FALSE;
    out.caps.clipDistance = supported.features.shaderClipDistance != VK_FALSE;
    out.caps.timelineSemaphore = supported12.timelineSemaphore != VK_FALSE;
    out.caps.maxAnisotropy = out.caps.anisotropy ? out.properties.limits.maxSamplerAnisotropy : 1.0f;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = out.queueFamily;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;

    VkDeviceCreateInfo info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &enabled};
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue;
    info.enabledExtensionCount = extensionCount;
    info.ppEnabledExtensionNames = extensions;
    VkResult result = vkCreateDevice(out.physical, &info, nullptr, &out.device);
    if (result != VK_SUCCESS)
    {
        nfs::Log("Vulkan: vkCreateDevice failed (%s)", ResultName(result));
        return false;
    }
    LoadDeviceFunctions(out.device);
    callprofile::Install();
    stream::Install();
    vkGetDeviceQueue(out.device, out.queueFamily, 0, &out.queue);
    return true;
}
} // namespace

bool CreateDevice(Device &out, const DeviceOptions &options)
{
    out = {};
    if (!LoadLoader())
    {
        nfs::Log("Vulkan: vulkan-1.dll is not installed");
        return false;
    }
    if (!CreateInstance(out, options) || !PickPhysicalDevice(out, options) || !CreateLogicalDevice(out, options) ||
        !CreateAllocator(out))
    {
        DestroyDevice(out);
        return false;
    }
    nfs::Log("Vulkan: using %s, queue family %d, push descriptors %s, max anisotropy %d, %d memory allocations",
             out.properties.deviceName, out.queueFamily, out.caps.pushDescriptor ? "yes" : "no",
             static_cast<int>(out.caps.maxAnisotropy), out.properties.limits.maxMemoryAllocationCount);
    return true;
}

void ReportAddressBindings(uint64_t address)
{
    if (!g_bindings)
        return;
    const uint32_t count = static_cast<uint32_t>(g_bindingCount);
    const uint32_t oldest = count > kBindings ? count - kBindings : 0;
    int shown = 0;
    for (uint32_t n = count; n > oldest && shown < 24; --n)
    {
        const Binding &b = g_bindings[(n - 1) & (kBindings - 1)];
        if (address + 0x200000 < b.base || address >= b.base + b.size + 0x200000)
            continue;
        ++shown;
        nfs::Log("Vulkan: binding %u: %s %08x%08x-%08x%08x%s, object type %u handle %08x%08x, tick %u", n - 1,
                 b.type == VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT ? "bind" : "unbind",
                 static_cast<uint32_t>(b.base >> 32), static_cast<uint32_t>(b.base),
                 static_cast<uint32_t>((b.base + b.size) >> 32), static_cast<uint32_t>(b.base + b.size),
                 (b.flags & VK_DEVICE_ADDRESS_BINDING_INTERNAL_OBJECT_BIT_EXT) ? " (internal)" : "", b.objectType,
                 static_cast<uint32_t>(b.handle >> 32), static_cast<uint32_t>(b.handle), b.tick);
    }
    nfs::Log("Vulkan: %d bindings near %08x%08x (of %u reported)", shown, static_cast<uint32_t>(address >> 32),
             static_cast<uint32_t>(address), count);
    // All of them, to nfs_bindings.txt next to the exe.
    char path[MAX_PATH];
    DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    while (length && path[length - 1] != '\\')
        --length;
    lstrcpyA(path + length, "nfs_bindings.txt");
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    for (uint32_t n = oldest; n < count; ++n)
    {
        const Binding &b = g_bindings[n & (kBindings - 1)];
        char line[160];
        const int written = wsprintfA(line, "%u %s %08x%08x %08x%08x %u %u %08x%08x %u\r\n", n,
                                      b.type == VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT ? "bind" : "unbind",
                                      static_cast<uint32_t>(b.base >> 32), static_cast<uint32_t>(b.base),
                                      static_cast<uint32_t>(b.size >> 32), static_cast<uint32_t>(b.size), b.flags,
                                      b.objectType, static_cast<uint32_t>(b.handle >> 32),
                                      static_cast<uint32_t>(b.handle), b.tick);
        DWORD done;
        WriteFile(file, line, static_cast<DWORD>(written), &done, nullptr);
    }
    CloseHandle(file);
}

void DestroyDevice(Device &device)
{
    if (device.device)
    {
        vkDeviceWaitIdle(device.device);
        DestroyAllocator(device);
        vkDestroyDevice(device.device, nullptr);
    }
    if (device.instance)
        vkDestroyInstance(device.instance, nullptr);
    device = {};
}

uint32_t FindMemoryType(const Device &device, uint32_t typeBits, VkMemoryPropertyFlags required,
                        VkMemoryPropertyFlags preferred)
{
    uint32_t fallback = ~0u;
    for (uint32_t i = 0; i < device.memory.memoryTypeCount; ++i)
    {
        if (!(typeBits & (1u << i)))
            continue;
        VkMemoryPropertyFlags flags = device.memory.memoryTypes[i].propertyFlags;
        if ((flags & required) != required)
            continue;
        if ((flags & preferred) == preferred)
            return i;
        if (fallback == ~0u)
            fallback = i;
    }
    return fallback;
}

const char *ResultName(VkResult result)
{
    switch (result)
    {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_INCOMPLETE:
        return "VK_INCOMPLETE";
    case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:
        return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
        return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
    default:
        return "VkResult error";
    }
}
} // namespace nfs::vk
