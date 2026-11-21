/**
 * The views of a frame (original: zPlatform ___eDisplayFrame 0x6de300), drawn between the two front-end passes: the
 * road reflection, the environment map faces, then the player's view (sky, scenery, world models, cars, flares,
 * particles, rain). Both renderers run this version, with X360 Stuff's changes: the Xbox 360 per-pass effect setup
 * (source/engine/rendering), the player view drawn twice when the depth effects need its depth first, the cars drawn into the rear
 * environment map face, which also stands in for the mirror, and the post processing done after the views
 * (nfs::x360::RenderWorld, which ___eDisplayFrame now is).
 */
#include "game/gen/api.h"
#include "game/platform/Device.h"
#include "game/platform/Effect.h"
#include "game/platform/Platform.h"
#include "engine/rendering/shadows/shadow.h"
#include "core/config.h"
#include "legacy/link.h"
#include "core/log.h"
#include "dev/profile.h"
#include "engine/rendering/raindrops.h"
#include "dev/proving.h"
#include "dev/testtrack.h"
#include "gameplay/world/timeofday.h"
#include "engine/rendering/vulkan/renderer.h"
#include "gameplay/world/weather.h"
#include "engine/rendering/x360/frame.h"
#include "engine/rendering/x360/render.h"

NFS_REPLACE(0x6de300, ___eDisplayFrame)

using device::BeginEffect;
using device::EndEffect;

extern "C" int INT_009017d0;
extern "C" int dword_982BA0;
extern "C" int dword_982B34;
extern "C" int dword_982B9C;
extern "C" int dword_982BA4;
extern "C" float flt_982BA8;
extern "C" float flt_8F9340;
extern "C" float flt_8F9344;
extern "C" int lpData_00901830;
extern "C" int lpData_009017d4;
extern "C" int dword_8FAE6C;
/** The sky layer's colour and fog state, saved around the extra sky passes. */
extern "C" int dword_8FADE0;
extern "C" int dword_8FADF0;
extern "C" int dword_8FADF4;
extern "C" int dword_8FADF8;
extern "C" int dword_8FADFC;
extern "C" int dword_8F92E0;
extern "C" int dword_8F92E4;
extern "C" int dword_9017E4;
extern "C" int EnableEnvMap__009017B8;
extern "C" int lpData_009017bc;
extern "C" int dword_9017C4;
extern "C" int lpData_009017dc;
extern "C" int dword_8F9218;
extern "C" int g_X360EffectsEnable;
extern "C" int lpData_009017ec;
extern "C" int lpData_009017fc;
extern "C" int dword_901800;
extern "C" int dword_901804;
extern "C" int dword_90180C;
extern "C" int lpData_00901810;
extern "C" int lpData_00901828;
extern "C" float flt_8FAE68;
extern "C" float flt_8FAE50;
extern "C" float flt_8FAE64;
extern "C" int dword_982C08;
extern "C" int TheGameFlowManager_CurrentGameFlowState;
extern "C" void *TimeOfDayPointer;
extern "C" TextureInfo *dword_982B48;
extern "C" TextureInfo *dword_982B4C;
extern "C" int dword_93DE38[8];
extern "C" int dword_9828A0[8];
extern "C" int dword_982B90;
extern "C" int dword_982B94;
extern "C" int dword_982B98;
extern "C" eRenderTarget dword_93DB0C;
/** The player view's scenery effects (sub_753d90). */
extern "C" char dword_9AE364[];
extern "C" void *pVisualTreatmentPlat;
extern "C" int dword_9885B8;
extern "C" unsigned char dword_982B8C;
extern "C" unsigned char dword_982C38;
extern "C" unsigned char dword_982B26;
extern "C" unsigned char dword_982B27;
extern "C" unsigned char dword_982B29;
extern "C" unsigned char dword_982B2A;
extern "C" unsigned char byte_9B0A74;
extern "C" unsigned char dword_982B30;
extern "C" float dword_982B2C;
extern "C" float flt_8F94C0[3];
extern "C" float dword_8F94EC;
extern "C" float flt_8F94E4;
extern "C" float flt_8F94E8;
extern "C" unsigned char byte_8F9B5C;
extern "C" unsigned char byte_982BAD;

