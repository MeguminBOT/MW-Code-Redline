/**
 * Front end car records for the added cars (original: zFe2 FECarRecord::GetManufacturerName 0x581790,
 * FECarRecord::Default 0x5815b0, QuickRaceUnlocker::IsCarUnlocked 0x58a0d0, UIQRCarSelect::RefreshBonusCarList
 * 0x7b4f60, GetBonusUnlockText 0x7a3c00 and GetBonusUnlockBinNumber 0x7a3c70; UserProfile::LoadFromBuffer 0x5acb70 and
 * cFrontendDatabase::Default 0x5ab900 run as before, with the additions below).
 *
 * GetManufacturerName turns the frontend class's manufacturer number into the name the car's string
 * (CARNAME_<manufacturer>_<name>) and logo (CARSELECT_MANUFACTURER_<manufacturer>) are found by. The original knows
 * Most Wanted's 25 makes; the table carries on with the ones Carbon's cars bring, numbered as Carbon numbers them
 * (tools/carbonmerge.py MANUFACTURERS is the same list).
 *
 * A car record's filter bits name its quick race list: stock 0x01, career 0x02, custom 0x04, bonus 0x08 (the presets
 * Most Wanted's bonus cars are). FECarRecord::Default gives a car whose frontend says it is not customizable 0x20,
 * which no list shows: Most Wanted's own such cars are in the bonus list as presets. An added car that is not
 * customizable (Carbon gives it no tuning or customization, tools/carbonmerge.py) goes in the bonus list itself, and
 * unlocks there as a stock car does (its frontend UnlockedAt against the career's progress). The career's car lot only
 * sells the stock list, and the customization shop is closed to it. The quick race bonus list is a fixed list of Most
 * Wanted's presets: the added bonus cars now follow them, and a locked one names the blacklist rival whose defeat
 * unlocks it, as a locked stock car does.
 *
 * A profile's car list is saved whole, and FEPlayerCarDB::Default only fills it in when the profile is created, so
 * cars added to the game after that never reached an existing profile (UserProfile::LoadFromBuffer copies the saved
 * list in place, at 0x414 in the profile). Loading a profile now brings its stock records (a record with one of those
 * list bits and no customization or career record, unlike the presets Default also makes) up to a fresh list Default
 * builds in scratch memory: each takes the fresh record's list, and each stock car whose pvehicle the profile has no
 * record of goes into a free slot.
 *
 * A saved profile carries a version, the CRC32 of a new profile's car list as the game that saved it built it
 * (cFrontendDatabase::Default works it out once, at start), and LoadFromBuffer rejects a profile of another version.
 * The added cars change the list, so every profile saved before them would be rejected and replaced by a new one.
 * Default now also keeps the new profile's list, and a profile rejected on load is tried again under the versions
 * the list had before: with every added car in the stock list (the merge made them all customizable until
 * 2026-10-28), with the added bonus cars in no list (as FECarRecord::Default alone lists them), then without the
 * added cars (each record of a car whose type is past CARTYPE_ANY taken out and the later ones moved up, which is the
 * list Default makes without them). It is then saved under the current version.
 *
 * Cars can only have been added since (the install only appends cars and parts), so a profile of any other version is
 * good: when none of those match, it loads under the version it says it has. New stock cars reach it as above.
 *
 * A car can also go: a mod car whose folder was deleted (mods\, docs/MODS.md). A record of a car whose pvehicle
 * collection the game no longer has is freed on load, as if the profile never had the car. A career that drove it
 * goes on in another of its cars, or in a new one of the first stock car when it had no other.
 */
#include <string.h>

#include "game/gen/api.h"
#include "dev/proving.h"
#include "gameplay/vehicles/cars.h"
#include "core/config.h"
#include "legacy/link.h"
#include "core/log.h"
#include "gameplay/vehicles/performance.h"
#include "gameplay/progression/preferences.h"
#include "gameplay/progression/profileoptions.h"
#include "gameplay/vehicles/realism.h"
#include "dev/testprofile.h"

