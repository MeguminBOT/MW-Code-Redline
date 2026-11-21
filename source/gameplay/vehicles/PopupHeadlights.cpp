/**
 * Pop-up headlights (tools/carbonmerge.py add_popup_headlights). A car with them has, besides its shut headlights, the
 * raised ones as <CAR>_POPUP_KIT00_<slot>_<LOD> solids, and its LEFT_HEADLIGHT slot holds one of two parts carrying
 * POPUP_HEADLIGHTS: 1 shut by day (the stock part), 2 raised by day. At night they are raised either way; shut, they
 * give no light. The parts shop has them in an option of its own (CUSTOMIZE\OPTIONS.INI POPUP_HEADLIGHTS), and a
 * profile keeps the choice as it keeps any part.
 * Not in the original game, whose Most Wanted cars keep theirs shut.
 */
#include "game/gen/api.h"
#include "legacy/link.h"
#include "core/config.h"
#include "core/log.h"
#include "gameplay/vehicles/cars.h"
#include "gameplay/world/timeofday.h"

#include <windows.h>

#include <cstring>


namespace
{
const int kRideInfo = 0x84;
const int kModels = 0xb84;
const int kLodsPerSlot = 5;
const int kMinLod = 0x1628;
const int kMaxLod = 0x162c;
/** The headlights draw at full unless broken (RenderFlaresOnCar 0x742950): shut lamps are drawn as broken. */
const int kBrokenLights = 0x1624;
const int kReplacementTextures = 0x23c;
const int kReplacementTextureCount = 0x49;
const int kModelNameHash = 8;
const unsigned int kModelPointer = 0xfffffffc;
SlotPool *const *const kModelPool = reinterpret_cast<SlotPool *const *>(0x9b091c);
/** VehicleFX::ID: the left and right headlights. */
const unsigned int kHeadlightBits = 3;

const int kFlares = 0x104;
const int kFlareMarker = 8;
const int kFlarePosition = 0x10;
const unsigned int kMarkerLeft = 0x9db90133;
const unsigned int kMarkerRight = 0xd09091c6;
const float kFlareAhead = 0.02f;
/** The lens stands this far up a raised lamp, of its height. */
const float kFlareHeight = 0.65f;

const int kSlotLeft = 31;
const int kPartTypeIndex = 7;
const int kRaisedByDay = 2;
const float kNightLights = 0.5f;
const char kLodLetters[] = "ABCDE";

struct Lamp
{
    int slot;
    const char *raised;
};
const Lamp kLamps[] = {{31, "_POPUP_KIT00_LEFT_HEADLIGHT_"},
                       {32, "_POPUP_KIT00_LEFT_HEADLIGHT_GLASS_"},
                       {40, "_POPUP_KIT00_RIGHT_HEADLIGHT_"},
                       {41, "_POPUP_KIT00_RIGHT_HEADLIGHT_GLASS_"}};

unsigned int PopupAttribute()
{
    static const unsigned int hash = bStringHash("POPUP_HEADLIGHTS");
    return hash;
}

unsigned int Extend(unsigned int hash, const char *rest)
{
    for (; *rest; ++rest)
        hash = hash * 33 + static_cast<unsigned char>(*rest);
    return hash;
}

/** The hash of the car's raised lamp at this LOD: the hash of the car's name goes on with the rest. */
unsigned int RaisedName(const CarPart *part, const char *raised, int lod)
{
    const unsigned char type = reinterpret_cast<const unsigned char *>(part)[kPartTypeIndex];
    const char letter[2] = {kLodLetters[lod], 0};
    return Extend(Extend(CarPartTypeNameHashTable[type], raised), letter);
}

int PopupMode(CarPart *part)
{
    return part ? part->GetAppliedAttributeIParam(PopupAttribute(), 0) : 0;
}

/** Whether the car's lamps stand raised now. */
bool Raised(int mode)
{
    return mode == kRaisedByDay || (nfs::timeofday::Lit() && nfs::timeofday::LocalLights() > kNightLights);
}

/** The shop's and the game's way of filling a model slot (CarRenderInfo::UpdateCarParts 0x755800). A car whose shut lamps
 * are part of its body has no model in the slot until the raised ones take it. */
void SetLamp(unsigned char *self, unsigned int *slot, unsigned int want)
{
    typedef void *(__thiscall * Malloc)(SlotPool *);
    SlotPool *pool = *kModelPool;
    eModel *model = reinterpret_cast<eModel *>(*slot & kModelPointer);
    const bool found = NFS_ORIGINAL(0x505340, eSolid *(__cdecl *)(unsigned int))(want) != nullptr;
    if (model)
    {
        if (want == *reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(model) + kModelNameHash))
            return;
        model->UnInit();
        if (!found)
        {
            pool->Free(model);
            *slot &= 1;
            return;
        }
    }
    else
    {
        if (!found)
            return;
        model = static_cast<eModel *>(NFS_ORIGINAL(0x465f70, Malloc)(pool));
        *slot = (*slot & 1) | reinterpret_cast<unsigned int>(model);
    }
    model->Init(want);
    model->AttachReplacementTextureTable(reinterpret_cast<eReplacementTextureTable *>(self + kReplacementTextures),
                                         kReplacementTextureCount, 0);
}

