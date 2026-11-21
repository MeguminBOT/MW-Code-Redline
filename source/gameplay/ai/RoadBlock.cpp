/** A roadblock: its cops, smackables, centre and pursuit (original: zAI AIRoadBlock). */
#include <math.h>

#include "legacy/link.h"

NFS_REPLACE(0x419da0, AIRoadBlockAddVehicle)
NFS_REPLACE(0x419de0, AIRoadBlockGetNumCops)
NFS_REPLACE(0x42bf80, AIRoadBlockIsComprisedOf)
NFS_REPLACE(0x42f9b0, AIRoadBlockOnDetached)
NFS_REPLACE(0x432920, AIRoadBlockSetPursuit)
NFS_REPLACE(0x4329f0, AIRoadBlockSetRoadBlockCentre)
NFS_REPLACE(0x432ab0, AIRoadBlockReleaseAllSmackables)
NFS_REPLACE(0x42e1e0, IPursuitConstruct)
NFS_REPLACE(0x42e290, IRoadBlockConstruct)
NFS_REPLACE(0x43dab0, AIRoadBlockConstruct)
NFS_REPLACE(0x43db80, AIRoadBlockFactory)
NFS_REPLACE(0x43dc10, AIRoadBlockAddSmackable)
NFS_REPLACE(0x43dc30, AIRoadBlockOnAttached)
NFS_REPLACE(0x42bd00, AIRoadBlockGetMinDistanceToTarget)

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

void *const kPursuitAiHandle = reinterpret_cast<void *>(0x4038e0);
void *const kVehicleHandle = reinterpret_cast<void *>(0x4040e0);

void *Find(void *object, void *handle)
{
    return NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(At<void *>(object, 4), handle);
}
} // namespace

bool __fastcall AIRoadBlockAddVehicle(char *self, void *, void *vehicle)
{
    const bool added = Virtual<bool, void *>(self - 0x1c, 8, vehicle);
    if (void *pursuitAi = Find(vehicle, kPursuitAiHandle))
        Virtual<void>(pursuitAi, 8);
    return added;
}

int __fastcall AIRoadBlockGetNumCops(char *self)
{
    void **begin = At<void **>(self, 0xc);
    return begin ? static_cast<int>(At<void **>(self, 0x10) - begin) : 0;
}

void *__fastcall AIRoadBlockIsComprisedOf(char *self, void *, void *simable)
{
    void **begin = At<void **>(self, 0xc);
    if (!begin || At<void **>(self, 0x10) == begin)
        return nullptr;
    for (void **it = begin; it != At<void **>(self, 0x10); ++it)
    {
        void *vehicle = *it;
        if (Virtual<void *>(Virtual<void *>(vehicle, 4), 0x34) == simable)
            return vehicle;
    }
    return nullptr;
}

void __fastcall AIRoadBlockOnDetached(char *self, void *, char *attachable)
{
    char *pursuit = At<char *>(self, 0x38);
    if (pursuit && attachable && At<void *>(attachable, 4) == At<void *>(pursuit, 4))
    {
        At<void *>(self, 0x38) = nullptr;
        return;
    }
    if (!attachable && !pursuit)
    {
        At<void *>(self, 0x38) = nullptr;
        return;
    }
    void *vehicle = Find(attachable, kVehicleHandle);
    if (!vehicle)
        return;
    void **begin = At<void **>(self, 0x1c);
    void **end = At<void **>(self, 0x20);
    void **it = begin;
    while (it != end && *it != vehicle)
        ++it;
    if (it == end)
        return;
    for (void **next = it + 1; next != end; ++next)
        next[-1] = *next;
    At<void **>(self, 0x20) = end - 1;
}

void __fastcall AIRoadBlockSetPursuit(char *self, void *, void *pursuit)
{
    void *current = At<void *>(self, 0x28);
    if (current == pursuit)
        return;
    if (current)
    {
        Virtual<bool, void *>(self - 0x1c, 0xc, current);
        At<void *>(self, 0x28) = nullptr;
    }
    Virtual<bool, void *>(self - 0x1c, 8, pursuit);
    At<void *>(self, 0x28) = pursuit;
}

