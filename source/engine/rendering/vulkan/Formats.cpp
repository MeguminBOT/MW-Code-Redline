#include "engine/rendering/vulkan/formats.h"

namespace nfs::vk
{
namespace
{
const VkComponentSwizzle R = VK_COMPONENT_SWIZZLE_R;
const VkComponentSwizzle G = VK_COMPONENT_SWIZZLE_G;
const VkComponentSwizzle B = VK_COMPONENT_SWIZZLE_B;
const VkComponentSwizzle A = VK_COMPONENT_SWIZZLE_A;
const VkComponentSwizzle ONE = VK_COMPONENT_SWIZZLE_ONE;
const VkComponentSwizzle ZERO = VK_COMPONENT_SWIZZLE_ZERO;
const VkComponentMapping kIdentity = {R, G, B, A};

constexpr uint32_t FourCC(char a, char b, char c, char d)
{
    return static_cast<uint32_t>(a) | static_cast<uint32_t>(b) << 8 | static_cast<uint32_t>(c) << 16 |
           static_cast<uint32_t>(d) << 24;
}

FormatInfo Plain(VkFormat format, uint8_t bytes, VkComponentMapping swizzle = kIdentity)
{
    return {format, bytes, false, false, false, swizzle};
}

FormatInfo Block(VkFormat format, uint8_t bytes)
{
    return {format, bytes, true, false, false, kIdentity};
}

FormatInfo Depth(VkFormat format, uint8_t bytes, bool stencil)
{
    return {format, bytes, false, true, stencil, kIdentity};
}
} // namespace

FormatInfo TextureFormat(uint32_t d3dFormat)
{
    switch (d3dFormat)
    {
    case 21: // A8R8G8B8
        return Plain(VK_FORMAT_B8G8R8A8_UNORM, 4);
    case 22: // X8R8G8B8
        return Plain(VK_FORMAT_B8G8R8A8_UNORM, 4, {R, G, B, ONE});
    case 23: // R5G6B5
        return Plain(VK_FORMAT_R5G6B5_UNORM_PACK16, 2);
    case 24: // X1R5G5B5
        return Plain(VK_FORMAT_A1R5G5B5_UNORM_PACK16, 2, {R, G, B, ONE});
    case 25: // A1R5G5B5
        return Plain(VK_FORMAT_A1R5G5B5_UNORM_PACK16, 2);
    case 26: // A4R4G4B4: bits A R G B land in B4G4R4A4's B G R A
        return Plain(VK_FORMAT_B4G4R4A4_UNORM_PACK16, 2, {G, R, A, B});
    case 30: // X4R4G4B4
        return Plain(VK_FORMAT_B4G4R4A4_UNORM_PACK16, 2, {G, R, A, ONE});
    case 28: // A8
        return Plain(VK_FORMAT_R8_UNORM, 1, {ZERO, ZERO, ZERO, R});
    case 31: // A2B10G10R10
        return Plain(VK_FORMAT_A2B10G10R10_UNORM_PACK32, 4);
    case 32: // A8B8G8R8
        return Plain(VK_FORMAT_R8G8B8A8_UNORM, 4);
    case 33: // X8B8G8R8
        return Plain(VK_FORMAT_R8G8B8A8_UNORM, 4, {R, G, B, ONE});
    case 34: // G16R16
        return Plain(VK_FORMAT_R16G16_UNORM, 4, {R, G, ONE, ONE});
    case 35: // A2R10G10B10
        return Plain(VK_FORMAT_A2R10G10B10_UNORM_PACK32, 4);
    case 36: // A16B16G16R16
        return Plain(VK_FORMAT_R16G16B16A16_UNORM, 8);
    case 50: // L8
        return Plain(VK_FORMAT_R8_UNORM, 1, {R, R, R, ONE});
    case 51: // A8L8
        return Plain(VK_FORMAT_R8G8_UNORM, 2, {R, R, R, G});
    case 60: // V8U8
        return Plain(VK_FORMAT_R8G8_SNORM, 2, {R, G, ONE, ONE});
    case 63: // Q8W8V8U8
        return Plain(VK_FORMAT_R8G8B8A8_SNORM, 4);
    case 64: // V16U16
        return Plain(VK_FORMAT_R16G16_SNORM, 4, {R, G, ONE, ONE});
    case 81: // L16
        return Plain(VK_FORMAT_R16_UNORM, 2, {R, R, R, ONE});
    case 111: // R16F
        return Plain(VK_FORMAT_R16_SFLOAT, 2, {R, ONE, ONE, ONE});
    case 112: // G16R16F
        return Plain(VK_FORMAT_R16G16_SFLOAT, 4, {R, G, ONE, ONE});
    case 113: // A16B16G16R16F
        return Plain(VK_FORMAT_R16G16B16A16_SFLOAT, 8);
    case 114: // R32F
        return Plain(VK_FORMAT_R32_SFLOAT, 4, {R, ONE, ONE, ONE});
    case 115: // G32R32F
        return Plain(VK_FORMAT_R32G32_SFLOAT, 8, {R, G, ONE, ONE});
    case 116: // A32B32G32R32F
        return Plain(VK_FORMAT_R32G32B32A32_SFLOAT, 16);
    case FourCC('D', 'X', 'T', '1'):
        return Block(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 8);
    case FourCC('D', 'X', 'T', '2'):
    case FourCC('D', 'X', 'T', '3'):
        return Block(VK_FORMAT_BC2_UNORM_BLOCK, 16);
    case FourCC('D', 'X', 'T', '4'):
    case FourCC('D', 'X', 'T', '5'):
        return Block(VK_FORMAT_BC3_UNORM_BLOCK, 16);
    case 80: // D16
    case FourCC('D', 'F', '1', '6'):
        return Depth(VK_FORMAT_D16_UNORM, 2, false);
    case 71: // D32
    case 77: // D24X8
    case FourCC('D', 'F', '2', '4'):
        return Depth(VK_FORMAT_D32_SFLOAT, 4, false);
    case 75: // D24S8
    // INTZ: a depth-stencil buffer the shaders can read depth from (the Xbox 360 effects' depth of field and motion
    // blur mask); the views drawn into it use its stencil too.
    case FourCC('I', 'N', 'T', 'Z'):
        return Depth(VK_FORMAT_D24_UNORM_S8_UINT, 4, true);
    default:
        return {VK_FORMAT_UNDEFINED, 0, false, false, false, kIdentity};
    }
}

uint32_t LevelSize(const FormatInfo &info, uint32_t width, uint32_t height)
{
    if (info.compressed)
        return ((width + 3) / 4 ? (width + 3) / 4 : 1) * ((height + 3) / 4 ? (height + 3) / 4 : 1) * info.bytes;
    return (width ? width : 1) * (height ? height : 1) * info.bytes;
}

uint32_t RowPitch(const FormatInfo &info, uint32_t width)
{
    if (info.compressed)
        return ((width + 3) / 4 ? (width + 3) / 4 : 1) * info.bytes;
    return (width ? width : 1) * info.bytes;
}

VkFormat VertexFormat(uint32_t declType)
{
    switch (declType)
    {
    case 0:
        return VK_FORMAT_R32_SFLOAT;
    case 1:
        return VK_FORMAT_R32G32_SFLOAT;
    case 2:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case 3:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case 4: // D3DCOLOR: bytes B G R A
        return VK_FORMAT_B8G8R8A8_UNORM;
    case 5:
        return VK_FORMAT_R8G8B8A8_USCALED;
    case 6:
        return VK_FORMAT_R16G16_SSCALED;
    case 7:
        return VK_FORMAT_R16G16B16A16_SSCALED;
    case 8:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case 9:
        return VK_FORMAT_R16G16_SNORM;
    case 10:
        return VK_FORMAT_R16G16B16A16_SNORM;
    case 11:
        return VK_FORMAT_R16G16_UNORM;
    case 12:
        return VK_FORMAT_R16G16B16A16_UNORM;
    case 13:
        return VK_FORMAT_A2B10G10R10_USCALED_PACK32;
    case 14:
        return VK_FORMAT_A2B10G10R10_SNORM_PACK32;
    case 15:
        return VK_FORMAT_R16G16_SFLOAT;
    case 16:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    default:
        return VK_FORMAT_UNDEFINED;
    }
}

uint32_t VertexFormatSize(uint32_t declType)
{
    static const uint8_t kSizes[] = {4, 8, 12, 16, 4, 4, 4, 8, 4, 4, 8, 4, 8, 4, 4, 4, 8};
    return declType < sizeof(kSizes) ? kSizes[declType] : 0;
}

VkBlendFactor BlendFactor(uint32_t d3dBlend)
{
    switch (d3dBlend)
    {
    case 1:
        return VK_BLEND_FACTOR_ZERO;
    case 2:
        return VK_BLEND_FACTOR_ONE;
    case 3:
        return VK_BLEND_FACTOR_SRC_COLOR;
    case 4:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case 5:
    case 12: // BOTHSRCALPHA as a source factor
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case 6:
    case 13: // BOTHINVSRCALPHA as a source factor
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 7:
        return VK_BLEND_FACTOR_DST_ALPHA;
    case 8:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case 9:
        return VK_BLEND_FACTOR_DST_COLOR;
    case 10:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case 11:
        return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case 14:
        return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case 15:
        return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    default:
        return VK_BLEND_FACTOR_ONE;
    }
}

VkBlendOp BlendOp(uint32_t d3dBlendOp)
{
    switch (d3dBlendOp)
    {
    case 2:
        return VK_BLEND_OP_SUBTRACT;
    case 3:
        return VK_BLEND_OP_REVERSE_SUBTRACT;
    case 4:
        return VK_BLEND_OP_MIN;
    case 5:
        return VK_BLEND_OP_MAX;
    default:
        return VK_BLEND_OP_ADD;
    }
}

VkCompareOp CompareOp(uint32_t d3dCmpFunc)
{
    switch (d3dCmpFunc)
    {
    case 1:
        return VK_COMPARE_OP_NEVER;
    case 2:
        return VK_COMPARE_OP_LESS;
    case 3:
        return VK_COMPARE_OP_EQUAL;
    case 4:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case 5:
        return VK_COMPARE_OP_GREATER;
    case 6:
        return VK_COMPARE_OP_NOT_EQUAL;
    case 7:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    default:
        return VK_COMPARE_OP_ALWAYS;
    }
}

VkStencilOp StencilOp(uint32_t d3dStencilOp)
{
    switch (d3dStencilOp)
    {
    case 2:
        return VK_STENCIL_OP_ZERO;
    case 3:
        return VK_STENCIL_OP_REPLACE;
    case 4:
        return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case 5:
        return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case 6:
        return VK_STENCIL_OP_INVERT;
    case 7:
        return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case 8:
        return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    default:
        return VK_STENCIL_OP_KEEP;
    }
}

VkSamplerAddressMode AddressMode(uint32_t d3dTextureAddress)
{
    switch (d3dTextureAddress)
    {
    case 2:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case 3:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case 4:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case 5:
        return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    default:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}
} // namespace nfs::vk
