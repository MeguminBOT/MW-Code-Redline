/**
 * Vulkan 1.0 (engine/rendering/vulkan/device.h compat, docs/VULKAN.md "Vulkan 1.0 cards"). The renderer is written against Vulkan 1.3:
 * dynamic rendering, synchronization2, the extended dynamic state and push descriptors. On a card or driver without
 * them this file stands in for those calls. Every Vulkan call goes through the pointers of engine/rendering/vulkan/vk.h, so Install
 * points the missing ones at the functions here, which do the same work with what Vulkan 1.0 has:
 *
 * - vkCmdBeginRendering / vkCmdEndRendering: a render pass for the attachments' formats and a framebuffer for their
 *   views, both kept.
 * - vkCreateGraphicsPipelines: keeps the description. The state that is dynamic in 1.3 (topology, culling, depth and
 *   stencil tests, depth bias on or off, vertex strides) is part of a 1.0 pipeline, so the pipeline itself is made at
 *   the first draw with each combination of it, and bound when the combination changes.
 * - vkCmdPipelineBarrier2, vkQueueSubmit2, vkCmdWriteTimestamp2: their 1.0 forms.
 * - vkCmdPushDescriptorSetKHR (when the extension is missing): descriptor sets from pools that turn over with the
 *   frames, one set for each combination of textures in a frame.
 *
 * The renderer draws from one command buffer at a time, which is what the state kept here assumes.
 */
#include <string.h>

#include "core/log.h"
#include "engine/rendering/vulkan/device.h"
#include "engine/rendering/vulkan/pipeline.h"
#include "engine/rendering/vulkan/shaders.h"

