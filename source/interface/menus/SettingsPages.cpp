/**
 * The rebuild's own options pages (interface/menus/settingspages.h, docs/CONTROLS.md "The Controller Settings page", docs/NIGHT.md
 * "World Settings"). Not in the original game.
 *
 * Each page's entry goes into the options menu after one of the menu's own (UIOptionsMain::Setup 0x528f30 wrapped,
 * IconScrollerMenu::AddOption 0x573960 wrapped while it runs) and sets the options category to the page's. The menu's
 * own routing (UIOptionsMain::NotificationMessage 0x545310, wrapped) has no package for a category it does not know,
 * so the wrapper opens Options.fng (Pause_Options.fng from the pause menu) for it, where UIOptionsScreen::Setup builds
 * the rows (source/interface/menus/UIOptionsScreen.cpp). A row is a speed.ini key with named steps; a change is saved and put in use
 * at once, so there is nothing to accept or revert.
 */
#include <windows.h>

#include "game/gen/api.h"
#include "gameplay/ai/aituning.h"
#include "engine/rendering/camera/camera.h"
#include "core/config.h"
#include "core/input.h"
#include "engine/rendering/lights/lamps.h"
#include "legacy/link.h"
#include "interface/menus/optionsrows.h"
#include "interface/menus/settingspages.h"
#include "engine/audio/sound.h"
#include "gameplay/world/timeofday.h"
#include "gameplay/world/traffic.h"
#include "gameplay/world/weather.h"

NFS_REPLACE(0x528f30, OptionsMainSetup)
NFS_REPLACE(0x545310, OptionsMainMessage)
NFS_REPLACE(0x573960, IconMenuAddOption)

namespace nfs::rumble
{
void SetStrength(int percent);
}

