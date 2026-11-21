/**
 * Live tuning of the player's car (gameplay/vehicles/tuning.h). Not in the original game.
 *
 * The settings are the layout fields of the car's physics classes (their offsets are the classes' definitions in
 * attributes.bin, the same for every car). The pvehicle layout is the one the car's attributes instance reads (a car
 * with upgrades reads a modified copy); each referenced class is the collection its pvehicle reference names at index 0,
 * which is the one the car's behaviours look up (BehaviorSpecsPtr), and its layout is Attrib::Collection +0x18.
 * An Attrib array in a layout is an 8-byte header (room, count, element size, flags; 16 bits each) and its elements.
 *
 * The first time a layout is seen its bytes are kept, so the original values survive edits, behaviour reloads and races
 * for the rest of the session.
 *
 * Upgrades (Physics::Upgrades): a physics reference of the pvehicle is a list, the stock collection first and the fully
 * upgraded one last. Installing a level (SetLevel 0x678ad0) makes the car's pvehicle a modified copy whose first
 * reference is a collection made between the two (UpgradeInternal 0x678730), so the base groups then show the car as
 * upgraded; the "upgraded" groups are the last references, what the levels lead to. An edit of those is carried into
 * the made collections by installing the levels again before the car's behaviours are made again.
 */
#include <string.h>
#include <windows.h>

#include "game/gen/api.h"
#include "dev/dev.h"
#include "legacy/link.h"
#include "core/log.h"
#include "dev/pose.h"
#include "gameplay/vehicles/realism.h"
#include "gameplay/vehicles/tuning.h"

