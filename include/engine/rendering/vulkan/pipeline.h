#pragma once

#include "engine/rendering/vulkan/device.h"
#include "engine/rendering/vulkan/shaders.h"

namespace nfs::vk
{
/**
 * The layout every translated shader uses: set 0 holds the VS and PS constants as dynamic uniform buffers (a draw only
 * gives their offsets in the frame's ring), set 1 is a push-descriptor set with a combined image sampler per sampler
 * stage, and the D3D9State push constants. The translator puts every binding in set 0; ShaderModule moves the samplers
 * to set 1.
 */
struct PipelineLayout
{
    VkDescriptorSetLayout constantsLayout;
    VkDescriptorSetLayout samplersLayout;
    VkDescriptorSetLayout sceneLayout;
    VkPipelineLayout layout;
};

constexpr uint32_t kConstantsSet = 0;
constexpr uint32_t kSamplersSet = 1;
/** Set 2, the scene (docs/SHADOWS.md): the scene constants (binding 0), the sun's shadow atlas with a comparison sampler
 * (binding 1) and with a point sampler (binding 2), and the lamps' baked light (bindings 3 to 6: the two levels' light
 * and direction volumes, engine/rendering/lights/lamps.h). Bound for every draw; only the rebuild's own shaders read it. */
constexpr uint32_t kSceneSet = 2;
constexpr uint32_t kSceneConstantsBinding = 0;
constexpr uint32_t kSunShadowCompareBinding = 1;
constexpr uint32_t kSunShadowDepthBinding = 2;
constexpr uint32_t kLampVolumeBinding = 3;
constexpr uint32_t kLampVolumes = 4;
constexpr uint32_t kHeadlightShadowBinding = kLampVolumeBinding + kLampVolumes;
constexpr uint32_t kSceneBindings = kHeadlightShadowBinding + 1;
constexpr uint32_t kSceneConstantsBytes = 8192;
/** Bytes each constant buffer descriptor covers: the vertex shader's 256 registers (the pixel shader has 224). */
constexpr uint32_t kConstantsRange = 256 * 16;

bool CreatePipelineLayout(const Device &device, PipelineLayout &out);
void DestroyPipelineLayout(const Device &device, PipelineLayout &layout);
/** A pool for `sets` constants sets and `sets` scene sets. */
VkDescriptorPool CreateConstantsPool(const Device &device, uint32_t sets);
/** A scene set (set 2) whose constants are `constants`, from `pool`. */
VkDescriptorSet AllocateSceneSet(const Device &device, const PipelineLayout &layout, VkDescriptorPool pool,
                                 VkBuffer constants);
/** Points a scene set's two sun shadow bindings at `view` (in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL). */
void WriteSceneShadow(const Device &device, VkDescriptorSet set, VkImageView view, VkSampler compare,
                      VkSampler point);
/** Points a scene set's headlight shadow binding at `view` (a depth texture, read only). */
void WriteSceneHeadlightShadow(const Device &device, VkDescriptorSet set, VkImageView view, VkSampler compare);
/** Points a scene set's lamp volume bindings at `views` (kLampVolumes of them, read only), sampled with `sampler`. */
void WriteSceneLamps(const Device &device, VkDescriptorSet set, const VkImageView *views, VkSampler sampler);
/** A constants set (set 0) whose three buffers are `buffer`, from `pool`. */
VkDescriptorSet AllocateConstantsSet(const Device &device, const PipelineLayout &layout, VkDescriptorPool pool,
                                     VkBuffer buffer);

struct VertexAttribute
{
    uint8_t location;
    uint8_t binding;
    uint16_t offset;
    VkFormat format;
};

constexpr uint32_t kMaxVertexAttributes = 16;
constexpr uint32_t kMaxVertexBindings = 4;

/**
 * The state baked into a pipeline. Viewport, scissor, cull mode, front face, topology, depth and stencil tests, depth
 * bias, blend constants and vertex strides are dynamic state, set while recording.
 */
struct PipelineDesc
{
    int16_t vertexShader;
    int16_t pixelShader;
    uint8_t attributeCount;
    uint8_t bindingCount;
    VertexAttribute attributes[kMaxVertexAttributes];
    /** Only the class (points, lines, triangles) is fixed; the exact topology is dynamic. */
    VkPrimitiveTopology topology;
    VkBool32 blendEnable;
    VkBlendFactor srcColor;
    VkBlendFactor dstColor;
    VkBlendOp colorOp;
    VkBlendFactor srcAlpha;
    VkBlendFactor dstAlpha;
    VkBlendOp alphaOp;
    VkColorComponentFlags writeMask;
    VkFormat colorFormat;
    VkFormat depthFormat;
};

/** The shader module of kShaders[index], made on first use. */
VkShaderModule ShaderModule(const Device &device, uint32_t index);
void DestroyShaderModules(const Device &device);

VkPipeline CreatePipeline(const Device &device, const PipelineLayout &layout, const PipelineDesc &desc,
                          VkPipelineCache cache = VK_NULL_HANDLE);

/**
 * VK_EXT_graphics_pipeline_library: a pipeline is linked from four parts. The two shader parts (one per shader, where
 * all compiling happens) are made at start; the vertex input and fragment output parts carry no code and are made
 * when a draw first needs them; linking them is fast.
 */
VkPipeline CreateShaderLibrary(const Device &device, const PipelineLayout &layout, uint32_t shader,
                               VkPipelineCache cache);
/** The vertex input part (`output` false: attributes, bindings, topology) or the fragment output part (true: blending,
 * write mask, target formats) of `desc`. */
VkPipeline CreateInterfaceLibrary(const Device &device, const PipelineLayout &layout, const PipelineDesc &desc,
                                  bool output, VkPipelineCache cache);
/** Links vertex input, vertex shader, pixel shader and fragment output parts without link-time optimisation. */
VkPipeline LinkPipeline(const Device &device, const PipelineLayout &layout, const VkPipeline parts[4],
                        VkPipelineCache cache);

/** Sets every piece of dynamic state to D3D9's defaults (no culling, no depth or stencil test) for a target of the
 * given size, with the negative-height viewport that keeps D3D's clip space. */
void SetDefaultDynamicState(VkCommandBuffer cmd, uint32_t width, uint32_t height);
} // namespace nfs::vk
