/** Spike strips: a strip punctures the tyres of the cars whose wheels it touches (original: zPhysicsBehaviors
 * SpikeStrip). */
#include "legacy/link.h"

NFS_REPLACE(0x6a4ba0, SpikeStripConstruct)
NFS_REPLACE(0x6a4c40, SpikeStripOnBehaviorChange)
NFS_REPLACE(0x6a4c80, SpikeStripOnCollide)
NFS_REPLACE(0x6a4dc0, SpikeStripOnTaskSimulate)
NFS_REPLACE(0x6b0a50, SpikeStripFactory)

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

void *Find(void *object, unsigned int handle)
{
    return NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(At<void *>(object, 4),
                                                                       reinterpret_cast<void *>(handle));
}

const float kTyreWidth = 0.15f;
const unsigned int kCollisionBodies = 0xa0;

/** Watches its simable's collision map (SimCollisionMap) for bodies. */
void WatchCollisions(char *self)
{
    At<void *>(self, 0x48) = Find(At<void *>(self, 0x34), 0x668400);
    if (void *collisions = At<void *>(self, 0x48))
        Virtual<void, unsigned int, unsigned int>(collisions, 4, 0x20b, 0x8001);
}

struct Geometry
{
    alignas(16) unsigned char bytes[0xc0];
};
} // namespace

void *__fastcall SpikeStripConstruct(char *self, void *, const void *params)
{
    NFS_ORIGINAL(0x6717f0, void *(__thiscall *)(void *, const void *, int))(self, params, 0);
    At<unsigned int>(self, 0) = 0x8aba94;
    At<unsigned int>(self, 8) = 0x8aba8c;
    WatchCollisions(self);
    return self;
}

void __fastcall SpikeStripOnBehaviorChange(char *self, void *, const unsigned int *behavior)
{
    if (*behavior == *reinterpret_cast<const unsigned int *>(0x92c5e0))
        WatchCollisions(self);
}

void __fastcall SpikeStripOnCollide(char *, void *, const void *strip, void *body, float dt)
{
    void *tyres = Find(body, 0x68cc70);
    if (!tyres)
        return;
    void *suspension = Find(body, 0x404040);
    if (!suspension)
        return;
    const unsigned int wheels = Virtual<unsigned int>(suspension, 8);
    for (unsigned int wheel = 0; wheel < wheels; ++wheel)
    {
        if (!Virtual<bool, unsigned int>(suspension, 0x24, wheel) || Virtual<int, unsigned int>(tyres, 4, wheel))
            continue;
        const float *velocity = Virtual<const float *>(body, 0x24);
        const float travel[3] = {dt * velocity[0], dt * velocity[1], dt * velocity[2]};
        alignas(16) float orientation[16];
        Virtual<void, float *>(body, 0x40, orientation);
        float position[3];
        Virtual<void, float *, unsigned int>(suspension, 0x14, position, wheel);
        const float radius = Virtual<float, unsigned int>(suspension, 0x68, wheel);
        const float dimensions[3] = {kTyreWidth, radius, radius};
        Geometry tyre;
        NFS_ORIGINAL(0x48b5a0, void *(__thiscall *)(void *, const float *, const float *, const float *, int, const float *))(
            tyre.bytes, orientation, position, dimensions, 1, travel);
        if (NFS_ORIGINAL(0x48c100, bool(__cdecl *)(const void *, const void *, void *))(strip, tyre.bytes, tyre.bytes))
            Virtual<void, unsigned int>(tyres, 0xc, wheel);
    }
}

void __fastcall SpikeStripOnTaskSimulate(char *self, void *, float dt)
{
    if (!At<void *>(self, 0x48))
        return;
    void *model = Virtual<void *>(At<void *>(self, 0x34), 0x80);
    if (!model)
        return;
    void *bounds = Virtual<void *>(model, 0x40);
    if (!bounds || !At<void *>(self, 0x48))
        return;
    void *map = Virtual<void *>(At<void *>(self, 0x48), 0x14);
    alignas(16) float matrix[16];
    if (map && NFS_ORIGINAL(0x667aa0, bool(__thiscall *)(void *))(map))
    {
        Virtual<void, float *>(model, 0x3c, matrix);
        const float position[3] = {matrix[12], matrix[13], matrix[14]};
        const float *identity = reinterpret_cast<const float *>(0x8a2fd0);
        for (int i = 0; i < 4; ++i)
            matrix[12 + i] = identity[i];
        float half[3];
        NFS_ORIGINAL(0x668520, void *(__thiscall *)(void *, float *))(bounds, half);
        Geometry strip;
        NFS_ORIGINAL(0x48b5a0, void *(__thiscall *)(void *, const float *, const float *, const float *, int, const float *))(
            strip.bytes, matrix, position, half, 1, reinterpret_cast<const float *>(0x8a2fb4));
        for (unsigned int i = 0; i < kCollisionBodies; ++i)
        {
            if (!NFS_ORIGINAL(0x667ac0, bool(__thiscall *)(void *, unsigned int))(map, i))
                continue;
            if (void *body = NFS_ORIGINAL(0x6e8be0, void *(__thiscall *)(void *, unsigned int))(map, i))
                SpikeStripOnCollide(self, nullptr, strip.bytes, body, dt);
        }
        return;
    }
    void *triggerable = nullptr;
    if (!NFS_ORIGINAL(0x66edf0, bool(__thiscall *)(void *, void **))(model, &triggerable))
        return;
    Virtual<void, float *>(At<void *>(self, 0x34), 0x64, matrix);
    Virtual<void, const float *, int>(triggerable, 4, matrix, 1);
    Virtual<void>(At<void *>(self, 0x34), 8);
}

void *__cdecl SpikeStripFactory(const void *params)
{
    void *memory = NFS_ORIGINAL(0x5d29d0, void *(__thiscall *)(void *, unsigned int, const char *))(
        reinterpret_cast<void *>(0x925b30), 0x4c, nullptr);
    return memory ? SpikeStripConstruct(static_cast<char *>(memory), nullptr, params) : nullptr;
}