void SetLamps(unsigned char *self, RideInfo *ride, bool raised)
{
    const int first = *reinterpret_cast<int *>(self + kMinLod);
    const int last = *reinterpret_cast<int *>(self + kMaxLod);
    /** The stock part keeps the car's own name for the raised lamps; slot 32's part is always stock. */
    CarPart *named = ride->GetPart(kLamps[1].slot);
    if (!named)
        return;
    for (const Lamp &lamp : kLamps)
    {
        CarPart *part = ride->GetPart(lamp.slot);
        if (!part)
            continue;
        for (int lod = first; lod <= last; ++lod)
        {
            SetLamp(self, reinterpret_cast<unsigned int *>(self + kModels + (lamp.slot * kLodsPerSlot + lod) * 4),
                    raised ? RaisedName(named, lamp.raised, lod) : part->GetModelNameHash(0, lod));
        }
    }
}
/** The headlight flares stand at the shut lamp's marker in the car's BASE, inside the raised one: they go to the front
 * of the raised lamp. A flare (CarRenderInfo::CreateCarLightFlares 0x74df10): next, previous, marker hash, position. */
void PlaceFlares(unsigned char *self)
{
    const int lod = *reinterpret_cast<int *>(self + kMinLod);
    for (unsigned char *node = *reinterpret_cast<unsigned char **>(self + kFlares); node && node != self + kFlares;
         node = *reinterpret_cast<unsigned char **>(node))
    {
        const unsigned int marker = *reinterpret_cast<unsigned int *>(node + kFlareMarker);
        const int slot = marker == kMarkerLeft ? kLamps[0].slot : marker == kMarkerRight ? kLamps[2].slot : -1;
        if (slot < 0)
            continue;
        const unsigned int model = *reinterpret_cast<unsigned int *>(self + kModels + (slot * kLodsPerSlot + lod) * 4);
        if (!(model & kModelPointer))
            continue;
        bVector3 low, high;
        reinterpret_cast<eModel *>(model & kModelPointer)->GetBoundingBox(&low, &high);
        float *position = reinterpret_cast<float *>(node + kFlarePosition);
        position[0] = high.x + kFlareAhead;
        position[1] = (low.y + high.y) * 0.5f;
        position[2] = low.z + (high.z - low.z) * kFlareHeight;
    }
}
} // namespace

void CarRenderInfo::UpdateLightStateTextures()
{
    // -set Debug.BrakeLights=1: every car's brake lamps lit (the original's ForceBrakelightsOn), to look at them in the
    // front end.
    static const bool brakes = nfs::config::Int("Debug", "BrakeLights", 0) != 0;
    if (brakes)
        ForceBrakelightsOn = 1;
    NFS_ORIGINAL(0x737e30, void(__thiscall *)(CarRenderInfo *))(this);
    unsigned char *self = reinterpret_cast<unsigned char *>(this);
    RideInfo *ride = *reinterpret_cast<RideInfo **>(self + kRideInfo);
    if (!ride)
        return;
    if (const int mode = PopupMode(ride->GetPart(kSlotLeft)))
        SetLamps(self, ride, Raised(mode));
}

void CarRenderInfo::RenderFlaresOnCar(eView *view, const bVector3 *position, const bMatrix4 *matrix, int reflection,
                                      int a, int b)
{
    unsigned char *self = reinterpret_cast<unsigned char *>(this);
    RideInfo *ride = *reinterpret_cast<RideInfo **>(self + kRideInfo);
    unsigned int *broken = reinterpret_cast<unsigned int *>(self + kBrokenLights);
    const unsigned int was = *broken;
    if (ride)
    {
        const int mode = PopupMode(ride->GetPart(kSlotLeft));
        if (mode)
        {
            if (Raised(mode))
                PlaceFlares(self);
            else
                *broken |= kHeadlightBits;
        }
    }
    NFS_ORIGINAL(0x742950,
                 void(__thiscall *)(CarRenderInfo *, eView *, const bVector3 *, const bMatrix4 *, int, int, int))(
        this, view, position, matrix, reflection, a, b);
    *broken = was;
}

int LoadedCar::GetModelHashes(unsigned int *hashes, int capacity)
{
    int count = NFS_ORIGINAL(0x752b60, int(__thiscall *)(LoadedCar *, unsigned int *, int))(this, hashes, capacity);
    const unsigned char *self = reinterpret_cast<const unsigned char *>(this);
    RideInfo *ride = *reinterpret_cast<RideInfo *const *>(self + 0xc);
    if (!ride || !PopupMode(ride->GetPart(kSlotLeft)))
        return count;
    CarPart *named = ride->GetPart(kLamps[1].slot);
    if (!named)
        return count;
    const unsigned char *ride_bytes = reinterpret_cast<const unsigned char *>(ride);
    const int range = *reinterpret_cast<const int *>(self + 0x10) ? 0x10 : 8;
    const int first = *reinterpret_cast<const int *>(ride_bytes + range);
    const int last = *reinterpret_cast<const int *>(ride_bytes + range + 4);
    for (const Lamp &lamp : kLamps)
    {
        for (int lod = first; lod <= last && count < capacity; ++lod)
            hashes[count++] = RaisedName(named, lamp.raised, lod);
    }
    return count;
}

bool nfs::cars::PopupsShut(const void *render_info)
{
    RideInfo *ride = *reinterpret_cast<RideInfo *const *>(static_cast<const unsigned char *>(render_info) + kRideInfo);
    const int mode = ride ? PopupMode(ride->GetPart(kSlotLeft)) : 0;
    return mode && !Raised(mode);
}
