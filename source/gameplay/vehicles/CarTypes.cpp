/**
 * Car types past the shipped 84 (original: zWorld CarPartDatabase::GetCarType 0x7398d0, GetCarTypeInfoFromHash
 * 0x739940, RideInfo::FillWithPreset 0x7598d0, CarRenderConn::Construct 0x75e6c0, HeliRenderConn::Construct 0x75e780;
 * zPhysics PVehicle::Resource::Resource 0x669080).
 *
 * The originals stop at 84 types (NUM_CARTYPES), the length of the shipped CarTypeInfo table. The table in GLOBALB can
 * be longer now (tools/carbonmerge.py adds Carbon's cars); these count it from its chunk. Type 84 stays CARTYPE_ANY:
 * the data keeps a placeholder entry there (no model, a name no part uses), so code that passes 84 for "any car" or
 * reads it as "not found" works as before, and added cars start at 85. Code that only compares a type against 84 to
 * look up the table (CarPart::GetCarTypeNameHash's helper sub_7398A0) needs no change.
 *
 * Change from the original: FillWithPreset leaves the ride as it is for a preset of an unknown car type, where the
 * original read through a null CarTypeInfo.
 */
#include "game/gen/api.h"
#include "gameplay/vehicles/cars.h"
#include "legacy/link.h"
#include "core/log.h"

inline void *operator new(size_t, void *where)
{
    return where;
}

NFS_REPLACE(0x669080, VehicleResource::Construct)

namespace
{
const unsigned int kCarTypesChunk = 0x00034600;
const unsigned int kPadWord = 0x11111111;
const int kRenderConnSize = 0x400;
const int kHeliRenderConnSize = 0x1C0;
const int kPresetSlots = 0x8B;
const int kSimUserModeSingle = 1;

const unsigned char *g_counted;
int g_count = nfs::cars::kAnyCarType;

const unsigned char *TypeInfo(int index)
{
    return reinterpret_cast<const unsigned char *>(CarTypeInfoArray) + index * nfs::cars::kCarTypeInfoSize;
}

/** LoaderCarInfo points CarTypeInfoArray at the first 16-byte aligned address of the chunk's data, after up to three
 * 0x11 padding words; the chunk's size gives the count. */
int CountCarTypes(const unsigned char *array)
{
    for (int lead = 0; lead < 16; lead += 4)
    {
        const unsigned int *header = reinterpret_cast<const unsigned int *>(array - lead - 8);
        if (header[0] != kCarTypesChunk)
            continue;
        bool padded = true;
        for (int i = 0; i < lead / 4; ++i)
            padded = padded && header[2 + i] == kPadWord;
        if (!padded)
            continue;
        const int bytes = static_cast<int>(header[1]) - lead;
        if (bytes >= nfs::cars::kAnyCarType * nfs::cars::kCarTypeInfoSize)
            return bytes / nfs::cars::kCarTypeInfoSize;
    }
    return nfs::cars::kAnyCarType;
}

struct PresetCarData
{
    void *node[2];
    char carTypeName[32];
    char presetName[32];
    unsigned long long feKey;
    unsigned long long vehicleKey;
    unsigned int filterBits;
    int physicsLevel;
    unsigned int partNameHashes[kPresetSlots];
};

struct ConnectionDataView
{
    void *unknown;
    const unsigned char *packet;
};
} // namespace

namespace nfs
{
namespace cars
{
int NumCarTypes()
{
    const unsigned char *array = reinterpret_cast<const unsigned char *>(CarTypeInfoArray);
    if (array != g_counted)
    {
        g_count = array ? CountCarTypes(array) : kAnyCarType;
        g_counted = array;
        nfs::Log("car types: %d", g_count);
    }
    return g_count;
}
} // namespace cars
} // namespace nfs

enum CarType CarPartDatabase::GetCarType(unsigned int nameHash)
{
    const int count = nfs::cars::NumCarTypes();
    for (int i = 0; i < count; ++i)
    {
        const unsigned char *info = TypeInfo(i);
        if (bStringHash(reinterpret_cast<const char *>(info + 0x10)) == nameHash)
            return *reinterpret_cast<const enum CarType *>(info + 0x90);
    }
    return static_cast<enum CarType>(nfs::cars::kAnyCarType);
}

