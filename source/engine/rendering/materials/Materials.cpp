/**
 * Per-draw material setup for the Xbox 360 effects (X360 Stuff's Rendering::TextureAssignmentHook 0x100313d0, the
 * texture hooks at 0x6c6e12 and 0x6c6d09, SetXenonLightMaterial 0x10030ca0 and eDataRenderHook 0x10031fc0): texture
 * wrapping, fog, colour write and depth alpha, parallax, the grass shells' opacity map and parameters, the light
 * material and the car level of detail. They replace the original texture and light material setters
 * (zPlatform 0x6c6cc0, 0x6c6d10, 0x6c76e0) for every caller.
 */
#include "game/gen/api.h"
#include "game/platform/Device.h"
#include "game/platform/Effect.h"
#include "game/platform/Platform.h"
#include "game/platform/Solid.h"
#include "core/config.h"
#include "legacy/link.h"
#include "utils/simd.h"
#include "engine/rendering/surface.h"
#include "engine/rendering/vulkan/renderer.h"
#include "engine/rendering/x360/render.h"
#include "engine/rendering/x360/resources.h"
#include "engine/rendering/x360/shader.h"

NFS_REPLACE(0x6c6cc0, sub_6C6CC0)
NFS_REPLACE(0x6c6d10, sub_6C6D10)
NFS_REPLACE(0x6c76e0, sub_6C76E0)

/** The textures and flags bound last (diffuse and three more), and the effect they were bound for. */
extern "C" TextureInfo *dword_982CB4;
extern "C" TextureInfo *dword_982CB8;
extern "C" TextureInfo *dword_982CBC;
extern "C" TextureInfo *dword_982CC0;
extern "C" TextureInfo *dword_982CC4;
extern "C" eEffect *dword_982CC8;
extern "C" unsigned int dword_982CD0;
/** Statistics: texture changes. */
extern "C" int dword_987B30;
/** The sun colour (SUNCOLOR). */
extern "C" float unk_8FAE24[4];
extern "C" char *TimeOfDayPointer;
extern "C" int TheGameFlowManager_CurrentGameFlowState;
/** The video option the depth effects (motion blur, depth of field) need. */
extern "C" int __multisamplequality;

extern "C" void __cdecl sub_6C68B0(TextureInfo *texture, unsigned int flags);
extern "C" void __cdecl sub_6C6CC0(TextureInfo *texture, unsigned int flags);
extern "C" void __cdecl sub_6C6D10(TextureInfo **textures, unsigned int flags);
extern "C" void __cdecl sub_6C76E0(char *material, unsigned int flags);
void __cdecl eDataRender(TextureInfo **textures, unsigned int flags);

