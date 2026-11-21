#pragma once

#include "engine/rendering/vulkan/vk.h"

/**
 * The Direct3D 9 values the game's code and data use (texture formats, vertex declaration types, blend factors,
 * compare functions...) and what they are in Vulkan.
 */
namespace nfs::vk
{
struct FormatInfo
{
    VkFormat format;
    /** Bytes per texel, or per 4x4 block for block-compressed formats. */
    uint8_t bytes;
    bool compressed;
    bool depth;
    bool stencil;
    /** How the shader sees the texel (D3D9 fills missing channels with 1, or replicates luminance). */
    VkComponentMapping swizzle;
};

/** D3DFORMAT -> Vulkan; format VK_FORMAT_UNDEFINED when there is no equivalent. */
FormatInfo TextureFormat(uint32_t d3dFormat);
/** Bytes of one mip level of a texture of that format. */
uint32_t LevelSize(const FormatInfo &info, uint32_t width, uint32_t height);
/** Bytes between rows of 4x4 blocks (compressed) or of texels. */
uint32_t RowPitch(const FormatInfo &info, uint32_t width);

/** D3DDECLTYPE -> vertex attribute format (VK_FORMAT_UNDEFINED for D3DDECLTYPE_UNUSED). */
VkFormat VertexFormat(uint32_t declType);
uint32_t VertexFormatSize(uint32_t declType);

VkBlendFactor BlendFactor(uint32_t d3dBlend);
VkBlendOp BlendOp(uint32_t d3dBlendOp);
VkCompareOp CompareOp(uint32_t d3dCmpFunc);
VkStencilOp StencilOp(uint32_t d3dStencilOp);
VkSamplerAddressMode AddressMode(uint32_t d3dTextureAddress);
} // namespace nfs::vk
