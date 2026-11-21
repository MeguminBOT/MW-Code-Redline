/**
 * Display setup (original: zPlatform __getResolutionByIndex 0x6c27d0 and __init_window 0x6bfab0), extended with the
 * modes in engine/rendering/display/display.h. The settings start from speed.ini; the in-game Video page changes them and writes them back.
 * Full-screen anti-aliasing is removed: the back buffer is never multisampled.
 */
#include <string.h>

#include "game/platform/Platform.h"
#include "core/config.h"
#include "engine/rendering/display/display.h"
#include "core/sdl.h"
#include "core/timing.h"
#include "engine/rendering/display/video.h"
#include "engine/rendering/vulkan/renderer.h"

namespace nfs::display
{
namespace
{
const DWORD kWindowedStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
const int kMaxResolutions = 128;
/** The Video page's render scales, in percent. */
const int kRenderScales[] = {100, 125, 150, 175, 200};
const int kRenderScaleCount = sizeof(kRenderScales) / sizeof(kRenderScales[0]);
/** D3DKMT_SCHEDULINGPRIORITYCLASS_BELOW_NORMAL, for gdi32's D3DKMTSetProcessSchedulingPriorityClass. */
const int kGpuPriorityBelowNormal = 1;
/** Motion blur range, in percent of the original's. */
const int kMaxMotionBlur = 200;
const int kDefaultMotionBlur = 100;

struct Resolution
{
    int width;
    int height;
    /** The highest refresh rate the monitor offers at this size. */
    int refresh_rate;
};

/** The monitor's resolutions in 32-bit colour, 640x480 and up, smallest first. */
struct ResolutionList
{
    Resolution entries[kMaxResolutions];
    int count;
};

struct State
{
    Settings settings = {Mode::Borderless, 0, 0};
    /** RefreshRate from speed.ini; 0 takes the highest the monitor offers at the fullscreen resolution. */
    int configured_refresh_rate = 0;
    /** -1: follow the game's VSync option. */
    int vsync = -1;
    /** Percent; see RenderScale(). */
    int render_scale = 100;
    /** Percent; see MotionBlur(). */
    int motion_blur = kDefaultMotionBlur;
    /** Percent; see VisualTreatment(). */
    int visual_treatment = 100;
    /** See DynamicShadows(). */
    bool dynamic_shadows = true;
    /** See AntiAliasingMode(). */
    bool fxaa = false;
    int fxaa_quality = 2;
    int supersampling = 150;
    /** See Anisotropy(). */
    int anisotropy = 16;
};

/** Supersampling's scales on the Video page, and what the option starts at. */
const int kSupersamplingScales[] = {125, 150, 175, 200};
const int kDefaultSupersampling = 150;

State g_state;
ResolutionList g_resolutions;

void DesktopMode(int &width, int &height, int &refresh_rate)
{
    DEVMODEA mode = {};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &mode))
    {
        width = static_cast<int>(mode.dmPelsWidth);
        height = static_cast<int>(mode.dmPelsHeight);
        refresh_rate = static_cast<int>(mode.dmDisplayFrequency);
    }
    else
    {
        width = GetSystemMetrics(SM_CXSCREEN);
        height = GetSystemMetrics(SM_CYSCREEN);
        refresh_rate = 0;
    }
}

RECT MonitorRect(bool work_area)
{
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    GetMonitorInfoA(MonitorFromWindow(g_hWnd, MONITOR_DEFAULTTOPRIMARY), &info);
    return work_area ? info.rcWork : info.rcMonitor;
}

/** "1920x1080" -> width and height; false (outputs untouched) if the text is not of that form. */
bool ParseResolution(const char *text, int &width, int &height)
{
    const char *x = text;
    while (*x && *x != 'x')
        ++x;
    if (!*x)
        return false;
    const int w = bStrToLong(text), h = bStrToLong(x + 1);
    if (w < 320 || h < 240)
        return false;
    width = w;
    height = h;
    return true;
}

void AddResolution(ResolutionList &list, int width, int height, int refresh_rate)
{
    for (int i = 0; i < list.count; ++i)
    {
        Resolution &r = list.entries[i];
        if (r.width == width && r.height == height)
        {
            if (refresh_rate > r.refresh_rate)
                r.refresh_rate = refresh_rate;
            return;
        }
    }
    if (list.count < kMaxResolutions)
        list.entries[list.count++] = {width, height, refresh_rate};
}

