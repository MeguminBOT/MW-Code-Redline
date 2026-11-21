/**
 * Main menu (original: zFe UIMain::Setup 0x544e50, with the option constructors it inlines).
 *
 * Change from the original: no LAN Play (MainLAN) or Online (MainOnline) entries. Online play went through EA's
 * long-closed servers and LAN play through the same networking code; a peer-to-peer server browser is to replace both.
 * The options screen only lists its Online page while an online session runs, so that page is unreachable too.
 */
#include <stddef.h>

#include "game/gen/api.h"
#include "legacy/link.h"
#include "dev/proving.h"

NFS_REPLACE(0x544e50, UIMain::Setup)

static_assert(sizeof(MenuScreen) == 0x2C, "MenuScreen layout");
static_assert(sizeof(IconOption) == 0x4C, "IconOption layout");
static_assert(sizeof(IconScroller) == 0xFC, "IconScroller layout");
static_assert(offsetof(IconScrollerMenu, bFadeInIconsImmediately) == 0x129, "IconScrollerMenu layout");
static_assert(sizeof(UIMain) == 0x14C, "UIMain layout");
static_assert(offsetof(cFrontendDatabase, FEGameMode) == 0x12C, "cFrontendDatabase layout");

namespace
{
const unsigned int kTitleObject = 0xB71B576D;
const unsigned int kMainMenuString = 0xB24AAE58; // "main menu"
const unsigned int kAcceptMessage = 0x0C407210;
const unsigned int kGameModeAliasManager = 0x80;
} // namespace

MainCareer::MainCareer() : IconOption(0x03704F3D, 0x5815A2B5 /* "Career" */, 0) {}

Challenge::Challenge() : IconOption(0x9A962438, 0xCC8CB746 /* "Challenge Series" */, 0)
{
    bReactImmediately = true;
}

MainQuickRace::MainQuickRace() : IconOption(0x4E6FBB02, 0x54020A7A /* "Quick Race" */, 0) {}

MainCustomize::MainCustomize() : IconOption(0xB0C46023, 0x1AFD5BE6 /* "My Cars" */, 0) {}

MainProfileManager::MainProfileManager() : IconOption(0x6B303856, 0xBCB18F38 /* "Alias Manager" */, 0) {}

/** The original's (0x51aea0): accepting the entry opens the alias manager (FEGameMode bit 0x80). */
void MainProfileManager::React(const char *, unsigned int message, class FEObject *, unsigned int, unsigned int)
{
    if (message == kAcceptMessage)
        FEDatabase->FEGameMode |= kGameModeAliasManager;
}

MainOptions::MainOptions() : IconOption(0x3058FE37, 0x19A8C0AF /* "Options" */, 0) {}

namespace
{
/** Not in the original: the proving ground (dev/proving.h), after the game's own entries. */
class ProvingGroundOption : public IconOption
{
  public:
    ProvingGroundOption() : IconOption(0x74CE8C0B /* UI_ICON_DEBUG */, 0x5D51E25A /* "DEBUG Physics" */, 0) {}
    void React(const char *, unsigned int message, class FEObject *, unsigned int, unsigned int) override
    {
        if (message == kAcceptMessage)
            nfs::proving::StartFromMenu();
    }
};
} // namespace

void UIMain::Setup()
{
    FEDatabase->FEGameMode = 0;
    FEDatabase->SetPlayersJoystickPort(0, -1);
    if (GetMikeMannBuild())
    {
        // Press-demo build: a reduced menu with everything unlocked.
        AddOption(new Challenge());
        AddOption(new MainQuickRace());
        AddOption(new MainCustomize());
        AddOption(new MainOptions());
        UnlockAllThings = true;
    }
    else
    {
        AddOption(new MainCareer());
        AddOption(new Challenge());
        AddOption(new MainQuickRace());
        AddOption(new MainCustomize());
        if (IsMemcardEnabled)
            AddOption(new MainProfileManager());
        AddOption(new MainOptions());
    }
    if (nfs::proving::MenuShown())
        AddOption(new ProvingGroundOption());

    FEngSetLanguageHash(PackageName, kTitleObject, kMainMenuString);
    const unsigned char last_button = FEngGetLastButton(PackageName);
    if (bFadeInIconsImmediately)
    {
        Options.bDelayUpdate = false;
        Options.bFadingIn = true;
        Options.bFadingOut = false;
        Options.fCurFadeTime = 0.0f;
    }
    Options.SetInitialPos(last_button);
    RefreshHeader();
    UpdateProfileData();
}
