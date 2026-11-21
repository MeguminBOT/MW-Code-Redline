/** Steering of cops: pursuit offsets, separation, desired speeds, formation slots (original: zAI AISteer::Pursuit,
 * Separation, GetDesiredSpeedToTarget, AIGoalPursuit::Update, BoxInFormation and RollingBlockFormation getPosition). */
#include <math.h>

#include "legacy/link.h"

NFS_REPLACE(0x409f40, AISteerPursuit)
NFS_REPLACE(0x40a390, AISteerSeparation)
NFS_REPLACE(0x40a560, AISteerGetDesiredSpeedToTarget)
NFS_REPLACE(0x40a700, AIGoalPursuitUpdate)
NFS_REPLACE(0x40a930, BoxInFormationGetPosition)
NFS_REPLACE(0x40a970, RollingBlockFormationGetPosition)

namespace
{
template <typename R, typename... A> R Virtual(void *object, int offset, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*reinterpret_cast<void ***>(object))[offset / 4])(object, args...);
}

const float kLeastSeparationCube = 0.1f;
const float kSlowestChase = 4.4703f;
const float kSlowestCatchUp = 11.17575f;
const float kCatchUpFactor = 1.2f;
const float *const kBoxInSlots = reinterpret_cast<const float *>(0x890e28);
const float *const kRollingBlockSlots = reinterpret_cast<const float *>(0x890e58);
} // namespace

void __cdecl AISteerPursuit(float *out, const float *from, const float *, const float *offset, const float *target)
{
    const float x = target[0] + offset[0];
    const float y = target[1] + offset[1];
    const float z = target[2] + offset[2];
    out[0] = x - from[0];
    out[1] = y - from[1];
    out[2] = z - from[2];
}

void __cdecl AISteerSeparation(float *out, const float *a0, const float *a1, float radiusA, const float *b0,
                               const float *b1, float radiusB)
{
    float away[3];
    NFS_ORIGINAL(0x409f90, void(__cdecl *)(float *, const float *, const float *, const float *, const float *))(
        away, a0, a1, b0, b1);
    const float squared = away[2] * away[2] + away[0] * away[0] + away[1] * away[1];
    const float reach = radiusA + radiusB;
    float cube = squared * sqrtf(squared);
    if (!(cube > kLeastSeparationCube))
        cube = kLeastSeparationCube;
    const float push = reach * reach / cube;
    out[0] += away[0] * push;
    out[1] += away[1] * push;
    out[2] += away[2] * push;
}

float __cdecl AISteerGetDesiredSpeedToTarget(float distance, float speed)
{
    const float wanted = distance * 0.5f + speed;
    if (distance < 0.0f)
        return wanted < kSlowestChase ? kSlowestChase : wanted;
    float most = speed * kCatchUpFactor;
    if (kSlowestCatchUp > most)
        most = kSlowestCatchUp;
    return most < wanted ? most : wanted;
}

void __fastcall AIGoalPursuitUpdate(char *self, void *, float dt)
{
    Virtual<void, float>(self, 4, dt);
    if (void *formation = *reinterpret_cast<void **>(self + 4))
        Virtual<void, float>(formation, 0x1c, dt);
}

void __fastcall BoxInFormationGetPosition(void *, void *, int slot, float scale, float *out)
{
    const float *position = kBoxInSlots + slot * 3;
    out[0] = scale * position[0];
    out[1] = scale * position[1];
    out[2] = scale * position[2];
}

void __fastcall RollingBlockFormationGetPosition(void *, void *, int slot, float scale, float *out)
{
    const float *position = kRollingBlockSlots + slot * 3;
    out[0] = scale * position[0];
    out[1] = scale * position[1];
    out[2] = scale * position[2];
}
