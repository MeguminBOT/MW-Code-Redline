/**
 * The Xbox 360 frame around and inside the views (X360 Stuff's Rendering::eRenderWorldMultiPlayer 0x1002ff20, which
 * takes the place of the views call in eDisplayFrame, and its hooks in ___eDisplayFrame: RoadReflectionPreRender
 * 0x10032840, EnvMapPreRender 0x100327a0, DrawCarsInCubemaps 0x100326c0).
 */
#include "game/gen/api.h"
#include "game/platform/Device.h"
#include "game/platform/Effect.h"
#include "game/platform/Platform.h"
#include "legacy/link.h"
#include "dev/profile.h"
#include "gameplay/world/timeofday.h"
#include "engine/rendering/x360/frame.h"
#include "engine/rendering/x360/post.h"
#include "engine/rendering/x360/render.h"
#include "engine/rendering/x360/resources.h"
#include "engine/rendering/x360/shader.h"

extern "C" char *TimeOfDayPointer;
extern "C" int CurrentViewMode;
extern "C" int __multisamplequality;
extern "C" int lpData_009017bc;
extern "C" int lpData_00901828;
extern "C" unsigned char byte_8F9B5C;
extern "C" void *pVisualTreatmentPlat;

extern "C" void __cdecl sub_6E2F50();
extern "C" void __cdecl sub_750B10(eView *view, int);
void RenderViews();

namespace nfs::x360
{
int g_envFace;
bool g_envMapsStale = true;
float g_frameSeconds;
void *g_basePoly;

namespace
{
const int kRearCubeFace = 0x14;

void *TextureNamed(const char *name)
{
    TextureInfo *info = GetTextureInfo(bStringHash(name), 1, 0);
    return info && info->PlatInfo ? info->PlatInfo->Texture : nullptr;
}

/** Seconds from `since` to now on the performance counter. Only the difference is converted to floating point:
 * Direct3D 9 runs the FPU at single precision, which cannot hold an absolute time stamp. */
float SecondsSince(const LARGE_INTEGER &since)
{
    static LARGE_INTEGER frequency;
    if (!frequency.QuadPart)
        QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const LONGLONG ticks = now.QuadPart - since.QuadPart;
    if (ticks < 0)
        return 0.0f;
    if (ticks > 0x7fffffff)
        return 1.0f;
    return static_cast<float>(static_cast<int>(ticks)) / static_cast<float>(frequency.QuadPart);
}

void UpdateVisualTreatment(eView *view)
{
    NFS_ORIGINAL(0x74c000, void(__thiscall *)(void *, eView *))(pVisualTreatmentPlat, view);
}
} // namespace

void RoadReflectionPreRender()
{
    device::SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, FALSE);
    Shader &sky = g_shaders[EFFECT_SKY];
    if (ID3DXEffect *e = sky.Effect())
        e->SetFloat(sky.standardBrightness, nfs::timeofday::RoadReflectionSkyGain() * nfs::timeofday::SkyBrightness(2.0f));
    void *base = TextureNamed("BASEPOLY");
    static const int kMasked[3] = {EFFECT_FE, EFFECT_FE_MASK, EFFECT_WORLD_PRELIT};
    for (int slot : kMasked)
    {
        Shader &s = g_shaders[slot];
        if (ID3DXEffect *e = s.Effect())
            e->SetTexture(s.feMaskMap, static_cast<IDirect3DBaseTexture9 *>(base));
    }
}

void EnvMapPreRender(int face)
{
    Shader &sky = g_shaders[EFFECT_SKY];
    if (ID3DXEffect *e = sky.Effect())
        e->SetFloat(sky.standardBrightness, *reinterpret_cast<const float *>(TimeOfDayPointer + 0x64));
    Shader &prelit = g_shaders[EFFECT_WORLD_PRELIT];
    if (ID3DXEffect *e = prelit.Effect())
        e->SetInt(prelit.feColorWriteFlag, 7);
    g_envFace = face;
}