namespace
{
const unsigned int kAcceptMessage = 0x0C407210;
/** The options menu's "go to the chosen entry's page" message, and the entry's message it stored before (+0x138). */
const unsigned int kGoToPage = 0xE1FDE1D1;
const int kPreviousMessage = 0x138;
/** UIOptionsMain: opened from the pause menu (+0x148). FEDatabase +300: online and LAN flags (bits 3 and 6), with
 * which the menu stays in the main menu's packages. */
const int kFromPauseMenu = 0x148;
const int kDatabaseFlags = 300;
const unsigned int kActionLeft = 0x9120409E;
const unsigned int kActionRight = 0xB5971BF1;
/** Options.fng's page icon and title. */
const unsigned int kPageIcon = 0x08007B4C;
const unsigned int kPageTitle = 0x42ADB44C;

typedef void(__thiscall *Setup)(void *);
typedef void(__thiscall *AddOptionCall)(void *, IconOption *);
typedef void(__thiscall *Message)(void *, unsigned int, FEObject *, unsigned int, unsigned int);
/** Set while the options menu adds its entries. */
bool g_settingUpOptions;

/** A number as speed.ini holds it ("1.5", "100"), without the C runtime's parser (not linked). */
float Number(const char *text)
{
    float whole = 0.0f, scale = 0.0f;
    for (const char *p = text; *p; ++p)
    {
        if (*p == '.' && scale == 0.0f)
            scale = 1.0f;
        else if (*p >= '0' && *p <= '9')
        {
            if (scale > 0.0f)
            {
                scale *= 0.1f;
                whole += (*p - '0') * scale;
            }
            else
                whole = whole * 10.0f + (*p - '0');
        }
        else
            break;
    }
    return whole;
}

OptionsSettings &Options()
{
    return FEDatabase->CurrentUserProfile->TheOptionsSettings;
}

/** A row: named steps of a speed.ini key (stored as the step's text), and what puts a new value in use. */
struct Setting
{
    const char *title;
    const char *section;
    const char *key;
    const char *const *values;
    const char *const *names;
    int count;
    int fallback;
    void (*apply)(const char *value);
};

struct Page
{
    int category;
    /** The entry's icon and name (texture and language hashes), and the menu entry (name) it follows. */
    unsigned int icon;
    unsigned int title;
    unsigned int after;
    const Setting *settings;
    int count;
    /** A page of rows of its own instead of settings: the rows (`count` of them) and its defaults. */
    void (*build)(UIOptionsScreen *screen, int rows);
    void (*defaults)();
};

// ---- Controller Settings ------------------------------------------------------------------------------------------

void ApplyRumble(const char *value)
{
    nfs::rumble::SetStrength(static_cast<int>(Number(value)));
    nfs::input::ReloadSettings();
}

void ApplyShaping(const char *)
{
    nfs::input::ReloadSettings();
}

const char *const kRumbleValues[] = {"0", "25", "50", "75", "100", "150"};
const char *const kRumbleNames[] = {"Off", "25%", "50%", "75%", "100%", "150%"};
const char *const kOnOffValues[] = {"0", "1"};
const char *const kOnOffNames[] = {"Off", "On"};
const char *const kDeadZoneValues[] = {"0", "5", "10", "15", "20", "25"};
const char *const kDeadZoneNames[] = {"None", "5%", "10%", "15%", "20%", "25%"};
const char *const kOuterValues[] = {"0", "5", "10", "15"};
const char *const kOuterNames[] = {"None", "5%", "10%", "15%"};
const char *const kCurveValues[] = {"100", "125", "150", "200"};
const char *const kCurveNames[] = {"Linear", "Curved 1.25", "Curved 1.5", "Curved 2"};

const Setting kControllerSettings[] = {
    {"Vibration", "Controls", "Rumble", kRumbleValues, kRumbleNames, 6, 4, ApplyRumble},
    {"Xbox Pad Vibration (Restart)", "Controls", "XboxRumble", kOnOffValues, kOnOffNames, 2, 0, nullptr},
    {"Stick Dead Zone", "Controls", "DeadZone", kDeadZoneValues, kDeadZoneNames, 6, 0, ApplyShaping},
    {"Stick Outer Dead Zone", "Controls", "OuterDeadZone", kOuterValues, kOuterNames, 4, 0, ApplyShaping},
    {"Steering Response", "Controls", "SteeringCurve", kCurveValues, kCurveNames, 4, 0, ApplyShaping},
    {"Trigger Dead Zone", "Controls", "TriggerDeadZone", kDeadZoneValues, kDeadZoneNames, 6, 0, ApplyShaping},
};

// ---- World Settings -----------------------------------------------------------------------------------------------

void ApplyClock(const char *)
{
    nfs::timeofday::Reconfigure();
}

void ApplyWeather(const char *)
{
    nfs::weather::Reconfigure();
}

void ApplyLampGlow(const char *value)
{
    nfs::lamps::SetHazeScale(Number(value));
}

void ApplyTraffic(const char *value)
{
    nfs::traffic::SetScale(Number(value));
}

const char *const kHourValues[] = {"cycle", "06:00", "09:00", "12:00", "15:00", "18:30", "21:00", "night", "day"};
const char *const kHourNames[] = {"Day and Night",   "Dawn (06:00)",    "Morning (09:00)",
                                  "Noon (12:00)",    "Afternoon (15:00)", "Dusk (18:30)",
                                  "Evening (21:00)", "Night (01:00)",   "Original Lighting"};
const char *const kSpeedValues[] = {"1", "2", "5", "10", "20", "60"};
const char *const kSpeedNames[] = {"1 Min an Hour", "2 Min an Hour", "5 Min an Hour", "10 Min an Hour",
                                   "20 Min an Hour", "Real Time"};
const char *const kWeatherValues[] = {"cycle", "clear", "cloudy", "rain", "storm", "fog", "game"};
const char *const kWeatherNames[] = {"Changing", "Clear", "Cloudy", "Rain", "Storm", "Fog", "Original"};
const char *const kGlowValues[] = {"0", "0.5", "1", "1.5"};
const char *const kGlowNames[] = {"Off", "Low", "Normal", "High"};
const char *const kTrafficValues[] = {"0", "0.3", "0.6", "1"};
const char *const kTrafficNames[] = {"None", "Light", "Medium", "Normal"};

const Setting kWorldSettings[] = {
    {"Time of Day", "World", "TimeOfDay", kHourValues, kHourNames, 9, 0, ApplyClock},
    {"Clock Speed", "World", "MinutesPerHour", kSpeedValues, kSpeedNames, 6, 2, ApplyClock},
    {"Weather", "World", "Weather", kWeatherValues, kWeatherNames, 7, 0, ApplyWeather},
    {"Lightning", "World", "Lightning", kOnOffValues, kOnOffNames, 2, 1, ApplyWeather},
    {"Lamp Glow", "World", "LampHaze", kGlowValues, kGlowNames, 4, 2, ApplyLampGlow},
    {"Traffic", "World", "Traffic", kTrafficValues, kTrafficNames, 4, 3, ApplyTraffic},
};

// ---- Opponents and Cops ------------------------------------------------------------------------------------------

void ApplyAi(const char *)
{
    nfs::aituning::Apply();
}

const char *const kDrivingValues[] = {"original", "improved"};
const char *const kDrivingNames[] = {"Original", "Improved"};
const char *const kRubberValues[] = {"original", "reduced", "off"};
const char *const kRubberNames[] = {"Original", "Reduced", "Off"};

const Setting kAiSettings[] = {
    {"AI Driving", "AI", "Driving", kDrivingValues, kDrivingNames, 2, 1, ApplyAi},
    {"Rubber Band", "AI", "RubberBand", kRubberValues, kRubberNames, 3, 1, ApplyAi},
    {"Cop Tactics", "AI", "Cops", kDrivingValues, kDrivingNames, 2, 1, ApplyAi},
    {"Traffic Speeds", "AI", "Traffic", kDrivingValues, kDrivingNames, 2, 1, ApplyAi},
};

// ---- Camera Settings ----------------------------------------------------------------------------------------------

/** The camera page's rows: the preset, the view it edits, then that view's adjustments. */
enum CameraRowKind
{
    kCameraPreset,
    kCameraTarget,
    kCameraView,
    kCameraHeight,
    kCameraDistance,
    kCameraTilt,
    kCameraFov,
    kCameraRows
};

struct AdjustmentStep
{
    const char *title;
    float step, lowest, highest;
};

const AdjustmentStep kAdjustmentSteps[nfs::camera::kAdjustments] = {
    {"Height", 0.1f, -1.0f, 3.0f},
    {"Distance", 0.25f, -3.0f, 8.0f},
    {"Tilt", 1.0f, -15.0f, 20.0f},
    {"Field of View", 2.0f, -30.0f, 50.0f},
};
const char *const kViewNames[nfs::camera::kViews] = {"Bumper", "Hood", "Close", "Far"};

struct CameraPage
{
    UIOptionsScreen *screen;
    int view;
    /** The adjustment rows edit the current car's own values (else the preset's). */
    bool car;
};

CameraPage g_camera = {nullptr, nfs::camera::kFar, false};

float CameraValue(int adjustment)
{
    return g_camera.car ? nfs::camera::GetCar(g_camera.view, adjustment)
                        : nfs::camera::Get(nfs::camera::Preset(), g_camera.view, adjustment);
}

void RedrawCameraRows()
{
    if (!g_camera.screen)
        return;
    const int rows = static_cast<int>(g_camera.screen->iIndexToAdd) - 1;
    for (int i = 1; i <= rows; ++i)
        if (FEWidget *w = g_camera.screen->GetWidget(i))
            w->Draw();
}

/** "+0.3 m", "-1.25 m", "3 Degrees Down", "+10 Degrees", "Game" for none. */
void AdjustmentText(int adjustment, float value, char *out, int size)
{
    const int hundredths = static_cast<int>(value * 100.0f + (value < 0.0f ? -0.5f : 0.5f));
    const int magnitude = hundredths < 0 ? -hundredths : hundredths;
    const int whole = magnitude / 100, part = magnitude % 100;
    const char sign = hundredths < 0 ? '-' : '+';
    if (!hundredths)
        lstrcpynA(out, "Game", size);
    else if (adjustment == nfs::camera::kTilt)
        wsprintfA(out, "%d %s %s", whole, whole == 1 ? "Degree" : "Degrees", hundredths > 0 ? "Down" : "Up");
    else if (adjustment == nfs::camera::kFov)
        wsprintfA(out, "%c%d Degrees", sign, whole);
    else if (part % 10 == 0)
        wsprintfA(out, "%c%d.%d m", sign, whole, part / 10);
    else
        wsprintfA(out, "%c%d.%02d m", sign, whole, part);
}

class CameraRow : public FEToggleWidget
{
  public:
    explicit CameraRow(int kind) : FEToggleWidget(true), Kind(kind) {}

