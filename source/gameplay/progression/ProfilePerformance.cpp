/**
 * A new profile's first save (UIMemcardBase::DoSaveFlow 0x544370, step 8: the profile takes the name the memory card
 * screen holds and is saved). Not in the original game:
 * - the game's "Enable autosave?" question after it is not asked: gMemcardSetup bit 15 has
 *   UIMemcardBase::InitComplete (0x562c30, state 0xa0) turn autosave on by itself;
 * - the profile starts with the game's standard car performance, and the new profile's options page
 *   (gameplay/progression/profileoptions.h) opens next, where autosave, realistic car performance and the rest are chosen.
 *
 * A profile is new at step 8 when it has no name yet (the first profile of an install), or when the name the memory
 * card screen holds is not the name the loaded profile has (the Alias Manager makes a new profile from the profile
 * already loaded, so that one is named before step 8 and only the screen's name differs), or when this session never
 * loaded or made a profile of that name (nfs::profileoptions::IsKnown; the profile can hold the screen's name and be
 * named already by then).
 *
 * Step 8 also saves a profile that already has its name (a save from the menus): that is left to the game, so a
 * profile's choices are never touched after it was made. An earlier version asked its realistic performance question
 * at every step 8, and a save of an existing profile answered it for that profile.
 */
#include <windows.h>

#include "game/gen/api.h"
#include "legacy/link.h"
#include "core/log.h"
#include "gameplay/progression/profileoptions.h"
#include "gameplay/vehicles/realism.h"

NFS_REPLACE(0x544370, ProfilePerformance::DoSaveFlow)

namespace
{
/** DoSaveFlow's step that names the profile and saves it, and the memory card screen's name it takes. */
const int kSaveNamed = 8;
const int kScreenName = 0x40;
const unsigned int kAutoSaveWithoutAsking = 0x8000;

unsigned int &MemcardSetup()
{
    return *reinterpret_cast<unsigned int *>(&gMemcardSetup);
}
} // namespace

/** UIMemcardBase (zFe). */
struct ProfilePerformance
{
    void DoSaveFlow(int step);
};

void ProfilePerformance::DoSaveFlow(int step)
{
    typedef void(__thiscall * Original)(ProfilePerformance *, int);
    UserProfile *profile = FEDatabase ? FEDatabase->CurrentUserProfile : nullptr;
    const char *screenName = reinterpret_cast<const char *>(this) + kScreenName;
    // A new profile is unnamed until this step gives it one (cFrontendDatabase::DefaultProfile left it unnamed), or,
    // when made from a loaded profile (Alias Manager), is named already but the screen holds a different name, or
    // already holds the screen's name too (a log showed both 'realtes' with m_bNamed 1) but this session never loaded
    // or made a profile of that name (nfs::profileoptions::IsKnown).
    const bool known = screenName[0] && nfs::profileoptions::IsKnown(screenName);
    const bool made = step == kSaveNamed && profile &&
                      (!profile->m_bNamed ||
                       (screenName[0] && lstrcmpiA(screenName, profile->m_aProfileName) != 0) ||
                       (screenName[0] && !known));
    if (step == kSaveNamed && profile)
        nfs::Log("profile: step %d, screen name '%s', profile '%s', named %d, known %d -> %s", step, screenName,
                 profile->m_aProfileName, profile->m_bNamed ? 1 : 0, known ? 1 : 0,
                 made ? "new profile" : "existing profile");
    if (made)
    {
        MemcardSetup() |= kAutoSaveWithoutAsking;
        // Before the save this step makes: until the page has been taken, no save writes the profile's settings in.
        nfs::profileoptions::Begin(screenName);
    }
    NFS_ORIGINAL(0x544370, Original)(this, step);
    if (step != kSaveNamed || !profile)
        return;
    if (!made)
    {
        nfs::Log("profile: %s saved; its choices stay as they are", profile->m_aProfileName);
        return;
    }
    nfs::Log("profile: %s made; its options page comes next", profile->m_aProfileName);
    nfs::realism::ChooseForProfile(profile->m_aProfileName, false);
    nfs::profileoptions::Begin(profile->m_aProfileName);
}