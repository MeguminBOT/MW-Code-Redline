#pragma once

/**
 * The new profile's options (source/gameplay/progression/ProfileOptions.cpp): a page of the options screen, laid out like the Video page,
 * that a new profile gets once its first save is done, in place of the game's "Enable autosave?" question and the
 * rebuild's earlier "Realistic car performance?" one. Not in the original game.
 *
 * Rows: Difficulty, Autosave, Realistic Car Performance, Realistic Physics, Realistic Damage, Most Wanted Cars Only,
 * Vanilla Bug Fixes, Speedrunner Mode. Autosave, Realistic Car Performance and Vanilla Bug Fixes (gameplay/vanillafixes.h)
 * work; the rest are stored for the profile and do nothing yet.
 *
 * The profile's save holds them (all but autosave, which is the game's own option): the first kSaveBlockSize bytes of
 * the profile's data, which the game leaves zero and never reads (UserProfile::SaveToBuffer 0x58fd10 zeroes them,
 * LoadFromBuffer 0x5acb70 and the profile's MD5 start after them; the memory card's signature of the data covers
 * them, so a save stays valid). "NFSR", a version (1), the flags (realistic performance, physics, damage, Most Wanted
 * cars only, vanilla bug fixes, speedrunner mode: bits 0 to 5), the difficulty, then zeros. A save made before has
 * none: its settings come from profiles.ini ([Profiles] <name> = real or standard, [Profile <name>]) until its next
 * save writes them in. A new profile's settings are written in from its first save after the page: the page opens
 * after the save that names it, and profiles.ini keeps them meanwhile.
 */
namespace nfs::profileoptions
{
/** OptionsSettings::CurrentCategory of the page (the game's own run 0 to 9). */
const int kCategory = 10;
/** The bytes of a profile's data the settings take. */
const int kSaveBlockSize = 16;

enum Difficulty
{
    kEasy,
    kMedium,
    kHard,
    kDifficulties
};

struct Settings
{
    /** The quick race difficulty, for the career too (stub). */
    int difficulty;
    bool autosave;
    bool realPerformance;
    /** Stubs. */
    bool realPhysics;
    bool realDamage;
    bool mostWantedCarsOnly;
    /** gameplay/vanillafixes.h. */
    bool vanillaFixes;
    /** Stub. */
    bool speedrunner;
};

/** The settings the page edits. */
Settings &Current();
/** Every row at its default. */
void Defaults();
/** After a new profile's first save: the page opens in place of the next menu the front end goes to. */
void Begin(const char *profile);
/** The page is left: the settings go to the profile, and the front end goes on to the menu it was going to. */
void Finish();
/** A profile's settings: as its save (or the page) gave them, else profiles.ini's, else the defaults. */
Settings ForProfile(const char *profile);
/** Whether this session has loaded, saved or made a profile of that name (so it is not a new one). */
bool IsKnown(const char *profile);
/** A count that changes whenever ForProfile's answer for some profile may have changed (for callers that keep it). */
unsigned int Changes();
/** Asked by a new career starting from the front end: true while a new profile's page is still to come or up. The career then
 * waits for the page to close. */
bool HoldCareer();
/** A profile was loaded from its save (`data`: the profile's data, its first kSaveBlockSize bytes the settings). */
void Loaded(const char *profile, const unsigned char *data);
/** A profile is being saved (`data` as Loaded's): its settings go into the block. */
void Saving(const char *profile, unsigned char *data);
} // namespace nfs::profileoptions
