/**
 * The Video options page (interface/menus/videopage.h). Not in the original game, whose page (UIOptionsScreen::SetupVideo 0x5295f0,
 * with a basic and an advanced view) this replaces.
 *
 * The page is a table of rows (kRows): headings, settings, and settings that show only while their parent setting
 * calls for them. It edits a copy of the settings (Settings); Accept puts the copy in use through the modules that own
 * each setting (engine/rendering/display/display.h, core/timing.h, interface/hud/overlay.h, engine/rendering/lights/lamps.h and the game's own video options). When a
 * change makes rows come or go, the list is built again around the row the player is on.
 */
#include <string.h>

#include "game/gen/api.h"
#include "core/config.h"
#include "engine/rendering/display/display.h"
#include "interface/engine/feng.h"
#include "engine/rendering/lights/lamps.h"
#include "core/log.h"
#include "interface/menus/optionsrows.h"
#include "interface/hud/overlay.h"
#include "core/timing.h"
#include "engine/rendering/display/video.h"
#include "engine/rendering/surface.h"
#include "interface/menus/videopage.h"
#include "engine/rendering/vulkan/renderer.h"

namespace
{
using nfs::display::AntiAliasing;
using nfs::optionsrows::Metrics;
using nfs::timing::Sync;

const unsigned int kActionLeft = 0x9120409E;
const unsigned int kActionRight = 0xB5971BF1;
/** The screen's messages: Accept, and the answers of the "keep these settings?" countdown. */
const unsigned int kMessageAccept = 0x406415E3;
const unsigned int kMessageKeep = 0x4D0D83B9;
/** Scripts that show and hide an object (FEScrollBar's). */
const unsigned int kScriptShown = 0x1CA7C0;
const unsigned int kScriptHidden = 0x16A259;
/** The script a row's objects run while the row is highlighted, and the least a name fades to in it. */
const unsigned int kScriptHighlight = 0x249DB7B7;
const int kHighlightLeastAlpha = 150;
/** The group inside the cursor that holds its brackets and arrows ("CURSOR"). */
const unsigned int kCursorInner = 0x557FEADD;

enum Option
{
    kHeadingDisplay,
    kDisplayMode,
    kResolution,
    kRefreshRate,
    kWidescreen,
    kSync,
    kFrameLimit,
    kFpsCounter,
    kHeadingQuality,
    kPreset,
    kAntiAliasing,
    kFxaaQuality,
    kSupersampling,
    kTextureFiltering,
    kAnisotropy,
    kWorldDetail,
    kCarDetail,
    kShadows,
    kDynamicShadows,
    kSurfaceDetail,
    kRoadWear,
    kCarReflections,
    kCarReflectionDetail,
    kCarReflectionSize,
    kRoadReflections,
    kRoadReflectionSize,
    kHeadingEffects,
    kRain,
    kBloom,
    kMotionBlur,
    kTreatment,
    kLightGlow,
    kOptionCount,
};

enum RowKind
{
    kHeading,
    kSetting,
    /** A setting of the setting above it. */
    kSubSetting,
};

struct RowInfo
{
    Option id;
    RowKind kind;
    const char *title;
    const char *description;
};

const RowInfo kRows[] = {
    {kHeadingDisplay, kHeading, "DISPLAY", ""},
    {kDisplayMode, kSetting, "Display Mode",
     "Borderless fills the screen as a window and switches to other programs at once. Fullscreen draws at the "
     "resolution below. Windowed keeps the title bar."},
    {kResolution, kSubSetting, "Resolution",
     "The size the game is drawn at. Lower sizes run faster and look softer."},
    {kRefreshRate, kSubSetting, "Refresh Rate", "How often the monitor shows a new picture in fullscreen."},
    {kWidescreen, kSetting, "Widescreen",
     "A wider view on wide monitors, with the HUD at the screen's edges. Off keeps the original 4:3 picture."},
    {kSync, kSetting, "Sync",
     "Stops tearing. V-Sync waits for the monitor with every frame. G-Sync / FreeSync is for monitors with a variable "
     "refresh rate: frames show the moment they are ready."},
    {kFrameLimit, kSubSetting, "Frame Rate Limit",
     "The most frames drawn each second. A limit your computer always reaches gives the smoothest motion."},
    {kFpsCounter, kSetting, "Frame Rate Counter", "Shows frames per second and frame times in the corner (F11)."},
    {kHeadingQuality, kHeading, "QUALITY", ""},
    {kPreset, kSetting, "Quality Preset", "Sets every quality setting below at once. Custom: they were set one by one."},
    {kAntiAliasing, kSetting, "Anti-Aliasing",
     "Smooths jagged edges. FXAA is nearly free and softens slightly. Supersampling draws more pixels than the screen "
     "has: the sharpest and by far the most demanding."},
    {kFxaaQuality, kSubSetting, "FXAA Quality", "How far along an edge FXAA looks: higher smooths long, shallow edges better."},
    {kSupersampling, kSubSetting, "Supersampling",
     "The size the game is drawn at before it is scaled down to the screen. 200% is four times the pixels."},
    {kTextureFiltering, kSetting, "Texture Filtering",
     "How surfaces look at a distance and at a slant. Anisotropic keeps the road sharp far ahead."},
    {kAnisotropy, kSubSetting, "Anisotropy", "Samples taken for slanted surfaces. 16x is sharpest; the cost is small."},
    {kWorldDetail, kSetting, "World Detail", "How far away the world keeps its full detail."},
    {kCarDetail, kSetting, "Car Detail", "The detail of the cars' models."},
    {kShadows, kSetting, "Shadows", "The sun's and the moon's shadows."},
    {kDynamicShadows, kSubSetting, "Headlight Shadows", "Your headlights cast shadows at night."},
    {kSurfaceDetail, kSetting, "Surface Detail",
     "Cobbles, bricks, slabs and rock get real depth, with their own shadows. Looks only: driving is unchanged."},
    {kRoadWear, kSubSetting, "Road Wear",
     "Uneven asphalt, patches, sealed cracks, potholes, manhole covers and stains on the roads."},
    {kCarReflections, kSetting, "Car Reflections",
     "The world mirrored in the cars' paint. Higher settings redraw the reflection more often."},
    {kCarReflectionDetail, kSubSetting, "Reflected Detail", "How much of the world is drawn into the reflection."},
    {kCarReflectionSize, kSubSetting, "Resolution", "The size of each side of the reflection. Higher is sharper."},
    {kRoadReflections, kSetting, "Road Reflections", "Reflections on wet roads and in puddles."},
    {kRoadReflectionSize, kSubSetting, "Resolution", "The reflection's size as a share of the picture's."},
    {kHeadingEffects, kHeading, "EFFECTS", ""},
    {kRain, kSetting, "Rain", "Falling rain, splashes and drops on the screen. Roads still get wet without it."},
    {kBloom, kSetting, "Bloom", "Bright lights and surfaces glow (Over Bright)."},
    {kMotionBlur, kSetting, "Motion Blur", "Blurs the picture's edges with speed."},
    {kTreatment, kSetting, "Visual Treatment",
     "The game's colour grade: the warm tint, contrast and vignette. Off shows the world's own colours."},
    {kLightGlow, kSetting, "Light Glow", "Street lamps and headlights glow in the air at night, more in fog and rain."},
};
const int kRowCount = sizeof(kRows) / sizeof(kRows[0]);
static_assert(kRowCount == kOptionCount, "one row for each option, in order");

/** What the page edits. */
struct Settings
{
    nfs::display::Settings display;
    int refreshRate;
    bool widescreen;
    Sync sync;
    int frameLimit;
    bool fpsCounter;
    AntiAliasing antiAliasing;
    int fxaaQuality;
    int supersampling;
    int textureFiltering;
    int anisotropy;
    int worldDetail;
    int carDetail;
    int shadows;
    bool dynamicShadows;
    int surfaceDetail;
    bool roadWear;
    int carReflections;
    int carReflectionDetail;
    int carReflectionSize;
    int roadReflections;
    int roadReflectionPercent;
    int rain;
    int bloom;
    /** Percent of the original's speed blur and of the treatment's look; 0 is off. */
    int motionBlur;
    int treatment;
    bool lightGlow;
    /** The preset last chosen: stepping from "Custom" starts there. */
    int performanceLevel;
};

bool Same(const Settings &a, const Settings &b)
{
    return a.display == b.display && a.refreshRate == b.refreshRate && a.widescreen == b.widescreen &&
           a.sync == b.sync && a.frameLimit == b.frameLimit && a.fpsCounter == b.fpsCounter &&
           a.antiAliasing == b.antiAliasing && a.fxaaQuality == b.fxaaQuality && a.supersampling == b.supersampling &&
           a.textureFiltering == b.textureFiltering && a.anisotropy == b.anisotropy && a.worldDetail == b.worldDetail &&
           a.carDetail == b.carDetail && a.shadows == b.shadows && a.dynamicShadows == b.dynamicShadows &&
           a.surfaceDetail == b.surfaceDetail && a.roadWear == b.roadWear &&
           a.carReflections == b.carReflections && a.carReflectionDetail == b.carReflectionDetail &&
           a.carReflectionSize == b.carReflectionSize && a.roadReflections == b.roadReflections &&
           a.roadReflectionPercent == b.roadReflectionPercent && a.rain == b.rain && a.bloom == b.bloom &&
           a.motionBlur == b.motionBlur && a.treatment == b.treatment && a.lightGlow == b.lightGlow;
}

/** A change the monitor may not be able to show: it is kept only when the player says so within ten seconds. */
bool SameDisplay(const Settings &a, const Settings &b)
{
    return a.display == b.display && a.refreshRate == b.refreshRate;
}

/** Row objects for every row the page can show. */
const int kMaxRows = kRowCount;
const float kSubSettingIndent = 14.0f;
/** The description under the rows: its name in the package, its text's scale against a value's, its place in the room
 * under the rows, and how bright it is against a value. */
const char kDescriptionName[] = "VIDEO_DESCRIPTION";
const float kDescriptionScale = 0.74f;
const float kDescriptionTop = 9.0f;
const int kDescriptionShade = 78;
const unsigned int kDescriptionRoom = 320;
/** How bright a value that cannot be changed is against one that can, in percent. */
const int kDisabledShade = 45;
/** The scroll bar: the room its arrows take at the ends of the rows. */
const float kScrollArrowRoom = 12.0f;
/** A click this far left of the value column's middle still counts as on the value. */
const float kValueHalfWidth = 115.0f;

const int kMotionBlurLevels[] = {0, 35, 70, 100, 135};
const int kMotionBlurLevelCount = sizeof(kMotionBlurLevels) / sizeof(kMotionBlurLevels[0]);
const int kReflectionSizes[] = {256, 512, 1024, 2048, 4096};
const int kRoadReflectionPercents[] = {25, 50, 75, 100};
const int kAnisotropies[] = {2, 4, 8, 16};

struct Page
{
    /** What the page shows, what was last kept, and what is in use. */
    Settings pending;
    Settings original;
    Settings live;
    Metrics metrics;
    /** False with a package the layout does not know: the rows keep the package's own places. */
    bool laidOut;
    /** Rows came or went: the list is built again after the message that caused it. */
    bool rebuild;
    /** A value changed: the rows are drawn again after the message (a preset changes many). */
    bool redraw;
    FEString *description;
    /** The value text's own colour in this package (red, green, blue, alpha). */
    int valueColour[4];
    bool haveValueColour;
    /** What the scroll bar last showed. */
    int scrollTop;
    int scrollTotal;
};

Page g_page;

int Clamp(int value, int lowest, int highest)
{
    return value < lowest ? lowest : value > highest ? highest : value;
}

FEObject *AsObject(FEString *string)
{
    return reinterpret_cast<FEObject *>(string);
}

OptionsSettings &ProfileOptions()
{
    return FEDatabase->CurrentUserProfile->TheOptionsSettings;
}

/** The next entry of an ascending list after the one `value` stands at (step 1) or before it (step -1), within the
 * list. */
template <int N> int StepIn(const int (&list)[N], int value, int step)
{
    int index = 0;
    for (int i = 0; i < N; ++i)
        if (list[i] <= value)
            index = i;
    return list[Clamp(index + step, 0, N - 1)];
}

/** The game's video options with the page's settings in them. */
VideoOptions ToOptions(const Settings &s)
{
    VideoOptions v = g_VideoOptions;
    v.CarEnvironmentMapEnable = s.carReflections;
    v.CarEnvironmentMapUpdateData = s.carReflectionDetail;
    v.RoadReflectionEnable = s.roadReflections;
    v.WorldLodLevel = s.worldDetail;
    v.CarLodLevel = s.carDetail;
    v.OverBrightEnable = s.bloom;
    v.RainEnable = s.rain;
    v.TextureFiltering = s.textureFiltering;
    v.ShadowDetail = s.shadows;
    v.VisualTreatment = s.treatment > 0 ? g_VideoOptionLimits.VisualTreatment : 0;
    v.MotionBlurEnable = s.motionBlur > 0 ? g_VideoOptionLimits.MotionBlurEnable : 0;
    v.VSyncOn = s.sync != Sync::Off ? 1 : 0;
    return v;
}

/** The page's quality settings from the game's video options (a preset's, or the ones in use). */
void QualityFrom(Settings &s, const VideoOptions &v)
{
    s.carReflections = v.CarEnvironmentMapEnable;
    s.carReflectionDetail = v.CarEnvironmentMapUpdateData;
    s.roadReflections = v.RoadReflectionEnable;
    s.worldDetail = v.WorldLodLevel;
    s.carDetail = v.CarLodLevel;
    s.bloom = v.OverBrightEnable;
    s.rain = v.RainEnable;
    s.textureFiltering = v.TextureFiltering;
    s.shadows = v.ShadowDetail;
    if (!v.VisualTreatment)
        s.treatment = 0;
    else if (s.treatment <= 0)
        s.treatment = 100;
}

Settings InUse()
{
    Settings s = {};
    s.display = nfs::display::Current();
    s.refreshRate = nfs::display::ConfiguredRefreshRate();
    s.widescreen = ProfileOptions().TheVideoSettings.WideScreen;
    s.sync = nfs::timing::SyncMode();
    s.frameLimit = nfs::timing::FitLimit(s.sync, nfs::timing::Limit());
    s.fpsCounter = nfs::overlay::Shown();
    s.antiAliasing = nfs::display::AntiAliasingMode();
    s.fxaaQuality = nfs::display::FxaaQuality();
    s.supersampling = nfs::display::SupersamplingScale();
    s.anisotropy = nfs::display::Anisotropy();
    s.dynamicShadows = nfs::display::DynamicShadows();
    s.surfaceDetail = nfs::surface::DetailLevel();
    s.roadWear = nfs::surface::RoadWear();
    s.carReflectionSize = nfs::display::ReflectionMapSize();
    s.roadReflectionPercent = nfs::display::RoadReflectionPercent();
    s.motionBlur = g_VideoOptions.MotionBlurEnable ? nfs::display::MotionBlur() : 0;
    s.treatment = g_VideoOptions.VisualTreatment ? nfs::display::VisualTreatment() : 0;
    s.lightGlow = nfs::lamps::HazeScale() > 0.0f;
    s.performanceLevel = g_PerformanceLevel;
    QualityFrom(s, g_VideoOptions);
    return s;
}

/** What "restore defaults" fills the page with: the highest quality the device offers, borderless at the monitor's
 * resolution, no sync and the monitor's rate as the limit, no anti-aliasing. */
Settings DefaultSettings()
{
    Settings s = InUse();
    s.display = nfs::display::Defaults();
    s.refreshRate = 0;
    s.widescreen = nfs::display::IsWideMonitor();
    s.sync = Sync::Off;
    s.frameLimit = nfs::timing::kMonitorRate;
    s.fpsCounter = false;
    s.antiAliasing = AntiAliasing::Off;
    s.fxaaQuality = nfs::display::kFxaaQualities - 1;
    s.supersampling = 150;
    s.anisotropy = 16;
    s.dynamicShadows = true;
    s.surfaceDetail = nfs::surface::kHigh;
    s.roadWear = true;
    s.carReflectionSize = 1024;
    s.roadReflectionPercent = 50;
    s.motionBlur = g_VideoOptionLimits.MotionBlurEnable ? 100 : 0;
    s.treatment = 100;
    s.lightGlow = true;
    s.performanceLevel = nfs::video::kHighestPreset;
    QualityFrom(s, nfs::video::Preset(nfs::video::kHighestPreset, ToOptions(s)));
    return s;
}

/** Puts `s` in use and saves it. Display mode, sizes and the swap interval take effect at the device reset this asks
 * for; the rest on the next frame. */
void PutInUse(const Settings &s)
{
    g_VideoOptions = ToOptions(s);
    g_PerformanceLevel = s.performanceLevel;
    if (!nfs::video::TestOptions())
        SaveVideoOptions();
    ProfileOptions().TheVideoSettings.WideScreen = s.widescreen;
    const nfs::display::Settings display = nfs::display::Fit(s.display, s.widescreen);
    if (display != nfs::display::Current())
        nfs::display::Apply(display);
    if (s.refreshRate != nfs::display::ConfiguredRefreshRate())
        nfs::display::SetConfiguredRefreshRate(s.refreshRate);
    if (s.sync != nfs::timing::SyncMode())
        nfs::timing::SetSyncMode(s.sync);
    if (s.frameLimit != nfs::timing::Limit())
        nfs::timing::SetLimit(s.frameLimit);
    if (s.fpsCounter != nfs::overlay::Shown())
        nfs::overlay::SetShown(s.fpsCounter);
    if (s.supersampling != nfs::display::SupersamplingScale())
        nfs::display::SetSupersamplingScale(s.supersampling);
    if (s.antiAliasing != nfs::display::AntiAliasingMode() ||
        (nfs::display::HasSsaa(s.antiAliasing) && nfs::display::RenderScale() != s.supersampling))
        nfs::display::SetAntiAliasing(s.antiAliasing);
    if (s.fxaaQuality != nfs::display::FxaaQuality())
        nfs::display::SetFxaaQuality(s.fxaaQuality);
    if (s.anisotropy != nfs::display::Anisotropy())
        nfs::display::SetAnisotropy(s.anisotropy);
    if (s.dynamicShadows != nfs::display::DynamicShadows())
        nfs::display::SetDynamicShadows(s.dynamicShadows);
    if (s.surfaceDetail != nfs::surface::DetailLevel())
        nfs::surface::SetDetailLevel(s.surfaceDetail);
    if (s.roadWear != nfs::surface::RoadWear())
        nfs::surface::SetRoadWear(s.roadWear);
    if (s.carReflectionSize != nfs::display::ReflectionMapSize())
        nfs::display::SetReflectionMapSize(s.carReflectionSize);
    if (s.roadReflectionPercent != nfs::display::RoadReflectionPercent())
        nfs::display::SetRoadReflectionPercent(s.roadReflectionPercent);
    if (s.motionBlur > 0 && s.motionBlur != nfs::display::MotionBlur())
        nfs::display::SetMotionBlur(s.motionBlur);
    if (s.treatment > 0 && s.treatment != nfs::display::VisualTreatment())
        nfs::display::SetVisualTreatment(s.treatment);
    if (s.lightGlow != (nfs::lamps::HazeScale() > 0.0f))
        nfs::lamps::SetHazeScale(s.lightGlow ? 1.0f : 0.0f);
    g_VideoOptionsChanged = true;
    g_page.live = s;
}

/** Whether the row shows with these settings. */
bool Visible(Option id, const Settings &s)
{
    switch (id)
    {
    case kResolution:
        return s.display.mode != nfs::display::Mode::Borderless;
    case kRefreshRate:
        return nfs::display::RefreshRateSelectable(s.display.mode);
    case kFrameLimit:
        return s.sync != Sync::VSync;
    case kFxaaQuality:
        return nfs::display::HasFxaa(s.antiAliasing);
    case kSupersampling:
        return nfs::display::HasSsaa(s.antiAliasing);
    case kAnisotropy:
        return s.textureFiltering >= 2;
    case kDynamicShadows:
        return s.shadows > 0;
    case kRoadWear:
        return s.surfaceDetail > 0;
    case kCarReflectionDetail:
    case kCarReflectionSize:
        return s.carReflections > 0;
    case kRoadReflectionSize:
        return s.roadReflections > 0;
    default:
        return true;
    }
}

/** The highest level of a setting the device offers (DetectVideoOptionLimits), -1 for a setting without one. */
int Limit(Option id)
{
    const VideoOptions &limits = g_VideoOptionLimits;
    switch (id)
    {
    case kTextureFiltering:
        return limits.TextureFiltering;
    case kWorldDetail:
        return limits.WorldLodLevel;
    case kCarDetail:
        return limits.CarLodLevel;
    case kShadows:
        return limits.ShadowDetail;
    case kCarReflections:
        return limits.CarEnvironmentMapEnable;
    case kCarReflectionDetail:
        return limits.CarEnvironmentMapUpdateData;
    case kRoadReflections:
        return limits.RoadReflectionEnable;
    case kRain:
        return limits.RainEnable;
    case kBloom:
        return limits.OverBrightEnable;
    case kMotionBlur:
        return limits.MotionBlurEnable;
    case kTreatment:
        return limits.VisualTreatment;
    default:
        return -1;
    }
}

/** A setting the device cannot do at all shows greyed and is passed over. */
bool Available(Option id)
{
    if (id == kAntiAliasing || id == kFxaaQuality || id == kLightGlow || id == kDynamicShadows || id == kSurfaceDetail ||
        id == kRoadWear)
        return nfs::vk::g_renderer != nullptr || id == kAntiAliasing;
    return Limit(id) != 0;
}

int &LevelOf(Option id, Settings &s)
{
    switch (id)
    {
    case kTextureFiltering:
        return s.textureFiltering;
    case kWorldDetail:
        return s.worldDetail;
    case kCarDetail:
        return s.carDetail;
    case kShadows:
        return s.shadows;
    case kCarReflections:
        return s.carReflections;
    case kCarReflectionDetail:
        return s.carReflectionDetail;
    case kRoadReflections:
        return s.roadReflections;
    case kRain:
        return s.rain;
    default:
        return s.bloom;
    }
}

/** One step left (-1) or right (1) on a row. */
void Step(Option id, Settings &s, int step)
{
    switch (id)
    {
    case kDisplayMode:
    {
        // A new mode starts from the monitor's resolution (the largest window that fits, for windowed).
        const int modes = 3;
        nfs::display::Settings next = nfs::display::Defaults();
        next.mode = static_cast<nfs::display::Mode>((static_cast<int>(s.display.mode) + step + modes) % modes);
        s.display = nfs::display::Fit(next, s.widescreen);
        break;
    }
    case kResolution:
        s.display = nfs::display::StepResolution(s.display, step, s.widescreen);
        break;
    case kRefreshRate:
        s.refreshRate = nfs::display::StepRefreshRate(s.display.width, s.display.height, s.refreshRate, step);
        break;
    case kWidescreen:
        s.widescreen = !s.widescreen;
        s.display = nfs::display::Fit(s.display, s.widescreen);
        break;
    case kSync:
        s.sync = static_cast<Sync>((static_cast<int>(s.sync) + step + 3) % 3);
        s.frameLimit = nfs::timing::FitLimit(s.sync, s.frameLimit);
        break;
    case kFrameLimit:
        s.frameLimit = nfs::timing::StepLimitFor(s.sync, s.frameLimit, step);
        break;
    case kFpsCounter:
        s.fpsCounter = !s.fpsCounter;
        break;
    case kPreset:
    {
        // From "Custom" it steps from the last preset chosen.
        const int current = nfs::video::MatchingPreset(ToOptions(s));
        const int level = Clamp((current ? current : s.performanceLevel) + step, nfs::video::kLowestPreset,
                                nfs::video::kHighestPreset);
        QualityFrom(s, nfs::video::Preset(level, ToOptions(s)));
        s.performanceLevel = level;
        break;
    }
    case kAntiAliasing:
    {
        // Without the Vulkan renderer there is no FXAA: off or supersampling.
        const bool fxaa = nfs::vk::g_renderer != nullptr;
        int mode = static_cast<int>(s.antiAliasing);
        do
            mode = (mode + step + 4) % 4;
        while (!fxaa && nfs::display::HasFxaa(static_cast<AntiAliasing>(mode)));
        s.antiAliasing = static_cast<AntiAliasing>(mode);
        break;
    }
    case kFxaaQuality:
        s.fxaaQuality = Clamp(s.fxaaQuality + step, 0, nfs::display::kFxaaQualities - 1);
        break;
    case kSupersampling:
        s.supersampling = nfs::display::StepSupersampling(s.supersampling, step);
        break;
    case kAnisotropy:
        s.anisotropy = StepIn(kAnisotropies, s.anisotropy, step);
        break;
    case kDynamicShadows:
        s.dynamicShadows = !s.dynamicShadows;
        break;
    case kSurfaceDetail:
        s.surfaceDetail = (s.surfaceDetail + nfs::surface::kLevels + (step < 0 ? -1 : 1)) % nfs::surface::kLevels;
        break;
    case kRoadWear:
        s.roadWear = !s.roadWear;
        break;
    case kCarReflectionSize:
        s.carReflectionSize = StepIn(kReflectionSizes, s.carReflectionSize, step);
        break;
    case kRoadReflectionSize:
        s.roadReflectionPercent = StepIn(kRoadReflectionPercents, s.roadReflectionPercent, step);
        break;
    case kMotionBlur:
        s.motionBlur = StepIn(kMotionBlurLevels, s.motionBlur, step);
        break;
    case kTreatment:
        s.treatment = StepIn(nfs::display::kVisualTreatmentLevels, s.treatment, step);
        break;
    case kLightGlow:
        s.lightGlow = !s.lightGlow;
        break;
    case kRain:
    case kBloom:
    {
        // On or off.
        int &level = LevelOf(id, s);
        level = level ? 0 : Limit(id);
        break;
    }
    default:
    {
        int &level = LevelOf(id, s);
        level = Clamp(level + step, 0, Limit(id));
        break;
    }
    }
}

const char *OnOff(bool on)
{
    return on ? "On" : "Off";
}

/** The name of level `value` of a setting with `levels` levels above "Off" (or above nothing, without `hasOff`). */
const char *LevelName(int value, int levels, bool hasOff)
{
    static const char *const kNames[4][4] = {
        {"On"},
        {"Low", "High"},
        {"Low", "Medium", "High"},
        {"Low", "Medium", "High", "Ultra"},
    };
    if (hasOff && value <= 0)
        return "Off";
    const int level = Clamp(hasOff ? value - 1 : value, 0, levels - 1);
    return kNames[Clamp(levels, 1, 4) - 1][level];
}

void ValueText(Option id, const Settings &s, char *out, int size)
{
    const char *text = nullptr;
    switch (id)
    {
    case kDisplayMode:
        text = nfs::display::ModeName(s.display.mode);
        break;
    case kResolution:
        bSNPrintf(out, size, "%d x %d", s.display.width, s.display.height);
        return;
    case kRefreshRate:
        if (s.refreshRate > 0)
            bSNPrintf(out, size, "%d Hz", s.refreshRate);
        else
            text = "Highest";
        break;
    case kWidescreen:
        text = OnOff(s.widescreen);
        break;
    case kSync:
        text = s.sync == Sync::Vrr ? "G-Sync / FreeSync" : s.sync == Sync::VSync ? "V-Sync" : "Off";
        break;
    case kFrameLimit:
        if (s.frameLimit == nfs::timing::kUncapped)
            text = "Unlimited";
        else if (s.frameLimit == nfs::timing::kMonitorRate)
            bSNPrintf(out, size, "Monitor (%d FPS)", nfs::display::RefreshRate());
        else if (s.frameLimit == nfs::timing::kVrrRate)
            bSNPrintf(out, size, "Automatic (%d FPS)", nfs::timing::VrrRate());
        else
            bSNPrintf(out, size, "%d FPS", s.frameLimit);
        break;
    case kFpsCounter:
        text = OnOff(s.fpsCounter);
        break;
    case kPreset:
    {
        static const char *const kPresets[] = {"Custom", "Very Low", "Low", "Medium", "High", "Ultra"};
        text = kPresets[Clamp(nfs::video::MatchingPreset(ToOptions(s)), 0, 5)];
        break;
    }
    case kAntiAliasing:
    {
        static const char *const kModes[] = {"Off", "FXAA", "Supersampling", "FXAA + SSAA"};
        text = kModes[Clamp(static_cast<int>(s.antiAliasing), 0, 3)];
        break;
    }
    case kFxaaQuality:
        text = LevelName(s.fxaaQuality, nfs::display::kFxaaQualities, false);
        break;
    case kSupersampling:
        bSNPrintf(out, size, "%d%%", s.supersampling);
        return;
    case kTextureFiltering:
    {
        static const char *const kFilters[] = {"Bilinear", "Trilinear", "Anisotropic"};
        text = kFilters[Clamp(s.textureFiltering, 0, 2)];
        break;
    }
    case kAnisotropy:
        bSNPrintf(out, size, "%dx", s.anisotropy);
        return;
    case kWorldDetail:
        text = LevelName(s.worldDetail, 4, false);
        break;
    case kCarDetail:
        text = LevelName(s.carDetail, 2, false);
        break;
    case kShadows:
        text = LevelName(s.shadows, 2, true);
        break;
    case kDynamicShadows:
        text = OnOff(s.dynamicShadows);
        break;
    case kSurfaceDetail:
    {
        static const char *const kNames[nfs::surface::kLevels] = {"Off", "Low", "Medium", "High"};
        text = kNames[s.surfaceDetail];
        break;
    }
    case kRoadWear:
        text = OnOff(s.roadWear);
        break;
    case kCarReflections:
        text = LevelName(s.carReflections, 3, true);
        break;
    case kCarReflectionDetail:
        text = LevelName(s.carReflectionDetail, 2, false);
        break;
    case kCarReflectionSize:
        bSNPrintf(out, size, "%d x %d", s.carReflectionSize, s.carReflectionSize);
        return;
    case kRoadReflections:
        text = LevelName(s.roadReflections, 3, true);
        break;
    case kRoadReflectionSize:
        bSNPrintf(out, size, "%d%%", s.roadReflectionPercent);
        return;
    case kRain:
        text = OnOff(s.rain != 0);
        break;
    case kBloom:
        text = OnOff(s.bloom != 0);
        break;
    case kMotionBlur:
    {
        text = nullptr;
        for (int i = 0; i < kMotionBlurLevelCount; ++i)
            if (kMotionBlurLevels[i] == s.motionBlur)
                text = LevelName(i, kMotionBlurLevelCount - 1, true);
        if (!text)
        {
            // A percentage from speed.ini between two levels.
            bSNPrintf(out, size, "%d%%", s.motionBlur);
            return;
        }
        break;
    }
    case kTreatment:
        if (s.treatment > 0)
        {
            bSNPrintf(out, size, "%d%%", s.treatment);
            return;
        }
        text = "Off";
        break;
    case kLightGlow:
        text = OnOff(s.lightGlow);
        break;
    default:
        text = "";
        break;
    }
    if (text)
        bSNPrintf(out, size, "%s", text);
}

/** Which rows show, a bit each. */
unsigned long long VisibleRows(const Settings &s)
{
    unsigned long long rows = 0;
    for (int i = 0; i < kRowCount; ++i)
        if (Visible(kRows[i].id, s))
            rows |= 1ull << i;
    return rows;
}
static_assert(kRowCount <= 64, "a bit for each row");

void ShowDescription(Option id)
{
    if (g_page.description)
        FEPrintf(g_page.description, "%s", kRows[id].description);
}

void RedrawRows(UIWidgetMenu *menu)
{
    const int rows = static_cast<int>(menu->iIndexToAdd) - 1;
    for (int i = 1; i <= rows; ++i)
        menu->GetWidget(i)->Draw();
}

/** A row of the page: a heading or a setting. */
class Row : public FEToggleWidget
{
  public:
    Row(Option id, bool enabled) : FEToggleWidget(enabled), Id(id) {}

