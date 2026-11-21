/** The draw behaviours of vehicles, their render connections' open packets, and a vehicle's detachable parts as
 * simulation models of their own (original: zPhysicsBehaviors DrawVehicle::DrawVehicle 0x6b9e70, SpawnModel 0x6b0500,
 * DrawVehicle::Part::Part 0x6935a0 and OnBeginSimulation 0x6937c0, DrawCar::DrawCar 0x6bbc70 and its attachable's
 * deleting destructor 0x6bbc50, DrawPerformanceCar::DrawPerformanceCar 0x6bbf30, DrawRaceCar::DrawRaceCar 0x6bc1c0,
 * DrawHeli::DrawHeli 0x6ba0b0, RenderConn::Pkt_Car_Open 0x68c2e0 and Pkt_Heli_Open 0x68c690). */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x6935a0, DrawVehiclePartConstruct)
NFS_REPLACE(0x6937c0, DrawVehiclePartBeginSimulation)
NFS_REPLACE(0x6b0500, DrawVehicleSpawnModel)
NFS_REPLACE(0x6b9e70, DrawVehicleConstruct)
NFS_REPLACE(0x6bbc50, DrawCarAttachableDelete)
NFS_REPLACE(0x6bbf30, DrawPerformanceCarConstruct)
NFS_REPLACE(0x6bc1c0, DrawRaceCarConstructor)
NFS_REPLACE(0x6ba0b0, DrawHeliConstructor)
NFS_REPLACE(0x68c690, HeliOpenPacketConstruct)

namespace
{
template <typename T> T &At(void *base, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}

template <typename R, typename... A> R Virtual(void *object, int offset, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*reinterpret_cast<void ***>(object))[offset / 4])(object, args...);
}

const int kObjectList = 0x10;
const int kInterface = 0x94;
const int kTrigger = 0xa0;
const int kAttributes = 0xa4;
const int kSequence = 0xac;
const int kBroken = 0xb8;
const int kBrokenTime = 0xbc;
const int kIndex = 0xc0;
const int kModelKind = 2;
void *const kInterfaceHandle = reinterpret_cast<void *>(0x667d30);
int &PartCount()
{
    return *reinterpret_cast<int *>(0x937808);
}
/** How many parts and models there may be before a vehicle makes no more. */
const int kMostParts = 0x78;
const int kMostModels = 0x8f8;
int ModelCount()
{
    return *reinterpret_cast<const int *>(0x92d944);
}
void *const kSimAllocator = reinterpret_cast<void *>(0x925b30);
const unsigned int kPartSize = 0xc4;
const int kPartModel = 0x34;
/** DrawVehicle, from its IModel: its collision bounds, and the object that numbers its parts. */
const int kVehicleBounds = 0x14;
const int kVehicleNumbering = -0x20;
const int kModelFromBase = 0x54;
const unsigned int kDrawEvent = 0x804c146e;
const unsigned int kBreakThreshold = 0xc141f7f8;

/** What BeginDraw takes: the part's index, its model's shape and bounds, its collision. */
struct DrawParams
{
    unsigned int vtable;
    unsigned int index;
    unsigned int shape;
    unsigned int bounds;
    unsigned int collision;
};
const unsigned int kRenderService = 0x804c146e;
const char *const kNoName = reinterpret_cast<const char *>(0x890978);

void *Find(char *self, unsigned int handle)
{
    return NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(At<void *>(At<char *>(self, 0x34), 4),
                                                                       reinterpret_cast<void *>(handle));
}

void SetVtables(char *self, unsigned int main, unsigned int second, unsigned int renderable, unsigned int model,
                unsigned int attachable)
{
    At<unsigned int>(self, 0) = main;
    At<unsigned int>(self, 8) = second;
    At<unsigned int>(self, 0x4c) = renderable;
    At<unsigned int>(self, 0x54) = model;
    At<unsigned int>(self, 0x60) = attachable;
}

/** The render connection's name: its model's, or none. */
const char *RenderName(char *self)
{
    char *info = Virtual<char *>(At<void *>(self, 0x48), 0x48);
    const char *name = At<const char *>(At<char *>(info, 8), 0x1c);
    return name ? name : kNoName;
}

void *OpenService(char *self, void *packet)
{
    return NFS_ORIGINAL(0x6f4400, void *(__thiscall *)(void *, unsigned int, void *))(self, kRenderService, packet);
}
} // namespace

