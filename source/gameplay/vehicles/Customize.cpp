/**
 * Customization options (gameplay/vehicles/customize.h, docs/CUSTOMIZATION.md). The options are read from
 * CUSTOMIZE\OPTIONS.INI, a car's settings from its files:
 *
 *  - NFSMW Unlimiter's UnlimiterData\_General.ini and UnlimiterData\<CAR>.ini (also under scripts\, where its add-ons
 *    install them), read only when the car has its own file: [Parts] turns options on and off (Attachments is a count),
 *    [Names] and [Icons] rename them (Parts<Key>, PartsAttachment0-9), as the Unlimiter's Extra Customization does;
 *  - ours, CUSTOMIZE\CARS\<CAR>.INI: the same sections, and [Options] / [Names] / [Icons] by option id.
 *
 * A file on the disk beside the exe comes before one a pack serves, so a mod can override either. Labels and icons are
 * names (bStringHashUpper, as the front end's own) or 0x and their hash.
 */
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/gen/api.h"
#include "legacy/link.h"
#include "core/config.h"
#include "core/log.h"
#include "engine/resources/pack.h"
#include "gameplay/vehicles/cars.h"
#include "gameplay/vehicles/customize.h"

namespace
{
using nfs::customize::Option;

const char kOptionsPath[] = "CUSTOMIZE\\OPTIONS.INI";
const char *const kUnlimiterFolders[] = {"UnlimiterData", "scripts\\UnlimiterData"};
const int kMaxOptions = 96;
const unsigned int kFirstCategory = 0x110;
const unsigned int kLastCategory = 0x1ff;
const int kCars = 8;
const int kPartType = 7;
const int kAnyLevel = -1;
const int kAttachments = 10;

/** The model slots by name (decompiled CarPartID.h CAR_SLOT_ID). */
const char *const kSlotNames[] = {
    "BASE", "DAMAGE_FRONT_WINDOW", "DAMAGE_BODY", "DAMAGE_COP_LIGHTS", "DAMAGE_COP_SPOILER", "DAMAGE_FRONT_WHEEL",
    "DAMAGE_LEFT_BRAKELIGHT", "DAMAGE_RIGHT_BRAKELIGHT", "DAMAGE_LEFT_HEADLIGHT", "DAMAGE_RIGHT_HEADLIGHT", "DAMAGE_HOOD",
    "DAMAGE_BUSHGUARD", "DAMAGE_FRONT_BUMPER", "DAMAGE_RIGHT_DOOR", "DAMAGE_RIGHT_REAR_DOOR", "DAMAGE_TRUNK",
    "DAMAGE_REAR_BUMPER", "DAMAGE_REAR_LEFT_WINDOW", "DAMAGE_FRONT_LEFT_WINDOW", "DAMAGE_FRONT_RIGHT_WINDOW",
    "DAMAGE_REAR_RIGHT_WINDOW", "DAMAGE_LEFT_DOOR", "DAMAGE_LEFT_REAR_DOOR", "BODY", "FRONT_BRAKE", "FRONT_LEFT_WINDOW",
    "FRONT_RIGHT_WINDOW", "FRONT_WINDOW", "INTERIOR", "LEFT_BRAKELIGHT", "LEFT_BRAKELIGHT_GLASS", "LEFT_HEADLIGHT",
    "LEFT_HEADLIGHT_GLASS", "LEFT_SIDE_MIRROR", "REAR_BRAKE", "REAR_LEFT_WINDOW", "REAR_RIGHT_WINDOW", "REAR_WINDOW",
    "RIGHT_BRAKELIGHT", "RIGHT_BRAKELIGHT_GLASS", "RIGHT_HEADLIGHT", "RIGHT_HEADLIGHT_GLASS", "RIGHT_SIDE_MIRROR",
    "DRIVER", "SPOILER", "UNIVERSAL_SPOILER_BASE", "DAMAGE0_FRONT", "DAMAGE0_FRONTLEFT", "DAMAGE0_FRONTRIGHT",
    "DAMAGE0_REAR", "DAMAGE0_REARLEFT", "DAMAGE0_REARRIGHT", "ATTACHMENT0", "ATTACHMENT1", "ATTACHMENT2", "ATTACHMENT3",
    "ATTACHMENT4", "ATTACHMENT5", "ATTACHMENT6", "ATTACHMENT7", "ATTACHMENT8", "ATTACHMENT9", "ROOF", "HOOD", "HEADLIGHT",
    "BRAKELIGHT", "FRONT_WHEEL", "REAR_WHEEL", "SPINNER", "LICENSE_PLATE"};
const int kSlotCount = sizeof(kSlotNames) / sizeof(kSlotNames[0]);

/** The original's own categories (decompiled FECustomize.hpp eCustomizeCategory). */
struct Named
{
    const char *name;
    unsigned int category;
};
const Named kOriginals[] = {{"BODY_KIT", 0x101}, {"SPOILERS", 0x102}, {"RIM_BRANDS", 0x103}, {"HOODS", 0x104},
                            {"ROOF_SCOOPS", 0x105}};

// -- INI files

struct Entry
{
    const char *section;
    const char *key;
    const char *value;
};

/** A parsed INI file: its text, cut into sections, keys and values in place. */
struct Ini
{
    char *text = nullptr;
    Entry *entries = nullptr;
    int count = 0;