class CarTypeInfo *GetCarTypeInfoFromHash(unsigned int nameHash)
{
    const int count = nfs::cars::NumCarTypes();
    for (int i = 0; i < count; ++i)
    {
        const unsigned char *info = TypeInfo(i);
        if (*reinterpret_cast<const unsigned int *>(info + 0x50) == nameHash)
            return reinterpret_cast<CarTypeInfo *>(const_cast<unsigned char *>(info));
    }
    return nullptr;
}

/** PVehicle::Resource (zPhysics): the car type of a pvehicle collection, its memory cost and flags. */
struct VehicleResource
{
    int type;
    unsigned int cost;
    unsigned int flags;

    VehicleResource *Construct(const unsigned char *const *attributes, bool preload, bool player);
};

VehicleResource *VehicleResource::Construct(const unsigned char *const *attributes, bool preload, bool player)
{
    flags = 0;
    const char *model = *reinterpret_cast<const char *const *>(attributes[2] + 0x1C);
    type = CarPartDB.GetCarType(bStringHash(model ? model : ""));
    if (type != -1 && type < nfs::cars::NumCarTypes())
    {
        if (CarInfo_IsSkinned(static_cast<enum CarType>(type)))
            flags |= 4;
        cost = CarInfo_GetResourceCost(static_cast<enum CarType>(type), player,
                                       Sim::GetUserMode() == kSimUserModeSingle);
        if (preload)
            flags |= 2;
        if (cost)
            flags |= 1;
    }
    return this;
}

void RideInfo::FillWithPreset(unsigned int presetHash)
{
    const PresetCarData *preset = reinterpret_cast<const PresetCarData *>(FindFEPresetCar(presetHash));
    if (!preset)
        return;
    unsigned char *self = reinterpret_cast<unsigned char *>(this);
    self[7] = 1;
    const CarTypeInfo *info = GetCarTypeInfoFromHash(FEHashUpper(preset->carTypeName));
    if (!info)
        return;
    const enum CarType type = *reinterpret_cast<const enum CarType *>(reinterpret_cast<const unsigned char *>(info) +
                                                                        0x90);
    if (type != *reinterpret_cast<enum CarType *>(self))
    {
        Init(type, static_cast<enum CarRenderUsage>(0), 0, 0);
        SetStockParts();
    }
    for (int slot = 0; slot < kPresetSlots; ++slot)
    {
        const unsigned int part = preset->partNameHashes[slot];
        if (part == 1)
            continue;
        SetPart(slot, part ? CarPartDB.NewGetCarPart(type, slot, part, nullptr, -1) : nullptr, true);
    }
    nfs::cars::ApplyDebugParts(this);
}

Sim::Connection *CarRenderConn::Construct(const Sim::ConnectionData &data)
{
    const unsigned char *packet = reinterpret_cast<const ConnectionDataView &>(data).packet;
    const int type = CarPartDB.GetCarType(*reinterpret_cast<const unsigned int *>(packet + 4));
    if (type == -1 || type >= nfs::cars::NumCarTypes())
        return nullptr;
    void *memory = gFastMem.Alloc(kRenderConnSize, nullptr);
    if (!memory)
        return nullptr;
    new (memory) CarRenderConn(data, static_cast<enum CarType>(type),
                               reinterpret_cast<RenderConn::Pkt_Car_Open *>(const_cast<unsigned char *>(packet)));
    return static_cast<Sim::Connection *>(memory);
}

Sim::Connection *HeliRenderConn::Construct(const Sim::ConnectionData &data)
{
    const unsigned char *packet = reinterpret_cast<const ConnectionDataView &>(data).packet;
    const int type = CarPartDB.GetCarType(*reinterpret_cast<const unsigned int *>(packet + 4));
    if (type == -1 || type >= nfs::cars::NumCarTypes())
        return nullptr;
    void *memory = gFastMem.Alloc(kHeliRenderConnSize, nullptr);
    if (!memory)
        return nullptr;
    new (memory) HeliRenderConn(data, static_cast<enum CarType>(type),
                                reinterpret_cast<RenderConn::Pkt_Heli_Open *>(const_cast<unsigned char *>(packet)));
    return static_cast<Sim::Connection *>(memory);
}