namespace nfs::x360
{
int g_passMode;
bool g_depthPassDone;
/** The world effects' Begin flags: they set every state they use, and what follows sets its own, so there is no need for
 * the per-Begin state save the original asked for (a state block capture per effect switch on Direct3D 9). */
unsigned long g_beginFlags = D3DXFX_DONOTSAVESTATE;
const float *g_fog;
const float *g_skyFog;
void *g_grassAttributes;
void *g_depthTexture;
void *g_depthSurface;
int g_motionBlur = options::kMotionBlur;
bool g_depthOfField = true;

namespace
{
const int kGameFlowRacing = 6;
const unsigned int kAttribNoiseSpace = 0x3a417eae;

/** Textures drawn with alpha writes on during the depth alpha passes (X360 Stuff's table at 0x1004aa20). */
const unsigned int kAlphaWriteTextures[16] = {
    0xfeabcf59, 0x224a6d98, 0xc567bac7, 0x725f13d9, 0x8294983f, 0x829524a0, 0xdca86b52, 0xceda1111,
    0xdca8f7b3, 0xdca98414, 0xcedb29d3, 0xe4fcbfc2, 0xb481fb50, 0x96fc75c3, 0xdfe4522f, 0xefa7a83c,
};

/** Normal-mapped textures with parallax (0x1004aa80). */
const unsigned int kParallaxTextures[78] = {
    0xd6a10eac, 0x5744fded, 0x758b247f, 0xf60310ef, 0x2f76687f, 0x427d8058, 0xacb93034, 0x853cd9f6, 0xe26d0431,
    0xb43ba71c, 0x39985c5d, 0xb1bfdcbb, 0x81db7fd1, 0xe1ae138e, 0x7487f58f, 0x1bcf2b2d, 0x2804510e, 0xb214eff2,
    0x488e9887, 0x57f79b09, 0xedb35739, 0xbafaec0d, 0xb1569031, 0xd1a6f7cc, 0xf05bf05a, 0xa2692ecd, 0x327f4e90,
    0xbc8c143c, 0xc2862cb5, 0xaec80196, 0x10a9203a, 0xfceaf51b, 0xa0bf3ffd, 0xa08432d1, 0x61b36441, 0x4df53922,
    0xf1c98404, 0x869579a9, 0x4fe3a632, 0x99e7d26d, 0x1a8bc1ae, 0x51274ba0, 0xb949d4b0, 0xea445700, 0xa8f36b39,
    0xc144eed5, 0x99c89897, 0x887096f2, 0xe05ae2fd, 0x65b7983e, 0xbf8e36fc, 0xb51df332, 0x14f086ef, 0xa7ca68f0,
    0x6af0366e, 0x77255c4f, 0x4da5cf73, 0xaf048368, 0x65c5f54a, 0xa88145ba, 0x9697132e, 0xe689d732, 0x20c8030d,
    0x239e63bb, 0x65aff28e, 0xf5c61251, 0xca38e3dd, 0xd711eb56, 0xc353c037, 0x5fca2b7b, 0x4c0c005c, 0xefe04b3e,
    0xe5390352, 0xa66834c2, 0x92aa09a3, 0x367e5485, 0xe5c1be0a, 0xca247c33,
};

/** Grass textures whose opacity map is the shared GRASS_OPACITY-style texture 0xda45656f when it is loaded. */
const unsigned int kSharedOpacityGrass[4] = {0x319706ca, 0x10bd9b5d, 0x23ea3729, 0x0310cbbc};
const unsigned int kSharedGrassOpacity = 0xda45656f;

bool Contains(const unsigned int *table, int count, unsigned int hash)
{
    for (int i = 0; i < count; ++i)
        if (table[i] == hash)
            return true;
    return false;
}

void *Plat(const TextureInfo *texture)
{
    return texture->PlatInfo->Texture;
}

float Float(const char *object, int offset)
{
    return *reinterpret_cast<const float *>(object + offset);
}

/** The diffuse texture name hash extended with "_OPACITY" (bStringHash continued). */
unsigned int OpacityHash(unsigned int hash)
{
    for (const char *c = "_OPACITY"; *c; ++c)
        hash = hash * 33 + static_cast<unsigned int>(*c);
    return hash;
}

/** The effect and handle table TextureAssignmentHook keeps between calls, and what it last saw. */
eEffect *s_lastEffect;
Shader *s_shader;
ID3DXEffect *s_effect;
TextureInfo *s_lastTexture;
/** Which grass technique is set: 1/2 the main one, 3/4 a transition one (the higher for GrassNormalMap). */
int s_grassTechnique;
/** The depth alpha techniques are set on the world effects for this pass. */
bool s_depthAlphaSet;

void SetWrap(ID3DXEffect *effect, const Shader &shader, const TextureInfo *texture)
{
    if (shader.baseAddressU)
        effect->SetInt(shader.baseAddressU, ~(static_cast<signed char>(texture->TilableUV) * 2) & 2 | 1);
    if (shader.baseAddressV)
        effect->SetInt(shader.baseAddressV, ~texture->TilableUV & 2 | 1);
}

/** What SetFog last gave each effect slot (the fog's 21 values and the sun colour), and to which effect. */
struct FogSet
{
    ID3DXEffect *effect;
    float values[28];
};
FogSet s_fogSet[NUM_EFFECTS];

void SetFog(Shader &shader)
{
    ID3DXEffect *e = shader.Effect();
    const float *fog = &shader == &g_shaders[EFFECT_SKY] ? g_skyFog : g_fog;
    // Every texture change sets the fog again, which only changes with the hour and the view: the same values again
    // only cost the calls.
    FogSet &set = s_fogSet[&shader - g_shaders];
    float values[28] = {};
    for (int i = 0; i < 21; ++i)
        values[i] = fog[i];
    for (int i = 0; i < 4; ++i)
        values[21 + i] = unk_8FAE24[i];
    const bool changed = nfs::StoreIfChanged(set.values, values, sizeof(values));
    static const bool cache = nfs::config::Int("Debug", "FogCache", 1) != 0;
    if (cache && set.effect == e && !changed)
        return;
    set.effect = e;
    if (shader.fogBrPlusBm)
        e->SetVector(shader.fogBrPlusBm, fog);
    if (shader.fogOneOverBrBm)
        e->SetVector(shader.fogOneOverBrBm, fog + 4);
    for (int i = 0; i < 3; ++i)
        if (shader.fogConst[i])
            e->SetVector(shader.fogConst[i], fog + 8 + 4 * i);
    if (shader.sunColor)
        e->SetVector(shader.sunColor, unk_8FAE24);
    if (shader.fogDistanceScale)
        e->SetFloat(shader.fogDistanceScale, fog[20]);
}

/** The world effects' depth pass technique, once per pass: alpha-tested depth, or the main one in low detail views.
 * X360 Stuff hands the normal-mapped effect the world effect's handles; Direct3D rejects a foreign handle, so that
 * call changes nothing and is left out. */
void SetDepthAlphaTechniques()
{
    Shader &world = g_shaders[EFFECT_WORLD];
    if (ID3DXEffect *e = world.Effect())
        e->SetTechnique(g_passMode == kPassLowLod ? world.mainTechnique : world.renderLightAlphaTex);
    g_shaders[EFFECT_WORLD_NORMAL_MAP].Effect();
    static const int kSlots[3] = {EFFECT_WORLD, EFFECT_WORLD_NORMAL_MAP, EFFECT_WORLD_REFLECT};
    for (int slot : kSlots)
        if (g_Effects[slot])
            g_Effects[slot]->PassCount = 1;
}

/** Colour writes: alpha holds depth in the depth passes, except for the textures that need their own alpha. */
void SetColorWrites(const TextureInfo *texture)
{
    if (g_passMode == kPassMain && (g_depthPassDone || !ShouldDrawDepth() || !__multisamplequality))
    {
        device::SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
        s_depthAlphaSet = false;
        return;
    }
    device::SetRenderState(D3DRS_COLORWRITEENABLE, 7);
    if (g_passMode != kPassDepth && Contains(kAlphaWriteTextures, 16, texture->NameHash))
        device::SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    if (!s_depthAlphaSet)
        SetDepthAlphaTechniques();
    s_depthAlphaSet = true;
    if (s_shader->shadowMapAlphaEnabled)
        s_effect->SetBool(s_shader->shadowMapAlphaEnabled, texture->AlphaUsageType != 2);
}

/** The grass effects (Grass, GrassNormalMap): the opacity map picks the transition technique and the shell count. */
void SetGrass(bool effectChanged)
{
    TextureInfo *diffuse = dword_982CB4;
    if (!effectChanged && diffuse == s_lastTexture)
        return;
    const int type = g_CurrentEffect->Type;
    TextureInfo *opacity = nullptr;
    if (Contains(kSharedOpacityGrass, 4, diffuse->NameHash))
    {
        opacity = GetTextureInfo(kSharedGrassOpacity, 1, 0);
        if (opacity == DefaultTextureInfo)
            opacity = GetTextureInfo(OpacityHash(diffuse->NameHash), 1, 0);
    }
    else
    {
        opacity = GetTextureInfo(OpacityHash(diffuse->NameHash), 1, 0);
    }
    bool bound = false;
    if (opacity && opacity != diffuse && opacity != DefaultTextureInfo)
    {
        s_effect->SetTexture(s_shader->opacityMap, static_cast<IDirect3DBaseTexture9 *>(Plat(opacity)));
        bound = true;
    }
    const bool normalMapped = type == EFFECT_WORLD_NORMAL_DEPTH;
    const int transition = normalMapped ? 4 : 3;
    const int plain = normalMapped ? 2 : 1;
    D3DXHANDLE technique = nullptr;
    bool restart = false;
    if (s_grassTechnique != transition && bound)
    {
        technique = normalMapped ? s_shader->grassTransition : s_shader->grassTransitionNoNormalMap;
        s_grassTechnique = transition;
        restart = true;
    }
    else if (s_grassTechnique != plain && !bound)
    {
        technique = s_shader->mainTechnique;
        s_grassTechnique = plain;
        restart = true;
    }
    if (restart)
    {
        s_effect->EndPass();
        s_effect->End();
        if (technique)
            s_effect->SetTechnique(technique);
        UINT passes;
        s_effect->Begin(&passes, g_beginFlags);
        s_effect->BeginPass(0);
        int shells = 1;
        if (g_passMode == kPassMain)
            shells = g_grassAttributes ? static_cast<int>(Float(AttributeLayout(g_grassAttributes), 0x64)) : 5;
        if (s_shader->slot && *s_shader->slot)
            (*s_shader->slot)->PassCount = shells;
    }
    ID3DXEffect *e = s_effect;
    Shader &s = *s_shader;
    if (g_grassAttributes)
    {
        AttributeFloat(g_grassAttributes, kAttribNoiseSpace);
        const char *data = AttributeLayout(g_grassAttributes);
        e->SetVector(s.grassColour, data);
        e->SetFloat(s.maxShells, g_passMode != kPassMain ? 1.0f : Float(data, 0x64));
        e->SetFloat(s.grassHeight, Float(data, 0x70));
        e->SetFloat(s.diffuseSpace, Float(data, 0x78));
        e->SetFloat(s.diffuseMipMapBias, Float(data, 0x6c));
        e->SetFloat(s.noiseSpace, *AttributeFloat(g_grassAttributes, kAttribNoiseSpace));
        e->SetFloat(s.noiseMipMapBias, Float(data, 0x54));
        e->SetFloat(s.lowNoiseSpace, Float(data, 0x50));
        e->SetFloat(s.lowNoiseIntensity, Float(data, 0x60));
        e->SetFloat(s.scruff, Float(data, 0x4c));
        e->SetFloat(s.curlTightness, 1.05f);
        e->SetFloat(s.lodStart, Float(data, 0x58));
        e->SetFloat(s.lodRamp, Float(data, 0x48));
        e->SetFloat(s.grassGamma, Float(data, 0x40));
    }
    else
    {
        static const float kColour[4] = {1.0f, 0.98f, 0.93f, 1.0f};
        e->SetVector(s.grassColour, kColour);
        e->SetFloat(s.maxShells, g_passMode != kPassMain ? 1.0f : 5.0f);
        e->SetFloat(s.grassHeight, 0.04f);
        e->SetFloat(s.diffuseSpace, 1.0f);
        e->SetFloat(s.diffuseMipMapBias, 0.0f);
        e->SetFloat(s.noiseSpace, 0.23f);
        e->SetFloat(s.noiseMipMapBias, -1.0f);
        e->SetFloat(s.lowNoiseSpace, 0.005f);
        e->SetFloat(s.lowNoiseIntensity, 0.34f);
        e->SetFloat(s.scruff, 0.005f);
        e->SetFloat(s.curlTightness, 1.05f);
        e->SetFloat(s.lodStart, 21.45f);
        e->SetFloat(s.lodRamp, 0.0f);
        e->SetFloat(s.grassGamma, 1.0f);
    }
}

/** Texture setup for the shaders that only take the diffuse texture (Rendering::SimpleTextureAssignmentHookCave). */
void AssignSimpleTexture(TextureInfo *texture)
{
    Shader &shader = ShaderFor(g_CurrentEffect);
    ID3DXEffect *e = g_CurrentEffect->Effect;
    SetWrap(e, shader, texture);
    if (g_passMode == kPassDepth && shader.cullMode)
        e->SetInt(shader.cullMode, 1);
}
} // namespace

void ResetMaterialCache()
{
    s_lastEffect = nullptr;
    s_shader = nullptr;
    s_effect = nullptr;
    s_lastTexture = nullptr;
    s_grassTechnique = 0;
    s_depthAlphaSet = false;
    memset(s_fogSet, 0, sizeof(s_fogSet));
}

bool DepthOfFieldActive()
{
    if (!g_depthOfField)
        return false;
    // The player views' cameras: depth of field when the focal range is set.
    const char *views = reinterpret_cast<const char *>(eViews);
    const char *const cameras[2] = {*reinterpret_cast<char *const *>(views + 0xb0),
                                    *reinterpret_cast<char *const *>(views + 0x120)};
    for (const char *camera : cameras)
        if (camera && (Float(camera, 0xb8) > 0.0001f || Float(camera, 0xb4) > 0.0001f))
            return true;
    return false;
}

bool ShouldDrawDepth()
{
    if (!g_depthTexture || !g_depthSurface)
        return false;
    // Vulkan: the main view always draws into the depth texture (its depth buffer either way, so it costs nothing): the
    // haze passes read it (PostProcess.cpp DrawHaze).
    return true;
}

bool DepthPassActive()
{
    return ShouldDrawDepth() && __multisamplequality;
}

void AssignTextures()
{
    eEffect *current = g_CurrentEffect;
    ID3DXEffect *effect = current->Effect;
    const bool effectChanged = s_lastEffect != current || s_effect != effect;
    if (effectChanged)
    {
        s_shader = &ShaderFor(current);
        s_effect = effect;
        s_lastEffect = current;
    }
    TextureInfo *diffuse = dword_982CB4;
    if (s_lastTexture != diffuse)
    {
        SetWrap(s_effect, *s_shader, diffuse);
        const int type = current->Type;
        D3DXHANDLE cull = s_shader->cullMode;
        if (cull && (g_passMode == kPassDepth ||
                     ((type == EFFECT_CAR || type == EFFECT_CAR_DEPTH) &&
                      !(g_passMode == kPassMain && TheGameFlowManager_CurrentGameFlowState == kGameFlowRacing &&
                        (diffuse->AlphaUsageType == 0 || diffuse->AlphaBlendType == 0)))))
            current->Effect->SetInt(cull, 1);
    }
    if (effectChanged || diffuse != s_lastTexture)
    {
        if (g_fog)
            SetFog(*s_shader);
        // Light that additive and darkening textures add or take would gather the haze's light too: the Xbox 360 path
        // turned their fog off. The rebuild's own world shaders fade them with the haze instead (D3D9State::blend).
        if (g_passMode != kPassDepth && current != g_Effects[EFFECT_WORLD_BONE] && diffuse->AlphaUsageType == 2 &&
            static_cast<signed char>(reinterpret_cast<const char *>(diffuse)[0x51]) > 0)
            device::SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        SetColorWrites(diffuse);
    }
    if (effectChanged || diffuse != s_lastTexture)
    {
        const int type = current->Type;
        D3DXHANDLE parallax = s_shader->isParallaxMapped, parallaxByName = s_shader->isParallaxMappedByName;
        if ((type == EFFECT_WORLD_NORMAL_MAP || type == EFFECT_WORLD_NORMAL_DEPTH) && dword_982CB8 &&
            options::kDoParallax && (parallax || parallaxByName))
        {
            const BOOL on = Contains(kParallaxTextures, 78, dword_982CB8->NameHash);
            if (parallax)
                s_effect->SetBool(parallax, on);
            if (parallaxByName)
                s_effect->SetBool(parallaxByName, on);
        }
    }
    const int type = current->Type;
    // The surface detail of the roads' and the normal-mapped world's full shaders (engine/rendering/surface.h): relief and wear by
    // the pair of textures.
    if (nfs::vk::Renderer *r = nfs::vk::g_renderer)
        r->SetDrawSurface(g_passMode == kPassMain && dword_982CB8 &&
                                  (type == EFFECT_WORLD_REFLECT || type == EFFECT_WORLD_NORMAL_MAP)
                              ? nfs::surface::DrawValue(diffuse->NameHash, dword_982CB8->NameHash)
                              : 0);
    if (type != EFFECT_TREE_DEPTH && type != EFFECT_WORLD_NORMAL_DEPTH)
    {
        s_grassTechnique = 0;
        s_lastTexture = diffuse;
        return;
    }
    SetGrass(effectChanged);
    s_lastTexture = dword_982CB4;
}
} // namespace nfs::x360

