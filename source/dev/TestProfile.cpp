/**
 * Test profiles and scripted text entry. Not in the original game.
 *
 * A profile named in profiles.ini's [UnlockAll] (NAME = 1) has everything unlocked while it is loaded: the game's own
 * unlock-everything switches (the UnlockAllThings global every unlock check ORs in, and the profile's byte at +0x90dc
 * the quick race and career checks read), and at least kTestCash in its career. A career it starts skips the prologue
 * (the game's debug switches SkipCareerIntro and SkipDDayRaces, which CareerSettings::StartNewCareer 0x5a3950 reads:
 * the career starts at the first blacklist rival, in the safehouse) and with kTestCash too. Loading another profile
 * puts the globals back.
 *
 * Under -dev the Developer profile (source/core/HeadlessSaves.cpp) is such a profile whatever profiles.ini says.
 *
 * -set Debug.FEText=TEXT: the next text the front end asks for (a profile's name) starts as TEXT instead of the
 * game's default (KeyboardEditString::StartCapture 0x57f1d0), so headless runs can name a profile.
 */
#include <windows.h>

#include "game/gen/api.h"
#include "core/config.h"
#include "legacy/link.h"
#include "core/log.h"
#include "gameplay/progression/profileoptions.h"
#include "dev/testprofile.h"
#include "dev/dev.h"

NFS_REPLACE(0x57f1d0, ScriptedText::StartCapture)
NFS_REPLACE(0x5a3950, TestCareer::StartNewCareer)
NFS_REPLACE(0x57f570, TestCareer::ResumeCareer)

namespace
{
/** UserProfile: the unlock-everything byte and the career's cash (CareerSettings at +0xa8, its cash at +0xc). */
const int kProfileUnlockAll = 0x90dc;
const int kProfileCash = 0xb4;
const int kCareerCash = 0xc;
const int kTestCash = 10000000;
const int kTextLength = 32;

bool g_setUnlockAll;
bool g_developer;
char g_text[kTextLength];
int g_textState = -1;
} // namespace

namespace nfs::testprofile
{
void Apply(void *profile)
{
    unsigned char *p = static_cast<unsigned char *>(profile);
    const char *name = reinterpret_cast<const char *>(p);
    g_developer = nfs::dev::Enabled() && !lstrcmpiA(name, "Developer");
    const bool test = name[0] && (g_developer || nfs::config::ProfileInt("UnlockAll", name, 0) != 0);
    if (!test)
    {
        if (g_setUnlockAll)
        {
            UnlockAllThings = false;
            SkipCareerIntro = 0;
            SkipDDayRaces = 0;
        }
        g_setUnlockAll = false;
        return;
    }
    UnlockAllThings = true;
    SkipCareerIntro = 1;
    SkipDDayRaces = 1;
    g_setUnlockAll = true;
    p[kProfileUnlockAll] = 1;
    int &cash = *reinterpret_cast<int *>(p + kProfileCash);
    if (cash < kTestCash)
        cash = kTestCash;
    nfs::Log("test profile: %s has everything unlocked", name);
}
} // namespace nfs::testprofile

/** CareerSettings (zFe2). */
struct TestCareer
{
    void StartNewCareer(bool fromFrontEnd);
    void ResumeCareer();
};

namespace
{
TestCareer *g_heldCareer;
bool g_heldNew;
}

/** The memory card's way out after Start Career made the player name an alias (UIMemcardBase::ExitComplete 0x562ff0): that
 * career is already set up, so the game resumes it. */
void TestCareer::ResumeCareer()
{
    if (nfs::profileoptions::HoldCareer())
    {
        g_heldCareer = this;
        g_heldNew = false;
        return;
    }
    NFS_ORIGINAL(0x57f570, void(__thiscall *)(TestCareer *))(this);
}

void TestCareer::StartNewCareer(bool fromFrontEnd)
{
    // Start Career, when it made the player name an alias: the alias's options page comes first (gameplay/progression/profileoptions.h).
    if (fromFrontEnd && nfs::profileoptions::HoldCareer())
    {
        g_heldCareer = this;
        g_heldNew = true;
        return;
    }
    NFS_ORIGINAL(0x5a3950, void(__thiscall *)(TestCareer *, bool))(this, fromFrontEnd);
    if (!g_setUnlockAll)
        return;
    int &cash = *reinterpret_cast<int *>(reinterpret_cast<unsigned char *>(this) + kCareerCash);
    if (cash < kTestCash)
        cash = kTestCash;
    nfs::Log("test profile: the new career has %d", cash);
}

namespace nfs::testprofile
{
void StartHeldCareer()
{
    if (g_heldCareer)
    {
        TestCareer *career = g_heldCareer;
        g_heldCareer = nullptr;
        if (g_heldNew)
            career->StartNewCareer(true);
        else
            career->ResumeCareer();
    }
}
} // namespace nfs::testprofile

/** KeyboardEditString (zFe2). */
struct ScriptedText
{
    void StartCapture(void *input, unsigned int type, const char *text, unsigned int length);
};

void ScriptedText::StartCapture(void *input, unsigned int type, const char *text, unsigned int length)
{
    typedef void(__thiscall * Original)(ScriptedText *, void *, unsigned int, const char *, unsigned int);
    if (g_textState < 0)
    {
        nfs::config::String("Debug", "FEText", "", g_text, sizeof(g_text));
        g_textState = g_text[0] ? 1 : 0;
    }
    if (g_textState == 1)
    {
        g_textState = 0;
        if (length && length < sizeof(g_text))
            g_text[length] = 0;
        nfs::Log("fescript: text entry starts as %s (at most %u characters)", g_text, length);
        text = g_text;
    }
    NFS_ORIGINAL(0x57f1d0, Original)(this, input, type, text, length);
}

bool nfs::testprofile::Developer()
{
    return g_developer;
}