    /** FEWidget's own Draw is pure in the original (it has no code to bind to): the rows' classes draw. */
    void Draw() override {}

    /** The row's two strings at their places: the name on the left (`indent` in), the value in the value column. */
    void Place(float indent, unsigned int valueFormat, float valueX, const int colour[3], int shade)
    {
        const Metrics &m = g_page.metrics;
        const float y = vTopLeft.y + m.rowHeight * 0.5f;
        nfs::feng::SetTextFormat(pTitle, nfs::feng::kTextMiddle);
        nfs::feng::SetPosition(AsObject(pTitle), m.left + indent, y);
        nfs::feng::SetTextFormat(pData, valueFormat | nfs::feng::kTextMiddle);
        nfs::feng::SetPosition(AsObject(pData), valueX, y);
        nfs::feng::SetColour(AsObject(pData), colour[0] * shade / 100, colour[1] * shade / 100, colour[2] * shade / 100,
                             g_page.valueColour[3]);
    }

    const Option Id;
};

class HeadingRow : public Row
{
  public:
    explicit HeadingRow(Option id) : Row(id, false) {}

    void Draw() override
    {
        // The heading is the row's value string, on the left: its colour is the page's to set (the names' scripts
        // colour them).
        FEPrintf(pTitle, "");
        FEPrintf(pData, "%s", kRows[Id].title);
    }

