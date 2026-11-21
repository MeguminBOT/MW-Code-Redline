/**
 * Options screen (original: zFe UIOptionsScreen::Setup 0x545c50, SetupVideo 0x5295f0, OptionsDidNotChange 0x510360,
 * RestoreOriginals 0x510450, RestoreDefaults 0x51d170, the video apply step 0x510300, NotificationMessage 0x545750,
 * and VOScreenResolution's Act 0x50f970 and Draw 0x51b7a0).
 *
 * Changes from the original:
 * - The Video page is the rebuild's own (interface/menus/videopage.h, source/interface/menus/VideoPage.cpp): headings, settings that show only
 *   while another setting calls for them, a description of the highlighted setting, and an Accept that applies and
 *   stays. The screen's change check, apply, revert and defaults go to it, and its messages pass through it first.
 * - The new profile's page (gameplay/progression/profileoptions.h), a category of its own.
 * Both lay their rows out at run time (interface/menus/optionsrows.h).
 */
#include <stddef.h>
#include <string.h>

#include "game/gen/api.h"
#include "core/config.h"
#include "interface/menus/settingspages.h"
#include "engine/rendering/display/display.h"
#include "interface/engine/feng.h"
#include "legacy/link.h"
#include "interface/menus/optionsrows.h"
#include "gameplay/progression/profileoptions.h"
#include "interface/menus/videopage.h"

NFS_REPLACE(0x545c50, UIOptionsScreen::Setup)
NFS_REPLACE(0x5295f0, UIOptionsScreen::SetupVideo)
NFS_REPLACE(0x510360, UIOptionsScreen::OptionsDidNotChange)
NFS_REPLACE(0x510450, UIOptionsScreen::RestoreOriginals)
NFS_REPLACE(0x51d170, UIOptionsScreen::RestoreDefaults)
NFS_REPLACE(0x545750, UIOptionsScreen::NotificationMessage)
NFS_REPLACE(0x50f970, VOScreenResolution::Act)
NFS_REPLACE(0x51b7a0, VOScreenResolution::Draw)

static_assert(sizeof(UIWidgetMenu) == 0x124, "UIWidgetMenu layout");
static_assert(offsetof(UIWidgetMenu, iIndexToAdd) == 0x104, "UIWidgetMenu layout");
static_assert(offsetof(UIWidgetMenu, pCursor) == 0x50 && offsetof(UIWidgetMenu, pTitleName) == 0xB0,
              "UIWidgetMenu layout");
static_assert(offsetof(UIWidgetMenu, vWidgetStartPos) == 0xCC && offsetof(UIWidgetMenu, iMaxWidgetsOnScreen) == 0x10C,
              "UIWidgetMenu layout");
static_assert(offsetof(UIWidgetMenu, pCurrentOption) == 0x34 && offsetof(UIWidgetMenu, pViewTop) == 0x38,
              "UIWidgetMenu layout");
static_assert(offsetof(UIWidgetMenu, bHasScrollBar) == 0x11E && offsetof(UIWidgetMenu, bViewNeedsSync) == 0x11F,
              "UIWidgetMenu layout");
static_assert(offsetof(UIOptionsScreen, NeedsColorCal) == 0x125, "UIOptionsScreen layout");
static_assert(offsetof(UIOptionsScreen, mInitialAutoSaveValue) == 0x126, "UIOptionsScreen layout");
static_assert(offsetof(UIOptionsScreen, OriginalAudioSettings) == 0x130, "UIOptionsScreen layout");
static_assert(offsetof(UIOptionsScreen, WorkingVideoOptions) == 0x144, "UIOptionsScreen layout");
static_assert(offsetof(UIOptionsScreen, ShowAdvancedVideo) == 0x154, "UIOptionsScreen layout");
static_assert(sizeof(FEToggleWidget) == 0x5C, "FEToggleWidget layout");
static_assert(offsetof(FEToggleWidget, bEnabled) == 0x28 && offsetof(FEToggleWidget, pTitle) == 0x2C,
              "FEToggleWidget layout");