bool Smaller(const Resolution &a, const Resolution &b)
{
    const int area_a = a.width * a.height, area_b = b.width * b.height;
    return area_a != area_b ? area_a < area_b : a.width < b.width;
}

const ResolutionList &Resolutions()
{
    ResolutionList &list = g_resolutions;
    if (list.count == 0)
    {
        DEVMODEA mode = {};
        mode.dmSize = sizeof(mode);
        for (DWORD i = 0; EnumDisplaySettingsA(nullptr, i, &mode); ++i)
            if (mode.dmBitsPerPel == 32 && mode.dmPelsWidth >= 640 && mode.dmPelsHeight >= 480)
                AddResolution(list, mode.dmPelsWidth, mode.dmPelsHeight, mode.dmDisplayFrequency);
        int width, height, refresh_rate;
        DesktopMode(width, height, refresh_rate);
        AddResolution(list, width, height, refresh_rate);
        for (int i = 1; i < list.count; ++i)
        {
            const Resolution r = list.entries[i];
            int j = i;
            for (; j > 0 && Smaller(r, list.entries[j - 1]); --j)
                list.entries[j] = list.entries[j - 1];
            list.entries[j] = r;
        }
    }
    return list;
}

int FindResolution(int width, int height)
{
    const ResolutionList &list = Resolutions();
    for (int i = 0; i < list.count; ++i)
        if (list.entries[i].width == width && list.entries[i].height == height)
            return i;
    return -1;
}

/** True when a window with this client size, title bar included, fits the desktop work area. */
bool FitsWindow(int width, int height)
{
    RECT frame = {0, 0, width, height};
    AdjustWindowRect(&frame, kWindowedStyle, FALSE);
    const RECT work = MonitorRect(true);
    return frame.right - frame.left <= work.right - work.left && frame.bottom - frame.top <= work.bottom - work.top;
}

bool IsWide(int width, int height)
{
    return width * 3 > height * 4;
}

bool Allows(Mode mode, const Resolution &r, bool widescreen)
{
    if (!widescreen && IsWide(r.width, r.height))
        return false;
    if (mode == Mode::Borderless)
    {
        const Settings desktop = Defaults();
        return r.width == desktop.width && r.height == desktop.height;
    }
    return mode == Mode::Fullscreen || FitsWindow(r.width, r.height);
}

/** The largest resolution the mode allows that is no taller than the monitor (else the largest allowed); -1 if none. */
int LargestAllowed(Mode mode, bool widescreen)
{
    const ResolutionList &list = Resolutions();
    const Settings desktop = Defaults();
    int fallback = -1;
    for (int i = list.count - 1; i >= 0; --i)
    {
        if (!Allows(mode, list.entries[i], widescreen))
            continue;
        if (list.entries[i].height <= desktop.height)
            return i;
        if (fallback < 0)
            fallback = i;
    }
    return fallback;
}

int RefreshRateFor(const Settings &settings)
{
    if (settings.mode != Mode::Fullscreen)
        return 0;
    if (g_state.configured_refresh_rate > 0)
        return g_state.configured_refresh_rate;
    const int index = FindResolution(settings.width, settings.height);
    return index >= 0 ? Resolutions().entries[index].refresh_rate : 0;
}

void MakeCurrent(const Settings &settings)
{
    g_state.settings = settings;
    g_Windowed = settings.mode != Mode::Fullscreen;
}

const char *ConfigName(Mode mode)
{
    return mode == Mode::Fullscreen ? "fullscreen" : mode == Mode::Windowed ? "windowed" : "borderless";
}
} // namespace