extern "C" void __cdecl sub_6E2F50();
extern "C" void __cdecl sub_6C83D0();
extern "C" void __cdecl sub_6BFDD0(void *cull);
extern "C" void __cdecl sub_6BD3B0(eRenderTarget *target, int clear);
extern "C" void __cdecl StuffSkyLayer(eView *view, int layer, float value);
extern "C" void __cdecl sub_750B10(eView *view, int);
extern "C" void __cdecl sub_6D4960();
extern "C" void __cdecl nullsub_47(eView *view, int);
extern "C" void __cdecl nullsub_46(eView *view, int);
extern "C" void __cdecl nullsub_28();
extern "C" void __cdecl sub_6C00B0();
extern "C" void __cdecl sub_6CFF40(void *texture, int, int, int);
extern "C" void __cdecl sub_6E54E0(eView *view, void *cull);
extern "C" void __cdecl sub_6DE210(eView *view, int, int);
extern "C" void __cdecl sub_7268C0(eView *view);
extern "C" void __cdecl sub_6DAE20();
extern "C" void __cdecl sub_6D4100(void *target, void *texture, D3DCOLOR colour, int blend, int source,
                                   int destination, bool keepStates);
extern "C" void __cdecl sub_6DBEC0();
extern "C" void __cdecl sub_6DBFE0();
extern "C" void __cdecl sub_6D4620(bool);
extern "C" void __cdecl sub_6D49A0(eView *view, bool);
extern "C" void __cdecl sub_6D3850(float, bool);
extern "C" void __cdecl sub_6DC040(bool);
void RenderPolyTransformed(ePoly *poly, TextureInfo *texture, bMatrix4 *transform, int unused);
void __fastcall VisualTreatmentRender(char *plat, void *, bool toBackBuffer);

extern "C" void *dword_982A34[6];

namespace
{
/** Draw what Vulkan mode can: without the post effects not ported yet, the sun flares and the visual treatment. */
bool Reduced()
{
    static const bool reduced = nfs::vk::HasArgument("-nopost");
    return nfs::vk::g_renderer || reduced;
}

const int EVIEW_PLAYER1 = 1;
const int EVIEW_RVM = 3;
const int EVIEW_SHADOW = 4;
const int EVIEW_8 = 8;
const int EVIEW_ENV_FIRST = 18;
const int EVIEW_ENV_LAST = 23;
/** The environment map face behind the car: the mirror shows it, and it is the one face cars are drawn into. */
const int kRearEnvFace = 20;
const int kGameFlowFrontEnd = 3;
const int kGameFlowRacing = 6;
const int kParamColorWriteMode = 0x18c;
const int kParamFEColorWriteFlag = 0x484;

/** GrandSceneryCullInfo: up to 12 views of 0xd0 bytes and their count. */
struct alignas(16) CullInfo
{
    char views[12][0xd0];
    int count;
};

/** eView's three words after RenderTarget: an object with the camera's light flash quad, the camera mover (lighting
 * level, rain) and the visual look state. */
struct ViewExtra
{
    char *Flash;
    char *Mover;
    void *Look;
};

ViewExtra &Extra(eView &view)
{
    return *reinterpret_cast<ViewExtra *>(view.unknown_64);
}

float Float(const char *object, int offset)
{
    return *reinterpret_cast<const float *>(object + offset);
}

int Int(const char *object, int offset)
{
    return *reinterpret_cast<const int *>(object + offset);
}

void Flush()
{
    sub_6E2F50();
}

void StuffScenery(CullInfo &cull, eView *view, int flags)
{
    reinterpret_cast<GrandSceneryCullInfo *>(&cull)->StuffScenery(view, flags);
}

void SetTimeOfDayLighting(eView &view)
{
    NFS_ORIGINAL(0x769580, void(__thiscall *)(void *, int))(TimeOfDayPointer, Int(Extra(view).Mover, 0x290));
    nfs::timeofday::ApplyLighting(static_cast<char *>(TimeOfDayPointer));
}

/** The sky layer state that the extra sky passes change and give back. */
struct SkyState
{
    int e0, f0, f4, f8, fc;

    void Save()
    {
        e0 = dword_8FADE0;
        f0 = dword_8FADF0;
        f4 = dword_8FADF4;
        f8 = dword_8FADF8;
        fc = dword_8FADFC;
    }