void DrawCarsInCubemaps()
{
    sub_6E2F50();
    if (g_envFace != kRearCubeFace && !(options::kCarsInAllCubemapFaces && CurrentViewMode == 2))
        return;
    eEffect *prelit = g_Effects[EFFECT_WORLD_PRELIT];
    prelit->Start();
    g_CurrentEffect = prelit;
    device::SetVertexDeclaration(prelit);
    prelit->Effect->Begin(nullptr, 0);
    sub_750B10(&eViews[g_envFace], 0);
    prelit->End();
    prelit->Effect->End();
    g_CurrentEffect = nullptr;
    sub_6E2F50();
}

void *FrontEndBasePoly()
{
    return g_basePoly;
}

void SetFrontEndConstants()
{
    static const float kWhite[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    void *base = TextureNamed("BASEPOLY");
    g_basePoly = base;
    static const int kSlots[3] = {EFFECT_FE, EFFECT_FE_MASK, EFFECT_WORLD_PRELIT};
    for (int slot : kSlots)
    {
        Shader &s = g_shaders[slot];
        ID3DXEffect *e = s.Effect();
        if (!e)
            continue;
        if (s.diffuseColour)
            e->SetVector(s.diffuseColour, kWhite);
        if (base && s.feMaskMap)
            e->SetTexture(s.feMaskMap, static_cast<IDirect3DBaseTexture9 *>(base));
        if (s.standardBrightness)
            e->SetFloat(s.standardBrightness, 2.0f);
    }
}

void PreDisplay()
{
    SimpleModelAnim::Update();
    UpdateLightFlareParameters();
    UpdateRenderSize();
}

void RenderWorld()
{
    // Real time since the last frame. X360 Stuff also held the front end to 60 frames per second here; the game's own
    // frame rate setting decides that now (360, VRR or uncapped alike).
    static LARGE_INTEGER last;
    if (!last.QuadPart)
        QueryPerformanceCounter(&last);
    g_frameSeconds = SecondsSince(last);
    QueryPerformanceCounter(&last);
    LoadGlobalTextures();
    if (g_depthSurface)
    {
        // The main view's depth pass draws depth alone (Direct3D 9 drew its alpha into a colour target too).
        if (__multisamplequality)
            device::SetColorTarget(nullptr);
        device::SetDepthTarget(g_depthSurface);
        device::SetViewport(0, 0, g_renderWidth, g_renderHeight);
        device::Clear(D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    }
    g_envFace = 0x11;
    if (CurrentViewMode == 3)
    {
        // Both player views in turn, the second through the first's slot.
        const int detail = lpData_009017bc;
        lpData_009017bc = 0;
        RenderViews();
        g_envFace = 0x11;
        eView first = eViews[1], mirror = eViews[3];
        eViews[1] = eViews[2];
        if (eViews[1].Active && eViews[1].RenderTarget->unknown_10)
            RenderViews();
        eViews[1] = first;
        eViews[3] = mirror;
        lpData_009017bc = detail;
    }
    else
    {
        RenderViews();
    }
    if (byte_8F9B5C && lpData_00901828 && pVisualTreatmentPlat)
        UpdateVisualTreatment(&eViews[1]);
    device::SetViewport(0, 0, g_renderWidth, g_renderHeight);
    {
        nfs::profile::Scope profile(nfs::profile::kPost);
        ExecutePostProcessing();
    }
    eViews[1].RenderTarget->Width = static_cast<int>(g_renderWidth);
    eViews[1].RenderTarget->Height = static_cast<int>(g_renderHeight);
}
} // namespace nfs::x360

/** The end of a frame's display (ePostDisplay): X360 Stuff no longer updates the screen effects here. */
void ePostDisplay()
{
    UpdateTextureAnimations();
    eResestLightFlarePool();
    if (QueryFlushAccumulationBuffer())
        AccumulationBufferFlushed();
}
