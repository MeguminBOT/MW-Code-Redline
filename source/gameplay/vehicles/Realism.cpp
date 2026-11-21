/**
 * Realistic or standard car performance per profile (gameplay/vehicles/realism.h). Not in the original game.
 *
 * GLOBAL\REALISM.BIN (tools/realism.py) holds edits of attribute collections: every car's engine, gearbox, weight,
 * tyres and drag set to the real car's, its upgrades a realistic street build, and the career's car lot (the
 * frontend collections' prices and unlock bins) placed by realistic performance. Switching to realistic writes them
 * over the loaded layouts (an Attrib::Collection's layout is at +0x18, and the instances read it in place, so every car
 * sees the change), keeping what was there; switching back puts that back. The front end's figures and bars follow
 * (nfs::performance::Reset). Car lists, saves and the front end use the same collections either way.
 *
 * The front end's vault (FE_ATTRIB.BIN) is loaded with the front end and unloaded for a race
 * (UnloadFrontEndVault 0x664670), each time at a new place, so its edits are let go when it unloads and found again
 * (with its standard bytes kept again) whenever a vault has loaded (InitializeSingleAttributeVault 0x664430).
 *
 * -set Debug.CarCost=NAME logs what the car lot asks for the car with that frontend collection (FECarRecord::GetCost
 * 0x581730) whenever the performance switches.
 *
 * REALISM.BIN: "NREL", version (2), edit count; per edit the class and collection keys, the offset in the layout and
 * the size (16 bits each), then that many bytes.
 */
#include <string.h>
#include <windows.h>

#include "game/gen/api.h"
#include "gameplay/vehicles/carmods.h"
#include "core/config.h"
#include "legacy/link.h"
#include "core/log.h"
#include "gameplay/vehicles/performance.h"
#include "gameplay/progression/profileoptions.h"
#include "gameplay/vehicles/realism.h"
#include "engine/resources/attribtables.h"
#include "engine/resources/pack.h"