    ~Ini()
    {
        ::operator delete(text);
        ::operator delete(entries);
    }

    /** The value of the last line with this section and key (case does not matter), or null. */
    const char *Get(const char *section, const char *key) const
    {
        for (int i = count - 1; i >= 0; --i)
        {
            if (!lstrcmpiA(entries[i].section, section) && !lstrcmpiA(entries[i].key, key))
                return entries[i].value;
        }
        return nullptr;
    }
};

char *Trim(char *s)
{
    while (*s == ' ' || *s == '\t')
        ++s;
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
        --end;
    *end = 0;
    return s;
}

/** Cuts a value's trailing comment: "//" anywhere (the Unlimiter's files), ";" or "#" after a blank. */
void CutComment(char *s)
{
    for (char *p = s; *p; ++p)
    {
        if ((p[0] == '/' && p[1] == '/') || ((*p == ';' || *p == '#') && p > s && (p[-1] == ' ' || p[-1] == '\t')))
        {
            *p = 0;
            return;
        }
    }
}

/** Parses text (taken over: freed with the Ini). */
void Parse(Ini &ini, unsigned char *data)
{
    ini.text = reinterpret_cast<char *>(data);
    int lines = 1;
    for (const char *p = ini.text; *p; ++p)
        lines += *p == '\n';
    ini.entries = static_cast<Entry *>(::operator new(sizeof(Entry) * lines));
    const char *section = "";
    char *line = ini.text;
    while (line)
    {
        char *next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        char *s = Trim(line);
        if (*s == '[')
        {
            char *close = strchr(s, ']');
            if (close)
            {
                *close = 0;
                section = Trim(s + 1);
            }
        }
        else if (*s && *s != ';' && *s != '#' && !(s[0] == '/' && s[1] == '/'))
        {
            char *equals = strchr(s, '=');
            if (equals)
            {
                *equals = 0;
                CutComment(equals + 1);
                ini.entries[ini.count++] = {section, Trim(s), Trim(equals + 1)};
            }
        }
        line = next;
    }
}

unsigned char *ReadDisk(const char *path)
{
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return nullptr;
    const DWORD size = GetFileSize(file, nullptr);
    unsigned char *data = static_cast<unsigned char *>(::operator new(size + 1));
    DWORD read = 0;
    ReadFile(file, data, size, &read, nullptr);
    CloseHandle(file);
    data[read] = 0;
    return data;
}

/** The file from the disk beside the exe, else from a pack; whether there was one. */
bool Load(Ini &ini, const char *path, bool packs)
{
    unsigned char *data = ReadDisk(path);
    if (!data && packs)
    {
        uint32_t size = 0;
        data = nfs::pack::ReadWhole(path, size);
    }
    if (!data)
        return false;
    Parse(ini, data);
    return true;
}

// -- values

unsigned int Hash(const char *name)
{
    if (!name || !*name)
        return 0;
    if (name[0] == '0' && (name[1] == 'x' || name[1] == 'X'))
        return strtoul(name + 2, nullptr, 16);
    return bStringHashUpper(name);
}

int SlotNamed(const char *name)
{
    for (int i = 0; i < kSlotCount; ++i)
    {
        if (!lstrcmpiA(kSlotNames[i], name))
            return i;
    }
    return -1;
}

unsigned int OriginalNamed(const char *name)
{
    for (const Named &n : kOriginals)
    {
        if (!lstrcmpiA(n.name, name))
            return n.category;
    }
    return 0;
}

/** Calls f(item) for each comma-separated item of a value. */
template <typename F> void EachItem(const char *value, F f)
{
    while (value && *value)
    {
        char item[64];
        int n = 0;
        while (*value && *value != ',')
        {
            if (n + 1 < static_cast<int>(sizeof(item)))
                item[n++] = *value;
            ++value;
        }
        item[n] = 0;
        if (*value == ',')
            ++value;
        char *trimmed = Trim(item);
        if (*trimmed)
            f(trimmed);
    }
}

// -- the options

struct Definition
{
    Option option;
    /** The Unlimiter's [Parts] key that turns it on, or the attachment it is (Attachments counts them); [Names] and
     * [Icons] take Parts<key> / PartsAttachment<n>. */
    char unlimiter[24];
    int attachment;
};

Definition g_options[kMaxOptions];
int g_count = -1;

void LoadOptions()
{
    g_count = 0;
    Ini ini;
    if (!Load(ini, kOptionsPath, true))
    {
        nfs::Log("customize: no %s, the parts shop as the original has it", kOptionsPath);
        return;
    }
    unsigned int next = kFirstCategory;
    for (int i = 0; i < ini.count && g_count < kMaxOptions; ++i)
    {
        const char *section = ini.entries[i].section;
        if (_strnicmp(section, "Option.", 7) || (i && !lstrcmpiA(ini.entries[i - 1].section, section)))
            continue;
        const char *id = section + 7;
        Definition &d = g_options[g_count];
        d = Definition();
        d.attachment = -1;
        Option &o = d.option;
        lstrcpynA(o.id, id, sizeof(o.id));
        const char *kind = ini.Get(section, "Original");
        if (kind)
        {
            o.kind = nfs::customize::kOriginal;
            o.category = OriginalNamed(kind);
            if (!o.category)
            {
                nfs::Log("customize: %s: no original category %s", id, kind);
                continue;
            }
        }
        else
        {
            o.kind = nfs::customize::kSlot;
            if (next > kLastCategory)
            {
                nfs::Log("customize: more options than the shop has categories for; %s left out", id);
                continue;
            }
            EachItem(ini.Get(section, "Slots"), [&](const char *name) {
                const int slot = SlotNamed(name);
                if (slot < 0)
                    nfs::Log("customize: %s: no slot %s", id, name);
                else if (o.slotCount < nfs::customize::kMaxSlots)
                    o.slots[o.slotCount++] = slot;
            });
            if (!o.slotCount)
            {
                nfs::Log("customize: %s has no slot", id);
                continue;
            }
            o.category = next++;
        }
        // An original category keeps its own name and icons unless the option gives others (0: its own).
        const bool original = o.kind == nfs::customize::kOriginal;
        char label[64];
        const char *named = ini.Get(section, "Label");
        if (!named && !original)
        {
            bSNPrintf(label, sizeof(label), "OPTION_%s", id);
            label[sizeof(label) - 1] = 0;
            named = label;
        }
        o.title = Hash(named);
        const char *icon = ini.Get(section, "Icon");
        o.icon = Hash(icon ? icon : original ? nullptr : "VISUAL_PART_BODY");
        const char *back = ini.Get(section, "IconBackRoom");
        o.iconBackRoom = back ? Hash(back) : o.icon;
        o.filter = Hash(ini.Get(section, "Filter"));
        EachItem(ini.Get(section, "Hides"), [&](const char *name) {
            for (int k = 0; k < g_count; ++k)
            {
                if (!lstrcmpiA(g_options[k].option.id, name) && o.hideCount < nfs::customize::kMaxHides)
                    o.hides[o.hideCount++] = g_options[k].option.category;
            }
        });
        const char *show = ini.Get(section, "Show");
        o.show = !show || !lstrcmpiA(show, "Always")   ? nfs::customize::kAlways
                 : !lstrcmpiA(show, "HasParts")         ? nfs::customize::kHasParts
                                                        : nfs::customize::kWhenEnabled;
        if (o.kind == nfs::customize::kOriginal)
            o.show = nfs::customize::kAlways;
        const char *effect = ini.Get(section, "Effect");
        lstrcpynA(o.effect, effect ? effect : "", sizeof(o.effect));
        const char *unlimiter = ini.Get(section, "Unlimiter");
        if (unlimiter && !_strnicmp(unlimiter, "Attachment", 10) && unlimiter[10] >= '0' && unlimiter[10] <= '9')
            d.attachment = unlimiter[10] - '0';
        else
            lstrcpynA(d.unlimiter, unlimiter ? unlimiter : "", sizeof(d.unlimiter));
        ++g_count;
    }
    nfs::Log("customize: %d options from %s", g_count, kOptionsPath);
}

void Ready()
{
    if (g_count < 0)
        LoadOptions();
}

// -- a car's settings

struct CarOptions
{
    int type = -1;
    Option options[kMaxOptions];
    /** -1 by the option's own rule, 0 off, 1 on. */
    signed char enabled[kMaxOptions];
};

CarOptions g_cars[kCars];
int g_nextCar;
const CarOptions *g_current;

const char *TypeName(int type)
{
    if (type < 0 || type >= nfs::cars::NumCarTypes())
        return "";
    return reinterpret_cast<const char *>(CarTypeInfoArray) + type * nfs::cars::kCarTypeInfoSize;
}

/** The Unlimiter's sections, over the options (its car file and ours). */
void ApplyUnlimiter(CarOptions &car, const Ini &ini)
{
    const char *count = ini.Get("Parts", "Attachments");
    const int attachments = count ? atoi(count) % (kAttachments + 1) : -1;
    for (int i = 0; i < g_count; ++i)
    {
        const Definition &d = g_options[i];
        char key[40];
        if (d.attachment >= 0)
        {
            if (attachments >= 0)
                car.enabled[i] = d.attachment < attachments ? 1 : 0;
            bSNPrintf(key, sizeof(key), "PartsAttachment%d", d.attachment);
        }
        else if (d.unlimiter[0])
        {
            if (const char *on = ini.Get("Parts", d.unlimiter))
                car.enabled[i] = atoi(on) != 0 ? 1 : 0;
            bSNPrintf(key, sizeof(key), "Parts%s", d.unlimiter);
        }
        else
            continue;
        key[sizeof(key) - 1] = 0;
        if (const char *name = ini.Get("Names", key))
            if (*name)
                car.options[i].title = Hash(name);
        if (const char *icon = ini.Get("Icons", key))
            if (*icon)
                car.options[i].icon = car.options[i].iconBackRoom = Hash(icon);
    }
}

/** Our own sections, by option id. */
void ApplyOwn(CarOptions &car, const Ini &ini)
{
    for (int i = 0; i < g_count; ++i)
    {
        const char *id = g_options[i].option.id;
        if (const char *on = ini.Get("Options", id))
            car.enabled[i] = atoi(on) != 0 ? 1 : 0;
        if (const char *name = ini.Get("Names", id))
            car.options[i].title = Hash(name);
        if (const char *icon = ini.Get("Icons", id))
            car.options[i].icon = car.options[i].iconBackRoom = Hash(icon);
    }
}

const CarOptions &CarSettings(int type)
{
    for (const CarOptions &c : g_cars)
    {
        if (c.type == type)
            return c;
    }
    CarOptions &car = g_cars[g_nextCar];
    g_nextCar = (g_nextCar + 1) % kCars;
    car.type = type;
    for (int i = 0; i < g_count; ++i)
    {
        car.options[i] = g_options[i].option;
        car.enabled[i] = -1;
    }
    const char *name = TypeName(type);
    char path[MAX_PATH];
    for (const char *folder : kUnlimiterFolders)
    {
        bSNPrintf(path, sizeof(path), "%s\\%s.ini", folder, name);
        path[sizeof(path) - 1] = 0;
        Ini own;
        if (!Load(own, path, false))
            continue;
        bSNPrintf(path, sizeof(path), "%s\\_General.ini", folder);
        path[sizeof(path) - 1] = 0;
        Ini general;
        if (Load(general, path, false))
            ApplyUnlimiter(car, general);
        ApplyUnlimiter(car, own);
        nfs::Log("customize: %s from %s\\%s.ini", name, folder, name);
        break;
    }
    bSNPrintf(path, sizeof(path), "CUSTOMIZE\\CARS\\%s.INI", name);
    path[sizeof(path) - 1] = 0;
    Ini ours;
    const bool own = Load(ours, path, true);
    if (own)
    {
        ApplyUnlimiter(car, ours);
        ApplyOwn(car, ours);
    }
    int on = 0;
    for (int i = 0; i < g_count; ++i)
        on += car.enabled[i] > 0;
    nfs::Log("customize: car type %d (%s): %s, %d options turned on by its files", type, name,
             own ? path : "no file of its own", on);
    return car;
}

/** Whether the car has parts for a slot option: one carrying its filter, or with none a second part. */
bool HasParts(int type, const Option &o)
{
    const CarType car = static_cast<CarType>(type);
    int found = 0;
    for (CarPart *part = CarPartDB.NewGetFirstCarPart(car, o.slots[0], 0, kAnyLevel); part;
         part = CarPartDB.NewGetNextCarPart(part, car, o.slots[0], 0, kAnyLevel))
    {
        if (o.filter ? part->HasAppliedAttribute(o.filter) != 0 : ++found > 1)
            return true;
    }
    return false;
}

// -- parts carried along

unsigned int Extend(unsigned int hash, const char *rest)
{
    for (; *rest; ++rest)
        hash = hash * 33 + static_cast<unsigned char>(*rest);
    return hash;
}

unsigned int PartHash(const CarPart *part)
{
    unsigned int h;
    memcpy(&h, part, 4);
    return h;
}

/** The hash of the part's name up to its slot's name (<CAR>_, <CAR>_KITnn_ or <CAR>_STYLEnn_), if it is named so. */
bool NamePrefix(const CarPart *part, int slot, unsigned int &prefix)
{
    const unsigned int whole = PartHash(part);
    const unsigned int car = CarPartTypeNameHashTable[reinterpret_cast<const unsigned char *>(part)[kPartType]];
    const unsigned int base = Extend(car, "_");
    const char *slotName = kSlotNames[slot];
    if (Extend(base, slotName) == whole)
    {
        prefix = base;
        return true;
    }
    static const char *const kKinds[] = {"KIT", "STYLE"};
    for (const char *kind : kKinds)
    {
        const unsigned int k = Extend(base, kind);
        for (int n = 0; n < 100; ++n)
        {
            const char digits[] = {static_cast<char>('0' + n / 10), static_cast<char>('0' + n % 10), '_', 0};
            const unsigned int p = Extend(k, digits);
            if (Extend(p, slotName) == whole)
            {
                prefix = p;
                return true;
            }
        }
    }
    return false;
}

/** The slot option whose first slot is this one, or null. */
const Option *Leading(int slot)
{
    for (int i = 0; i < g_count; ++i)
    {
        const Option &o = g_options[i].option;
        if (o.kind == nfs::customize::kSlot && o.slotCount > 1 && o.slots[0] == slot)
            return &o;
    }
    return nullptr;
}

/** The part named after `part` (in `from`) for slot `to`, or null. */
CarPart *Named(RideInfo *ride, const CarPart *part, int from, int to)
{
    unsigned int prefix;
    if (!part || !NamePrefix(part, from, prefix))
        return nullptr;
    const CarType type = *reinterpret_cast<const CarType *>(ride);
    return CarPartDB.NewGetCarPart(type, to, Extend(prefix, kSlotNames[to]), nullptr, kAnyLevel);
}
} // namespace

