/**
 * Device resources and global textures of the Xbox 360 rendering (engine/rendering/x360/resources.h).
 */
#include "engine/rendering/x360/resources.h"
#include "game/gen/api.h"
#include "game/platform/Device.h"
#include "game/platform/Effect.h"
#include "game/platform/Platform.h"
#include "legacy/link.h"
#include "core/log.h"
#include "gameplay/world/timeofday.h"
#include "engine/rendering/vulkan/renderer.h"
#include "engine/rendering/x360/frame.h"
#include "engine/rendering/x360/post.h"
#include "engine/rendering/x360/render.h"
#include "engine/rendering/x360/shader.h"

/** The shadow map's size (the immediate GetShadowWidth returns). */
int GetShadowWidth();
extern "C" int __multisamplequality;

namespace nfs::x360
{
unsigned long g_renderWidth = 640;
unsigned long g_renderHeight = 480;
float g_renderAspect = 4.0f / 3.0f;
float g_time;
bool g_wideUi;
float g_screenUnits[2] = {640.0f, 480.0f};
void *g_noiseVolume[2];
void *g_whiteAlpha;
void *g_flatNormal;
void *g_rainSplash[16];
void *g_lookAttributes;

namespace
{
const unsigned int kAttribClass = 0xd32a743f;
const unsigned int kGrassCollection = 0xa79251b8;
const unsigned int kLookCollection = 0x20897ce8;
const DWORD kFormatIntz = 0x5a544e49;
const int kNoiseSize = 32;
const int kNoiseLevels = 6;
const unsigned int kNoiseBytes = 0x24924;

void Release(void *&object)
{
    if (!object)
        return;
    {
        nfs::vk::Renderer *r = nfs::vk::g_renderer;
        r->Release(static_cast<nfs::vk::Object *>(object));
    }
    object = nullptr;
}

int Random(int range)
{
    return NFS_ORIGINAL(0x45da30, int(__cdecl *)(int))(range);
}

/** One noise channel: X360 Stuff converts (random - 32) as unsigned, so the values below 32 come out as 0. */
unsigned char NoiseByte()
{
    const int r = Random(256) - 32;
    return r < 0 ? 0 : static_cast<unsigned char>(static_cast<int>(static_cast<float>(r) * 1.1f));
}

/** The noise volumes' texels: level 0 random, the smaller levels left zero (they read past level 0 in the plugin). */
void FillNoise(unsigned char *data, bool grass)
{
    for (unsigned int i = 0; i < kNoiseBytes; ++i)
        data[i] = 0;
    for (int i = 0; i < kNoiseSize * kNoiseSize * kNoiseSize; ++i)
    {
        unsigned char *texel = data + i * 4;
        texel[0] = grass ? static_cast<unsigned char>(static_cast<int>(static_cast<float>(Random(256)) * 0.33333334f) -
                                                      0x56)
                         : NoiseByte();
        texel[1] = NoiseByte();
        texel[2] = NoiseByte();
        texel[3] = 0xFF;
    }
}

void *CreateNoiseVolume(bool grass)
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    nfs::vk::Texture *volume = r->CreateVolumeTexture(kNoiseSize, kNoiseSize, kNoiseSize, kNoiseLevels,
                                                      D3DFMT_A8R8G8B8);
    if (!volume)
        return nullptr;
    unsigned char *data = static_cast<unsigned char *>(::operator new(kNoiseBytes));
    FillNoise(data, grass);
    const unsigned char *source = data;
    for (int level = 0; level < kNoiseLevels; ++level)
    {
        const int size = kNoiseSize >> level;
        const uint32_t bytes = static_cast<uint32_t>(size * size * size * 4);
        r->UploadTexture(volume, static_cast<uint32_t>(level), 0, source, bytes);
        source += bytes;
    }
    ::operator delete(data);
    return volume;
}

/** A 1x1 texture of one texel value. */
void *CreateSolid(D3DFORMAT format, unsigned int texel)
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    nfs::vk::Texture *t = r->CreateTexture(1, 1, 1, format, false);
    if (t)
        r->UploadTexture(t, 0, 0, &texel, format == D3DFMT_A8 ? 1 : 4);
    return t;
}

void SetTexture(Shader &shader, D3DXHANDLE Shader::*parameter, void *texture)
{
    if (ID3DXEffect *e = shader.Effect())
        e->SetTexture(shader.*parameter, static_cast<IDirect3DBaseTexture9 *>(texture));
}

void *TextureNamed(const char *name)
{
    TextureInfo *info = GetTextureInfo(bStringHash(name), 1, 0);
    return info && info->PlatInfo ? info->PlatInfo->Texture : nullptr;
}

