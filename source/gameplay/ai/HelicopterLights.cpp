/** The pursuit helicopter's night lights (gameplay/ai/helilights.h). */
#include <math.h>
#include <windows.h>

#include "engine/rendering/lights/flares.h"
#include "engine/rendering/lights/lights.h"
#include "gameplay/ai/aituning.h"
#include "gameplay/ai/helilights.h"
#include "gameplay/world/timeofday.h"

namespace
{
template <typename R, typename... A> R Virtual(void *object, int offset, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*reinterpret_cast<void ***>(object))[offset / 4])(object, args...);
}

struct PoolFlare
{
    unsigned char unknown00[8];
    unsigned int name;
    unsigned char unknown0c[4];
    float position[3];
    float reflectionHeight;
    float direction[3];
    unsigned char type;
    unsigned char flags;
};

/** AIVehicleHelicopter; its ISimable at +0x34. */
char *const *const kActiveHelicopter = reinterpret_cast<char *const *>(0x90d61c);
const int kHelicopterSimable = 0x34;
const unsigned int kExcludeNoView = 1u;
const unsigned char kFlareOmni = 2;
const unsigned char kDirectional = 0;
const float kNoReflection = 10000.0f;
const unsigned char kRedType = 2;
const unsigned char kWhiteType = 3;
const unsigned char kHeadlightType = 0;
/** LocalLights.cpp makes a flare with this name a spot light. */
const unsigned int kHeadlightName = 0x9db90133;
const float kBeaconHeight = 1.6f;
const float kStrobeSpan = 4.0f;
const float kSearchlightDrop = 1.2f;
const float kSearchlightReach = 75.0f;
const DWORD kBeaconPeriod = 1200;
const DWORD kBeaconOn = 400;
const DWORD kStrobePeriod = 1000;
const DWORD kStrobeFlash = 60;

void Add(const float position[3], const float direction[3], unsigned char type, unsigned char flags, unsigned int name,
         float intensity)
{
    PoolFlare *flare = reinterpret_cast<PoolFlare *>(nfs::flares::Allocate(kExcludeNoView, intensity));
    if (!flare)
        return;
    flare->name = name;
    for (int i = 0; i < 3; ++i)
    {
        flare->position[i] = position[i];
        flare->direction[i] = direction[i];
    }
    flare->reflectionHeight = kNoReflection;
    flare->type = type;
    flare->flags = flags | nfs::lights::kFlareMoving;
}

void World(const float *sim, float *world)
{
    world[0] = sim[2];
    world[1] = -sim[0];
    world[2] = sim[1];
}

void *Body(void *simable)
{
    return simable ? Virtual<void *>(simable, 0x54) : nullptr;
}
} // namespace

namespace nfs::helilights
{
void AddFlares()
{
    char *helicopter = *kActiveHelicopter;
    if (!helicopter)
        return;
    const float night = nfs::timeofday::LocalLights();
    if (night <= 0.0f || !nfs::aituning::CopsImproved())
        return;
    void *body = Body(*reinterpret_cast<void **>(helicopter + kHelicopterSimable));
    if (!body)
        return;
    float at[3];
    World(Virtual<const float *>(body, 0x20), at);
    const float intensity = night < 1.0f ? night : 1.0f;
    const float down[3] = {0.0f, 0.0f, -1.0f};
    const DWORD now = GetTickCount();
    if (now % kBeaconPeriod < kBeaconOn)
    {
        const float beacon[3] = {at[0], at[1], at[2] + kBeaconHeight};
        Add(beacon, down, kRedType, kFlareOmni, 0, intensity);
    }
    const DWORD strobe = now % kStrobePeriod;
    if (strobe < kStrobeFlash || (strobe >= 2 * kStrobeFlash && strobe < 3 * kStrobeFlash))
    {
        float forward[4];
        float side[3];
        Virtual<void, float *>(body, 0x34, forward);
        World(forward, side);
        const float across[2] = {-side[1], side[0]};
        const float length = sqrtf(across[0] * across[0] + across[1] * across[1]);
        if (length > 0.001f)
        {
            const float scale = kStrobeSpan / length;
            for (int s = -1; s <= 1; s += 2)
            {
                const float tip[3] = {at[0] + across[0] * scale * s, at[1] + across[1] * scale * s, at[2]};
                Add(tip, down, kWhiteType, kFlareOmni, 0, intensity);
            }
        }
    }
    if (!*reinterpret_cast<int *>(0x92cd84))
        return;
    void *player = **reinterpret_cast<void ***>(0x92cd7c);
    void *playerBody = player ? Body(Virtual<void *>(player, 4)) : nullptr;
    if (!playerBody)
        return;
    float car[3];
    World(Virtual<const float *>(playerBody, 0x20), car);
    const float light[3] = {at[0], at[1], at[2] - kSearchlightDrop};
    float aim[3] = {car[0] - light[0], car[1] - light[1], car[2] - light[2]};
    const float distance = sqrtf(aim[0] * aim[0] + aim[1] * aim[1] + aim[2] * aim[2]);
    if (distance < 1.0f || distance > kSearchlightReach)
        return;
    for (float &a : aim)
        a /= distance;
    Add(light, aim, kHeadlightType, kDirectional, kHeadlightName, intensity);
}
} // namespace nfs::helilights
