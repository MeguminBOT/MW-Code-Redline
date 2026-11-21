/**
 * The effects' parameter table and the state they set when they start (original: zPlatform): every eEffect knows the
 * engine's 102 semantics by name and hash, and looks up its own effect's handle for each once the effect is created. The
 * per-effect start hooks set the time of day's colours and the shadow map's textures.
 */
#include <stdlib.h>

#include "game/gen/api.h"
#include "game/platform/Effect.h"
#include "engine/rendering/display/display.h"
#include "legacy/link.h"

extern "C" char *TimeOfDayPointer;
extern "C" int lpData_00901818;
extern "C" int lpData_00901830;
extern "C" int GPUVendor;

NFS_REPLACE(0x6c3d20, ConstructEffect)
NFS_REPLACE(0x6c5500, BindEffectParameters)
NFS_REPLACE(0x6c5e60, SetEffectDefaults)
NFS_REPLACE(0x6c3360, ReleaseEffectTargets)
NFS_REPLACE(0x6c3a40, ReleaseEffectExtras)
NFS_REPLACE(0x6c5b80, StartShadowEffect)
NFS_REPLACE(0x6c6300, SetEffectDiffuse)
NFS_REPLACE(0x6c6450, SetEffectAmbient)
NFS_REPLACE(0x6c6630, SetEffectSkyColour)
NFS_REPLACE(0x6c5150, EffectNothing)
NFS_REPLACE(0x6c5160, EffectNothing)
NFS_REPLACE(0x6c5190, EffectNothing)
NFS_REPLACE(0x6c5240, EffectNothing)
NFS_REPLACE(0x6c52b0, EffectNothing)
NFS_REPLACE(0x6c52c0, EffectNothing)
NFS_REPLACE(0x6c52f0, EffectNothing)
NFS_REPLACE(0x6c5320, EffectNothing)
NFS_REPLACE(0x6c5350, EffectNothing)
NFS_REPLACE(0x6c5360, EffectNothing)
NFS_REPLACE(0x6c57d0, EffectNothing)
NFS_REPLACE(0x6c58b0, EffectNothing)
NFS_REPLACE(0x6c5b60, EffectNothing)
NFS_REPLACE(0x6c5b70, EffectNothing)
NFS_REPLACE(0x6c5c70, EffectNothing)
NFS_REPLACE(0x6c5c80, EffectNothing)
NFS_REPLACE(0x6c5cf0, EffectNothing)
NFS_REPLACE(0x6c5d00, EffectNothing)
NFS_REPLACE(0x6c5d10, EffectNothing)
NFS_REPLACE(0x6c6620, EffectNothing)
NFS_REPLACE(0x6c6710, EffectNothing)