    void Position() override
    {
        if (!g_page.laidOut)
        {
            FEToggleWidget::Position();
            return;
        }
        Place(0.0f, 0, g_page.metrics.left, g_page.metrics.accent, 100);
    }

    /** Passed over like a greyed setting, without its grey. */
    void Disable() override
    {
        bEnabled = false;
    }
};

class SettingRow : public Row
{
  public:
    explicit SettingRow(Option id) : Row(id, Available(id)) {}

    void Act(const char *name, unsigned int action) override
    {
        if (action == kActionLeft || action == kActionRight)
            Change(action == kActionRight ? 1 : -1);
        bMovedLastUpdate = true;
        BlinkArrows(action);
    }

    /** A click on the selected row: left or right of the value's middle steps it that way. */
    void CheckMouse(const char *name, float x, float y) override
    {
        if (g_page.laidOut && x >= g_page.metrics.valueX - kValueHalfWidth)
            Change(x < g_page.metrics.valueX ? -1 : 1);
    }

    void Draw() override
    {
        char value[64];
        ValueText(Id, g_page.pending, value, sizeof(value));
        FEPrintf(pTitle, "%s", kRows[Id].title);
        FEPrintf(pData, "%s", value);
    }

    void Position() override
    {
        if (!g_page.laidOut)
        {
            FEToggleWidget::Position();
            return;
        }
        Place(kRows[Id].kind == kSubSetting ? kSubSettingIndent : 0.0f, nfs::feng::kTextCentre, g_page.metrics.valueX,
              g_page.valueColour, bEnabled ? 100 : kDisabledShade);
    }