int nfs::customize::PartsMenu(int carType, const Option **out, int capacity)
{
    Ready();
    const CarOptions &car = CarSettings(carType);
    g_current = &car;
    bool shown[kMaxOptions];
    unsigned int hidden[kMaxOptions * kMaxHides];
    int hiddenCount = 0;
    for (int i = 0; i < g_count; ++i)
    {
        const Option &o = car.options[i];
        if (car.enabled[i] >= 0)
            shown[i] = car.enabled[i] != 0;
        else
            shown[i] = o.show == kAlways || (o.show == kHasParts && HasParts(carType, o));
        if (shown[i] && o.kind == kSlot)
        {
            for (int k = 0; k < o.hideCount; ++k)
                hidden[hiddenCount++] = o.hides[k];
        }
    }
    int count = 0;
    for (int i = 0; i < g_count && count < capacity; ++i)
    {
        if (!shown[i])
            continue;
        bool hide = false;
        for (int k = 0; k < hiddenCount; ++k)
            hide |= hidden[k] == car.options[i].category;
        if (!hide || car.options[i].kind == kSlot)
            out[count++] = &car.options[i];
    }
    return count;
}

const Option *nfs::customize::ByCategory(unsigned int category)
{
    Ready();
    if (g_current)
    {
        for (int i = 0; i < g_count; ++i)
        {
            if (g_current->options[i].category == category)
                return &g_current->options[i];
        }
    }
    for (int i = 0; i < g_count; ++i)
    {
        if (g_options[i].option.category == category)
            return &g_options[i].option;
    }
    return nullptr;
}