namespace
{
const int kParameterCount = 102;
const unsigned int kBaseEffectVtable = 0x8af1f8;

/** The semantics, in the table's order. */
const char *const kSemantics[kParameterCount] = {
    "AMBIENT", "AMBIENTCOEFF", "AMBIENTCOLOUR", "BLENDSTATE", "BASEMAGTEXTUREFILTER", "BASEMINTEXTUREFILTER",
    "BASEMIPTEXTUREFILTER", "BASETEXTUREFILTERPARAM", "BLENDMATRICES", "COLORWRITEMODE", "CLOUDSCROLL", "CULL_MODE",
    "DIFFUSECOLOUR", "DiffuseMap", "DIFFUSEMIN", "DIFFUSERANGE", "DIFFUSETEX", "DISPLACEMAP", "DOFFALLOFF", "DOFOFFSET1",
    "DOFOFFSET2", "ENVMAPANGE", "ENVMAPMIN", "ENVMAPPOWER", "EnvMapTexture", "FFAMBIENTCOLOR", "FFSKYBLENDFACTOR",
    "FFSKYCOLOR", "FECOLORWRITEFLAG", "FEMaskMap", "FILTERBLEND", "FILTERTEXTURE0", "FILTERTEXTURE1", "FILTERTEXTURE2",
    "FILTERTEXTURE3", "FILTERWEIGHTS", "FOCALRANGE", "FOGCOLOR", "FOGON", "FOG_BR_PLUS_BM", "FOG_ONE_OVER_BRBM",
    "FOG_CONST_1", "FOG_CONST_2", "FOG_CONST_3", "FOGVALUE", "GAINMAP", "HARMONIC", "HEADLIGHTCLIPMAT", "HEADLIGHTCLIPTEX",
    "HEADLIGHTCOLOR", "HEADLIGHTTEX", "HEADLIGHTWVP", "HeightMapTexture", "INVSHADOWSTRENGTH", "LIGHTCOLOR", "LIGHTDIR",
    "LOCALEYEPOS", "LOCALLIGHTDIRVEC", "LOCALCOLOURMATRIX", "LOCALDIRECTIONMATRIX", "MOTIONALPHA", "NormalMapTexture",
    "OPACITYMAPTEXTURE", "OVERBRIGHTGREYSCALE", "OVERBRIGHTOFFSET", "RAINDROPOFFSET", "RAINDROPALPHA", "RAMPMAP",
    "REFLECTEDPROJ", "REFLECTEDTEX", "REFLECTEDTEXMIN", "SATURATIONSCALE", "SCREENOFFSET", "SHADOWCOLORMAP",
    "SHADOWCURRENTGEN_COLOR", "SHADOWLEVEL", "SHADOWMAP", "SHADOWMAPSCALE", "SHADOWTRANSFORM", "SPECULARCOLOUR",
    "SPECULARMAPTEXTURE", "SPECULARMIN", "SPECULARPOWER", "SPECULARRANGE", "SPLINE", "SKYMORPHALPHA", "SKYMORPHALPHA2",
    "SKYMORPHTEXTURE", "SKYRGBA", "SKY_TEXTURE_MISC_1", "SKY_TEXTURE_MISC_2", "SKY_TEXTURE_MISC_3", "SKY_DIFFUSESCALE",
    "SUNCOLOR", "TEXTUREOFFSET", "TEXTUREOFFSETMATRIX", "TINTCOLOR", "LOCALWORLD", "WINDOWREFLECTION", "WORLDLIGHTDIRVEC",
    "WORLDVIEW", "WORLDVIEWPROJECTION",
};

/** Handles by the table's byte offsets (eEffect::Parameter). */
enum ParameterOffset
{
    kAmbientColour = 0x74,
    kBlendState = 0x9c,
    kMagFilter = 0xc4,
    kMinFilter = 0xec,
    kMipFilter = 0x114,
    kFilterParam = 0x13c,
    kColourWriteMode = 0x18c,
    kCullMode = 0x1dc,
    kDiffuseColour = 0x204,
    kEnvMapTexture = 0x3e4,
    kAmbientFixed = 0x40c,
    kSkyColourFixed = 0x45c,
    kFeColourWrite = 0x484,
    kFogOn = 0x614,
    kShadowColourMap = 0xb8c,
    kShadowCurrentGenColour = 0xbb4,
    kShadowMap = 0xc04,
    kShadowMapScale = 0xc2c,
    kWorldLightDirection = 0xf9c,
};

/** Game state the hooks read. */
template <typename T> T &At(unsigned int address)
{
    return *reinterpret_cast<T *>(address);
}

const unsigned int kVideoCaps = 0x93d958;
const unsigned int kMaxAnisotropy = 0x93d984;
const int kVendorMatrox = 0x102b;
const unsigned int kAnisotropicMin = 0x400;
const unsigned int kAnisotropicMag = 0x4000000;
const int kAnisotropic = 2;
const int kFilterLinear = 2;
const int kEffectFilter = 11;
const int kColourWriteAll = 0xf;
const int kCullCw = 1;

/** ID3DXEffect::GetDesc's and GetParameterDesc's results (d3dx9effect.h). */
struct EffectDesc
{
    const char *Creator;
    UINT Parameters;
    UINT Techniques;
    UINT Functions;
};

struct ParameterDesc
{
    const char *Name;
    const char *Semantic;
    UINT Class, Type, Rows, Columns, Elements, Annotations, StructMembers;
    DWORD Flags;
    UINT Bytes;
};

int GetShadowWidth()
{
    return NFS_ORIGINAL(0x6c86b0, int(__cdecl *)())();
}

ID3DXEffect *EffectOf(eEffect *effect)
{
    return effect->Effect;
}

/** The time of day's colour at `offset`, each channel at most 1 after `scale`, and its fourth as it is. */
void TimeOfDayColour(int offset, float scale, float out[4])
{
    const float *c = reinterpret_cast<const float *>(TimeOfDayPointer + offset);
    for (int i = 0; i < 3; ++i)
    {
        const float v = scale * c[i];
        out[i] = v < 1.0f ? v : 1.0f;
    }
    out[3] = c[3];
}

/** The direction the time of day's light shines in (towards the ground). */
void LightDirection(float out[4])
{
    bVector3 sun = *reinterpret_cast<const bVector3 *>(TimeOfDayPointer + 0x180);
    bNormalize(&sun, &sun);
    out[0] = sun.x * -1.0f;
    out[1] = sun.y * -1.0f;
    out[2] = sun.z * -1.0f;
    out[3] = 0.0f;
}

unsigned int Channel(float v)
{
    const unsigned int c = static_cast<unsigned int>(static_cast<int>(v));
    return c < 0xff ? c : 0xff;
}

int FilterFor(unsigned int cap)
{
    if (lpData_00901818 != kAnisotropic || !(At<unsigned int>(kVideoCaps) & cap))
        return kFilterLinear;
    return kFilterLinear + (GPUVendor != kVendorMatrox ? 1 : 0);
}
} // namespace