NFS_REPLACE(0x7b4f60, BonusCarList::Refresh)
NFS_REPLACE(0x7a3c00, BonusCarList::UnlockText)
NFS_REPLACE(0x7a3c70, BonusCarList::UnlockBin)

inline void *operator new(size_t, void *where)
{
    return where;
}

namespace
{
const char *const kManufacturers[] = {
    "",        "BMW",        "FORD",       "SUBARU",     "PORSCHE",     "AUDI",       "MAZDA",    "GENERAL_MOTORS",
    "DODGE",   "TOYOTA",     "MITSUBISHI", "MCLAREN",    "MERCEDES",    "NISSAN",     "LOTUS",    "LAMBORGHINI",
    "RENAULT", "LEXUS",      "PONTIAC",    "CHEVROLET",  "VAUXHALL",    "ASTONMARTIN", "VOLKSWAGEN", "FIAT",
    "CADILLAC", "CORVETTE",  "JAGUAR",     "PLYMOUTH",   "PAGANI",      "CHRYSLER",   "ALFA_ROMEO", "KOENIGSEGG",
    "INFINITI", "SHELBY",     "ACURA",      "HONDA",      "HUMMER",      "HYUNDAI",    "LINCOLN",  "PEUGEOT",
    "SAAB",    "VOLVO",
};
const int kManufacturerCount = sizeof(kManufacturers) / sizeof(kManufacturers[0]);
const int kManufacturerOffset = 0x40;
const int kAttribInstanceSize = 0x14;
const int kInstanceLayout = 8;
const int kModelOffset = 0x1C;

const int kCarDBSize = 0x8CC8;
/** Where a saved profile holds its version: after its 16 byte block and the career settings. */
const int kSavedVersion = 0x5A09;
const int kProfileCarDB = 0x414;
/** UserProfile's CareerSettings, whose first word is the car the career drives (a car record's handle). */
const int kProfileCareer = 0xa8;
const int kActiveProfile = 0x10;
const int kSaveVersion = 8;
const int kCarRecords = 200;
const unsigned int kStockList = 0x01;
const unsigned int kBonusList = 0x08;
const unsigned int kNoList = 0x20;
const unsigned int kLists = kStockList | kBonusList | kNoList;
/** The region filter bit of region 3, whose cars FECarRecord::Default always gives kNoList. */
const unsigned int kRegion3 = 0x80000;
const int kUnlockedAt = 0x59;
const int kCareerBin = 0xB0;
const int kPlayers = 2;
const unsigned char kNone = 0xFF;

/** Most Wanted's bonus cars, in the order the bonus list shows them; the first kUnlockablePresets unlock
 * (UnlockSystem::IsCarUnlocked), the rest are always open. */
const unsigned int kPresetCars[] = {
    0x136253,   0x136252,   0x136251,   0x136250,   0x13624f,   0x13624e,   0x9666,     0x9665,
    0x9664,     0x9663,     0x9662,     0x9661,     0x9660,     0x965f,     0x3a94520,  0xcb6aaf2f,
    0x2cf370f0, 0x2cf385b2, 0x34498eb2, 0x634d1bd2, 0xe1075862, 0x2d642b8,  0x3d3401a,  0x3d8a6d1,
    0x54653c71, 0xe115ead0, 0x54655133, 0x582f21d9, 0x363a1fea,
};
const int kPresetCount = sizeof(kPresetCars) / sizeof(kPresetCars[0]);
const int kUnlockablePresets = 18;
const enum eUnlockFilters kQuickRaceUnlocks = static_cast<enum eUnlockFilters>(1);
const int kCarListHead = 0x2C;
const int kCarListPlayer = 0x1A8;
/** "Unlocked by defeating %s", which the car select fills with the blacklist rival of bin GetBonusUnlockBinNumber. */
const unsigned int kUnlockedByRivalText = 0x4EF2A115;

struct CarListNode
{
    CarListNode *next;
    CarListNode *prev;
    unsigned int handle;
    bool locked;
};

struct CarRecordData
{
    int handle;
    unsigned int feKey;
    unsigned int vehicleKey;
    unsigned int filterBits;
    unsigned char customization;
    unsigned char career;
    unsigned char pad[2];
};
static_assert(sizeof(CarRecordData) == 20, "FECarRecord");

unsigned char g_scratch[kCarDBSize];
unsigned char g_newProfileCars[kCarDBSize];
bool g_haveNewProfileCars;

bool IsStockRecord(const CarRecordData &record)
{
    return record.handle != -1 && (record.filterBits & kLists) && record.customization == kNone &&
           record.career == kNone;
}

bool IsAddedBonusRecord(const CarRecordData &record)
{
    return IsStockRecord(record) && (record.filterBits & kBonusList);
}

/** A player's active profile, or null. */
const unsigned char *ActiveProfile(int player)
{
    if (player < 0 || player >= kPlayers)
        return nullptr;
    return reinterpret_cast<const unsigned char *const *>(reinterpret_cast<unsigned char *>(FEDatabase) +
                                                          kActiveProfile)[player];
}

/** The career bin a car's frontend unlocks it at: it is open once the career is at that bin or past it. */
int UnlockedAt(unsigned int feKey)
{
    alignas(4) unsigned char instance[kAttribInstanceSize];
    new (instance) Attrib::Gen::frontend(feKey, 0, nullptr);
    const unsigned char *layout = *reinterpret_cast<unsigned char *const *>(instance + kInstanceLayout);
    const int bin = layout[kUnlockedAt];
    reinterpret_cast<Attrib::Gen::frontend *>(instance)->~frontend();
    return bin;
}

void AppendCar(unsigned char *carSelect, unsigned int handle, bool locked)
{
    CarListNode *node = static_cast<CarListNode *>(reinterpret_cast<void *(__cdecl *)(unsigned int)>(0x652ad0)(
        sizeof(CarListNode)));
    if (!node)
        return;
    CarListNode *head = reinterpret_cast<CarListNode *>(carSelect + kCarListHead);
    node->handle = handle;
    node->locked = locked;
    node->next = head;
    node->prev = head->prev;
    head->prev->next = node;
    head->prev = node;
}

bool IsAddedCar(unsigned int vehicleKey)
{
    alignas(4) unsigned char instance[kAttribInstanceSize];
    new (instance) Attrib::Gen::pvehicle(vehicleKey, 0, nullptr);
    const unsigned char *layout = *reinterpret_cast<unsigned char *const *>(instance + kInstanceLayout);
    const char *model = *reinterpret_cast<const char *const *>(layout + kModelOffset);
    const int type = CarPartDB.GetCarType(bStringHash(model ? model : ""));
    reinterpret_cast<Attrib::Instance *>(instance)->~Instance();
    return type > nfs::cars::kAnyCarType;
}

/** The version a new profile's car list had before the added cars: the list Default made at start without them. */
unsigned int ShippedCarsVersion()
{
    memcpy(g_scratch, g_newProfileCars, kCarDBSize);
    CarRecordData *records = reinterpret_cast<CarRecordData *>(g_scratch);
    const CarRecordData unused = records[kCarRecords - 1];
    int to = 0;
    for (int from = 0; from < kCarRecords; ++from)
    {
        if (records[from].handle != -1 && IsAddedCar(records[from].vehicleKey))
            continue;
        if (to != from)
        {
            records[to] = records[from];
            if (records[to].handle == from)
                records[to].handle = to;
        }
        ++to;
    }
    while (to < kCarRecords)
        records[to++] = unused;
    return bCalculateCrc32(g_scratch, kCarDBSize, 0xFFFFFFFF);
}

/** The version a new profile's car list had with the added cars' stock records in another list: every one (from is 0)
 * or the ones in the list from, moved to the list to (their region not putting them in no list anyway). */
unsigned int AddedCarsVersion(unsigned int from, unsigned int to)
{
    memcpy(g_scratch, g_newProfileCars, kCarDBSize);
    CarRecordData *records = reinterpret_cast<CarRecordData *>(g_scratch);
    for (int i = 0; i < kCarRecords; ++i)
    {
        CarRecordData &record = records[i];
        if (IsStockRecord(record) && (!from || (record.filterBits & from)) && !(record.filterBits & kRegion3) &&
            IsAddedCar(record.vehicleKey))
            record.filterBits = (record.filterBits & ~kLists) | to;
    }
    return bCalculateCrc32(g_scratch, kCarDBSize, 0xFFFFFFFF);
}

const unsigned int kPvehicleClass = 0x4a97ec8f;

/** Frees the records of cars the game no longer has (their pvehicle collection is gone); returns how many. */
int DropMissingCars(CarRecordData *records, unsigned int &currentCar)
{
    // -set Debug.MissingCar=NAME: that car is taken for one the game no longer has (a test of a mod car taken out).
    char name[32];
    nfs::config::String("Debug", "MissingCar", "", name, sizeof(name));
    const unsigned int missing = name[0] ? Attrib::StringToKey(name) : 0;
    int dropped = 0;
    bool lostCurrent = false;
    for (int i = 0; i < kCarRecords; ++i)
    {
        CarRecordData &record = records[i];
        if (record.handle == -1 ||
            (record.vehicleKey != missing && Attrib::FindCollection(kPvehicleClass, record.vehicleKey)))
            continue;
        nfs::Log("car list: record %d is of a car the game no longer has (pvehicle %08x), freed", i,
                 record.vehicleKey);
        lostCurrent |= static_cast<unsigned int>(record.handle) == currentCar;
        record.handle = -1;
        record.feKey = record.vehicleKey = record.filterBits = 0;
        record.customization = record.career = kNone;
        ++dropped;
    }
    if (!lostCurrent)
        return dropped;
    // The career drove the car that went: it goes on in another car of its own, else in a new one of the first stock
    // car (as if bought), so the safe house and free roam have a car to put it in.
    for (int i = 0; i < kCarRecords; ++i)
    {
        if (records[i].handle != -1 && records[i].career != kNone)
        {
            currentCar = static_cast<unsigned int>(records[i].handle);
            nfs::Log("car list: the career's car went; it goes on in its car of record %d", i);
            return dropped;
        }
    }
    for (int i = 0; i < kCarRecords; ++i)
    {
        if (!IsStockRecord(records[i]) || !(records[i].filterBits & kStockList))
            continue;
        FEPlayerCarDB *stable = reinterpret_cast<FEPlayerCarDB *>(records);
        if (FECarRecord *made = stable->CreateNewCareerCar(static_cast<unsigned int>(records[i].handle)))
        {
            currentCar = *reinterpret_cast<const unsigned int *>(made);
            nfs::Log("car list: the career's only car went; it goes on in a new one of stock record %d", i);
            return dropped;
        }
        break;
    }
    nfs::Log("car list: the career's car went and no other could be given to it");
    return dropped;
}

/** Brings a loaded list's stock records up to a freshly defaulted list's: each takes the fresh record's filter bits
 * (its list), and the stock cars it has no record of go into free slots. */
void UpdateStockCars(CarRecordData *records, int &moved, int &added)
{
    reinterpret_cast<FEPlayerCarDB *>(g_scratch)->Default();
    const CarRecordData *fresh = reinterpret_cast<const CarRecordData *>(g_scratch);
    moved = added = 0;
    int slot = 0;
    for (int i = 0; i < kCarRecords; ++i)
    {
        const CarRecordData &stock = fresh[i];
        if (!IsStockRecord(stock))
            continue;
        bool known = false;
        for (int j = 0; j < kCarRecords; ++j)
        {
            CarRecordData &record = records[j];
            if (record.handle == -1 || record.vehicleKey != stock.vehicleKey)
                continue;
            known = true;
            if (IsStockRecord(record) && record.filterBits != stock.filterBits)
            {
                record.filterBits = stock.filterBits;
                ++moved;
            }
        }
        if (known)
            continue;
        while (slot < kCarRecords && records[slot].handle != -1)
            ++slot;
        if (slot == kCarRecords)
            break;
        records[slot] = stock;
        records[slot].handle = slot;
        ++added;
    }
}
} // namespace

