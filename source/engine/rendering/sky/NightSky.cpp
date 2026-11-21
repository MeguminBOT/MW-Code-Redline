/**
 * The night sky (gameplay/world/timeofday.h, docs/NIGHT.md): Carbon's night dome, GLOBAL\NIGHTSKY.DOM (tools/night.py), drawn
 * with the rebuild's own effect (assets/shaders/nightsky.fx) around the camera at the far plane, over the game's sky as far as
 * the hour's NightSky says: faded in at dusk, alone at night. The moon and its halo move to where the clock puts the
 * moon (Carbon draws them at azimuth 143, elevation 17.4). Its textures are GLOBAL\NIGHTSKY.BIN, a texture pack loaded
 * with the global data. Not in the original game.
 */
#include <string.h>
#include <windows.h>

#include "game/gen/api.h"
#include "game/platform/Device.h"
#include "game/platform/Effect.h"
#include "game/platform/Platform.h"
#include "core/log.h"
#include "gameplay/world/timeofday.h"
#include "engine/rendering/vulkan/d3dx_effect.h"
#include "engine/rendering/vulkan/renderer.h"
#include "engine/rendering/x360/frame.h"
#include "engine/resources/pack.h"

namespace
{
const char kDomePath[] = "GLOBAL\\NIGHTSKY.DOM";
const unsigned int kMagic = 0x594b534e; // "NSKY"
const unsigned int kVersion = 1;
const int kMaxLayers = 8;
/** Where Carbon's dome has the moon (world azimuth and elevation, degrees). */
const float kDomeMoonAzimuth = 143.0f;
const float kDomeMoonElevation = 17.4f;
/** bAngle units (a turn is 65536) per degree. */
const float kAnglePerDegree = 65536.0f / 360.0f;

enum Blend
{
    kOpaque,
    kAlpha,
    kAdd,
    /** The opaque layer faded over the game's sky. */
    kFade,
    kBlends
};

struct Vertex
{
    float position[3];
    D3DCOLOR colour;
    float uv[2];
};

/** A layer as NIGHTSKY.DOM has it: its texture's name hash, blend, addressing, scroll per second and brightness. */
struct LayerHeader
{
    unsigned int texture;
    unsigned int blend;
    unsigned int wrap;
    float scroll[2];
    float brightness;
    unsigned int count;
};

struct Layer
{
    LayerHeader header;
    const Vertex *vertices;
};

const D3DVERTEXELEMENT9 kElements[] = {
    {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
    {0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    D3DDECL_END(),
};

struct Sky
{
    bool loaded;
    unsigned char *data;
    Layer layers[kMaxLayers];
    int count;
    ID3DXEffect *effect;
    D3DXHANDLE worldViewProj, layer, texture;
    D3DXHANDLE techniques[kBlends][2];
    void *declaration;
    float seconds;
    int frame;
};

Sky g_sky;

void LoadDome()
{
    uint32_t read = 0;
    g_sky.data = nfs::pack::ReadFile(kDomePath, read);
    if (!g_sky.data)
    {
        nfs::Log("night sky: no %s", kDomePath);
        return;
    }
    const unsigned int *head = reinterpret_cast<const unsigned int *>(g_sky.data);
    if (read < 12 || head[0] != kMagic || head[1] != kVersion)
    {
        nfs::Log("night sky: %s is not a version %u dome", kDomePath, kVersion);
        return;
    }
    const unsigned char *p = g_sky.data + 12;
    const unsigned char *end = g_sky.data + read;
    for (unsigned int i = 0; i < head[2] && g_sky.count < kMaxLayers; ++i)
    {
        Layer &layer = g_sky.layers[g_sky.count];
        if (p + sizeof(LayerHeader) > end)
            break;
        memcpy(&layer.header, p, sizeof(LayerHeader));
        p += sizeof(LayerHeader);
        layer.vertices = reinterpret_cast<const Vertex *>(p);
        p += layer.header.count * sizeof(Vertex);
        if (p > end || layer.header.blend >= kFade)
            break;
        ++g_sky.count;
    }
    nfs::Log("night sky: %d layers from %s", g_sky.count, kDomePath);
}

/** Row-vector rotation (3x3 in a 4x4) that carries the dome's moon to the clock's: about the vertical to azimuth 0,
 * up or down to the moon's elevation, about the vertical to its azimuth. */
void MoonRotation(float azimuth, float elevation, float out[3][3])
{
    const unsigned short a0 = static_cast<unsigned short>(static_cast<int>(-kDomeMoonAzimuth * kAnglePerDegree));
    const unsigned short a1 = static_cast<unsigned short>(static_cast<int>(azimuth * kAnglePerDegree));
    const unsigned short e =
        static_cast<unsigned short>(static_cast<int>((elevation - kDomeMoonElevation) * kAnglePerDegree));
    // Column-vector matrices: Rz(a1) . Ey(e) . Rz(a0), Ey raising +X towards +Z.
    const float rz0[3][3] = {{bCos(a0), -bSin(a0), 0.0f}, {bSin(a0), bCos(a0), 0.0f}, {0.0f, 0.0f, 1.0f}};
    const float ey[3][3] = {{bCos(e), 0.0f, -bSin(e)}, {0.0f, 1.0f, 0.0f}, {bSin(e), 0.0f, bCos(e)}};
    const float rz1[3][3] = {{bCos(a1), -bSin(a1), 0.0f}, {bSin(a1), bCos(a1), 0.0f}, {0.0f, 0.0f, 1.0f}};
    float t[3][3], m[3][3];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            t[r][c] = ey[r][0] * rz0[0][c] + ey[r][1] * rz0[1][c] + ey[r][2] * rz0[2][c];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            m[r][c] = rz1[r][0] * t[0][c] + rz1[r][1] * t[1][c] + rz1[r][2] * t[2][c];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            out[r][c] = m[c][r];
}

void LoadEffect()
{
    g_sky.effect = nfs::vk::CreateEffect("NFS_nightsky");
    if (!g_sky.effect)
    {
        nfs::Log("night sky: no effect NFS_nightsky");
        return;
    }
    ID3DXEffect *e = g_sky.effect;
    g_sky.worldViewProj = e->GetParameterByName(nullptr, "WorldViewProj");
    g_sky.layer = e->GetParameterByName(nullptr, "Layer");
    g_sky.texture = e->GetParameterByName(nullptr, "LayerTex");
    static const char *const kNames[kBlends][2] = {{"opaque_clamp", "opaque_wrap"},
                                                   {"alpha_clamp", "alpha_wrap"},
                                                   {"add_clamp", "add_wrap"},
                                                   {"fade_clamp", "fade_wrap"}};
    for (int b = 0; b < kBlends; ++b)
        for (int w = 0; w < 2; ++w)
            g_sky.techniques[b][w] = e->GetTechniqueByName(kNames[b][w]);
}

bool Ready()
{
    if (!g_sky.loaded)
    {
        g_sky.loaded = true;
        LoadDome();
        LoadEffect();
    }
    return g_sky.count && g_sky.effect;
}

void SetDeclaration()
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    if (!g_sky.declaration)
        g_sky.declaration = r->CreateVertexLayout(kElements);
    r->SetVertexLayout(static_cast<nfs::vk::VertexLayout *>(g_sky.declaration));
}

void *LayerTexture(unsigned int key)
{
    TextureInfo *info = GetTextureInfo(key, 0, 0);
    return info && info != DefaultTextureInfo && info->PlatInfo ? info->PlatInfo->Texture : nullptr;
}
} // namespace

namespace nfs::timeofday
{
bool DrawNightSky(eView *view, float gain, bool mirrored)
{
    const float fade = NightSky();
    if (fade <= 0.0f || !view || !view->pCamera || !view->PlatInfo || !Ready())
        return false;
    if (g_sky.frame != WorldTimeFrames)
    {
        g_sky.frame = WorldTimeFrames;
        g_sky.seconds += WorldTimeElapsed;
    }
    // The view's world to screen matrix (row vectors); the dome moved to the camera first, the moon's layers also
    // turned to the moon.
    const float *m = reinterpret_cast<const float *>(&view->PlatInfo->WorldScreenMatrix);
    const bVector3 &eye = view->pCamera->CurrentKey.Position;
    float moonAzimuth, moonElevation;
    Moon(moonAzimuth, moonElevation);
    float moon[3][3];
    MoonRotation(moonAzimuth, moonElevation, moon);
    static const unsigned int kMoonLayers[2] = {bStringHash("SKY_NIGHT_MOON_02_D"), bStringHash("SFX_NIGHT_HALO_01_D")};
    float wvp[2][16];
    for (int w = 0; w < 2; ++w)
    {
        float world[4][4] = {
            {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {eye.x, eye.y, eye.z, 1.0f}};
        if (w)
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    world[r][c] = moon[r][c];
        // The wet road's mirror looks at the dome upside down, about the camera's height (it is at the far plane).
        if (mirrored)
            for (int r = 0; r < 3; ++r)
                world[r][2] = -world[r][2];
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 4; ++k)
                wvp[w][r * 4 + k] =
                    world[r][0] * m[k] + world[r][1] * m[4 + k] + world[r][2] * m[8 + k] + world[r][3] * m[12 + k];
    }
    ID3DXEffect *e = g_sky.effect;
    SetDeclaration();
    const float brightness = DomeBrightness() * gain;
    for (int i = 0; i < g_sky.count; ++i)
    {
        const Layer &layer = g_sky.layers[i];
        const bool moonLayer = layer.header.texture == kMoonLayers[0] || layer.header.texture == kMoonLayers[1];
        if (moonLayer && moonElevation < 0.0f)
            continue;
        void *texture = LayerTexture(layer.header.texture);
        // Over the game's sky the opaque layer is faded in instead.
        const int blend = layer.header.blend == kOpaque && fade < 1.0f ? kFade : static_cast<int>(layer.header.blend);
        D3DXHANDLE technique = g_sky.techniques[blend][layer.header.wrap ? 1 : 0];
        if (!texture || !technique || layer.header.count < 3)
            continue;
        const float constants[4] = {layer.header.brightness * brightness, fade, layer.header.scroll[0] * g_sky.seconds,
                                    layer.header.scroll[1] * g_sky.seconds};
        e->SetMatrix(g_sky.worldViewProj, wvp[moonLayer ? 1 : 0]);
        e->SetTexture(g_sky.texture, static_cast<IDirect3DBaseTexture9 *>(texture));
        e->SetVector(g_sky.layer, constants);
        e->SetTechnique(technique);
        e->Begin(nullptr, 0);
        e->BeginPass(0);
        device::DrawPrimitiveUP(D3DPT_TRIANGLELIST, layer.header.count / 3, layer.vertices, sizeof(Vertex));
        e->EndPass();
        e->End();
    }
    e->SetTexture(g_sky.texture, nullptr);
    return true;
}

void NightSkyDeviceLost(bool lost)
{
    if (!g_sky.effect)
        return;
    if (lost)
        g_sky.effect->OnLostDevice();
    else
        g_sky.effect->OnResetDevice();
}

void ReleaseNightSky()
{
    if (g_sky.effect)
    {
        g_sky.effect->SetTexture(g_sky.texture, nullptr);
        g_sky.effect->Release();
        g_sky.effect = nullptr;
    }
    g_sky.loaded = false;
    g_sky.count = 0;
    ::operator delete(g_sky.data);
    g_sky.data = nullptr;
}
} // namespace nfs::timeofday
