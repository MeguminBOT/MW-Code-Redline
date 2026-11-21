/**
 * Frame rate limit (core/timing.h), and the game's real-time clock (original: zMisc AdvanceRealTime 0x65c6d0) made to
 * count time rather than frames above 60 frames per second.
 */
#include <windows.h>
#include <mmsystem.h>

#include "game/platform/Platform.h"
#include "core/config.h"
#include "engine/rendering/display/display.h"
#include "core/timing.h"
#include "engine/rendering/vulkan/renderer.h"

extern "C" int TheGameFlowManager_CurrentGameFlowState;

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace nfs::timing
{
namespace
{
/** The main loop's floor, from the original: shorter frames would not advance the game's 1/4000 s real-time clock. */
const float kMinFrameMs = 0.25f;
/** Sleeps end this early; the main loop polls the clock for the rest, which a sleep could overshoot. */
const float kTimerSpinMs = 0.5f;
const float kSleepSpinMs = 1.5f;
const int kFallbackRefreshRate = 60;
/** The simulation's steps per second. */
const int kStepRate = 60;
const int kLowestLimit = 20;
/** -headless (engine/rendering/display/display.h) holds races to 30 frames per second, or Debug.HeadlessFps, leaving the GPU to whatever else
 * is running; -uncapped lets them run as fast as they can (measuring frame times). */
const int kHeadlessFps = 30;
const int kGameFlowRacing = 6;
const int kHighestLimit = 1000;
/** The Video page's choices, in order: the modes, then the numbers, then uncapped. */
const int kLimits[] = {kMonitorRate, kVrrRate, 30, 60, 75, 90, 100, 120, 144, 165, 180, 240, 360, kUncapped};
const int kLimitCount = sizeof(kLimits) / sizeof(kLimits[0]);
const int kFirstNumber = 2;

/** The numbers the Video page offers. */
const int kNumbers[] = {30, 60, 75, 90, 100, 120, 144, 165, 180, 240, 360};
const int kNumberCount = sizeof(kNumbers) / sizeof(kNumbers[0]);

struct State
{
    int limit = kMonitorRate;
    Sync sync = Sync::Off;
    float min_frame_ms = kMinFrameMs;
    bool above_step_rate = false;
    /** A high-resolution waitable timer (Windows 10 1803 and later); without one, waits use Sleep. */
    HANDLE timer = nullptr;
    float spin_ms = kSleepSpinMs;
};

State g_state;

int ParseLimit(const char *text)
{
    if (!lstrcmpA(text, "monitor"))
        return kMonitorRate;
    if (!lstrcmpA(text, "vrr"))
        return kVrrRate;
    if (!lstrcmpA(text, "uncapped") || !lstrcmpA(text, "unlimited") || !lstrcmpA(text, "off"))
        return kUncapped;
    const int limit = bStrToLong(text);
    if (limit == 0 && text[0] == '0')
        return kUncapped;
    if (limit <= 0)
        return kMonitorRate;
    return limit < kLowestLimit ? kLowestLimit : limit > kHighestLimit ? kHighestLimit : limit;
}
} // namespace

void Initialize()
{
    char text[16];
    config::String("Display", "FrameRate", "monitor", text, sizeof(text));
    g_state.limit = ParseLimit(text);
    config::String("Display", "Sync", "", text, sizeof(text));
    if (!lstrcmpA(text, "vrr"))
        g_state.sync = Sync::Vrr;
    else if (!lstrcmpA(text, "vsync") || !lstrcmpA(text, "on"))
        g_state.sync = Sync::VSync;
    else if (!lstrcmpA(text, "off"))
        g_state.sync = Sync::Off;
    else
        // A speed.ini from before the key.
        g_state.sync = g_state.limit == kVrrRate              ? Sync::Vrr
                       : config::Int("Video", "VSyncOn", 0) ? Sync::VSync
                                                            : Sync::Off;
    g_state.limit = FitLimit(g_state.sync, g_state.limit);
    g_state.timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (g_state.timer)
        g_state.spin_ms = kTimerSpinMs;
    else
        timeBeginPeriod(1);
    Refresh();
}

int Limit()
{
    return g_state.limit;
}

Sync SyncMode()
{
    return g_state.sync;
}

void SetSyncMode(Sync mode)
{
    g_state.sync = mode;
    config::WriteString("Display", "Sync", mode == Sync::Vrr ? "vrr" : mode == Sync::VSync ? "vsync" : "off");
    Refresh();
}

bool WaitsForRefresh()
{
    return g_state.sync != Sync::Off;
}

int FitLimit(Sync mode, int limit)
{
    if (mode == Sync::Vrr)
        return limit > 0 && limit < VrrRate() ? limit : kVrrRate;
    return limit == kVrrRate ? kMonitorRate : limit;
}

int StepLimitFor(Sync mode, int limit, int step)
{
    // The choices from fewest frames to most: the numbers (with Vrr those under the automatic rate), then with Vrr
    // automatic, else the monitor's rate and uncapped.
    int choices[kNumberCount + 2];
    int count = 0;
    for (int i = 0; i < kNumberCount; ++i)
        if (mode != Sync::Vrr || kNumbers[i] < VrrRate())
            choices[count++] = kNumbers[i];
    if (mode == Sync::Vrr)
        choices[count++] = kVrrRate;
    else
    {
        choices[count++] = kMonitorRate;
        choices[count++] = kUncapped;
    }
    limit = FitLimit(mode, limit);
    int index = -1;
    for (int i = 0; i < count; ++i)
        if (choices[i] == limit)
            index = i;
    if (index < 0)
    {
        // A number from speed.ini that the list lacks: between its neighbours.
        int below = -1;
        for (int i = 0; i < count; ++i)
            if (choices[i] > 0 && choices[i] < limit)
                below = i;
        index = step > 0 ? below : below + 1;
    }
    index += step;
    return choices[index < 0 ? 0 : index >= count ? count - 1 : index];
}

int VrrRate()
{
    const int refresh_rate = display::RefreshRate() > 1 ? display::RefreshRate() : kFallbackRefreshRate;
    const int rate = refresh_rate - refresh_rate * refresh_rate / 3600;
    return rate < kLowestLimit ? kLowestLimit : rate;
}

void SetLimit(int limit)
{
    g_state.limit = limit;
    char text[16];
    if (limit == kMonitorRate)
        lstrcpyA(text, "monitor");
    else if (limit == kVrrRate)
        lstrcpyA(text, "vrr");
    else if (limit == kUncapped)
        lstrcpyA(text, "uncapped");
    else
        wsprintfA(text, "%d", limit);
    config::WriteString("Display", "FrameRate", text);
    Refresh();
}

int StepLimit(int limit, int step)
{
    for (int i = 0; i < kLimitCount; ++i)
        if (kLimits[i] == limit)
            return kLimits[(i + step + kLimitCount) % kLimitCount];
    // A number from speed.ini that the list lacks: step to its neighbours among the numbers.
    int below = kFirstNumber - 1;
    for (int i = kFirstNumber; i < kLimitCount - 1; ++i)
        if (kLimits[i] < limit)
            below = i;
    return kLimits[step > 0 ? below + 1 : below];
}

void Refresh()
{
    const int refresh_rate = display::RefreshRate();
    // With Vrr the frames stay under the refresh rate whatever the limit says; with VSync the refresh rate is the limit.
    int limit = g_state.limit == kMonitorRate ? (refresh_rate > 1 ? refresh_rate : kFallbackRefreshRate)
                : g_state.limit == kVrrRate   ? VrrRate()
                                              : g_state.limit;
    if (g_state.sync == Sync::Vrr && (limit == kUncapped || limit > VrrRate()))
        limit = VrrRate();
    // With VSync the device already holds frames to the refresh rate; a limiter at that rate could only miss refreshes.
    const bool vsync = g_PresentParameters.PresentationInterval != D3DPRESENT_INTERVAL_IMMEDIATE;
    const bool vsync_limits = vsync && refresh_rate > 1 && (limit == kUncapped || limit >= refresh_rate);
    if (limit == kUncapped || vsync_limits)
        g_state.min_frame_ms = kMinFrameMs;
    else
        g_state.min_frame_ms = 1000.0f / static_cast<float>(limit);
    const int max_rate = vsync_limits ? refresh_rate : limit;
    g_state.above_step_rate = max_rate == kUncapped || max_rate > kStepRate;
}

float MinFrameMs()
{
    static const bool uncapped = vk::HasArgument("-uncapped");
    static const float headlessFrameMs = 1000.0f / static_cast<float>(config::Int("Debug", "HeadlessFps", kHeadlessFps));
    if (display::Headless() && !uncapped && TheGameFlowManager_CurrentGameFlowState == kGameFlowRacing &&
        g_state.min_frame_ms < headlessFrameMs)
        return headlessFrameMs;
    if (uncapped)
        return 0.0f;
    return g_state.min_frame_ms;
}

bool AboveStepRate()
{
    return g_state.above_step_rate;
}

void Wait(float ms)
{
    const float sleep_ms = ms - g_state.spin_ms;
    if (sleep_ms <= 0.0f)
        return;
    if (g_state.timer)
    {
        LARGE_INTEGER due;
        due.QuadPart = -static_cast<LONGLONG>(sleep_ms * 10000.0f);
        if (SetWaitableTimer(g_state.timer, &due, 0, nullptr, nullptr, FALSE))
        {
            WaitForSingleObject(g_state.timer, INFINITE);
            return;
        }
    }
    Sleep(static_cast<DWORD>(sleep_ms));
}

int AddDroppedUnits(int truncated, float exact, float &carry)
{
    carry += exact - static_cast<float>(truncated);
    const int dropped = static_cast<int>(carry);
    carry -= static_cast<float>(dropped);
    return truncated + dropped;
}
} // namespace nfs::timing

/**
 * Advances the real-time clocks by the frame's real time: RealTimer (1/4000 s ticks) and RealTimeFrames, a count of
 * 1/60 s periods that drives real-time texture animations, speech timing and camera bookkeeping. The original counted
 * at least one period per frame; above 60 frames per second that made RealTimeFrames, and everything it times, run as
 * fast as the frame rate. Here it counts only whole periods, carrying the remainder as before.
 */
void AdvanceRealTime()
{
    const float elapsed = RealTimeElapsed + RealTimeElapsedFrame;
    const int periods = static_cast<int>(elapsed * 60.0f);
    RealTimeElapsedFrame = elapsed - static_cast<float>(periods) * (1.0f / 60.0f);
    RealTimeFrames += periods;
    RealTimeFramesElapsed = periods;
    RealTime = RealTimeFrames;
    RealTimer.PackedTime += static_cast<int>(RealTimeElapsed * 4000.0f + 0.5f);
    ++RealLoopCounter;
}