void Initialize()
{
    // SDL's window side (core/sdl.h), which also makes the game DPI aware: without that Windows reports a scaled desktop
    // size and stretches the window.
    if (!sdl::Start(SDL_INIT_VIDEO))
        SetProcessDPIAware();

    char text[32];
    Settings settings = Defaults();
    config::String("Display", "Mode", "borderless", text, sizeof(text));
    settings.mode = !strcmp(text, "fullscreen") ? Mode::Fullscreen
                    : !strcmp(text, "windowed") ? Mode::Windowed
                                                : Mode::Borderless;
    config::String("Display", "Resolution", "native", text, sizeof(text));
    ParseResolution(text, settings.width, settings.height);
    g_state.configured_refresh_rate = config::Int("Display", "RefreshRate", 0);
    config::String("Display", "VSync", "game", text, sizeof(text));
    g_state.vsync = !strcmp(text, "on") ? 1 : !strcmp(text, "off") ? 0 : -1;
    const int render_scale = config::Int("Display", "RenderScale", 100);
    g_state.render_scale = render_scale < kRenderScales[0]                       ? kRenderScales[0]
                           : render_scale > kRenderScales[kRenderScaleCount - 1] ? kRenderScales[kRenderScaleCount - 1]
                                                                                  : render_scale;
    const int motion_blur = nfs::video::TestOptions() ? nfs::video::kTestMotionBlur
                                                      : config::Int("Display", "MotionBlur", kDefaultMotionBlur);
    g_state.motion_blur = motion_blur < 0 ? 0 : motion_blur > kMaxMotionBlur ? kMaxMotionBlur : motion_blur;
    const int treatment = config::Int("Display", "VisualTreatment", 100);
    g_state.visual_treatment = treatment < 0 ? 0 : treatment > 100 ? 100 : treatment;
    g_state.dynamic_shadows = config::Int("Display", "DynamicShadows", 1) != 0;
    g_state.fxaa = config::Int("Display", "Fxaa", 0) != 0;
    const int quality = config::Int("Display", "FxaaQuality", 2);
    g_state.fxaa_quality = quality < 0 ? 0 : quality >= kFxaaQualities ? kFxaaQualities - 1 : quality;
    const int supersampling = config::Int("Display", "Supersampling",
                                          g_state.render_scale > 100 ? g_state.render_scale : kDefaultSupersampling);
    g_state.supersampling = supersampling < kSupersamplingScales[0] ? kSupersamplingScales[0]
                            : supersampling > 200                   ? 200
                                                                    : supersampling;
    const int anisotropy = config::Int("Display", "Anisotropy", 16);
    g_state.anisotropy = anisotropy < 2 ? 2 : anisotropy > 16 ? 16 : anisotropy;
    if (Headless())
    {
        // Never exclusive fullscreen, which waits while in the background; the CPU and GPU serve other programs first.
        settings.mode = Mode::Borderless;
        SetPriorityClass(GetCurrentProcess(),
                         config::Int("Debug", "HeadlessLowPriority", 0) ? IDLE_PRIORITY_CLASS : BELOW_NORMAL_PRIORITY_CLASS);
        typedef LONG(WINAPI * SetSchedulingPriority)(HANDLE, int);
        const SetSchedulingPriority setGpuPriority = reinterpret_cast<SetSchedulingPriority>(
            GetProcAddress(GetModuleHandleA("gdi32.dll"), "D3DKMTSetProcessSchedulingPriorityClass"));
        if (setGpuPriority)
            setGpuPriority(GetCurrentProcess(), kGpuPriorityBelowNormal);
    }
    MakeCurrent(Fit(settings));
}

bool Headless()
{
    static const bool headless = vk::HasArgument("-headless");
    return headless;
}