    void Restore() const
    {
        dword_8FADF0 = f0;
        dword_8FADF4 = f4;
        dword_8FADF8 = f8;
        dword_8FADFC = fc;
        dword_8FADE0 = e0;
    }
};

float AsFloat(int bits)
{
    return *reinterpret_cast<float *>(&bits);
}

int AsBits(float value)
{
    return *reinterpret_cast<int *>(&value);
}

/** A sky pass for a reflection view, at the time of day's sky level (half with the fixed-function sky). */
void StuffReflectionSky(eView *view, int layer)
{
    const float level = Float(static_cast<const char *>(TimeOfDayPointer), 0x64);
    SkyState saved;
    saved.Save();
    dword_8FADE0 = AsBits(level);
    StuffSkyLayer(view, layer, g_Effects[EFFECT_SKY]->HasShaders ? level : 0.5f);
    Flush();
    saved.Restore();
}

extern "C" void *dword_982CEC;
extern "C" void *dword_982A6C;

/** -debugviews: saves the shadow map's colour target once (shadow_color.bmp), to compare the renderers. */
void DumpShadowColor()
{
    static int frames;
    if (!nfs::vk::HasArgument("-debugviews") || ++frames != 4000)
        return;
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    nfs::vk::Texture *t = static_cast<nfs::vk::Texture *>(dword_982CEC);
    uint32_t *pixels = static_cast<uint32_t *>(::operator new(t->width * t->height * 4));
    if (r->ReadPixels(t, pixels))
    {
        HANDLE file = CreateFileA("shadow_color_vk.raw", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        DWORD written;
        WriteFile(file, pixels, t->width * t->height * 4, &written, nullptr);
        CloseHandle(file);
    }
    ::operator delete(pixels);
}

/** The bmp the next road reflection is saved as (-reflectdebug, nfs::x360::RequestReflectionDump). */
char g_reflectionDump[64];

void DumpReflection()
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    if (!g_reflectionDump[0] || !r)
        return;
    nfs::vk::Texture *t = static_cast<nfs::vk::Texture *>(dword_982A6C);
    const DWORD size = t->width * t->height * 4;
    unsigned char *pixels = static_cast<unsigned char *>(::operator new(size));
    if (r->ReadPixels(t, pixels))
    {
        for (DWORD i = 3; i < size; i += 4)
            pixels[i] = 255;
        BITMAPFILEHEADER file = {};
        BITMAPINFOHEADER info = {};
        file.bfType = 0x4d42;
        file.bfOffBits = sizeof(file) + sizeof(info);
        file.bfSize = file.bfOffBits + size;
        info.biSize = sizeof(info);
        info.biWidth = static_cast<LONG>(t->width);
        info.biHeight = -static_cast<LONG>(t->height);
        info.biPlanes = 1;
        info.biBitCount = 32;
        HANDLE out = CreateFileA(g_reflectionDump, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        DWORD written;
        WriteFile(out, &file, sizeof(file), &written, nullptr);
        WriteFile(out, &info, sizeof(info), &written, nullptr);
        WriteFile(out, pixels, size, &written, nullptr);
        CloseHandle(out);
    }
    ::operator delete(pixels);
    g_reflectionDump[0] = 0;
}

void RenderShadowView(CullInfo &cull)
{
    eView &view = eViews[EVIEW_SHADOW];
    sub_6BD3B0(view.RenderTarget, 1);
    SetTimeOfDayLighting(view);
    SkyState saved;
    saved.Save();
    dword_8FADE0 = dword_8F92E0;
    if (nfs::timeofday::NightSky() < 1.0f)
        StuffSkyLayer(&view, 4, AsFloat(dword_8F92E0));
    nfs::x360::RoadReflectionPreRender();
    Flush();
    saved.Restore();
    Flush();
    if (nfs::timeofday::DrawNightSky(&view, nfs::timeofday::RoadReflectionSkyGain(), true))
        Flush();
    const float lighting = Float(Extra(eViews[EVIEW_PLAYER1]).Mover, 0x36a0);
    static const bool noShadowDraw = nfs::vk::HasArgument("-noshadowdraw");
    if (lpData_009017d4 > 1 && lighting > 0.01f && !noShadowDraw)
    {
        StuffScenery(cull, &view, 8);
        BeginEffect(g_Effects[EFFECT_WORLD_PRELIT]);
        sub_750B10(&view, 1);
        EndEffect(g_Effects[EFFECT_WORLD_PRELIT]);
    }
    Flush();
    BeginEffect(g_Effects[EFFECT_WORLD_PRELIT]);
    if (dword_9017E4)
    {
        eRenderWorldLightFlares(&view, static_cast<flareType>(2));
        RenderVehicleFlares(&view, 1, 0);
        nullsub_47(&view, 1);
        eRenderLightFlarePool(&view);
    }
    EndEffect(g_Effects[EFFECT_WORLD_PRELIT]);
    Flush();
    DumpShadowColor();
    DumpReflection();
}

/** -dumpenv with Direct3D 9: saves the reflection cube's faces as d3d9env_N.dds next to the exe, once (-vkdumpenv is
 * the Vulkan counterpart). */
void DumpEnvFaces()
{
    static const bool dump = nfs::vk::HasArgument("-dumpenv");
    static int frames;
    if (!dump || nfs::vk::g_renderer || ++frames != 1500)
        return;
    return;
}

/** The environment map faces drawn this frame (a bit per face from the first): all of them after the targets were made
 * or when the maps were not drawn last frame; else, while the mirror shows the rear face, that one and the next of the
 * others in turn, otherwise the next faces in turn: [Display] EnvMapFaces faces in all (6: every face, every frame). */
unsigned int EnvironmentFacesThisFrame()
{
    static unsigned int s_lastFrame = ~0u - 1;
    static int s_next;
    static const int faces = [] {
        const int n = nfs::config::Int("Display", "EnvMapFaces", 3);
        return n < 1 ? 1 : n > 6 ? 6 : n;
    }();
    const bool all = nfs::x360::g_envMapsStale || eFrameCounter != s_lastFrame + 1u || faces == 6;
    s_lastFrame = eFrameCounter;
    nfs::x360::g_envMapsStale = false;
    if (all)
        return 0x3Fu;
    const bool mirror = eViews[EVIEW_RVM].Active != 0;
    const int rear = mirror ? kRearEnvFace - EVIEW_ENV_FIRST : -1;
    unsigned int mask = mirror ? 1u << rear : 0u;
    for (int added = mirror ? 1 : 0; added < faces;)
    {
        s_next = (s_next + 1) % 6;
        if (s_next == rear)
            continue;
        mask |= 1u << s_next;
        ++added;
    }
    return mask;
}

void RenderEnvironmentMaps(CullInfo &cull)
{
    g_RenderTargetsReset = true;
    dword_8FAE6C = 3;
    if (TheGameFlowManager_CurrentGameFlowState != kGameFlowRacing)
        return;
    const unsigned int faces = EnvironmentFacesThisFrame();
    for (int v = EVIEW_ENV_FIRST; v <= EVIEW_ENV_LAST; ++v)
    {
        eView &view = eViews[v];
        if (!(faces & (1u << (v - EVIEW_ENV_FIRST))) || !view.Active || !view.RenderTarget->unknown_10)
            continue;
        sub_6BD3B0(view.RenderTarget, 1);
        SetTimeOfDayLighting(view);
        device::SetRenderState(D3DRS_ZENABLE, FALSE);
        device::SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        nfs::x360::EnvMapPreRender(v);
        if (TheGameFlowManager_CurrentGameFlowState == kGameFlowRacing && nfs::timeofday::NightSky() < 1.0f)
            StuffReflectionSky(&view, 3);
        device::SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        device::SetRenderState(D3DRS_ZENABLE, TRUE);
        Flush();
        if (TheGameFlowManager_CurrentGameFlowState == kGameFlowRacing && nfs::timeofday::DrawNightSky(&view))
            Flush();
        if (lpData_009017bc)
            StuffScenery(cull, &view, 0);
        else if (lpData_00901830 < 2)
            StuffScenery(cull, &view, 0x1000);
        nfs::x360::DrawCarsInCubemaps();
        eEffect *prelit = g_Effects[EFFECT_WORLD_PRELIT];
        BeginEffect(prelit);
        if (dword_9017E4)
        {
            eRenderWorldLightFlares(&view, static_cast<flareType>(1));
            eRenderLightFlarePool(&view);
            RenderVehicleFlares(&view, 0, 1);
        }
        // X360 Stuff leaves the prelit effect current after each face.
        prelit->End();
        prelit->Effect->End();
    }
}

void RenderMirror(CullInfo &cull)
{
    eView &view = eViews[EVIEW_RVM];
    sub_6BD3B0(&dword_93DB0C, 1);
    StuffReflectionSky(&view, 3);
    Flush();
    StuffScenery(cull, &view, 1);
    sub_750B10(&view, 0);
    BeginEffect(g_Effects[EFFECT_WORLD_PRELIT]);
    if (dword_9017E4)
    {
        RenderVehicleFlares(&view, 0, 0);
        if (dword_9017E4)
            eRenderWorldLightFlares(&view, static_cast<flareType>(0));
    }
    EndEffect(g_Effects[EFFECT_WORLD_PRELIT]);
    eEffect *fe = g_Effects[EFFECT_FE];
    BeginEffect(fe);
    if (dword_982B48)
    {
        fe->Effect->SetInt(fe->Parameter(kParamFEColorWriteFlag), 15);
        sub_6CFF40(dword_982B48->PlatInfo->Texture, -1, 5, 6);
    }
    fe->Effect->SetInt(fe->Parameter(kParamFEColorWriteFlag), 8);
    if (dword_982B4C)
        sub_6CFF40(dword_982B4C->PlatInfo->Texture, -1, 2, 1);
    EndEffect(fe);
    fe->Effect->SetInt(fe->Parameter(kParamFEColorWriteFlag), 15);
    eResetRenderStates();
}

void BindPlayerTarget()
{
    eRenderTarget *target = eViews[EVIEW_PLAYER1].RenderTarget;
    Flush();
    device::SetTarget(target->Surface, target->DepthSurface, target->Width, target->Height);
    g_CurrentRenderTarget = target;
    g_CurrentEffect = nullptr;
}

/** The camera's light flash (a world-space quad the view's flash object carries), drawn white. */
void RenderLightFlash(const char *flash)
{
    ePoly poly;
    for (int i = 0; i < 4; ++i)
        for (int c = 0; c < 4; ++c)
            poly.Colours[i][c] = 0xFF;
    static const int kCorners[4] = {0x14c, 0x158, 0x170, 0x164};
    for (int i = 0; i < 4; ++i)
    {
        poly.Vertices[i][0] = Float(flash, kCorners[i]);
        poly.Vertices[i][1] = Float(flash, kCorners[i] + 4);
        poly.Vertices[i][2] = Float(flash, kCorners[i] + 8);
    }
    static const float kUVs[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    for (int i = 0; i < 4; ++i)
    {
        poly.UVs[i][0] = kUVs[i][0];
        poly.UVs[i][1] = kUVs[i][1];
    }
    poly.Flags = 0;
    BeginEffect(g_Effects[EFFECT_WORLD_MIN]);
    RenderPolyTransformed(&poly, nullptr, &eMathIdentityMatrix, 0);
    EndEffect(g_Effects[EFFECT_WORLD_MIN]);
}

void RenderPlayerWorld(CullInfo &cull);
void RenderPlayerOverlays();

/** The player's view up to the front end cars: drawn once more before the colour pass when the depth effects need its
 * depth (X360 Stuff's depth pass: the view loops back to its shadow map draws). */
void RenderPlayerScene(CullInfo &cull)
{
    eView &view = eViews[EVIEW_PLAYER1];
    for (;;)
    {
        nfs::x360::DoShadowMapLod();
        eRenderTarget *target = view.RenderTarget;
        device::SetTarget(target->Surface, target->DepthSurface, target->Width, target->Height);
        g_CurrentRenderTarget = target;
        g_CurrentEffect = nullptr;
        if (dword_9017C4)
            NFS_ORIGINAL(0x724230, void(__thiscall *)(CullInfo *, eView *, unsigned int))(&cull, &eViews[EVIEW_8],
                                                                                           0x4FAEE0);
        static const bool noShadowMesh = nfs::vk::HasArgument("-noshadowmesh");
        if (lpData_00901830 > 0 && !(flt_8FAE68 < flt_8FAE50) && !noShadowMesh)
        {
            nfs::profile::Scope profile(nfs::profile::kShadowMap);
            sub_6E54E0(&view, &cull);
        }
        BindPlayerTarget();
        eResetRenderStates();
        nfs::x360::PreRender();
        {
            nfs::profile::Scope profile(nfs::profile::kWorld);
            RenderPlayerWorld(cull);
        }
        if (TheGameFlowManager_CurrentGameFlowState == kGameFlowFrontEnd)
            Flush();
        if (nfs::x360::g_depthPassDone || !nfs::x360::DepthPassActive())
            return;
        nfs::x360::g_depthPassDone = true;
    }
}

void RenderPlayerView(CullInfo &cull)
{
    eView &view = eViews[EVIEW_PLAYER1];
    SetTimeOfDayLighting(view);
    NFS_ORIGINAL(0x753d90, void(__thiscall *)(void *, eView *, int))(dword_9AE364, &view, 0);
    RenderPlayerScene(cull);
    nfs::profile::Scope profile(nfs::profile::kOverlays);
    RenderPlayerOverlays();
}

/** The player view's sky, scenery, world models and front end cars. */
void RenderPlayerWorld(CullInfo &cull)
{
    eView &view = eViews[EVIEW_PLAYER1];
    eEffect *sky = g_Effects[EFFECT_SKY];
    sky->Effect->SetInt(sky->Parameter(kParamColorWriteMode), 7);
    sub_6DE210(&view, dword_8F92E4, 0);
    StuffScenery(cull, &view, 0x20);
    Flush();
    BeginEffect(g_Effects[EFFECT_WORLD]);
    RenderWorldModels(&view, 0);
    EndEffect(g_Effects[EFFECT_WORLD]);
    Flush();
    BeginEffect(g_Effects[EFFECT_WORLD_PRELIT]);
    nfs::testtrack::Render();
    nfs::proving::Render();
    sub_7268C0(&view);
    EndEffect(g_Effects[EFFECT_WORLD_PRELIT]);
    BeginEffect(g_Effects[EFFECT_WORLD_PRELIT]);
    sub_750B10(&view, 0);
    EndEffect(g_Effects[EFFECT_WORLD_PRELIT]);
    Flush();
    BeginEffect(g_Effects[EFFECT_WORLD_PRELIT]);
    nullsub_28();
    EndEffect(g_Effects[EFFECT_WORLD_PRELIT]);
    Flush();
    if (TheGameFlowManager_CurrentGameFlowState == kGameFlowFrontEnd)
    {
        BeginEffect(g_Effects[EFFECT_WORLD_PRELIT]);
        RenderFrontEndCars(&view, 1);
        EndEffect(g_Effects[EFFECT_WORLD_PRELIT]);
        Flush();
        if (GarageMainScreen *garage = GarageMainScreen::GetInstance())
            NFS_ORIGINAL(0x7a2200, void(__thiscall *)(GarageMainScreen *, int))(garage, 1);
        Flush();
        BeginEffect(g_Effects[EFFECT_WORLD_PRELIT]);
        RenderFrontEndCars(&view, 0);
        EndEffect(g_Effects[EFFECT_WORLD_PRELIT]);
    }
}

/** After the player view's scene: arrows, flares, particles, rain and the sun. X360 Stuff drops the light flash, the
 * speed blur and the sky's second pass here. */
void RenderPlayerOverlays()
{
    eView &view = eViews[EVIEW_PLAYER1];
    eEffect *sky = g_Effects[EFFECT_SKY];
    eResetRenderStates();
    RenderGpsArrows(&view);
    BeginEffect(g_Effects[EFFECT_WORLD_PRELIT]);
    nullsub_28();
    EndEffect(g_Effects[EFFECT_WORLD_PRELIT]);
    Flush();
    BeginEffect(g_Effects[EFFECT_WORLD_NO_FOG]);
    EndEffect(g_Effects[EFFECT_WORLD_NO_FOG]);
    if (dword_9017E4)
    {
        if (g_X360EffectsEnable)
        {
            device::SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
            sub_6DAE20();
            device::SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        }
        else
        {
            BeginEffect(g_Effects[EFFECT_WORLD_PRELIT]);
            device::SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
            eRenderWorldLightFlares(&view, static_cast<flareType>(0));
            RenderVehicleFlares(&view, 0, 0);
            nullsub_47(&view, 0);
            eRenderLightFlarePool(&view);
            nullsub_46(&view, 0);
            device::SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
            EndEffect(g_Effects[EFFECT_WORLD_PRELIT]);
        }
    }
    if (INT_009017d0)
    {
        BeginEffect(g_Effects[EFFECT_WORLD]);
        RenderAnimSceneEffects(&view, 0);
        EndEffect(g_Effects[EFFECT_WORLD]);
    }
    (void)sky;
    if (lpData_009017ec)
    {
        BeginEffect(g_Effects[EFFECT_PARTICLES]);
        gEmitterSystem.Render(&view);
        EndEffect(g_Effects[EFFECT_PARTICLES]);
    }
    {
        BeginEffect(g_Effects[EFFECT_PARTICLES]);
        device::SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        nfs::weather::BeforeRain(Extra(view).Mover);
        reinterpret_cast<Rain *>(Extra(view).Mover)->UpdateAndRender();
        nfs::weather::ApplyToRain(Extra(view).Mover);
        device::SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        EndEffect(g_Effects[EFFECT_PARTICLES]);
    }
    nfs::x360::DrawRainDrops(&view);
    if (nfs::timeofday::SunSprite() > 0.0f && nfs::x360::SunVisible(&view))
        nfs::x360::RenderSun(&view);
    if (pVisualTreatmentPlat)
        static_cast<IVisualTreatment *>(pVisualTreatmentPlat)->Update(&view);
}

/** The end of the views: the frame counter and the destination alpha fill (X360 Stuff skips the original post effects
 * that follow). */
void FinishViews()
{
    ++dword_9885B8;
    dword_982B8C = 0;
    if (dword_982C38)
    {
        device::SetRenderState(D3DRS_COLORWRITEENABLE, 7);
        sub_6D4100(g_BackBufferSurface, nullptr, 0xFFFFFFFF, 1, D3DBLEND_DESTALPHA, D3DBLEND_ZERO, false);
    }
}
} // namespace

namespace nfs::x360
{
void RequestReflectionDump(const char *name)
{
    lstrcpynA(g_reflectionDump, name, sizeof(g_reflectionDump));
}
} // namespace nfs::x360

extern "C" void ___eDisplayFrame()
{
    nfs::x360::RenderWorld();
}

void RenderViews()
{
    static const bool debugViews = nfs::vk::HasArgument("-debugviews");
    static int frame;
    ++frame;
    if (debugViews && frame % 300 == 0 && frame <= 3000)
        nfs::Log("frame %d tick %d tunnel %d intunnel %d elapsed %d", frame, GetTickCount(), (int)(flt_982BA8 * 1000),
                 AmIinATunnel(&eViews[EVIEW_PLAYER1], 1), (int)(WorldTimeElapsed * 100000));
    if (debugViews && frame == 300)
    {
        nfs::Log("shadow strength %d %d tod %d mover %d tunnel %d", (int)(flt_8FAE68 * 1000), (int)(flt_8FAE64 * 1000),
                 (int)(Float(static_cast<const char *>(TimeOfDayPointer), 0x188) * 1000),
                 (int)(Float(Extra(eViews[EVIEW_PLAYER1]).Mover, 0x290) * 1000), (int)(flt_982BA8 * 1000));
        for (int t = 0; t < NUM_EFFECTS; ++t)
        {
            const eEffect *e = g_Effects[t];
            nfs::Log("effect %d levels %d %d %d %d shaders %d %d %d %d max %d level %d tech %d hasshaders %d method %d", t,
                     e->LevelTechniques[0], e->LevelTechniques[1], e->LevelTechniques[2], e->LevelTechniques[3],
                     e->LevelShaders[0], e->LevelShaders[1], e->LevelShaders[2], e->LevelShaders[3], e->MaxLevel,
                     e->Level, e->TechniqueIndex, e->HasShaders, dword_982C08);
        }
        nfs::Log("post: overbright %d x360 %d blur %d treatment %d %p %d anim %d raindrops %d particles %d vt %d colorwrite %d "
                 "dof %d speedblur %d %d", lpData_009017fc, g_X360EffectsEnable, dword_901800, lpData_00901828,
                 pVisualTreatmentPlat, byte_8F9B5C, INT_009017d0, lpData_00901810, lpData_009017ec, dword_982B30,
                 dword_982C38, eDepthOfFieldActive, lpData_009017dc, dword_8F9218);
        static const int kViews[3] = {1, 4, 15};
        for (int v : kViews)
        {
            eView &view = eViews[v];
            nfs::Log("view %d id %d active %d target %p", v, view.ID, view.Active, view.RenderTarget);
            const float *m = reinterpret_cast<const float *>(view.PlatInfo);
            for (int k = 0; k < 3; ++k, m += 16)
                nfs::Log("  m%d %d %d %d %d | %d %d %d %d | %d %d %d %d | %d %d %d %d", k, (int)(m[0] * 1000),
                         (int)(m[1] * 1000), (int)(m[2] * 1000), (int)(m[3] * 1000), (int)(m[4] * 1000),
                         (int)(m[5] * 1000), (int)(m[6] * 1000), (int)(m[7] * 1000), (int)(m[8] * 1000),
                         (int)(m[9] * 1000), (int)(m[10] * 1000), (int)(m[11] * 1000), (int)(m[12] * 1000),
                         (int)(m[13] * 1000), (int)(m[14] * 1000), (int)(m[15] * 1000));
        }
    }
    if (!INT_009017d0)
        dword_982BA0 = 1;
    dword_982B9C = dword_982B34 ? 0x800 : 0;
    dword_982BA4 = AmIinATunnel(&eViews[EVIEW_PLAYER1], 1);
    if (dword_982BA4)
    {
        flt_982BA8 = flt_8F9340 * WorldTimeElapsed + flt_982BA8;
        if (flt_982BA8 > 1.0f)
            flt_982BA8 = 1.0f;
    }
    else
    {
        flt_982BA8 = flt_982BA8 - flt_8F9344 * WorldTimeElapsed;
        if (flt_982BA8 < 0.0f)
            flt_982BA8 = 0.0f;
    }
    RenderCharacterShadows = lpData_00901830 == 0;
    nfs::weather::Update();
    nfs::x360::UpdateLightScattering();
    g_CurrentEffect = nullptr;
    nfs::x360::PreDisplay();
    NFS_ORIGINAL(0x769360, void(__thiscall *)(void *))(TimeOfDayPointer);
    nfs::timeofday::ApplySun(static_cast<char *>(TimeOfDayPointer));
    {
        nfs::profile::Scope profile(nfs::profile::kShadowMap);
        nfs::shadow::RenderSunShadows();
    }
    nfs::x360::DoLowLod();
    CullInfo cull;
    cull.count = 0;
    {
        nfs::profile::Scope profile(nfs::profile::kCull);
        sub_6BFDD0(&cull);
    }

    if (lpData_009017d4)
    {
        dword_8FAE6C = 3;
        eView &shadow = eViews[EVIEW_SHADOW];
        if (!shadow.Active || !shadow.RenderTarget->unknown_10)
            return;
        nfs::profile::Scope profile(nfs::profile::kRoadReflection);
        RenderShadowView(cull);
    }
    if (EnableEnvMap__009017B8)
    {
        nfs::profile::Scope profile(nfs::profile::kEnvMaps);
        RenderEnvironmentMaps(cull);
    }
    else if (g_RenderTargetsReset)
    {
        for (int v = EVIEW_ENV_FIRST; v <= EVIEW_ENV_LAST; ++v)
            sub_6BD3B0(eViews[v].RenderTarget, 2);
        g_RenderTargetsReset = false;
    }
    Flush();
    dword_8FAE6C = 2;
    device::SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    device::SetRenderState(D3DRS_ZENABLE, TRUE);
    // The mirror shows the rear environment map face instead (nfs::x360::DrawRvm).
    if (eViews[EVIEW_RVM].Active && !nfs::x360::options::kCubemapRVM)
    {
        nfs::profile::Scope profile(nfs::profile::kMirror);
        RenderMirror(cull);
    }
    Flush();

    for (int i = 0; i < 8; ++i)
    {
        dword_93DE38[i] = 0;
        dword_9828A0[i] = 0;
    }
    dword_982B90 = dword_982B94 = dword_982B98 = 0;
    eView &player = eViews[EVIEW_PLAYER1];
    if (player.Active && player.RenderTarget->unknown_10)
        RenderPlayerView(cull);
    DumpEnvFaces();
    FinishViews();
}