using namespace nfs::x360;

/** Binds one texture as the current diffuse texture (zPlatform 0x6c6cc0), then the Xbox 360 wrap and culling. */
extern "C" void __cdecl sub_6C6CC0(TextureInfo *texture, unsigned int flags)
{
    if (dword_982CB4 == texture && dword_982CC8 == g_CurrentEffect)
        return;
    ++dword_987B30;
    dword_982CB4 = texture;
    dword_982CB8 = nullptr;
    dword_982CBC = nullptr;
    dword_982CC0 = nullptr;
    dword_982CC4 = nullptr;
    dword_982CC8 = g_CurrentEffect;
    if (!texture)
        return;
    sub_6C68B0(texture, flags);
    AssignSimpleTexture(texture);
}

/** Binds a draw's textures (zPlatform 0x6c6d10): the diffuse texture and its setup, then the normal, specular and
 * extra maps by their parameter (the effect's table entries 0x9ac, 0x844 and 0xca4). */
extern "C" void __cdecl sub_6C6D10(TextureInfo **textures, unsigned int flags)
{
    if (dword_982CB4 == textures[0] && dword_982CB8 == textures[1] && dword_982CBC == textures[2] &&
        dword_982CC0 == textures[3] && dword_982CC8 == g_CurrentEffect && dword_982CD0 == flags)
        return;
    ++dword_987B30;
    auto plat = [](TextureInfo *t) -> void * { return t && t->PlatInfo ? Plat(t) : nullptr; };
    dword_982CB4 = textures[0];
    void *diffuse = plat(textures[0]);
    dword_982CB8 = textures[1];
    void *second = plat(textures[1]);
    dword_982CBC = textures[2];
    void *third = plat(textures[2]);
    dword_982CC0 = textures[3];
    void *fourth = plat(textures[3]);
    dword_982CC4 = textures[4];
    if (!diffuse)
        return;
    eEffect *e = g_CurrentEffect;
    dword_982CC8 = e;
    dword_982CD0 = flags;
    sub_6C68B0(textures[0], flags);
    AssignTextures();
    if (second)
        if (D3DXHANDLE h = e->Parameter(0x9ac))
            e->Effect->SetTexture(h, static_cast<IDirect3DBaseTexture9 *>(second));
    if (third)
        if (D3DXHANDLE h = e->Parameter(0x844))
            e->Effect->SetTexture(h, static_cast<IDirect3DBaseTexture9 *>(third));
    if (fourth)
        if (D3DXHANDLE h = e->Parameter(0xca4))
            e->Effect->SetTexture(h, static_cast<IDirect3DBaseTexture9 *>(fourth));
}