void __fastcall EffectNothing(void *)
{
}

void __fastcall ConstructEffect(eEffect *effect, void *)
{
    *reinterpret_cast<unsigned int *>(effect) = kBaseEffectVtable;
    effect->HasShaders = false;
    effect->Parameters = static_cast<eEffectParameter *>(malloc(kParameterCount * sizeof(eEffectParameter)));
    for (int i = 0; i < 4; ++i)
    {
        effect->LevelTechniques[i] = -1;
        effect->LevelShaders[i] = -1;
    }
    effect->MaxLevel = 0;
    for (int i = 0; i < kParameterCount; ++i)
    {
        eEffectParameter &p = effect->Parameters[i];
        bStrCpy(p.Name, kSemantics[i]);
        p.Hash = bStringHash(kSemantics[i]);
        p.Handle = nullptr;
    }
    effect->Effect = nullptr;
}

void __fastcall BindEffectParameters(eEffect *effect, void *)
{
    ID3DXEffect *e = EffectOf(effect);
    EffectDesc desc;
    e->GetDesc(&desc);
    for (UINT i = 0; i < desc.Parameters; ++i)
    {
        D3DXHANDLE handle = e->GetParameter(nullptr, i);
        ParameterDesc parameter;
        e->GetParameterDesc(handle, &parameter);
        if (!parameter.Semantic)
            continue;
        const unsigned int hash = bStringHash(parameter.Semantic);
        for (int k = 0; k < kParameterCount; ++k)
            if (effect->Parameters[k].Hash == hash)
            {
                effect->Parameters[k].Handle = handle;
                break;
            }
    }
}

void __fastcall SetEffectDefaults(eEffect *effect, void *)
{
    ID3DXEffect *e = EffectOf(effect);
    if (D3DXHANDLE h = effect->Parameter(kCullMode))
        e->SetInt(h, kCullCw);
    if (D3DXHANDLE h = effect->Parameter(kBlendState))
    {
        int *blend = &At<int>(0x93de20);
        blend[0] = 0;
        blend[1] = 0;
        blend[2] = 0;
        blend[3] = 5;
        blend[4] = 6;
        e->SetIntArray(h, blend, 5);
    }
    if (D3DXHANDLE h = effect->Parameter(kMinFilter))
        e->SetInt(h, FilterFor(kAnisotropicMin));
    if (D3DXHANDLE h = effect->Parameter(kMagFilter))
        e->SetInt(h, FilterFor(kAnisotropicMag));
    if (D3DXHANDLE h = effect->Parameter(kMipFilter))
        e->SetInt(h, lpData_00901818 != 0 ? 2 : 1);
    if (D3DXHANDLE h = effect->Parameter(kFogOn))
        e->SetInt(h, 1);
    if (D3DXHANDLE h = effect->Parameter(kFilterParam))
    {
        // The Video page's Anisotropy, within what the device offers.
        const int most = At<int>(kMaxAnisotropy), wanted = nfs::display::Anisotropy();
        e->SetInt(h, lpData_00901818 == kAnisotropic ? (wanted < most ? wanted : most) : 1);
    }
    if (D3DXHANDLE h = effect->Parameter(kFeColourWrite))
        e->SetInt(h, kColourWriteAll);
    if (effect->Type == kEffectFilter)
        e->SetInt(effect->Parameter(kColourWriteMode), kColourWriteAll);
}

void __cdecl ReleaseEffectExtras()
{
}