namespace nfs::tuning
{
namespace
{
enum Group
{
    kVehicle,
    kEngine,
    kInduction,
    kTransmission,
    kChassis,
    kTires,
    kBrakes,
    kNos,
    kRigidBody,
    kTunings,
    kUpgrades,
    kGroupCount,
};

const char *const kGroupNames[kGroupCount] = {"pvehicle", "engine", "induction", "transmission", "chassis",       "tires",
                                              "brakes",   "nos",    "rigidbodyspecs", "tunings", "upgrades"};
/** The bytes kept of each group's layout (its definitions' end). */
const int kGroupSize[kGroupCount] = {0x50, 0x60, 0x1c, 0x84, 0x74, 0x50, 0x14, 0x20, 0x104, 0x1c, 0};
/** The groups with upgrades, and their Physics::Upgrades::Type (-1: none). */
const int kUpgradeType[kGroupCount] = {-1, 4, 5, 3, 2, 0, 1, 6, -1, -1, -1};
const int kUpgradeTypes = 7;

enum Kind
{
    kFloat,
    /** AxlePair: front, rear. */
    kPair,
    /** Vector4: x, y, z (w unused by the physics). */
    kVector,
    /** Attrib array of floats. */
    kArray,
    /** Attrib array of floats by gear: reverse, neutral, first... */
    kGears,
    kBool,
    /** An upgrade's level (the offset is its Physics::Upgrades::Type). */
    kLevel,
};

struct Field
{
    unsigned char group;
    unsigned char kind;
    unsigned short offset;
    /** kArray: the room; kVector: the components listed. */
    unsigned char count;
    /** Copied by the behaviours when they are made: takes effect after a reload. */
    bool reload;
    const char *name;
};

const Field kFields[] = {
    {kVehicle, kFloat, 0x38, 1, true, "MASS"},
    {kVehicle, kVector, 0x00, 3, true, "TENSOR_SCALE"},

    {kEngine, kArray, 0x00, 9, true, "TORQUE"},
    {kEngine, kFloat, 0x54, 1, true, "MAX_RPM"},
    {kEngine, kFloat, 0x58, 1, true, "RED_LINE"},
    {kEngine, kFloat, 0x5c, 1, true, "IDLE"},
    {kEngine, kArray, 0x2c, 2, false, "SPEED_LIMITER"},
    {kEngine, kArray, 0x3c, 3, false, "ENGINE_BRAKING"},
    {kEngine, kFloat, 0x50, 1, false, "FLYWHEEL_MASS"},

    {kInduction, kFloat, 0x14, 1, false, "PSI"},
    {kInduction, kFloat, 0x00, 1, false, "LOW_BOOST"},
    {kInduction, kFloat, 0x18, 1, false, "HIGH_BOOST"},
    {kInduction, kFloat, 0x0c, 1, false, "SPOOL"},
    {kInduction, kFloat, 0x08, 1, false, "VACUUM"},
    {kInduction, kFloat, 0x10, 1, false, "SPOOL_TIME_UP"},
    {kInduction, kFloat, 0x04, 1, false, "SPOOL_TIME_DOWN"},

    {kTransmission, kGears, 0x00, 9, true, "GEAR_RATIO"},
    {kTransmission, kFloat, 0x80, 1, true, "FINAL_GEAR"},
    {kTransmission, kGears, 0x40, 9, false, "GEAR_EFFICIENCY"},
    {kTransmission, kFloat, 0x70, 1, false, "TORQUE_SPLIT"},
    {kTransmission, kArray, 0x2c, 3, false, "DIFFERENTIAL"},
    {kTransmission, kFloat, 0x7c, 1, false, "SHIFT_SPEED"},
    {kTransmission, kFloat, 0x78, 1, false, "OPTIMAL_SHIFT"},
    {kTransmission, kFloat, 0x74, 1, false, "CLUTCH_SLIP"},
    {kTransmission, kFloat, 0x6c, 1, false, "TORQUE_CONVERTER"},

    {kChassis, kPair, 0x38, 2, false, "SPRING_STIFFNESS"},
    {kChassis, kPair, 0x08, 2, false, "SPRING_PROGRESSION"},
    {kChassis, kPair, 0x30, 2, false, "SHOCK_STIFFNESS"},
    {kChassis, kPair, 0x28, 2, false, "SHOCK_EXT_STIFFNESS"},
    {kChassis, kPair, 0x40, 2, false, "SHOCK_VALVING"},
    {kChassis, kPair, 0x00, 2, false, "SHOCK_DIGRESSION"},
    {kChassis, kFloat, 0x58, 1, false, "SHOCK_BLOWOUT"},
    {kChassis, kPair, 0x48, 2, false, "SWAYBAR_STIFFNESS"},
    {kChassis, kPair, 0x18, 2, false, "RIDE_HEIGHT"},
    {kChassis, kPair, 0x10, 2, false, "TRAVEL"},
    {kChassis, kFloat, 0x50, 1, false, "ROLL_CENTER"},
    {kChassis, kFloat, 0x6c, 1, false, "FRONT_WEIGHT_BIAS"},
    {kChassis, kFloat, 0x70, 1, false, "DRAG_COEFFICIENT"},
    {kChassis, kFloat, 0x68, 1, false, "AERO_COEFFICIENT"},
    {kChassis, kFloat, 0x5c, 1, false, "AERO_CG"},
    {kChassis, kPair, 0x20, 2, true, "TRACK_WIDTH"},
    {kChassis, kFloat, 0x54, 1, true, "WHEEL_BASE"},
    {kChassis, kFloat, 0x64, 1, true, "FRONT_AXLE"},
    {kChassis, kFloat, 0x60, 1, false, "RENDER_MOTION"},

    {kTires, kPair, 0x38, 2, false, "STATIC_GRIP"},
    {kTires, kPair, 0x20, 2, false, "DYNAMIC_GRIP"},
    {kTires, kPair, 0x18, 2, false, "GRIP_SCALE"},
    {kTires, kArray, 0x00, 4, false, "YAW_CONTROL"},
    {kTires, kFloat, 0x4c, 1, false, "YAW_SPEED"},
    {kTires, kFloat, 0x48, 1, false, "STEERING"},
    {kTires, kPair, 0x40, 2, true, "SECTION_WIDTH"},
    {kTires, kPair, 0x28, 2, true, "ASPECT_RATIO"},
    {kTires, kPair, 0x30, 2, true, "RIM_SIZE"},

    {kBrakes, kPair, 0x08, 2, false, "BRAKES"},
    {kBrakes, kPair, 0x00, 2, false, "BRAKE_LOCK"},
    {kBrakes, kFloat, 0x10, 1, false, "EBRAKE"},

    {kNos, kFloat, 0x04, 1, false, "TORQUE_BOOST"},
    {kNos, kFloat, 0x10, 1, false, "NOS_CAPACITY"},
    {kNos, kFloat, 0x08, 1, false, "FLOW_RATE"},
    {kNos, kFloat, 0x00, 1, false, "NOS_DISENGAGE"},
    {kNos, kFloat, 0x0c, 1, false, "RECHARGE_MIN"},
    {kNos, kFloat, 0x14, 1, false, "RECHARGE_MAX"},
    {kNos, kFloat, 0x1c, 1, false, "RECHARGE_MIN_SPEED"},
    {kNos, kFloat, 0x18, 1, false, "RECHARGE_MAX_SPEED"},

    {kRigidBody, kVector, 0x90, 3, true, "CG"},
    {kRigidBody, kVector, 0x10, 3, false, "DRAG"},
    {kRigidBody, kVector, 0x60, 3, false, "DRAG_ANGULAR"},
    {kRigidBody, kFloat, 0xf0, 1, false, "GRAVITY"},
    {kRigidBody, kArray, 0xe0, 2, false, "GROUND_FRICTION"},
    {kRigidBody, kArray, 0xd0, 2, false, "WALL_FRICTION"},
    {kRigidBody, kArray, 0xc0, 2, false, "OBJ_FRICTION"},
    {kRigidBody, kVector, 0x40, 3, false, "GROUND_ELASTICITY"},
    {kRigidBody, kVector, 0x70, 3, false, "WALL_ELASTICITY"},
    {kRigidBody, kVector, 0x50, 3, false, "OBJ_ELASTICITY"},
    {kRigidBody, kVector, 0x80, 3, false, "GROUND_MOMENT_SCALE"},
    {kRigidBody, kVector, 0x20, 3, false, "WORLD_MOMENT_SCALE"},
    {kRigidBody, kVector, 0x30, 3, false, "OBJ_MOMENT_SCALE"},
    {kRigidBody, kFloat, 0xf4, 1, false, "NATURAL_ANGULAR_DAMPING"},
    {kRigidBody, kVector, 0x00, 3, true, "COLLISION_BOX_PAD"},

    // Physics::Tunings: the game's tuning sliders, set through IVehicle::SetTunings.
    {kTunings, kFloat, 0x00, 1, false, "STEERING"},
    {kTunings, kFloat, 0x04, 1, false, "HANDLING"},
    {kTunings, kFloat, 0x08, 1, false, "BRAKES"},
    {kTunings, kFloat, 0x0c, 1, false, "RIDEHEIGHT"},
    {kTunings, kFloat, 0x10, 1, false, "AERODYNAMICS"},
    {kTunings, kFloat, 0x14, 1, false, "NOS"},
    {kTunings, kFloat, 0x18, 1, false, "INDUCTION"},

    // Performance upgrades (Physics::Upgrades::Type in the offset): their levels.
    {kUpgrades, kLevel, 4, 1, true, "ENGINE"},
    {kUpgrades, kLevel, 5, 1, true, "INDUCTION"},
    {kUpgrades, kLevel, 3, 1, true, "TRANSMISSION"},
    {kUpgrades, kLevel, 2, 1, true, "CHASSIS"},
    {kUpgrades, kLevel, 0, 1, true, "TIRES"},
    {kUpgrades, kLevel, 1, 1, true, "BRAKES"},
    {kUpgrades, kLevel, 6, 1, true, "NOS"},
};
const int kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

const unsigned int kVehicleHandle = 0x4040e0;
/** IVehicle: GetVehicleAttributes, GetVehicleName, GetTunings, SetTunings. */
const int kGetVehicleAttributes = 0x48;
const int kGetVehicleName = 0x4c;
/** ISimable::GetAttributes. */
const int kGetSimableAttributes = 0x44;
const int kGetTunings = 0xcc;
const int kSetTunings = 0xd0;
const int kInstanceLayout = 8;
const int kCollectionLayout = 0x18;
const int kArrayCount = 2;
const int kArrayData = 8;
const int kMaxRows = 320;
const int kMaxKept = 256;
const int kMaxReferences = 3;
/** Rows the panel shows around the selected one. */
const int kWindow = 22;
const float kMinimumStep = 0.001f;

typedef int(__cdecl *GetLevelFunction)(const Attrib::Instance *, int);
typedef bool(__cdecl *SetLevelFunction)(Attrib::Instance *, int, int);
typedef const void *(__cdecl *FindCollection)(unsigned int, unsigned int);

struct Row
{
    unsigned char field;
    unsigned char element;
    /** The group's fully upgraded collection (its last reference). */
    unsigned char top;
};

struct Kept
{
    const unsigned char *layout;
    /** The collection's key: a collection the upgrades made can come at the place of a freed one. */
    unsigned int collection;
    unsigned char bytes[0x104];
};

struct State
{
    void *vehicle;
    /** The car whose original levels and sliders are kept. */
    void *originalVehicle;
    Attrib::Instance *attributes;
    Attrib::Instance *vehicleAttributes;
    char car[32];
    unsigned char *layouts[kGroupCount];
    unsigned int collections[kGroupCount];
    const unsigned char *original[kGroupCount];
    unsigned char *tops[kGroupCount];
    unsigned int topCollections[kGroupCount];
    const unsigned char *topOriginal[kGroupCount];
    int levels[kUpgradeTypes];
    int maxLevels[kUpgradeTypes];
    int levelsOriginal[kUpgradeTypes];
    unsigned char tunings[0x1c];
    unsigned char tuningsOriginal[0x1c];
    bool tuningsKnown;
    Row rows[kMaxRows];
    int rowCount;
    int selected;
    bool reload;
    /** An upgraded collection was edited: the levels are installed again before the reload. */
    bool reinstall;
    Kept kept[kMaxKept];
    int keptCount;
    View view;
};

State g_state;

template <typename R, typename... A> R Virtual(void *object, int offset, A... args)
{
    typedef R(__thiscall * Function)(void *, A...);
    return (*reinterpret_cast<Function *>(*reinterpret_cast<char **>(object) + offset))(object, args...);
}

/** "-12.345" with `decimals` places (wsprintf has no floats). */
void Format(char *out, float value, int decimals)
{
    static const int kScale[] = {1, 10, 100, 1000, 10000, 100000};
    const bool negative = value < 0.0f;
    const double magnitude = negative ? -static_cast<double>(value) : static_cast<double>(value);
    while (decimals > 0 && magnitude * kScale[decimals] > 2.0e9)
        --decimals;
    const int scale = kScale[decimals];
    const int scaled = magnitude * scale < 2.0e9 ? static_cast<int>(magnitude * scale + 0.5) : 2000000000;
    const int whole = scaled / scale;
    const int part = scaled % scale;
    if (!decimals)
        wsprintfA(out, "%s%d", negative && scaled ? "-" : "", whole);
    else
    {
        char format[16];
        wsprintfA(format, "%%s%%d.%%0%dd", decimals);
        wsprintfA(out, format, negative && scaled ? "-" : "", whole, part);
    }
}

/** Enough places for the value's size. */
void FormatAuto(char *out, float value)
{
    const float a = value < 0.0f ? -value : value;
    Format(out, value, a >= 1000.0f ? 1 : (a >= 10.0f ? 3 : 4));
}

const unsigned char *Keep(const unsigned char *layout, unsigned int collection, int size)
{
    for (int i = 0; i < g_state.keptCount; ++i)
        if (g_state.kept[i].layout == layout && g_state.kept[i].collection == collection)
            return g_state.kept[i].bytes;
    if (g_state.keptCount == kMaxKept)
        return nullptr;
    Kept &k = g_state.kept[g_state.keptCount++];
    k.layout = layout;
    k.collection = collection;
    memcpy(k.bytes, layout, size);
    return k.bytes;
}

void *Vehicle()
{
    return nfs::pose::PlayerInterface(kVehicleHandle);
}

/** The number of elements a field has now. */
int Elements(const Field &f, const unsigned char *layout)
{
    switch (f.kind)
    {
    case kPair:
        return 2;
    case kVector:
        return f.count;
    case kArray:
    case kGears: {
        const int n = *reinterpret_cast<const unsigned short *>(layout + f.offset + kArrayCount);
        return n < f.count ? n : f.count;
    }
    default:
        return 1;
    }
}

int ElementOffset(const Field &f, int element)
{
    return f.offset + (f.kind == kArray || f.kind == kGears ? kArrayData : 0) + element * 4;
}

unsigned char *Layout(int group, bool top)
{
    if (group == kTunings)
        return g_state.tuningsKnown ? g_state.tunings : nullptr;
    if (group == kUpgrades)
        return g_state.attributes ? reinterpret_cast<unsigned char *>(g_state.levels) : nullptr;
    return top ? g_state.tops[group] : g_state.layouts[group];
}

const unsigned char *Original(int group, bool top)
{
    if (group == kTunings)
        return g_state.tuningsKnown ? g_state.tuningsOriginal : nullptr;
    if (group == kUpgrades)
        return g_state.attributes ? reinterpret_cast<const unsigned char *>(g_state.levelsOriginal) : nullptr;
    return top ? g_state.topOriginal[group] : g_state.original[group];
}

float Read(const unsigned char *layout, const Field &f, int element)
{
    if (f.kind == kBool)
        return layout[f.offset] ? 1.0f : 0.0f;
    if (f.kind == kLevel)
        return static_cast<float>(reinterpret_cast<const int *>(layout)[f.offset]);
    return *reinterpret_cast<const float *>(layout + ElementOffset(f, element));
}

/** A row's value now, or as it was. */
float Value(const Row &r, bool original)
{
    const Field &f = kFields[r.field];
    const unsigned char *layout = original ? Original(f.group, r.top != 0) : Layout(f.group, r.top != 0);
    return layout ? Read(layout, f, r.element) : 0.0f;
}

void PushTunings()
{
    void *vehicle = Vehicle();
    if (vehicle && g_state.tuningsKnown)
        Virtual<void>(vehicle, kSetTunings, static_cast<const void *>(g_state.tunings));
}

int LevelOf(int type)
{
    return g_state.attributes ? NFS_ORIGINAL(0x672d30, GetLevelFunction)(g_state.attributes, type) : 0;
}

bool Find();

/** Installs an upgrade's level (0: the stock part). */
void SetLevel(int type, int level)
{
    if (!g_state.attributes)
        return;
    const int most = g_state.maxLevels[type];
    level = level < 0 ? 0 : (level > most ? most : level);
    const bool done = NFS_ORIGINAL(0x678ad0, SetLevelFunction)(g_state.attributes, type, level);
    if (g_state.vehicleAttributes && g_state.vehicleAttributes != g_state.attributes)
        NFS_ORIGINAL(0x678ad0, SetLevelFunction)(g_state.vehicleAttributes, type, level);
    g_state.reload = true;
    nfs::Log("tuning: upgrade %d to level %d of %d: %s, now %d", type, level, most, done ? "done" : "refused",
             LevelOf(type));
}

void Write(int row, float value)
{
    const Row &r = g_state.rows[row];
    const Field &f = kFields[r.field];
    if (f.kind == kLevel)
    {
        SetLevel(f.offset, static_cast<int>(value + (value < 0.0f ? -0.5f : 0.5f)));
        Find();
        return;
    }
    unsigned char *layout = Layout(f.group, r.top != 0);
    if (!layout)
        return;
    if (f.kind == kBool)
        layout[f.offset] = value != 0.0f;
    else
        *reinterpret_cast<float *>(layout + ElementOffset(f, r.element)) = value;
    if (f.reload)
        g_state.reload = true;
    if (r.top)
    {
        g_state.reinstall = true;
        g_state.reload = true;
    }
    if (f.group == kTunings)
        PushTunings();
}

const char *ElementName(const Field &f, int element)
{
    static const char *const kAxle[2] = {"F", "R"};
    static const char *const kAxis[4] = {"X", "Y", "Z", "W"};
    static char index[8];
    switch (f.kind)
    {
    case kPair:
        return kAxle[element];
    case kVector:
        return kAxis[element];
    case kArray:
        wsprintfA(index, "%d", element);
        return index;
    case kGears:
        if (element < 2)
            return element ? "N" : "R";
        wsprintfA(index, "%d", element - 1);
        return index;
    default:
        return nullptr;
    }
}

/** A row's section: its group, and the upgraded collection apart. */
int Section(const Row &r)
{
    return kFields[r.field].group * 2 + r.top;
}

/** "engine", or "engine_top" for the upgraded collection. */
void SectionName(char *out, const Row &r, bool shown)
{
    const char *group = kGroupNames[kFields[r.field].group];
    if (!r.top)
        lstrcpyA(out, group);
    else
        wsprintfA(out, shown ? "%s upgraded" : "%s_top", group);
}

void AddRows(int field, bool top, const Row &selected)
{
    const Field &f = kFields[field];
    const unsigned char *layout = Layout(f.group, top);
    if (!layout)
        return;
    const int n = Elements(f, layout);
    for (int e = 0; e < n && g_state.rowCount < kMaxRows; ++e)
    {
        const Row row = {static_cast<unsigned char>(field), static_cast<unsigned char>(e),
                         static_cast<unsigned char>(top)};
        if (row.field == selected.field && row.element == selected.element && row.top == selected.top)
            g_state.selected = g_state.rowCount;
        g_state.rows[g_state.rowCount++] = row;
    }
}

/** The base groups, the levels, then each upgraded collection. */
void BuildRows()
{
    const Row selected = g_state.rowCount ? g_state.rows[g_state.selected] : Row{0, 0, 0};
    g_state.rowCount = 0;
    g_state.selected = 0;
    const bool levels = g_state.view == kViewUpgrades;
    if (g_state.view == kViewAll)
        for (int i = 0; i < kFieldCount; ++i)
            if (kFields[i].group == kUpgrades)
                AddRows(i, false, selected);
    for (int i = 0; i < kFieldCount; ++i)
        if ((kFields[i].group == kUpgrades) == levels)
            AddRows(i, false, selected);
    if (!levels)
        for (int i = 0; i < kFieldCount; ++i)
            if (kUpgradeType[kFields[i].group] >= 0)
                AddRows(i, true, selected);
}

/** A physics reference of the pvehicle (the index-th of its list): the collection's layout and key. */
unsigned char *Reference(int group, int index, unsigned int &key)
{
    const unsigned int *ref = static_cast<const unsigned int *>(
        g_state.attributes->GetAttributePointer(Attrib::StringToKey(kGroupNames[group]), index));
    if (!ref)
        return nullptr;
    const unsigned char *collection =
        static_cast<const unsigned char *>(NFS_ORIGINAL(0x455fd0, FindCollection)(ref[0], ref[1]));
    if (!collection)
        return nullptr;
    key = ref[1];
    return *reinterpret_cast<unsigned char *const *>(collection + kCollectionLayout);
}

bool Find()
{
    void *vehicle = Vehicle();
    if (!vehicle)
    {
        g_state.vehicle = nullptr;
        g_state.attributes = nullptr;
        return false;
    }
    // The car's behaviours read their collections through the simulation object's attributes (ISimable::GetAttributes;
    // BehaviorSpecsPtr), an instance of its own beside the vehicle's (IVehicle::GetVehicleAttributes): the edits and
    // the upgrade levels go to the former, the levels to both.
    void *simable = nfs::pose::PlayerSimable();
    Attrib::Instance *attributes = simable ? Virtual<Attrib::Instance *>(simable, kGetSimableAttributes) : nullptr;
    g_state.vehicleAttributes = Virtual<Attrib::Instance *>(vehicle, kGetVehicleAttributes);
    const char *name = Virtual<const char *>(vehicle, kGetVehicleName);
    if (!attributes)
        return false;
    g_state.vehicle = vehicle;
    g_state.attributes = attributes;
    lstrcpynA(g_state.car, name ? name : "", sizeof(g_state.car));
    unsigned char *layouts[kGroupCount] = {};
    unsigned int collections[kGroupCount] = {};
    layouts[kVehicle] = *reinterpret_cast<unsigned char *const *>(reinterpret_cast<const char *>(attributes) +
                                                                  kInstanceLayout);
    collections[kVehicle] = attributes->GetCollection();
    for (int g = kEngine; g <= kRigidBody; ++g)
    {
        layouts[g] = Reference(g, 0, collections[g]);
        // The last of the list is the fully upgraded collection.
        g_state.tops[g] = nullptr;
        if (kUpgradeType[g] < 0)
            continue;
        for (int i = kMaxReferences - 1; i > 0 && !g_state.tops[g]; --i)
            g_state.tops[g] = Reference(g, i, g_state.topCollections[g]);
        if (g_state.tops[g] == layouts[g])
            g_state.tops[g] = nullptr;
        g_state.topOriginal[g] = g_state.tops[g] ? Keep(g_state.tops[g], g_state.topCollections[g], kGroupSize[g]) : nullptr;
    }
    for (int g = 0; g < kTunings; ++g)
    {
        g_state.layouts[g] = layouts[g];
        g_state.collections[g] = collections[g];
        g_state.original[g] = layouts[g] ? Keep(layouts[g], collections[g], kGroupSize[g]) : nullptr;
    }
    int was[kUpgradeTypes];
    memcpy(was, g_state.levels, sizeof(was));
    for (int t = 0; t < kUpgradeTypes; ++t)
    {
        g_state.levels[t] = LevelOf(t);
        g_state.maxLevels[t] = NFS_ORIGINAL(0x672e80, GetLevelFunction)(attributes, t);
    }
    if (vehicle == g_state.originalVehicle && memcmp(was, g_state.levels, sizeof(was)))
        nfs::Log("tuning: levels now %d %d %d %d %d %d %d (engine %08x)", g_state.levels[0], g_state.levels[1],
                 g_state.levels[2], g_state.levels[3], g_state.levels[4], g_state.levels[5], g_state.levels[6],
                 collections[kEngine]);
    if (vehicle != g_state.originalVehicle)
    {
        g_state.originalVehicle = vehicle;
        const void *tunings = Virtual<const void *>(vehicle, kGetTunings);
        if (tunings)
            memcpy(g_state.tunings, tunings, sizeof(g_state.tunings));
        else
            memset(g_state.tunings, 0, sizeof(g_state.tunings));
        memcpy(g_state.tuningsOriginal, g_state.tunings, sizeof(g_state.tunings));
        memcpy(g_state.levelsOriginal, g_state.levels, sizeof(g_state.levels));
        g_state.tuningsKnown = true;
        nfs::Log("tuning: %s, engine %08x (upgraded %08x), transmission %08x, chassis %08x, tires %08x, levels "
                 "%d %d %d %d %d %d %d, tunings %s",
                 g_state.car, collections[kEngine], g_state.topCollections[kEngine], collections[kTransmission],
                 collections[kChassis], collections[kTires], g_state.levels[0], g_state.levels[1], g_state.levels[2],
                 g_state.levels[3], g_state.levels[4], g_state.levels[5], g_state.levels[6], tunings ? "set" : "none");
    }
    BuildRows();
    return true;
}

float Step(const Field &f, float original)
{
    if (f.kind == kLevel)
        return 1.0f;
    const float a = original < 0.0f ? -original : original;
    if (f.group == kTunings)
        return 0.1f;
    return a * 0.01f > kMinimumStep ? a * 0.01f : kMinimumStep;
}

/** Field and element by name: "TORQUE", "TORQUE[2]"; element -1 for all. */
int FindField(int group, const char *name, int length, int &element)
{
    element = -1;
    int n = length;
    for (int i = 0; i < length; ++i)
        if (name[i] == '[')
        {
            n = i;
            element = 0;
            for (int j = i + 1; j < length && name[j] >= '0' && name[j] <= '9'; ++j)
                element = element * 10 + (name[j] - '0');
            break;
        }
    for (int i = 0; i < kFieldCount; ++i)
        if (kFields[i].group == group && lstrlenA(kFields[i].name) == n &&
            CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, kFields[i].name, n, name, n) == CSTR_EQUAL)
            return i;
    return -1;
}

/** A number at `p` (sign, digits, point, digits); advances `p`. */
float Number(const char *&p)
{
    bool negative = false;
    if (*p == '-' || *p == '+')
        negative = *p++ == '-';
    double value = 0.0, scale = 1.0;
    bool point = false;
    for (;; ++p)
    {
        if (*p >= '0' && *p <= '9')
        {
            value = value * 10.0 + (*p - '0');
            if (point)
                scale *= 10.0;
        }
        else if (*p == '.' && !point)
            point = true;
        else
            break;
    }
    return static_cast<float>((negative ? -value : value) / scale);
}
int SetGears(int forward);
} // namespace