namespace nfs::vk::compat
{
namespace
{
/** The Vulkan 1.0 functions this file calls and the ones it stands in for, as the driver has them. */
struct Real
{
    PFN_vkCreateGraphicsPipelines createGraphicsPipelines;
    PFN_vkDestroyPipeline destroyPipeline;
    PFN_vkCmdBindPipeline bindPipeline;
    PFN_vkCmdDraw draw;
    PFN_vkCmdDrawIndexed drawIndexed;
    PFN_vkCreateImageView createImageView;
    PFN_vkDestroyImageView destroyImageView;
    PFN_vkBeginCommandBuffer beginCommandBuffer;
    PFN_vkCreateDescriptorSetLayout createDescriptorSetLayout;
};
Real g_real;
VkDevice g_device;
bool g_emulatePush;

/** The state Vulkan 1.3 sets on the command buffer and 1.0 builds into the pipeline. */
struct StateKey
{
    uint8_t topology, cullMode, frontFace, depthTest, depthWrite, depthCompare, stencilTest, depthBias;
    /** Front and back: fail, pass, depth fail, compare. */
    uint8_t stencil[2][4];
    uint32_t strides[kMaxVertexBindings];
};

struct Variant
{
    StateKey key;
    VkPipeline pipeline;
};

/** What vkCreateGraphicsPipelines was given: the handle the renderer holds is a pointer to this. */
struct BasePipeline
{
    VkPipelineShaderStageCreateInfo stages[2];
    uint32_t stageCount;
    VkVertexInputBindingDescription bindings[kMaxVertexBindings];
    uint32_t bindingCount;
    VkVertexInputAttributeDescription attributes[kMaxVertexAttributes];
    uint32_t attributeCount;
    VkPipelineColorBlendAttachmentState blend;
    VkFormat colorFormat;
    VkFormat depthFormat;
    VkPipelineLayout layout;
    VkPipelineCache cache;
    Variant *variants;
    uint32_t variantCount;
    uint32_t variantRoom;
};

BasePipeline *Base(VkPipeline handle)
{
    return reinterpret_cast<BasePipeline *>(static_cast<uintptr_t>(handle));
}

struct RenderPass
{
    VkFormat color, depth;
    VkRenderPass pass;
};
const uint32_t kMaxRenderPasses = 32;
RenderPass g_passes[kMaxRenderPasses];
uint32_t g_passCount;

struct Framebuffer
{
    VkRenderPass pass;
    VkImageView views[2];
    uint32_t width, height;
    VkFramebuffer framebuffer;
};
const uint32_t kMaxFramebuffers = 512;
Framebuffer g_framebuffers[kMaxFramebuffers];
uint32_t g_framebufferCount;

/** Each image view's format (vkCmdBeginRendering names views, a render pass wants formats): open addressing by the
 * handle, a removed entry left as a tombstone. Views are made on more than one thread. */
struct ViewFormat
{
    VkImageView view;
    VkFormat format;
};
const VkImageView kTombstone = static_cast<VkImageView>(1);
ViewFormat *g_views;
uint32_t g_viewRoom, g_viewsUsed;
CRITICAL_SECTION g_viewLock;

uint32_t ViewSlot(VkImageView view, uint32_t room)
{
    return static_cast<uint32_t>((static_cast<uint64_t>(view) * 0x9E3779B97F4A7C15ull) >> 40) & (room - 1);
}

void InsertView(ViewFormat *table, uint32_t room, VkImageView view, VkFormat format)
{
    uint32_t slot = ViewSlot(view, room);
    while (table[slot].view != VK_NULL_HANDLE && table[slot].view != kTombstone)
        slot = (slot + 1) & (room - 1);
    table[slot] = {view, format};
}

void RememberView(VkImageView view, VkFormat format)
{
    EnterCriticalSection(&g_viewLock);
    if ((g_viewsUsed + 1) * 2 > g_viewRoom)
    {
        // Twice the room, without the tombstones.
        const uint32_t room = g_viewRoom ? g_viewRoom * 2 : 4096;
        ViewFormat *table = static_cast<ViewFormat *>(::operator new(sizeof(ViewFormat) * room));
        memset(table, 0, sizeof(ViewFormat) * room);
        uint32_t used = 0;
        for (uint32_t i = 0; i < g_viewRoom; ++i)
            if (g_views[i].view != VK_NULL_HANDLE && g_views[i].view != kTombstone)
            {
                InsertView(table, room, g_views[i].view, g_views[i].format);
                ++used;
            }
        ::operator delete(g_views);
        g_views = table;
        g_viewRoom = room;
        g_viewsUsed = used;
    }
    InsertView(g_views, g_viewRoom, view, format);
    ++g_viewsUsed;
    LeaveCriticalSection(&g_viewLock);
}

ViewFormat *FindView(VkImageView view)
{
    if (!g_viewRoom)
        return nullptr;
    for (uint32_t slot = ViewSlot(view, g_viewRoom);; slot = (slot + 1) & (g_viewRoom - 1))
    {
        if (g_views[slot].view == view)
            return &g_views[slot];
        if (g_views[slot].view == VK_NULL_HANDLE)
            return nullptr;
    }
}

VkFormat FormatOf(VkImageView view)
{
    if (view == VK_NULL_HANDLE)
        return VK_FORMAT_UNDEFINED;
    EnterCriticalSection(&g_viewLock);
    const ViewFormat *found = FindView(view);
    const VkFormat format = found ? found->format : VK_FORMAT_UNDEFINED;
    LeaveCriticalSection(&g_viewLock);
    return format;
}

/** The textures pushed for the samplers' set, and the descriptor sets that stand in for the pushes. */
const uint32_t kGenerations = 4;
const uint32_t kPoolsPerGeneration = 32;
const uint32_t kSetsPerPool = 1024;
const uint32_t kSetSlots = 8192;

struct Generation
{
    VkDescriptorPool pools[kPoolsPerGeneration];
    uint32_t poolCount;
    /** The pool sets come from now, and how many it has given. */
    uint32_t pool;
    uint32_t given;
    uint64_t hashes[kSetSlots];
    VkDescriptorSet sets[kSetSlots];
    uint32_t setCount;
};
Generation *g_generations;
uint32_t g_generation;
VkDescriptorSetLayout g_pushLayout;

struct Bound
{
    VkCommandBuffer cmd;
    BasePipeline *base;
    VkPipeline pipeline;
    StateKey state;
    bool stateDirty;
    VkDescriptorImageInfo images[kMaxSamplers];
    uint32_t imageMask;
    bool imagesDirty;
    VkPipelineLayout pushLayout;
    uint32_t pushSet;
    VkDescriptorSet set;
};
Bound g_bound;

bool HasStencil(VkFormat format)
{
    return format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           format == VK_FORMAT_D16_UNORM_S8_UINT;
}

/** A render pass that keeps what its attachments hold, as the renderer's vkCmdBeginRendering does. */
VkRenderPass RenderPassFor(VkFormat color, VkFormat depth)
{
    for (uint32_t i = 0; i < g_passCount; ++i)
        if (g_passes[i].color == color && g_passes[i].depth == depth)
            return g_passes[i].pass;
    if (g_passCount == kMaxRenderPasses)
        return VK_NULL_HANDLE;
    VkAttachmentDescription attachments[2] = {};
    VkAttachmentReference references[2] = {};
    uint32_t count = 0;
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    if (color != VK_FORMAT_UNDEFINED)
    {
        VkAttachmentDescription &a = attachments[count];
        a.format = color;
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = a.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        references[0] = {count++, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &references[0];
    }
    if (depth != VK_FORMAT_UNDEFINED)
    {
        VkAttachmentDescription &a = attachments[count];
        a.format = depth;
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = HasStencil(depth) ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = HasStencil(depth) ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = a.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        references[1] = {count++, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        subpass.pDepthStencilAttachment = &references[1];
    }
    VkRenderPassCreateInfo info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = count;
    info.pAttachments = attachments;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    VkRenderPass pass = VK_NULL_HANDLE;
    const VkResult result = vkCreateRenderPass(g_device, &info, nullptr, &pass);
    if (result != VK_SUCCESS)
    {
        nfs::Log("Vulkan 1.0: vkCreateRenderPass (formats %d, %d) failed (%s)", color, depth, ResultName(result));
        return VK_NULL_HANDLE;
    }
    g_passes[g_passCount++] = {color, depth, pass};
    return pass;
}

void DropFramebuffer(uint32_t index)
{
    vkDestroyFramebuffer(g_device, g_framebuffers[index].framebuffer, nullptr);
    g_framebuffers[index] = g_framebuffers[--g_framebufferCount];
}

VkFramebuffer FramebufferFor(VkRenderPass pass, VkImageView color, VkImageView depth, uint32_t width, uint32_t height)
{
    for (uint32_t i = 0; i < g_framebufferCount; ++i)
    {
        const Framebuffer &f = g_framebuffers[i];
        if (f.pass == pass && f.views[0] == color && f.views[1] == depth && f.width == width && f.height == height)
            return f.framebuffer;
    }
    if (g_framebufferCount == kMaxFramebuffers)
    {
        // Never reached in play (a framebuffer goes with its views). Wait for the GPU, then start again.
        vkDeviceWaitIdle(g_device);
        while (g_framebufferCount)
            DropFramebuffer(g_framebufferCount - 1);
    }
    VkImageView views[2];
    uint32_t count = 0;
    if (color != VK_NULL_HANDLE)
        views[count++] = color;
    if (depth != VK_NULL_HANDLE)
        views[count++] = depth;
    VkFramebufferCreateInfo info = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass = pass;
    info.attachmentCount = count;
    info.pAttachments = views;
    info.width = width;
    info.height = height;
    info.layers = 1;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    const VkResult result = vkCreateFramebuffer(g_device, &info, nullptr, &framebuffer);
    if (result != VK_SUCCESS)
    {
        nfs::Log("Vulkan 1.0: vkCreateFramebuffer (%ux%u) failed (%s)", width, height, ResultName(result));
        return VK_NULL_HANDLE;
    }
    g_framebuffers[g_framebufferCount++] = {pass, {color, depth}, width, height, framebuffer};
    return framebuffer;
}

void VKAPI_CALL BeginRendering(VkCommandBuffer cmd, const VkRenderingInfo *info)
{
    const VkImageView color = info->colorAttachmentCount ? info->pColorAttachments[0].imageView : VK_NULL_HANDLE;
    const VkImageView depth = info->pDepthAttachment ? info->pDepthAttachment->imageView : VK_NULL_HANDLE;
    VkRenderPassBeginInfo begin = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = RenderPassFor(FormatOf(color), FormatOf(depth));
    begin.framebuffer = FramebufferFor(begin.renderPass, color, depth, info->renderArea.offset.x + info->renderArea.extent.width,
                                       info->renderArea.offset.y + info->renderArea.extent.height);
    begin.renderArea = info->renderArea;
    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
}

void VKAPI_CALL EndRendering(VkCommandBuffer cmd)
{
    vkCmdEndRenderPass(cmd);
}

VkResult VKAPI_CALL CreateImageView(VkDevice device, const VkImageViewCreateInfo *info, const VkAllocationCallbacks *a,
                                    VkImageView *view)
{
    const VkResult result = g_real.createImageView(device, info, a, view);
    if (result == VK_SUCCESS)
        RememberView(*view, info->format);
    return result;
}

void VKAPI_CALL DestroyImageView(VkDevice device, VkImageView view, const VkAllocationCallbacks *a)
{
    if (view != VK_NULL_HANDLE)
    {
        EnterCriticalSection(&g_viewLock);
        if (ViewFormat *found = FindView(view))
            found->view = kTombstone;
        LeaveCriticalSection(&g_viewLock);
        // The renderer destroys a view once the GPU is done with the frames that used it: so are its framebuffers.
        for (uint32_t i = g_framebufferCount; i-- > 0;)
            if (g_framebuffers[i].views[0] == view || g_framebuffers[i].views[1] == view)
                DropFramebuffer(i);
        for (uint32_t i = 0; i < kMaxSamplers; ++i)
            if ((g_bound.imageMask & (1u << i)) && g_bound.images[i].imageView == view)
            {
                g_bound.imageMask &= ~(1u << i);
                g_bound.imagesDirty = true;
            }
    }
    g_real.destroyImageView(device, view, a);
}

VkResult VKAPI_CALL CreateGraphicsPipelines(VkDevice, VkPipelineCache cache, uint32_t count,
                                            const VkGraphicsPipelineCreateInfo *infos, const VkAllocationCallbacks *,
                                            VkPipeline *pipelines)
{
    for (uint32_t n = 0; n < count; ++n)
    {
        const VkGraphicsPipelineCreateInfo &info = infos[n];
        BasePipeline *base = static_cast<BasePipeline *>(::operator new(sizeof(BasePipeline)));
        memset(base, 0, sizeof(*base));
        base->stageCount = info.stageCount < 2 ? info.stageCount : 2;
        for (uint32_t i = 0; i < base->stageCount; ++i)
        {
            base->stages[i] = info.pStages[i];
            base->stages[i].pName = "main";
            base->stages[i].pNext = nullptr;
        }
        if (const VkPipelineVertexInputStateCreateInfo *input = info.pVertexInputState)
        {
            base->bindingCount = input->vertexBindingDescriptionCount < kMaxVertexBindings
                                     ? input->vertexBindingDescriptionCount
                                     : kMaxVertexBindings;
            memcpy(base->bindings, input->pVertexBindingDescriptions, sizeof(base->bindings[0]) * base->bindingCount);
            base->attributeCount = input->vertexAttributeDescriptionCount < kMaxVertexAttributes
                                       ? input->vertexAttributeDescriptionCount
                                       : kMaxVertexAttributes;
            memcpy(base->attributes, input->pVertexAttributeDescriptions,
                   sizeof(base->attributes[0]) * base->attributeCount);
        }
        if (info.pColorBlendState && info.pColorBlendState->attachmentCount)
            base->blend = info.pColorBlendState->pAttachments[0];
        for (const VkBaseInStructure *next = static_cast<const VkBaseInStructure *>(info.pNext); next; next = next->pNext)
            if (next->sType == VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO)
            {
                const VkPipelineRenderingCreateInfo *r = reinterpret_cast<const VkPipelineRenderingCreateInfo *>(next);
                base->colorFormat = r->colorAttachmentCount ? r->pColorAttachmentFormats[0] : VK_FORMAT_UNDEFINED;
                base->depthFormat = r->depthAttachmentFormat;
            }
        base->layout = info.layout;
        base->cache = cache;
        pipelines[n] = static_cast<VkPipeline>(reinterpret_cast<uintptr_t>(base));
    }
    return VK_SUCCESS;
}

void VKAPI_CALL DestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *)
{
    BasePipeline *base = Base(pipeline);
    if (!base)
        return;
    for (uint32_t i = 0; i < base->variantCount; ++i)
        g_real.destroyPipeline(device, base->variants[i].pipeline, nullptr);
    if (g_bound.base == base)
    {
        g_bound.base = nullptr;
        g_bound.pipeline = VK_NULL_HANDLE;
    }
    ::operator delete(base->variants);
    ::operator delete(base);
}

/** The pipeline of a description with the state of now: made at its first use. */
VkPipeline VariantFor(BasePipeline *base, const StateKey &state)
{
    StateKey key = state;
    for (uint32_t i = base->bindingCount; i < kMaxVertexBindings; ++i)
        key.strides[i] = 0;
    if (base->depthFormat == VK_FORMAT_UNDEFINED)
    {
        key.depthTest = key.depthWrite = key.stencilTest = 0;
        key.depthCompare = 0;
    }
    if (!key.stencilTest)
        memset(key.stencil, 0, sizeof(key.stencil));
    for (uint32_t i = 0; i < base->variantCount; ++i)
        if (!memcmp(&base->variants[i].key, &key, sizeof(key)))
            return base->variants[i].pipeline;

    VkVertexInputBindingDescription bindings[kMaxVertexBindings];
    for (uint32_t i = 0; i < base->bindingCount; ++i)
    {
        bindings[i] = base->bindings[i];
        bindings[i].stride = key.strides[i];
    }
    VkPipelineVertexInputStateCreateInfo vertexInput = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = base->bindingCount;
    vertexInput.pVertexBindingDescriptions = bindings;
    vertexInput.vertexAttributeDescriptionCount = base->attributeCount;
    vertexInput.pVertexAttributeDescriptions = base->attributes;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = static_cast<VkPrimitiveTopology>(key.topology);
    VkPipelineViewportStateCreateInfo viewport = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;
    raster.cullMode = key.cullMode;
    raster.frontFace = static_cast<VkFrontFace>(key.frontFace);
    raster.depthBiasEnable = key.depthBias;
    VkPipelineMultisampleStateCreateInfo multisample = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = key.depthTest;
    depth.depthWriteEnable = key.depthWrite;
    depth.depthCompareOp = static_cast<VkCompareOp>(key.depthCompare);
    depth.stencilTestEnable = key.stencilTest;
    VkStencilOpState *faces[2] = {&depth.front, &depth.back};
    for (int f = 0; f < 2; ++f)
    {
        faces[f]->failOp = static_cast<VkStencilOp>(key.stencil[f][0]);
        faces[f]->passOp = static_cast<VkStencilOp>(key.stencil[f][1]);
        faces[f]->depthFailOp = static_cast<VkStencilOp>(key.stencil[f][2]);
        faces[f]->compareOp = static_cast<VkCompareOp>(key.stencil[f][3]);
    }
    depth.maxDepthBounds = 1.0f;
    VkPipelineColorBlendStateCreateInfo blend = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = base->colorFormat != VK_FORMAT_UNDEFINED ? 1 : 0;
    blend.pAttachments = &base->blend;
    static const VkDynamicState kStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,             VK_DYNAMIC_STATE_SCISSOR,            VK_DYNAMIC_STATE_DEPTH_BIAS,
        VK_DYNAMIC_STATE_BLEND_CONSTANTS,      VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,   VK_DYNAMIC_STATE_STENCIL_REFERENCE,
    };
    VkPipelineDynamicStateCreateInfo dynamic = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = sizeof(kStates) / sizeof(kStates[0]);
    dynamic.pDynamicStates = kStates;

    VkGraphicsPipelineCreateInfo info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = base->stageCount;
    info.pStages = base->stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = base->layout;
    info.renderPass = RenderPassFor(base->colorFormat, base->depthFormat);
    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult result = g_real.createGraphicsPipelines(g_device, base->cache, 1, &info, nullptr, &pipeline);
    if (result != VK_SUCCESS)
        nfs::Log("Vulkan 1.0: vkCreateGraphicsPipelines failed (%s)", ResultName(result));
    if (base->variantCount == base->variantRoom)
    {
        const uint32_t room = base->variantRoom ? base->variantRoom * 2 : 4;
        Variant *grown = static_cast<Variant *>(::operator new(sizeof(Variant) * room));
        if (base->variantCount)
            memcpy(grown, base->variants, sizeof(Variant) * base->variantCount);
        ::operator delete(base->variants);
        base->variants = grown;
        base->variantRoom = room;
    }
    base->variants[base->variantCount++] = {key, pipeline};
    return pipeline;
}

void VKAPI_CALL BindPipeline(VkCommandBuffer, VkPipelineBindPoint, VkPipeline pipeline)
{
    g_bound.base = Base(pipeline);
    g_bound.stateDirty = true;
}

/** The descriptor set that holds the textures pushed: one a frame for each combination of them. */
VkDescriptorSet SetForImages()
{
    uint64_t hash = 0xcbf29ce484222325ull;
    const auto mix = [&hash](uint64_t v) {
        hash ^= v;
        hash *= 0x100000001b3ull;
        hash ^= hash >> 29;
    };
    mix(g_bound.imageMask);
    for (uint32_t i = 0; i < kMaxSamplers; ++i)
        if (g_bound.imageMask & (1u << i))
        {
            mix(static_cast<uint64_t>(g_bound.images[i].imageView));
            mix(static_cast<uint64_t>(g_bound.images[i].sampler) + i);
        }
    if (!hash)
        hash = 1;
    Generation &g = g_generations[g_generation];
    uint32_t slot = static_cast<uint32_t>(hash >> 20) & (kSetSlots - 1);
    for (; g.hashes[slot]; slot = (slot + 1) & (kSetSlots - 1))
        if (g.hashes[slot] == hash)
            return g.sets[slot];

    VkDescriptorSet set = VK_NULL_HANDLE;
    for (int attempt = 0; attempt < 2 && set == VK_NULL_HANDLE; ++attempt)
    {
        if (g.pool == g.poolCount)
        {
            if (g.poolCount == kPoolsPerGeneration)
                return VK_NULL_HANDLE;
            const VkDescriptorPoolSize size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kSetsPerPool * kMaxSamplers};
            VkDescriptorPoolCreateInfo info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            info.maxSets = kSetsPerPool;
            info.poolSizeCount = 1;
            info.pPoolSizes = &size;
            if (vkCreateDescriptorPool(g_device, &info, nullptr, &g.pools[g.poolCount]) != VK_SUCCESS)
                return VK_NULL_HANDLE;
            ++g.poolCount;
            g.given = 0;
        }
        if (g.given < kSetsPerPool)
        {
            VkDescriptorSetAllocateInfo alloc = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            alloc.descriptorPool = g.pools[g.pool];
            alloc.descriptorSetCount = 1;
            alloc.pSetLayouts = &g_pushLayout;
            if (vkAllocateDescriptorSets(g_device, &alloc, &set) == VK_SUCCESS)
                ++g.given;
            else
                set = VK_NULL_HANDLE;
        }
        if (set == VK_NULL_HANDLE)
        {
            ++g.pool;
            g.given = 0;
        }
    }
    if (set == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;
    VkWriteDescriptorSet writes[kMaxSamplers];
    uint32_t count = 0;
    for (uint32_t i = 0; i < kMaxSamplers; ++i)
        if (g_bound.imageMask & (1u << i))
        {
            VkWriteDescriptorSet &w = writes[count++];
            w = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = set;
            w.dstBinding = kFirstSamplerBinding + i;
            w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.pImageInfo = &g_bound.images[i];
        }
    if (count)
        vkUpdateDescriptorSets(g_device, count, writes, 0, nullptr);
    // A full table only costs sets (each new combination takes one more).
    if (g.setCount < kSetSlots * 3 / 4)
    {
        g.hashes[slot] = hash;
        g.sets[slot] = set;
        ++g.setCount;
    }
    return set;
}

/** Before a draw: the pipeline for the state of now, and the textures' set. */
void Resolve(VkCommandBuffer cmd)
{
    if (g_bound.cmd != cmd)
    {
        g_bound.cmd = cmd;
        g_bound.pipeline = VK_NULL_HANDLE;
        g_bound.set = VK_NULL_HANDLE;
        g_bound.stateDirty = true;
        g_bound.imagesDirty = true;
    }
    if (g_bound.stateDirty && g_bound.base)
    {
        const VkPipeline pipeline = VariantFor(g_bound.base, g_bound.state);
        if (pipeline != g_bound.pipeline && pipeline != VK_NULL_HANDLE)
        {
            g_real.bindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            g_bound.pipeline = pipeline;
        }
        g_bound.stateDirty = false;
    }
    if (g_emulatePush && g_bound.imagesDirty && g_bound.pushLayout != VK_NULL_HANDLE)
    {
        const VkDescriptorSet set = SetForImages();
        if (set != VK_NULL_HANDLE && set != g_bound.set)
        {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_bound.pushLayout, g_bound.pushSet, 1, &set, 0,
                                    nullptr);
            g_bound.set = set;
        }
        g_bound.imagesDirty = false;
    }
}

void VKAPI_CALL Draw(VkCommandBuffer cmd, uint32_t vertices, uint32_t instances, uint32_t first, uint32_t firstInstance)
{
    Resolve(cmd);
    g_real.draw(cmd, vertices, instances, first, firstInstance);
}

void VKAPI_CALL DrawIndexed(VkCommandBuffer cmd, uint32_t indices, uint32_t instances, uint32_t first, int32_t offset,
                            uint32_t firstInstance)
{
    Resolve(cmd);
    g_real.drawIndexed(cmd, indices, instances, first, offset, firstInstance);
}

VkResult VKAPI_CALL BeginCommandBuffer(VkCommandBuffer cmd, const VkCommandBufferBeginInfo *info)
{
    if (g_bound.cmd == cmd)
        g_bound.cmd = VK_NULL_HANDLE;
    return g_real.beginCommandBuffer(cmd, info);
}

template <typename T> void Set(uint8_t &field, T value)
{
    if (field != static_cast<uint8_t>(value))
    {
        field = static_cast<uint8_t>(value);
        g_bound.stateDirty = true;
    }
}

void VKAPI_CALL SetCullMode(VkCommandBuffer, VkCullModeFlags mode)
{
    Set(g_bound.state.cullMode, mode);
}
void VKAPI_CALL SetFrontFace(VkCommandBuffer, VkFrontFace face)
{
    Set(g_bound.state.frontFace, face);
}
void VKAPI_CALL SetPrimitiveTopology(VkCommandBuffer, VkPrimitiveTopology topology)
{
    Set(g_bound.state.topology, topology);
}
void VKAPI_CALL SetDepthTestEnable(VkCommandBuffer, VkBool32 on)
{
    Set(g_bound.state.depthTest, on);
}
void VKAPI_CALL SetDepthWriteEnable(VkCommandBuffer, VkBool32 on)
{
    Set(g_bound.state.depthWrite, on);
}
void VKAPI_CALL SetDepthCompareOp(VkCommandBuffer, VkCompareOp op)
{
    Set(g_bound.state.depthCompare, op);
}
void VKAPI_CALL SetStencilTestEnable(VkCommandBuffer, VkBool32 on)
{
    Set(g_bound.state.stencilTest, on);
}
void VKAPI_CALL SetDepthBiasEnable(VkCommandBuffer, VkBool32 on)
{
    Set(g_bound.state.depthBias, on);
}
void VKAPI_CALL SetStencilOp(VkCommandBuffer, VkStencilFaceFlags faces, VkStencilOp fail, VkStencilOp pass,
                             VkStencilOp depthFail, VkCompareOp compare)
{
    for (int f = 0; f < 2; ++f)
        if (faces & (f ? VK_STENCIL_FACE_BACK_BIT : VK_STENCIL_FACE_FRONT_BIT))
        {
            Set(g_bound.state.stencil[f][0], fail);
            Set(g_bound.state.stencil[f][1], pass);
            Set(g_bound.state.stencil[f][2], depthFail);
            Set(g_bound.state.stencil[f][3], compare);
        }
}
/** Always off in the renderer: depth bounds, rasterizer discard, primitive restart. */
void VKAPI_CALL SetNothing(VkCommandBuffer, VkBool32)
{
}

void VKAPI_CALL BindVertexBuffers2(VkCommandBuffer cmd, uint32_t first, uint32_t count, const VkBuffer *buffers,
                                   const VkDeviceSize *offsets, const VkDeviceSize *, const VkDeviceSize *strides)
{
    for (uint32_t i = 0; strides && i < count && first + i < kMaxVertexBindings; ++i)
        if (g_bound.state.strides[first + i] != static_cast<uint32_t>(strides[i]))
        {
            g_bound.state.strides[first + i] = static_cast<uint32_t>(strides[i]);
            g_bound.stateDirty = true;
        }
    vkCmdBindVertexBuffers(cmd, first, count, buffers, offsets);
}

VkResult VKAPI_CALL CreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo *info,
                                              const VkAllocationCallbacks *a, VkDescriptorSetLayout *layout)
{
    if (!(info->flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR))
        return g_real.createDescriptorSetLayout(device, info, a, layout);
    VkDescriptorSetLayoutCreateInfo plain = *info;
    plain.flags &= ~static_cast<VkDescriptorSetLayoutCreateFlags>(VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR);
    const VkResult result = g_real.createDescriptorSetLayout(device, &plain, a, layout);
    if (result == VK_SUCCESS)
        g_pushLayout = *layout;
    return result;
}

