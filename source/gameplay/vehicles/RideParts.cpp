/**
 * A car with its bumpers and skirts as parts of their own (tools/pieces.py: the Saab's and the Volvo's) has no body kits
 * in the shop: a ride saved with one of its kits gets the stock body and that kit's bumpers, as the shop would have
 * given them.
 *
 * A development setting for trying parts on a car without the front end: Debug.RideParts lists part names (as GLOBALB
 * holds them, e.g. CHALLENGER71_BODY_KIT01, CHALLENGER71_STYLE01_HOOD, SPOILER_STYLE06) that every ride of a car that
 * has them gets after its stock or saved parts (RideInfo::SetStockParts 0x7594a0, FECustomizationRecord::
 * WriteRecordIntoRide 0x56f2b0, RideInfo::FillWithPreset 0x7598d0: the debug race's car when the car has a preset), in
 * whichever slot takes them. With -skipfe -skipfecar CAR it shows a customized car
 * in the debug race.
 *
 * A car with trim parts (the Unbound cars' window frames, strips and grilles: their own finishes, all plastic or all
 * chrome) has them per body kit: whichever style a ride gets (RideInfo::SetPart 0x756e90), it draws its kit's, and a new
 * kit takes the style along.
 *
 * A car with a wing of its own (the Unbound cars' stock wings: STOCK_WING on the part) gets it as its stock spoiler.
 *
 * A body kit with lamps of its own (the Volvo 242's STREET kit, the Silvia's set ST: tools/unboundcars.py
 * kit_lamp_parts) has them as hidden parts named <car>_KIT<kit>_<slot> in the eight head and tail lamp slots: a ride of
 * that kit gets them whatever lamp it is given, and a ride of another kit gets the stock lamps back. A body kit with a
 * hood of its own (the 180SX's Sil-eighty front, <car>_KIT<kit>_HOOD with KITHOOD) has it in place of the stock hood; a
 * shop hood stays.
 *
 * Underground 2's cars here have their bumpers and skirts as parts of their own beside their body kits
 * (tools/ug2extended.py): the stock body is a shell, and the ATTACHMENT0-2 slots hold the pieces. A body kit that brings
 * its own (OWN_PIECES on the part: a wide kit, an SUV's bumper kit) takes the pieces off (no part in their slots), as in
 * Underground 2; a piece given to a ride of such a kit brings the stock body back, and the other pieces' stock ones
 * where the kit had taken them off. So does a stock body given to a ride without pieces.
 */
#include "game/gen/api.h"
#include "gameplay/progression/parts.h"
#include "gameplay/vehicles/cars.h"
#include "gameplay/vehicles/customize.h"
#include "core/config.h"
#include "legacy/link.h"
#include "core/log.h"