void Refresh()
{
    Find();
}

void SetView(View view)
{
    if (view == g_state.view)
        return;
    g_state.view = view;
    g_state.rowCount = 0;
    Find();
}

float RedLine()
{
    const unsigned char *engine = g_state.layouts[kEngine];
    return engine ? *reinterpret_cast<const float *>(engine + 0x58) : 0.0f;
}

int ForwardGears()
{
    const unsigned char *t = g_state.layouts[kTransmission];
    return t ? *reinterpret_cast<const unsigned short *>(t + kArrayCount) - 2 : 0;
}

void Apply(Action action)
{
    if (!g_state.vehicle && !Find())
        return;
    if (!g_state.rowCount)
        return;
    const Row row = g_state.rows[g_state.selected];
    const Field &f = kFields[row.field];
    switch (action)
    {
    case kNextField:
        g_state.selected = (g_state.selected + 1) % g_state.rowCount;
        return;
    case kPreviousField:
        g_state.selected = (g_state.selected + g_state.rowCount - 1) % g_state.rowCount;
        return;
    case kNextGroup:
    case kPreviousGroup: {
        const int direction = action == kNextGroup ? 1 : -1;
        int i = g_state.selected;
        for (int n = 0; n < g_state.rowCount; ++n)
        {
            i = (i + direction + g_state.rowCount) % g_state.rowCount;
            if (Section(g_state.rows[i]) != Section(row))
                break;
        }
        // To the first row of that section.
        const int section = Section(g_state.rows[i]);
        while (i > 0 && Section(g_state.rows[i - 1]) == section)
            --i;
        g_state.selected = i;
        return;
    }
    default:
        break;
    }
    if (action == kRevertAll)
    {
        for (int t = 0; t < kUpgradeTypes; ++t)
            if (g_state.levels[t] != g_state.levelsOriginal[t])
                SetLevel(t, g_state.levelsOriginal[t]);
        Find();
        for (int g = 0; g < kUpgrades; ++g)
        {
            if (Layout(g, false) && Original(g, false))
                memcpy(Layout(g, false), Original(g, false), kGroupSize[g]);
            if (g != kTunings && Layout(g, true) && Original(g, true))
                memcpy(Layout(g, true), Original(g, true), kGroupSize[g]);
        }
        PushTunings();
        g_state.reload = true;
        g_state.reinstall = true;
        nfs::Log("tuning: every setting put back");
        BuildRows();
        return;
    }
    if (!Layout(f.group, row.top != 0) || !Original(f.group, row.top != 0))
        return;
    const float now = Value(row, false);
    const float was = Value(row, true);
    if (action == kRevertField)
    {
        Write(g_state.selected, was);
        return;
    }
    if (f.kind == kBool)
    {
        Write(g_state.selected, now != 0.0f ? 0.0f : 1.0f);
        return;
    }
    const float step = Step(f, was);
    float delta = 0.0f;
    switch (action)
    {
    case kIncrease:
        delta = step;
        break;
    case kDecrease:
        delta = -step;
        break;
    case kIncreaseLarge:
        delta = f.kind == kLevel ? step : step * 10.0f;
        break;
    case kDecreaseLarge:
        delta = f.kind == kLevel ? -step : -step * 10.0f;
        break;
    case kIncreaseFine:
        delta = f.kind == kLevel ? step : step * 0.1f;
        break;
    case kDecreaseFine:
        delta = f.kind == kLevel ? -step : -step * 0.1f;
        break;
    default:
        break;
    }
    Write(g_state.selected, now + delta);
}