void VKAPI_CALL PushDescriptorSet(VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout layout, uint32_t set,
                                  uint32_t count, const VkWriteDescriptorSet *writes)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint32_t stage = writes[i].dstBinding - kFirstSamplerBinding;
        if (stage < kMaxSamplers && writes[i].pImageInfo)
        {
            g_bound.images[stage] = *writes[i].pImageInfo;
            g_bound.imageMask |= 1u << stage;
        }
    }
    g_bound.pushLayout = layout;
    g_bound.pushSet = set;
    g_bound.imagesDirty = true;
}

/** synchronization2's stages and accesses as Vulkan 1.0 has them: the copy and blit stages are its transfer stage,
 * sampled reads its shader reads; no stage is the pipeline's top (before) or bottom (after). */
VkPipelineStageFlags Stages(VkPipelineStageFlags2 stages, bool source)
{
    VkPipelineStageFlags out = static_cast<VkPipelineStageFlags>(stages & 0x7fffffffull);
    if (stages & (VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT |
                  VK_PIPELINE_STAGE_2_CLEAR_BIT))
        out |= VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (stages & (VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT | VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT))
        out |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    if (!out)
        out = source ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    return out;
}

VkAccessFlags Access(VkAccessFlags2 access)
{
    VkAccessFlags out = static_cast<VkAccessFlags>(access & 0x1ffffull);
    if (access & (VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT))
        out |= VK_ACCESS_SHADER_READ_BIT;
    if (access & VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT)
        out |= VK_ACCESS_SHADER_WRITE_BIT;
    return out;
}