namespace
{
const char kPath[] = "GLOBAL\\REALISM.BIN";
const unsigned int kMagic = 0x4c45524e; // "NREL"
const unsigned int kVersion = 2;
const unsigned int kFrontendClass = 0x85885722;
const int kCollectionLayout = 0x18;
const int kHeaderSize = 12;
const int kEditHeaderSize = 12;

enum Mode
{
    kByProfile,
    kAlwaysStandard,
    kAlwaysReal,
};

struct Edit
{
    unsigned int cls;
    unsigned int collection;
    unsigned short offset;
    unsigned short size;
    /** Null while the collection's vault is not loaded. */
    unsigned char *target;
    const unsigned char *real;
    unsigned char *standard;
};

struct State
{
    bool loaded;
    bool active;
    Mode mode;
    unsigned char *data;
    unsigned char *standard;
    Edit *edits;
    int count;
    int placed;
};

State g_state;

typedef const void *(__cdecl *FindCollection)(unsigned int, unsigned int);

/** Finds the edits whose collections have loaded since, keeping their standard bytes (and writing the realistic ones
 * when realistic). Returns how many it found. */
int Place()
{
    const int before = g_state.placed;
    for (int i = 0; i < g_state.count; ++i)
    {
        Edit &e = g_state.edits[i];
        if (e.target)
            continue;
        const unsigned char *collection =
            static_cast<const unsigned char *>(NFS_ORIGINAL(0x455fd0, FindCollection)(e.cls, e.collection));
        unsigned char *layout =
            collection ? *reinterpret_cast<unsigned char *const *>(collection + kCollectionLayout) : nullptr;
        if (!layout)
            continue;
        e.target = layout + e.offset;
        memcpy(e.standard, e.target, e.size);
        if (g_state.active)
            memcpy(e.target, e.real, e.size);
        ++g_state.placed;
    }
    return g_state.placed - before;
}

/** Reads REALISM.BIN and places the edits it can. */
void Load()
{
    g_state.loaded = true;
    uint32_t read = 0;
    g_state.data = nfs::pack::ReadFile(kPath, read);
    if (!g_state.data)
    {
        nfs::Log("realism: no %s", kPath);
        return;
    }
    const unsigned int *head = reinterpret_cast<const unsigned int *>(g_state.data);
    if (read < kHeaderSize || head[0] != kMagic || head[1] != kVersion)
    {
        nfs::Log("realism: %s is not version %u", kPath, kVersion);
        return;
    }
    const unsigned int total = head[2];
    g_state.edits = static_cast<Edit *>(::operator new(total * sizeof(Edit)));
    g_state.standard = static_cast<unsigned char *>(::operator new(read));
    const unsigned char *p = g_state.data + kHeaderSize, *end = g_state.data + read;
    unsigned char *keep = g_state.standard;
    for (unsigned int i = 0; i < total && p + kEditHeaderSize <= end; ++i)
    {
        Edit &e = g_state.edits[g_state.count];
        e.cls = *reinterpret_cast<const unsigned int *>(p);
        e.collection = *reinterpret_cast<const unsigned int *>(p + 4);
        e.offset = *reinterpret_cast<const unsigned short *>(p + 8);
        e.size = *reinterpret_cast<const unsigned short *>(p + 10);
        e.real = p + kEditHeaderSize;
        p = e.real + e.size;
        if (p > end)
            break;
        e.target = nullptr;
        e.standard = keep;
        keep += e.size;
        ++g_state.count;
    }
    Place();
    nfs::Log("realism: %d edits from %s (%d placed, the rest when their vault loads)", g_state.count, kPath,
             g_state.placed);
}

/** Debug.CarCost: the car lot's price of a car, through the game's own lookup. */
void LogCarCost()
{
    char name[32];
    nfs::config::String("Debug", "CarCost", "", name, sizeof(name));
    if (!name[0])
        return;
    struct Record
    {
        unsigned int handle;
        unsigned int frontend;
    } record = {0, Attrib::StringToKey(name)};
    typedef unsigned int(__thiscall * GetCost)(Record *);
    nfs::Log("realism: the car lot asks %u for %s", NFS_ORIGINAL(0x581730, GetCost)(&record), name);
}

void Set(bool real)
{
    if (!g_state.loaded)
        Load();
    if (real == g_state.active || (real && !g_state.count))
        return;
    for (int i = 0; i < g_state.count; ++i)
    {
        const Edit &e = g_state.edits[i];
        if (e.target)
            memcpy(e.target, real ? e.real : e.standard, e.size);
    }
    g_state.active = real;
    nfs::performance::Reset();
    nfs::Log("realism: %s car performance", real ? "realistic" : "standard");
    // The car mods go on over the realistic edits and the standard bytes alike (gameplay/vehicles/carmods.h).
    nfs::carmods::Apply();
    LogCarCost();
}

bool ProfileIsReal(const char *name)
{
    return name && *name && nfs::profileoptions::ForProfile(name).realPerformance;
}
} // namespace

/** A vault has loaded (the front end's, among others): the edits of its collections take their places. */
Attrib::Vault *InitializeSingleAttributeVault(void *data, const char *name, unsigned char **out, unsigned int flags)
{
    Attrib::Vault *vault = nfs::attribtables::InitializeVault(data, name, out, flags);
    if (g_state.loaded)
    {
        if (const int placed = Place())
            nfs::Log("realism: %d edits placed in the %s vault", placed, name ? name : "?");
    }
    return vault;
}

/** The front end's vault unloads: its edits let go of their places. */
void UnloadFrontEndVault()
{
    for (int i = 0; i < g_state.count; ++i)
    {
        Edit &e = g_state.edits[i];
        if (e.cls == kFrontendClass && e.target)
        {
            e.target = nullptr;
            --g_state.placed;
        }
    }
    NFS_ORIGINAL(0x664670, void(__cdecl *)())();
}

namespace nfs::realism
{
void Init()
{
    char mode[16];
    nfs::config::String("Gameplay", "Physics", "profile", mode, sizeof(mode));
    g_state.mode = !lstrcmpiA(mode, "real") ? kAlwaysReal : (!lstrcmpiA(mode, "game") ? kAlwaysStandard : kByProfile);
    Set(g_state.mode == kAlwaysReal);
    if (!g_state.active)
        nfs::carmods::Apply();
}

void UseProfile(const char *name)
{
    if (g_state.mode == kByProfile)
        Set(ProfileIsReal(name));
}

void ChooseForProfile(const char *name, bool real)
{
    if (name && *name)
        nfs::config::ProfileWriteString("Profiles", name, real ? "real" : "standard");
    UseProfile(name);
}

bool Active()
{
    return g_state.active;
}
} // namespace nfs::realism