    void Act(const char *, unsigned int action) override
    {
        if (action == kActionLeft || action == kActionRight)
        {
            const int step = action == kActionRight ? 1 : -1;
            if (Kind == kCameraPreset)
            {
                const int next = nfs::camera::Preset() + step;
                if (next >= 0 && next <= nfs::camera::kPresets)
                    nfs::camera::SetPreset(next);
            }
            else if (Kind == kCameraTarget)
            {
                if (nfs::camera::CurrentCar()[0])
                    g_camera.car = !g_camera.car;
            }
            else if (Kind == kCameraView)
            {
                const int next = g_camera.view + step;
                if (next >= 0 && next < nfs::camera::kViews)
                    g_camera.view = next;
            }
            else
            {
                // Adjusting the game's cameras for all cars starts the first preset.
                if (!g_camera.car && !nfs::camera::Preset())
                    nfs::camera::SetPreset(1);
                const int adjustment = Kind - kCameraHeight;
                const AdjustmentStep &s = kAdjustmentSteps[adjustment];
                float value = CameraValue(adjustment) + step * s.step;
                value = value < s.lowest ? s.lowest : value > s.highest ? s.highest : value;
                // Whole steps, without the drift of adding tenths.
                value = static_cast<float>(static_cast<int>(value / s.step + (value < 0.0f ? -0.5f : 0.5f))) * s.step;
                if (g_camera.car)
                    nfs::camera::SetCar(g_camera.view, adjustment, value);
                else
                    nfs::camera::Set(nfs::camera::Preset(), g_camera.view, adjustment, value);
            }
            RedrawCameraRows();
        }
        bMovedLastUpdate = true;
        BlinkArrows(action);
        Draw();
    }