void VKAPI_CALL PipelineBarrier2(VkCommandBuffer cmd, const VkDependencyInfo *info)
{
    const uint32_t kMost = 16;
    VkMemoryBarrier memory[kMost];
    VkBufferMemoryBarrier buffers[kMost];
    VkImageMemoryBarrier images[kMost];
    const uint32_t memoryCount = info->memoryBarrierCount < kMost ? info->memoryBarrierCount : kMost;
    const uint32_t bufferCount = info->bufferMemoryBarrierCount < kMost ? info->bufferMemoryBarrierCount : kMost;
    const uint32_t imageCount = info->imageMemoryBarrierCount < kMost ? info->imageMemoryBarrierCount : kMost;
    VkPipelineStageFlags2 source = 0, destination = 0;
    for (uint32_t i = 0; i < memoryCount; ++i)
    {
        const VkMemoryBarrier2 &b = info->pMemoryBarriers[i];
        source |= b.srcStageMask;
        destination |= b.dstStageMask;
        memory[i] = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, Access(b.srcAccessMask), Access(b.dstAccessMask)};
    }
    for (uint32_t i = 0; i < bufferCount; ++i)
    {
        const VkBufferMemoryBarrier2 &b = info->pBufferMemoryBarriers[i];
        source |= b.srcStageMask;
        destination |= b.dstStageMask;
        buffers[i] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr, Access(b.srcAccessMask), Access(b.dstAccessMask),
                      b.srcQueueFamilyIndex, b.dstQueueFamilyIndex, b.buffer, b.offset, b.size};
    }
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        const VkImageMemoryBarrier2 &b = info->pImageMemoryBarriers[i];
        source |= b.srcStageMask;
        destination |= b.dstStageMask;
        images[i] = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr, Access(b.srcAccessMask), Access(b.dstAccessMask),
                     b.oldLayout, b.newLayout, b.srcQueueFamilyIndex, b.dstQueueFamilyIndex, b.image, b.subresourceRange};
    }
    vkCmdPipelineBarrier(cmd, Stages(source, true), Stages(destination, false), 0, memoryCount, memory, bufferCount,
                         buffers, imageCount, images);
}

