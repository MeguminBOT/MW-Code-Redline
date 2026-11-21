/**
 * Scheduler::Run (original: zMain 0x642e60), which advances the game once per frame.
 *
 * The game simulates in fixed steps of mTimestep (1/60 s, set by Scheduler::Init) and carries the remainder of a step
 * over to the next frame. The original also slept away the rest of a step whenever a frame came in early, which held
 * the game to 60 frames per second. That sleep is gone: frames are paced by nfs::timing instead, and above 60 frames
 * per second most frames run no step at all and show a state interpolated between the last two steps
 * (core/presentation.h), using the carried remainder. The steps themselves are unchanged, so the driving physics, which
 * the game tuned for exactly this step, behave as in the original at any frame rate.
 */
#include <stddef.h>

#include "game/gen/api.h"

static_assert(sizeof(Scheduler) == 0x2C, "Scheduler layout");
static_assert(offsetof(Scheduler, mLastTime) == 0x14, "Scheduler layout");
static_assert(offsetof(Scheduler, mStepFraction) == 0x1C, "Scheduler layout");
static_assert(offsetof(Scheduler, mTimestep) == 0x20, "Scheduler layout");
static_assert(offsetof(Scheduler, mStepCount) == 0x28, "Scheduler layout");

namespace
{
/** Timer ticks are 1/4000 s. */
const float kSecondsPerTick = 0.00025f;
/** Full-speed mode (a debug tweak) runs four steps per frame, as fast as the machine allows. */
const unsigned int kFullSpeedSteps = 4;
} // namespace

float Scheduler::Run(bool full_speed)
{
    const Timer target = GetTargetTimer();
    const float elapsed = static_cast<float>(target.PackedTime - mLastTime) * kSecondsPerTick;
    mLastTime = target.PackedTime;
    if (elapsed < 0.0f)
        return 0.0f;
    mTotalTime += elapsed;

    const float steps_due = elapsed / mTimestep + mStepFraction;
    unsigned int steps = static_cast<unsigned int>(steps_due);
    mStepFraction = steps_due - static_cast<float>(steps);
    if (full_speed)
    {
        steps = kFullSpeedSteps;
        mStepFraction = 0.0f;
    }
    RunNormalSpeed(steps);
    return static_cast<float>(steps) * mTimestep;
}
