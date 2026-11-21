/**
 * The end of each frame (original: zPlatform eDisplayFrame 0x6e7220, eResetRenderStates 0x6cfce0). With the
 * Direct3D 9 renderer these run the original code. In Vulkan mode (docs/VULKAN.md) eDisplayFrame does the original's
 * work with the renderer's frame in place of BeginScene, EndScene and Present. The views (___eDisplayFrame) are in
 * eDisplayViews.cpp.
 */
#include "game/gen/api.h"
#include "game/platform/Effect.h"
#include "game/platform/Platform.h"
#include "engine/rendering/display/display.h"
#include "legacy/link.h"
#include "core/log.h"
#include "dev/fescript.h"
#include "dev/profile.h"
#include "dev/test_options.h"
#include "engine/rendering/vulkan/renderer.h"

NFS_REPLACE(0x6e7220, eDisplayFrame)
NFS_REPLACE(0x6cfce0, eResetRenderStates)

extern "C" unsigned char byte_901AC8;
extern "C" int dword_982B14;
extern "C" void *pVisualTreatmentPlat;
extern "C" int SaveStreamAnim;
extern "C" int dword_982BB8;
extern "C" int dword_982BBC;
/** Texture detail setting, chooses how many top mip levels are dropped (dword_982AE0). */
extern "C" int lpData_009017f4;
extern "C" int dword_982B34;
extern "C" int dword_9A3A34;
extern "C" int dword_982AE0;
extern "C" int EnableEnvMap__009017B8;
extern "C" float dword_901AC4;
extern "C" int dword_9017F0;
extern "C" unsigned char byte_982B25;
extern "C" int dword_8FAE6C;
extern "C" int dword_982BF8;
extern "C" int dword_9885C4;
extern "C" int dword_982CA0;
extern "C" int dword_982CA4;
extern "C" char _unk_globalmouseshit_00987f50[];
extern "C" int __resolution_index;
extern "C" int lpData_00901818;
extern "C" int GPUVendor;
extern "C" int __multisamplequality;
extern "C" int dword_8F921C;

extern "C" void __cdecl sub_6BF290();
extern "C" void __cdecl sub_6C5DA0();
extern "C" void __cdecl sub_6D7500();
extern "C" int __cdecl sub_6DB0D0();
extern "C" void __cdecl MaybeChangeViewMode();
extern "C" void __cdecl sub_6D0B00(eView *view, int);
extern "C" void __cdecl sub_6C3870();
extern "C" void __cdecl sub_6C6F00();
extern "C" void __cdecl sub_6C05E0(int);
extern "C" void __cdecl nullsub_36();
extern "C" void __cdecl nullsub_35();
extern "C" void __cdecl sub_6C6880();
extern "C" void ___eDisplayFrame();

namespace
{
const unsigned int kVendorMatrox = 0x102B;

/** Parameter table offsets of the world effect's depth-of-field parameters. */
const int kParamDofOffset = 0x5c4;
const int kParamDofScale = 0x2f4;
const int kParamDofNear = 0x31c;
const int kParamDofFar = 0x344;
const int kParamColorWriteMode = 0x18c;

void SetupWorldLightContext(eView *view)
{
    NFS_ORIGINAL(0x5029f0, void(__thiscall *)(eView *))(view);
}

/** Depth of field from the player camera's focus (the world effect's DOF parameters); off when neither is set. */
void SetupDepthOfField()
{
    if (!dword_9017F0)
    {
        eDepthOfFieldActive = false;
        byte_982B25 = 0;
        return;
    }
    const CameraParams &key = eViews[1].pCamera->CurrentKey;
    float focus = key.FocalDistance, depth = key.DepthOfField;
    if (focus == 0.0f && depth == 0.0f)
    {
        eDepthOfFieldActive = false;
        byte_982B25 = 0;
        return;
    }
    if (focus < 0.0f)
        focus = 0.0f;
    if (depth < 0.0f)
        depth = 8.0f;
    eDepthOfFieldActive = true;
    eEffect *world = g_Effects[EFFECT_WORLD];
    world->Effect->SetFloat(world->Parameter(kParamDofOffset), 1.0f / 1024.0f);
    float halfDepth = depth * (1.0f / 2048.0f);
    float scaledFocus = focus * (1.0f / 1024.0f);
    float scale = static_cast<float>(1.0 / (depth * 0.01953125));
    world->Effect->SetFloat(world->Parameter(kParamDofScale), scale);
    world->Effect->SetFloat(world->Parameter(kParamDofNear), -((scaledFocus + halfDepth) * scale));
    world->Effect->SetFloat(world->Parameter(kParamDofFar), (scaledFocus - halfDepth) * scale);
    byte_982B25 = 1;
}
} // namespace

/** The default render state at the start of each frame and pass: depth test and write on, the texture filtering the
 * Video options ask for on every stage, no fixed-function lighting or fog. */
