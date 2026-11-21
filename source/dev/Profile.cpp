/**
 * Frame profiler (dev/profile.h). Each frame has GPU marks at its start and end and around each section: slot 0 the
 * start, 1 + 2 * section and 2 + 2 * section a section's start and end, the last slot the end. A section that runs
 * twice in a frame is timed once on the GPU (its first run) and fully on the CPU.
 */
#include "dev/profile.h"

#include "game/platform/Platform.h"
#include "core/log.h"
#include "engine/rendering/vulkan/renderer.h"

namespace nfs::profile
{
bool g_enabled;

namespace
{
const int kSlots = 2 + 2 * kSectionCount;
const int kFrameEnd = kSlots - 1;
const DWORD kLogMilliseconds = 5000;
const char *const kNames[kSectionCount] = {"cull",     "reflection", "envmaps",  "mirror",  "shadowmap",
                                           "world",    "overlays",   "post",     "fe1",     "fe"};
const char *const kTimerNames[kTimerCount] = {"display",   "flush",  "stuffscenery", "effects", "textures",
                                              "transform", "lights", "material",     "draw",    "clear",
                                              "gpuwait"};
const char *const kCounterNames[kCounterCount] = {"draws",          "flushes",          "effectswitches",
                                                   "passes",         "pipelinebinds",    "descriptorpushes",
                                                   "descriptorwrites", "vertexbinds",    "constantregisters",
                                                   "clears",         "blockdraws"};

struct State
{
    bool checked;
    LARGE_INTEGER frequency;
    LARGE_INTEGER frameStart;
    LARGE_INTEGER sectionStart[kSectionCount];
    bool open;
    unsigned int written;
    // Totals since the last log line.
    DWORD lastLog;
    int frames;
    double cpuFrame;
    double cpu[kSectionCount];
    int gpuFrames;
    double gpuFrame;
    double gpu[kSectionCount];
    long long timers[kTimerCount];
    unsigned long long displayCycles;
    int counters[kCounterCount];
    /** Draws counted while each section was open. */
    int section;
    int sectionDraws[kSectionCount];
};

State s = {};

double Milliseconds(LONGLONG ticks)
{
    return static_cast<double>(ticks) * 1000.0 / static_cast<double>(s.frequency.QuadPart);
}

void Mark(int slot)
{
    if (s.written & (1u << slot))
        return;
    s.written |= 1u << slot;
    nfs::vk::g_renderer->WriteTimestamp(static_cast<uint32_t>(slot));
}

void Open()
{
    s.open = true;
    s.written = 0;
    Mark(0);
}

/** Adds one frame's GPU times from the marks' timestamps (ticks of `frequency` a second). */
void AddGpuFrame(const UINT64 *marks, unsigned int written, double ticksPerMs)
{
    if (!(written & 1u) || !(written & (1u << kFrameEnd)))
        return;
    ++s.gpuFrames;
    s.gpuFrame += static_cast<double>(static_cast<LONGLONG>(marks[kFrameEnd] - marks[0])) / ticksPerMs;
    for (int i = 0; i < kSectionCount; ++i)
    {
        const int begin = 1 + 2 * i, end = begin + 1;
        if ((written & (1u << begin)) && (written & (1u << end)) && marks[end] >= marks[begin])
            s.gpu[i] += static_cast<double>(static_cast<LONGLONG>(marks[end] - marks[begin])) / ticksPerMs;
    }
}

void CollectGpu()
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    uint64_t ns[nfs::vk::Renderer::kMaxTimestamps];
    uint32_t written = 0;
    if (r->LatestTimestamps(ns, written))
        AddGpuFrame(ns, written, 1.0e6);
}

/** "12.34" (wsprintf has no floats). */
void Format(char *out, double ms)
{
    const int hundredths = static_cast<int>(ms * 100.0 + 0.5);
    wsprintfA(out, "%d.%02d", hundredths / 100, hundredths % 100);
}

void Log()
{
    char line[1024], value[32];
    const double frames = s.frames ? s.frames : 1;
    Format(value, s.cpuFrame / frames);
    int n = wsprintfA(line, "profile: %d frames, %s ms a frame; cpu", s.frames, value);
    for (int i = 0; i < kSectionCount; ++i)
    {
        Format(value, s.cpu[i] / frames);
        n += wsprintfA(line + n, " %s %s", kNames[i], value);
    }
    n += wsprintfA(line + n, "; cpu inside");
    for (int i = 0; i < kTimerCount; ++i)
    {
        Format(value, Milliseconds(s.timers[i]) / frames);
        n += wsprintfA(line + n, " %s %s", kTimerNames[i], value);
    }
    for (int i = 0; i < kCounterCount; ++i)
        n += wsprintfA(line + n, " %s %d", kCounterNames[i], s.frames ? s.counters[i] / s.frames : 0);
    n += wsprintfA(line + n, " displaykcycles %d", s.frames ? static_cast<int>(s.displayCycles / s.frames / 1000) : 0);
    n += wsprintfA(line + n, "; draws by section");
    for (int i = 0; i < kSectionCount; ++i)
        n += wsprintfA(line + n, " %s %d", kNames[i], s.frames ? s.sectionDraws[i] / s.frames : 0);
    if (s.gpuFrames)
    {
        const double gpuFrames = s.gpuFrames;
        Format(value, s.gpuFrame / gpuFrames);
        n += wsprintfA(line + n, "; gpu (%d frames) frame %s", s.gpuFrames, value);
        for (int i = 0; i < kSectionCount; ++i)
        {
            Format(value, s.gpu[i] / gpuFrames);
            n += wsprintfA(line + n, " %s %s", kNames[i], value);
        }
    }
    nfs::Log("%s", line);
    nfs::vk::g_renderer->LogShaderTimes();
}

bool Check()
{
    if (!s.checked)
    {
        s.checked = true;
        g_enabled = nfs::vk::HasArgument("-profile");
        QueryPerformanceFrequency(&s.frequency);
        QueryPerformanceCounter(&s.frameStart);
        s.lastLog = GetTickCount();
        s.section = -1;
    }
    return g_enabled;
}
} // namespace

