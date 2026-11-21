/**
 * Smaller X360 Stuff changes to game functions: the GPU multipliers it fixes at 10 (sub_6CB190, sub_6DD380), the
 * cutscene time of day and middle gray events (ENISTimeOfDay_ResolveEvent_Callback, SetMiddleGrayValue), the
 * split screen views (Rendering::epInitViewsEx), the visual treatment always on, the performance level, the screen
 * effects no longer ticked (TickSFX), the shadow views' settings and the Shadow Detail limit. Each replaced function has that one caller, so
 * the change applies where the plugin made it.
 */
#include "game/gen/api.h"
#include "game/platform/Platform.h"
#include "legacy/link.h"
#include "engine/rendering/x360/render.h"

NFS_REPLACE(0x6cb190, X360GpuMultiplier)
NFS_REPLACE(0x6dd380, X360GpuMultiplier2)
NFS_REPLACE(0x73db00, nullsub_166)
NFS_REPLACE(0x62ecb0, ENISTimeOfDayCallback)
NFS_REPLACE(0x6c06a0, epInitViews)
NFS_REPLACE(0x6c0a20, sub_6C0A20)

extern "C" char *TimeOfDayPointer;
extern "C" int TheGameFlowManager_CurrentGameFlowState;
/** Shadow map method: 0 none, 1 depth textures, 2 DF16. */
extern "C" int dword_982C08;
extern "C" float flt_905CE8;
/** The shadow map views' settings, one record of five values per view: field of view (degrees), then levels. */
extern "C" char flt_8FAEE0[];
extern "C" char dword_93DAD4[];

namespace nfs::x360
{
extern float g_middleGray;
}

double __cdecl X360GpuMultiplier(int, int)
{
    return 10.0;
}

double __cdecl X360GpuMultiplier2(int)
{
    return 10.0;
}

/** A cutscene's middle gray (SetMiddleGrayValue): negative values restore the default. */
extern "C" void __cdecl nullsub_166(float value)
{
    nfs::x360::g_middleGray = value >= 0.0f ? value : nfs::x360::options::kMiddleGrayValue;
}

/** A cutscene's time of day event, applied as it resolves (ENISTimeOfDay_ResolveEvent_Callback). */
void __cdecl ENISTimeOfDayCallback(const float *data)
{
    if (!nfs::x360::options::kNISTimeOfDayEvent)
        return;
    float *tod = reinterpret_cast<float *>(TimeOfDayPointer);
    tod[2] = data[1];
    tod[0] = data[2];
    tod[3] = data[3];
    flt_905CE8 = data[4];
}

/** The views at engine start (epInitViews), with the second player's view and its split screen twin set up as the
 * Xbox 360 version has them (Rendering::epInitViewsEx). */
extern "C" void __cdecl epInitViews()
{
    NFS_ORIGINAL(0x6c06a0, void(__cdecl *)())();
    char *views = reinterpret_cast<char *>(eViews);
    *reinterpret_cast<char **>(views + 0x140) = reinterpret_cast<char *>(0x93DAB8); // the render target table's first entry
    views[0xe8] = 0;
    *reinterpret_cast<int *>(views + 0x270) = *reinterpret_cast<int *>(views + 0x120);
    views[0x238] = 0;
    *reinterpret_cast<char **>(views + 0x290) = dword_93DAD4;
    *reinterpret_cast<int *>(views + 0x298) = *reinterpret_cast<int *>(views + 0x148);
}

/** Whether the visual treatment is on (sub_6C0A20): always, it is part of the Xbox 360 look. */
extern "C" bool __cdecl sub_6C0A20()
{
    return true;
}

/** The screen effects' tick (TickSFX): X360 Stuff draws none of the screen effects (ePreDisplay and ePostDisplay leave
 * them out), so they are not updated either; only the frame they were last ticked at follows. */
void TickSFX()
{
    // TickSFX's static: the frame the effects were last ticked at (not exported by the delinked objects).
    if (TheGameFlowManager_CurrentGameFlowState == 6)
        *reinterpret_cast<unsigned int *>(0x9AE358) = eFrameCounter;
}

/** The video options the device supports (DetectVideoOptionLimits), with Shadow Detail up to high whenever there is a
 * shadow map method. The original takes the shadow limit from the effects' shadow level techniques, which the Xbox 360
 * effects do not have (one technique draws every level), so it would stay at off. */
void DetectVideoOptionLimits()
{
    NFS_ORIGINAL(0x6d2100, void(__cdecl *)())();
    if (dword_982C08 && g_VideoOptionLimits.ShadowDetail < 2)
        g_VideoOptionLimits.ShadowDetail = 2;
}

namespace nfs::x360
{
void ApplyDataChanges()
{
    // The second shadow view takes the first's settings: a 10 degree field of view and levels 2 and 2 instead of 3
    // degrees and 4 and 4 (X360 Stuff's data patches at 0x8faef4-0x8faf00).
    *reinterpret_cast<float *>(flt_8FAEE0 + 0x14) = 10.0f;
    *reinterpret_cast<int *>(flt_8FAEE0 + 0x1c) = 2;
    *reinterpret_cast<int *>(flt_8FAEE0 + 0x20) = 2;
    // The garages' car light rigs (ShaperLightsBackRoom, CarLot, CShop, QRace, Safehouse: 0xa0 bytes each, the three
    // lights' scales at +0x38, +0x54, +0x70) at the Xbox 360 strengths, X360 Stuff's CarShaperLightType 0; the
    // directions and colours, and the in-game rig, are the same as the PC's.
    static const float kGarageScales[3] = {0.63f, 0.33f, 0.23f};
    for (unsigned int rig = 0x8f2b50; rig <= 0x8f2dd0; rig += 0xa0)
        for (int light = 0; light < 3; ++light)
            *reinterpret_cast<float *>(rig + 0x38 + light * 0x1c) = kGarageScales[light];
}
} // namespace nfs::x360

/** The performance level's options, clamped (ApplyPerformanceLevel); X360 Stuff keeps the depth effects option on ATI
 * cards, which the original turned off. */
void ApplyPerformanceLevel()
{
    SetPerformanceLevelOptions();
    ClampVideoOptions();
}