    void Draw() override
    {
        char value[48];
        const int preset = nfs::camera::Preset();
        switch (Kind)
        {
        case kCameraPreset:
            FEPrintf(pTitle, "%s", "Camera Preset");
            if (preset)
                wsprintfA(value, "Preset %d", preset);
            else
                lstrcpynA(value, "Game Cameras", sizeof(value));
            break;
        case kCameraTarget:
            FEPrintf(pTitle, "%s", "Adjust For");
            if (g_camera.car && nfs::camera::CurrentCar()[0])
                wsprintfA(value, "This Car (%s)", nfs::camera::CurrentCar());
            else
                lstrcpynA(value, "All Cars", sizeof(value));
            break;
        case kCameraView:
            FEPrintf(pTitle, "%s", "View to Adjust");
            lstrcpynA(value, kViewNames[g_camera.view], sizeof(value));
            break;
        default:
        {
            const int adjustment = Kind - kCameraHeight;
            FEPrintf(pTitle, "%s", kAdjustmentSteps[adjustment].title);
            AdjustmentText(adjustment, CameraValue(adjustment), value, sizeof(value));
            break;
        }
        }
        FEPrintf(pData, "%s", value);
    }

  private:
    int Kind;
};

void BuildCameraPage(UIOptionsScreen *screen, int rows)
{
    g_camera.screen = screen;
    g_camera.view = nfs::camera::CurrentView();
    g_camera.car = false;
    for (int i = 0; i < kCameraRows && i < rows; ++i)
        screen->AddToggleOption(new CameraRow(i), true);
}

/** The screen's Defaults: every view of what the rows edit (the chosen preset, or the car's own) back to the game's
 * (the preset stays chosen). */
void CameraDefaults()
{
    const int preset = nfs::camera::Preset();
    for (int v = 0; v < nfs::camera::kViews; ++v)
        for (int a = 0; a < nfs::camera::kAdjustments; ++a)
            if (g_camera.car)
                nfs::camera::SetCar(v, a, 0.0f);
            else
                nfs::camera::Set(preset, v, a, 0.0f);
    RedrawCameraRows();
}

// ---- The Audio page's own rows -------------------------------------------------------------------------------------

/** Rows added under the game's own on the Audio page: they act at once, as this file's pages do. */
enum AudioRowKind
{
    kAudioMaster,
    kAudioDevice,
    kAudioBackground,
    kAudioRows
};

class AudioRow : public FEToggleWidget
{
  public:
    explicit AudioRow(int kind) : FEToggleWidget(true), Kind(kind) {}