int ApplyText(const char *text)
{
    if (!Find())
        return 0;
    int made = 0;
    for (const char *p = text; *p;)
    {
        while (*p == ';' || *p == ' ' || *p == ',')
            ++p;
        if (!*p)
            break;
        const char *dot = p;
        while (*dot && *dot != '.' && *dot != ';')
            ++dot;
        const char *op = dot;
        while (*op && *op != '=' && *op != '*' && *op != '+' && *op != ';')
            ++op;
        if (*dot != '.' || !*op || *op == ';')
        {
            while (*p && *p != ';')
                ++p;
            continue;
        }
        // "group" or "group_top".
        int group = -1;
        bool top = false;
        const int length = static_cast<int>(dot - p);
        for (int g = 0; g < kGroupCount; ++g)
        {
            const int n = lstrlenA(kGroupNames[g]);
            if (CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, kGroupNames[g], n, p, length < n ? length : n) !=
                CSTR_EQUAL)
                continue;
            if (length == n)
                group = g;
            else if (length == n + 4 &&
                     CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, p + n, 4, "_top", 4) == CSTR_EQUAL)
            {
                group = g;
                top = true;
            }
        }
        int element = -1;
        const int field = group >= 0 ? FindField(group, dot + 1, static_cast<int>(op - dot - 1), element) : -1;
        const char kind = *op;
        const char *value = op + 1;
        const float number = Number(value);
        p = value;
        while (*p && *p != ';')
            ++p;
        if (group == kTransmission && !top && op - dot - 1 == 5 &&
            CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, dot + 1, 5, "GEARS", 5) == CSTR_EQUAL)
        {
            made += SetGears(static_cast<int>(number + 0.5f)) > 0;
            continue;
        }
        if (field < 0)
            continue;
        for (int r = 0; r < g_state.rowCount; ++r)
        {
            const Row row = g_state.rows[r];
            if (row.field != field || row.top != top || (element >= 0 && row.element != element))
                continue;
            const float now = Value(row, false);
            Write(r, kind == '=' ? number : (kind == '*' ? now * number : now + number));
            ++made;
            if (kFields[field].kind == kLevel)
                break;
        }
    }
    nfs::Log("tuning: %d edits from \"%s\"", made, text);
    return made;
}