void __fastcall AIRoadBlockSetRoadBlockCentre(char *self, void *, const float *centre, const float *direction)
{
    float *own = &At<float>(self, 0x34);
    for (int i = 0; i < 3; ++i)
        own[i] = centre[i];
    for (int i = 0; i < 3; ++i)
        own[3 + i] = direction[i];
}

void __fastcall AIRoadBlockReleaseAllSmackables(char *self)
{
    for (void **it = At<void **>(self, 0x1c); it != At<void **>(self, 0x20); ++it)
        Virtual<void>(*it, 0xc);
    if (void **begin = At<void **>(self, 0x1c))
    {
        const unsigned int bytes = static_cast<unsigned int>(At<void **>(self, 0x24) - begin) * 4;
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
            reinterpret_cast<void *>(0x925b30), begin, bytes, nullptr);
    }
    At<void *>(self, 0x1c) = nullptr;
    At<void *>(self, 0x20) = nullptr;
    At<void *>(self, 0x24) = nullptr;
    At<unsigned short>(self, 0x32) = 0;
}

namespace
{
void *Interface(char *self, void *list, unsigned int handle, unsigned int listable, unsigned int vtable)
{
    At<unsigned int>(self, 0) = 0x890970;
    At<void *>(self, 4) = list;
    NFS_ORIGINAL(0x5d7930, void(__thiscall *)(void *, void *, void *))(list, reinterpret_cast<void *>(handle), self);
    NFS_ORIGINAL(listable, void *(__thiscall *)(void *))(self + 8);
    At<unsigned int>(self, 0) = vtable;
    return self;
}
} // namespace

void *__fastcall IPursuitConstruct(char *self, void *, void *list)
{
    return Interface(self, list, 0x4038c0, 0x425220, 0x8916d8);
}

void *__fastcall IRoadBlockConstruct(char *self, void *, void *list)
{
    return Interface(self, list, 0x4038d0, 0x425290, 0x891820);
}

void *__fastcall AIRoadBlockConstruct(char *self, void *, unsigned int, unsigned int, unsigned int, unsigned int)
{
    NFS_ORIGINAL(0x6f94d0, void *(__thiscall *)(void *, unsigned int))(self, 1);
    IRoadBlockConstruct(self + 0x48, nullptr, self ? self + 0x10 : nullptr);
    At<unsigned int>(self, 0) = 0x8921c4;
    At<unsigned int>(self, 8) = 0x8921bc;
    At<unsigned int>(self, 0x2c) = 0x8921a8;
    At<unsigned int>(self, 0x38) = 0x89218c;
    At<unsigned int>(self, 0x48) = 0x892130;
    static const int kCleared[] = {0x54, 0x58, 0x5c, 0x64, 0x68, 0x6c, 0x70, 0xa0};
    for (const int offset : kCleared)
        At<unsigned int>(self, offset) = 0;
    At<unsigned char>(self, 0x74) = 0;
    At<unsigned short>(self, 0x76) = 0;
    At<unsigned short>(self, 0x78) = 0;
    At<unsigned short>(self, 0x7a) = 0;
    At<unsigned char>(self, 0xa4) = 0;
    const unsigned int *zero = reinterpret_cast<const unsigned int *>(0x8a2fb4);
    At<unsigned int>(self, 0x94) = zero[0];
    At<unsigned int>(self, 0x98) = zero[1];
    At<unsigned int>(self, 0x9c) = zero[2];
    return self;
}

void *__cdecl AIRoadBlockFactory(unsigned int type, unsigned int name, unsigned int data, unsigned int extra)
{
    void *memory = NFS_ORIGINAL(0x5d29d0, void *(__thiscall *)(void *, unsigned int, const char *))(
        reinterpret_cast<void *>(0x925b30), 0xa8, nullptr);
    if (!memory)
        return nullptr;
    char *made = static_cast<char *>(AIRoadBlockConstruct(static_cast<char *>(memory), nullptr, type, name, data, extra));
    return made ? made + 0x2c : nullptr;
}