    void Act(const char *, unsigned int action) override
    {
        if (action == kActionLeft || action == kActionRight)
        {
            const int step = action == kActionRight ? 1 : -1;
            switch (Kind)
            {
            case kAudioMaster:
            {
                const int next = (nfs::sound::MasterVolume() + 2) / 5 * 5 + step * 5;
                nfs::sound::SetMasterVolume(next < 0 ? 0 : next > 100 ? 100 : next);
                break;
            }
            case kAudioDevice:
            {
                // -1 is the system's default, then the devices SDL lists; wrapping around.
                const int count = nfs::sound::RefreshDevices();
                const int next = (nfs::sound::CurrentDevice() + 1 + step + count + 1) % (count + 1) - 1;
                nfs::sound::SetDevice(next);
                break;
            }
            default:
                nfs::sound::SetBackgroundSound(!nfs::sound::BackgroundSound());
                break;
            }
        }
        bMovedLastUpdate = true;
        BlinkArrows(action);
        Draw();
    }

    void Draw() override
    {
        switch (Kind)
        {
        case kAudioMaster:
            FEPrintf(pTitle, "%s", "Master Volume");
            FEPrintf(pData, "%d%%", nfs::sound::MasterVolume());
            break;
        case kAudioDevice:
        {
            // The device's name without its driver's (the part in brackets), cut to fit between the arrows.
            char name[32];
            lstrcpynA(name, nfs::sound::DeviceName(nfs::sound::CurrentDevice()), sizeof(name));
            for (char *p = name; *p; ++p)
                if (p[0] == ' ' && p[1] == '(' && p > name)
                {
                    *p = 0;
                    break;
                }
            const int kFits = 24;
            if (lstrlenA(name) > kFits)
                lstrcpyA(name + kFits - 3, "...");
            FEPrintf(pTitle, "%s", "Output Device");
            FEPrintf(pData, "%s", name);
            break;
        }
        default:
            FEPrintf(pTitle, "%s", "Sound in Background");
            FEPrintf(pData, "%s", nfs::sound::BackgroundSound() ? "On" : "Off");
            break;
        }
    }