char const *FECarRecord::GetManufacturerName()
{
    alignas(4) unsigned char instance[kAttribInstanceSize];
    const unsigned int feKey = reinterpret_cast<const CarRecordData *>(this)->feKey;
    new (instance) Attrib::Gen::frontend(feKey, 0, nullptr);
    const unsigned char *layout = *reinterpret_cast<unsigned char *const *>(instance + kInstanceLayout);
    const int number = layout[kManufacturerOffset];
    reinterpret_cast<Attrib::Gen::frontend *>(instance)->~frontend();
    return number < kManufacturerCount ? kManufacturers[number] : "";
}

void FECarRecord::Default()
{
    NFS_ORIGINAL(0x5815b0, void(__thiscall *)(FECarRecord *))(this);
    CarRecordData &record = *reinterpret_cast<CarRecordData *>(this);
    if (record.handle != -1 && (record.filterBits & kNoList) && !(record.filterBits & kRegion3) &&
        IsAddedCar(record.vehicleKey))
        record.filterBits = (record.filterBits & ~kNoList) | kBonusList;
}

bool QuickRaceUnlocker::IsCarUnlocked(enum eUnlockFilters filters, unsigned int handle, int player)
{
    typedef bool(__cdecl * Original)(enum eUnlockFilters, unsigned int, int);
    // The proving ground's car select (dev/proving.h) offers every car.
    if (nfs::proving::ChoosingCar())
        return true;
    const unsigned char *profile = ActiveProfile(player);
    if (profile && handle < static_cast<unsigned int>(kCarRecords))
    {
        const CarRecordData &record = reinterpret_cast<const CarRecordData *>(profile + kProfileCarDB)[handle];
        if (static_cast<unsigned int>(record.handle) == handle && IsAddedBonusRecord(record))
            return UnlockAllThings || UnlockedAt(record.feKey) >= ActiveProfile(0)[kCareerBin];
    }
    return NFS_ORIGINAL(0x58a0d0, Original)(filters, handle, player);
}

