/**
 * The new profile's options (gameplay/progression/profileoptions.h). Not in the original game.
 *
 * Once a new profile's first save is done (source/gameplay/progression/ProfilePerformance.cpp calls Begin), the next package switch the
 * front end queues to a menu (cFEng::QueuePackageSwitch 0x525940; the memory card's own screens, dialogs, the keyboard
 * and loading screens pass) goes to Options.fng instead, its category set to the page's (UIOptionsScreen::Setup,
 * source/interface/menus/UIOptionsScreen.cpp, builds the rows). Leaving the page queues the switch back to the options menu
 * (MainMenu_Sub.fng); that one is taken over too: the settings go to the profile and the front end goes on to the menu
 * it was going to before the page.
 *
 * Each profile's settings live in its save (gameplay/progression/profileoptions.h: Loaded and Saving, from UserProfile::LoadFromBuffer
 * and SaveToBuffer) and, for the session, in a list by profile name; profiles.ini only answers for a save made before
 * that, and for a new profile until its next save.
 */
#include <string.h>
#include <windows.h>

#include "game/gen/api.h"
#include "core/config.h"
#include "legacy/link.h"
#include "core/log.h"
#include "gameplay/progression/preferences.h"
#include "gameplay/progression/profileoptions.h"
#include "dev/proving.h"
#include "gameplay/vehicles/realism.h"
#include "dev/testprofile.h"

namespace
{
const char kOptionsPackage[] = "Options.fng";
const char kOptionsMenuPackage[] = "MainMenu_Sub.fng";
const char kAliasManagerPackage[] = "MC_ProfileManager.fng";
/** Packages the page does not take the place of: the memory card's, dialogs, the keyboard and loading screens. */
const char *const kPassingPackages[] = {"MC_", "InGame_MC_", "Keyboard", "Dialog", "GenericDialog", "InGameDialog",
                                        "Autosave_Overlay", "Loading", "LOADING", "PC_Loading", "WS_Loading",
                                        "FadeScreen", "LS_", "MW_LS_"};

struct State
{
    nfs::profileoptions::Settings settings;
    char profile[32];
    char returnTo[64];
    bool pending;
    bool open;
    bool heldCareer;
};

State g_state;

/** The settings of the profiles loaded or made this session, as their saves (or the page) have them. */
struct Known
{
    char name[32];
    nfs::profileoptions::Settings settings;
};

const int kMaxKnown = 32;
Known g_known[kMaxKnown];
int g_knownCount;
/** Bumped whenever a profile's settings are learnt or forgotten (Changes). */
unsigned int g_changes;

/** The save block (gameplay/progression/profileoptions.h). */
const unsigned char kBlockMagic[4] = {'N', 'F', 'S', 'R'};
const unsigned char kBlockVersion = 1;
const int kBlockVersionAt = 4;
const int kBlockFlagsAt = 5;
const int kBlockDifficultyAt = 6;
enum BlockFlags
{
    kRealPerformance = 1,
    kRealPhysics = 2,
    kRealDamage = 4,
    kMostWantedCarsOnly = 8,
    kVanillaFixes = 16,
    kSpeedrunner = 32,
};

typedef void(__thiscall *QueueSwitch)(cFEng *, const char *, int, unsigned int, bool);

bool StartsWith(const char *text, const char *prefix)
{
    return _strnicmp(text, prefix, strlen(prefix)) == 0;
}

bool Passes(const char *package)
{
    for (const char *prefix : kPassingPackages)
        if (StartsWith(package, prefix))
            return true;
    return false;
}

void Section(const char *profile, char *out, int size)
{
    wsprintfA(out, "Profile %s", profile);
    out[size - 1] = 0;
}

void WriteInt(const char *section, const char *key, int value)
{
    char text[16];
    wsprintfA(text, "%d", value);
    nfs::config::ProfileWriteString(section, key, text);
}

OptionsSettings &Options()
{
    return FEDatabase->CurrentUserProfile->TheOptionsSettings;
}

Known *Find(const char *name)
{
    for (int i = 0; i < g_knownCount; ++i)
        if (lstrcmpiA(g_known[i].name, name) == 0)
            return &g_known[i];
    return nullptr;
}

void Remember(const char *name, const nfs::profileoptions::Settings &settings)
{
    if (!name || !*name)
        return;
    Known *k = Find(name);
    if (!k)
    {
        k = &g_known[g_knownCount < kMaxKnown ? g_knownCount++ : kMaxKnown - 1];
        lstrcpynA(k->name, name, sizeof(k->name));
    }
    k->settings = settings;
    ++g_changes;
}

/** A profile's settings as profiles.ini has them (a save made before the saves held them). */
nfs::profileoptions::Settings FromIni(const char *profile)
{
    nfs::profileoptions::Settings saved = g_state.settings;
    nfs::profileoptions::Defaults();
    nfs::profileoptions::Settings s = g_state.settings;
    g_state.settings = saved;
    if (!profile || !*profile)
        return s;
    char value[16];
    nfs::config::ProfileString("Profiles", profile, "standard", value, sizeof(value));
    s.realPerformance = lstrcmpiA(value, "real") == 0;
    char section[64];
    Section(profile, section, sizeof(section));
    s.difficulty = nfs::config::ProfileInt(section, "Difficulty", s.difficulty);
    s.realPhysics = nfs::config::ProfileInt(section, "RealisticPhysics", 0) != 0;
    s.realDamage = nfs::config::ProfileInt(section, "RealisticDamage", 0) != 0;
    s.mostWantedCarsOnly = nfs::config::ProfileInt(section, "MostWantedCarsOnly", 0) != 0;
    s.vanillaFixes = nfs::config::ProfileInt(section, "VanillaBugFixes", 0) != 0;
    s.speedrunner = nfs::config::ProfileInt(section, "SpeedrunnerMode", 0) != 0;
    return s;
}

const char *Yes(bool on)
{
    return on ? "on" : "off";
}

void LogSettings(const char *what, const char *profile, const nfs::profileoptions::Settings &s)
{
    nfs::Log("profile options: %s %s: difficulty %d, realistic performance %s, physics %s, damage %s, Most Wanted cars "
             "only %s, vanilla bug fixes %s, speedrunner mode %s",
             profile, what, s.difficulty, Yes(s.realPerformance), Yes(s.realPhysics), Yes(s.realDamage),
             Yes(s.mostWantedCarsOnly), Yes(s.vanillaFixes), Yes(s.speedrunner));
}
} // namespace