bool DebugKeyDown(int vk)
{
    static DWORD s_focusBucket = ~0u;
    static bool s_focus;
    static DWORD s_keyBucket[256];
    static bool s_keyDown[256];
    const DWORD bucket = GetTickCount() >> 4;
    if (bucket != s_focusBucket)
    {
        s_focusBucket = bucket;
        s_focus = !Headless() && GetForegroundWindow() == g_hWnd;
    }
    if (!s_focus)
        return false;
    vk &= 0xFF;
    if (s_keyBucket[vk] != bucket + 1)
    {
        s_keyBucket[vk] = bucket + 1;
        s_keyDown[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
    return s_keyDown[vk];
}

bool IsWideMonitor()
{
    int width = GetSystemMetrics(SM_CXSCREEN), height = GetSystemMetrics(SM_CYSCREEN);
    return height > 0 && width * 3 > height * 4;
}

bool IsHorPlus()
{
    // The in-game Video option (VideoSettings::WideScreen in the profile); before a profile exists, the monitor decides.
    if (FEDatabase && FEDatabase->CurrentUserProfile)
        return FEDatabase->CurrentUserProfile->TheOptionsSettings.TheVideoSettings.WideScreen;
    return IsWideMonitor();
}

float FrontEndScaleX()
{
    if (!IsHorPlus() || g_WindowWidth <= 0 || g_WindowHeight <= 0)
        return 1.0f;
    float aspect = static_cast<float>(g_WindowWidth) / static_cast<float>(g_WindowHeight);
    return aspect > 4.0f / 3.0f ? (4.0f / 3.0f) / aspect : 1.0f;
}

bool IsWindowed()
{
    return g_state.settings.mode != Mode::Fullscreen;
}

void PlaceWindow()
{
    SDL_Window *window = sdl::Window();
    if (!window || Headless())
        return;
    const Settings &settings = g_state.settings;
    if (settings.mode != Mode::Windowed)
    {
        // Fullscreen and borderless alike: a window covering the monitor, onto which the renderer scales its frame (no
        // display mode changes). SDL keeps the windowed size and place to go back to.
        SDL_SetWindowFullscreenMode(window, nullptr);
        SDL_SetWindowFullscreen(window, true);
    }
    else
    {
        SDL_SetWindowFullscreen(window, false);
        SDL_SetWindowBordered(window, true);
        SDL_SetWindowResizable(window, false);
        SDL_SetWindowSize(window, settings.width, settings.height);
        // Centred in the monitor's work area, the title bar kept on it.
        SDL_Rect work = {};
        SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window), &work);
        int top = 0, left = 0, bottom = 0, right = 0;
        SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right);
        const int x = work.x + left + (work.w - left - right - settings.width) / 2;
        int y = work.y + top + (work.h - top - bottom - settings.height) / 2;
        if (y < work.y + top)
            y = work.y + top;
        SDL_SetWindowPosition(window, x, y);
    }
    SDL_ShowWindow(window);
    SDL_SyncWindow(window);
    GetWindowRect(g_hWnd, &g_WindowRect);
    GetClientRect(g_hWnd, &g_ClientRect);
}

Settings Current()
{
    return g_state.settings;
}

int RefreshRate()
{
    const int fullscreen = RefreshRateFor(g_state.settings);
    if (fullscreen > 0)
        return fullscreen;
    int width, height, refresh_rate;
    DesktopMode(width, height, refresh_rate);
    return refresh_rate;
}

Settings Defaults()
{
    int width, height, refresh_rate;
    DesktopMode(width, height, refresh_rate);
    return {Mode::Borderless, width, height};
}

Settings Fit(Settings settings, bool widescreen)
{
    const Settings desktop = Defaults();
    if (settings.mode == Mode::Borderless)
    {
        settings.width = desktop.width;
        settings.height = desktop.height;
        return settings;
    }
    const bool narrow_enough = widescreen || !IsWide(settings.width, settings.height);
    const bool valid = settings.mode == Mode::Fullscreen ? FindResolution(settings.width, settings.height) >= 0
                                                         : FitsWindow(settings.width, settings.height);
    if (valid && narrow_enough)
        return settings;
    if (settings.mode == Mode::Fullscreen && widescreen)
    {
        settings.width = desktop.width;
        settings.height = desktop.height;
        return settings;
    }
    const int index = LargestAllowed(settings.mode, widescreen);
    if (index >= 0)
    {
        settings.width = Resolutions().entries[index].width;
        settings.height = Resolutions().entries[index].height;
    }
    return settings;
}

Settings StepResolution(Settings settings, int step, bool widescreen)
{
    const ResolutionList &list = Resolutions();
    int index = FindResolution(settings.width, settings.height);
    if (index < 0)
        index = step > 0 ? -1 : list.count;
    for (int tries = 0; tries < list.count; ++tries)
    {
        index = (index + step + list.count) % list.count;
        if (Allows(settings.mode, list.entries[index], widescreen))
        {
            settings.width = list.entries[index].width;
            settings.height = list.entries[index].height;
            break;
        }
    }
    return settings;
}

void Apply(const Settings &requested)
{
    const Settings settings = Fit(requested);
    MakeCurrent(settings);

    char text[32];
    const Settings desktop = Defaults();
    if (settings.width == desktop.width && settings.height == desktop.height)
        lstrcpyA(text, "native");
    else
        wsprintfA(text, "%dx%d", settings.width, settings.height);
    config::WriteString("Display", "Mode", ConfigName(settings.mode));
    config::WriteString("Display", "Resolution", text);
}

const char *ModeName(Mode mode)
{
    return mode == Mode::Fullscreen ? "Fullscreen" : mode == Mode::Windowed ? "Windowed" : "Borderless";
}