/** The quick race car select's list (zFe2 UIQRCarSelect). */
struct BonusCarList
{
    void Refresh();
    unsigned int UnlockText(FECarRecord *record);
    int UnlockBin(FECarRecord *record);
};

void BonusCarList::Refresh()
{
    unsigned char *self = reinterpret_cast<unsigned char *>(this);
    const int player = *reinterpret_cast<const int *>(self + kCarListPlayer);
    for (int i = 0; i < kPresetCount; ++i)
        AppendCar(self, kPresetCars[i],
                  i < kUnlockablePresets && !UnlockSystem::IsCarUnlocked(kQuickRaceUnlocks, kPresetCars[i], player));
    if (const unsigned char *profile = ActiveProfile(player))
    {
        const CarRecordData *records = reinterpret_cast<const CarRecordData *>(profile + kProfileCarDB);
        for (int i = 0; i < kCarRecords; ++i)
        {
            const CarRecordData &record = records[i];
            if (IsAddedBonusRecord(record))
                AppendCar(self, record.handle, !UnlockSystem::IsCarUnlocked(kQuickRaceUnlocks, record.handle, player));
        }
    }
    NFS_ORIGINAL(0x7b4e20, void(__thiscall *)(BonusCarList *, int))(this, player);
}

unsigned int BonusCarList::UnlockText(FECarRecord *record)
{
    if (IsAddedBonusRecord(*reinterpret_cast<const CarRecordData *>(record)))
        return kUnlockedByRivalText;
    return NFS_ORIGINAL(0x7a3c00, unsigned int(__thiscall *)(BonusCarList *, FECarRecord *))(this, record);
}