/** cFEng (zFe). */
struct PackageSwitch
{
    void Queue(const char *package, int a, unsigned int b, bool c);
};

NFS_REPLACE(0x525940, PackageSwitch::Queue)

void PackageSwitch::Queue(const char *package, int a, unsigned int b, bool c)
{
    cFEng *self = reinterpret_cast<cFEng *>(this);
    static bool debugChecked;
    if (!debugChecked)
    {
        // -set Debug.ProfileOptions=NAME: the page opens once for that profile (a test profile), without making one.
        debugChecked = true;
        char name[32];
        nfs::config::String("Debug", "ProfileOptions", "", name, sizeof(name));
        if (name[0])
            nfs::profileoptions::Begin(name);
    }
    if (const char *redirect = nfs::proving::PackageFor(package))
        package = redirect;
    if (package)
        nfs::Log("profile options: switch to %s%s", package, g_state.pending ? " (page pending)" : "");
    // A named profile this session never loaded or chose for (an alias the player made and chose not to save): its page
    // comes all the same.
    if (package && !g_state.pending && !g_state.open && !Passes(package) && FEDatabase && FEDatabase->CurrentUserProfile)
    {
        const UserProfile *current = FEDatabase->CurrentUserProfile;
        if (current->m_bNamed && current->m_aProfileName[0] && !nfs::profileoptions::IsKnown(current->m_aProfileName))
            nfs::profileoptions::Begin(current->m_aProfileName);
    }
    // The Alias Manager comes back to its own screen after making an alias: the page comes then, not when the player leaves it.
    if (package && g_state.pending && (!Passes(package) || _stricmp(package, kAliasManagerPackage) == 0))
    {
        g_state.pending = false;
        g_state.open = true;
        lstrcpynA(g_state.returnTo, package, sizeof(g_state.returnTo));
        Options().CurrentCategory = nfs::profileoptions::kCategory;
        nfs::Log("profile options: %s's page in place of %s", g_state.profile, package);
        NFS_ORIGINAL(0x525940, QueueSwitch)(self, kOptionsPackage, a, b, c);
        return;
    }
    if (package && g_state.open && Options().CurrentCategory == nfs::profileoptions::kCategory &&
        _stricmp(package, kOptionsMenuPackage) == 0)
    {
        g_state.open = false;
        nfs::profileoptions::Finish();
        nfs::prefs::StoreFromProfile(FEDatabase->CurrentUserProfile);
        // The career the player asked for when the game made them name an alias: it goes on from here, in place of the menu.
        if (g_state.heldCareer)
        {
            g_state.heldCareer = false;
            nfs::testprofile::StartHeldCareer();
            return;
        }
        NFS_ORIGINAL(0x525940, QueueSwitch)(self, g_state.returnTo, a, b, c);
        return;
    }
    // Leaving an options page (back to the options menu, the online menu or the pause menu): its audio and gameplay
    // options go to speed.ini (gameplay/progression/preferences.h).
    if (package && (_stricmp(package, kOptionsMenuPackage) == 0 || _stricmp(package, "Pause_Main.fng") == 0 ||
                    _stricmp(package, "OL_MAIN.fng") == 0))
        nfs::prefs::StoreFromProfile(FEDatabase->CurrentUserProfile);
    NFS_ORIGINAL(0x525940, QueueSwitch)(self, package, a, b, c);
}

