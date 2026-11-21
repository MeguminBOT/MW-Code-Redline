/**
 * The Vulkan renderer's Direct3D 9 state tracking and draws (engine/rendering/vulkan/renderer.h). State is kept as the game sets it;
 * at a draw the baked part picks a pipeline, the rest becomes dynamic state, push constants and pushed descriptors.
 */
#include <intrin.h>
#include <string.h>

#include "core/config.h"
#include "core/log.h"
#include "utils/simd.h"
#include "dev/profile.h"
#include "engine/rendering/vulkan/renderer.h"

namespace nfs::vk
{
namespace
{
enum : uint32_t
{
    RS_ZENABLE = 7,
    RS_FILLMODE = 8,
    RS_SHADEMODE = 9,
    RS_ZWRITEENABLE = 14,
    RS_ALPHATESTENABLE = 15,
    RS_LASTPIXEL = 16,
    RS_SRCBLEND = 19,
    RS_DESTBLEND = 20,
    RS_CULLMODE = 22,
    RS_ZFUNC = 23,
    RS_ALPHAREF = 24,
    RS_ALPHAFUNC = 25,
    RS_ALPHABLENDENABLE = 27,
    RS_STENCILENABLE = 52,
    RS_STENCILFAIL = 53,
    RS_STENCILZFAIL = 54,
    RS_STENCILPASS = 55,
    RS_STENCILFUNC = 56,
    RS_STENCILREF = 57,
    RS_STENCILMASK = 58,
    RS_STENCILWRITEMASK = 59,
    RS_TEXTUREFACTOR = 60,
    RS_CLIPPING = 136,
    RS_LIGHTING = 137,
    RS_POINTSIZE = 154,
    RS_COLORWRITEENABLE = 168,
    RS_BLENDOP = 171,
    RS_SCISSORTESTENABLE = 174,
    RS_SLOPESCALEDEPTHBIAS = 175,
    RS_TWOSIDEDSTENCILMODE = 185,
    RS_CCW_STENCILFAIL = 186,
    RS_CCW_STENCILZFAIL = 187,
    RS_CCW_STENCILPASS = 188,
    RS_CCW_STENCILFUNC = 189,
    RS_COLORWRITEENABLE1 = 190,
    RS_COLORWRITEENABLE2 = 191,
    RS_COLORWRITEENABLE3 = 192,
    RS_BLENDFACTOR = 193,
    RS_DEPTHBIAS = 195,
    RS_SEPARATEALPHABLENDENABLE = 206,
    RS_SRCBLENDALPHA = 207,
    RS_DESTBLENDALPHA = 208,
    RS_BLENDOPALPHA = 209,
};

enum : uint32_t
{
    SAMP_ADDRESSU = 1,
    SAMP_ADDRESSV = 2,
    SAMP_ADDRESSW = 3,
    SAMP_BORDERCOLOR = 4,
    SAMP_MAGFILTER = 5,
    SAMP_MINFILTER = 6,
    SAMP_MIPFILTER = 7,
    SAMP_MIPMAPLODBIAS = 8,
    SAMP_MAXMIPLEVEL = 9,
    SAMP_MAXANISOTROPY = 10,
};

const uint32_t kDefaultBinding = 3;
const uint32_t kMaxPipelines = 4096;
const uint32_t kMaxInterfaceLibraries = 1024;
const uint32_t kMaxSamplerObjects = 512;

/** HLSL attribute location of a D3D declaration usage (tools/dxso.py ATTRIBUTE_LOCATIONS), or -1. */
int AttributeLocation(uint32_t usage, uint32_t index)
{
    switch (usage)
    {
    case 0: // POSITION
        return index == 0 ? 0 : -1;
    case 3: // NORMAL
        return index == 0 ? 1 : -1;
    case 10: // COLOR
        return index < 2 ? 2 + static_cast<int>(index) : -1;
    case 5: // TEXCOORD
        return index < 8 ? 4 + static_cast<int>(index) : -1;
    case 6: // TANGENT
        return index == 0 ? 12 : -1;
    case 7: // BINORMAL
        return index == 0 ? 13 : -1;
    case 1: // BLENDWEIGHT
        return index == 0 ? 14 : -1;
    case 2: // BLENDINDICES
        return index == 0 ? 15 : -1;
    default:
        return -1;
    }
}

/** FNV-1a over whole words (the pipeline description is a multiple of four bytes). */
uint32_t Hash(const void *data, uint32_t bytes)
{
    const uint32_t *p = static_cast<const uint32_t *>(data);
    uint32_t h = 2166136261u;
    for (uint32_t i = 0, n = bytes / 4; i < n; ++i)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

static_assert(sizeof(PipelineDesc) % 4 == 0, "Hash reads whole words");

/** Stencil operations of one face packed into a word, to compare with what was recorded. */
uint32_t PackStencil(VkStencilOp fail, VkStencilOp pass, VkStencilOp depthFail, VkCompareOp compare)
{
    return static_cast<uint32_t>(fail) | static_cast<uint32_t>(pass) << 8 | static_cast<uint32_t>(depthFail) << 16 |
           static_cast<uint32_t>(compare) << 24;
}

enum DynamicSlot
{
    kDynCull,
    kDynDepthTest,
    kDynDepthWrite,
    kDynDepthCompare,
    kDynStencilTest,
    kDynStencilFront,
    kDynStencilBack,
    kDynStencilCompareMask,
    kDynStencilWriteMask,
    kDynStencilReference,
    kDynDepthBiasEnable,
    kDynTopology,
};

float AsFloat(uint32_t word)
{
    float f;
    memcpy(&f, &word, 4);
    return f;
}

VkPrimitiveTopology Topology(uint32_t primitiveType)
{
    switch (primitiveType)
    {
    case 1:
        return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case 2:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case 3:
        return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case 5:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case 6:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    default:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

/** The topology a pipeline is made with: only the class (points, lines, triangles) is fixed. */
VkPrimitiveTopology TopologyClass(uint32_t primitiveType)
{
    if (primitiveType == 1)
        return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    if (primitiveType == 2 || primitiveType == 3)
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

uint32_t VertexCount(uint32_t primitiveType, uint32_t primitives)
{
    switch (primitiveType)
    {
    case 1:
        return primitives;
    case 2:
        return primitives * 2;
    case 3:
        return primitives + 1;
    case 5:
    case 6:
        return primitives + 2;
    default:
        return primitives * 3;
    }
}

void ColorToFloats(uint32_t color, float out[4])
{
    out[0] = ((color >> 16) & 0xFF) / 255.0f;
    out[1] = ((color >> 8) & 0xFF) / 255.0f;
    out[2] = (color & 0xFF) / 255.0f;
    out[3] = (color >> 24) / 255.0f;
}
} // namespace

struct Renderer::PipelineEntry
{
    PipelineDesc desc;
    uint32_t hash;
    VkPipeline pipeline;
};

struct Renderer::InterfaceEntry
{
    PipelineDesc key;
    uint32_t hash;
    VkPipeline pipeline;
};

struct Renderer::SamplerEntry
{
    uint32_t key[10];
    VkSampler sampler;
};

void Renderer::ResetState()
{
    memset(render_, 0, sizeof(render_));
    render_[RS_ZENABLE] = 1;
    render_[RS_FILLMODE] = 3;
    render_[RS_SHADEMODE] = 2;
    render_[RS_ZWRITEENABLE] = 1;
    render_[RS_LASTPIXEL] = 1;
    render_[RS_SRCBLEND] = 2;
    render_[RS_DESTBLEND] = 1;
    render_[RS_CULLMODE] = 3;
    render_[RS_ZFUNC] = 4;
    render_[RS_ALPHAFUNC] = 8;
    render_[RS_STENCILFAIL] = render_[RS_STENCILZFAIL] = render_[RS_STENCILPASS] = 1;
    render_[RS_STENCILFUNC] = 8;
    render_[RS_STENCILMASK] = render_[RS_STENCILWRITEMASK] = 0xFFFFFFFF;
    render_[RS_TEXTUREFACTOR] = 0xFFFFFFFF;
    render_[RS_CLIPPING] = 1;
    render_[RS_LIGHTING] = 1;
    render_[RS_POINTSIZE] = 0x3F800000;
    render_[RS_COLORWRITEENABLE] = render_[RS_COLORWRITEENABLE1] = render_[RS_COLORWRITEENABLE2] =
        render_[RS_COLORWRITEENABLE3] = 0xF;
    render_[RS_BLENDOP] = 1;
    render_[RS_CCW_STENCILFAIL] = render_[RS_CCW_STENCILZFAIL] = render_[RS_CCW_STENCILPASS] = 1;
    render_[RS_CCW_STENCILFUNC] = 8;
    render_[RS_BLENDFACTOR] = 0xFFFFFFFF;
    render_[RS_SRCBLENDALPHA] = 2;
    render_[RS_DESTBLENDALPHA] = 1;
    render_[RS_BLENDOPALPHA] = 1;
    for (uint32_t s = 0; s < kMaxSamplers; ++s)
    {
        memset(sampler_[s], 0, sizeof(sampler_[s]));
        sampler_[s][SAMP_ADDRESSU] = sampler_[s][SAMP_ADDRESSV] = sampler_[s][SAMP_ADDRESSW] = 1;
        sampler_[s][SAMP_MAGFILTER] = sampler_[s][SAMP_MINFILTER] = 1;
        sampler_[s][SAMP_MAXANISOTROPY] = 1;
        textures_[s] = nullptr;
    }
    samplerDirty_ = ~0u;
    vertexShader_ = pixelShader_ = -1;
    vertexLayout_ = nullptr;
    fvf_ = 0;
    ++stateSerial_;
}

void Renderer::DestroyStateObjects()
{
    for (uint32_t i = 0; shaderLibraries_ && i < kShaderCount; ++i)
        if (shaderLibraries_[i])
            vkDestroyPipeline(device_.device, shaderLibraries_[i], nullptr);
    ::operator delete(shaderLibraries_);
    shaderLibraries_ = nullptr;
    for (uint32_t i = 0; interfaceLibraries_ && i < kMaxInterfaceLibraries; ++i)
        if (interfaceLibraries_[i].pipeline)
            vkDestroyPipeline(device_.device, interfaceLibraries_[i].pipeline, nullptr);
    ::operator delete(interfaceLibraries_);
    interfaceLibraries_ = nullptr;
    interfaceLibraryCount_ = 0;
    ClearPipelineKeys();
    memset(stageSamplers_, 0, sizeof(stageSamplers_));
    recorded_.valid = false;
    for (uint32_t i = 0; pipelines_ && i < pipelineCapacity_; ++i)
        if (pipelines_[i].pipeline)
            vkDestroyPipeline(device_.device, pipelines_[i].pipeline, nullptr);
    ::operator delete(pipelines_);
    pipelines_ = nullptr;
    pipelineCount_ = 0;
    for (uint32_t i = 0; i < samplerCount_; ++i)
        vkDestroySampler(device_.device, samplers_[i].sampler, nullptr);
    ::operator delete(samplers_);
    samplers_ = nullptr;
    samplerCount_ = 0;
}

void Renderer::SetRenderState(uint32_t state, uint32_t value)
{
    if (state < 256 && render_[state] != value)
    {
        render_[state] = value;
        ++stateSerial_;
    }
}

void Renderer::SetSamplerState(uint32_t stage, uint32_t state, uint32_t value)
{
    if (stage < kMaxSamplers && state < 14 && sampler_[stage][state] != value)
    {
        sampler_[stage][state] = value;
        samplerDirty_ |= 1u << stage;
    }
}

void Renderer::SetTexture(uint32_t stage, void *texture)
{
    if (stage < kMaxSamplers)
        textures_[stage] = static_cast<nfs::vk::Texture *>(texture);
}

void Renderer::SetShaders(int vertexShader, int pixelShader)
{
    if (vertexShader == vertexShader_ && pixelShader == pixelShader_)
        return;
    vertexShader_ = vertexShader;
    pixelShader_ = pixelShader;
    ++stateSerial_;
}

void Renderer::SetConstants(bool pixel, uint32_t first, uint32_t count, const float (*registers)[4])
{
    float(*target)[4] = pixel ? psConstants_ : vsConstants_;
    uint32_t limit = pixel ? 224 : 256;
    if (first >= limit)
        return;
    if (first + count > limit)
        count = limit - first;
    if (!nfs::StoreIfChanged(target[first], registers, count * 16))
        return;
    (pixel ? psDirty_ : vsDirty_) = true;
    if (!++constantsStamp_[pixel])
        constantsStamp_[pixel] = 1;
}

VertexLayout *Renderer::CreateVertexLayout(const void *elements)
{
    struct Element
    {
        uint16_t stream;
        uint16_t offset;
        uint8_t type;
        uint8_t method;
        uint8_t usage;
        uint8_t usageIndex;
    };
    VertexLayout *layout = static_cast<VertexLayout *>(::operator new(sizeof(VertexLayout)));
    memset(layout, 0, sizeof(VertexLayout));
    layout->vtable = kObjectVtable;
    layout->refs = 1;
    layout->kind = kObjectVertexLayout;
    for (const Element *e = static_cast<const Element *>(elements); e->stream != 0xFF; ++e)
    {
        int location = AttributeLocation(e->usage, e->usageIndex);
        VkFormat format = VertexFormat(e->type);
        if (location < 0 || format == VK_FORMAT_UNDEFINED || e->stream >= kMaxVertexBindings - 1 ||
            layout->attributeCount == kMaxVertexAttributes)
            continue;
        VertexAttribute &a = layout->attributes[layout->attributeCount++];
        a.location = static_cast<uint8_t>(location);
        a.binding = static_cast<uint8_t>(e->stream);
        a.offset = e->offset;
        a.format = format;
        if (e->stream + 1u > layout->streamCount)
            layout->streamCount = e->stream + 1u;
        uint32_t end = e->offset + VertexFormatSize(e->type);
        if (end > layout->streamSizes[e->stream])
            layout->streamSizes[e->stream] = end;
    }
    return layout;
}

void Renderer::ReleaseVertexLayout(VertexLayout *layout)
{
    if (!layout || --layout->refs)
        return;
    if (vertexLayout_ == layout)
        vertexLayout_ = nullptr;
    // A layout made later at the same address must not match a pipeline's key.
    ClearPipelineKeys();
    ++stateSerial_;
    ::operator delete(layout);
}

void Renderer::SetVertexLayout(const VertexLayout *layout)
{
    if (layout == vertexLayout_ && !fvf_)
        return;
    vertexLayout_ = layout;
    fvf_ = 0;
    ++stateSerial_;
}

void Renderer::SetFVF(uint32_t fvf)
{
    if (fvf != fvf_ || vertexLayout_ != &fvfLayout_)
    {
        memset(&fvfLayout_, 0, sizeof(fvfLayout_));
        uint32_t offset = 0;
        auto add = [&](int location, VkFormat format, uint32_t size) {
            if (location >= 0)
                fvfLayout_.attributes[fvfLayout_.attributeCount++] = {static_cast<uint8_t>(location), 0,
                                                                      static_cast<uint16_t>(offset), format};
            offset += size;
        };
        switch (fvf & 0xE)
        {
        case 0x2: // XYZ
            add(0, VK_FORMAT_R32G32B32_SFLOAT, 12);
            break;
        case 0x4: // XYZRHW (pre-transformed; only for the fixed function pipeline)
            add(0, VK_FORMAT_R32G32B32A32_SFLOAT, 16);
            break;
        default:
            break;
        }
        if (fvf & 0x10)
            add(1, VK_FORMAT_R32G32B32_SFLOAT, 12);
        if (fvf & 0x20)
            add(-1, VK_FORMAT_UNDEFINED, 4);
        if (fvf & 0x40)
            add(2, VK_FORMAT_B8G8R8A8_UNORM, 4);
        if (fvf & 0x80)
            add(3, VK_FORMAT_B8G8R8A8_UNORM, 4);
        uint32_t coords = (fvf >> 8) & 0xF;
        for (uint32_t i = 0; i < coords && i < 8; ++i)
        {
            static const VkFormat kFormats[4] = {VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT,
                                                 VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R32_SFLOAT};
            static const uint32_t kSizes[4] = {8, 12, 16, 4};
            uint32_t f = (fvf >> (16 + 2 * i)) & 3;
            add(4 + static_cast<int>(i), kFormats[f], kSizes[f]);
        }
        fvfLayout_.streamCount = 1;
        fvfLayout_.streamSizes[0] = offset;
        fvf_ = fvf;
        ++stateSerial_;
    }
    vertexLayout_ = &fvfLayout_;
}

void Renderer::SetRenderTarget(Object *color, Object *depth)
{
    uint32_t face = 0;
    if (color && color->kind == kObjectSurface)
    {
        face = static_cast<Surface *>(color)->face;
        color = static_cast<Surface *>(color)->texture;
    }
    if (depth && depth->kind == kObjectSurface)
        depth = static_cast<Surface *>(depth)->texture;
    SetRenderTarget(static_cast<nfs::vk::Texture *>(color), static_cast<nfs::vk::Texture *>(depth), face);
}

void Renderer::SetRenderTarget(nfs::vk::Texture *color, nfs::vk::Texture *depth, uint32_t colorFace)
{
    if (color != colorTarget_ || depth != depthTarget_ || colorFace != colorFace_)
    {
        EndRendering();
        colorTarget_ = color;
        depthTarget_ = depth;
        colorFace_ = colorFace;
    }
    // Like Direct3D 9, binding a render target resets the viewport and scissor to the whole target.
    if (Texture *area = color ? color : depth)
    {
        viewport_ = {0, 0, area->width, area->height, 0.0f, 1.0f};
        scissor_ = {{0, 0}, {area->width, area->height}};
    }
    ++stateSerial_;
}

void Renderer::SetViewport(const Viewport &viewport)
{
    if (memcmp(&viewport, &viewport_, sizeof(viewport)) == 0)
        return;
    viewport_ = viewport;
    ++stateSerial_;
}

void Renderer::SetScissor(int left, int top, int right, int bottom)
{
    const VkRect2D scissor = {{left, top},
                              {static_cast<uint32_t>(right > left ? right - left : 0),
                               static_cast<uint32_t>(bottom > top ? bottom - top : 0)}};
    if (memcmp(&scissor, &scissor_, sizeof(scissor)) == 0)
        return;
    scissor_ = scissor;
    ++stateSerial_;
}

void Renderer::Transition(nfs::vk::Texture *texture, VkImageLayout layout)
{
    if (!texture || texture->layout == layout)
        return;
    if (rendering_)
        EndRendering();
    VkPipelineStageFlags2 dstStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkAccessFlags2 dstAccess = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    CrumbBegin(7, texture->width << 16 | texture->height, static_cast<uint32_t>(texture->layout) << 16 | layout,
               reinterpret_cast<uint32_t>(texture));
    ImageBarrier(cmd_, texture->image.image, texture->image.desc.aspect | (texture->format.stencil
                                                                               ? VK_IMAGE_ASPECT_STENCIL_BIT
                                                                               : 0),
                 texture->layout, layout, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, dstStage,
                 dstAccess);
    CrumbEnd();
    texture->layout = layout;
}

void Renderer::EndRendering()
{
    if (rendering_)
    {
        vkCmdEndRendering(cmd_);
        rendering_ = false;
    }
}

bool Renderer::BeginRendering()
{
    if (rendering_)
        return true;
    Texture *area = AreaTarget();
    if (!area)
        return false;
    VkRenderingAttachmentInfo color = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (colorTarget_)
    {
        Transition(colorTarget_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        color.imageView = colorTarget_->cube ? colorTarget_->faceViews[colorFace_] : colorTarget_->image.view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = (pendingClear_ & 1) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue = pendingColor_;
    }
    if (depthTarget_)
        Transition(depthTarget_, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo depth = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (depthTarget_)
    {
        depth.imageView = depthTarget_->image.view;
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth.loadOp = (pendingClear_ & 2) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.clearValue = pendingDepth_;
    }
    pendingClear_ = 0;
    // A depth buffer smaller than the colour target (Direct3D 9 leaves that undefined) limits the area drawn: outside
    // it the depth writes would land past the end of the depth image.
    renderWidth_ = area->width;
    renderHeight_ = area->height;
    if (depthTarget_ && (depthTarget_->width < renderWidth_ || depthTarget_->height < renderHeight_))
    {
        static int logged;
        if (logged++ < 16)
            nfs::Log("Vulkan: depth target %08x %ux%u is smaller than colour target %08x %ux%u (frame %u)",
                     reinterpret_cast<uint32_t>(depthTarget_), depthTarget_->width, depthTarget_->height,
                     reinterpret_cast<uint32_t>(area), area->width, area->height,
                     static_cast<uint32_t>(frameNumber_));
        renderWidth_ = depthTarget_->width < renderWidth_ ? depthTarget_->width : renderWidth_;
        renderHeight_ = depthTarget_->height < renderHeight_ ? depthTarget_->height : renderHeight_;
    }
    VkRenderingInfo info = {VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, {renderWidth_, renderHeight_}};
    info.layerCount = 1;
    info.colorAttachmentCount = colorTarget_ ? 1 : 0;
    info.pColorAttachments = colorTarget_ ? &color : nullptr;
    if (depthTarget_)
    {
        info.pDepthAttachment = &depth;
        if (depthTarget_->format.stencil)
            info.pStencilAttachment = &depth;
    }
    CrumbBegin(8, reinterpret_cast<uint32_t>(colorTarget_), reinterpret_cast<uint32_t>(depthTarget_),
               area->width << 16 | area->height);
    vkCmdBeginRendering(cmd_, &info);
    CrumbEnd();
    rendering_ = true;
    boundPipeline_ = VK_NULL_HANDLE;
    return true;
}

bool Renderer::FullClear(uint32_t flags, uint32_t color, float z, uint32_t stencil)
{
    static const bool enabled = nfs::config::Int("Debug", "LoadOpClears", 1) != 0;
    Texture *area = AreaTarget();
    if (!enabled || !area)
        return false;
    const bool clearColor = (flags & 1) && colorTarget_;
    const bool clearDepth = (flags & 2) && depthTarget_;
    const bool clearStencil = (flags & 4) && depthTarget_ && depthTarget_->format.stencil;
    // Depth and stencil are one attachment here: both cleared, or neither.
    if (depthTarget_ && depthTarget_->format.stencil && clearDepth != clearStencil)
        return false;
    if (!clearColor && !clearDepth)
        return false;
    // The whole of the area the rendering covers, as BeginRendering works it out.
    uint32_t width = area->width, height = area->height;
    if (depthTarget_)
    {
        width = depthTarget_->width < width ? depthTarget_->width : width;
        height = depthTarget_->height < height ? depthTarget_->height : height;
    }
    int32_t x0 = static_cast<int32_t>(viewport_.x), y0 = static_cast<int32_t>(viewport_.y);
    int32_t x1 = x0 + static_cast<int32_t>(viewport_.width), y1 = y0 + static_cast<int32_t>(viewport_.height);
    if (render_[RS_SCISSORTESTENABLE])
    {
        x0 = x0 > scissor_.offset.x ? x0 : scissor_.offset.x;
        y0 = y0 > scissor_.offset.y ? y0 : scissor_.offset.y;
        const int32_t sx1 = scissor_.offset.x + static_cast<int32_t>(scissor_.extent.width);
        const int32_t sy1 = scissor_.offset.y + static_cast<int32_t>(scissor_.extent.height);
        x1 = x1 < sx1 ? x1 : sx1;
        y1 = y1 < sy1 ? y1 : sy1;
    }
    if (x0 > 0 || y0 > 0 || x1 < static_cast<int32_t>(width) || y1 < static_cast<int32_t>(height))
        return false;
    EndRendering();
    pendingClear_ = (clearColor ? 1u : 0u) | (clearDepth ? 2u : 0u);
    if (clearColor)
        ColorToFloats(color, pendingColor_.color.float32);
    if (clearDepth)
        pendingDepth_.depthStencil = {z, stencil};
    CrumbBegin(4, (clearColor ? 1u : 0u) + (clearDepth ? 1u : 0u), area->width << 16 | area->height,
               static_cast<uint32_t>(area->d3dFormat));
    const bool begun = BeginRendering();
    CrumbEnd();
    return begun;
}

void Renderer::Clear(uint32_t flags, uint32_t color, float z, uint32_t stencil)
{
    nfs::profile::TimerScope profile(nfs::profile::kTimerClear);
    if (nfs::profile::g_enabled)
        nfs::profile::Count(nfs::profile::kCounterClears, 1);
    if (!frameActive_)
        BeginFrame();
    if (FullClear(flags, color, z, stencil))
        return;
    if (!BeginRendering())
        return;
    VkClearAttachment attachments[2];
    uint32_t count = 0;
    if ((flags & 1) && colorTarget_)
    {
        attachments[count] = {};
        attachments[count].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        attachments[count].colorAttachment = 0;
        ColorToFloats(color, attachments[count].clearValue.color.float32);
        ++count;
    }
    VkImageAspectFlags aspects = 0;
    if ((flags & 2) && depthTarget_)
        aspects |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if ((flags & 4) && depthTarget_ && depthTarget_->format.stencil)
        aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
    if (aspects)
    {
        attachments[count] = {};
        attachments[count].aspectMask = aspects;
        attachments[count].clearValue.depthStencil = {z, stencil};
        ++count;
    }
    if (!count)
        return;
    int32_t x0 = static_cast<int32_t>(viewport_.x), y0 = static_cast<int32_t>(viewport_.y);
    int32_t x1 = x0 + static_cast<int32_t>(viewport_.width), y1 = y0 + static_cast<int32_t>(viewport_.height);
    if (render_[RS_SCISSORTESTENABLE])
    {
        x0 = x0 > scissor_.offset.x ? x0 : scissor_.offset.x;
        y0 = y0 > scissor_.offset.y ? y0 : scissor_.offset.y;
        int32_t sx1 = scissor_.offset.x + static_cast<int32_t>(scissor_.extent.width);
        int32_t sy1 = scissor_.offset.y + static_cast<int32_t>(scissor_.extent.height);
        x1 = x1 < sx1 ? x1 : sx1;
        y1 = y1 < sy1 ? y1 : sy1;
    }
    int32_t w = static_cast<int32_t>(renderWidth_), h = static_cast<int32_t>(renderHeight_);
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > w ? w : x1;
    y1 = y1 > h ? h : y1;
    if (x1 <= x0 || y1 <= y0)
        return;
    VkClearRect rect = {{{x0, y0}, {static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0)}}, 0, 1};
    CrumbBegin(4, count, AreaTarget()->width << 16 | AreaTarget()->height, static_cast<uint32_t>(AreaTarget()->d3dFormat));
    vkCmdClearAttachments(cmd_, count, attachments, 1, &rect);
    CrumbEnd();
}

bool Renderer::InsertPipeline(const PipelineDesc &desc, VkPipeline pipeline)
{
    if (!pipelines_)
    {
        pipelineCapacity_ = kMaxPipelines;
        pipelines_ = static_cast<PipelineEntry *>(::operator new(sizeof(PipelineEntry) * pipelineCapacity_));
        memset(pipelines_, 0, sizeof(PipelineEntry) * pipelineCapacity_);
    }
    const uint32_t hash = Hash(&desc, sizeof(desc));
    uint32_t slot = hash & (pipelineCapacity_ - 1);
    for (uint32_t probe = 0; probe < pipelineCapacity_; ++probe, slot = (slot + 1) & (pipelineCapacity_ - 1))
    {
        PipelineEntry &e = pipelines_[slot];
        if (!e.pipeline)
        {
            if (pipelineCount_ * 4 > pipelineCapacity_ * 3)
                return false;
            e.desc = desc;
            e.hash = hash;
            e.pipeline = pipeline;
            ++pipelineCount_;
            return true;
        }
        if (e.hash == hash && memcmp(&e.desc, &desc, sizeof(desc)) == 0)
            return false;
    }
    return false;
}

VkPipeline Renderer::Pipeline(const PipelineDesc &desc)
{
    if (!pipelines_)
    {
        pipelineCapacity_ = kMaxPipelines;
        pipelines_ = static_cast<PipelineEntry *>(::operator new(sizeof(PipelineEntry) * pipelineCapacity_));
        memset(pipelines_, 0, sizeof(PipelineEntry) * pipelineCapacity_);
    }
    uint32_t hash = Hash(&desc, sizeof(desc));
    uint32_t slot = hash & (pipelineCapacity_ - 1);
    for (uint32_t probe = 0; probe < pipelineCapacity_; ++probe, slot = (slot + 1) & (pipelineCapacity_ - 1))
    {
        PipelineEntry &e = pipelines_[slot];
        if (!e.pipeline)
        {
            if (pipelineCount_ * 4 > pipelineCapacity_ * 3)
            {
                nfs::Log("Vulkan: pipeline table full");
                return VK_NULL_HANDLE;
            }
            e.desc = desc;
            e.hash = hash;
            int64_t start, end;
            QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&start));
            e.pipeline = MakePipeline(desc);
            QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&end));
            ++cost_.pipelines;
            cost_.pipelineTicks += end - start;
            lastPipelineCreated_ = GetTickCount();
            if (!e.pipeline)
                return VK_NULL_HANDLE;
            ++pipelineCount_;
            if (stats_ && !shaderLibraries_)
                nfs::Log("Vulkan: pipeline compiled during play (vs %d, ps %d); it is added to vkpipelines.lst",
                         desc.vertexShader, desc.pixelShader);
            return e.pipeline;
        }
        if (e.hash == hash && memcmp(&e.desc, &desc, sizeof(desc)) == 0)
            return e.pipeline;
    }
    return VK_NULL_HANDLE;
}

VkSampler Renderer::Sampler(uint32_t stage, uint32_t levels)
{
    if (stageSamplers_[stage] && !(samplerDirty_ & (1u << stage)))
        return stageSamplers_[stage];
    samplerDirty_ &= ~(1u << stage);
    const uint32_t *s = sampler_[stage];
    uint32_t key[10] = {s[SAMP_ADDRESSU],    s[SAMP_ADDRESSV],     s[SAMP_ADDRESSW],     s[SAMP_BORDERCOLOR],
                        s[SAMP_MAGFILTER],   s[SAMP_MINFILTER],    s[SAMP_MIPFILTER],    s[SAMP_MIPMAPLODBIAS],
                        s[SAMP_MAXMIPLEVEL], s[SAMP_MAXANISOTROPY]};
    if (stageSamplers_[stage] && memcmp(stageSamplerKeys_[stage], key, sizeof(key)) == 0)
        return stageSamplers_[stage];
    memcpy(stageSamplerKeys_[stage], key, sizeof(key));
    if (!samplers_)
    {
        samplers_ = static_cast<SamplerEntry *>(::operator new(sizeof(SamplerEntry) * kMaxSamplerObjects));
        memset(samplers_, 0, sizeof(SamplerEntry) * kMaxSamplerObjects);
    }
    for (uint32_t i = 0; i < samplerCount_; ++i)
        if (memcmp(samplers_[i].key, key, sizeof(key)) == 0)
            return stageSamplers_[stage] = samplers_[i].sampler;
    if (samplerCount_ == kMaxSamplerObjects)
        return samplers_[0].sampler;

    VkSamplerCreateInfo info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = key[4] >= 2 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.minFilter = key[5] >= 2 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.mipmapMode = key[6] == 2 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = AddressMode(key[0]);
    info.addressModeV = AddressMode(key[1]);
    info.addressModeW = AddressMode(key[2]);
    info.mipLodBias = AsFloat(key[7]);
    if (key[4] == 3 || key[5] == 3)
    {
        info.anisotropyEnable = device_.caps.anisotropy ? VK_TRUE : VK_FALSE;
        float aniso = static_cast<float>(key[9] ? key[9] : 1);
        info.maxAnisotropy = aniso < device_.caps.maxAnisotropy ? aniso : device_.caps.maxAnisotropy;
    }
    info.minLod = static_cast<float>(key[8]);
    // MIPFILTER NONE samples only the largest level allowed (MAXMIPLEVEL).
    info.maxLod = key[6] == 0 ? static_cast<float>(key[8]) : VK_LOD_CLAMP_NONE;
    uint32_t border = key[3];
    info.borderColor = border == 0 ? VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK
                       : (border >> 24) == 0xFF && (border & 0xFFFFFF) == 0xFFFFFF ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
                                                                                    : VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    VkSampler sampler = VK_NULL_HANDLE;
    if (vkCreateSampler(device_.device, &info, nullptr, &sampler) != VK_SUCCESS)
        return samplerCount_ ? samplers_[0].sampler : VK_NULL_HANDLE;
    memcpy(samplers_[samplerCount_].key, key, sizeof(key));
    samplers_[samplerCount_++].sampler = sampler;
    return stageSamplers_[stage] = sampler;
}

bool Renderer::PrepareDraw(uint32_t primitiveType)
{
    ProfileScope profile(kProfilePrepareDraw);
    if (!frameActive_)
        BeginFrame();
    if (vertexShader_ < 0 || pixelShader_ < 0 || !vertexLayout_ || !AreaTarget())
    {
        ++statCounts_[vertexShader_ < 0 || pixelShader_ < 0 ? kStatNoShaders : !vertexLayout_ ? kStatNoLayout
                                                                                                : kStatNoTarget];
        return false;
    }
    const ShaderBlob &vs = kShaders[vertexShader_];
    const ShaderBlob &ps = kShaders[pixelShader_];
    {
        // Debug.SkipShader=NAME: draws with that shader left out, to find what draws something on screen.
        static int skip = -2;
        if (skip == -2)
        {
            char name[64];
            nfs::config::String("Debug", "SkipShader", "", name, sizeof(name));
            skip = -1;
            for (uint32_t i = 0; name[0] && i < kShaderCount; ++i)
                if (!lstrcmpiA(kShaders[i].name, name))
                    skip = static_cast<int>(i);
        }
        if (skip >= 0 && (vertexShader_ == skip || pixelShader_ == skip))
            return false;
    }
    if (shaderLog_)
        NoteShaders(vertexShader_, pixelShader_);
    if (shaderTiming_)
        TimeDraw(pixelShader_ * 3 + (colorTarget_ == backBuffer_ ? 0 : colorTarget_ && colorTarget_->cube ? 1 : 2));

    nfs::vk::Texture *bound[kMaxSamplers];
    static const bool blackDepth = HasArgument("-vkblackdepth");
    for (unsigned long mask = ps.samplerMask & ((1u << kMaxSamplers) - 1), stage; _BitScanForward(&stage, mask);
         mask &= mask - 1)
    {
        nfs::vk::Texture *t = textures_[stage];
        if (t && t->kind != kObjectTexture)
        {
            static int logged;
            if (logged++ < 8)
                nfs::Log("Vulkan: stage %u holds a freed texture (%08x); drawn with the default", stage,
                         reinterpret_cast<uint32_t>(t));
            textures_[stage] = t = nullptr;
        }
        SamplerType type = ps.TypeOf(stage);
        bool fits = t && t != colorTarget_ && t != depthTarget_ && (type == SamplerType::Cube) == t->cube &&
                    (type == SamplerType::Volume) == t->volume && !(blackDepth && t->format.depth);
        bound[stage] = fits ? t : defaultTextures_[static_cast<int>(type)];
        Transition(bound[stage], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    if (sunShadowAtlas_ && sunShadowAtlas_ != depthTarget_)
        Transition(sunShadowAtlas_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (headlightShadow_ && headlightShadow_ != depthTarget_)
        Transition(headlightShadow_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!BeginRendering())
        return false;

    const uint32_t topologyClass = static_cast<uint32_t>(TopologyClass(primitiveType));
    VkPipeline pipeline = lastPipeline_;
    uint32_t missing = lastMissing_;
    if (!pipeline || pipelineSerial_ != stateSerial_ || pipelineTopology_ != topologyClass)
    {
        const uint32_t key[16] = {static_cast<uint32_t>(vertexShader_),
                                  static_cast<uint32_t>(pixelShader_),
                                  reinterpret_cast<uint32_t>(vertexLayout_),
                                  fvf_,
                                  topologyClass,
                                  render_[RS_ALPHABLENDENABLE],
                                  render_[RS_SRCBLEND],
                                  render_[RS_DESTBLEND],
                                  render_[RS_BLENDOP],
                                  render_[RS_SEPARATEALPHABLENDENABLE],
                                  render_[RS_SRCBLENDALPHA],
                                  render_[RS_DESTBLENDALPHA],
                                  render_[RS_BLENDOPALPHA],
                                  depthPrepass_ == 1 ? 0u : render_[RS_COLORWRITEENABLE],
                                  colorTarget_ ? static_cast<uint32_t>(colorTarget_->image.desc.format) : 0u,
                                  depthTarget_ ? static_cast<uint32_t>(depthTarget_->image.desc.format) : 0u};
        if (!pipeline || memcmp(key, lastPipelineKey_, sizeof(key)) != 0)
        {
            uint32_t hash = 2166136261u;
            for (uint32_t k : key)
                hash = (hash ^ k) * 16777619u;
            PipelineKeyEntry &entry = pipelineKeys_[(hash ^ hash >> 15) & (kPipelineKeySlots - 1)];
            if (entry.pipeline && memcmp(entry.key, key, sizeof(key)) == 0)
            {
                pipeline = entry.pipeline;
                missing = entry.missing;
            }
            else
            {
                missing = MakePipelineDesc(primitiveType, vs);
                pipeline = lastPipeline_;
                if (!pipeline)
                {
                    ++statCounts_[kStatNoPipeline];
                    return false;
                }
                memcpy(entry.key, key, sizeof(key));
                entry.pipeline = pipeline;
                entry.missing = missing;
            }
            lastPipeline_ = pipeline;
            memcpy(lastPipelineKey_, key, sizeof(key));
            lastMissing_ = missing;
        }
        pipelineSerial_ = stateSerial_;
        pipelineTopology_ = topologyClass;
    }
    ++statCounts_[kStatDrawn];
    if (pipeline != boundPipeline_)
    {
        vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        boundPipeline_ = pipeline;
        if (stats_)
        {
            const uint32_t *k = lastPipelineKey_, *b = boundKey_;
            bindReasons_[0] += k[0] != b[0] || k[1] != b[1];
            bindReasons_[1] += k[2] != b[2] || k[3] != b[3];
            bindReasons_[2] += k[4] != b[4];
            bindReasons_[3] += memcmp(k + 5, b + 5, 8 * sizeof(uint32_t)) != 0;
            bindReasons_[4] += k[13] != b[13];
            bindReasons_[5] += k[14] != b[14] || k[15] != b[15];
            CountBind(static_cast<int>(boundKey_[1]), pixelShader_);
            memcpy(boundKey_, lastPipelineKey_, sizeof(boundKey_));
        }
        if (nfs::profile::g_enabled)
            nfs::profile::Count(nfs::profile::kCounterPipelineBinds, 1);
    }
    return Record(primitiveType, vs, ps, bound, missing);
}

namespace
{
struct BindCount
{
    uint32_t key;
    uint32_t count;
};
BindCount g_bindCounts[4096];
} // namespace

void Renderer::CountBind(int fromPixelShader, int toPixelShader)
{
    const uint32_t key = (static_cast<uint32_t>(fromPixelShader) & 0xFFFF) << 16 | (static_cast<uint32_t>(toPixelShader) & 0xFFFF);
    for (uint32_t slot = (key * 2654435761u) >> 20;; slot = (slot + 1) & 4095)
    {
        BindCount &b = g_bindCounts[slot];
        if (b.count && b.key != key)
            continue;
        b.key = key;
        ++b.count;
        return;
    }
}

void Renderer::LogBinds(uint32_t frames)
{
    if (!frames)
        return;
    char line[2048];
    int n = wsprintfA(line, "Vulkan pipeline binds a frame by pixel shader (from > to):");
    for (int shown = 0; shown < 24; ++shown)
    {
        BindCount *best = nullptr;
        for (BindCount &b : g_bindCounts)
            if (b.count && (!best || b.count > best->count))
                best = &b;
        if (!best)
            break;
        const int from = static_cast<int16_t>(best->key >> 16), to = static_cast<int16_t>(best->key & 0xFFFF);
        n += wsprintfA(line + n, " %s > %s %d;", from >= 0 ? kShaders[from].name : "-", to >= 0 ? kShaders[to].name : "-",
                       static_cast<int>(best->count / frames));
        best->count = 0;
        if (n > 900)
            break;
    }
    memset(g_bindCounts, 0, sizeof(g_bindCounts));
    nfs::Log("%s", line);
}

uint32_t Renderer::MakePipelineDesc(uint32_t primitiveType, const ShaderBlob &vs)
{
    PipelineDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.vertexShader = static_cast<int16_t>(vertexShader_);
    desc.pixelShader = static_cast<int16_t>(pixelShader_);
    uint32_t provided = 0;
    for (uint32_t i = 0; i < vertexLayout_->attributeCount; ++i)
    {
        const VertexAttribute &a = vertexLayout_->attributes[i];
        if (!(vs.inputMask & (1u << a.location)))
            continue;
        desc.attributes[desc.attributeCount++] = a;
        provided |= 1u << a.location;
    }
    desc.bindingCount = static_cast<uint8_t>(vertexLayout_->streamCount);
    uint32_t missing = vs.inputMask & ~provided;
    if (missing)
    {
        for (uint32_t loc = 0; loc < 16; ++loc)
            if (missing & (1u << loc))
                desc.attributes[desc.attributeCount++] = {static_cast<uint8_t>(loc), kDefaultBinding, 0,
                                                          VK_FORMAT_R32G32B32A32_SFLOAT};
        desc.bindingCount = kDefaultBinding + 1;
    }
    desc.topology = TopologyClass(primitiveType);
    if (render_[RS_ALPHABLENDENABLE])
    {
        uint32_t src = render_[RS_SRCBLEND], dst = render_[RS_DESTBLEND];
        if (src == 12)
            dst = 6;
        else if (src == 13)
            dst = 5;
        desc.blendEnable = VK_TRUE;
        desc.srcColor = BlendFactor(src);
        desc.dstColor = BlendFactor(dst);
        desc.colorOp = BlendOp(render_[RS_BLENDOP]);
        if (render_[RS_SEPARATEALPHABLENDENABLE])
        {
            desc.srcAlpha = BlendFactor(render_[RS_SRCBLENDALPHA]);
            desc.dstAlpha = BlendFactor(render_[RS_DESTBLENDALPHA]);
            desc.alphaOp = BlendOp(render_[RS_BLENDOPALPHA]);
        }
        else
        {
            desc.srcAlpha = desc.srcColor;
            desc.dstAlpha = desc.dstColor;
            desc.alphaOp = desc.colorOp;
        }
    }
    desc.writeMask = depthPrepass_ == 1 ? 0 : render_[RS_COLORWRITEENABLE] & 0xF;
    desc.colorFormat = colorTarget_ ? colorTarget_->image.desc.format : VK_FORMAT_UNDEFINED;
    desc.depthFormat = depthTarget_ ? depthTarget_->image.desc.format : VK_FORMAT_UNDEFINED;
    lastPipeline_ = Pipeline(desc);
    return missing;
}

void Renderer::ClearPipelineKeys()
{
    lastPipeline_ = VK_NULL_HANDLE;
    memset(pipelineKeys_, 0, sizeof(pipelineKeys_));
}

bool Renderer::Record(uint32_t primitiveType, const ShaderBlob &vs, const ShaderBlob &ps,
                      nfs::vk::Texture *const *bound, uint32_t missing)
{
    Recorded &rec = recorded_;
    const bool fresh = !rec.valid;
    if (fresh)
    {
        // State no draw changes: set once per command buffer.
        vkCmdSetFrontFace(cmd_, VK_FRONT_FACE_CLOCKWISE);
        vkCmdSetDepthBoundsTestEnable(cmd_, VK_FALSE);
        vkCmdSetRasterizerDiscardEnable(cmd_, VK_FALSE);
        vkCmdSetPrimitiveRestartEnable(cmd_, VK_FALSE);
        memset(&rec, 0, sizeof(rec));
        rec.valid = true;
    }
    const auto changed = [&](DynamicSlot slot, uint32_t value) {
        if (!fresh && rec.dynamic[slot] == value)
            return false;
        rec.dynamic[slot] = value;
        return true;
    };

    if (fresh || rec.dynamicSerial != stateSerial_)
        RecordDynamicState(fresh);
    const VkPrimitiveTopology topology = Topology(primitiveType);
    if (changed(kDynTopology, topology))
        vkCmdSetPrimitiveTopology(cmd_, topology);

    D3D9State state;
    state.pixelOffset[0] = viewport_.width ? 1.0f / viewport_.width : 0.0f;
    state.pixelOffset[1] = viewport_.height ? -1.0f / viewport_.height : 0.0f;
    state.alphaFunc = render_[RS_ALPHATESTENABLE] ? render_[RS_ALPHAFUNC] : 8;
    state.alphaRef = static_cast<float>(render_[RS_ALPHAREF] & 0xFF);
    state.bools = vsBools_ | psBools_ << 16;
    state.fade = drawFade_;
    state.shadowQuality = (colorTarget_ && colorTarget_->cube ? 1u : 0u) | (depthPrepass_ == 1 ? 2u : 0u);
    state.litParticles = drawLitParticles_ ? 1u : 0u;
    state.blend = static_cast<uint32_t>(drawBlend_);
    // Only the rebuild's own world shaders read it: other draws keep the last value pushed, not a new push.
    state.surface = ps.owned ? drawSurface_ : rec.push.surface;
    if (fresh || memcmp(&state, &rec.push, sizeof(state)) != 0)
    {
        vkCmdPushConstants(cmd_, layout_.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(state), &state);
        rec.push = state;
    }
    return RecordBindings(vs, ps, bound, missing, fresh);
}

/** The viewport, scissor and dynamic render states, for draws after a state change (Record). */
void Renderer::RecordDynamicState(bool fresh)
{
    Recorded &rec = recorded_;
    rec.dynamicSerial = stateSerial_;
    const auto changed = [&](DynamicSlot slot, uint32_t value) {
        if (!fresh && rec.dynamic[slot] == value)
            return false;
        rec.dynamic[slot] = value;
        return true;
    };
    VkViewport viewport = {static_cast<float>(viewport_.x), static_cast<float>(viewport_.y + viewport_.height),
                           static_cast<float>(viewport_.width), -static_cast<float>(viewport_.height),
                           viewport_.minZ, viewport_.maxZ};
    if (fresh || memcmp(&viewport, &rec.viewport, sizeof(viewport)) != 0)
    {
        vkCmdSetViewport(cmd_, 0, 1, &viewport);
        rec.viewport = viewport;
    }
    VkRect2D scissor = {{0, 0}, {renderWidth_, renderHeight_}};
    if (render_[RS_SCISSORTESTENABLE])
    {
        scissor = scissor_;
        const int32_t right = scissor.offset.x + static_cast<int32_t>(scissor.extent.width);
        const int32_t bottom = scissor.offset.y + static_cast<int32_t>(scissor.extent.height);
        if (right > static_cast<int32_t>(renderWidth_))
            scissor.extent.width = renderWidth_ > static_cast<uint32_t>(scissor.offset.x)
                                       ? renderWidth_ - static_cast<uint32_t>(scissor.offset.x) : 0;
        if (bottom > static_cast<int32_t>(renderHeight_))
            scissor.extent.height = renderHeight_ > static_cast<uint32_t>(scissor.offset.y)
                                        ? renderHeight_ - static_cast<uint32_t>(scissor.offset.y) : 0;
    }
    if (fresh || memcmp(&scissor, &rec.scissor, sizeof(scissor)) != 0)
    {
        vkCmdSetScissor(cmd_, 0, 1, &scissor);
        rec.scissor = scissor;
    }
    static const bool noCull = HasArgument("-vknocull"), noDepth = HasArgument("-vknodepth");
    const uint32_t cull = noCull ? 1 : render_[RS_CULLMODE];
    const VkCullModeFlags cullMode = cull == 2 ? VK_CULL_MODE_FRONT_BIT : cull == 3 ? VK_CULL_MODE_BACK_BIT
                                                                                     : VK_CULL_MODE_NONE;
    if (changed(kDynCull, cullMode))
        vkCmdSetCullMode(cmd_, cullMode);
    const bool depth = depthTarget_ && render_[RS_ZENABLE] && !noDepth;
    if (changed(kDynDepthTest, depth))
        vkCmdSetDepthTestEnable(cmd_, depth);
    const bool depthWrite = depth && render_[RS_ZWRITEENABLE] && depthPrepass_ != 2;
    if (changed(kDynDepthWrite, depthWrite))
        vkCmdSetDepthWriteEnable(cmd_, depthWrite);
    const VkCompareOp depthCompare = depthPrepass_ == 2 ? VK_COMPARE_OP_EQUAL : CompareOp(render_[RS_ZFUNC]);
    if (changed(kDynDepthCompare, depthCompare))
        vkCmdSetDepthCompareOp(cmd_, depthCompare);
    const bool stencil = depthTarget_ && depthTarget_->format.stencil && render_[RS_STENCILENABLE];
    if (changed(kDynStencilTest, stencil))
        vkCmdSetStencilTestEnable(cmd_, stencil);
    VkStencilOp frontFail = VK_STENCIL_OP_KEEP, frontPass = VK_STENCIL_OP_KEEP, frontDepthFail = VK_STENCIL_OP_KEEP;
    VkCompareOp frontCompare = VK_COMPARE_OP_ALWAYS;
    VkStencilOp backFail = VK_STENCIL_OP_KEEP, backPass = VK_STENCIL_OP_KEEP, backDepthFail = VK_STENCIL_OP_KEEP;
    VkCompareOp backCompare = VK_COMPARE_OP_ALWAYS;
    uint32_t compareMask = 0xFF, writeMask = 0xFF, reference = 0;
    if (stencil)
    {
        frontFail = backFail = StencilOp(render_[RS_STENCILFAIL]);
        frontPass = backPass = StencilOp(render_[RS_STENCILPASS]);
        frontDepthFail = backDepthFail = StencilOp(render_[RS_STENCILZFAIL]);
        frontCompare = backCompare = CompareOp(render_[RS_STENCILFUNC]);
        if (render_[RS_TWOSIDEDSTENCILMODE])
        {
            backFail = StencilOp(render_[RS_CCW_STENCILFAIL]);
            backPass = StencilOp(render_[RS_CCW_STENCILPASS]);
            backDepthFail = StencilOp(render_[RS_CCW_STENCILZFAIL]);
            backCompare = CompareOp(render_[RS_CCW_STENCILFUNC]);
        }
        compareMask = render_[RS_STENCILMASK] & 0xFF;
        writeMask = render_[RS_STENCILWRITEMASK] & 0xFF;
        reference = render_[RS_STENCILREF] & 0xFF;
    }
    if (changed(kDynStencilFront, PackStencil(frontFail, frontPass, frontDepthFail, frontCompare)))
        vkCmdSetStencilOp(cmd_, VK_STENCIL_FACE_FRONT_BIT, frontFail, frontPass, frontDepthFail, frontCompare);
    if (changed(kDynStencilBack, PackStencil(backFail, backPass, backDepthFail, backCompare)))
        vkCmdSetStencilOp(cmd_, VK_STENCIL_FACE_BACK_BIT, backFail, backPass, backDepthFail, backCompare);
    if (changed(kDynStencilCompareMask, compareMask))
        vkCmdSetStencilCompareMask(cmd_, VK_STENCIL_FACE_FRONT_AND_BACK, compareMask);
    if (changed(kDynStencilWriteMask, writeMask))
        vkCmdSetStencilWriteMask(cmd_, VK_STENCIL_FACE_FRONT_AND_BACK, writeMask);
    if (changed(kDynStencilReference, reference))
        vkCmdSetStencilReference(cmd_, VK_STENCIL_FACE_FRONT_AND_BACK, reference);
    const float bias = AsFloat(render_[RS_DEPTHBIAS]), slope = AsFloat(render_[RS_SLOPESCALEDEPTHBIAS]);
    if (changed(kDynDepthBiasEnable, depthTarget_ && (bias != 0.0f || slope != 0.0f)))
        vkCmdSetDepthBiasEnable(cmd_, rec.dynamic[kDynDepthBiasEnable]);
    // Direct3D 9 adds the bias in depth units ([0, 1]); Vulkan counts it in steps of the depth format.
    float steps = 16777216.0f;
    if (depthTarget_ && depthTarget_->image.desc.format == VK_FORMAT_D16_UNORM)
        steps = 65536.0f;
    else if (depthTarget_ && (depthTarget_->image.desc.format == VK_FORMAT_D32_SFLOAT ||
                              depthTarget_->image.desc.format == VK_FORMAT_D32_SFLOAT_S8_UINT))
        steps = 8388608.0f;
    const float depthBias[2] = {bias * steps, slope};
    if (fresh || memcmp(depthBias, rec.depthBias, sizeof(depthBias)) != 0)
    {
        vkCmdSetDepthBias(cmd_, depthBias[0], 0.0f, depthBias[1]);
        memcpy(rec.depthBias, depthBias, sizeof(depthBias));
    }
    float blend[4];
    ColorToFloats(render_[RS_BLENDFACTOR], blend);
    if (fresh || memcmp(blend, rec.blend, sizeof(blend)) != 0)
    {
        vkCmdSetBlendConstants(cmd_, blend);
        memcpy(rec.blend, blend, sizeof(blend));
    }
}

/** The constants, descriptor sets, samplers and missing vertex inputs of a draw (Record). */
bool Renderer::RecordBindings(const ShaderBlob &vs, const ShaderBlob &ps, nfs::vk::Texture *const *bound,
                              uint32_t missing, bool fresh)
{
    Recorded &rec = recorded_;
    // The constants are offsets into the frame's ring (set 0's dynamic buffers); push descriptors keep what was pushed
    // before, so only changed samplers are pushed.
    const uint32_t alignment = constantsAlignment_;
    VkWriteDescriptorSet writes[kMaxSamplers];
    VkDescriptorImageInfo images[kMaxSamplers];
    uint32_t count = 0;
    uint32_t offsets[4] = {rec.constantOffsets[0], rec.constantOffsets[1], rec.constantOffsets[2],
                           rec.constantOffsets[3]};
    if (vs.owned && vs.constantCount)
    {
        // One block per instance, written for every draw: the registers, then the world rows.
        const uint32_t rows = vs.constantCount + 3;
        VkBuffer buffer;
        VkDeviceSize offset;
        float(*block)[4] = static_cast<float(*)[4]>(
            AllocateRing(instanceCount_ * rows * 16, alignment, buffer, offset, kInstanceBlocksRange));
        if (!block)
            return false;
        if (instanceBlocks_)
        {
            nfs::StreamRows(block, instanceBlocks_, instanceCount_ * rows);
        }
        else
        {
            nfs::StreamRows(block, vsConstants_, vs.constantCount);
            nfs::StreamRows(block + vs.constantCount, world_, 3);
        }
        offsets[3] = static_cast<uint32_t>(offset);
    }
    else if (vs.constantCount)
    {
        if (vsDirty_ || vsConstantsCount_ < vs.constantCount)
        {
            void *data = AllocateRing(vs.constantCount * 16, alignment, vsConstantsBuffer_, vsConstantsOffset_,
                                      kConstantsRange);
            if (!data)
                return false;
            nfs::StreamRows(data, vsConstants_, vs.constantCount);
            if (nfs::profile::g_enabled)
                nfs::profile::Count(nfs::profile::kCounterConstantRegisters, static_cast<int>(vs.constantCount));
            vsConstantsCount_ = vs.constantCount;
            vsDirty_ = false;
        }
        offsets[0] = static_cast<uint32_t>(vsConstantsOffset_);
    }
    if (ps.constantCount)
    {
        if (psDirty_ || psConstantsCount_ < ps.constantCount)
        {
            void *data = AllocateRing(ps.constantCount * 16, alignment, psConstantsBuffer_, psConstantsOffset_,
                                      kConstantsRange);
            if (!data)
                return false;
            nfs::StreamRows(data, psConstants_, ps.constantCount);
            if (nfs::profile::g_enabled)
                nfs::profile::Count(nfs::profile::kCounterConstantRegisters, static_cast<int>(ps.constantCount));
            psConstantsCount_ = ps.constantCount;
            psDirty_ = false;
        }
        offsets[1] = static_cast<uint32_t>(psConstantsOffset_);
    }
    if (!vs.owned && (worldDirty_ || fresh || !worldBuffer_))
    {
        void *data = AllocateRing(kDrawConstantsBytes, alignment, worldBuffer_, worldOffset_, kDrawConstantsBytes);
        if (!data)
            return false;
        nfs::StreamRows(data, world_, 3);
        worldDirty_ = false;
    }
    if (worldBuffer_)
        offsets[2] = static_cast<uint32_t>(worldOffset_);
    if (fresh || rec.sceneSet != frameScene_)
    {
        vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_.layout, kSceneSet, 1, &frameScene_, 0,
                                nullptr);
        rec.sceneSet = frameScene_;
    }
    const VkDescriptorSet constants = frameConstants_;
    if (fresh || rec.constantsSet != constants || offsets[0] != rec.constantOffsets[0] ||
        offsets[1] != rec.constantOffsets[1] || offsets[2] != rec.constantOffsets[2] ||
        offsets[3] != rec.constantOffsets[3])
    {
        vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_.layout, kConstantsSet, 1, &constants, 4,
                                offsets);
        rec.constantsSet = constants;
        memcpy(rec.constantOffsets, offsets, sizeof(offsets));
    }
    for (unsigned long mask = ps.samplerMask & ((1u << kMaxSamplers) - 1), stage; _BitScanForward(&stage, mask);
         mask &= mask - 1)
    {
        const VkSampler sampler = Sampler(stage, bound[stage]->levels);
        const VkImageView view = bound[stage]->image.view;
        if (!fresh && rec.samplers[stage] == sampler && rec.views[stage] == view)
            continue;
        rec.samplers[stage] = sampler;
        rec.views[stage] = view;
        images[stage] = {sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        writes[count] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[count].dstBinding = kFirstSamplerBinding + stage;
        writes[count].descriptorCount = 1;
        writes[count].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[count++].pImageInfo = &images[stage];
    }
    if (count)
    {
        vkCmdPushDescriptorSetKHR(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_.layout, kSamplersSet, count, writes);
        if (nfs::profile::g_enabled)
        {
            nfs::profile::Count(nfs::profile::kCounterDescriptorPushes, 1);
            nfs::profile::Count(nfs::profile::kCounterDescriptorWrites, static_cast<int>(count));
        }
    }
    if (missing)
    {
        static const float kZero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        VkBuffer buffer;
        VkDeviceSize offset;
        void *data = AllocateRing(16, 16, buffer, offset);
        if (!data)
            return false;
        memcpy(data, kZero, 16);
        VkDeviceSize stride = 0;
        vkCmdBindVertexBuffers2(cmd_, kDefaultBinding, 1, &buffer, &offset, nullptr, &stride);
        if (kDefaultBinding < kMaxVertexBindings)
            rec.vertexBuffers[kDefaultBinding] = VK_NULL_HANDLE;
    }
    return true;
}

void Renderer::DrawUP(uint32_t primitiveType, uint32_t primitiveCount, const void *vertices, uint32_t stride)
{
    if (!primitiveCount || !PrepareDraw(primitiveType))
        return;
    uint32_t vertexCount = VertexCount(primitiveType, primitiveCount);
    VkBuffer buffer;
    VkDeviceSize offset;
    void *data = AllocateRing(vertexCount * stride, 16, buffer, offset);
    if (!data)
        return;
    nfs::StreamBytes(data, vertices, vertexCount * stride);
    VkDeviceSize strides[kMaxVertexBindings] = {stride};
    VkBuffer buffers[kMaxVertexBindings] = {buffer};
    VkDeviceSize offsets[kMaxVertexBindings] = {offset};
    uint32_t bindings = vertexLayout_->streamCount ? vertexLayout_->streamCount : 1;
    for (uint32_t i = 1; i < bindings; ++i)
    {
        buffers[i] = buffer;
        offsets[i] = offset;
        strides[i] = stride;
    }
    BindVertexBuffers(bindings, buffers, offsets, strides);
    CrumbBegin(1, static_cast<uint32_t>(vertexShader_) | static_cast<uint32_t>(pixelShader_) << 16, vertexCount,
               AreaTarget()->width << 16 | AreaTarget()->height);
    vkCmdDraw(cmd_, vertexCount, 1, 0, 0);
    CrumbEnd();
    streams_[0] = nullptr;
}

void Renderer::DrawIndexedUP(uint32_t primitiveType, uint32_t minIndex, uint32_t vertexCount, uint32_t startIndex,
                             uint32_t primitiveCount, const void *vertices, uint32_t stride, GeometryBuffer *indices)
{
    if (!primitiveCount || !indices || !PrepareDraw(primitiveType))
        return;
    const uint32_t bytes = (minIndex + vertexCount) * stride;
    VkBuffer buffer;
    VkDeviceSize offset;
    void *data = AllocateRing(bytes, 16, buffer, offset);
    if (!data)
        return;
    nfs::StreamBytes(data, vertices, bytes);
    VkDeviceSize strides[kMaxVertexBindings], offsets[kMaxVertexBindings];
    VkBuffer buffers[kMaxVertexBindings];
    const uint32_t bindings = vertexLayout_->streamCount ? vertexLayout_->streamCount : 1;
    for (uint32_t i = 0; i < bindings; ++i)
    {
        buffers[i] = buffer;
        offsets[i] = offset;
        strides[i] = stride;
    }
    BindVertexBuffers(bindings, buffers, offsets, strides);
    BindIndexBuffer(BufferOf(indices));
    CrumbBegin(2, static_cast<uint32_t>(vertexShader_) | static_cast<uint32_t>(pixelShader_) << 16,
               VertexCount(primitiveType, primitiveCount), AreaTarget()->width << 16 | AreaTarget()->height);
    vkCmdDrawIndexed(cmd_, VertexCount(primitiveType, primitiveCount), 1, startIndex + indices->arenaOffset / 2, 0, 0);
    CrumbEnd();
}

void Renderer::SetStreamSource(uint32_t stream, GeometryBuffer *buffer, uint32_t offset, uint32_t stride)
{
    if (stream >= kMaxVertexBindings - 1)
        return;
    streams_[stream] = buffer;
    streamOffsets_[stream] = offset;
    streamStrides_[stream] = stride;
}

void Renderer::SetIndices(GeometryBuffer *buffer)
{
    indices_ = buffer;
}

void Renderer::DrawIndexed(uint32_t primitiveType, int baseVertex, uint32_t, uint32_t, uint32_t startIndex,
                           uint32_t primitiveCount)
{
    if (!indices_)
        ++statCounts_[kStatNoBuffers];
    if (!primitiveCount || !indices_ || !PrepareDraw(primitiveType))
        return;
    VkBuffer buffers[kMaxVertexBindings];
    VkDeviceSize offsets[kMaxVertexBindings], strides[kMaxVertexBindings];
    const uint32_t bindings = vertexLayout_->streamCount ? vertexLayout_->streamCount : 1;
    for (uint32_t i = 0; i < bindings; ++i)
    {
        GeometryBuffer *s = streams_[i];
        if (!s)
        {
            ++statCounts_[kStatNoBuffers];
            return;
        }
        const uint32_t stride = streamStrides_[i];
        // A static vertex buffer of one stream moves into an arena of its stride at its first draw; there the draw
        // reaches it through vertexOffset, so the arena stays bound.
        if (!s->arena && !s->moveTried && !s->cpu && bindings == 1 && stride)
            MoveToArena(s, stride);
        buffers[i] = BufferOf(s);
        const uint32_t offset = streamOffsets_[i];
        offsets[i] = s->arenaOffset + offset;
        strides[i] = stride;
        if (s->arena && bindings == 1 && arenas_[s->arena - 1].stride == stride &&
            (!offset || offset % stride == 0))
        {
            baseVertex += static_cast<int>(s->arenaVertex + (offset ? offset / stride : 0));
            offsets[i] = 0;
        }
    }
    BindVertexBuffers(bindings, buffers, offsets, strides);
    BindIndexBuffer(BufferOf(indices_));
    startIndex += indices_->arenaOffset / 2;
    CrumbBegin(3, static_cast<uint32_t>(vertexShader_) | static_cast<uint32_t>(pixelShader_) << 16,
               VertexCount(primitiveType, primitiveCount), AreaTarget()->width << 16 | AreaTarget()->height);
    vkCmdDrawIndexed(cmd_, VertexCount(primitiveType, primitiveCount), 1, startIndex, baseVertex, 0);
    CrumbEnd();
}

uint32_t Renderer::InstanceBlockRows() const
{
    if (vertexShader_ < 0)
        return 0;
    const ShaderBlob &vs = kShaders[vertexShader_];
    if (vertexLayout_ && vertexLayout_->streamCount > 1)
        return 0;
    return vs.owned && vs.constantCount ? vs.constantCount + 3u : 0u;
}

void Renderer::QueueBlockDraw(uint32_t startIndex, uint32_t primitiveCount)
{
    if (blockDrawCount_)
    {
        BlockDraw &last = blockDraws_[blockDrawCount_ - 1];
        if (last.stream == streams_[0] && last.indices == indices_ && last.startIndex == startIndex &&
            last.primitiveCount == primitiveCount && last.offset == streamOffsets_[0] &&
            last.stride == streamStrides_[0])
        {
            ++last.instances;
            return;
        }
    }
    if (blockDrawCount_ < kMaxBlockDraws)
        blockDraws_[blockDrawCount_++] = {streams_[0], streamOffsets_[0], streamStrides_[0], indices_, startIndex,
                                          primitiveCount, 1};
}

bool Renderer::DrawBlockRun(uint32_t primitiveType, const float (*blocks)[4], uint32_t rows)
{
    const uint32_t draws = blockDrawCount_;
    blockDrawCount_ = 0;
    uint32_t instances = 0;
    for (uint32_t i = 0; i < draws; ++i)
    {
        const BlockDraw &d = blockDraws_[i];
        if (!d.stream || !d.indices || !d.primitiveCount)
        {
            ++statCounts_[kStatNoBuffers];
            return false;
        }
        instances += d.instances;
    }
    static const bool logFailures = nfs::config::Int("Debug", "BlockLog", 0) != 0;
    if (!draws || !rows || rows != InstanceBlockRows() || instances * rows * 16 > kInstanceBlocksRange)
    {
        if (logFailures)
            nfs::Log("block run not drawn: %u draws, %u instances, %u rows (shader wants %u), %u bytes", draws, instances,
                     rows, InstanceBlockRows(), instances * rows * 16);
        return false;
    }
    instanceBlocks_ = blocks;
    instanceCount_ = instances;
    const bool prepared = PrepareDraw(primitiveType);
    instanceBlocks_ = nullptr;
    instanceCount_ = 1;
    if (!prepared)
    {
        if (logFailures)
            nfs::Log("block run not drawn: PrepareDraw failed (%u draws, %u instances)", draws, instances);
        return false;
    }
    uint32_t first = 0;
    for (uint32_t i = 0; i < draws; ++i)
    {
        const BlockDraw &d = blockDraws_[i];
        GeometryBuffer *s = d.stream;
        if (!s->arena && !s->moveTried && !s->cpu && d.stride)
            MoveToArena(s, d.stride);
        VkBuffer buffer = BufferOf(s);
        VkDeviceSize offset = s->arenaOffset + d.offset;
        const VkDeviceSize stride = d.stride;
        int baseVertex = 0;
        if (s->arena && arenas_[s->arena - 1].stride == d.stride && (!d.offset || d.offset % d.stride == 0))
        {
            baseVertex = static_cast<int>(s->arenaVertex + (d.offset ? d.offset / d.stride : 0));
            offset = 0;
        }
        BindVertexBuffers(1, &buffer, &offset, &stride);
        BindIndexBuffer(BufferOf(d.indices));
        const uint32_t vertices = VertexCount(primitiveType, d.primitiveCount);
        CrumbBegin(3, static_cast<uint32_t>(vertexShader_) | static_cast<uint32_t>(pixelShader_) << 16, vertices,
                   AreaTarget()->width << 16 | AreaTarget()->height);
        vkCmdDrawIndexed(cmd_, vertices, d.instances, d.startIndex + d.indices->arenaOffset / 2, baseVertex, first);
        CrumbEnd();
        first += d.instances;
    }
    if (nfs::profile::g_enabled)
        nfs::profile::Count(nfs::profile::kCounterBlockDraws, static_cast<int>(draws));
    return true;
}

void Renderer::BindVertexBuffers(uint32_t bindings, const VkBuffer *buffers, const VkDeviceSize *offsets,
                                 const VkDeviceSize *strides)
{
    Recorded &rec = recorded_;
    if (rec.vertexBindings == bindings && memcmp(rec.vertexBuffers, buffers, bindings * sizeof(VkBuffer)) == 0 &&
        memcmp(rec.vertexOffsets, offsets, bindings * sizeof(VkDeviceSize)) == 0 &&
        memcmp(rec.vertexStrides, strides, bindings * sizeof(VkDeviceSize)) == 0)
        return;
    vkCmdBindVertexBuffers2(cmd_, 0, bindings, buffers, offsets, nullptr, strides);
    if (nfs::profile::g_enabled)
        nfs::profile::Count(nfs::profile::kCounterVertexBinds, 1);
    rec.vertexBindings = bindings;
    memcpy(rec.vertexBuffers, buffers, bindings * sizeof(VkBuffer));
    memcpy(rec.vertexOffsets, offsets, bindings * sizeof(VkDeviceSize));
    memcpy(rec.vertexStrides, strides, bindings * sizeof(VkDeviceSize));
}

namespace
{
/** vkpipelines.lst / config/vk_pipelines.bin: a header, then the descriptions. */
struct PipelineListHeader
{
    uint32_t magic;
    uint32_t version;
    uint32_t descSize;
    uint32_t shaderHash;
    uint32_t count;
};
const uint32_t kPipelineListMagic = 0x4C50464E; // "NFPL"

/** Pipeline descriptions name shaders by index, so a list only fits the shader table it was made with. */
uint32_t ShaderTableHash()
{
    static uint32_t hash;
    if (!hash)
    {
        uint32_t h = 2166136261u ^ kShaderCount;
        for (uint32_t i = 0; i < kShaderCount; ++i)
        {
            h = (h ^ kShaders[i].spirvWords) * 16777619u;
            h = (h ^ Hash(kShaders[i].spirv, kShaders[i].spirvWords * 4)) * 16777619u;
        }
        hash = h ? h : 1;
    }
    return hash;
}

void PipelineListPath(char *path)
{
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/')
        --n;
    lstrcpyA(path + n, "vkpipelines.lst");
}

/** Pipelines made at start in more than this (ms) were compiled, not found in the cache (109 found take about 90 ms). */
const int kPrewarmCompiled = 400;

/** A shader's identity across builds: its SPIR-V's hash (kShaders' indices change whenever a shader is added). */
uint32_t ShaderContentHash(uint32_t index)
{
    static uint32_t *hashes;
    if (!hashes)
    {
        hashes = static_cast<uint32_t *>(::operator new(kShaderCount * sizeof(uint32_t)));
        for (uint32_t i = 0; i < kShaderCount; ++i)
            hashes[i] = Hash(kShaders[i].spirv, kShaders[i].spirvWords * 4);
    }
    return hashes[index];
}

int ShaderOfHash(uint32_t hash)
{
    for (uint32_t i = 0; i < kShaderCount; ++i)
        if (ShaderContentHash(i) == hash)
            return static_cast<int>(i);
    return -1;
}

/** A shader's name's hash: the same shader after an edit (its content's hash changed). */
uint32_t ShaderNameHash(uint32_t index)
{
    static uint32_t *hashes;
    if (!hashes)
    {
        hashes = static_cast<uint32_t *>(::operator new(kShaderCount * sizeof(uint32_t)));
        for (uint32_t i = 0; i < kShaderCount; ++i)
            hashes[i] = Hash(kShaders[i].name, static_cast<uint32_t>(lstrlenA(kShaders[i].name)));
    }
    return hashes[index];
}

int ShaderOfName(uint32_t hash)
{
    for (uint32_t i = 0; i < kShaderCount; ++i)
        if (ShaderNameHash(i) == hash)
            return static_cast<int>(i);
    return -1;
}

/** The shader of a version 3 entry: the one with that name and content; else the content's (renamed); else the name's
 * (edited). By content alone two shaders the same (nfs_nightsky_03 and _04) were one: the second's pipelines were
 * dropped from every list and compiled while driving, each run. */
int ShaderOf(uint32_t content, uint32_t name)
{
    for (uint32_t i = 0; i < kShaderCount; ++i)
        if (ShaderContentHash(i) == content && ShaderNameHash(i) == name)
            return static_cast<int>(i);
    const int byContent = ShaderOfHash(content);
    return byContent >= 0 ? byContent : ShaderOfName(name);
}

/** Words per pipeline after the descriptions: version 2 the two shaders' content hashes, version 3 their name hashes
 * too. */
uint32_t ListWords(uint32_t version)
{
    return version == 3 ? 4 : version == 2 ? 2 : 0;
}

/**
 * The descriptions of a list file appended to `out` (room for `room`), the shaders put at their indices in this build:
 * version 2 names each pipeline's shaders by their content's hash (after the descriptions), so a list made by another
 * build keeps every pipeline whose shaders are unchanged (version 1 named them by index, and a list of another shader
 * table was dropped whole: every new build compiled its pipelines again while driving). Version 3 adds their names'
 * hashes: a pipeline whose shader was edited (a change to lighting.hlsli changes most of them) is still made at start,
 * compiled there, instead of while driving. Descriptions already in `out` are not added twice. Returns how many were
 * added.
 */
uint32_t ReadPipelineList(const void *data, uint32_t size, PipelineDesc *out, uint32_t have, uint32_t room)
{
    const PipelineListHeader *header = static_cast<const PipelineListHeader *>(data);
    if (!data || size < sizeof(PipelineListHeader) || header->magic != kPipelineListMagic ||
        header->descSize != sizeof(PipelineDesc) ||
        size < sizeof(PipelineListHeader) + header->count * sizeof(PipelineDesc))
        return 0;
    const PipelineDesc *descs = reinterpret_cast<const PipelineDesc *>(header + 1);
    const uint32_t *hashes = nullptr;
    const uint32_t words = ListWords(header->version);
    if (words)
    {
        if (size < sizeof(PipelineListHeader) + header->count * (sizeof(PipelineDesc) + words * sizeof(uint32_t)))
            return 0;
        hashes = reinterpret_cast<const uint32_t *>(descs + header->count);
    }
    else if (header->version != 1 || header->shaderHash != ShaderTableHash())
        return 0;
    uint32_t added = 0;
    for (uint32_t i = 0; i < header->count && have + added < room; ++i)
    {
        PipelineDesc d = descs[i];
        if (hashes)
        {
            const uint32_t *h = hashes + i * words;
            const int vs = words == 4 ? ShaderOf(h[0], h[2]) : ShaderOfHash(h[0]);
            const int ps = words == 4 ? ShaderOf(h[1], h[3]) : ShaderOfHash(h[1]);
            if (vs < 0 || ps < 0 || kShaders[vs].stage != ShaderStage::Vertex || kShaders[ps].stage != ShaderStage::Pixel)
                continue;
            d.vertexShader = static_cast<int16_t>(vs);
            d.pixelShader = static_cast<int16_t>(ps);
        }
        bool seen = false;
        for (uint32_t k = 0; k < have + added && !seen; ++k)
            seen = memcmp(&out[k], &d, sizeof(d)) == 0;
        if (!seen)
            out[have + added++] = d;
    }
    return added;
}

/** A list file's bytes (null when there is none or it is too large); `size` set. */
void *ReadListFile(const char *path, uint32_t &size)
{
    size = 0;
    HANDLE handle = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return nullptr;
    void *file = nullptr;
    const DWORD bytes = GetFileSize(handle, nullptr);
    DWORD read = 0;
    if (bytes != INVALID_FILE_SIZE && bytes < (16u << 20))
    {
        file = ::operator new(bytes ? bytes : 1);
        if (!ReadFile(handle, file, bytes, &read, nullptr) || read != bytes)
        {
            ::operator delete(file);
            file = nullptr;
        }
        else
            size = bytes;
    }
    CloseHandle(handle);
    return file;
}

struct PrewarmJob
{
    const Device *device;
    const PipelineLayout *layout;
    VkPipelineCache cache;
    const PipelineDesc *descs;
    VkPipeline *pipelines;
    uint32_t count;
    volatile LONG next;
};

DWORD WINAPI PrewarmWorker(void *parameter)
{
    PrewarmJob &job = *static_cast<PrewarmJob *>(parameter);
    for (;;)
    {
        const LONG i = InterlockedIncrement(&job.next) - 1;
        if (i < 0 || static_cast<uint32_t>(i) >= job.count)
            return 0;
        job.pipelines[i] = CreatePipeline(*job.device, *job.layout, job.descs[i], job.cache);
    }
}
} // namespace

namespace
{
struct LibraryJob
{
    const Device *device;
    const PipelineLayout *layout;
    VkPipelineCache cache;
    VkPipeline *libraries;
    volatile LONG next;
};

DWORD WINAPI LibraryWorker(void *parameter)
{
    LibraryJob &job = *static_cast<LibraryJob *>(parameter);
    for (;;)
    {
        const LONG i = InterlockedIncrement(&job.next) - 1;
        if (i < 0 || static_cast<uint32_t>(i) >= kShaderCount)
            return 0;
        job.libraries[i] = CreateShaderLibrary(*job.device, *job.layout, static_cast<uint32_t>(i), job.cache);
    }
}

/** Runs `worker` on up to eight threads (this one included) and returns how many took part. */
uint32_t RunWorkers(LPTHREAD_START_ROUTINE worker, void *job, uint32_t items)
{
    SYSTEM_INFO system;
    GetSystemInfo(&system);
    uint32_t threads = system.dwNumberOfProcessors > 1 ? system.dwNumberOfProcessors - 1 : 1;
    if (threads > 7)
        threads = 7;
    if (threads > items)
        threads = items;
    HANDLE workers[7];
    uint32_t started = 0;
    for (uint32_t t = 0; t < threads; ++t)
        if ((workers[started] = CreateThread(nullptr, 0, worker, job, 0, nullptr)) != nullptr)
            ++started;
    worker(job);
    if (started)
        WaitForMultipleObjects(started, workers, TRUE, INFINITE);
    for (uint32_t t = 0; t < started; ++t)
        CloseHandle(workers[t]);
    return started + 1;
}
} // namespace

VkPipeline Renderer::ShaderLibrary(int shader)
{
    if (!shaderLibraries_ || shader < 0 || static_cast<uint32_t>(shader) >= kShaderCount)
        return VK_NULL_HANDLE;
    VkPipeline &library = shaderLibraries_[shader];
    if (!library)
        library = CreateShaderLibrary(device_, layout_, static_cast<uint32_t>(shader), pipelineCache_);
    return library;
}

VkPipeline Renderer::InterfaceLibrary(const PipelineDesc &desc, bool output)
{
    // The part of the description the library depends on; the rest stays zero.
    PipelineDesc key;
    memset(&key, 0, sizeof(key));
    key.vertexShader = output ? 1 : 0;
    if (output)
    {
        key.blendEnable = desc.blendEnable;
        key.srcColor = desc.srcColor;
        key.dstColor = desc.dstColor;
        key.colorOp = desc.colorOp;
        key.srcAlpha = desc.srcAlpha;
        key.dstAlpha = desc.dstAlpha;
        key.alphaOp = desc.alphaOp;
        key.writeMask = desc.writeMask;
        key.colorFormat = desc.colorFormat;
        key.depthFormat = desc.depthFormat;
    }
    else
    {
        key.attributeCount = desc.attributeCount;
        key.bindingCount = desc.bindingCount;
        memcpy(key.attributes, desc.attributes, sizeof(key.attributes));
        key.topology = desc.topology;
    }
    if (!interfaceLibraries_)
    {
        interfaceLibraries_ = static_cast<InterfaceEntry *>(::operator new(sizeof(InterfaceEntry) *
                                                                            kMaxInterfaceLibraries));
        memset(interfaceLibraries_, 0, sizeof(InterfaceEntry) * kMaxInterfaceLibraries);
    }
    const uint32_t hash = Hash(&key, sizeof(key));
    uint32_t slot = hash & (kMaxInterfaceLibraries - 1);
    for (uint32_t probe = 0; probe < kMaxInterfaceLibraries; ++probe, slot = (slot + 1) & (kMaxInterfaceLibraries - 1))
    {
        InterfaceEntry &e = interfaceLibraries_[slot];
        if (e.pipeline && e.hash == hash && memcmp(&e.key, &key, sizeof(key)) == 0)
            return e.pipeline;
        if (e.pipeline)
            continue;
        if (interfaceLibraryCount_ * 4 > kMaxInterfaceLibraries * 3)
            return VK_NULL_HANDLE;
        e.pipeline = CreateInterfaceLibrary(device_, layout_, desc, output, pipelineCache_);
        if (!e.pipeline)
            return VK_NULL_HANDLE;
        e.key = key;
        e.hash = hash;
        ++interfaceLibraryCount_;
        return e.pipeline;
    }
    return VK_NULL_HANDLE;
}

VkPipeline Renderer::MakePipeline(const PipelineDesc &desc)
{
    if (shaderLibraries_)
    {
        const VkPipeline parts[4] = {InterfaceLibrary(desc, false), ShaderLibrary(desc.vertexShader),
                                     ShaderLibrary(desc.pixelShader), InterfaceLibrary(desc, true)};
        if (parts[0] && parts[1] && parts[2] && parts[3])
            if (VkPipeline pipeline = LinkPipeline(device_, layout_, parts, pipelineCache_))
                return pipeline;
    }
    return CreatePipeline(device_, layout_, desc, pipelineCache_);
}

void Renderer::PrewarmPipelines()
{
    int64_t start, end, frequency;
    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&start));
    QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER *>(&frequency));
    if (device_.caps.pipelineLibrary)
    {
        // Every shader's library: the modules first (the workers only read them), then the compiles in parallel.
        for (uint32_t i = 0; i < kShaderCount; ++i)
            ShaderModule(device_, i);
        shaderLibraries_ = static_cast<VkPipeline *>(::operator new(kShaderCount * sizeof(VkPipeline)));
        memset(shaderLibraries_, 0, kShaderCount * sizeof(VkPipeline));
        LibraryJob job = {&device_, &layout_, pipelineCache_, shaderLibraries_, 0};
        const uint32_t threads = RunWorkers(LibraryWorker, &job, kShaderCount);
        uint32_t made = 0;
        for (uint32_t i = 0; i < kShaderCount; ++i)
            made += shaderLibraries_[i] ? 1 : 0;
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&end));
        const int ms = static_cast<int>(static_cast<double>(end - start) * 1000.0 / static_cast<double>(frequency));
        cacheDirty_ = ms > kPrewarmCompiled;
        nfs::Log("Vulkan: %u of %u shaders compiled as pipeline libraries at start in %d ms on %u threads", made,
                 kShaderCount, ms, threads);
        lastPipelineCreated_ = GetTickCount();
        return;
    }
    // This install's list (what was drawn here before) and the list shipped beside the exe (vkpipelines_base.lst: the
    // pipelines of the whole game, gathered by tours, races and the front end), so a first start or a new build has
    // them made before anything is drawn instead of compiling them while driving.
    char path[MAX_PATH], basePath[MAX_PATH];
    PipelineListPath(path);
    lstrcpyA(basePath, path);
    lstrcpyA(basePath + lstrlenA(basePath) - 4, "_base.lst");
    uint32_t sizes[2];
    void *files[2] = {ReadListFile(path, sizes[0]), ReadListFile(basePath, sizes[1])};
    uint32_t room = 0;
    for (int f = 0; f < 2; ++f)
        if (files[f])
            room += sizes[f] / sizeof(PipelineDesc) + 1;
    PipelineDesc *saved = static_cast<PipelineDesc *>(::operator new(room * sizeof(PipelineDesc) + 1));
    uint32_t savedCount = 0, fromList[2] = {};
    for (int f = 0; f < 2; ++f)
    {
        fromList[f] = ReadPipelineList(files[f], sizes[f], saved, savedCount, room);
        savedCount += fromList[f];
        ::operator delete(files[f]);
    }
    if (!savedCount)
    {
        ::operator delete(saved);
        return;
    }
    nfs::Log("Vulkan: pipeline lists: %u from vkpipelines.lst, %u more from vkpipelines_base.lst", fromList[0],
             fromList[1]);
    PipelineDesc *descs = static_cast<PipelineDesc *>(::operator new(savedCount * sizeof(PipelineDesc) + 1));
    uint32_t count = 0;
    for (uint32_t i = 0; i < savedCount; ++i)
    {
        const PipelineDesc &d = saved[i];
        if (d.vertexShader < 0 || d.pixelShader < 0 || static_cast<uint32_t>(d.vertexShader) >= kShaderCount ||
            static_cast<uint32_t>(d.pixelShader) >= kShaderCount)
            continue;
        ShaderModule(device_, static_cast<uint32_t>(d.vertexShader));
        ShaderModule(device_, static_cast<uint32_t>(d.pixelShader));
        descs[count++] = d;
    }
    ::operator delete(saved);
    VkPipeline *pipelines = static_cast<VkPipeline *>(::operator new(count * sizeof(VkPipeline) + 1));
    memset(pipelines, 0, count * sizeof(VkPipeline));
    PrewarmJob job = {&device_, &layout_, pipelineCache_, descs, pipelines, count, 0};
    const uint32_t threads = count ? RunWorkers(PrewarmWorker, &job, count) : 0;
    uint32_t made = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (!pipelines[i])
            continue;
        if (InsertPipeline(descs[i], pipelines[i]))
            ++made;
        else
            vkDestroyPipeline(device_.device, pipelines[i], nullptr);
    }
    ::operator delete(pipelines);
    ::operator delete(descs);
    lastPipelineCreated_ = GetTickCount();
    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&end));
    const int ms = static_cast<int>(static_cast<double>(end - start) * 1000.0 / static_cast<double>(frequency));
    // A start this slow compiled pipelines the cache did not have: it is written when the game quits.
    cacheDirty_ = ms > kPrewarmCompiled;
    nfs::Log("Vulkan: %u pipelines from vkpipelines.lst made at start in %d ms on %u threads", made, ms, threads);
}