namespace
{
const int kGearRatio = 0x00;
const int kGearEfficiency = 0x40;
const int kGearRoom = 9;
/** Reverse and neutral come before the forward gears. */
const int kFirstForward = 2;
const int kMinForward = 2;
const float kAddedGearStep = 0.82f;

unsigned short &GearCount(unsigned char *layout, int offset)
{
    return *reinterpret_cast<unsigned short *>(layout + offset + kArrayCount);
}

float &Gear(unsigned char *layout, int offset, int index)
{
    return *reinterpret_cast<float *>(layout + offset + kArrayData + index * 4);
}

/** Sets the forward gears to `forward`; returns them, or 0 when that is out of range. */
int SetGears(int forward)
{
    unsigned char *t = g_state.layouts[kTransmission];
    if (!t)
        return 0;
    const int count = forward + kFirstForward;
    const unsigned short room = *reinterpret_cast<unsigned short *>(t + kGearRatio);
    if (forward < kMinForward || count > kGearRoom || (room && count > room))
        return 0;
    const int now = GearCount(t, kGearRatio);
    for (int i = now; i < count; ++i)
    {
        Gear(t, kGearRatio, i) = Gear(t, kGearRatio, i - 1) * kAddedGearStep;
        Gear(t, kGearEfficiency, i) = Gear(t, kGearEfficiency, i - 1);
    }
    GearCount(t, kGearRatio) = static_cast<unsigned short>(count);
    if (GearCount(t, kGearEfficiency) != count)
        GearCount(t, kGearEfficiency) = static_cast<unsigned short>(count);
    g_state.reload = true;
    BuildRows();
    nfs::Log("tuning: %s has %d forward gears", g_state.car, forward);
    return forward;
}
} // namespace

