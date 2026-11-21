/**
 * The per-frame world update and draw (original: zMisc Main_AnimateFrame 0x64a680, Main_DisplayFrame 0x64a7c0),
 * extended for frames between simulation steps (core/presentation.h).
 *
 * When frames can outpace the 60 steps per second, the render connections no longer update only once per step with
 * the newest poses: every frame they update with the world placed between the last two steps, and every frame is drawn
 * with the cameras placed the same way. The cameras themselves still move once per step, as in the original, which also
 * gave front-end cameras a fixed 1/100 s of motion on frames without a step; with interpolation that would make them
 * run fast.
 */
#include "game/gen/api.h"
#include "dev/aiwatch.h"
#include "dev/clipwatch.h"
#include "dev/dev.h"
#include "dev/fescript.h"
#include "dev/testbridge.h"
#include "dev/devprofile.h"
#include "dev/pose.h"
#include "core/presentation.h"
#include "dev/proving.h"
#include "dev/pursuitpanel.h"
#include "dev/roaddump.h"
#include "dev/testtrack.h"

namespace
{
/** GameFlowManager::CurrentGameFlowState in the front end. */
const int kGameFlowInFrontEnd = 3;
/** Sim::State while the simulation runs. */
const int kSimRunning = 3;
/** How far front-end cameras move on a frame without a step, when frames are not interpolated. */
const float kStillFrameCameraTime = 0.01f;
/** Main_DisplayFrame's HackTime advance per frame (nothing reads HackTime). */
const float kHackTimeStep = 0.01666f;
/** The speed blur pass (0x6dbb20) fades its extra blur in and out by these steps per frame. */
const float kSpeedBlurFadeInStep = 0.02f;
const float kSpeedBlurFadeOutStep = 0.01f;

float g_CameraTime;
unsigned int g_LastAnimatedTick;
unsigned int g_LastAnimatedFrame;

/** Not in the original: the speed blur's fade steps assume 60 frames per second, so above that they are scaled to the
 * frame's time and the fades take as long as before. */
void ScaleSpeedBlurFades()
{
    const float scale = nfs::present::Active() ? RealTimeElapsed * 60.0f : 1.0f;
    SpeedBlurFadeInStep = kSpeedBlurFadeInStep * scale;
    SpeedBlurFadeOutStep = kSpeedBlurFadeOutStep * scale;
}
} // namespace

float Main_AnimateFrame(float world_time)
{
    const bool interpolate = nfs::present::Active();
    if (world_time <= 0.0f)
    {
        if (interpolate)
            nfs::present::AnimateRender();
        else if (TheGameFlowManager.CurrentGameFlowState == kGameFlowInFrontEnd)
        {
            CameraAI::Update(kStillFrameCameraTime);
            Camera::UpdateAll(kStillFrameCameraTime);
        }
        return 0.0f;
    }

    if (!TweakerPauseCamera)
        g_CameraTime += world_time;
    if (Sim::Exists() && static_cast<int>(Sim::GetState()) == kSimRunning && g_LastAnimatedTick != Sim::GetTick() &&
        g_LastAnimatedFrame != eFrameCounter)
    {
        const float frame_time = Sim::GetFrameTimeElapsed();
        if (frame_time <= 0.0f)
            return 0.0f;
        g_LastAnimatedTick = Sim::GetTick();
        g_LastAnimatedFrame = eFrameCounter;
        WorldConn::UpdateServices(frame_time);
        SoundConn::UpdateServices(frame_time);
        if (interpolate)
            nfs::present::CaptureBodies();
        else
            RenderConn::UpdateServices(frame_time);
    }
    CameraAI::Update(g_CameraTime);
    Camera::UpdateAll(g_CameraTime);
    g_CameraTime = 0.0f;
    if (interpolate)
    {
        nfs::present::CaptureCameras();
        nfs::present::AnimateRender();
    }
    return world_time;
}

void Main_DisplayFrame()
{
    if (gFramesToSkip > 0)
    {
        --gFramesToSkip;
        nfs::present::Restore();
        return;
    }
    FrameTimingStartTime = FrameTimingEndTime;
    FrameTimingEndTime = bGetTicker();
    RenderTimingStart = bGetTicker();
    nfs::present::ApplyCameras();
    nfs::pose::Update();
    nfs::clipwatch::Update();
    nfs::aiwatch::Update();
    nfs::dev::Update();
    nfs::testtrack::Update();
    nfs::proving::Update();
    nfs::pursuitpanel::Update();
    nfs::roaddump::Update();
    nfs::fescript::Update();
    nfs::testbridge::Update();
    nfs::devprofile::Update();
    ScaleSpeedBlurFades();
    eDisplayFrame();
    nfs::present::Restore();
    RenderTimingEnd = bGetTicker();
    HackTime += kHackTimeStep;
    EnableInterrupts();
}