/** Writes every pipeline description in the table to vkpipelines.lst (the shipped list is made from these). */
void Renderer::SavePipelineList()
{
    if (!pipelines_ || !pipelineCount_)
        return;
    const uint32_t words = ListWords(3);
    const uint32_t bytes = sizeof(PipelineListHeader) + pipelineCount_ * (sizeof(PipelineDesc) + words * sizeof(uint32_t));
    unsigned char *data = static_cast<unsigned char *>(::operator new(bytes));
    PipelineListHeader *header = reinterpret_cast<PipelineListHeader *>(data);
    *header = {kPipelineListMagic, 3, sizeof(PipelineDesc), ShaderTableHash(), 0};
    PipelineDesc *out = reinterpret_cast<PipelineDesc *>(header + 1);
    for (uint32_t i = 0; i < pipelineCapacity_ && header->count < pipelineCount_; ++i)
        if (pipelines_[i].pipeline)
            out[header->count++] = pipelines_[i].desc;
    // Version 3: each pipeline's two shaders by their content's hash, then by their name's, after the descriptions
    // (ReadPipelineList).
    uint32_t *hashes = reinterpret_cast<uint32_t *>(out + header->count);
    for (uint32_t i = 0; i < header->count; ++i)
    {
        const uint32_t vs = static_cast<uint32_t>(out[i].vertexShader), ps = static_cast<uint32_t>(out[i].pixelShader);
        hashes[i * words] = ShaderContentHash(vs);
        hashes[i * words + 1] = ShaderContentHash(ps);
        hashes[i * words + 2] = ShaderNameHash(vs);
        hashes[i * words + 3] = ShaderNameHash(ps);
    }
    char path[MAX_PATH], temp[MAX_PATH];
    PipelineListPath(path);
    lstrcpyA(temp, path);
    lstrcatA(temp, ".tmp");
    HANDLE file = CreateFileA(temp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        const DWORD size = sizeof(PipelineListHeader) + header->count * (sizeof(PipelineDesc) + words * sizeof(uint32_t));
        DWORD written = 0;
        const BOOL ok = WriteFile(file, data, size, &written, nullptr) && written == size;
        CloseHandle(file);
        if (ok)
            MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING);
    }
    ::operator delete(data);
}

void Renderer::BindIndexBuffer(VkBuffer buffer)
{
    if (recorded_.indexBuffer == buffer)
        return;
    vkCmdBindIndexBuffer(cmd_, buffer, 0, VK_INDEX_TYPE_UINT16);
    recorded_.indexBuffer = buffer;
}
} // namespace nfs::vk
