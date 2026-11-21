/**
 * Car part lookups that also find added parts (original: zWorld CarPartDatabase::NewGetCarPart 0x747c40).
 *
 * A slot's parts come from the two part types GetTypesFromSlot (0x7399f0) names for the car: its own and a shared one
 * (SPOILER, SPOILER_HATCH ...). A profile saves its cars' parts by their index in the part table, so parts added to
 * the game (tools/carbonmerge.py) go after every part already there. The original looks a type up in the sorted table;
 * here, the first lookup lists each type name's parts in table order, wherever they lie, so added parts can name
 * their type as the originals do. Added parts also come under a type named after the car's or the shared one with
 * kAddedSuffix (240SX_PLUS, SPOILER_PLUS), kKitSuffix (240SX_UG2) or kPopupSuffix (the raised headlights' part,
 * source/gameplay/vehicles/PopupHeadlights.cpp). The lookup goes through the car's own type, those, the shared one and the shared
 * added one, so a part asked for by its hash (a preset, a saved car) still finds the original part, and lists (the
 * shop's) go on into the added parts.
 *
 * The part records (14 bytes): name hash, slot, group | level << 5, model selector, type name index, ... The
 * database is a list of part packs, each with its parts at +0x34 and their count at +0x38.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

#include <algorithm>
#include <cstdlib>

namespace
{
const char kAddedSuffix[] = "_PLUS";
const char kKitSuffix[] = "_UG2";
const char kPopupSuffix[] = "_POPUP";
const int kTypes = 6;
const int kPartSize = 0xe;
const int kPartSlot = 4;
const int kPartGroup = 5;
const int kPartType = 7;
const int kLevelShift = 5;
const int kPackParts = 0x34;
const int kPackCount = 0x38;
const int kAnyLevel = -1;
const int kTypeSlots = 512;
/** Parts kept only for saved cars that have them (the withdrawn Saab and Volvo bumper designs, tools/pieces.py): a list
 * leaves them out, a lookup by hash or index still finds them. */
const unsigned int kShopHidden = 0x342d0784;

struct TypeParts
{
    unsigned int type;
    int first;
    int count;
};

TypeParts g_types[kTypeSlots];
const unsigned char **g_parts;
int g_partCount = -1;

/** bStringHash of a name, carried on from the hash of the name's start. */
unsigned int Extend(unsigned int hash, const char *rest)
{
    for (; *rest; ++rest)
        hash = hash * 33 + static_cast<unsigned char>(*rest);
    return hash;
}

TypeParts *TypeSlot(unsigned int type)
{
    unsigned int at = (type * 2654435761u) >> 23;
    while (g_types[at].count && g_types[at].type != type)
        at = (at + 1) & (kTypeSlots - 1);
    return &g_types[at];
}

int CountParts(unsigned char *head)
{
    int total = 0;
    for (unsigned char *pack = *reinterpret_cast<unsigned char **>(head); pack != head;
         pack = *reinterpret_cast<unsigned char **>(pack))
        total += *reinterpret_cast<const int *>(pack + kPackCount);
    return total;
}

/** Each type name's parts in table order, the types' lists next to each other in g_parts. */
void ListParts(unsigned char *head, int total)
{
    g_parts = static_cast<const unsigned char **>(std::malloc(sizeof(*g_parts) * total));
    for (TypeParts &slot : g_types)
        slot = TypeParts();
    for (unsigned char *pack = *reinterpret_cast<unsigned char **>(head); pack != head;
         pack = *reinterpret_cast<unsigned char **>(pack))
    {
        const unsigned char *p = *reinterpret_cast<unsigned char **>(pack + kPackParts);
        const unsigned char *const end = p + *reinterpret_cast<const int *>(pack + kPackCount) * kPartSize;
        for (; p < end; p += kPartSize)
        {
            TypeParts *const slot = TypeSlot(CarPartTypeNameHashTable[p[kPartType]]);
            slot->type = CarPartTypeNameHashTable[p[kPartType]];
            ++slot->count;
        }
    }
    int next = 0;
    for (TypeParts &slot : g_types)
    {
        slot.first = next;
        next += slot.count;
        slot.count = 0;
    }
    for (unsigned char *pack = *reinterpret_cast<unsigned char **>(head); pack != head;
         pack = *reinterpret_cast<unsigned char **>(pack))
    {
        const unsigned char *p = *reinterpret_cast<unsigned char **>(pack + kPackParts);
        const unsigned char *const end = p + *reinterpret_cast<const int *>(pack + kPackCount) * kPartSize;
        for (; p < end; p += kPartSize)
        {
            const unsigned int type = CarPartTypeNameHashTable[p[kPartType]];
            unsigned int at = (type * 2654435761u) >> 23;
            while (g_types[at].type != type)
                at = (at + 1) & (kTypeSlots - 1);
            g_parts[g_types[at].first + g_types[at].count++] = p;
        }
    }
    g_partCount = total;
}

const TypeParts *FindType(unsigned int type)
{
    unsigned int at = (type * 2654435761u) >> 23;
    while (g_types[at].count)
    {
        if (g_types[at].type == type)
            return &g_types[at];
        at = (at + 1) & (kTypeSlots - 1);
    }
    return nullptr;
}
} // namespace

CarPart *CarPartDatabase::NewGetCarPart(CarType car, int slot, unsigned int hash, CarPart *previous, int level)
{
    typedef const unsigned int *(__cdecl * TypesFromSlot)(int, int);
    const unsigned int *pair = NFS_ORIGINAL(0x7399f0, TypesFromSlot)(slot, car);
    const unsigned int types[kTypes] = {pair[0], pair[0] ? Extend(pair[0], kAddedSuffix) : 0,
                                        pair[0] ? Extend(pair[0], kKitSuffix) : 0,
                                        pair[0] ? Extend(pair[0], kPopupSuffix) : 0, pair[1],
                                        pair[1] ? Extend(pair[1], kAddedSuffix) : 0};
    const signed char slotId = static_cast<signed char>(reinterpret_cast<const int *>(&CarPartSlotMap)[slot]);
    const unsigned char *after = reinterpret_cast<const unsigned char *>(previous);
    unsigned char *const head = reinterpret_cast<unsigned char *>(this);
    const int total = CountParts(head);
    if (total != g_partCount)
        ListParts(head, total);
    int first = 0;
    if (after)
    {
        const unsigned int type = CarPartTypeNameHashTable[after[kPartType]];
        while (first < kTypes && types[first] != type)
            ++first;
        if (first == kTypes)
            return nullptr;
    }
    for (int t = first; t < kTypes; ++t)
    {
        const TypeParts *const list = types[t] ? FindType(types[t]) : nullptr;
        if (!list)
            continue;
        const unsigned char *const *p = g_parts + list->first;
        const unsigned char *const *const end = p + list->count;
        if (after && t == first)
            p = std::upper_bound(p, end, after);
        for (; p < end; ++p)
        {
            const unsigned char *const part = *p;
            if (static_cast<signed char>(part[kPartSlot]) != slotId ||
                (hash && *reinterpret_cast<const unsigned int *>(part) != hash))
                continue;
            if (level != kAnyLevel && (part[kPartGroup] >> kLevelShift) != level)
                continue;
            CarPart *const found = reinterpret_cast<CarPart *>(const_cast<unsigned char *>(part));
            if (!hash && found->GetAppliedAttributeIParam(kShopHidden, 0))
                continue;
            return found;
        }
    }
    return nullptr;
}
