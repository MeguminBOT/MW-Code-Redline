/**
 * Default PC video options (original: zPlatform 0x6df8f0), applied on the first start (no "FirstTime" registry value)
 * and by "restore defaults" on the display options screen.
 *
 * The original picked a performance level from a GPU benchmark (capped for a few 2004-era cards), applied that level's
 * preset and clamped the result to what the device supports.
 *
 * Change from the original: the defaults are the highest settings the device supports. Performance level 5's preset
 * is applied, then every display option goes to the top of its slider or toggle (g_VideoOptionLimits, derived from the
 * D3D caps by DetectVideoOptionLimits). VSync keeps its default (off), and anti-aliasing stays off because this build
 * removes it (see InitPresentParameters). Safe mode (`speed.exe /s`) still resets to the lowest preset, as before. The
 * Windows 9x special cases are gone. Display mode and resolution are not part of these options any more
 * (engine/rendering/display/display.h); the racing resolution index stays as the preset leaves it.
 *
 * Also here: the Video page's quality presets (engine/rendering/display/video.h), built the same way.
 */
#include <stddef.h>

#include "game/gen/api.h"
#include "engine/rendering/display/video.h"
#include "engine/rendering/vulkan/renderer.h"

static_assert(sizeof(VideoOptions) == 0x84, "VideoOptions layout");
static_assert(offsetof(VideoOptions, FSAALevel) == 0x58, "VideoOptions layout");
static_assert(offsetof(VideoOptions, RacingResolution) == 0x6C, "VideoOptions layout");
static_assert(offsetof(VideoOptions, ShadowDetail) == 0x80, "VideoOptions layout");

namespace
{
const int kSafeModePerformanceLevel = 0;
const int kHighestPerformanceLevel = 5;

void RaiseToLimits(VideoOptions &options, const VideoOptions &limits)
{
    options.CarEnvironmentMapEnable = limits.CarEnvironmentMapEnable;
    options.CarEnvironmentMapUpdateData = limits.CarEnvironmentMapUpdateData;
    options.RoadReflectionEnable = limits.RoadReflectionEnable;
    options.MotionBlurEnable = limits.MotionBlurEnable;
    options.ParticleSystemEnable = limits.ParticleSystemEnable;
    options.WorldLodLevel = limits.WorldLodLevel;
    options.CarLodLevel = limits.CarLodLevel;
    options.OverBrightEnable = limits.OverBrightEnable;
    options.RainEnable = limits.RainEnable;
    options.TextureFiltering = limits.TextureFiltering;
    options.VisualTreatment = limits.VisualTreatment;
    options.ShadowDetail = limits.ShadowDetail;
}

/** The quality options the Video page shows. */
bool SameQuality(const VideoOptions &a, const VideoOptions &b)
{
    return a.CarEnvironmentMapEnable == b.CarEnvironmentMapEnable &&
           a.CarEnvironmentMapUpdateData == b.CarEnvironmentMapUpdateData &&
           a.RoadReflectionEnable == b.RoadReflectionEnable && a.WorldLodLevel == b.WorldLodLevel &&
           a.CarLodLevel == b.CarLodLevel && a.OverBrightEnable == b.OverBrightEnable && a.RainEnable == b.RainEnable &&
           a.TextureFiltering == b.TextureFiltering && a.VisualTreatment == b.VisualTreatment &&
           a.ShadowDetail == b.ShadowDetail;
}
} // namespace

void SetDefaultVideoOptions()
{
    if (g_SafeMode)
    {
        g_PerformanceLevel = kSafeModePerformanceLevel;
        SetPerformanceLevelOptions();
    }
    else
    {
        g_PerformanceLevel = kHighestPerformanceLevel;
        SetPerformanceLevelOptions();
        RaiseToLimits(g_VideoOptions, g_VideoOptionLimits);
    }
    ClampVideoOptions();
}

namespace nfs::video
{
VideoOptions Preset(int level, const VideoOptions &options)
{
    // SetPerformanceLevelOptions and ClampVideoOptions work on the live options, so the preset is made there and the
    // live options are put back.
    const VideoOptions live = g_VideoOptions;
    const int live_level = g_PerformanceLevel;
    g_PerformanceLevel = level;
    SetPerformanceLevelOptions();
    if (level >= kHighestPreset)
        RaiseToLimits(g_VideoOptions, g_VideoOptionLimits);
    ClampVideoOptions();
    VideoOptions preset = g_VideoOptions;
    g_VideoOptions = live;
    g_PerformanceLevel = live_level;

    preset.Version = options.Version;
    preset.Size = options.Size;
    preset.MotionBlurEnable = options.MotionBlurEnable;
    preset.FSAALevel = options.FSAALevel;
    preset.RacingResolution = options.RacingResolution;
    preset.FirstTime = options.FirstTime;
    preset.VSyncOn = options.VSyncOn;
    return preset;
}

int MatchingPreset(const VideoOptions &options)
{
    for (int level = kLowestPreset; level <= kHighestPreset; ++level)
        if (SameQuality(Preset(level, options), options))
            return level;
    return 0;
}

bool TestOptions()
{
    static const bool test = nfs::vk::HasArgument("-testvideo");
    return test;
}

void ApplyTestOptions()
{
    RaiseToLimits(g_VideoOptions, g_VideoOptionLimits);
    g_VideoOptions.VisualTreatment = 0;
    ClampVideoOptions();
}
} // namespace nfs::video