int ChangeGears(int delta)
{
    if (!Find() || !g_state.layouts[kTransmission])
        return 0;
    return SetGears(GearCount(g_state.layouts[kTransmission], kGearRatio) - kFirstForward + delta);
}

bool NeedsReload()
{
    return g_state.reload;
}

void PrepareReload()
{
    if (!g_state.reinstall || !Find())
        return;
    // The made collections follow the edited upgraded ones: each installed level again.
    g_state.reinstall = false;
    int levels[kUpgradeTypes];
    memcpy(levels, g_state.levels, sizeof(levels));
    for (int t = 0; t < kUpgradeTypes; ++t)
        if (levels[t] > 0)
        {
            NFS_ORIGINAL(0x678ad0, SetLevelFunction)(g_state.attributes, t, 0);
            NFS_ORIGINAL(0x678ad0, SetLevelFunction)(g_state.attributes, t, levels[t]);
        }
    nfs::Log("tuning: upgrades installed again over the edited collections");
}

void Reloaded()
{
    g_state.reload = false;
    g_state.vehicle = nullptr;
}

namespace
{
/** The edits as ApplyText takes them (the levels first, then the gears, then the settings); the rows must be the
 * kViewAll ones. */
void Changes(char *out, int size)
{
    out[0] = 0;
    int length = 0;
    char line[160];
    auto add = [&](const char *text) {
        const int n = lstrlenA(text);
        if (length + n + 2 < size)
        {
            if (length)
                out[length++] = ';';
            lstrcpyA(out + length, text);
            length += n;
        }
    };
    int r = 0;
    for (; r < g_state.rowCount && kFields[g_state.rows[r].field].group == kUpgrades; ++r)
        if (Value(g_state.rows[r], false) != Value(g_state.rows[r], true))
        {
            wsprintfA(line, "upgrades.%s=%d", kFields[g_state.rows[r].field].name,
                      static_cast<int>(Value(g_state.rows[r], false)));
            add(line);
        }
    if (const unsigned char *t = g_state.layouts[kTransmission])
        if (const unsigned char *o = g_state.original[kTransmission])
        {
            const int now = *reinterpret_cast<const unsigned short *>(t + kArrayCount);
            if (now != *reinterpret_cast<const unsigned short *>(o + kArrayCount))
            {
                wsprintfA(line, "transmission.GEARS=%d", now - 2);
                add(line);
            }
        }
    for (; r < g_state.rowCount; ++r)
    {
        const Row &row = g_state.rows[r];
        const Field &f = kFields[row.field];
        const float v = Value(row, false), o = Value(row, true);
        if (v == o || f.kind == kLevel)
            continue;
        char value[32], section[32];
        Format(value, v, 5);
        SectionName(section, row, false);
        if (ElementName(f, row.element))
            wsprintfA(line, "%s.%s[%d]=%s", section, f.name, row.element, value);
        else
            wsprintfA(line, "%s.%s=%s", section, f.name, value);
        add(line);
    }
}

/** tuning\\kept\\CAR.txt in the dev folder (nfs::dev::Folder). */
void KeptPath(char *path)
{
    nfs::dev::Folder(path, "tuning");
    lstrcatA(path, "\\kept");
    CreateDirectoryA(path, nullptr);
    char safe[32];
    int length = 0;
    for (const char *c = g_state.car; *c && length < 31; ++c)
        safe[length++] = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') ? *c : '_';
    safe[length] = 0;
    lstrcatA(path, "\\");
    lstrcatA(path, safe[0] ? safe : "car");
    lstrcatA(path, ".txt");
}

bool WriteExport(HANDLE file, const SYSTEMTIME &now, const char *path);

/** Runs `body` with every row listed (kViewAll), then puts the view back. */
template <typename Body> void WithAllRows(Body body)
{
    const View view = g_state.view;
    const int selected = g_state.selected;
    g_state.view = kViewAll;
    BuildRows();
    body();
    g_state.view = view;
    g_state.rowCount = 0;
    BuildRows();
    g_state.selected = selected < g_state.rowCount ? selected : 0;
}
} // namespace

