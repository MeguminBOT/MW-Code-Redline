/** The factories the simulation makes a vehicle's draw and effect behaviours with, and placed scenery's interface
 * (original: zPhysicsBehaviors Draw*::Construct, Effects*::Construct; zWorld SmackableRenderConn::Construct; zPhysics
 * IPlaceableScenery::CreateInstance). */
#include "game/gen/api.h"
#include "legacy/link.h"
#include "core/log.h"

NFS_REPLACE(0x6bc2d0, DrawTrafficConstruct)
NFS_REPLACE(0x6bc360, DrawNisCarConstruct)
NFS_REPLACE(0x6bc3f0, DrawCopCarConstruct)
NFS_REPLACE(0x6bc480, DrawRaceCarConstruct)
NFS_REPLACE(0x6bafe0, DrawHeliConstruct)
NFS_REPLACE(0x6a7e20, EffectsVehicleConstruct)
NFS_REPLACE(0x6a7ed0, EffectsCarConstruct)
NFS_REPLACE(0x6a8230, EffectsPlayerConstruct)
NFS_REPLACE(0x6a82f0, EffectsSmackableConstruct)
NFS_REPLACE(0x6a84f0, EffectsFragmentConstruct)
NFS_REPLACE(0x74d6a0, SmackableRenderConnFactory)
NFS_REPLACE(0x6816a0, PlaceableSceneryCreateInstance)

void *__fastcall EffectsConstruct(char *self, void *, const void *params);
void *__fastcall EffectsCarConstructor(char *self, void *, const void *params);
void *__fastcall DrawCarConstructor(char *self, void *, const void *params, int usage);
void *__fastcall DrawPerformanceCarConstruct(char *self, void *, const void *params, int usage);
void *__fastcall DrawRaceCarConstructor(char *self, void *, const void *params, int usage);
void *__fastcall DrawHeliConstructor(char *self, void *, const void *params);

namespace
{
void *const kSimAllocator = reinterpret_cast<void *>(0x925b30);
void *const kVehicleHandle = reinterpret_cast<void *>(0x4040e0);
const int kPlaceableInterface = 0xe4;

template <typename T> T &At(void *base, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}

void *Allocate(unsigned int size)
{
    return NFS_ORIGINAL(0x5d29d0, void *(__thiscall *)(void *, unsigned int, const char *))(kSimAllocator, size,
                                                                                            nullptr);
}

/** Vtables of a class made on a base constructor: the main one, the second interface's and the third's. */
struct Vtables
{
    int offsets[5];
    unsigned int tables[5];
};

void SetVtables(void *object, const Vtables &v)
{
    for (int i = 0; i < 5 && v.offsets[i] >= 0; ++i)
        At<unsigned int>(object, v.offsets[i]) = v.tables[i];
}


void *PerformanceCar(const void *params, int kind, const Vtables &v)
{
    void *self = Allocate(0xc4);
    if (!self)
        return nullptr;
    DrawPerformanceCarConstruct(static_cast<char *>(self), nullptr, params, kind);
    SetVtables(self, v);
    return self;
}

using EffectsPort = void *(__fastcall *)(char *, void *, const void *);

void *MakeEffects(unsigned int size, EffectsPort ctor, const void *params, const Vtables &v)
{
    void *self = Allocate(size);
    if (!self)
        return nullptr;
    ctor(static_cast<char *>(self), nullptr, params);
    SetVtables(self, v);
    return self;
}

const Vtables kNoTables = {{-1, -1, -1, -1, -1}, {}};
} // namespace

void *__cdecl DrawTrafficConstruct(const void *params)
{
    void *self = Allocate(0xbc);
    if (!self)
        return nullptr;
    DrawCarConstructor(static_cast<char *>(self), nullptr, params, 4);
    SetVtables(self, {{0, 8, 0x4c, 0x54, 0x60}, {0x8ad92c, 0x8ad924, 0x8ad908, 0x8ad888, 0x8ad86c}});
    return self;
}

void *__cdecl DrawNisCarConstruct(const void *params)
{
    return PerformanceCar(params, 6, {{0, 8, 0x4c, 0x54, 0x60}, {0x8adb0c, 0x8adb04, 0x8adae8, 0x8ada68, 0x8ada4c}});
}

void *__cdecl DrawCopCarConstruct(const void *params)
{
    return PerformanceCar(params, 3, {{0, 8, 0x4c, 0x54, 0x60}, {0x8adbfc, 0x8adbf4, 0x8adbd8, 0x8adb58, 0x8adb3c}});
}

void *__cdecl DrawRaceCarConstruct(const void *params)
{
    void *owner = At<void *>(const_cast<void *>(params), 4);
    char *objects = owner ? static_cast<char *>(owner) + 0x2c : nullptr;
    void *vehicle =
        NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(At<void *>(objects, 4), kVehicleHandle);
    if (!vehicle)
        return nullptr;
    using Class = int(__thiscall *)(void *);
    const int vehicleClass = reinterpret_cast<Class>((*reinterpret_cast<void ***>(vehicle))[0x58 / 4])(vehicle);
    const int kind = vehicleClass ? (vehicleClass != 6 ? 2 : 1) : 0;
    void *self = Allocate(0xc8);
    if (!self)
        return nullptr;
    return DrawRaceCarConstructor(static_cast<char *>(self), nullptr, params, kind);
}

void *__cdecl DrawHeliConstruct(const void *params)
{
    void *self = Allocate(0x10c);
    return self ? DrawHeliConstructor(static_cast<char *>(self), nullptr, params) : nullptr;
}

void *__cdecl EffectsVehicleConstruct(const void *params)
{
    return MakeEffects(0xec, &EffectsConstruct, params, {{0, 8, 0x48, -1, -1}, {0x8ab408, 0x8ab3fc, 0x8ab3f8}});
}

void *__cdecl EffectsCarConstruct(const void *params)
{
    return MakeEffects(0xf0, &EffectsCarConstructor, params, kNoTables);
}

void *__cdecl EffectsPlayerConstruct(const void *params)
{
    return MakeEffects(0xf0, &EffectsCarConstructor, params, {{0, 8, 0x48, -1, -1}, {0x8ab4a8, 0x8ab49c, 0x8ab498}});
}

void *__cdecl EffectsSmackableConstruct(const void *params)
{
    return MakeEffects(0xec, &EffectsConstruct, params, {{0, 8, 0x48, -1, -1}, {0x8ab4f8, 0x8ab4ec, 0x8ab4e8}});
}

void *__cdecl EffectsFragmentConstruct(const void *params)
{
    return MakeEffects(0xec, &EffectsConstruct, params, {{0, 8, 0x48, -1, -1}, {0x8ab548, 0x8ab53c, 0x8ab538}});
}

void *__fastcall SmackableRenderConnConstruct(void *self, void *, const void *data);

void *__cdecl SmackableRenderConnFactory(const void *data)
{
    void *self = Allocate(0xa0);
    return self ? SmackableRenderConnConstruct(self, nullptr, data) : nullptr;
}

void *__cdecl PlaceableSceneryCreateInstance(const char *name, unsigned int flags)
{
    char *scenery = NFS_ORIGINAL(0x681410, char *(__cdecl *)(const char *, unsigned int))(name, flags);
    if (!scenery)
    {
        static int logged = 0;
        if (logged++ < 20)
            nfs::Log("placeable scenery: %s could not be made", name ? name : "?");
    }
    return scenery ? scenery + kPlaceableInterface : nullptr;
}