void eResetRenderStates()
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    r->SetRenderState(D3DRS_ZENABLE, TRUE);
    r->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    r->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    r->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
    r->SetRenderState(D3DRS_SPECULARENABLE, TRUE);
    r->SetRenderState(D3DRS_LIGHTING, FALSE);
    r->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dword_8FAE6C = 2;
    r->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    for (DWORD stage = 0; stage < g_DeviceCaps.MaxTextureBlendStages; ++stage)
    {
        DWORD anisotropic = GPUVendor != kVendorMatrox ? D3DTEXF_ANISOTROPIC : D3DTEXF_LINEAR;
        bool aniso = lpData_00901818 == 2;
        r->SetSamplerState(stage, D3DSAMP_MINFILTER,
                           aniso && (g_DeviceCaps.TextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC) ? anisotropic
                                                                                                     : D3DTEXF_LINEAR);
        r->SetSamplerState(stage, D3DSAMP_MAGFILTER,
                           aniso && (g_DeviceCaps.TextureFilterCaps & D3DPTFILTERCAPS_MAGFANISOTROPIC) ? anisotropic
                                                                                                     : D3DTEXF_LINEAR);
        r->SetSamplerState(stage, D3DSAMP_MIPFILTER, lpData_00901818 ? D3DTEXF_LINEAR : D3DTEXF_POINT);
        const DWORD wanted = static_cast<DWORD>(nfs::display::Anisotropy());
        r->SetSamplerState(stage, D3DSAMP_MAXANISOTROPY,
                           aniso ? (wanted < g_DeviceCaps.MaxAnisotropy ? wanted : g_DeviceCaps.MaxAnisotropy) : 1);
    }
    r->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, dword_8F921C && __multisamplequality > 0);
    // The texture stage states it resets are fixed-function only.
    eEffect *world = g_Effects[EFFECT_WORLD];
    world->Effect->SetInt(world->Parameter(kParamColorWriteMode), 7);
    sub_6C6880();
}

void eDisplayFrame()
{
    nfs::profile::TimerScope profile(nfs::profile::kTimerDisplay);
    nfs::profile::DisplayCycleScope cycles;
    nfs::test::CycleVideoOptions();
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    g_CurrentEffect = nullptr;
    if (byte_901AC8)
    {
        sub_6BF290();
        byte_901AC8 = 0;
    }
    if (dword_982B14)
    {
        sub_6C5DA0();
        sub_6D7500();
        if (pVisualTreatmentPlat)
            NFS_ORIGINAL(0x6d4dc0, void(__thiscall *)(void *))(pVisualTreatmentPlat);
        dword_982B14 = 0;
    }
    if (g_VideoOptionsChanged)
    {
        r->LogCrumbMark("video options reset begins");
        sub_6C5DA0();
        sub_6D7500();
        if (pVisualTreatmentPlat)
            NFS_ORIGINAL(0x6d4dc0, void(__thiscall *)(void *))(pVisualTreatmentPlat);
        int width, height;
        GetRacingResolution(&width, &height);
        InitPresentParameters(width, height, g_RefreshRates[__resolution_index]);
        sub_6DB0D0();
        r->LogCrumbMark("video options reset ends");
        g_VideoOptionsChanged = false;
    }
    dword_982BB8 = 0;
    if (SaveStreamAnim)
    {
        dword_982BB8 = 2;
        dword_982BBC = 0;
    }
    else if (dword_982BBC)
    {
        dword_982BB8 = 1;
    }
    MaybeChangeViewMode();
    switch (lpData_009017f4)
    {
    case 0:
        dword_982B34 = dword_9A3A34 = 1;
        dword_982AE0 = 2;
        break;
    case 1:
        dword_982B34 = dword_9A3A34 = 1;
        dword_982AE0 = 1;
        break;
    case 2:
        dword_982B34 = dword_9A3A34 = 1;
        dword_982AE0 = 0;
        break;
    case 3:
        dword_982B34 = dword_9A3A34 = 0;
        dword_982AE0 = 0;
        break;
    }
    if (EnableEnvMap__009017B8)
        UpdateEnvironmentMapCameras();
    elBeginFrame();
    for (int v = 0; v <= 0x17; ++v)
    {
        eView *view = &eViews[v];
        eViewPlatInfo *plat = view->PlatInfo;
        if (v == 8)
            sub_6D0B00(view, 0);
        if (view->Active || v >= 0x12)
            plat->SetupProjection(view, view->ID == 3 ? dword_901AC4 : 0.0f, 15000.0f, 0, true);
    }
    eResetRenderStates();
    SetupDepthOfField();

    r->BeginFrame();
    dword_8FAE6C = 2;
    eRenderFrontEndPass(1);
    if (!GiveTheMoviePlayerBandwidth())
    {
        for (int v = 1; v <= 3; ++v)
            if (eViews[v].Active)
                SetupWorldLightContext(&eViews[v]);
        if (!ExitTheGameFlag)
            eResetRenderStates();
        ___eDisplayFrame();
        sub_6C3870();
    }
    eRenderFrontEndPass(0);
    sub_6C6F00();
    if (dword_982BF8)
    {
        sub_6C05E0(0);
        dword_982BF8 = 0;
    }
    eServiceScreenCapture();
    r->EndFrame();
    if (dword_9885C4)
    {
        nullsub_36();
        dword_9885C4 = 0;
    }
    nullsub_35();
    ePostDisplay();
    // The original clears the back buffer for the next frame right after presenting it.
    r->BeginFrame();
    r->Clear(D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);
    // A frame that ran out of frame memory silently dropped whatever asked for more (cars, flares, shadow casters).
    static DWORD s_peak, s_logged;
    const DWORD used = static_cast<DWORD>(CurrentBufferPos - CurrentBufferStart);
    s_peak = used > s_peak ? used : s_peak;
    if ((FrameMallocFailed || nfs::vk::HasArgument("-framememory")) && GetTickCount() - s_logged > 4000)
    {
        s_logged = GetTickCount();
        nfs::Log("frame memory: %u of %u bytes used (peak %u), %u more asked for", used, FrameMemoryBufferSize, s_peak,
                 FrameMallocFailed ? FrameMallocFailAmount : 0);
    }
    eSwapFrameMallocBuffers();
    eSwapDynamicFrameMemory();
    ++eFrameCounter;
    dword_982CA0 = 0;
    dword_982CA4 = 0;
    NFS_ORIGINAL(0x6c9c80, void(__thiscall *)(void *))(_unk_globalmouseshit_00987f50);
    nfs::fescript::Mouse();
}