void AddDisplayCycles(unsigned long long cycles)
{
    s.displayCycles += cycles;
}

DisplayCycleScope::DisplayCycleScope() : start(0)
{
    if (g_enabled)
        QueryThreadCycleTime(GetCurrentThread(), &start);
}

DisplayCycleScope::~DisplayCycleScope()
{
    if (!g_enabled)
        return;
    unsigned long long now;
    QueryThreadCycleTime(GetCurrentThread(), &now);
    s.displayCycles += now - start;
}

long long Now()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

void AddTime(Timer timer, long long ticks)
{
    s.timers[timer] += ticks;
}

void Count(Counter counter, int amount)
{
    s.counters[counter] += amount;
    if (counter == kCounterDraws && s.section >= 0)
        s.sectionDraws[s.section] += amount;
}

void Begin(Section section)
{
    s.section = section;
    QueryPerformanceCounter(&s.sectionStart[section]);
    if (!s.open)
        Open();
    Mark(1 + 2 * section);
}

void End(Section section)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    s.cpu[section] += Milliseconds(now.QuadPart - s.sectionStart[section].QuadPart);
    s.section = -1;
    Mark(2 + 2 * section);
}

void EndFrame()
{
    if (!Check())
        return;
    if (!s.open)
        Open();
    Mark(kFrameEnd);
    s.open = false;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    s.cpuFrame += Milliseconds(now.QuadPart - s.frameStart.QuadPart);
    s.frameStart = now;
    ++s.frames;
    CollectGpu();
    if (GetTickCount() - s.lastLog >= kLogMilliseconds)
    {
        Log();
        s.lastLog = GetTickCount();
        s.frames = s.gpuFrames = 0;
        s.cpuFrame = s.gpuFrame = 0.0;
        for (int i = 0; i < kSectionCount; ++i)
            s.cpu[i] = s.gpu[i] = 0.0;
        s.displayCycles = 0;
        for (long long &t : s.timers)
            t = 0;
        for (int &c : s.counters)
            c = 0;
        for (int &c : s.sectionDraws)
            c = 0;
    }
}

void ReleaseDeviceObjects()
{
    s.open = false;
}
} // namespace nfs::profile