void Keep()
{
    if (!Find() || !g_state.car[0])
        return;
    static char changes[0x4000];
    WithAllRows([&] { Changes(changes, sizeof(changes)); });
    char path[MAX_PATH];
    KeptPath(path);
    if (!changes[0])
    {
        DeleteFileA(path);
        return;
    }
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(file, changes, lstrlenA(changes), &written, nullptr);
    CloseHandle(file);
}

bool Restore()
{
    if (!Find() || !g_state.car[0])
        return false;
    char path[MAX_PATH];
    KeptPath(path);
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    static char text[0x4000];
    DWORD read = 0;
    ReadFile(file, text, sizeof(text) - 1, &read, nullptr);
    CloseHandle(file);
    text[read] = 0;
    int made = 0;
    WithAllRows([&] { made = ApplyText(text); });
    nfs::Log("tuning: %s's kept edits made again (%d)", g_state.car, made);
    return made > 0;
}

bool Export(char *path)
{
    if (!Find())
        return false;
    char folder[MAX_PATH];
    nfs::dev::Folder(folder, "tuning");
    SYSTEMTIME now;
    GetLocalTime(&now);
    char safe[32];
    int length = 0;
    for (const char *c = g_state.car; *c && length < 31; ++c)
        safe[length++] = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') ? *c : '_';
    safe[length] = 0;
    wsprintfA(path, "%s\\%s_%04d%02d%02d_%02d%02d%02d.json", folder, safe[0] ? safe : "car", now.wYear, now.wMonth,
              now.wDay, now.wHour, now.wMinute, now.wSecond);
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        nfs::Log("tuning: could not write %s", path);
        return false;
    }
    bool written_all = false;
    WithAllRows([&] { written_all = WriteExport(file, now, path); });
    return written_all;
}