/** The grass effects' 2D noise and noise volume, and the car effects' noise volume (null: none). */
void BindNoise(void *noise2D, void *grassVolume, void *carVolume)
{
    static const int kGrassSlots[2] = {EFFECT_WORLD_NORMAL_DEPTH, EFFECT_TREE_DEPTH};
    for (int slot : kGrassSlots)
    {
        SetTexture(g_shaders[slot], &Shader::noise2D, noise2D);
        SetTexture(g_shaders[slot], &Shader::volumeMapTexture, grassVolume);
    }
    SetTexture(g_shaders[EFFECT_CAR], &Shader::volumeMapTexture, carVolume);
    SetTexture(g_shaders[EFFECT_CAR_DEPTH], &Shader::volumeMapTexture, carVolume);
}

void *CreateAttributes(unsigned int collection)
{
    void *instance = bWareMalloc(0x14, nullptr, 0, 0);
    if (!instance)
        return nullptr;
    const void *c = NFS_ORIGINAL(0x455fd0, const void *(__cdecl *)(unsigned int, unsigned int))(kAttribClass,
                                                                                              collection);
    static_cast<int *>(instance)[1] = 0;
    NFS_ORIGINAL(0x452380, void(__thiscall *)(void *, const void *, unsigned int, void *))(instance, c, 0, nullptr);
    // A missing collection leaves the instance without a layout.
    if (reinterpret_cast<unsigned int>(AttributeLayout(instance)) < 0x400000)
    {
        NFS_ORIGINAL(0x45a430, void(__thiscall *)(void *))(instance);
        bFree(instance);
        return nullptr;
    }
    return instance;
}
} // namespace

double Fmod(double x, double y)
{
    const int whole = static_cast<int>(x / y);
    return x - static_cast<double>(whole) * y;
}

const char *AttributeLayout(void *instance)
{
    return *reinterpret_cast<const char *const *>(static_cast<char *>(instance) + 8);
}

const float *AttributeFloat(void *instance, unsigned int key)
{
    const void *p = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(instance,
                                                                                                         key, 0);
    if (!p)
        p = NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(0);
    return static_cast<const float *>(p);
}

void InitAttributes()
{
    static bool done;
    if (done)
        return;
    done = true;
    g_grassAttributes = CreateAttributes(kGrassCollection);
    g_lookAttributes = CreateAttributes(kLookCollection);
}

void UpdateRenderSize()
{
    g_renderWidth = 640;
    g_renderHeight = 480;
    // X360 Stuff asks for the resolution setting; the rebuild renders at the back buffer's size (render scale, HD), so
    // that is the size the targets and viewports follow.
    if (g_BackBufferSurface)
    {
        UINT width, height;
        device::SurfaceSize(g_BackBufferSurface, width, height);
        if (width && height)
        {
            g_renderWidth = width;
            g_renderHeight = height;
        }
    }
    else
    {
        NFS_ORIGINAL(0x6c27d0, void(__cdecl *)(unsigned long *, unsigned long *))(&g_renderWidth, &g_renderHeight);
    }
    g_renderAspect = static_cast<float>(g_renderWidth) / static_cast<float>(g_renderHeight);
    g_wideUi = g_renderAspect >= 1.77777779f;
}

void InitResources()
{
    g_envMapsStale = true;
    UpdateRenderSize();
    CreatePostTargets();
    nfs::timeofday::NightSkyDeviceLost(false);
    g_depthTexture = g_depthSurface =
        nfs::vk::g_renderer->CreateTexture(g_renderWidth, g_renderHeight, 1, kFormatIntz, true);
    if (!g_depthTexture)
    {
        // Without a depth texture the depth effects are off (the plugin's INTZ check).
        g_depthOfField = false;
        g_motionBlur = g_motionBlur ? 2 : 0;
    }
    g_noiseVolume[0] = CreateNoiseVolume(true);
    g_noiseVolume[1] = CreateNoiseVolume(false);
    g_whiteAlpha = CreateSolid(D3DFMT_A8, 0xFF);
    g_flatNormal = CreateSolid(D3DFMT_A8R8G8B8, 0xFF7F7FFF);
}

void ReleaseResources()
{
    ReleasePostTargets();
    nfs::timeofday::NightSkyDeviceLost(true);
    // The effects hold a reference to each texture they were given (Direct3D 9).
    BindNoise(nullptr, nullptr, nullptr);
    g_depthSurface = nullptr;
    Release(g_depthTexture);
    for (void *&v : g_noiseVolume)
        Release(v);
    Release(g_whiteAlpha);
    Release(g_flatNormal);
}

void LoadGlobalTextures()
{
    char name[32];
    for (int i = 0; i < 16; ++i)
    {
        wsprintfA(name, "RAINSPLASH%02d", i);
        g_rainSplash[i] = TextureNamed(name);
    }
    BindNoise(TextureNamed("2DNOISE"), g_noiseVolume[0], g_noiseVolume[1]);
}
} // namespace nfs::x360