namespace nfs::profileoptions
{
Settings &Current()
{
    return g_state.settings;
}

void Defaults()
{
    Settings &s = g_state.settings;
    s.difficulty = kMedium;
    s.autosave = true;
    s.realPerformance = false;
    s.realPhysics = false;
    s.realDamage = false;
    s.mostWantedCarsOnly = false;
    s.vanillaFixes = false;
    s.speedrunner = false;
}

void Begin(const char *profile)
{
    // A new profile: whatever this session knew of a profile of that name is not its.
    for (int i = 0; profile && i < g_knownCount; ++i)
        if (lstrcmpiA(g_known[i].name, profile) == 0)
            g_known[i] = g_known[--g_knownCount];
    ++g_changes;
    Defaults();
    lstrcpynA(g_state.profile, profile ? profile : "", sizeof(g_state.profile));
    g_state.pending = g_state.profile[0] != 0;
    g_state.open = false;
}

void Finish()
{
    const Settings &s = g_state.settings;
    OptionsSettings &options = Options();
    options.CurrentCategory = 0;
    options.TheGameplaySettings.AutoSaveOn = s.autosave;
    FEDatabase->bIsOptionsDirty = true;
    // The profile's next save takes them in; profiles.ini keeps them until then.
    Remember(g_state.profile, s);
    nfs::realism::ChooseForProfile(g_state.profile, s.realPerformance);
    char section[64];
    Section(g_state.profile, section, sizeof(section));
    WriteInt(section, "Difficulty", s.difficulty);
    WriteInt(section, "RealisticPhysics", s.realPhysics);
    WriteInt(section, "RealisticDamage", s.realDamage);
    WriteInt(section, "MostWantedCarsOnly", s.mostWantedCarsOnly);
    WriteInt(section, "VanillaBugFixes", s.vanillaFixes);
    WriteInt(section, "SpeedrunnerMode", s.speedrunner);
    nfs::Log("profile options: %s: difficulty %d, autosave %d, realistic performance %d, physics %d, damage %d, Most "
             "Wanted cars only %d, vanilla bug fixes %d, speedrunner mode %d",
             g_state.profile, s.difficulty, s.autosave, s.realPerformance, s.realPhysics, s.realDamage,
             s.mostWantedCarsOnly, s.vanillaFixes, s.speedrunner);
}

bool HoldCareer()
{
    if (!g_state.pending && !g_state.open)
        return false;
    g_state.heldCareer = true;
    nfs::Log("profile options: the new career waits for %s's page", g_state.profile);
    return true;
}

unsigned int Changes()
{
    return g_changes;
}

bool IsKnown(const char *profile)
{
    return profile && *profile && Find(profile) != nullptr;
}

Settings ForProfile(const char *profile)
{
    if (const Known *k = profile ? Find(profile) : nullptr)
        return k->settings;
    return FromIni(profile);
}

void Loaded(const char *profile, const unsigned char *data)
{
    if (!profile || !*profile || !data)
        return;
    Settings s;
    if (memcmp(data, kBlockMagic, sizeof(kBlockMagic)) == 0 && data[kBlockVersionAt] == kBlockVersion)
    {
        Settings saved = g_state.settings;
        Defaults();
        s = g_state.settings;
        g_state.settings = saved;
        const unsigned char flags = data[kBlockFlagsAt];
        s.realPerformance = (flags & kRealPerformance) != 0;
        s.realPhysics = (flags & kRealPhysics) != 0;
        s.realDamage = (flags & kRealDamage) != 0;
        s.mostWantedCarsOnly = (flags & kMostWantedCarsOnly) != 0;
        s.vanillaFixes = (flags & kVanillaFixes) != 0;
        s.speedrunner = (flags & kSpeedrunner) != 0;
        s.difficulty = data[kBlockDifficultyAt] < kDifficulties ? data[kBlockDifficultyAt] : kMedium;
        LogSettings("loaded from its save", profile, s);
    }
    else
    {
        s = FromIni(profile);
        LogSettings("loaded from a save without them (profiles.ini's until its next save)", profile, s);
    }
    Remember(profile, s);
}

void Saving(const char *profile, unsigned char *data)
{
    if (!profile || !*profile || !data)
        return;
    // The save that names a new profile comes before its page: profiles.ini answers for it until the next one.
    if (g_state.pending || g_state.open)
    {
        nfs::Log("profile options: %s saved before its page: its settings go in with its next save", profile);
        return;
    }
    const Settings s = ForProfile(profile);
    memset(data, 0, kSaveBlockSize);
    memcpy(data, kBlockMagic, sizeof(kBlockMagic));
    data[kBlockVersionAt] = kBlockVersion;
    data[kBlockFlagsAt] = static_cast<unsigned char>(
        (s.realPerformance ? kRealPerformance : 0) | (s.realPhysics ? kRealPhysics : 0) |
        (s.realDamage ? kRealDamage : 0) | (s.mostWantedCarsOnly ? kMostWantedCarsOnly : 0) |
        (s.vanillaFixes ? kVanillaFixes : 0) | (s.speedrunner ? kSpeedrunner : 0));
    data[kBlockDifficultyAt] = static_cast<unsigned char>(s.difficulty);
    Remember(profile, s);
    LogSettings("written into its save", profile, s);
}
} // namespace nfs::profileoptions