/** The light material for the car and skinned effects (zPlatform 0x6c76e0 in X360 Stuff's version): diffuse, specular
 * and environment map ranges, metallic scale and powers. */
extern "C" void __cdecl sub_6C76E0(char *m, unsigned int)
{
    Shader &shader = ShaderFor(g_CurrentEffect);
    ID3DXEffect *e = shader.Effect();
    const int type = (*shader.slot)->Type;
    if (type != EFFECT_CAR && type != EFFECT_CAR_DEPTH && type != EFFECT_WORLD_BONE)
        return;
    auto f = [m](int offset) { return Float(m, offset); };
    const float d0 = f(0x30), d1 = f(0x40);
    const float diffuseMin[4] = {d0 * f(0x34), d0 * f(0x38), d0 * f(0x3c), f(0x50)};
    const float diffuseRange[4] = {d1 * f(0x44) - diffuseMin[0], d1 * f(0x48) - diffuseMin[1],
                                   d1 * f(0x4c) - diffuseMin[2], f(0x54) - f(0x50)};
    e->SetVector(shader.diffuseMin, diffuseMin);
    e->SetVector(shader.diffuseRange, diffuseRange);
    if (type != EFFECT_CAR && type != EFFECT_CAR_DEPTH)
        return;
    const float sky = Float(TimeOfDayPointer, 0x60);
    const float e0 = f(0x80) * 0.5f, e1 = f(0x90) * 0.5f;
    const float envMin[4] = {e0 * f(0x84), e0 * f(0x88), e0 * f(0x8c), 0.0f};
    const float envRange[4] = {e1 * f(0x94) - envMin[0], e1 * f(0x98) - envMin[1], e1 * f(0x9c) - envMin[2], 0.0f};
    const float s0 = f(0x5c), s1 = f(0x6c);
    const float specMin[4] = {s0 * f(0x60) * sky, s0 * f(0x64) * sky, s0 * f(0x68) * sky, 0.0f};
    const float specRange[4] = {s1 * f(0x70) * sky - specMin[0], s1 * f(0x74) * sky - specMin[1],
                                s1 * f(0x78) * sky - specMin[2], 0.0f};
    e->SetFloat(shader.metallicScale, f(0xa0));
    e->SetFloat(shader.specularHotspot, sky * f(0xa4));
    e->SetVector(shader.envMapMin, envMin);
    e->SetVector(shader.envMapRange, envRange);
    e->SetVector(shader.specularMin, specMin);
    e->SetVector(shader.specularRange, specRange);
    e->SetFloat(shader.specularPower, f(0x58));
    e->SetFloat(shader.envMapPower, f(0x7c));
}