struct CarOpenPacket
{
    unsigned int vtable;
    unsigned int name;
    unsigned int world;
    int usage;
    unsigned int attributes;
    const void *collection;
    bool simulated;
};

struct HeliOpenPacket
{
    unsigned int vtable;
    unsigned int name;
    unsigned int world;
    bool simulated;
};

void *__fastcall DrawVehiclePartConstruct(char *self, void *, void *model, unsigned int index, const void *bounds,
                                          const void *attributes, unsigned int name)
{
    NFS_ORIGINAL(0x6fc5f0, void *(__thiscall *)(void *, void *, const void *, unsigned int, int))(self, model, bounds,
                                                                                                    name, kModelKind);
    char *list = self ? self + kObjectList : nullptr;
    char *iface = self + kInterface;
    At<unsigned int>(iface, 0) = 0x890970;
    At<char *>(iface, 4) = list;
    NFS_ORIGINAL(0x5d7930, void(__thiscall *)(void *, void *, void *))(list, kInterfaceHandle, iface);
    At<unsigned int>(iface, 0) = 0x8a93b8;
    At<unsigned int>(self, 0) = 0x8ab304;
    At<unsigned int>(self, 8) = 0x8ab2fc;
    At<unsigned int>(self, 0x2c) = 0x8ab2e0;
    At<unsigned int>(self, 0x34) = 0x8ab260;
    At<unsigned int>(self, 0x40) = 0x8ab258;
    At<unsigned int>(iface, 0) = 0x8ab250;
    At<void *>(self, kTrigger) = nullptr;
    NFS_ORIGINAL(0x6698a0, void *(__thiscall *)(void *, const void *, unsigned int, unsigned int))(
        self + kAttributes, attributes, 0, 0);
    At<unsigned char>(self, kBroken) = 0;
    At<unsigned int>(self, kBrokenTime) = 0;
    At<unsigned int>(self, kIndex) = index;
    ++PartCount();
    return self;
}

void __fastcall DrawVehiclePartBeginSimulation(char *self)
{
    if (void *trigger = At<void *>(self, kTrigger))
        NFS_ORIGINAL(0x667fb0, void(__thiscall *)(void *))(trigger);
    char *model = self + 0x34;
    const unsigned int collision = At<unsigned int>(Virtual<char *>(model, 0x40), 0x28);
    unsigned int bounds;
    Virtual<void, unsigned int *>(model, 8, &bounds);
    const unsigned int index = At<unsigned int>(self, kIndex);
    const unsigned int shape = Virtual<unsigned int>(model, 0xc);
    DrawParams params = {0x8ab180, index, shape, bounds, collision};
    NFS_ORIGINAL(0x6f4620, void(__thiscall *)(void *, unsigned int, DrawParams *))(self, kDrawEvent, &params);
    if (!At<unsigned char>(self, kBroken))
    {
        const float *threshold = static_cast<const float *>(
            NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
                self + kAttributes, kBreakThreshold, 0));
        if (!threshold)
            threshold = static_cast<const float *>(NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(4));
        if (*threshold > 0.0f)
        {
            At<unsigned char>(self, kBroken) = 1;
            At<unsigned int>(self, kBrokenTime) = 0;
        }
    }
    NFS_ORIGINAL(0x6e9f20, void(__thiscall *)(void *, unsigned int))(self, At<unsigned int>(At<char *>(self, kSequence), 8));
}

void *__fastcall DrawVehicleSpawnModel(char *self, void *, unsigned int name, unsigned int child, unsigned int attributes)
{
    if (PartCount() > kMostParts || ModelCount() > kMostModels)
        return nullptr;
    char *bounds = At<char *>(self, kVehicleBounds);
    if (!bounds)
        return nullptr;
    void *childBounds = NFS_ORIGINAL(0x66a6a0, void *(__thiscall *)(void *, void *, unsigned int))(
        At<void *>(bounds, 0x2c), bounds, child);
    if (!childBounds)
        return nullptr;
    void *collection = NFS_ORIGINAL(0x66b670, void *(__cdecl *)(unsigned int))(attributes);
    if (!collection)
        return nullptr;
    char *memory = static_cast<char *>(
        NFS_ORIGINAL(0x5d29d0, void *(__thiscall *)(void *, unsigned int, const char *))(kSimAllocator, kPartSize, nullptr));
    if (!memory)
        return nullptr;
    const unsigned int index = Virtual<unsigned int>(At<void *>(self, kVehicleNumbering), 0x70);
    void *model = reinterpret_cast<unsigned int>(self) != static_cast<unsigned int>(kModelFromBase) ? self : nullptr;
    char *part = static_cast<char *>(DrawVehiclePartConstruct(memory, nullptr, model, index, childBounds, collection, name));
    return part ? part + kPartModel : nullptr;
}