namespace
{
const int kSlots = 0x8B;
const int kMaxParts = 32;

unsigned int g_parts[kMaxParts];
int g_count = -1;

void LoadParts()
{
    g_count = 0;
    char text[1024];
    nfs::config::String("Debug", "RideParts", "", text, sizeof(text));
    const char *p = text;
    while (*p && g_count < kMaxParts)
    {
        char name[96];
        int n = 0;
        while (*p && *p != ',')
        {
            if (n + 1 < static_cast<int>(sizeof(name)))
                name[n++] = *p;
            ++p;
        }
        name[n] = 0;
        if (*p == ',')
            ++p;
        if (n)
            g_parts[g_count++] = bStringHashUpper(name);
    }
    if (g_count)
        nfs::Log("ride parts: %d parts from Debug.RideParts", g_count);
}

void ApplyDebugParts(RideInfo *ride)
{
    if (g_count < 0)
        LoadParts();
    if (!g_count)
        return;
    const enum CarType type = *reinterpret_cast<const enum CarType *>(ride);
    int placed = 0;
    for (int i = 0; i < g_count; ++i)
    {
        for (int slot = 0; slot < kSlots; ++slot)
        {
            if (CarPart *part = CarPartDB.NewGetCarPart(type, slot, g_parts[i], nullptr, -1))
            {
                ride->SetPart(slot, part, true);
                ++placed;
                break;
            }
        }
    }
    if (placed)
        nfs::Log("ride parts: %d of %d parts on a ride of car type %d", placed, g_count, static_cast<int>(type));
}
const int kSlotBody = 23;
const int kSlotFront = 52;
const int kSlotRear = 53;
const int kPartGroup = 5;
const int kPartType = 7;
const int kLevelShift = 5;
const int kAnyLevel = -1;
const unsigned int kLanguageHash = 0x4732da07;
const unsigned int kKitNumber = 0x796c0cb0;

unsigned int Extend(unsigned int hash, const char *rest)
{
    for (; *rest; ++rest)
        hash = hash * 33 + static_cast<unsigned char>(*rest);
    return hash;
}

int Level(const CarPart *part)
{
    return reinterpret_cast<const unsigned char *>(part)[kPartGroup] >> kLevelShift;
}

/** The kit's piece in a slot for the car's own type (named <car><piece>K<kit>), in place of the stock one there. */
void TakeKitPiece(RideInfo *ride, CarType type, int slot, unsigned int car, const char *piece, int kit)
{
    CarPart *stock = ride->GetPart(slot);
    if (!stock || Level(stock) != 0)
        return;
    char tag[4] = {'K', static_cast<char>('0' + kit / 10 % 10), static_cast<char>('0' + kit % 10), 0};
    if (CarPart *part = CarPartDB.NewGetCarPart(type, slot, Extend(Extend(car, piece), tag), nullptr, kAnyLevel))
        ride->SetPart(slot, part, true);
}

const unsigned int kOwnPieces = 0x9d088eeb;
const int kPieceSlots[] = {52, 53, 54};

bool OwnPieces(CarPart *body)
{
    return body && body->GetAppliedAttributeIParam(kOwnPieces, 0) != 0;
}

/** While a saved ride is written into a ride slot by slot: its pieces do not bring the stock body back (a ride saved
 * before the pieces were parts has the empty stock attachments with its body kit). */
bool g_writingRecord;

void TakeOffPieces(RideInfo *ride)
{
    if (!OwnPieces(ride->GetPart(kSlotBody)))
        return;
    for (int slot : kPieceSlots)
        if (ride->GetPart(slot))
            ride->SetPart(slot, nullptr, true);
}

void FitKitPieces(RideInfo *ride)
{
    CarPart *front = ride->GetPart(kSlotFront);
    CarPart *body = ride->GetPart(kSlotBody);
    if (!front || !body || !front->GetAppliedAttributeIParam(kLanguageHash, 0) || OwnPieces(body))
        return;
    const int kit = body->GetAppliedAttributeIParam(kKitNumber, 0);
    if (!kit)
        return;
    const CarType type = *reinterpret_cast<const CarType *>(ride);
    const unsigned int car = CarPartTypeNameHashTable[reinterpret_cast<const unsigned char *>(front)[kPartType]];
    CarPart *stock = CarPartDB.NewGetCarPart(type, kSlotBody, Extend(car, "_BODY_KIT00"), nullptr, kAnyLevel);
    if (!stock)
        return;
    TakeKitPiece(ride, type, kSlotFront, car, "_FRONT_BUMPER_", kit);
    TakeKitPiece(ride, type, kSlotRear, car, "_REAR_BUMPER_", kit);
    ride->SetPart(kSlotBody, stock, true);
    nfs::Log("ride parts: body kit %d of car type %d as its bumpers", kit, static_cast<int>(type));
}

const int kSlotTrim = 55;
const unsigned int kTrimSet = 0x1a75be47;
const unsigned int kTrimStyle = 0x87a8b02b;
const char *const kTrimStyles[] = {"_STOCK_", "_PLASTIC_", "_CHROME_"};
const int kTrimStyleCount = 3;

/** The trim part of the ride's body kit in the style of the one given (tools/unboundcars.py trim_parts: <set>_<STYLE>_K<kit>,
 * TRIM_STYLE 0 stock, 1 plastic, 2 chrome; the stock kit's stock finishes are the slot's stock part). */
CarPart *KitTrim(RideInfo *ride, CarPart *trim)
{
    const unsigned int set = trim->GetAppliedAttributeIParam(kTrimSet, 0);
    if (!set)
        return trim;
    CarPart *body = ride->GetPart(kSlotBody);
    const int kit = body ? body->GetAppliedAttributeIParam(kKitNumber, 0) : 0;
    int style = trim->GetAppliedAttributeIParam(kTrimStyle, 0);
    if (style < 0 || style >= kTrimStyleCount)
        style = 0;
    const CarType type = *reinterpret_cast<const CarType *>(ride);
    CarPart *found;
    if (!kit && !style)
        found = CarPartDB.NewGetCarPart(type, kSlotTrim, 0, nullptr, 0);
    else
    {
        const char tag[] = {'K', static_cast<char>('0' + kit / 10 % 10), static_cast<char>('0' + kit % 10), 0};
        found = CarPartDB.NewGetCarPart(type, kSlotTrim, Extend(Extend(set, kTrimStyles[style]), tag), nullptr,
                                        kAnyLevel);
    }
    return found ? found : trim;
}

const int kLampCount = 8;
const int kLampSlots[kLampCount] = {29, 30, 38, 39, 31, 32, 40, 41};
const char *const kLampNames[kLampCount] = {"LEFT_BRAKELIGHT", "LEFT_BRAKELIGHT_GLASS", "RIGHT_BRAKELIGHT",
                                            "RIGHT_BRAKELIGHT_GLASS", "LEFT_HEADLIGHT", "LEFT_HEADLIGHT_GLASS",
                                            "RIGHT_HEADLIGHT", "RIGHT_HEADLIGHT_GLASS"};
const unsigned int kKitLamp = 0x51716371;

int LampIndex(int slot)
{
    for (int i = 0; i < kLampCount; ++i)
        if (kLampSlots[i] == slot)
            return i;
    return -1;
}

/** The lamp of the ride's body kit in a lamp slot (<car>_KIT<kit>_<slot>), or the stock one in place of another
 * kit's. */
CarPart *KitLamp(RideInfo *ride, int lamp, CarPart *part)
{
    const int slot = kLampSlots[lamp];
    const CarType type = *reinterpret_cast<const CarType *>(ride);
    CarPart *body = ride->GetPart(kSlotBody);
    const int kit = body ? body->GetAppliedAttributeIParam(kKitNumber, 0) : 0;
    if (kit)
    {
        typedef const unsigned int *(__cdecl * TypesFromSlot)(int, int);
        const unsigned int car = NFS_ORIGINAL(0x7399f0, TypesFromSlot)(slot, type)[0];
        const char tag[] = {'_', 'K', 'I', 'T', static_cast<char>('0' + kit / 10 % 10), static_cast<char>('0' + kit % 10),
                            '_', 0};
        if (CarPart *own = CarPartDB.NewGetCarPart(type, slot, Extend(Extend(car, tag), kLampNames[lamp]), nullptr,
                                                   kAnyLevel))
            return own;
    }
    if (!part->GetAppliedAttributeIParam(kKitLamp, 0))
        return part;
    CarPart *stock = CarPartDB.NewGetCarPart(type, slot, 0, nullptr, 0);
    return stock ? stock : part;
}

const int kSlotHood = 63;
const unsigned int kKitHood = 0x516f6db1;

/** The hood of the ride's body kit in place of the stock one (<car>_KIT<kit>_HOOD), or the stock one in place of another
 * kit's; a shop hood (above level 0) stays. */
CarPart *KitHood(RideInfo *ride, CarPart *part)
{
    const bool kitHood = part->GetAppliedAttributeIParam(kKitHood, 0) != 0;
    if (Level(part) != 0 && !kitHood)
        return part;
    const CarType type = *reinterpret_cast<const CarType *>(ride);
    CarPart *body = ride->GetPart(kSlotBody);
    const int kit = body ? body->GetAppliedAttributeIParam(kKitNumber, 0) : 0;
    if (kit)
    {
        typedef const unsigned int *(__cdecl * TypesFromSlot)(int, int);
        const unsigned int car = NFS_ORIGINAL(0x7399f0, TypesFromSlot)(kSlotHood, type)[0];
        const char tag[] = {'_', 'K', 'I', 'T', static_cast<char>('0' + kit / 10 % 10), static_cast<char>('0' + kit % 10),
                            '_', 'H', 'O', 'O', 'D', 0};
        if (CarPart *own = CarPartDB.NewGetCarPart(type, kSlotHood, Extend(car, tag), nullptr, kAnyLevel))
            return own;
    }
    if (!kitHood)
        return part;
    CarPart *stock = CarPartDB.NewGetCarPart(type, kSlotHood, 0, nullptr, 0);
    return stock ? stock : part;
}
} // namespace

