#include <string.h>

#include "core/log.h"
#include "engine/rendering/vulkan/pipeline.h"

namespace nfs::vk
{
namespace
{
VkShaderModule g_modules[512];

const VkDynamicState kDynamicStates[] = {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR,
    VK_DYNAMIC_STATE_CULL_MODE,
    VK_DYNAMIC_STATE_FRONT_FACE,
    VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
    VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE,
    VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
    VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
    VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
    VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE,
    VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,
    VK_DYNAMIC_STATE_STENCIL_OP,
    VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
    VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
    VK_DYNAMIC_STATE_STENCIL_REFERENCE,
    VK_DYNAMIC_STATE_DEPTH_BIAS,
    VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,
    VK_DYNAMIC_STATE_BLEND_CONSTANTS,
    VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE,
    VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE,
};

const uint32_t kOpDecorate = 71;
const uint32_t kDecorationBinding = 33;
const uint32_t kDecorationDescriptorSet = 34;
const uint32_t kSpirvHeaderWords = 5;

/** Gives the variables at the sampler bindings (the translator puts every binding in set 0) set kSamplersSet. Only
 * those in set 0: the owned shaders' scene set (kSceneSet) has image bindings of its own, and its shadow depth at
 * binding 2, moved with them, read the material's first texture instead. */
void MoveSamplersToTheirSet(uint32_t *code, uint32_t words)
{
    uint32_t samplers[kMaxSamplers];
    uint32_t count = 0;
    for (uint32_t i = kSpirvHeaderWords; i < words && (code[i] >> 16);)
    {
        const uint32_t length = code[i] >> 16;
        if ((code[i] & 0xFFFF) == kOpDecorate && length >= 4 && code[i + 2] == kDecorationBinding &&
            code[i + 3] >= kFirstSamplerBinding && code[i + 3] < kFirstSamplerBinding + kMaxSamplers &&
            count < kMaxSamplers)
            samplers[count++] = code[i + 1];
        i += length;
    }
    for (uint32_t i = kSpirvHeaderWords; i < words && (code[i] >> 16);)
    {
        const uint32_t length = code[i] >> 16;
        if ((code[i] & 0xFFFF) == kOpDecorate && length >= 4 && code[i + 2] == kDecorationDescriptorSet)
            for (uint32_t s = 0; s < count; ++s)
                if (samplers[s] == code[i + 1] && code[i + 3] == 0)
                    code[i + 3] = kSamplersSet;
        i += length;
    }
}
} // namespace

bool CreatePipelineLayout(const Device &device, PipelineLayout &out)
{
    out = {};
    VkDescriptorSetLayoutBinding constants[4] = {};
    constants[0].binding = kVertexConstantsBinding;
    constants[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    constants[0].descriptorCount = 1;
    constants[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    constants[1].binding = kPixelConstantsBinding;
    constants[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    constants[1].descriptorCount = 1;
    constants[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    constants[2].binding = kDrawConstantsBinding;
    constants[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    constants[2].descriptorCount = 1;
    constants[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    constants[3].binding = kInstanceBlocksBinding;
    constants[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    constants[3].descriptorCount = 1;
    constants[3].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutBinding samplers[kMaxSamplers] = {};
    for (uint32_t i = 0; i < kMaxSamplers; ++i)
    {
        VkDescriptorSetLayoutBinding &b = samplers[i];
        b.binding = kFirstSamplerBinding + i;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo set = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set.bindingCount = 4;
    set.pBindings = constants;
    VkResult result = vkCreateDescriptorSetLayout(device.device, &set, nullptr, &out.constantsLayout);
    if (result == VK_SUCCESS)
    {
        set.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        set.bindingCount = kMaxSamplers;
        set.pBindings = samplers;
        result = vkCreateDescriptorSetLayout(device.device, &set, nullptr, &out.samplersLayout);
    }
    VkDescriptorSetLayoutBinding scene[kSceneBindings] = {};
    scene[0].binding = kSceneConstantsBinding;
    scene[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    scene[0].descriptorCount = 1;
    scene[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    for (uint32_t i = 1; i < kSceneBindings; ++i)
    {
        scene[i].binding = i;
        scene[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        scene[i].descriptorCount = 1;
        // The lamps' light is read by vertex shaders too (particles are lit per vertex).
        scene[i].stageFlags = i >= kLampVolumeBinding ? VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
                                                      : VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    if (result == VK_SUCCESS)
    {
        set.flags = 0;
        set.bindingCount = kSceneBindings;
        set.pBindings = scene;
        result = vkCreateDescriptorSetLayout(device.device, &set, nullptr, &out.sceneLayout);
    }
    if (result != VK_SUCCESS)
    {
        nfs::Log("Vulkan: vkCreateDescriptorSetLayout failed (%s)", ResultName(result));
        DestroyPipelineLayout(device, out);
        return false;
    }
    const VkDescriptorSetLayout sets[3] = {out.constantsLayout, out.samplersLayout, out.sceneLayout};
    VkPushConstantRange push = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(D3D9State)};
    VkPipelineLayoutCreateInfo layout = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 3;
    layout.pSetLayouts = sets;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    result = vkCreatePipelineLayout(device.device, &layout, nullptr, &out.layout);
    if (result != VK_SUCCESS)
    {
        nfs::Log("Vulkan: vkCreatePipelineLayout failed (%s)", ResultName(result));
        DestroyPipelineLayout(device, out);
        return false;
    }
    return true;
}

void DestroyPipelineLayout(const Device &device, PipelineLayout &layout)
{
    if (layout.layout)
        vkDestroyPipelineLayout(device.device, layout.layout, nullptr);
    if (layout.constantsLayout)
        vkDestroyDescriptorSetLayout(device.device, layout.constantsLayout, nullptr);
    if (layout.samplersLayout)
        vkDestroyDescriptorSetLayout(device.device, layout.samplersLayout, nullptr);
    if (layout.sceneLayout)
        vkDestroyDescriptorSetLayout(device.device, layout.sceneLayout, nullptr);
    layout = {};
}

VkDescriptorPool CreateConstantsPool(const Device &device, uint32_t sets)
{
    const VkDescriptorPoolSize sizes[4] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, sets * 3},
                                           {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, sets},
                                           {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sets * (kSceneBindings - 1)},
                                           {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, sets}};
    VkDescriptorPoolCreateInfo info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    info.maxSets = sets * 2;
    info.poolSizeCount = 4;
    info.pPoolSizes = sizes;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkResult result = vkCreateDescriptorPool(device.device, &info, nullptr, &pool);
    if (result != VK_SUCCESS)
        nfs::Log("Vulkan: vkCreateDescriptorPool failed (%s)", ResultName(result));
    return pool;
}

VkDescriptorSet AllocateConstantsSet(const Device &device, const PipelineLayout &layout, VkDescriptorPool pool,
                                     VkBuffer buffer)
{
    VkDescriptorSetAllocateInfo alloc = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = pool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &layout.constantsLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkResult result = vkAllocateDescriptorSets(device.device, &alloc, &set);
    if (result != VK_SUCCESS)
    {
        nfs::Log("Vulkan: vkAllocateDescriptorSets failed (%s)", ResultName(result));
        return VK_NULL_HANDLE;
    }
    const VkDescriptorBufferInfo infos[3] = {
        {buffer, 0, kConstantsRange}, {buffer, 0, kDrawConstantsBytes}, {buffer, 0, kInstanceBlocksRange}};
    static const uint32_t kBindings[4] = {kVertexConstantsBinding, kPixelConstantsBinding, kDrawConstantsBinding,
                                          kInstanceBlocksBinding};
    VkWriteDescriptorSet writes[4] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},
                                      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    for (uint32_t i = 0; i < 4; ++i)
    {
        writes[i].dstSet = set;
        writes[i].dstBinding = kBindings[i];
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = i == 3 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[i].pBufferInfo = &infos[i == 3 ? 2 : i == 2 ? 1 : 0];
    }
    vkUpdateDescriptorSets(device.device, 4, writes, 0, nullptr);
    return set;
}

VkDescriptorSet AllocateSceneSet(const Device &device, const PipelineLayout &layout, VkDescriptorPool pool,
                                 VkBuffer constants)
{
    VkDescriptorSetAllocateInfo alloc = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = pool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &layout.sceneLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkResult result = vkAllocateDescriptorSets(device.device, &alloc, &set);
    if (result != VK_SUCCESS)
    {
        nfs::Log("Vulkan: vkAllocateDescriptorSets (scene) failed (%s)", ResultName(result));
        return VK_NULL_HANDLE;
    }
    VkDescriptorBufferInfo info = {constants, 0, kSceneConstantsBytes};
    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.dstBinding = kSceneConstantsBinding;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &info;
    vkUpdateDescriptorSets(device.device, 1, &write, 0, nullptr);
    return set;
}

void WriteSceneShadow(const Device &device, VkDescriptorSet set, VkImageView view, VkSampler compare,
                      VkSampler point)
{
    const VkDescriptorImageInfo images[2] = {{compare, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                             {point, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkWriteDescriptorSet writes[2] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    for (uint32_t i = 0; i < 2; ++i)
    {
        writes[i].dstSet = set;
        writes[i].dstBinding = i == 0 ? kSunShadowCompareBinding : kSunShadowDepthBinding;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &images[i];
    }
    vkUpdateDescriptorSets(device.device, 2, writes, 0, nullptr);
}

void WriteSceneHeadlightShadow(const Device &device, VkDescriptorSet set, VkImageView view, VkSampler compare)
{
    const VkDescriptorImageInfo image = {compare, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.dstBinding = kHeadlightShadowBinding;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(device.device, 1, &write, 0, nullptr);
}

void WriteSceneLamps(const Device &device, VkDescriptorSet set, const VkImageView *views, VkSampler sampler)
{
    VkDescriptorImageInfo images[kLampVolumes];
    VkWriteDescriptorSet writes[kLampVolumes];
    for (uint32_t i = 0; i < kLampVolumes; ++i)
    {
        images[i] = {sampler, views[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[i].dstSet = set;
        writes[i].dstBinding = kLampVolumeBinding + i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &images[i];
    }
    vkUpdateDescriptorSets(device.device, kLampVolumes, writes, 0, nullptr);
}

VkShaderModule ShaderModule(const Device &device, uint32_t index)
{
    if (index >= kShaderCount || index >= sizeof(g_modules) / sizeof(g_modules[0]))
        return VK_NULL_HANDLE;
    if (!g_modules[index])
    {
        const uint32_t words = kShaders[index].spirvWords;
        uint32_t *code = static_cast<uint32_t *>(::operator new(words * 4));
        memcpy(code, kShaders[index].spirv, words * 4);
        MoveSamplersToTheirSet(code, words);
        VkShaderModuleCreateInfo info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = words * 4;
        info.pCode = code;
        VkResult result = vkCreateShaderModule(device.device, &info, nullptr, &g_modules[index]);
        ::operator delete(code);
        if (result != VK_SUCCESS)
            nfs::Log("Vulkan: shader %s: vkCreateShaderModule failed (%s)", kShaders[index].name, ResultName(result));
    }
    return g_modules[index];
}

void DestroyShaderModules(const Device &device)
{
    for (VkShaderModule &module : g_modules)
    {
        if (module)
            vkDestroyShaderModule(device.device, module, nullptr);
        module = VK_NULL_HANDLE;
    }
}

VkPipeline CreatePipeline(const Device &device, const PipelineLayout &layout, const PipelineDesc &desc,
                          VkPipelineCache cache)
{
    VkPipelineShaderStageCreateInfo stages[2] = {};
    uint32_t stageCount = 0;
    if (desc.vertexShader >= 0)
    {
        stages[stageCount] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[stageCount].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[stageCount].module = ShaderModule(device, desc.vertexShader);
        stages[stageCount++].pName = "main";
    }
    if (desc.pixelShader >= 0)
    {
        stages[stageCount] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[stageCount].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[stageCount].module = ShaderModule(device, desc.pixelShader);
        stages[stageCount++].pName = "main";
    }

    VkVertexInputBindingDescription bindings[kMaxVertexBindings];
    for (uint32_t i = 0; i < desc.bindingCount; ++i)
        bindings[i] = {i, 0, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[kMaxVertexAttributes];
    for (uint32_t i = 0; i < desc.attributeCount; ++i)
    {
        const VertexAttribute &a = desc.attributes[i];
        attributes[i] = {a.location, a.binding, a.format, a.offset};
    }
    VkPipelineVertexInputStateCreateInfo vertexInput = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = desc.bindingCount;
    vertexInput.pVertexBindingDescriptions = bindings;
    vertexInput.vertexAttributeDescriptionCount = desc.attributeCount;
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = desc.topology;

    VkPipelineViewportStateCreateInfo viewport = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.maxDepthBounds = 1.0f;

    VkPipelineColorBlendAttachmentState attachment = {};
    attachment.blendEnable = desc.blendEnable;
    attachment.srcColorBlendFactor = desc.srcColor;
    attachment.dstColorBlendFactor = desc.dstColor;
    attachment.colorBlendOp = desc.colorOp;
    attachment.srcAlphaBlendFactor = desc.srcAlpha;
    attachment.dstAlphaBlendFactor = desc.dstAlpha;
    attachment.alphaBlendOp = desc.alphaOp;
    attachment.colorWriteMask = desc.writeMask;
    VkPipelineColorBlendStateCreateInfo blend = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = desc.colorFormat != VK_FORMAT_UNDEFINED ? 1 : 0;
    blend.pAttachments = &attachment;

    VkPipelineDynamicStateCreateInfo dynamic = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = sizeof(kDynamicStates) / sizeof(kDynamicStates[0]);
    dynamic.pDynamicStates = kDynamicStates;

    VkPipelineRenderingCreateInfo rendering = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = desc.colorFormat != VK_FORMAT_UNDEFINED ? 1 : 0;
    rendering.pColorAttachmentFormats = &desc.colorFormat;
    rendering.depthAttachmentFormat = desc.depthFormat;
    if (desc.depthFormat == VK_FORMAT_D24_UNORM_S8_UINT || desc.depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT)
        rendering.stencilAttachmentFormat = desc.depthFormat;

    VkGraphicsPipelineCreateInfo info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &rendering};
    info.stageCount = stageCount;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout.layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateGraphicsPipelines(device.device, cache, 1, &info, nullptr, &pipeline);
    if (result != VK_SUCCESS)
    {
        nfs::Log("Vulkan: vkCreateGraphicsPipelines (vs %d, ps %d) failed (%s)", desc.vertexShader, desc.pixelShader,
                 ResultName(result));
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

void SetDefaultDynamicState(VkCommandBuffer cmd, uint32_t width, uint32_t height)
{
    VkViewport viewport = {0.0f, static_cast<float>(height), static_cast<float>(width), -static_cast<float>(height),
                           0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor = {{0, 0}, {width, height}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
    vkCmdSetFrontFace(cmd, VK_FRONT_FACE_CLOCKWISE);
    vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdSetDepthTestEnable(cmd, VK_FALSE);
    vkCmdSetDepthWriteEnable(cmd, VK_FALSE);
    vkCmdSetDepthCompareOp(cmd, VK_COMPARE_OP_LESS_OR_EQUAL);
    vkCmdSetDepthBoundsTestEnable(cmd, VK_FALSE);
    vkCmdSetStencilTestEnable(cmd, VK_FALSE);
    vkCmdSetStencilOp(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP,
                      VK_COMPARE_OP_ALWAYS);
    vkCmdSetStencilCompareMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 0xFF);
    vkCmdSetStencilWriteMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 0xFF);
    vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
    vkCmdSetDepthBias(cmd, 0.0f, 0.0f, 0.0f);
    vkCmdSetDepthBiasEnable(cmd, VK_FALSE);
    const float blendConstants[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    vkCmdSetBlendConstants(cmd, blendConstants);
    vkCmdSetRasterizerDiscardEnable(cmd, VK_FALSE);
    vkCmdSetPrimitiveRestartEnable(cmd, VK_FALSE);
}
namespace
{
VkPipeline CreateLibrary(const Device &device, VkGraphicsPipelineCreateInfo &info,
                         VkGraphicsPipelineLibraryFlagsEXT part, VkPipelineCache cache)
{
    VkGraphicsPipelineLibraryCreateInfoEXT library = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT,
                                                      const_cast<void *>(info.pNext)};
    library.flags = part;
    info.pNext = &library;
    info.flags |= VK_PIPELINE_CREATE_LIBRARY_BIT_KHR | VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateGraphicsPipelines(device.device, cache, 1, &info, nullptr, &pipeline);
    if (result != VK_SUCCESS)
    {
        nfs::Log("Vulkan: pipeline library (part %x) failed (%s)", part, ResultName(result));
        return VK_NULL_HANDLE;
    }
    return pipeline;
}
} // namespace

VkPipeline CreateShaderLibrary(const Device &device, const PipelineLayout &layout, uint32_t shader,
                               VkPipelineCache cache)
{
    if (shader >= kShaderCount)
        return VK_NULL_HANDLE;
    const bool vertex = kShaders[shader].stage == ShaderStage::Vertex;
    VkPipelineShaderStageCreateInfo stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stage.stage = vertex ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
    stage.module = ShaderModule(device, shader);
    stage.pName = "main";
    if (!stage.module)
        return VK_NULL_HANDLE;
    VkPipelineViewportStateCreateInfo viewport = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.maxDepthBounds = 1.0f;
    VkPipelineDynamicStateCreateInfo dynamic = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = sizeof(kDynamicStates) / sizeof(kDynamicStates[0]);
    dynamic.pDynamicStates = kDynamicStates;
    VkPipelineRenderingCreateInfo rendering = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    VkGraphicsPipelineCreateInfo info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &rendering};
    info.stageCount = 1;
    info.pStages = &stage;
    info.pDynamicState = &dynamic;
    info.layout = layout.layout;
    if (vertex)
    {
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        return CreateLibrary(device, info, VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT, cache);
    }
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    return CreateLibrary(device, info, VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT, cache);
}

VkPipeline CreateInterfaceLibrary(const Device &device, const PipelineLayout &layout, const PipelineDesc &desc,
                                  bool output, VkPipelineCache cache)
{
    VkPipelineDynamicStateCreateInfo dynamic = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = sizeof(kDynamicStates) / sizeof(kDynamicStates[0]);
    dynamic.pDynamicStates = kDynamicStates;
    VkGraphicsPipelineCreateInfo info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pDynamicState = &dynamic;
    info.layout = layout.layout;
    if (!output)
    {
        VkVertexInputBindingDescription bindings[kMaxVertexBindings];
        for (uint32_t i = 0; i < desc.bindingCount; ++i)
            bindings[i] = {i, 0, VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attributes[kMaxVertexAttributes];
        for (uint32_t i = 0; i < desc.attributeCount; ++i)
        {
            const VertexAttribute &a = desc.attributes[i];
            attributes[i] = {a.location, a.binding, a.format, a.offset};
        }
        VkPipelineVertexInputStateCreateInfo vertexInput = {
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInput.vertexBindingDescriptionCount = desc.bindingCount;
        vertexInput.pVertexBindingDescriptions = bindings;
        vertexInput.vertexAttributeDescriptionCount = desc.attributeCount;
        vertexInput.pVertexAttributeDescriptions = attributes;
        VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        inputAssembly.topology = desc.topology;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &inputAssembly;
        return CreateLibrary(device, info, VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT, cache);
    }
    VkPipelineMultisampleStateCreateInfo multisample = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState attachment = {};
    attachment.blendEnable = desc.blendEnable;
    attachment.srcColorBlendFactor = desc.srcColor;
    attachment.dstColorBlendFactor = desc.dstColor;
    attachment.colorBlendOp = desc.colorOp;
    attachment.srcAlphaBlendFactor = desc.srcAlpha;
    attachment.dstAlphaBlendFactor = desc.dstAlpha;
    attachment.alphaBlendOp = desc.alphaOp;
    attachment.colorWriteMask = desc.writeMask;
    VkPipelineColorBlendStateCreateInfo blend = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = desc.colorFormat != VK_FORMAT_UNDEFINED ? 1 : 0;
    blend.pAttachments = &attachment;
    VkPipelineRenderingCreateInfo rendering = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = desc.colorFormat != VK_FORMAT_UNDEFINED ? 1 : 0;
    rendering.pColorAttachmentFormats = &desc.colorFormat;
    rendering.depthAttachmentFormat = desc.depthFormat;
    if (desc.depthFormat == VK_FORMAT_D24_UNORM_S8_UINT || desc.depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT)
        rendering.stencilAttachmentFormat = desc.depthFormat;
    info.pNext = &rendering;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &blend;
    return CreateLibrary(device, info, VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT, cache);
}

VkPipeline LinkPipeline(const Device &device, const PipelineLayout &layout, const VkPipeline parts[4],
                        VkPipelineCache cache)
{
    VkPipelineLibraryCreateInfoKHR libraries = {VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR};
    libraries.libraryCount = 4;
    libraries.pLibraries = parts;
    VkGraphicsPipelineCreateInfo info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &libraries};
    info.layout = layout.layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateGraphicsPipelines(device.device, cache, 1, &info, nullptr, &pipeline);
    if (result != VK_SUCCESS)
    {
        nfs::Log("Vulkan: linking a pipeline failed (%s)", ResultName(result));
        return VK_NULL_HANDLE;
    }
    return pipeline;
}
} // namespace nfs::vk
