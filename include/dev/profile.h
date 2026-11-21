#pragma once

/**
 * Frame profiler (-profile): CPU and GPU time of each part of the frame, logged to nfs.log every few seconds as
 * averages per frame. GPU times come from timestamp queries (Direct3D 9) or timestamps in the command buffer (Vulkan),
 * read a few frames later, so nothing waits for them. Without -profile the scopes cost a flag check.
 */
namespace nfs::profile
{
enum Section
{
    kCull,
    kRoadReflection,
    kEnvMaps,
    kMirror,
    kShadowMap,
    kWorld,
    kOverlays,
    kPost,
    kFrontEndFirst,
    kFrontEnd,
    kSectionCount
};

/** CPU-only timers, which may run inside sections and each other, and per-frame counts. */
enum Timer
{
    kTimerDisplay,
    kTimerFlush,
    kTimerStuffScenery,
    kTimerEffects,
    kTimerTextures,
    kTimerTransform,
    kTimerLights,
    kTimerMaterial,
    kTimerDraw,
    kTimerClear,
    kTimerGpuWait,
    kTimerCount
};
enum Counter
{
    kCounterDraws,
    kCounterFlushes,
    kCounterEffectSwitches,
    kCounterPasses,
    kCounterPipelineBinds,
    kCounterDescriptorPushes,
    kCounterDescriptorWrites,
    kCounterVertexBinds,
    kCounterConstantRegisters,
    kCounterClears,
    kCounterBlockDraws,
    kCounterCount
};

extern bool g_enabled;

void Begin(Section section);
void End(Section section);
void AddTime(Timer timer, long long ticks);
void Count(Counter counter, int amount);
long long Now();

struct Scope
{
    explicit Scope(Section section) : section(section)
    {
        if (g_enabled)
            Begin(section);
    }
    ~Scope()
    {
        if (g_enabled)
            End(section);
    }
    Section section;
};

/** The main thread's own CPU cycles in eDisplayFrame (QueryThreadCycleTime): unlike the timers, time the thread spent
 * preempted (headless runs yield to other programs) does not count. */
void AddDisplayCycles(unsigned long long cycles);

struct DisplayCycleScope
{
    DisplayCycleScope();
    ~DisplayCycleScope();
    unsigned long long start;
};

struct TimerScope
{
    explicit TimerScope(Timer timer) : timer(timer), start(g_enabled ? Now() : 0)
    {
    }
    ~TimerScope()
    {
        if (g_enabled)
            AddTime(timer, Now() - start);
    }
    Timer timer;
    long long start;
};

/** The frame is finished (just before it is presented): closes this frame's measurements and opens the next's. */
void EndFrame();
/** Releases the Direct3D 9 queries; call before the device is reset or released. */
void ReleaseDeviceObjects();
} // namespace nfs::profile