    void SetFocus(const char *name) override
    {
        FEToggleWidget::SetFocus(name);
        ShowDescription(Id);
    }

  private:
    void Change(int step)
    {
        const unsigned long long before = VisibleRows(g_page.pending);
        Step(Id, g_page.pending, step);
        // The list is built again after this message (the row is in the middle of handling it).
        g_page.redraw = true;
        if (VisibleRows(g_page.pending) != before)
            g_page.rebuild = true;
    }
};

Row *RowAt(UIWidgetMenu *menu, int index)
{
    return static_cast<Row *>(static_cast<FEToggleWidget *>(menu->GetWidget(index)));
}

int RowsShown(UIWidgetMenu *menu)
{
    return static_cast<int>(menu->iIndexToAdd) - 1;
}

/** The row with this option in the list (1 the first), 0 when it does not show. */
int IndexOf(UIWidgetMenu *menu, int id)
{
    const int rows = RowsShown(menu);
    for (int i = 1; i <= rows; ++i)
        if (RowAt(menu, i)->Id == id)
            return i;
    return 0;
}

void ShowObject(FEObject *object, bool shown)
{
    if (!object)
        return;
    if (shown)
        FEngSetVisible(object);
    else
        FEngSetInvisible(object);
    FEngSetScript(object, shown ? kScriptShown : kScriptHidden, true);
}

/** The scroll bar beside the rows: its handle as large and as far down as the rows in view are of all the rows. */
void UpdateScrollBar(UIWidgetMenu *menu)
{
    const Metrics &m = g_page.metrics;
    if (!g_page.laidOut || !m.scrollTrack || !m.scrollHandle)
        return;
    const int total = RowsShown(menu), visible = m.visibleRows;
    const int top = menu->pViewTop ? static_cast<int>(menu->GetWidgetIndex(menu->pViewTop)) : 1;
    if (top == g_page.scrollTop && total == g_page.scrollTotal)
        return;
    g_page.scrollTop = top;
    g_page.scrollTotal = total;
    const bool needed = total > visible;
    ShowObject(m.scrollTrack, needed);
    ShowObject(m.scrollHandle, needed);
    ShowObject(m.scrollArrows[0], needed);
    ShowObject(m.scrollArrows[1], needed);
    if (!needed)
        return;
    // On the screen, then in the scroll bar's group.
    const float rowsTop = m.top + m.groupY, rowsBottom = rowsTop + static_cast<float>(visible) * m.rowHeight;
    const float trackTop = rowsTop + kScrollArrowRoom, trackHeight = rowsBottom - rowsTop - 2.0f * kScrollArrowRoom;
    const float handleHeight = trackHeight * static_cast<float>(visible) / static_cast<float>(total);
    const float travel = (trackHeight - handleHeight) * static_cast<float>(top - 1) / static_cast<float>(total - visible);
    nfs::feng::StretchImageY(m.scrollTrack, trackTop + trackHeight * 0.5f - m.scrollParentY, trackHeight);
    nfs::feng::StretchImageY(m.scrollHandle, trackTop + travel + handleHeight * 0.5f - m.scrollParentY, handleHeight);
    nfs::feng::SetTrack(m.scrollArrows[0], nfs::feng::kPosition, 1, rowsTop + kScrollArrowRoom * 0.5f - m.scrollParentY);
    nfs::feng::SetTrack(m.scrollArrows[1], nfs::feng::kPosition, 1,
                        rowsBottom - kScrollArrowRoom * 0.5f - m.scrollParentY);
}

/**
 * Puts row `top` at the top of the view, as near as the list and the selection allow: the selected row stays in view,
 * and a heading right above it shows with it. `force` moves the rows even when the view's top stays the same row
 * (after the list was built again).
 */
void SetView(UIWidgetMenu *menu, int top, bool force)
{
    const int total = RowsShown(menu), visible = static_cast<int>(menu->iMaxWidgetsOnScreen);
    if (total < 1)
        return;
    const int current = menu->pCurrentOption ? static_cast<int>(menu->GetWidgetIndex(menu->pCurrentOption)) : 0;
    if (current)
    {
        if (current > 1 && current <= top && kRows[RowAt(menu, current - 1)->Id].kind == kHeading)
            top = current - 1;
        else if (current < top)
            top = current;
        if (current > top + visible - 1)
            top = current - visible + 1;
    }
    top = Clamp(top, 1, total > visible ? total - visible + 1 : 1);
    FEWidget *widget = menu->GetWidget(top);
    if (widget != menu->pViewTop || force)
    {
        menu->pViewTop = widget;
        menu->Reposition();
    }
    menu->bViewNeedsSync = false;
    UpdateScrollBar(menu);
}

/**
 * Shows the cursor's brackets and arrows. In the main menu's package they start hidden and only the first row's name
 * shows them, when it is first highlighted; the page's first row is a heading, which never is.
 */
void ShowCursor(UIWidgetMenu *menu)
{
    if (!menu->pCursor)
        return;
    FEObject *inner = reinterpret_cast<FEGroup *>(menu->pCursor)->FindChildRecursive(kCursorInner);
    if (inner && inner->FindScript(kScriptShown))
        FEngSetScript(inner, kScriptShown, true);
}

/** The description's string: a copy of a value string in the room under the rows, its lines broken at the rows' width. */
void ProvideDescription(UIWidgetMenu *menu)
{
    Page &p = g_page;
    p.description = nullptr;
    if (!p.laidOut)
        return;
    const Metrics &m = p.metrics;
    const unsigned int name = FEHashUpper(kDescriptionName);
    FEString *first = FEngFindString(menu->PackageName, static_cast<int>(FEHashUpper("OPTION_DATA_1")));
    if (!p.haveValueColour)
    {
        p.valueColour[0] = p.valueColour[1] = p.valueColour[2] = p.valueColour[3] = 255;
        if (first)
            nfs::feng::GetColour(AsObject(first), p.valueColour);
        p.haveValueColour = true;
    }
    FEString *string = FEngFindString(menu->PackageName, static_cast<int>(name));
    if (!string && first)
        string = reinterpret_cast<FEString *>(nfs::feng::Clone(AsObject(first), m.group, name));
    if (!string)
        return;
    FEObject *object = AsObject(string);
    nfs::feng::Reserve(string, kDescriptionRoom);
    const float scale = m.valueScale * kDescriptionScale;
    FEngSetScaleX(object, scale);
    FEngSetScaleY(object, scale);
    nfs::feng::SetTextFormat(string, nfs::feng::kTextWrap, static_cast<unsigned int>((m.right - m.left) / scale));
    nfs::feng::SetPosition(object, m.left, m.belowTop + kDescriptionTop);
    nfs::feng::SetColour(object, p.valueColour[0] * kDescriptionShade / 100, p.valueColour[1] * kDescriptionShade / 100,
                         p.valueColour[2] * kDescriptionShade / 100, p.valueColour[3]);
    FEngSetVisible(object);
    FEPrintf(string, "");
    p.description = string;
}

/** Fills the cleared menu with the rows that show now. */
void AddRows(UIOptionsScreen *screen)
{
    Page &p = g_page;
    // The page keeps the scroll bar itself (UpdateScrollBar): the menu's own counts on the package's layout.
    screen->bHasScrollBar = false;
    const int strings = nfs::optionsrows::ProvideRowStrings(screen, kMaxRows);
    p.laidOut = nfs::optionsrows::Pack(screen, strings, true, &p.metrics);
    ProvideDescription(screen);
    p.scrollTop = p.scrollTotal = -1;
    int added = 0;
    for (int i = 0; i < kRowCount && added < strings; ++i)
    {
        const RowInfo &info = kRows[i];
        if (!Visible(info.id, p.pending))
            continue;
        Row *row = info.kind == kHeading ? static_cast<Row *>(new HeadingRow(info.id))
                                         : static_cast<Row *>(new SettingRow(info.id));
        screen->AddToggleOption(row, true);
        ++added;
        // The highlighted row's name pulses instead of blinking away.
        nfs::feng::LimitScriptFade(AsObject(row->pTitle), kScriptHighlight, kHighlightLeastAlpha);
        if (p.laidOut)
        {
            // What a click must hit: the whole row.
            row->vTopLeft.x = p.metrics.left;
            row->vSize.x = p.metrics.right - p.metrics.left;
            row->vSize.y = p.metrics.rowHeight;
            row->Position();
        }
    }
}

/** Builds the list again after rows came or went, around the row the player is on. */
void Rebuild(UIOptionsScreen *screen)
{
    const int rows = RowsShown(screen);
    const int current = screen->pCurrentOption ? static_cast<int>(screen->GetWidgetIndex(screen->pCurrentOption)) : 0;
    const int top = screen->pViewTop ? static_cast<int>(screen->GetWidgetIndex(screen->pViewTop)) : 1;
    const int currentId = current >= 1 && current <= rows ? RowAt(screen, current)->Id : -1;
    const int topId = top >= 1 && top <= rows ? RowAt(screen, top)->Id : -1;
    screen->ClearWidgets();
    AddRows(screen);
    // The row the player was on is still there: it is its change that made others come or go.
    const int index = IndexOf(screen, currentId);
    screen->SetInitialOption(index);
    // The view starts where it was, or at the last row before it that still shows.
    int view = 1;
    for (int i = 1; i <= RowsShown(screen); ++i)
        if (RowAt(screen, i)->Id <= topId)
            view = i;
    SetView(screen, view, true);
    if (currentId >= 0)
        ShowDescription(static_cast<Option>(currentId));
}
} // namespace