  private:
    int Kind;
};

const unsigned int kControlsString = 0x04DC6DB5;          // "Controls"
const unsigned int kGameplayString = 0xD0CF6EE1;          // "Gameplay"
const unsigned int kControllerSettingsString = 0xA5C4F35D; // "Controller Settings"
const unsigned int kWorldSettingsString = 0x748D52C2;      // OPTIONS_WORLD_SETTINGS (tools/fepages.py)
const unsigned int kControllerIcon = 0xA04A7B26;            // OPTIONS_ICON_CONTROLS
const unsigned int kWorldIcon = 0xF505FDFA;                 // OPTIONS_ICON_WORLD (tools/fepages.py)
const unsigned int kCameraSettingsString = 0xD4FBE8E3;     // OPTIONS_CAMERA_SETTINGS (tools/fepages.py)
const unsigned int kCameraIcon = 0x661E5FFB;                // OPTIONS_ICON_CAMERA (tools/fepages.py)
const unsigned int kAiSettingsString = 0x901BA144;         // OPTIONS_AI_SETTINGS (tools/fepages.py)
const unsigned int kAiIcon = 0x447E2B3C;                    // OPTIONS_ICON_AI (tools/fepages.py)

const Page kPages[] = {
    {nfs::settingspages::kControllerCategory, kControllerIcon, kControllerSettingsString, kControlsString,
     kControllerSettings, sizeof(kControllerSettings) / sizeof(kControllerSettings[0]), nullptr, nullptr},
    {nfs::settingspages::kWorldCategory, kWorldIcon, kWorldSettingsString, kGameplayString, kWorldSettings,
     sizeof(kWorldSettings) / sizeof(kWorldSettings[0]), nullptr, nullptr},
    {nfs::settingspages::kCameraCategory, kCameraIcon, kCameraSettingsString, kGameplayString, nullptr, kCameraRows,
     BuildCameraPage, CameraDefaults},
    {nfs::settingspages::kAiCategory, kAiIcon, kAiSettingsString, kGameplayString, kAiSettings,
     sizeof(kAiSettings) / sizeof(kAiSettings[0]), nullptr, nullptr},
};

const Page *PageOf(int category)
{
    for (const Page &p : kPages)
        if (p.category == category)
            return &p;
    return nullptr;
}

/** The step a stored value is at: the same text, or for numbers the nearest at or below it; -1 for a value of
 * the setting's own (typed into speed.ini), which the row shows as it is. */
int StepOf(const Setting &s, const char *value)
{
    for (int i = 0; i < s.count; ++i)
        if (!lstrcmpiA(value, s.values[i]))
            return i;
    const bool numeric = (value[0] >= '0' && value[0] <= '9') || value[0] == '.';
    for (int i = 0; numeric && i < s.count; ++i)
        if (!(s.values[i][0] >= '0' && s.values[i][0] <= '9'))
            return -1;
    if (!numeric)
        return -1;
    const float v = Number(value);
    int step = 0;
    for (int i = 0; i < s.count; ++i)
        if (Number(s.values[i]) <= v + 1e-4f)
            step = i;
    return step;
}

void Read(const Setting &s, char *value, int size)
{
    nfs::config::String(s.section, s.key, s.values[s.fallback], value, size);
}

void Store(const Setting &s, const char *value)
{
    nfs::config::WriteString(s.section, s.key, value);
    if (s.apply)
        s.apply(value);
}

class SettingRow : public FEToggleWidget
{
  public:
    explicit SettingRow(const Setting &setting) : FEToggleWidget(true), S(setting) {}

    void Act(const char *, unsigned int action) override
    {
        if (action == kActionLeft || action == kActionRight)
        {
            char value[32];
            Read(S, value, sizeof(value));
            const int step = StepOf(S, value);
            const int next = step < 0 ? S.fallback : step + (action == kActionRight ? 1 : -1);
            if (next >= 0 && next < S.count)
                Store(S, S.values[next]);
        }
        bMovedLastUpdate = true;
        BlinkArrows(action);
        Draw();
    }

    void Draw() override
    {
        char value[32];
        Read(S, value, sizeof(value));
        const int step = StepOf(S, value);
        FEPrintf(pTitle, "%s", S.title);
        FEPrintf(pData, "%s", step >= 0 ? S.names[step] : value);
    }

  private:
    const Setting &S;
};

class PageEntry : public IconOption
{
  public:
    explicit PageEntry(const Page &page) : IconOption(page.icon, page.title, 0), Category(page.category) {}
    void React(const char *, unsigned int message, class FEObject *, unsigned int, unsigned int) override
    {
        if (message == kAcceptMessage)
            Options().CurrentCategory = Category;
    }