namespace
{
CarPart *SetOne(RideInfo *ride, int slot, CarPart *part, bool update);
}

/** A part, then the parts a customization option carries along with it (gameplay/vehicles/customize.h). */
CarPart *RideInfo::SetPart(int slot, CarPart *part, bool update)
{
    CarPart *result = SetOne(this, slot, nfs::customize::Carried(this, slot, part), update);
    nfs::customize::CarryAlong(this, slot, GetPart(slot), update, [](RideInfo *ride, int s, CarPart *p, bool u) {
        return SetOne(ride, s, p, u);
    });
    return result;
}

namespace
{
CarPart *SetOne(RideInfo *ride, int slot, CarPart *part, bool update)
{
    typedef CarPart *(__thiscall * Set)(RideInfo *, int, CarPart *, bool);
    const Set set = NFS_ORIGINAL(0x756e90, Set);
    if (slot == kSlotTrim && part)
        return set(ride, slot, KitTrim(ride, part), update);
    const int lamp = part ? LampIndex(slot) : -1;
    if (lamp >= 0)
        return set(ride, slot, KitLamp(ride, lamp, part), update);
    if (slot == kSlotHood && part)
        return set(ride, slot, KitHood(ride, part), update);
    const bool piece = part && slot >= kPieceSlots[0] && slot <= kPieceSlots[2];
    const CarType type = *reinterpret_cast<const CarType *>(ride);
    if (piece && !g_writingRecord && OwnPieces(ride->GetPart(kSlotBody)))
    {
        // A piece on a body kit that has its own: the stock body comes back, and the other pieces' stock ones.
        if (CarPart *stock = CarPartDB.NewGetCarPart(type, kSlotBody, 0, nullptr, 0))
            set(ride, kSlotBody, stock, update);
        for (int other : kPieceSlots)
            if (other != slot && !ride->GetPart(other))
                if (CarPart *stock = CarPartDB.NewGetCarPart(type, other, 0, nullptr, 0))
                    set(ride, other, stock, update);
    }
    CarPart *result = set(ride, slot, part, update);
    if (slot == kSlotBody && part)
    {
        if (OwnPieces(part))
        {
            for (int other : kPieceSlots)
                if (ride->GetPart(other))
                    set(ride, other, nullptr, update);
        }
        else
        {
            for (int other : kPieceSlots)
            {
                CarPart *stock = ride->GetPart(other) ? nullptr : CarPartDB.NewGetCarPart(type, other, 0, nullptr, 0);
                // Only a car whose stock piece draws (its own type's, not the template's empty attachment) gets it back.
                if (stock && stock->GetAppliedAttributeIParam(kLanguageHash, 0))
                    set(ride, other, stock, update);
            }
        }
    }
    if (slot == kSlotBody)
    {
        CarPart *trim = ride->GetPart(kSlotTrim);
        CarPart *kitTrim = trim ? KitTrim(ride, trim) : nullptr;
        if (kitTrim != trim)
            set(ride, kSlotTrim, kitTrim, update);
        for (int i = 0; i < kLampCount; ++i)
        {
            CarPart *now = ride->GetPart(kLampSlots[i]);
            CarPart *kitLamp = now ? KitLamp(ride, i, now) : nullptr;
            if (kitLamp != now)
                set(ride, kLampSlots[i], kitLamp, update);
        }
        CarPart *hood = ride->GetPart(kSlotHood);
        CarPart *kitHood = hood ? KitHood(ride, hood) : nullptr;
        if (kitHood != hood)
            set(ride, kSlotHood, kitHood, update);
    }
    return result;
}
} // namespace