namespace
{
bool WriteExport(HANDLE file, const SYSTEMTIME &now, const char *path)
{
    DWORD written = 0;
    char line[256];
    auto put = [&](const char *text) { WriteFile(file, text, lstrlenA(text), &written, nullptr); };
    wsprintfA(line, "{\r\n  \"car\": \"%s\",\r\n  \"date\": \"%04d-%02d-%02d %02d:%02d:%02d\",\r\n", g_state.car,
              now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    put(line);
    wsprintfA(line, "  \"performance\": \"%s\",\r\n  \"collections\": {", nfs::realism::Active() ? "real" : "standard");
    put(line);
    for (int g = 0; g < kTunings; ++g)
    {
        wsprintfA(line, "%s\"%s\": \"0x%08x\"", g ? ", " : "", kGroupNames[g], g_state.collections[g]);
        put(line);
    }
    put("},\r\n  \"changes\": \"");
    static char changes[0x4000];
    Changes(changes, sizeof(changes));
    put(changes);
    put("\",\r\n  \"settings\": [\r\n");
    int changed = 0;
    for (int r = 0; r < g_state.rowCount; ++r)
    {
        const Row &row = g_state.rows[r];
        const Field &f = kFields[row.field];
        const int e = row.element;
        const float v = Value(row, false), o = Value(row, true);
        char value[32], original[32], section[32];
        Format(value, v, f.kind == kLevel ? 0 : 5);
        Format(original, o, f.kind == kLevel ? 0 : 5);
        SectionName(section, row, false);
        const char *element = ElementName(f, e);
        char label[16] = "";
        if (element)
            wsprintfA(label, ", \"element\": \"%s\"", element);
        wsprintfA(line,
                  "    {\"group\": \"%s\", \"field\": \"%s\", \"index\": %d%s, \"original\": %s, \"value\": %s, "
                  "\"changed\": %s}%s\r\n",
                  section, f.name, e, label, original, value, v != o ? "true" : "false",
                  r + 1 < g_state.rowCount ? "," : "");
        put(line);
        changed += v != o;
    }
    put("  ]\r\n}\r\n");
    CloseHandle(file);
    nfs::Log("tuning: %d of %d settings changed, written to %s", changed, g_state.rowCount, path);
    return true;
}
} // namespace

int Text(char (*lines)[kLineLength], int max)
{
    Find();
    int n = 0;
    if (!g_state.rowCount)
    {
        if (max > 0)
            lstrcpyA(lines[n++], "TUNING: NO CAR");
        return n;
    }
    int changed = 0;
    for (int r = 0; r < g_state.rowCount; ++r)
        changed += Value(g_state.rows[r], false) != Value(g_state.rows[r], true);
    wsprintfA(lines[n++], "%s %s   %d CHANGED", g_state.view == kViewUpgrades ? "UPGRADES" : "TUNING", g_state.car,
              changed);
    int first = g_state.selected - kWindow / 2;
    if (first > g_state.rowCount - kWindow)
        first = g_state.rowCount - kWindow;
    if (first < 0)
        first = 0;
    int section = -1;
    for (int r = first; r < g_state.rowCount && r < first + kWindow && n < max - 1; ++r)
    {
        const Row &row = g_state.rows[r];
        const Field &f = kFields[row.field];
        if (Section(row) != section)
        {
            section = Section(row);
            char name[32];
            SectionName(name, row, true);
            wsprintfA(lines[n++], "-- %s", name);
            if (n >= max - 1)
                break;
        }
        const int e = row.element;
        const float v = Value(row, false), o = Value(row, true);
        char value[24], original[24], name[40];
        if (f.kind == kLevel)
        {
            wsprintfA(value, "%d/%d", static_cast<int>(v), g_state.maxLevels[f.offset]);
            wsprintfA(original, "%d", static_cast<int>(o));
        }
        else
        {
            FormatAuto(value, v);
            FormatAuto(original, o);
        }
        const char *element = ElementName(f, e);
        if (element)
            wsprintfA(name, "%s[%s]", f.name, element);
        else
            lstrcpynA(name, f.name, sizeof(name));
        char text[kLineLength];
        if (v != o)
            wsprintfA(text, "%s %-25s %10s (%s)%s", r == g_state.selected ? ">" : " ", name, value, original,
                      f.reload ? " *" : "");
        else
            wsprintfA(text, "%s %-25s %10s%s", r == g_state.selected ? ">" : " ", name, value, f.reload ? "   *" : "");
        lstrcpynA(lines[n++], text, kLineLength);
    }
    if (n < max)
        lstrcpyA(lines[n++], g_state.reload ? "* CHANGED: APPLY RELOADS THE CAR (ENTER, NUM/)"
                                            : "* TAKES EFFECT WHEN APPLIED (RELOAD)");
    return n;
}

const char *CarName()
{
    return g_state.car;
}

bool FieldInfo(const char *group, const char *field, FieldSpec &out)
{
    for (int g = kVehicle; g <= kRigidBody; ++g)
    {
        if (lstrcmpiA(kGroupNames[g], group) && !(g == kVehicle && !lstrcmpiA(group, "vehicle")))
            continue;
        int element;
        const int i = FindField(g, field, lstrlenA(field), element);
        if (i < 0)
            return false;
        const Field &f = kFields[i];
        out.offset = f.offset;
        out.count = f.count;
        out.array = f.kind == kArray || f.kind == kGears;
        out.reload = f.reload;
        out.upgradable = kUpgradeType[g] >= 0;
        return f.kind == kFloat || f.kind == kPair || f.kind == kVector || out.array;
    }
    return false;
}

const char *GroupName(const char *group)
{
    for (int g = kVehicle; g <= kRigidBody; ++g)
        if (!lstrcmpiA(kGroupNames[g], group) || (g == kVehicle && !lstrcmpiA(group, "vehicle")))
            return kGroupNames[g];
    return nullptr;
}
} // namespace nfs::tuning