static_assert(sizeof(VOScreenResolution) == 0x5C, "widget allocation size");
static_assert(sizeof(cOnlineSettings) == 0x100, "cOnlineSettings layout");
static_assert(offsetof(cFrontendDatabase, OnlineSettings) == 0x134, "cFrontendDatabase layout");
static_assert(offsetof(cFrontendDatabase, OnlineSettings.AppearOffline) == 0x176, "cFrontendDatabase layout");

namespace
{
enum OptionsCategory
{
    OPTIONS_AUDIO = 0,
    OPTIONS_VIDEO = 1,
    OPTIONS_GAMEPLAY = 3,
    OPTIONS_PLAYER = 4,
    OPTIONS_ONLINE = 9,
};

const unsigned int kActionLeft = 0x9120409E;
const unsigned int kActionRight = 0xB5971BF1;

const unsigned int kPlayerOnlyObjects[] = {0x444969FD, 0x444969FE};
const unsigned int kHiddenObjects[] = {0xE54C30BE, 0x8E1BEA84, 0x608BB8C8, 0x444969FD, 0x444969FE};

const unsigned int kPageIcon = 0x08007B4C;
const unsigned int kVideoIconTexture = 0x8A006328;
const unsigned int kPageTitle = 0x42ADB44C;
const unsigned int kVideoString = 0x48478029;          // "video"
const unsigned int kVideoPauseMenuString = 0xD94EA03F; // "VIDEO"
const unsigned int kOnString = 0x417B2604;             // "On"
const unsigned int kOffString = 0x70DFE5C2;            // "Off"

/** The brightness calibration hint (text, two logos and, in the pause menu's package, a backdrop) that the packages
 * show below the original basic view. They show and hide its group themselves when the view is switched, so the
 * members are hidden one by one. */
const unsigned int kBrightnessCalibrationObjects[] = {0x545570C6, 0x7F30EB56, 0x135A0F20, 0xF063760C};

int Clamp(int value, int lowest, int highest)
{
    return value < lowest ? lowest : value > highest ? highest : value;
}

OptionsSettings &CurrentOptions()
{
    return FEDatabase->CurrentUserProfile->TheOptionsSettings;
}

void RedrawWidgets(UIWidgetMenu *menu)
{
    const int widgets = static_cast<int>(menu->iIndexToAdd) - 1;
    for (int i = 1; i <= widgets; ++i)
        menu->GetWidget(i)->Draw();
}

/** A row of the new profile's page (gameplay/progression/profileoptions.h): a value of `count` named choices, an int or a bool. */
class ProfileChoiceOption : public FEToggleWidget
{
  public:
    ProfileChoiceOption(const char *title, int *value, const char *const *names, int count)
        : FEToggleWidget(true), Title(title), Value(value), Flag(nullptr), Names(names), Count(count)
    {
    }
    ProfileChoiceOption(const char *title, bool *flag)
        : FEToggleWidget(true), Title(title), Value(nullptr), Flag(flag), Names(nullptr), Count(2)
    {
    }

    void Act(const char *name, unsigned int action) override
    {
        if (action == kActionLeft || action == kActionRight)
        {
            if (Flag)
                *Flag = !*Flag;
            else
                *Value = Clamp(*Value + (action == kActionRight ? 1 : -1), 0, Count - 1);
        }
        bMovedLastUpdate = true;
        BlinkArrows(action);
        Draw();
    }

    void Draw() override
    {
        FEPrintf(pTitle, "%s", Title);
        if (Flag)
            FEngSetLanguageHash(pData, *Flag ? kOnString : kOffString);
        else
            FEPrintf(pData, "%s", Names[Clamp(*Value, 0, Count - 1)]);
    }

  private:
    const char *Title;
    int *Value;
    bool *Flag;
    const char *const *Names;
    int Count;
};

const char *const kDifficultyNames[nfs::profileoptions::kDifficulties] = {"Easy", "Medium", "Hard"};
const int kNewProfileRows = 8;
} // namespace

/** The original's resolution row: the Video page has its own (source/interface/menus/VideoPage.cpp), so nothing makes one. */
void VOScreenResolution::Act(const char *name, unsigned int action)
{
    bMovedLastUpdate = true;
    BlinkArrows(action);
    Draw();
}