namespace
{
/** Ambient brightness of particular solids (X360 Stuff's table at 0x10053b30): name hash and scale. */
struct SolidBrightness
{
    unsigned int hash;
    float scale;
};
#include "SolidBrightness.inc"

/** kSolidBrightness by hash (every draw looks one up; a linear search through the table was most of the draw's time
 * here): 256 slots, linear probing, 0 for an empty slot, the first entry of a hash in the table wins. */
struct BrightnessTable
{
    unsigned int hash[256];
    float scale[256];
    BrightnessTable()
    {
        memset(hash, 0, sizeof(hash));
        for (const SolidBrightness &b : kSolidBrightness)
        {
            unsigned int slot = (b.hash * 2654435761u) >> 24;
            while (hash[slot] && hash[slot] != b.hash)
                slot = (slot + 1) & 255;
            if (!hash[slot])
            {
                hash[slot] = b.hash;
                scale[slot] = b.scale;
            }
        }
    }
};

/** The solid's entry in kSolidBrightness, or null. */
const float *BrightnessOf(unsigned int hash)
{
    static const BrightnessTable table;
    if (!hash)
        return nullptr;
    for (unsigned int slot = (hash * 2654435761u) >> 24;; slot = (slot + 1) & 255)
    {
        if (table.hash[slot] == hash)
            return &table.scale[slot];
        if (!table.hash[slot])
            return nullptr;
    }
}

float s_ambientScale = 1.0f;
/** The effect type of the previous draw, and the car level of detail technique set (-1: none yet). */
int s_lastType = -1;
int s_carLod = -1;

const char *SolidOf(const RenderListEntry &entry)
{
    return reinterpret_cast<const char *>(entry.Solid);
}

/** In the main view's depth pass and the depth passes only the diffuse texture matters. */
void BindDepthTextures(TextureInfo **textures, unsigned int flags)
{
    if (dword_982CB4 == textures[0])
        return;
    dword_982CB4 = textures[0];
    sub_6C68B0(textures[0], flags);
    if (textures[0])
        AssignTextures();
}
} // namespace