int BonusCarList::UnlockBin(FECarRecord *record)
{
    const CarRecordData &data = *reinterpret_cast<const CarRecordData *>(record);
    if (IsAddedBonusRecord(data))
        return UnlockedAt(data.feKey) + 1;
    return NFS_ORIGINAL(0x7a3c70, int(__thiscall *)(BonusCarList *, FECarRecord *))(this, record);
}

void cFrontendDatabase::Default()
{
    typedef void(__thiscall * Original)(cFrontendDatabase *);
    NFS_ORIGINAL(0x5ab900, Original)(this);
    if (!g_haveNewProfileCars)
    {
        const unsigned char *profile = *reinterpret_cast<unsigned char *const *>(reinterpret_cast<unsigned char *>(this) +
                                                                                 kActiveProfile);
        memcpy(g_newProfileCars, profile + kProfileCarDB, kCarDBSize);
        g_haveNewProfileCars = true;
    }
    nfs::prefs::ApplyToProfile(CurrentUserProfile);
}

bool UserProfile::LoadFromBuffer(void *buffer, int size, bool career, int which)
{
    typedef bool(__thiscall * Original)(UserProfile *, void *, int, bool, int);
    bool loaded = NFS_ORIGINAL(0x5acb70, Original)(this, buffer, size, career, which);
    if (!loaded && g_haveNewProfileCars)
    {
        unsigned int &version = *reinterpret_cast<unsigned int *>(reinterpret_cast<unsigned char *>(FEDatabase) +
                                                                  kSaveVersion);
        const unsigned int current = version;
        const unsigned int earlier[] = {AddedCarsVersion(0, kStockList), AddedCarsVersion(kBonusList, kNoList),
                                        ShippedCarsVersion()};
        const char *const lists[] = {"every added car in the stock list", "the added bonus cars in no list",
                                     "the list without the added cars"};
        for (int i = 0; i < 3 && !loaded; ++i)
        {
            if (earlier[i] == current)
                continue;
            version = earlier[i];
            loaded = NFS_ORIGINAL(0x5acb70, Original)(this, buffer, size, career, which);
            version = current;
            nfs::Log("car list: profile rejected under version %08x, %s under %08x (%s)", current,
                     loaded ? "loaded" : "also rejected", earlier[i], lists[i]);
        }
        if (!loaded && size >= kSavedVersion + static_cast<int>(sizeof(unsigned int)))
        {
            version = *reinterpret_cast<const unsigned int *>(static_cast<const char *>(buffer) + kSavedVersion);
            loaded = NFS_ORIGINAL(0x5acb70, Original)(this, buffer, size, career, which);
            nfs::Log("car list: %s under the version it was saved with, %08x", loaded ? "loaded" : "also rejected",
                     version);
            version = current;
        }
    }
    if (loaded)
        nfs::prefs::ApplyToProfile(this);
    if (loaded)
    {
        unsigned char *carDB = reinterpret_cast<unsigned char *>(this) + kProfileCarDB;
        unsigned int &currentCar = *reinterpret_cast<unsigned int *>(reinterpret_cast<unsigned char *>(this) +
                                                                      kProfileCareer);
        DropMissingCars(reinterpret_cast<CarRecordData *>(carDB), currentCar);
        int moved, added;
        UpdateStockCars(reinterpret_cast<CarRecordData *>(carDB), moved, added);
        if (moved || added)
            nfs::Log("car list: %d stock cars the profile did not have added, %d moved between the stock and bonus "
                     "lists", added, moved);
        // -set Debug.CarList=1: the loaded list's records (handle, front end key, pvehicle, lists, customization,
        // career).
        if (nfs::config::Int("Debug", "CarList", 0))
        {
            const CarRecordData *records = reinterpret_cast<const CarRecordData *>(carDB);
            for (int i = 0; i < kCarRecords; ++i)
                if (records[i].handle != -1)
                    nfs::Log("car list: %3d handle %3d fe %08x pvehicle %08x lists %08x custom %02x career %02x", i,
                             records[i].handle, records[i].feKey, records[i].vehicleKey, records[i].filterBits,
                             records[i].customization, records[i].career);
        }
        if (which == 0)
            nfs::profileoptions::Loaded(m_aProfileName, static_cast<const unsigned char *>(buffer));
        nfs::realism::UseProfile(m_aProfileName);
        nfs::testprofile::Apply(this);
        if (nfs::config::Int("Debug", "RatingsCompare", 0))
            nfs::performance::LogCareerRatings(reinterpret_cast<FEPlayerCarDB *>(carDB));
    }
    return loaded;
}