VkResult VKAPI_CALL QueueSubmit2(VkQueue queue, uint32_t count, const VkSubmitInfo2 *submits, VkFence fence)
{
    const uint32_t kMost = 8;
    VkResult result = VK_SUCCESS;
    for (uint32_t n = 0; n < count && result == VK_SUCCESS; ++n)
    {
        const VkSubmitInfo2 &s = submits[n];
        VkSemaphore waits[kMost], signals[kMost];
        VkPipelineStageFlags stages[kMost];
        VkCommandBuffer commands[kMost];
        VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        for (uint32_t i = 0; i < s.waitSemaphoreInfoCount && i < kMost; ++i)
        {
            waits[i] = s.pWaitSemaphoreInfos[i].semaphore;
            stages[i] = Stages(s.pWaitSemaphoreInfos[i].stageMask, true);
            ++submit.waitSemaphoreCount;
        }
        for (uint32_t i = 0; i < s.commandBufferInfoCount && i < kMost; ++i)
        {
            commands[i] = s.pCommandBufferInfos[i].commandBuffer;
            ++submit.commandBufferCount;
        }
        for (uint32_t i = 0; i < s.signalSemaphoreInfoCount && i < kMost; ++i)
        {
            signals[i] = s.pSignalSemaphoreInfos[i].semaphore;
            ++submit.signalSemaphoreCount;
        }
        submit.pWaitSemaphores = waits;
        submit.pWaitDstStageMask = stages;
        submit.pCommandBuffers = commands;
        submit.pSignalSemaphores = signals;
        // The fence goes with the last one.
        result = vkQueueSubmit(queue, 1, &submit, n + 1 == count ? fence : VK_NULL_HANDLE);
    }
    return result;
}

