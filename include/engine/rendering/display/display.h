#pragma once

/**
 * Display mode of the rebuilt game: exclusive fullscreen, borderless window or window, at any resolution the monitor
 * offers (its own by default). The original only offered six 4:3 resolutions in exclusive fullscreen at the adapter's
 * default refresh rate. The mode and resolution are chosen on the in-game Video page and stored in speed.ini.
 */
namespace nfs::display
{
enum class Mode
{
    Fullscreen,
    Borderless,
    Windowed,
};

struct Settings
{
    Mode mode;
    /** Back buffer size; the monitor's size in borderless mode. */
    int width;
    int height;
};

inline bool operator==(const Settings &a, const Settings &b)
{
    return a.mode == b.mode && a.width == b.width && a.height == b.height;
}

inline bool operator!=(const Settings &a, const Settings &b)
{
    return !(a == b);
}

/** Reads the settings and prepares the window mode. Call before the game creates its window. */
void Initialize();
/** Gives the game window its style and placement for the current mode. */
void PlaceWindow();
/** True when the game draws into a window (windowed or borderless) rather than exclusive fullscreen. */
bool IsWindowed();
/** -headless, for test runs while the computer is in use: the window is never shown and never takes the focus, the
 * Vulkan renderer (which -headless selects) draws off screen and presents nothing (its captures still work), races
 * run at most 30 frames per second (nfs::timing::MinFrameMs), and other programs come first for the CPU and GPU. */
bool Headless();
/** A -headless run's profiles are beside its exe instead of in the player's Documents (source/core/HeadlessSaves.cpp). */
void RedirectHeadlessSaves();
/** Whether debug key `vk` (F8-F11) is held while the game window has the focus; never in headless runs. The key and
 * the focus are read at most every 16 ms: each read is a system call, and a slow one off the input desktop. */
bool DebugKeyDown(int vk);
/** True when the in-game Widescreen option is on: 3D views keep the 4:3 vertical field of view and widen horizontally,
 * the front end keeps its proportions and the HUD uses its widescreen layout. */
bool IsHorPlus();
/** True when the primary monitor is wider than 4:3 (the default for the Widescreen option). */
bool IsWideMonitor();
/** Horizontal scale for the 640x480 front end so it keeps its proportions on the current screen (1 at 4:3 or narrower). */
float FrontEndScaleX();

Settings Current();
/** The refresh rate the game is shown at: the fullscreen mode's, else the desktop's (0 if Windows does not say). */
int RefreshRate();
/** Borderless at the monitor's resolution. */
Settings Defaults();
/** The settings with a resolution the mode allows: the monitor's own for borderless, one of the monitor's modes for
 * fullscreen, one whose window fits the desktop for windowed. Without `widescreen`, fullscreen and windowed are limited
 * to 4:3 or narrower resolutions (borderless keeps the monitor's). */
Settings Fit(Settings settings, bool widescreen = true);
/** The next (step 1) or previous (step -1) resolution the settings' mode allows, wrapping around. */
Settings StepResolution(Settings settings, int step, bool widescreen = true);
/** Makes the settings current and saves them to speed.ini. They take effect at the next device reset, which the caller
 * requests (g_VideoOptionsChanged). */
void Apply(const Settings &settings);
const char *ModeName(Mode mode);

/** The refresh rate exclusive fullscreen asks for (speed.ini [Display] RefreshRate): 0 for the highest the monitor
 * offers at the resolution. Direct3D 9 only: the Vulkan renderer shows its frames at the desktop's rate in every mode. */
int ConfiguredRefreshRate();
void SetConfiguredRefreshRate(int hertz);
/** The refresh rate after `hertz` among those the monitor offers at this size (step 1) or before it (step -1), 0 (the
 * highest) first, without wrapping. */
int StepRefreshRate(int width, int height, int hertz, int step);
/** True when the mode's refresh rate can be chosen: exclusive fullscreen on Direct3D 9. */
bool RefreshRateSelectable(Mode mode);

/**
 * Anti-aliasing, chosen on the Video page as one option with its settings under it:
 * - FXAA (speed.ini [Display] Fxaa, FxaaQuality): a pass over the finished 3D frame, before the HUD, that finds edges
 *   by their contrast and blends along them (source/engine/rendering/post/PostProcess.cpp, assets/shaders/post.fx `fxaa`; Vulkan). The quality
 *   (0 to 2) is how far along an edge it looks.
 * - SSAA, supersampling: a RenderScale() over 100, everything drawn at that percentage of the screen size and filtered
 *   down. Turning it off keeps the scale it had ([Display] Supersampling) for when it is turned on again.
 */
enum class AntiAliasing
{
    Off,
    Fxaa,
    Ssaa,
    FxaaSsaa,
};
const int kFxaaQualities = 3;
AntiAliasing AntiAliasingMode();
/** Switches FXAA and supersampling on or off (the render scale goes to 100 or to SupersamplingScale()) and saves it. */
void SetAntiAliasing(AntiAliasing mode);
bool FxaaOn();
inline bool HasFxaa(AntiAliasing mode)
{
    return mode == AntiAliasing::Fxaa || mode == AntiAliasing::FxaaSsaa;
}
inline bool HasSsaa(AntiAliasing mode)
{
    return mode == AntiAliasing::Ssaa || mode == AntiAliasing::FxaaSsaa;
}
int FxaaQuality();
void SetFxaaQuality(int quality);
/** The scale supersampling has when it is on (125 to 200 percent). */
int SupersamplingScale();
void SetSupersamplingScale(int percent);

/** Anisotropic texture filtering's sample count (2, 4, 8 or 16; speed.ini [Display] Anisotropy), used while the Video
 * page's Texture Filtering is at Anisotropic, within what the device offers. Takes effect on the next frame. */
int Anisotropy();
void SetAnisotropy(int samples);

/**
 * Render scale, in percent: the game draws everything (3D, effects, HUD and menus) at the back buffer size times this
 * and scales the result to the screen. Above 100% that is supersampling, which stands in for the removed full-screen
 * anti-aliasing. Chosen on the Video page and stored in speed.ini ([Display] RenderScale); like the settings above it
 * takes effect at the next device reset.
 */
int RenderScale();
void SetRenderScale(int percent);
/** The render scale after `percent` in the Video page's list (step 1) or before it (step -1), wrapping around. */
int StepRenderScale(int percent, int step);
/** The supersampling scale after `percent` in the Video page's list (step 1) or before it (step -1), without wrapping. */
int StepSupersampling(int percent, int step);

/**
 * Motion blur, in percent of the original game's speed blur (0 to 200, 0 turns it off): scales how strongly and how far
 * the frame is smeared along the camera's motion at speed (eRenderSpeedBlur). Chosen on the Video page and stored in
 * speed.ini ([Display] MotionBlur). The strength takes effect on the next frame; the page also switches the video
 * options' MotionBlurEnable (whether the effect's render targets exist) with it, at the next device reset.
 */
int MotionBlur();
void SetMotionBlur(int percent);

/**
 * Dynamic Shadows, on the Video page and in speed.ini ([Display] DynamicShadows): lights that move cast shadows, at
 * present the player's headlights at night (source/engine/rendering/shadows/SunShadows.cpp). Takes effect on the next frame.
 */
bool DynamicShadows();
void SetDynamicShadows(bool on);


/**
 * The visual treatment's strength, in percent of the original look (0 to 100): the colour curve, tint, desaturation,
 * brightness, vignette and its motion blur are blended from neutral by it (source/engine/rendering/post/PostProcess.cpp). The Video page's
 * Visual Treatment row steps through kVisualTreatmentLevels, shown as percentages (Off for 0); speed.ini [Display]
 * VisualTreatment takes any percentage. 0 also switches the video options' VisualTreatment off.
 */
const int kVisualTreatmentLevels[] = {0, 25, 33, 50, 66, 75, 100};
const int kVisualTreatmentLevelCount = 7;
int VisualTreatment();
void SetVisualTreatment(int percent);

/**
 * Resolution of the effects drawn into textures (speed.ini [Display]; what the HD Reflections mod changed by patching
 * the game, done natively for both renderers). They take effect when the render targets are next made.
 * - ShadowMapResolution: the car shadow map, 256 to 8192 (default 2048; the original 1024).
 * - ReflectionResolution: each face of the car reflection cube, 64 to 4096 (default 1024; the original 256).
 * - RoadReflectionScale: road reflections as a fraction of the render size, 0.1 to 1 (default 0.5; the original was a
 *   fixed 320x240, 4:3 whatever the screen).
 * - MirrorScale: the rear-view mirror at half the render width and a sixth of its height, times this, 0.25 to 2
 *   (default 1; the original was a 256x256 square stretched over the mirror).
 */
int ShadowMapSize();
int ReflectionMapSize();
void SetReflectionMapSize(int size);
void RoadReflectionSize(int &width, int &height);
/** RoadReflectionScale in percent (10 to 100). */
int RoadReflectionPercent();
void SetRoadReflectionPercent(int percent);
void MirrorSize(int &width, int &height);
} // namespace nfs::display