  private:
    int Category;
};
} // namespace

/** 0x528f30 wrapped: the options menu's entries, the pages' among them (IconMenuAddOption). */
void __fastcall OptionsMainSetup(void *self, void *)
{
    g_settingUpOptions = true;
    NFS_ORIGINAL(0x528f30, Setup)(self);
    g_settingUpOptions = false;
}

/** IconScrollerMenu::AddOption (0x573960) as before; while the options menu sets up, each page's entry follows the
 * menu's entry it belongs after (added later, it would land past the scroller's end markers, out of reach). */
void __fastcall IconMenuAddOption(void *self, void *, IconOption *option)
{
    const AddOptionCall original = NFS_ORIGINAL(0x573960, AddOptionCall);
    original(self, option);
    if (!g_settingUpOptions || !option)
        return;
    for (const Page &p : kPages)
        if (option->NameHash == p.after)
            original(self, new PageEntry(p));
}

/** 0x545310 wrapped: the menu's routing, then the page's package for a page's category. */
void __fastcall OptionsMainMessage(void *self, void *, unsigned int message, FEObject *object, unsigned int param1,
                                   unsigned int param2)
{
    NFS_ORIGINAL(0x545310, Message)(self, message, object, param1, param2);
    const char *base = static_cast<const char *>(self);
    if (message != kGoToPage || *reinterpret_cast<const unsigned int *>(base + kPreviousMessage) != kAcceptMessage ||
        !FEDatabase || !FEDatabase->CurrentUserProfile || !PageOf(Options().CurrentCategory))
        return;
    const unsigned int flags = *reinterpret_cast<const unsigned int *>(reinterpret_cast<const char *>(FEDatabase) +
                                                                       kDatabaseFlags);
    const bool pause = base[kFromPauseMenu] != 0 && !(flags & 0x48);
    cFEng::mInstance->QueuePackageSwitch(pause ? "Pause_Options.fng" : "Options.fng", pause ? 1 : 0, 0, false);
}

namespace nfs::settingspages
{
bool Owns(int category)
{
    return PageOf(category) != nullptr;
}

void Setup(UIOptionsScreen *screen, int category)
{
    const Page *page = PageOf(category);
    if (!page)
        return;
    if (FEImage *icon = FEngFindImage(screen->PackageName, kPageIcon))
        FEngSetTextureHash(icon, page->icon);
    FEngSetLanguageHash(screen->PackageName, kPageTitle, page->title);
    const int rows = nfs::optionsrows::ProvideRowStrings(screen, page->count);
    nfs::optionsrows::Pack(screen, rows, false, nullptr);
    if (page->build)
    {
        page->build(screen, rows);
        return;
    }
    for (int i = 0; i < page->count && i < rows; ++i)
        screen->AddToggleOption(new SettingRow(page->settings[i]), true);
}

void AddAudioRows(UIOptionsScreen *screen, bool first)
{
    if (first)
    {
        screen->AddToggleOption(new AudioRow(kAudioMaster), true);
        return;
    }
    nfs::sound::RefreshDevices();
    const int before = static_cast<int>(screen->iIndexToAdd) - 1;
    const int rows = nfs::optionsrows::ProvideRowStrings(screen, before + kAudioRows - 1);
    for (int i = kAudioDevice; i < kAudioRows && before + i - kAudioDevice < rows; ++i)
        screen->AddToggleOption(new AudioRow(i), true);
}

void AudioDefaults()
{
    nfs::sound::SetMasterVolume(100);
    nfs::sound::SetBackgroundSound(true);
}

void Defaults(int category)
{
    const Page *page = PageOf(category);
    if (page && page->defaults)
        page->defaults();
    else if (page)
        for (int i = 0; i < page->count; ++i)
            Store(page->settings[i], page->settings[i].values[page->settings[i].fallback]);
}
} // namespace nfs::settingspages