void VOScreenResolution::Draw()
{
    const nfs::display::Settings current = nfs::display::Current();
    FEPrintf(pTitle, "Resolution");
    FEPrintf(pData, "%d x %d", current.width, current.height);
}

/** The new profile's page (gameplay/progression/profileoptions.h), its rows packed so that all show at once where the package has
 * room. */
static void SetupNewProfile(UIOptionsScreen *screen)
{
    if (FEObject *title = FEngFindObject(screen->PackageName, kPageTitle))
        FEPrintf(screen->PackageName, title, "new profile");
    nfs::profileoptions::Settings &s = nfs::profileoptions::Current();
    const int rows = nfs::optionsrows::ProvideRowStrings(screen, kNewProfileRows);
    nfs::optionsrows::Pack(screen, rows, false, nullptr);
    FEToggleWidget *const widgets[kNewProfileRows] = {
        new ProfileChoiceOption("Difficulty", &s.difficulty, kDifficultyNames, nfs::profileoptions::kDifficulties),
        new ProfileChoiceOption("Autosave", &s.autosave),
        new ProfileChoiceOption("Realistic Car Performance", &s.realPerformance),
        new ProfileChoiceOption("Realistic Physics", &s.realPhysics),
        new ProfileChoiceOption("Realistic Damage", &s.realDamage),
        new ProfileChoiceOption("Most Wanted Cars Only", &s.mostWantedCarsOnly),
        new ProfileChoiceOption("Vanilla Bug Fixes", &s.vanillaFixes),
        new ProfileChoiceOption("Speedrunner Mode", &s.speedrunner),
    };
    for (int i = 0; i < kNewProfileRows; ++i)
    {
        if (i < rows)
            screen->AddToggleOption(widgets[i], true);
        else
            delete widgets[i];
    }
}

void UIOptionsScreen::Setup()
{
    ClearWidgets();
    NeedsColorCal = false;
    OptionsSettings &options = CurrentOptions();
    mInitialAutoSaveValue = options.TheGameplaySettings.AutoSaveOn;
    for (unsigned int hash : kHiddenObjects)
        FEngSetInvisible(FEngFindObject(PackageName, hash));

    switch (options.CurrentCategory)
    {
    case OPTIONS_AUDIO:
        nfs::settingspages::AddAudioRows(this, true);
        SetupAudio();
        nfs::settingspages::AddAudioRows(this, false);
        break;
    case OPTIONS_VIDEO:
        SetupVideo();
        break;
    case OPTIONS_GAMEPLAY:
        SetupGameplay();
        break;
    case OPTIONS_PLAYER:
        SetupPlayer();
        for (unsigned int hash : kPlayerOnlyObjects)
            FEngSetVisible(PackageName, hash);
        break;
    case OPTIONS_ONLINE:
        SetupOnline();
        break;
    case nfs::profileoptions::kCategory:
        SetupNewProfile(this);
        break;
    default:
        nfs::settingspages::Setup(this, options.CurrentCategory);
        break;
    }
    SetInitialOption(0);
    if (options.CurrentCategory == OPTIONS_VIDEO)
        nfs::videopage::AfterSetup(this);
}

void UIOptionsScreen::SetupVideo()
{
    static bool dumped;
    if (!dumped && nfs::config::Int("Debug", "FEDump", 0))
    {
        dumped = true;
        nfs::feng::Dump(PackageName);
    }
    FEngSetTextureHash(FEngFindImage(PackageName, kPageIcon), kVideoIconTexture);
    FEngSetLanguageHash(PackageName, kPageTitle, mCalledFromPauseMenu ? kVideoPauseMenuString : kVideoString);
    // One view: the hidden Basic/Advanced button's message still flips this and sets the page up again.
    ShowAdvancedVideo = true;
    for (unsigned int hash : kBrightnessCalibrationObjects)
        FEngSetInvisible(FEngFindObject(PackageName, hash));
    nfs::videopage::Setup(this);
}

/** The screen's messages. On the Video page they pass through the page first (an Accept that applies and stays, the
 * countdown's "keep") and, after the screen's own handling, the page looks after its list. */