void VKAPI_CALL WriteTimestamp2(VkCommandBuffer cmd, VkPipelineStageFlags2 stage, VkQueryPool pool, uint32_t query)
{
    vkCmdWriteTimestamp(cmd, static_cast<VkPipelineStageFlagBits>(Stages(stage, false)), pool, query);
}
} // namespace

void Install(const Device &device, bool emulatePush)
{
    g_device = device.device;
    g_emulatePush = emulatePush;
    InitializeCriticalSection(&g_viewLock);
    memset(&g_bound, 0, sizeof(g_bound));
    g_bound.state.frontFace = VK_FRONT_FACE_CLOCKWISE;
    g_bound.state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    g_bound.state.depthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;

    g_real.createGraphicsPipelines = vkCreateGraphicsPipelines;
    g_real.destroyPipeline = vkDestroyPipeline;
    g_real.bindPipeline = vkCmdBindPipeline;
    g_real.draw = vkCmdDraw;
    g_real.drawIndexed = vkCmdDrawIndexed;
    g_real.createImageView = vkCreateImageView;
    g_real.destroyImageView = vkDestroyImageView;
    g_real.beginCommandBuffer = vkBeginCommandBuffer;
    g_real.createDescriptorSetLayout = vkCreateDescriptorSetLayout;

    vkCreateGraphicsPipelines = &CreateGraphicsPipelines;
    vkDestroyPipeline = &DestroyPipeline;
    vkCmdBindPipeline = &BindPipeline;
    vkCmdDraw = &Draw;
    vkCmdDrawIndexed = &DrawIndexed;
    vkCreateImageView = &CreateImageView;
    vkDestroyImageView = &DestroyImageView;
    vkBeginCommandBuffer = &BeginCommandBuffer;
    vkCmdBeginRendering = &BeginRendering;
    vkCmdEndRendering = &EndRendering;
    vkCmdSetCullMode = &SetCullMode;
    vkCmdSetFrontFace = &SetFrontFace;
    vkCmdSetPrimitiveTopology = &SetPrimitiveTopology;
    vkCmdSetDepthTestEnable = &SetDepthTestEnable;
    vkCmdSetDepthWriteEnable = &SetDepthWriteEnable;
    vkCmdSetDepthCompareOp = &SetDepthCompareOp;
    vkCmdSetDepthBoundsTestEnable = &SetNothing;
    vkCmdSetStencilTestEnable = &SetStencilTestEnable;
    vkCmdSetStencilOp = &SetStencilOp;
    vkCmdSetDepthBiasEnable = &SetDepthBiasEnable;
    vkCmdSetRasterizerDiscardEnable = &SetNothing;
    vkCmdSetPrimitiveRestartEnable = &SetNothing;
    vkCmdBindVertexBuffers2 = &BindVertexBuffers2;
    vkCmdPipelineBarrier2 = &PipelineBarrier2;
    vkQueueSubmit2 = &QueueSubmit2;
    vkCmdWriteTimestamp2 = &WriteTimestamp2;
    if (emulatePush)
    {
        g_generations = static_cast<Generation *>(::operator new(sizeof(Generation) * kGenerations));
        memset(g_generations, 0, sizeof(Generation) * kGenerations);
        vkCreateDescriptorSetLayout = &CreateDescriptorSetLayout;
        vkCmdPushDescriptorSetKHR = &PushDescriptorSet;
    }
    nfs::Log("Vulkan 1.0: render passes, pipelines by state%s", emulatePush ? ", descriptor sets for the textures" : "");
}

void BeginFrame()
{
    if (!g_generations)
        return;
    // The sets of kGenerations frames ago: the GPU is done with them (the renderer keeps fewer frames in flight).
    g_generation = (g_generation + 1) % kGenerations;
    Generation &g = g_generations[g_generation];
    for (uint32_t i = 0; i < g.poolCount; ++i)
        vkResetDescriptorPool(g_device, g.pools[i], 0);
    g.pool = 0;
    g.given = 0;
    if (g.setCount)
    {
        memset(g.hashes, 0, sizeof(g.hashes));
        g.setCount = 0;
    }
    g_bound.set = VK_NULL_HANDLE;
    g_bound.imagesDirty = true;
}
} // namespace nfs::vk::compat