namespace nfs::videopage
{
void Setup(UIOptionsScreen *screen)
{
    Page &p = g_page;
    if (!screen->WorkingVideoOptions)
    {
        // A screen new to the page. The screen's own copies of the game's options stay for its code that reads them.
        screen->WorkingVideoOptions = new VideoOptions(g_VideoOptions);
        screen->WorkingPerformanceLevel = g_PerformanceLevel;
        p.pending = p.original = p.live = InUse();
        p.haveValueColour = false;
    }
    if (!screen->OriginalVideoOptions)
    {
        screen->OriginalVideoOptions = new VideoOptions(g_VideoOptions);
        screen->OriginalPerformanceLevel = g_PerformanceLevel;
    }
    p.rebuild = p.redraw = false;
    AddRows(screen);
}

void AfterSetup(UIOptionsScreen *screen)
{
    SetView(screen, 1, true);
    ShowCursor(screen);
    // -set Debug.FEDump=2: the package's objects as the page has laid them out.
    static bool dumped;
    if (!dumped && nfs::config::Int("Debug", "FEDump", 0) == 2)
    {
        dumped = true;
        nfs::feng::Dump(screen->PackageName);
    }
}

bool Unchanged()
{
    return Same(g_page.pending, g_page.original);
}

void Revert()
{
    Page &p = g_page;
    p.pending = p.original;
    if (!Same(p.live, p.original))
        PutInUse(p.original);
    p.rebuild = true;
}

void Defaults()
{
    g_page.pending = DefaultSettings();
    g_page.rebuild = true;
}

void Apply()
{
    PutInUse(g_page.pending);
}

bool BeforeMessage(UIOptionsScreen *screen, unsigned int message)
{
    Page &p = g_page;
    if (message == kMessageKeep)
    {
        // The countdown's "keep": what is in use is what the page goes back to from here. The page stays.
        p.original = p.pending;
        return true;
    }
    if (message == kMessageAccept && !Unchanged() && SameDisplay(p.pending, p.original))
    {
        // Nothing the monitor could fail to show: applied and kept at once.
        PutInUse(p.pending);
        p.original = p.pending;
        RedrawRows(screen);
        return true;
    }
    return false;
}

void AfterMessage(UIOptionsScreen *screen, unsigned int message)
{
    Page &p = g_page;
    if (p.redraw && !p.rebuild)
        RedrawRows(screen);
    p.redraw = false;
    if (p.rebuild)
    {
        p.rebuild = false;
        Rebuild(screen);
        return;
    }
    // The menu scrolls a row at a time after the selection, which passes over headings; and the wheel moves the view
    // without the selection (bViewNeedsSync, until the next key).
    if (!screen->bViewNeedsSync && screen->pViewTop)
        SetView(screen, static_cast<int>(screen->GetWidgetIndex(screen->pViewTop)), false);
    else
        UpdateScrollBar(screen);
}
} // namespace nfs::videopage