void __cdecl ReleaseEffectTargets()
{
    auto release = [](unsigned int address) {
        IUnknown *&object = At<IUnknown *>(address);
        if (object)
            object->Release();
        object = nullptr;
    };
    for (int i = 0; i < 2; ++i)
    {
        IUnknown *a = At<IUnknown *>(0x93de60 + i * 4), *b = At<IUnknown *>(0x93def8 + i * 4);
        if (a)
            a->Release();
        if (b)
            b->Release();
        At<IUnknown *>(0x93de60 + i * 4) = nullptr;
        At<IUnknown *>(0x93def8 + i * 4) = nullptr;
    }
    for (int i = 0; i < 4; ++i)
    {
        release(0x982990 + i * 4);
        release(0x982964 + i * 4);
    }
    release(0x982960);
    release(0x93e7bc);
    release(0x93e7b8);
    if (At<IUnknown *>(0x982c64))
    {
        release(0x982c64);
        At<IUnknown *>(0x982c68)->Release();
        At<IUnknown *>(0x982c68) = nullptr;
    }
    ReleaseEffectExtras();
}

/** The shadow effect's start: the front end's environment map, and the shadow map and its texel size while shadows are
 * on and the player view draws. */
void __fastcall StartShadowEffect(eEffect *effect, void *)
{
    if (At<char>(0x8f945b))
    {
        eEffect *world = At<eEffect *>(0x982c80);
        void *environment = At<int>(0x925e90) == 6 ? At<void *>(0x982a30) : At<void *>(0x982a64);
        world->Effect->SetTexture(world->Parameter(kEnvMapTexture), static_cast<IDirect3DBaseTexture9 *>(environment));
    }
    if (lpData_00901830 <= 0 || At<int *>(0x982a20)[1] != 1)
        return;
    ID3DXEffect *e = EffectOf(effect);
    D3DXHANDLE colourOrMap = effect->Parameter(kShadowMap);
    if (At<int>(0x982c08))
    {
        e->SetTexture(effect->Parameter(kShadowMap), At<IDirect3DBaseTexture9 *>(0x982cf8));
        colourOrMap = effect->Parameter(kShadowColourMap);
    }
    e->SetTexture(colourOrMap, At<IDirect3DBaseTexture9 *>(0x982cf0));
    e->SetFloat(effect->Parameter(kShadowMapScale), 1.0f / static_cast<float>(GetShadowWidth()));
}

void __cdecl SetEffectDiffuse(eEffect *effect)
{
    float colour[4];
    TimeOfDayColour(0x20, At<float>(0x9018c4), colour);
    ID3DXEffect *e = EffectOf(effect);
    if (D3DXHANDLE h = effect->Parameter(kDiffuseColour))
        e->SetVector(h, static_cast<const void *>(colour));
    if (!effect->HasShaders && effect->Parameter(kWorldLightDirection))
    {
        float direction[4];
        LightDirection(direction);
        e->SetVector(effect->Parameter(kWorldLightDirection), static_cast<const void *>(direction));
    }
}

void __cdecl SetEffectAmbient(eEffect *effect)
{
    float colour[4];
    TimeOfDayColour(0x30, 1.0f, colour);
    ID3DXEffect *e = EffectOf(effect);
    if (effect->Parameter(kAmbientColour) && effect->HasShaders)
    {
        e->SetVector(effect->Parameter(kAmbientColour), static_cast<const void *>(colour));
        return;
    }
    if (!effect->Parameter(kAmbientFixed))
        return;
    float direction[4];
    LightDirection(direction);
    e->SetVector(effect->Parameter(kWorldLightDirection), static_cast<const void *>(direction));
    const float alpha = colour[3];
    const unsigned int r = Channel(colour[0] * alpha * 255.0f), g = Channel(colour[1] * alpha * 255.0f),
                       b = Channel(colour[2] * alpha * 255.0f);
    e->SetInt(effect->Parameter(kAmbientFixed), static_cast<INT>(0xff000000u | r << 16 | g << 8 | b));
}

void __fastcall SetEffectSkyColour(eEffect *effect, void *)
{
    float colour[4];
    const float blend = At<float>(0x8fae68);
    const bool reflection = *At<int *>(0x982a20) != 0;
    ID3DXEffect *e = EffectOf(effect);
    if (effect->HasShaders)
    {
        colour[0] = At<float>(0x9018d8);
        colour[1] = At<float>(0x9018d4);
        colour[2] = At<float>(0x9018d0);
        float alpha = At<float>(0x9018cc);
        if (reflection)
        {
            colour[0] = colour[1] = colour[2] = 0.5f;
            alpha = At<float>(0x9018c8);
        }
        colour[3] = alpha * blend;
        e->SetVector(effect->Parameter(kShadowCurrentGenColour), static_cast<const void *>(colour));
        return;
    }
    colour[0] = colour[1] = colour[2] = 0.0f;
    colour[3] = reflection ? blend + blend : blend;
    e->SetVector(effect->Parameter(kSkyColourFixed), static_cast<const void *>(colour));
}
