#pragma once

/**
 * Frame rate of the rebuilt game. The original simulated in fixed 1/60 s steps and slept in Scheduler::Run until a step
 * was due, so it never drew more than 60 frames per second. The steps are unchanged, but frames are no longer tied to
 * them: the main loop holds frames to the limit chosen on the Video page (speed.ini [Display] FrameRate), and frames
 * between steps show the world interpolated (core/presentation.h).
 */
namespace nfs::timing
{
/** Limits other than a number of frames per second. kVrrRate is for variable refresh rate monitors (FreeSync, G-Sync):
 * a little under the refresh rate (VrrRate()), so that frames stay inside the monitor's range and never wait for a
 * fixed refresh. The monitor only varies its refresh in exclusive fullscreen (Display Mode: Fullscreen); in a window
 * the desktop compositor shows frames at the fixed refresh rate. */
const int kUncapped = 0;
const int kMonitorRate = -1;
const int kVrrRate = -2;

/**
 * How frames are synchronised with the monitor, one choice on the Video page (speed.ini [Display] Sync):
 * - Off: frames are shown as they finish (tearing possible), held to the frame rate limit;
 * - VSync: each frame waits for the monitor's refresh, which is then the frame rate;
 * - Vrr: for variable refresh rate monitors (G-Sync, FreeSync): frames are presented with the refresh wait on and held
 *   a little under the refresh rate (VrrRate(), or a lower limit), so every frame is shown the moment it is ready,
 *   without tearing and without waiting.
 * A speed.ini from before the key gives Vrr for FrameRate = vrr, VSync for [Video] VSyncOn, else Off.
 */
enum class Sync
{
    Off,
    VSync,
    Vrr,
};

/** Reads the limit and the sync mode from speed.ini. */
void Initialize();
Sync SyncMode();
/** Sets the sync mode and saves it to speed.ini; the swap interval changes at the next device reset, which the caller
 * requests (g_VideoOptionsChanged). */
void SetSyncMode(Sync mode);
/** Whether frames wait for the monitor's refresh (VSync and Vrr). */
bool WaitsForRefresh();
/** The limit after `limit` among those the Video page offers with sync mode `mode` (step 1) or before it (step -1),
 * without wrapping: with Off the numbers, the monitor's rate and uncapped; with Vrr automatic (kVrrRate) and the
 * numbers under VrrRate(). */
int StepLimitFor(Sync mode, int limit, int step);
/** `limit` as sync mode `mode` can have it (kVrrRate only with Vrr, no number at or over VrrRate() there). */
int FitLimit(Sync mode, int limit);
/** kMonitorRate, kVrrRate, kUncapped or frames per second. */
int Limit();
/** The frames per second kVrrRate holds to: the refresh rate less its square over 3600 (138 at 144 Hz, 224 at 240 Hz),
 * the margin NVIDIA Reflex uses for the same purpose. */
int VrrRate();
/** Sets the limit and saves it to speed.ini. */
void SetLimit(int limit);
/** The limit after `limit` in the Video page's list (step 1) or before it (step -1), wrapping around. */
int StepLimit(int limit, int step);
/** Picks up a new monitor refresh rate or VSync setting; called whenever the device is (re)configured. */
void Refresh();
/** The shortest time a frame may take, in milliseconds. */
float MinFrameMs();
/** True when frames may come faster than the simulation's 60 steps per second (see core/presentation.h). */
bool AboveStepRate();
/** Sleeps for most of `ms` milliseconds, leaving the last part to the caller, which polls the clock. */
void Wait(float ms);
/** For original code that passes each frame's time on in whole units, truncated: returns `truncated` plus the whole
 * units that the truncations have dropped so far, and keeps the rest in `carry`. `exact` is the untruncated value. */
int AddDroppedUnits(int truncated, float exact, float &carry);
} // namespace nfs::timing
