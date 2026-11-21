/**
 * -dev starts in the Developer profile (docs/DEV.md): past the title screen, the profile list's Developer row chosen and
 * loaded, as a player would, reading the screen through the test channel's readers (dev/testbridge.h). The profile is
 * the dev saves' (source/core/HeadlessSaves.cpp) and has everything unlocked (source/dev/TestProfile.cpp).
 * -set Dev.AutoLoad=0 keeps the title and the list. Not in the original game.
 */
#include <windows.h>

#include "core/config.h"
#include "core/log.h"
#include "dev/dev.h"
#include "dev/devprofile.h"
#include "dev/fescript.h"
#include "dev/testbridge.h"

namespace
{
const char kTitle[] = "MW_LS_Splash.fng";
const char kList[] = "MC_List.fng";
const char kMainMenu[] = "MainMenu.fng";
const char kDeveloper[] = "Developer";
const DWORD kStepMs = 700;
const DWORD kGiveUpMs = 120000;
const int kMaxMoves = 12;

enum State
{
    kOff,
    kWaiting,
    kDone,
};

State g_state;
DWORD g_started;
DWORD g_last;
int g_moves;
} // namespace

void nfs::devprofile::Update()
{
    if (g_state == kDone || g_state == kOff && g_started)
        return;
    if (!g_started)
    {
        g_started = GetTickCount();
        if (!nfs::dev::Enabled() || !nfs::config::Int("Dev", "AutoLoad", 1))
        {
            g_state = kOff;
            return;
        }
        g_state = kWaiting;
        nfs::Log("dev: loading the Developer profile");
    }
    const DWORD now = GetTickCount();
    if (now - g_started > kGiveUpMs)
    {
        nfs::Log("dev: the Developer profile did not load in %u s; left to the player", kGiveUpMs / 1000);
        g_state = kDone;
        return;
    }
    if (now - g_last < kStepMs)
        return;
    const char *package = nfs::testbridge::ControlPackage();
    if (!lstrcmpiA(package, kMainMenu))
    {
        nfs::Log("dev: in the main menu");
        g_state = kDone;
        return;
    }
    if (!lstrcmpiA(package, kTitle))
    {
        nfs::fescript::Press("accept");
        g_last = now;
        return;
    }
    if (!lstrcmpiA(package, kList))
    {
        char chosen[64];
        if (!nfs::testbridge::Chosen(chosen, sizeof(chosen)))
            return;
        if (!lstrcmpiA(chosen, kDeveloper))
            nfs::fescript::Press("accept");
        else if (g_moves++ < kMaxMoves)
            nfs::fescript::Press("down");
        else
        {
            nfs::Log("dev: no Developer row in the profile list; left to the player");
            g_state = kDone;
        }
        g_last = now;
    }
}
