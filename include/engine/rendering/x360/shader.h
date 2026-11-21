#pragma once
/**
 * The Xbox 360 effects' techniques and parameters by name, one table per effect slot (X360 Stuff's MWShader; the post
 * processing's own effect, engine/rendering/x360/post.h, replaces its PostFXShader). The layout is MWShader's, so the ported code
 * reads like the plugin's: the handles are looked up again whenever the slot's effect is recreated (Effect()).
 */
#include <stddef.h>
#include "game/platform/Effect.h"

namespace nfs::x360
{
struct Shader
{
    /** The effect the handles belong to; Effect() looks them up again when the slot holds another one. */
    ID3DXEffect *Effect(bool refresh = true);
    void InitParams();
    void InitTechniques();

    void *reserved;                     // 0x000 (MWShader's vtable)
    ID3DXEffect *effect;                // 0x004
    /** The g_Effects slot. */
    eEffect **slot;                     // 0x008
    /** The technique with the highest "shader" annotation among those with a "shadowLevel". */
    D3DXHANDLE mainTechnique;           // 0x00c
    D3DXHANDLE lowLod;                  // 0x010
    D3DXHANDLE worldNoFog;              // 0x014
    D3DXHANDLE wetRoad;                 // 0x018
    D3DXHANDLE dryRoad;                 // 0x01c
    D3DXHANDLE world;                   // 0x020
    D3DXHANDLE worldNormalMap;          // 0x024
    D3DXHANDLE car;                     // 0x028
    D3DXHANDLE carNormalMap;            // 0x02c
    D3DXHANDLE waterSea;                // 0x030
    D3DXHANDLE rvm;                     // 0x034
    D3DXHANDLE rvmNoCubemap;            // 0x038
    D3DXHANDLE grass;                   // 0x03c
    D3DXHANDLE grassTransition;         // 0x040
    D3DXHANDLE grassTransitionNoNormalMap; // 0x044
    D3DXHANDLE renderLight;             // 0x048
    D3DXHANDLE renderLightAlphaTex;     // 0x04c
    D3DXHANDLE renderWhite;             // 0x050
    D3DXHANDLE renderWhiteAlphaTex;     // 0x054
    D3DXHANDLE zPrePass;                // 0x058
    D3DXHANDLE colorWriteMode;          // 0x05c
    D3DXHANDLE blurVector;              // 0x060
    D3DXHANDLE isMotionBlurVignetted;   // 0x064
    D3DXHANDLE heightMapTexture;        // 0x068
    D3DXHANDLE unused6c;                // 0x06c
    D3DXHANDLE combinedBrightness;      // 0x070
    D3DXHANDLE rainSplashTex;           // 0x074
    D3DXHANDLE curveCoeffs[4];          // 0x078
    D3DXHANDLE vignetteScale;           // 0x088
    D3DXHANDLE volumeMapTexture;        // 0x08c
    D3DXHANDLE opacityMapTexture;       // 0x090
    D3DXHANDLE opacityMap;              // 0x094
    D3DXHANDLE specularMapTexture;      // 0x098
    D3DXHANDLE diffuseMap;              // 0x09c
    D3DXHANDLE diffuseTex;              // 0x0a0
    D3DXHANDLE worldViewProjection;     // 0x0a4
    D3DXHANDLE rvmWorldBrightness;      // 0x0a8
    D3DXHANDLE surfaceReflection;       // 0x0ac
    D3DXHANDLE rainIntensity;           // 0x0b0
    D3DXHANDLE unusedb4;                // 0x0b4
    D3DXHANDLE waveNormalMapTex;        // 0x0b8
    D3DXHANDLE reflectedTex;            // 0x0bc
    D3DXHANDLE textureAnimOffset;       // 0x0c0
    D3DXHANDLE miscMapTexture[4];       // 0x0c4
    D3DXHANDLE desaturation;            // 0x0d4
    D3DXHANDLE standardBrightness;      // 0x0d8
    D3DXHANDLE carBrightness;           // 0x0dc
    D3DXHANDLE noise2D;                 // 0x0e0
    D3DXHANDLE isParallaxMapped;        // 0x0e4
    D3DXHANDLE isParallaxMappedByName;  // 0x0e8
    D3DXHANDLE shadowMapAlphaEnabled;   // 0x0ec
    D3DXHANDLE shadowMapAlphaMin;       // 0x0f0
    D3DXHANDLE shadowMapBias;           // 0x0f4
    D3DXHANDLE shadowMapCarShadowEnabled; // 0x0f8
    D3DXHANDLE shadowMapEnabled;        // 0x0fc
    D3DXHANDLE shadowMapScaleX;         // 0x100
    D3DXHANDLE shadowMapScaleY;         // 0x104
    D3DXHANDLE ambientColour;           // 0x108
    D3DXHANDLE baseAlphaRef;            // 0x10c
    D3DXHANDLE metallicScale;           // 0x110
    D3DXHANDLE specularHotspot;         // 0x114
    D3DXHANDLE diffuseMin;              // 0x118
    D3DXHANDLE diffuseRange;            // 0x11c
    D3DXHANDLE specularMin;             // 0x120
    D3DXHANDLE specularRange;           // 0x124
    D3DXHANDLE envMapMin;               // 0x128
    D3DXHANDLE envMapRange;             // 0x12c
    D3DXHANDLE specularPower;           // 0x130
    D3DXHANDLE envMapPower;             // 0x134
    D3DXHANDLE baseAddressU;            // 0x138
    D3DXHANDLE baseAddressV;            // 0x13c
    D3DXHANDLE rainDropOffset;          // 0x140
    D3DXHANDLE localCenter;             // 0x144
    D3DXHANDLE displaceMap;             // 0x148
    D3DXHANDLE normalMapTexture;        // 0x14c
    D3DXHANDLE skyDiffuseScale;         // 0x150
    D3DXHANDLE diffuseColour;           // 0x154
    D3DXHANDLE feMaskMap;               // 0x158
    D3DXHANDLE feColorWriteFlag;        // 0x15c
    /** The grass effect's shell parameters (by name): GRASSCOLOUR to GRASSGAMMA. */
    D3DXHANDLE grassColour;             // 0x160
    D3DXHANDLE maxShells;               // 0x164
    D3DXHANDLE grassHeight;             // 0x168
    D3DXHANDLE diffuseSpace;            // 0x16c
    D3DXHANDLE diffuseMipMapBias;       // 0x170
    D3DXHANDLE noiseSpace;              // 0x174
    D3DXHANDLE noiseMipMapBias;         // 0x178
    D3DXHANDLE lowNoiseSpace;           // 0x17c
    D3DXHANDLE lowNoiseIntensity;       // 0x180
    D3DXHANDLE scruff;                  // 0x184
    D3DXHANDLE curlTightness;           // 0x188
    D3DXHANDLE lodStart;                // 0x18c
    D3DXHANDLE lodRamp;                 // 0x190
    D3DXHANDLE grassGamma;              // 0x194
    D3DXHANDLE fogBrPlusBm;             // 0x198
    D3DXHANDLE fogOneOverBrBm;          // 0x19c
    D3DXHANDLE fogConst[3];             // 0x1a0
    D3DXHANDLE sunColor;                // 0x1ac
    D3DXHANDLE unused1b0;               // 0x1b0
    D3DXHANDLE fogDistanceScale;        // 0x1b4
    D3DXHANDLE adaptedLum;              // 0x1b8
    D3DXHANDLE bloomScale;              // 0x1bc
    D3DXHANDLE brightPassThreshold;     // 0x1c0
    D3DXHANDLE middleGray;              // 0x1c4
    D3DXHANDLE visualEffectBrightness;  // 0x1c8
    D3DXHANDLE visualEffectVignette;    // 0x1cc
    D3DXHANDLE cullMode;                // 0x1d0
};

static_assert(offsetof(Shader, colorWriteMode) == 0x5c, "Shader layout");
static_assert(offsetof(Shader, shadowMapAlphaEnabled) == 0xec, "Shader layout");
static_assert(offsetof(Shader, grassColour) == 0x160, "Shader layout");
static_assert(offsetof(Shader, cullMode) == 0x1d0, "Shader layout");

/** One per effect slot (X360 Stuff's table at 0x1004a9a0). */
extern Shader g_shaders[NUM_EFFECTS];

inline Shader &ShaderFor(const eEffect *effect)
{
    return g_shaders[effect->Type];
}

/** Sets a float, by semantic, in every slot's effect that has it (MWShader::SetGlobalFloat). */
void SetGlobalFloat(const char *semantic, float value);
/** Sets a float, by parameter name (MWShader::SetGlobalFloatByName). */
void SetGlobalFloatByName(const char *name, float value);
/** Sets a vector, by semantic (MWShader::SetGlobalVector). */
void SetGlobalVector(const char *semantic, const float *value);
} // namespace nfs::x360
