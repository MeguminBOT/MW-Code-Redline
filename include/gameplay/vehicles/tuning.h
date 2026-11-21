#pragma once

/**
 * Live tuning of the player's car (source/gameplay/vehicles/CarTuning.cpp), for the proving ground's panel (dev/proving.h). Not in the
 * original game.
 *
 * Every physics setting of the car is listed: its pvehicle collection's own (MASS, TENSOR_SCALE) and those of the
 * collections it references (engine, induction, transmission, chassis, tires, brakes, nos, rigidbodyspecs), plus the
 * game's seven tuning sliders (Physics::Tunings), the performance upgrades' levels and the fully upgraded collections
 * they lead to. Values are written into the loaded attribute layouts, which the car's
 * behaviours read in place, so most take effect at once; the ones the behaviours copy when they are made (tyre sizes,
 * axle geometry, mass and inertia, gear shift points) are marked and take effect when the car's behaviours are made
 * again (Reload). Edits last until the game quits or they are put back.
 *
 * Export writes every setting, its original value and its current one to dev\tuning\CAR_DATE.json next to the exe.
 */
namespace nfs::tuning
{
const int kLineLength = 72;

enum Action
{
    kNextField,
    kPreviousField,
    kNextGroup,
    kPreviousGroup,
    kIncrease,
    kDecrease,
    kIncreaseLarge,
    kDecreaseLarge,
    kIncreaseFine,
    kDecreaseFine,
    kRevertField,
    kRevertAll,
};

/** What the list shows: every setting, or the upgrades' levels. */
enum View
{
    kViewSettings,
    kViewUpgrades,
    /** Everything, the levels first (export, keeping). */
    kViewAll,
};
void SetView(View view);
/** The engine's red line (rpm) and the forward gears, 0 without a car. */
float RedLine();
int ForwardGears();

/** Finds the player car's collections anew (after a race loads or the car's behaviours are made again). */
void Refresh();
/** A key's action on the selected setting. */
void Apply(Action action);
/** Edits from text: "class.FIELD=value", "class.FIELD*factor", "class.FIELD+delta", FIELD[i] for one element (pair
 * fields: [0] front, [1] rear; gears: [0] reverse, [1] neutral, [2] first...), "transmission.GEARS=n" for the number of
 * forward gears, separated by ';'. Returns how many were made. */
int ApplyText(const char *text);
/** Adds (+1) or removes (-1) the top forward gear: GEAR_RATIO and GEAR_EFFICIENCY get one element more or fewer (an
 * added gear is 0.82 of the one below it). Returns the forward gears now, or 0 when there is no room. */
int ChangeGears(int delta);
/** Edits waiting for the car's behaviours to be made again. */
bool NeedsReload();
/** Just before the behaviours are made again: edits of the upgraded collections are carried into the car's. */
void PrepareReload();
/** Called once the behaviours were made again. */
void Reloaded();
/** Each car's edits are kept (dev\\tuning\\kept\\CAR.txt, as Debug.ProvingTune text) whenever they change, and
 * made again when the car comes back (Restore: true when there were any; the car's behaviours then need making
 * again). PUT ALL TUNING BACK forgets them. */
void Keep();
bool Restore();
/** Writes the export; its path goes to `path` (MAX_PATH). False when there is no car. */
bool Export(char *path);
/** The panel's lines around the selected setting; returns how many (at most `max`). */
int Text(char (*lines)[kLineLength], int max);
/** The car's name, or "". */
const char *CarName();

/** Where a physics setting lies in its collection's layout (gameplay/vehicles/carmods.h): floats from `offset`, `count` of them (an
 * array's room; its element count is the ushort at offset + 2 and its floats start at offset + 8). */
struct FieldSpec
{
    int offset;
    int count;
    bool array;
    /** The behaviours copy it when they are made: a car already on the road keeps the old value. */
    bool reload;
    /** Its class has an upgraded (fully upgraded) collection. */
    bool upgradable;
};
/** A setting by its class ("engine", "tires", "vehicle" for the pvehicle's own) and name; false for unknown ones. */
bool FieldInfo(const char *group, const char *field, FieldSpec &out);
/** The attribute class's name for a group name ("vehicle" is "pvehicle"), or null. */
const char *GroupName(const char *group);
} // namespace nfs::tuning