void *__fastcall DrawVehicleConstruct(char *self, void *, const void *params)
{
    NFS_ORIGINAL(0x677480, void *(__thiscall *)(void *, const void *, int))(self, params, 2);
    char *owner = At<char *>(const_cast<void *>(params), 4);
    char *renderable = self + 0x4c;
    At<unsigned int>(renderable, 0) = 0x890970;
    At<char *>(renderable, 4) = owner ? owner + kObjectList : nullptr;
    NFS_ORIGINAL(0x5d7930, void(__thiscall *)(void *, void *, void *))(At<char *>(renderable, 4),
                                                                        reinterpret_cast<void *>(0x404ff0), renderable);
    At<unsigned int>(renderable, 0) = 0x8a93c0;
    char *list = self ? self + kObjectList : nullptr;
    NFS_ORIGINAL(0x6b5900, void *(__thiscall *)(void *, void *))(self + 0x54, list);
    char *attachable = self + 0x60;
    At<unsigned int>(attachable, 0) = 0x890970;
    At<char *>(attachable, 4) = list;
    NFS_ORIGINAL(0x5d7930, void(__thiscall *)(void *, void *, void *))(list, reinterpret_cast<void *>(0x402820),
                                                                        attachable);
    At<unsigned int>(attachable, 0) = 0x894974;
    At<unsigned int>(self, 0x54) = 0x8ad488;
    At<unsigned int>(self, 0) = 0x8ad52c;
    At<unsigned int>(self, 8) = 0x8ad524;
    At<unsigned int>(renderable, 0) = 0x8ad508;
    At<unsigned int>(attachable, 0) = 0x8ad468;
    At<unsigned int>(self, 0x68) = 0;
    char *attachments = static_cast<char *>(
        NFS_ORIGINAL(0x5d29d0, void *(__thiscall *)(void *, unsigned int, const char *))(kSimAllocator, 0x14, nullptr));
    if (attachments)
    {
        At<unsigned int>(attachments, 0) = 0x8952f0;
        At<void *>(attachments, 8) = NFS_ORIGINAL(0x478ec0, void *(__thiscall *)(void *))(attachments + 4);
        At<unsigned int>(attachments, 0xc) = 0;
        At<char *>(attachments, 0x10) = attachable;
    }
    At<char *>(self, 0x6c) = attachments;
    At<void *>(self, 0x74) = NFS_ORIGINAL(0x6a5fe0, void *(__thiscall *)(void *))(self + 0x70);
    At<unsigned int>(self, 0x78) = 0;
    At<unsigned int>(self, 0x7c) = 0;
    At<unsigned int>(self, 0x80) = 0;
    void *found = NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(At<void *>(At<char *>(self, 0x34), 4),
                                                                               reinterpret_cast<void *>(0x620c50));
    if (found)
        At<unsigned int>(self, 0x68) = Virtual<unsigned int>(found, 4);
    return self;
}

void *__fastcall HeliOpenPacketConstruct(HeliOpenPacket *self, void *, const char *name, unsigned int world,
                                         bool simulated)
{
    self->vtable = 0x8ab148;
    self->name = bStringHash(name);
    self->world = world;
    self->simulated = simulated;
    return self;
}