namespace
{
const int kSlotSpoiler = 44;
const unsigned int kStockWing = 0xc68ef997;

/** A car with a wing of its own (tools/unboundcars.py: STOCK_WING on its stock spoiler part) gets it: the stock part
 * the original picks is the first of the slot's types in the table, the spoiler family's, which lies earlier. */
void FitStockWing(RideInfo *ride)
{
    const CarType type = *reinterpret_cast<const CarType *>(ride);
    for (CarPart *part = CarPartDB.NewGetFirstCarPart(type, kSlotSpoiler, 0, kAnyLevel); part;
         part = CarPartDB.NewGetNextCarPart(part, type, kSlotSpoiler, 0, kAnyLevel))
    {
        if (part->GetAppliedAttributeIParam(kStockWing, 0))
        {
            ride->SetPart(kSlotSpoiler, part, true);
            nfs::Log("ride parts: car type %d gets its own wing", static_cast<int>(type));
            return;
        }
    }
}
} // namespace

void RideInfo::SetStockParts()
{
    NFS_ORIGINAL(0x7594a0, void(__thiscall *)(RideInfo *))(this);
    FitStockWing(this);
    ApplyDebugParts(this);
}

void nfs::cars::ApplyDebugParts(RideInfo *ride)
{
    ::ApplyDebugParts(ride);
}

void FECustomizationRecord::WriteRecordIntoRide(RideInfo *ride) const
{
    g_writingRecord = true;
    for (int slot = 0; slot < kSlots; ++slot)
        ride->SetPart(slot, nfs::parts::Installed(this, slot), true);
    g_writingRecord = false;
    TakeOffPieces(ride);
    FitKitPieces(ride);
    ApplyDebugParts(ride);
}