void UIOptionsScreen::NotificationMessage(unsigned int message, FEObject *object, unsigned int param1,
                                          unsigned int param2)
{
    typedef void(__thiscall * Handler)(UIOptionsScreen *, unsigned int, FEObject *, unsigned int, unsigned int);
    const Handler original = NFS_ORIGINAL(0x545750, Handler);
    const bool video = FEDatabase && FEDatabase->CurrentUserProfile && CurrentOptions().CurrentCategory == OPTIONS_VIDEO;
    if (!video)
    {
        original(this, message, object, param1, param2);
        return;
    }
    if (!nfs::videopage::BeforeMessage(this, message))
        original(this, message, object, param1, param2);
    nfs::videopage::AfterMessage(this, message);
}

bool UIOptionsScreen::OptionsDidNotChange()
{
    OptionsSettings &options = CurrentOptions();
    switch (options.CurrentCategory)
    {
    case OPTIONS_AUDIO:
        return options.TheAudioSettings == *OriginalAudioSettings;
    case OPTIONS_VIDEO:
        return nfs::videopage::Unchanged();
    case OPTIONS_GAMEPLAY:
        return options.TheGameplaySettings == *OriginalGameplaySettings;
    case OPTIONS_PLAYER:
        return options.ThePlayerSettings[PlayerToEdit] == *OriginalPlayerSettings;
    case OPTIONS_ONLINE:
        return FEDatabase->OnlineSettings == *OriginalOnlineSettings &&
               mTempAppearOffline == FEDatabase->OnlineSettings.AppearOffline;
    case nfs::profileoptions::kCategory:
        // Leaving the page keeps what it shows (nfs::profileoptions::Finish): there is nothing to confirm or revert.
        return true;
    default:
        // The rebuild's own pages: every change is in use and saved at once.
        return nfs::settingspages::Owns(options.CurrentCategory);
    }
}

void UIOptionsScreen::RestoreOriginals()
{
    OptionsSettings &options = CurrentOptions();
    switch (options.CurrentCategory)
    {
    case OPTIONS_AUDIO:
        options.TheAudioSettings = *OriginalAudioSettings;
        break;
    case OPTIONS_VIDEO:
        nfs::videopage::Revert();
        break;
    case OPTIONS_GAMEPLAY:
        options.TheGameplaySettings = *OriginalGameplaySettings;
        break;
    case OPTIONS_PLAYER:
        options.ThePlayerSettings[PlayerToEdit] = *OriginalPlayerSettings;
        break;
    case OPTIONS_ONLINE:
        FEDatabase->OnlineSettings = *OriginalOnlineSettings;
        break;
    }
}

void UIOptionsScreen::RestoreDefaults()
{
    OptionsSettings &options = CurrentOptions();
    switch (options.CurrentCategory)
    {
    case OPTIONS_AUDIO:
        options.TheAudioSettings.Default();
        nfs::settingspages::AudioDefaults();
        break;
    case OPTIONS_VIDEO:
        // The page shows the defaults; Accept applies them (the original applied them at once).
        nfs::videopage::Defaults();
        return;
    case OPTIONS_GAMEPLAY:
    {
        const bool autosave = options.TheGameplaySettings.AutoSaveOn;
        options.TheGameplaySettings.Default();
        if (!ShouldShowAutoSave())
            options.TheGameplaySettings.AutoSaveOn = autosave;
        break;
    }
    case OPTIONS_PLAYER:
        options.ThePlayerSettings[PlayerToEdit].DefaultFromOptionsScreen();
        break;
    case OPTIONS_ONLINE:
        FEDatabase->OnlineSettings.RestoreDefaultSettings();
        mTempAppearOffline = FEDatabase->OnlineSettings.AppearOffline;
        break;
    case nfs::profileoptions::kCategory:
        nfs::profileoptions::Defaults();
        break;
    default:
        nfs::settingspages::Defaults(options.CurrentCategory);
        break;
    }
    RedrawWidgets(this);
}

void UIOptionsScreen::ApplyVideoOptions()
{
    nfs::videopage::Apply();
}
