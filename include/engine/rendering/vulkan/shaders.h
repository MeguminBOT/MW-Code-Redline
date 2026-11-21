#pragma once

#include <stdint.h>

/**
 * The game's shaders for the Vulkan renderer: every vertex and pixel shader in the 31 D3D9 effects (the IDI_*_FX
 * resources), translated to HLSL and compiled to SPIR-V by tools/fxconv.py at build time (build/gen/shaders.cpp).
 * The conventions the translation follows (descriptor bindings, attribute and varying locations, push constants) are
 * described at the top of the HLSL section in tools/dxso.py.
 */
namespace nfs::vk
{
enum class ShaderStage : uint8_t
{
    Vertex,
    Pixel,
};

enum class SamplerType : uint8_t
{
    Texture2D,
    Cube,
    Volume,
};

/** Push constants every translated shader declares (struct D3D9State in the HLSL). */
struct D3D9State
{
    /** (1 / viewport width, -1 / viewport height): moves geometry by half a pixel to D3D9's pixel centres. */
    float pixelOffset[2];
    /** D3DCMPFUNC of the alpha test; 8 (always) when it is off. */
    uint32_t alphaFunc;
    /** D3DRS_ALPHAREF, 0 to 255. */
    float alphaRef;
    /** Boolean constants b0-b15 of the vertex shader (bits 0-15) and the pixel shader (bits 16-31). */
    uint32_t bools;
    /** Scenery fading in or out (engine/rendering/culling/culling.h): the share of its pixels the rebuild's own shaders draw, 1 in full. */
    float fade;
    /** Bit 0: the rebuild's own shaders take a cheap sun shadow: the car reflection cube's faces, whose pixels outnumber
     * the screen's and whose reflection blurs what the full filter adds (lighting.hlsli SunVisibility). Bit 1: a depth
     * pass (Renderer::SetDepthPrepass): the shaders that know it stop after their alpha test. */
    uint32_t shadowQuality;
    /** 1 when the draw's particles take the hour's light (smoke, dust, spray, leaves, rain); 0 for those that give
     * light (sparks, glows, fire) and every other draw (the particle shaders, assets/shaders/owned/x360_particles_*). */
    uint32_t litParticles;
    /** The draw's texture's blend type (TextureInfo::AlphaBlendType: 0 opaque, 1 and 4 alpha blended, 2 additive, 3
     * darkening, 5 and 6 by destination alpha): the rebuild's own glass shader lets the world show through alpha
     * blended glass, and its world shaders fade what additive and darkening textures add or take with the haze. */
    uint32_t blend;
    /** The draw's surface detail (engine/rendering/surface.h DrawValue, assets/shaders/owned/surface.hlsli): bits 0-7 the relief's depth
     * in ten-thousandths of a texture repeat (0: none), bits 8-11 the wear profile (nfs::surface::Profile). 0 for every
     * other draw. */
    uint32_t surface;
};

constexpr uint32_t kVertexConstantsBinding = 0;
constexpr uint32_t kPixelConstantsBinding = 1;
constexpr uint32_t kFirstSamplerBinding = 2;
constexpr uint32_t kMaxSamplers = 16;
/** Set 0's third dynamic uniform buffer: the draw's object-to-world transform as three rows (x, y, z of the world
 * position), for the rebuild's own vertex shaders (assets/shaders/owned, drawWorld). It changes with nearly every draw: as a
 * dynamic offset next to the constants' it costs the driver far less than in the push constants. */
constexpr uint32_t kDrawConstantsBinding = kFirstSamplerBinding + kMaxSamplers;
constexpr uint32_t kDrawConstantsBytes = 48;
/** Set 0's fourth dynamic buffer, a storage buffer: the rebuild's own vertex shaders (ShaderBlob::owned) read their
 * float registers and the draw's three world rows from one block per instance there (assets/shaders/owned, instanceBlocks),
 * (constantCount + 3) registers apart, so a run of instances of one strip is one draw (docs/NATIVE_WORLD.md). */
constexpr uint32_t kInstanceBlocksBinding = kDrawConstantsBinding + 1;
/** The range the instance blocks' descriptor covers from its offset: the most instances one draw can have. */
constexpr uint32_t kInstanceBlocksRange = 64 * 1024;

/** A constant table (CTAB) entry: an effect parameter and the registers the shader reads it from. */
struct ShaderConstant
{
    const char *name;
    /** D3DXREGISTER_SET: 0 bool, 1 int, 2 float, 3 sampler. */
    uint8_t registerSet;
    /** D3DXPARAMETER_CLASS as the shader sees the constant: matrices come as rows or columns of registers. */
    uint8_t cls;
    uint16_t index;
    uint16_t count;
    uint8_t rows;
    uint8_t columns;
    uint16_t elements;
};

struct ShaderBlob
{
    const char *name;
    const uint32_t *spirv;
    uint32_t spirvWords;
    /** The original D3D9 bytecode, for parity tests against Direct3D 9. */
    const uint32_t *dxso;
    uint32_t dxsoWords;
    ShaderStage stage;
    /** Float constant registers the shader reads (c0 up to this). */
    uint16_t constantCount;
    /** Pixel shaders: sampler stages read. */
    uint16_t samplerMask;
    /** Vertex shaders: vertex attribute locations read. Pixel shaders: varying locations read. */
    uint32_t inputMask;
    /** Vertex shaders: varying locations written. */
    uint32_t outputMask;
    /** SamplerType of each pixel shader sampler stage, two bits per stage. */
    uint32_t samplerTypes;
    /** ps_1_1-1_3: stages the pass samples with D3DTTFF_PROJECTED (baked into the translation). */
    uint16_t projectedMask;
    uint16_t constantInfoCount;
    const ShaderConstant *constants;
    /** Built from the rebuild's own source (assets/shaders/owned) rather than translated: deliberately not the original. */
    bool owned;

    SamplerType TypeOf(uint32_t stage) const
    {
        return static_cast<SamplerType>((samplerTypes >> (2 * stage)) & 3);
    }
};

/** A pass of one of the effects, with the shaders it binds (-1 for none: fixed function). */
struct ShaderPass
{
    const char *effect;
    const char *technique;
    uint16_t techniqueIndex;
    uint16_t passIndex;
    int16_t vertexShader;
    int16_t pixelShader;
};

extern const ShaderBlob kShaders[];
extern const uint32_t kShaderCount;
extern const ShaderPass kShaderPasses[];
extern const uint32_t kShaderPassCount;
} // namespace nfs::vk