void __fastcall AIRoadBlockAddSmackable(char *self, void *, void *scenery, bool spikes)
{
    NFS_ORIGINAL(0x439240, void(__thiscall *)(void *, void *const *))(self + 0x18, &scenery);
    if (spikes)
        ++At<short>(self, 0x32);
}

void __fastcall AIRoadBlockOnAttached(char *self, void *, char *attachable)
{
    void *vehicle = Find(attachable, kVehicleHandle);
    if (vehicle)
        NFS_ORIGINAL(0x4391d0, void(__thiscall *)(void *, void *const *))(self + 0x18, &vehicle);
}

namespace
{
const float kNoPursuitDistance = 200.0f;
const float kLevelWithTarget = 1.5f;
const float kEngagementRadius = 300.0f;
const float kNearRoadBlock = 500.0f;
const float kRoadBlockReachSquared = 90000.0f;
const float kRoadBlockWait = 20.0f;
} // namespace

float __fastcall AIRoadBlockGetMinDistanceToTarget(char *self, void *, float dt, float *nearestLevel, void **nearest)
{
    void *pursuit = Virtual<void *>(self, 0x1c);
    if (!pursuit || !Virtual<void *>(Virtual<void *>(self, 0x1c), 8))
        return kNoPursuitDistance;
    const float *chased = reinterpret_cast<const float *>(Virtual<char *>(Virtual<void *>(self, 0x1c), 8) + 0x10);
    Virtual<void *>(Virtual<void *>(self, 0x1c), 0x140);
    float closest = 3.4028235e+38f;
    float closestLevel = 3.4028235e+38f;
    for (void **it = At<void **>(self, 0xc); it != At<void **>(self, 0x10); ++it)
    {
        void *cop = *it;
        if (!Virtual<bool>(cop, 0x88) || Virtual<bool>(cop, 0x7c))
            continue;
        const float *at = Virtual<const float *>(cop, 0xc);
        const float height = fabsf(at[1] - chased[1]);
        at = Virtual<const float *>(cop, 0xc);
        const float dx = at[0] - chased[0];
        const float dy = at[1] - chased[1];
        const float dz = at[2] - chased[2];
        const float distance = sqrtf(dz * dz + dy * dy + dx * dx);
        at = Virtual<const float *>(cop, 0xc);
        const float fz = at[2] - chased[2];
        const float fx = at[0] - chased[0];
        const float flat = sqrtf(fx * fx + fz * fz);
        if (distance < closest)
        {
            closest = distance;
            if (nearest)
                *nearest = cop;
        }
        if (height < kLevelWithTarget && flat < closestLevel)
            closestLevel = flat;
        if (distance < kEngagementRadius)
        {
            if (void *pursuitAi = Find(cop, kPursuitAiHandle))
                Virtual<void>(pursuitAi, 0x88);
        }
    }
    *nearestLevel = closestLevel;
    if (!(dt > 0.0f))
        return closest;
    At<unsigned char>(self, 0x5c) = 0;
    if (!(closestLevel < kNearRoadBlock))
        return closest;
    const float cx = chased[0] - At<float>(self, 0x34);
    const float cy = chased[1] - At<float>(self, 0x38);
    const float cz = chased[2] - At<float>(self, 0x3c);
    if (!(cx * cx + cy * cy + cz * cz < kRoadBlockReachSquared))
    {
        At<float>(self, 0x4c) = chased[0];
        At<float>(self, 0x50) = chased[1];
        At<float>(self, 0x54) = chased[2];
        At<float>(self, 0x58) = 0.0f;
        return closest;
    }
    At<float>(self, 0x58) += dt;
    if (At<float>(self, 0x58) > kRoadBlockWait || Virtual<int>(Virtual<void *>(self, 0x1c), 0x114) == 2)
        At<unsigned char>(self, 0x5c) = 1;
    return closest;
}
