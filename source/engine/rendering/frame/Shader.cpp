/**
 * The Xbox 360 effects' handle tables (X360 Stuff's Rendering::MWShader, 0x10037340-0x10038460).
 */
#include "engine/rendering/x360/shader.h"
#include "core/log.h"
#include "engine/rendering/vulkan/d3dx_effect.h"
#include "engine/rendering/vulkan/renderer.h"

extern "C" ID3DXEffectPool *_g_ID3DXEffectPool;

namespace nfs::x360
{
Shader g_shaders[NUM_EFFECTS] = {
    {nullptr, nullptr, &g_Effects[0]},
    {nullptr, nullptr, &g_Effects[1]},
    {nullptr, nullptr, &g_Effects[2]},
    {nullptr, nullptr, &g_Effects[3]},
    {nullptr, nullptr, &g_Effects[4]},
    {nullptr, nullptr, &g_Effects[5]},
    {nullptr, nullptr, &g_Effects[6]},
    {nullptr, nullptr, &g_Effects[7]},
    {nullptr, nullptr, &g_Effects[8]},
    {nullptr, nullptr, &g_Effects[9]},
    {nullptr, nullptr, &g_Effects[10]},
    {nullptr, nullptr, &g_Effects[11]},
    {nullptr, nullptr, &g_Effects[12]},
    {nullptr, nullptr, &g_Effects[13]},
    {nullptr, nullptr, &g_Effects[14]},
    {nullptr, nullptr, &g_Effects[15]},
    {nullptr, nullptr, &g_Effects[16]},
    {nullptr, nullptr, &g_Effects[17]},
    {nullptr, nullptr, &g_Effects[18]},
    {nullptr, nullptr, &g_Effects[19]},
    {nullptr, nullptr, &g_Effects[20]},
    {nullptr, nullptr, &g_Effects[21]},
    {nullptr, nullptr, &g_Effects[22]},
    {nullptr, nullptr, &g_Effects[23]},
    {nullptr, nullptr, &g_Effects[24]},
    {nullptr, nullptr, &g_Effects[25]},
    {nullptr, nullptr, &g_Effects[26]},
    {nullptr, nullptr, &g_Effects[27]},
    {nullptr, nullptr, &g_Effects[28]},
    {nullptr, nullptr, &g_Effects[29]},
    {nullptr, nullptr, &g_Effects[30]}};
namespace
{
struct ParameterName
{
    unsigned short offset;
    bool byName;
    const char *name;
};

const ParameterName kParameters[] = {
    {0x05c, false, "COLORWRITEMODE"},
    {0x060, false, "BLUR_VECTOR"},
    {0x064, false, "IS_MOTIONBLUR_VIGNETTED"},
    {0x068, false, "HeightMapTexture"},
    {0x070, false, "COMBINED_BRIGHTNESS"},
    {0x074, false, "RAINSPLASHTEX"},
    {0x078, false, "CURVE_COEFFS_0"},
    {0x07c, false, "CURVE_COEFFS_1"},
    {0x080, false, "CURVE_COEFFS_2"},
    {0x084, false, "CURVE_COEFFS_3"},
    {0x088, true, "g_fVignetteScale"},
    {0x08c, false, "VolumeMapTexture"},
    {0x090, false, "OPACITYMAPTEXTURE"},
    {0x098, false, "SPECULARMAPTEXTURE"},
    {0x09c, false, "DiffuseMap"},
    {0x0a0, false, "DIFFUSETEX"},
    {0x0a4, false, "WORLDVIEWPROJECTION"},
    {0x0a8, false, "RVM_WORLD_BRIGHTNESS"},
    {0x0ac, false, "SURFACE_REFLECTION"},
    {0x0b0, false, "RAIN_INTENSITY"},
    {0x0b8, false, "WAVENORMALMAPTEX"},
    {0x0bc, false, "REFLECTEDTEX"},
    {0x0c0, false, "TEXTUREANIMOFFSET"},
    {0x0c4, true, "MISCMAP1_TEXTURE"},
    {0x0c8, true, "MISCMAP2_TEXTURE"},
    {0x0cc, true, "MISCMAP3_TEXTURE"},
    {0x0d0, true, "MISCMAP4_TEXTURE"},
    {0x0d4, false, "DESATURATION"},
    {0x0d8, false, "STANDARD_BRIGHTNESS"},
    {0x0dc, false, "CAR_BRIGHTNESS"},
    {0x0e0, false, "Noise2D"},
    {0x094, false, "OpacityMap"},
    {0x0e4, false, "IS_PARALLEX_MAPPED"},
    {0x0e8, true, "IS_PARALLEX_MAPPED"},
    {0x0ec, false, "SHADOWMAP_ALPHA_ENABLED"},
    {0x0f0, false, "SHADOWMAP_ALPHAMIN"},
    {0x0f4, false, "SHADOWMAP_BIAS"},
    {0x0f8, false, "SHADOWMAP_CAR_SHADOW_ENABLED"},
    {0x0fc, false, "SHADOWMAP_ENABLED"},
    {0x100, false, "SHADOWMAP_SCALE_X"},
    {0x104, false, "SHADOWMAP_SCALE_Y"},
    {0x108, false, "AMBIENTCOLOUR"},
    {0x10c, false, "BASEALPHAREF"},
    {0x110, false, "METALLICSCALE"},
    {0x114, false, "SPECULARHOTSPOT"},
    {0x118, false, "DIFFUSEMIN"},
    {0x11c, false, "DIFFUSERANGE"},
    {0x120, false, "SPECULARMIN"},
    {0x124, false, "SPECULARRANGE"},
    {0x128, false, "ENVMAPMIN"},
    {0x12c, false, "ENVMAPANGE"},
    {0x130, false, "SPECULARPOWER"},
    {0x134, false, "ENVMAPPOWER"},
    {0x140, false, "RAINDROPOFFSET"},
    {0x144, false, "LOCALCENTER"},
    {0x148, false, "DISPLACEMAP"},
    {0x14c, false, "NormalMapTexture"},
    {0x150, false, "SKY_DIFFUSESCALE"},
    {0x154, false, "DIFFUSECOLOUR"},
    {0x158, false, "FEMaskMap"},
    {0x15c, false, "FECOLORWRITEFLAG"},
    {0x160, true, "GRASSCOLOUR"},
    {0x164, true, "MAXSHELLS"},
    {0x168, true, "GRASSHEIGHT"},
    {0x16c, true, "DIFFUSESPACE"},
    {0x170, true, "DIFFUSEMIPMAPBIAS"},
    {0x174, true, "NOISESPACE"},
    {0x178, true, "NOISEMIPMAPBIAS"},
    {0x17c, true, "LOWNOISESPACE"},
    {0x180, true, "LOWNOISEINTENSITY"},
    {0x184, true, "SCRUFF"},
    {0x188, true, "CURLTIGHTNESS"},
    {0x18c, true, "LODSTART"},
    {0x190, true, "LODRAMP"},
    {0x194, true, "GRASSGAMMA"},
    {0x198, false, "FOG_BR_PLUS_BM"},
    {0x19c, false, "FOG_ONE_OVER_BRBM"},
    {0x1ac, false, "SUNCOLOR"},
    {0x138, false, "BASEADDRESSU"},
    {0x13c, false, "BASEADDRESSV"},
    {0x1b8, true, "g_fAdaptedLum"},
    {0x1bc, true, "g_fBloomScale"},
    {0x1c0, true, "g_fBrightPassThreshold"},
    {0x1c4, true, "g_fMiddleGray"},
    {0x1cc, false, "VISUAL_EFFECT_VIGNETTE"},
    {0x1c8, false, "VISUAL_EFFECT_BRIGHTNESS"},
    {0x1d0, false, "CULL_MODE"},
};

/** Parameters with an older semantic to fall back on. */
const ParameterName kFallbacks[][2] = {
    {{0x1a0, false, "FOG_CONST1"}, {0x1a0, false, "FOG_CONST_1"}},
    {{0x1a4, false, "FOG_CONST2"}, {0x1a4, false, "FOG_CONST_2"}},
    {{0x1a8, false, "FOG_CONST3"}, {0x1a8, false, "FOG_CONST_3"}},
    {{0x1b4, false, "FOG_DISTANCE_SCALE"}, {0x1b4, false, "FOGVALUE"}},
};

struct TechniqueName
{
    unsigned short offset;
    const char *name;
};

const TechniqueName kTechniques[] = {
    {0x10, "lowlod"},     {0x14, "worldnofog"},     {0x28, "car"},
    {0x2c, "carnormalmap"}, {0x1c, "dryroad"},      {0x18, "wetroad"},
    {0x20, "world"},      {0x24, "worldnormalmap"}, {0x30, "watersea"},
    {0x34, "rvm"},        {0x38, "rvm_no_cubemap"}, {0x3c, "Grass"},
    {0x40, "GrassTransition"}, {0x44, "GrassTransitionNoNormalmap"}, {0x48, "RenderLight"},
    {0x4c, "RenderLightAlphaTex"}, {0x50, "RenderWhite"}, {0x54, "RenderWhiteAlphaTex"},
    {0x58, "ZPrePass"},
};

D3DXHANDLE &At(Shader *shader, unsigned short offset)
{
    return *reinterpret_cast<D3DXHANDLE *>(reinterpret_cast<char *>(shader) + offset);
}

D3DXHANDLE Lookup(ID3DXEffect *effect, const ParameterName &p)
{
    return p.byName ? effect->GetParameterByName(nullptr, p.name) : effect->GetParameterBySemantic(nullptr, p.name);
}

/** Every effect a global setter reaches: the slots'. */
template <typename F> void ForEachEffect(F f)
{
    for (eEffect *e : g_Effects)
        if (e && e->Effect)
            f(e->Effect);
}

/** A global parameter's handle in each slot's effect, looked up again when the slot's effect changes. */
struct GlobalHandles
{
    const char *semantic;
    ID3DXEffect *effects[NUM_EFFECTS];
    D3DXHANDLE handles[NUM_EFFECTS];
};

const int kMaxGlobals = 8;
GlobalHandles g_globals[kMaxGlobals];
int g_globalCount;

GlobalHandles &Globals(const char *semantic)
{
    for (int i = 0; i < g_globalCount; ++i)
        if (g_globals[i].semantic == semantic || lstrcmpA(g_globals[i].semantic, semantic) == 0)
            return g_globals[i];
    GlobalHandles &g = g_globals[g_globalCount < kMaxGlobals ? g_globalCount++ : kMaxGlobals - 1];
    memset(&g, 0, sizeof(g));
    g.semantic = semantic;
    return g;
}

/** Every slot's effect that has the parameter with `semantic`, with its handle. */
template <typename F> void ForEachGlobal(const char *semantic, F f)
{
    GlobalHandles &g = Globals(semantic);
    for (int i = 0; i < NUM_EFFECTS; ++i)
    {
        ID3DXEffect *e = g_Effects[i] ? g_Effects[i]->Effect : nullptr;
        if (!e)
            continue;
        if (g.effects[i] != e)
        {
            g.effects[i] = e;
            g.handles[i] = e->GetParameterBySemantic(nullptr, g.semantic);
        }
        if (g.handles[i])
            f(e, g.handles[i]);
    }
}
} // namespace

ID3DXEffect *Shader::Effect(bool refresh)
{
    if (!slot)
        return effect;
    ID3DXEffect *current = *slot ? (*slot)->Effect : nullptr;
    if (refresh && current != effect)
    {
        effect = current;
        if (current)
        {
            InitParams();
            InitTechniques();
        }
    }
    return current;
}

void Shader::InitParams()
{
    for (const ParameterName &p : kParameters)
        At(this, p.offset) = Lookup(effect, p);
    for (const ParameterName(&f)[2] : kFallbacks)
        if (!(At(this, f[0].offset) = Lookup(effect, f[0])))
            At(this, f[1].offset) = Lookup(effect, f[1]);
}

void Shader::InitTechniques()
{
    mainTechnique = effect->GetTechnique(0);
    struct
    {
        void *creator;
        UINT parameters, techniques, functions;
    } desc = {};
    effect->GetDesc(&desc);
    // As in the plugin, a technique without the annotations keeps the values the one before it had.
    int best = 0, level = -1, shader = 0;
    for (UINT i = 0; i < desc.techniques; ++i)
    {
        effect->GetInt(effect->GetAnnotationByName(effect->GetTechnique(i), "shadowLevel"), &level);
        effect->GetInt(effect->GetAnnotationByName(effect->GetTechnique(i), "shader"), &shader);
        if (level >= 0)
        {
            level = 0;
            if (best < shader)
            {
                best = shader;
                mainTechnique = effect->GetTechnique(i);
            }
        }
    }
    for (const TechniqueName &t : kTechniques)
        At(this, t.offset) = effect->GetTechniqueByName(t.name);
}

void SetGlobalFloat(const char *semantic, float value)
{
    ForEachGlobal(semantic, [&](ID3DXEffect *e, D3DXHANDLE h) { e->SetFloat(h, value); });
}

void SetGlobalFloatByName(const char *name, float value)
{
    ForEachEffect([&](ID3DXEffect *e) {
        if (D3DXHANDLE h = e->GetParameterByName(nullptr, name))
            e->SetFloat(h, value);
    });
}

void SetGlobalVector(const char *semantic, const float *value)
{
    ForEachGlobal(semantic, [&](ID3DXEffect *e, D3DXHANDLE h) { e->SetVector(h, value); });
}
} // namespace nfs::x360