const Option *nfs::customize::BySlot(int slot)
{
    Ready();
    const Option *list = g_current ? g_current->options : nullptr;
    for (int i = 0; i < g_count; ++i)
    {
        const Option &o = list ? list[i] : g_options[i].option;
        if (o.kind != kSlot)
            continue;
        for (int k = 0; k < o.slotCount; ++k)
        {
            if (o.slots[k] == slot)
                return &o;
        }
    }
    return nullptr;
}

CarPart *nfs::customize::Carried(RideInfo *ride, int slot, CarPart *part)
{
    Ready();
    if (!part)
        return part;
    for (int i = 0; i < g_count; ++i)
    {
        const Option &o = g_options[i].option;
        if (o.kind != kSlot)
            continue;
        for (int k = 1; k < o.slotCount; ++k)
        {
            if (o.slots[k] != slot)
                continue;
            if (CarPart *named = Named(ride, ride->GetPart(o.slots[0]), o.slots[0], slot))
                return named;
            return part;
        }
    }
    return part;
}

void nfs::customize::CarryAlong(RideInfo *ride, int slot, CarPart *part, bool update, SetPartFunction set)
{
    Ready();
    const Option *o = part ? Leading(slot) : nullptr;
    if (!o)
        return;
    for (int k = 1; k < o->slotCount; ++k)
    {
        CarPart *named = Named(ride, part, slot, o->slots[k]);
        if (named && ride->GetPart(o->slots[k]) != named)
        {
            set(ride, o->slots[k], named, update);
            if (nfs::config::Int("Debug", "CarryLog", 0))
                nfs::Log("customize: %08x in slot %d carries %08x into slot %d", PartHash(part), slot, PartHash(named),
                         o->slots[k]);
        }
    }
}
