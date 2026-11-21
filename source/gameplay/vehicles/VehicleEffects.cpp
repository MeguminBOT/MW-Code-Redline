/** The collision effects behaviour of vehicles and smackables (original: zPhysicsBehaviors Effects::Effects,
 * EffectsCar::EffectsCar). */
#include <string.h>

#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x69b900, EffectsConstruct)
NFS_REPLACE(0x69bd80, EffectsCarConstructor)

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

const int kListener = 0x48;
const int kEffect = 0x90;
const int kSuspension = 0xec;
void *const kSuspensionHandle = reinterpret_cast<void *>(0x404040);
} // namespace

void *__fastcall EffectsConstruct(char *self, void *, const void *params)
{
    NFS_ORIGINAL(0x6717f0, void *(__thiscall *)(void *, const void *, int))(self, params, 0);
    At<unsigned int>(self, 0) = 0x8ab3b8;
    At<unsigned int>(self, 8) = 0x8ab3ac;
    At<unsigned int>(self, kListener) = 0x8ab3a8;
    memset(self + 0x4c, 0, 0x90 - 0x4c);
    char *owner = At<char *>(const_cast<void *>(params), 4);
    char *simable = owner ? owner + 0x2c : nullptr;
    const unsigned int world = At<unsigned int>(Virtual<void *>(simable, 0x44), 4);
    const unsigned int id = Virtual<unsigned int>(simable, 0x70);
    NFS_ORIGINAL(0x6eea40, void *(__thiscall *)(void *, unsigned int, unsigned int))(self + kEffect, id, world);
    At<int>(self, 0xe8) = 0;
    NFS_ORIGINAL(0x6fbea0, void(__cdecl *)(void *, void *, const char *))(self + kListener, At<void *>(self, 0x34),
                                                                          reinterpret_cast<const char *>(0x8ab3a0));
    return self;
}

void *__fastcall EffectsCarConstructor(char *self, void *, const void *params)
{
    EffectsConstruct(self, nullptr, params);
    At<unsigned int>(self, 0) = 0x8ab458;
    At<unsigned int>(self, 8) = 0x8ab44c;
    At<unsigned int>(self, kListener) = 0x8ab448;
    At<void *>(self, kSuspension) = NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(
        At<void *>(At<void *>(self, 0x34), 4), kSuspensionHandle);
    return self;
}