void *__fastcall DrawCarConstructor(char *self, void *, const void *params, int usage)
{
    DrawVehicleConstruct(self, nullptr, params);
    SetVtables(self, 0x8ad83c, 0x8ad834, 0x8ad818, 0x8ad798, 0x8ad778);
    At<void *>(self, 0x84) = nullptr;
    char *hidden = self + 0xa4;
    char *head = static_cast<char *>(NFS_ORIGINAL(0x6a63a0, void *(__thiscall *)(void *))(hidden));
    At<char *>(hidden, 4) = head;
    At<unsigned char>(head, 0x15) = 1;
    At<char *>(head, 4) = head;
    At<char *>(head, 0) = head;
    At<char *>(head, 8) = head;
    At<unsigned int>(hidden, 8) = 0;
    At<unsigned char>(self, 0xb0) = 0;
    At<unsigned int>(self, 0xb4) = 0;
    At<unsigned char>(self, 0xb8) = 0;
    At<void *>(self, 0x90) = Find(self, 0x4040e0);
    At<void *>(self, 0x88) = Find(self, 0x404040);
    At<void *>(self, 0x8c) = Find(self, 0x405000);
    At<void *>(self, 0xa0) = Find(self, 0x68cc70);
    At<void *>(self, 0x94) = Find(self, 0x620fd0);
    At<void *>(self, 0x98) = Find(self, 0x4039e0);
    At<void *>(self, 0x9c) = Find(self, 0x445650);
    if (!At<void *>(self, 0x88))
        return self;
    const char *name = RenderName(self);
    void *view = At<void *>(self, 0x48);
    void *simable = At<void *>(self, 0x34);
    const bool simulated = Virtual<bool>(view, 0x40);
    void *instance = Virtual<void *>(simable, 0x44);
    const void *collection = NFS_ORIGINAL(0x452430, const void *(__thiscall *)(void *))(instance);
    const unsigned int attributes = Virtual<unsigned int>(view, 0xc8);
    const unsigned int world = Virtual<unsigned int>(simable, 0x70);
    CarOpenPacket packet = {0x8ab110, bStringHash(name), world, usage, attributes, collection, simulated};
    At<void *>(self, 0x84) = OpenService(self, &packet);
    return self;
}

void *__fastcall DrawCarAttachableDelete(char *attachable, void *, unsigned int flags)
{
    char *self = attachable - 0x60;
    NFS_ORIGINAL(0x6bbae0, void(__thiscall *)(void *))(self);
    if (flags & 1)
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(kSimAllocator, self, 0xbc,
                                                                                            nullptr);
    return self;
}

void *__fastcall DrawPerformanceCarConstruct(char *self, void *, const void *params, int usage)
{
    DrawCarConstructor(self, nullptr, params, usage);
    SetVtables(self, 0x8ada1c, 0x8ada14, 0x8ad9f8, 0x8ad978, 0x8ad95c);
    At<void *>(self, 0xbc) = Find(self, 0x404010);
    At<void *>(self, 0xc0) = Find(self, 0x404020);
    return self;
}

void *__fastcall DrawRaceCarConstructor(char *self, void *, const void *params, int usage)
{
    DrawPerformanceCarConstruct(self, nullptr, params, usage);
    SetVtables(self, 0x8adcec, 0x8adce4, 0x8adcc8, 0x8adc48, 0x8adc2c);
    At<void *>(self, 0xc4) = Find(self, 0x604250);
    return self;
}

void *__fastcall DrawHeliConstructor(char *self, void *, const void *params)
{
    DrawVehicleConstruct(self, nullptr, params);
    SetVtables(self, 0x8ad61c, 0x8ad614, 0x8ad5f8, 0x8ad578, 0x8ad558);
    At<void *>(self, 0x84) = nullptr;
    NFS_ORIGINAL(0x6eea40, void *(__thiscall *)(void *, unsigned int, const void *))(self + 0x88, 0, nullptr);
    const unsigned int key = Attrib::StringToKey(reinterpret_cast<const char *>(0x8ad6e8));
    NFS_ORIGINAL(0x69b6c0, void *(__thiscall *)(void *, unsigned int, unsigned int, void *))(self + 0xe0, key, 0,
                                                                                           nullptr);
    At<unsigned char>(self, 0xf8) = 0;
    At<unsigned int>(self, 0xfc) = 0;
    const char *name = RenderName(self);
    const bool simulated = Virtual<bool>(At<void *>(self, 0x48), 0x40);
    const unsigned int world = Virtual<unsigned int>(At<void *>(self, 0x34), 0x70);
    HeliOpenPacket packet;
    HeliOpenPacketConstruct(&packet, nullptr, name, world, simulated);
    unsigned int task = stringhash32(reinterpret_cast<const char *>(0x8918c0));
    void *handle = NFS_ORIGINAL(0x6ee9b0, void *(__thiscall *)(void *, const unsigned int *, float, float, int))(
        self, &task, 1.0f, 0.0f, 1);
    At<void *>(self, 0xf4) = handle;
    NFS_ORIGINAL(0x6e8e90, void(__cdecl *)(void *, const char *))(handle, reinterpret_cast<const char *>(0x8ad6dc));
    At<void *>(self, 0x84) = OpenService(self, &packet);
    At<void *>(self, 0x104) = Find(self, 0x4040e0);
    At<void *>(self, 0x108) = Find(self, 0x4039c0);
    return self;
}