/** A render list entry's textures and per-solid constants (X360 Stuff's eDataRenderHook): the car level of detail
 * technique, the solid's ambient brightness and LOCALCENTER. */
void __cdecl eDataRender(TextureInfo **textures, unsigned int flags)
{
    const RenderListEntry &entry = *reinterpret_cast<const RenderListEntry *>(
        reinterpret_cast<const char *>(textures) - offsetof(RenderListEntry, Textures));
    const int type = entry.Effect->Type;
    if (s_lastType != type)
        s_carLod = -1;
    Shader &shader = ShaderFor(g_CurrentEffect);
    if ((type == EFFECT_CAR || type == EFFECT_CAR_DEPTH) && !options::kUseFullLODShaderOnTrafficAndCops &&
        shader.lowLod && g_passMode == kPassMain)
    {
        const int lod = (entry.Flags >> 11) & 1;
        if (lod != s_carLod)
        {
            ID3DXEffect *e = shader.Effect();
            e->End();
            e->SetTechnique(lod ? shader.lowLod : shader.mainTechnique);
            e->Begin(nullptr, 0);
            e->BeginPass(0);
        }
        s_carLod = lod;
    }
    const char *solid = SolidOf(entry);
    const bool depthPass = g_passMode == kPassMain && !g_depthPassDone && DepthPassActive();
    if (solid && !depthPass)
    {
        const unsigned int hash = *reinterpret_cast<const unsigned int *>(solid + 0x10);
        s_ambientScale = 1.0f;
        if (const float *scale = BrightnessOf(hash))
            s_ambientScale = hash == 0x0cfa7606 && textures[0]->NameHash != 0x764d5d0c ? 0.75f : *scale;
    }
    if (depthPass || g_passMode == kPassDepth)
        BindDepthTextures(textures, flags);
    else
        sub_6C6D10(textures, flags);
    if (g_CurrentEffect == g_Effects[EFFECT_WORLD_BONE])
        device::SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    if (depthPass)
    {
        s_lastType = type;
        return;
    }
    Shader &current = ShaderFor(g_CurrentEffect);
    if (current.localCenter)
    {
        float centre[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        if (solid)
            for (int i = 0; i < 3; ++i)
                centre[i] = (Float(solid, 0x30 + 4 * i) - Float(solid, 0x20 + 4 * i)) * 0.5f;
        entry.Effect->Effect->SetVector(current.localCenter, centre);
    }
    if (current.ambientColour)
    {
        float ambient[4];
        for (int i = 0; i < 4; ++i)
            ambient[i] = Float(TimeOfDayPointer, 0x30 + 4 * i);
        for (int i = 0; i < 3; ++i)
            ambient[i] *= s_ambientScale;
        g_CurrentEffect->Effect->SetVector(current.ambientColour, ambient);
    }
    s_lastType = type;
}