int RenderScale()
{
    return g_state.render_scale;
}

void SetRenderScale(int percent)
{
    g_state.render_scale = percent;
    config::WriteInt("Display", "RenderScale", percent);
    if (percent > 100)
        SetSupersamplingScale(percent);
}

AntiAliasing AntiAliasingMode()
{
    const bool ssaa = g_state.render_scale > 100;
    return g_state.fxaa ? (ssaa ? AntiAliasing::FxaaSsaa : AntiAliasing::Fxaa)
                        : (ssaa ? AntiAliasing::Ssaa : AntiAliasing::Off);
}

void SetAntiAliasing(AntiAliasing mode)
{
    g_state.fxaa = HasFxaa(mode);
    config::WriteInt("Display", "Fxaa", g_state.fxaa ? 1 : 0);
    const int scale = HasSsaa(mode) ? g_state.supersampling : 100;
    if (scale != g_state.render_scale)
        SetRenderScale(scale);
}

bool FxaaOn()
{
    return g_state.fxaa;
}

int FxaaQuality()
{
    return g_state.fxaa_quality;
}

void SetFxaaQuality(int quality)
{
    g_state.fxaa_quality = quality < 0 ? 0 : quality >= kFxaaQualities ? kFxaaQualities - 1 : quality;
    config::WriteInt("Display", "FxaaQuality", g_state.fxaa_quality);
}

int SupersamplingScale()
{
    return g_state.supersampling;
}

void SetSupersamplingScale(int percent)
{
    if (percent == g_state.supersampling)
        return;
    g_state.supersampling = percent;
    config::WriteInt("Display", "Supersampling", percent);
}

int Anisotropy()
{
    return g_state.anisotropy;
}

void SetAnisotropy(int samples)
{
    g_state.anisotropy = samples < 2 ? 2 : samples > 16 ? 16 : samples;
    config::WriteInt("Display", "Anisotropy", g_state.anisotropy);
}

int ConfiguredRefreshRate()
{
    return g_state.configured_refresh_rate;
}

void SetConfiguredRefreshRate(int hertz)
{
    g_state.configured_refresh_rate = hertz;
    config::WriteInt("Display", "RefreshRate", hertz);
}

bool RefreshRateSelectable(Mode mode)
{
    return mode == Mode::Fullscreen && !vk::Enabled();
}

int StepRefreshRate(int width, int height, int hertz, int step)
{
    // The monitor's rates at this size, lowest first, after 0 (the highest).
    int rates[64];
    int count = 0;
    rates[count++] = 0;
    DEVMODEA mode = {};
    mode.dmSize = sizeof(mode);
    for (DWORD i = 0; EnumDisplaySettingsA(nullptr, i, &mode); ++i)
    {
        if (mode.dmBitsPerPel != 32 || static_cast<int>(mode.dmPelsWidth) != width ||
            static_cast<int>(mode.dmPelsHeight) != height)
            continue;
        const int rate = static_cast<int>(mode.dmDisplayFrequency);
        int at = 1;
        while (at < count && rates[at] < rate)
            ++at;
        if ((at < count && rates[at] == rate) || count == 64)
            continue;
        for (int j = count; j > at; --j)
            rates[j] = rates[j - 1];
        rates[at] = rate;
        ++count;
    }
    int index = 0;
    for (int i = 1; i < count; ++i)
        if (rates[i] <= hertz)
            index = i;
    index += step;
    return rates[index < 0 ? 0 : index >= count ? count - 1 : index];
}

int StepRenderScale(int percent, int step)
{
    int index = 0;
    for (int i = 0; i < kRenderScaleCount; ++i)
        if (kRenderScales[i] <= percent)
            index = i;
    return kRenderScales[(index + step + kRenderScaleCount) % kRenderScaleCount];
}

int StepSupersampling(int percent, int step)
{
    const int count = sizeof(kSupersamplingScales) / sizeof(kSupersamplingScales[0]);
    int index = 0;
    for (int i = 0; i < count; ++i)
        if (kSupersamplingScales[i] <= percent)
            index = i;
    index += step;
    return kSupersamplingScales[index < 0 ? 0 : index >= count ? count - 1 : index];
}

int MotionBlur()
{
    return g_state.motion_blur;
}

int VisualTreatment()
{
    return g_state.visual_treatment;
}

void SetVisualTreatment(int percent)
{
    g_state.visual_treatment = percent;
    if (nfs::video::TestOptions())
        return;
    char text[16];
    wsprintfA(text, "%d", percent);
    config::WriteString("Display", "VisualTreatment", text);
}

bool DynamicShadows()
{
    return g_state.dynamic_shadows;
}

void SetDynamicShadows(bool on)
{
    g_state.dynamic_shadows = on;
    if (!nfs::video::TestOptions())
        config::WriteString("Display", "DynamicShadows", on ? "1" : "0");
}

void SetMotionBlur(int percent)
{
    g_state.motion_blur = percent;
    if (nfs::video::TestOptions())
        return;
    char text[16];
    wsprintfA(text, "%d", percent);
    config::WriteString("Display", "MotionBlur", text);
}
} // namespace nfs::display

using nfs::display::g_state;

void __stdcall GetRacingResolution(int *width, int *height)
{
    *width = g_state.settings.width;
    *height = g_state.settings.height;
}

void InitPresentParameters(int width, int height, int refresh_rate)
{
    // Every caller passes the current settings' size (GetRacingResolution) except the Alt+Enter toggle (0x6de150),
    // which passes g_WindowWidth and g_WindowHeight: with a render scale, the scaled size. So the settings decide.
    width = g_state.settings.width;
    height = g_state.settings.height;
    D3DPRESENT_PARAMETERS &pp = g_PresentParameters;
    memset(&pp, 0, sizeof(pp));
    if (g_Windowed)
    {
        GetWindowRect(g_hWnd, &g_WindowRect);
        GetClientRect(g_hWnd, &g_ClientRect);
        pp.FullScreen_RefreshRateInHz = 0;
    }
    pp.Windowed = g_Windowed;
    pp.BackBufferWidth = width;
    pp.BackBufferHeight = height;
    bool discard = (g_DeviceCaps.Caps3 & D3DCAPS3_ALPHA_FULLSCREEN_FLIP_OR_DISCARD) || g_MatroxWorkarounds;
    pp.SwapEffect = discard ? D3DSWAPEFFECT_DISCARD : D3DSWAPEFFECT_COPY;
    pp.BackBufferFormat = g_BackBufferFormat;
    pp.AutoDepthStencilFormat = g_UseDepth16 ? D3DFMT_D16 : D3DFMT_D24S8;
    // The Video page's Sync (core/timing.h); the original's VSync option follows it for the code that still reads it.
    g_VideoOptions.VSyncOn = nfs::timing::WaitsForRefresh() ? 1 : 0;
    pp.PresentationInterval = g_VideoOptions.VSyncOn ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
    pp.hDeviceWindow = g_hWnd;
    pp.EnableAutoDepthStencil = TRUE;
    pp.Flags = 0;
    // Not in the original: the game draws at the render scale (engine/rendering/display/display.h). g_WindowWidth and g_WindowHeight size
    // everything it draws into, so they take the scaled size, within what the device can create.
    const int render_scale = nfs::display::RenderScale();
    int render_width = width * render_scale / 100, render_height = height * render_scale / 100;
    if (g_DeviceCaps.MaxTextureWidth && g_DeviceCaps.MaxTextureHeight &&
        (render_width > static_cast<int>(g_DeviceCaps.MaxTextureWidth) ||
         render_height > static_cast<int>(g_DeviceCaps.MaxTextureHeight)))
    {
        render_width = width;
        render_height = height;
    }
    g_WindowHeight = render_height;
    g_WindowWidth = render_width;
    // Not in the original: full-screen anti-aliasing is removed. The original multisampled the back buffer at
    // FSAALevel; the level and its limit are forced to 0 here, before any device or render target is created, so a
    // level saved by the original game or set by a performance-level preset cannot turn it back on.
    g_VideoOptions.FSAALevel = 0;
    g_VideoOptionLimits.FSAALevel = 0;
    pp.MultiSampleType = D3DMULTISAMPLE_NONE;
    pp.MultiSampleQuality = 0;

    // Not in the original: the table's refresh rate argument was ignored, so fullscreen ran at the adapter default.
    if (!g_Windowed)
        pp.FullScreen_RefreshRateInHz = nfs::display::RefreshRateFor(g_state.settings);
    if (g_state.vsync >= 0)
        pp.PresentationInterval = g_state.vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
    nfs::display::PlaceWindow();
    nfs::timing::Refresh();
}
