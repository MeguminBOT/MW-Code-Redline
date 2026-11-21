/** Small cop pursuit members that forward to another interface or adjust a few fields (original: zAI AICopManager,
 * AIPursuit, AIRoadBlock, AIPerpVehicle, AIVehiclePursuit, PursuitFormation). */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x406320, AICopManagerMessageForcePursuitStart)
NFS_REPLACE(0x408f70, AIPerpVehicleSetRacerInfo)
NFS_REPLACE(0x40aa80, AIPursuitEndCurrentFormation)
NFS_REPLACE(0x40b040, AIPursuitEndPursuitEnteringSafehouse)
NFS_REPLACE(0x40b060, AIRoadBlockRemoveVehicle)
NFS_REPLACE(0x416380, AIPerpVehicleOnBehaviorChange)
NFS_REPLACE(0x4198d0, AIPursuitIsTarget)
NFS_REPLACE(0x4234d0, AIVehiclePursuitDoInPositionGoal)
NFS_REPLACE(0x42bcf0, AIPursuitClearGroundSupportRequest)
NFS_REPLACE(0x4338a0, AIPursuitAddVehicle)
NFS_REPLACE(0x4338b0, AIPursuitRemoveVehicle)
NFS_REPLACE(0x433a50, AIPursuitNotifySpikeStripsDodged)
NFS_REPLACE(0x433a80, AIPursuitNotifyHeliSpikeStripDeployed)
NFS_REPLACE(0x433ca0, AIPursuitGetNumCopsRemainingInWave)
NFS_REPLACE(0x43c3f0, PursuitFormationAddTargetOffset)
NFS_REPLACE(0x4073c0, AIActionStaticRoadBlockCanBeAttempted)
NFS_REPLACE(0x407400, AIActionStaticRoadBlockUpdate)
NFS_REPLACE(0x413da0, AIActionStaticRoadBlockOnBehaviorChange)
NFS_REPLACE(0x416390, AIPerpVehicleIsBeingPursued)
NFS_REPLACE(0x432320, AIVehiclePursuitSetPursuitOffset)
NFS_REPLACE(0x432350, AIVehiclePursuitSetInPositionOffset)
NFS_REPLACE(0x432550, AIVehicleHelicopterSetLookAtPosition)
NFS_REPLACE(0x432570, AIVehicleHelicopterGetLookAtPosition)
NFS_REPLACE(0x4333f0, AIPerpVehicleGetShortcutSkill)
NFS_REPLACE(0x433940, AIPursuitAttemptingToReAquire)
NFS_REPLACE(0x433990, AIPursuitGetNearestCopInRoadblock)
NFS_REPLACE(0x433a10, AIPursuitNotifyPropertyDamaged)
NFS_REPLACE(0x433be0, AIPursuitPursuitMeterCanShowBusted)
NFS_REPLACE(0x418e20, AIGoalHeliExitUpdate)
NFS_REPLACE(0x4163f0, AIPerpVehicleOnCausedExplosion)

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

const int kVehicleFromPursuitAI = 0x758;
} // namespace

void __fastcall AICopManagerMessageForcePursuitStart(char *self, void *, const char *message)
{
    Virtual<void, int>(self + 0x54, 0x34, At<int>(const_cast<char *>(message), 0x10));
}

void __fastcall AIPerpVehicleSetRacerInfo(char *self, void *, void *info)
{
    At<void *>(self, 0x4c) = info;
    NFS_ORIGINAL(0x408e30, void(__thiscall *)(void *))(self - kVehicleFromPursuitAI);
}

void __fastcall AIPursuitEndCurrentFormation(char *self)
{
    At<void *>(self, 0xc4) = nullptr;
    At<float>(self, 0xb0) = -1.0f;
}

void __fastcall AIPursuitEndPursuitEnteringSafehouse(char *self)
{
    At<int>(self, 0x218) = 4;
    At<float>(self, 0xfc) = 1.0f;
    At<unsigned char>(self, 0x240) = 1;
}

bool __fastcall AIRoadBlockRemoveVehicle(char *self, void *, void *vehicle)
{
    return Virtual<bool, void *>(self - 0x1c, 0xc, vehicle);
}

void __fastcall AIPerpVehicleOnBehaviorChange(char *self, void *, const unsigned int *behavior)
{
    NFS_ORIGINAL(0x414fc0, void(__thiscall *)(void *, const unsigned int *))(self, behavior);
}

bool __fastcall AIPursuitIsTarget(char *self, void *, const void *target)
{
    return NFS_ORIGINAL(0x418090, bool(__thiscall *)(void *, const void *))(At<void *>(self, 0x74), target);
}

void __fastcall AIVehiclePursuitDoInPositionGoal(char *self)
{
    NFS_ORIGINAL(0x422480, void(__thiscall *)(void *, const void *))(self - kVehicleFromPursuitAI, self + 0x3c);
}

void __fastcall AIPursuitClearGroundSupportRequest(char *self)
{
    NFS_ORIGINAL(0x42b410, void(__thiscall *)(void *))(self + 0x194);
}

bool __fastcall AIPursuitAddVehicle(char *self, void *, void *vehicle)
{
    return Virtual<bool, void *>(self - 0x1c, 8, vehicle);
}

bool __fastcall AIPursuitRemoveVehicle(char *self, void *, void *vehicle)
{
    return Virtual<bool, void *>(self - 0x1c, 0xc, vehicle);
}

void __fastcall AIPursuitNotifySpikeStripsDodged(char *self, void *, int count)
{
    At<int>(self, 0x16c) += count;
}

void __fastcall AIPursuitNotifyHeliSpikeStripDeployed(char *self, void *, int count)
{
    At<int>(self, 0x180) += count;
}

int __fastcall AIPursuitGetNumCopsRemainingInWave(char *self)
{
    const int remaining = At<int>(self, 0x144) - At<int>(self, 0x14c);
    return remaining < 1 ? 1 : remaining;
}

void __fastcall PursuitFormationAddTargetOffset(char *self, void *, const void *offset)
{
    NFS_ORIGINAL(0x4393f0, void(__thiscall *)(void *, const void *))(self, offset);
}

bool __fastcall AIActionStaticRoadBlockCanBeAttempted(char *self, void *, float)
{
    return At<void *>(self, 0x48) && At<void *>(self, 0x4c);
}

void __fastcall AIActionStaticRoadBlockUpdate(char *self, void *, float)
{
    void *input = At<void *>(self, 0x4c);
    Virtual<void, float>(input, 0x18, 0.0f);
    Virtual<void, float>(At<void *>(self, 0x4c), 0x1c, 1.0f);
    Virtual<void, float>(At<void *>(self, 0x4c), 0x14, 0.0f);
    Virtual<void, float>(At<void *>(self, 0x4c), 0x2c, 0.0f);
    Virtual<void, float>(At<void *>(self, 0x4c), 0x24, 0.0f);
    Virtual<void, float>(At<void *>(self, 0x4c), 0x20, 0.0f);
}

void __fastcall AIActionStaticRoadBlockOnBehaviorChange(char *self, void *, const unsigned int *behavior)
{
    if (*behavior != *reinterpret_cast<const unsigned int *>(0x9261b8))
        return;
    At<void *>(self, 0x4c) = NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(
        At<void *>(At<char *>(self, 0x30), 4), reinterpret_cast<void *>(0x403f90));
}

bool __fastcall AIPerpVehicleIsBeingPursued(char *self)
{
    void *simable = Virtual<void *>(self - 0x70c, 4);
    char *list = reinterpret_cast<char *>(0x92fd2c);
    for (void **it = At<void **>(list, 0); it != At<void **>(list, 0) + At<int>(list, 8); ++it)
    {
        char *target = Virtual<char *>(*it, 8);
        if (target && At<void *>(target, 0x1c) == simable)
            return true;
    }
    return false;
}

void __fastcall AIVehiclePursuitSetPursuitOffset(char *self, void *, const float *offset)
{
    for (int i = 0; i < 3; ++i)
        At<float>(self, 0x24 + i * 4) = offset[i];
}

void __fastcall AIVehiclePursuitSetInPositionOffset(char *self, void *, const float *offset)
{
    for (int i = 0; i < 3; ++i)
        At<float>(self, 0x30 + i * 4) = offset[i];
}

void __fastcall AIVehicleHelicopterSetLookAtPosition(char *self, void *, float x, float y, float z)
{
    At<float>(self, 0x14) = x;
    At<float>(self, 0x18) = y;
    At<float>(self, 0x1c) = z;
}

float *__fastcall AIVehicleHelicopterGetLookAtPosition(char *self, void *, float *out)
{
    out[0] = At<float>(self, 0x14);
    out[1] = At<float>(self, 0x18);
    out[2] = At<float>(self, 0x1c);
    return out;
}

float __fastcall AIPerpVehicleGetShortcutSkill(char *self)
{
    const float skill = At<float>(self, 0x76c) + At<float>(self, 0x75c);
    if (!(skill > 0.0f))
        return 0.0f;
    return 1.0f < skill ? 1.0f : skill;
}

bool __fastcall AIPursuitAttemptingToReAquire(char *self)
{
    return !At<unsigned char>(self, 0x114) && !At<unsigned char>(self, 0xe8) && !At<unsigned char>(self, 0xe9);
}

void *__fastcall AIPursuitGetNearestCopInRoadblock(char *self, void *, float *distance)
{
    if (distance)
        *distance = At<float>(self, 0x7c);
    return At<void *>(self, 0x78);
}

void __fastcall AIPursuitNotifyPropertyDamaged(char *self, void *, int value)
{
    At<int>(self, 0x174) += value;
    ++At<int>(self, 0x178);
}

bool __fastcall AIPursuitPursuitMeterCanShowBusted(char *self)
{
    if (!At<unsigned char>(self, 0x12c))
        return true;
    return At<float>(self, 0x130) > 2.5f;
}

void __fastcall AIGoalHeliExitUpdate(char *self, void *, float dt)
{
    Virtual<void, float>(self, 4, dt);
    if (void *action = At<void *>(self, 4))
        Virtual<void, float>(action, 0x1c, dt);
    const unsigned int exit = stringhash32(reinterpret_cast<const char *>(0x89110c));
    char *action = At<char *>(self, 4);
    if (!action || exit != At<unsigned int>(action, 0x38) || !Virtual<bool>(action, 0xc))
        return;
    void *ai = NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(At<void *>(At<char *>(self, 0x14), 4),
                                                                           reinterpret_cast<void *>(0x403700));
    Virtual<void>(ai, 0x8c);
}

void __fastcall AIPerpVehicleOnCausedExplosion(char *self, void *, void *explosion, void *victim)
{
    const float when = Virtual<float>(explosion, 0x18);
    const float now = NFS_ORIGINAL(0x6e8de0, float(__cdecl *)())();
    if (!(now - when > 2.0f))
    {
        const int kind = Virtual<int>(victim, 4);
        void *simable = Virtual<void *>(victim, 0x80);
        const bool active = simable && Virtual<bool>(simable, 0x54);
        int points = 0;
        if (kind == 2)
        {
            if (active)
            {
                alignas(8) unsigned char smackable[0x20];
                NFS_ORIGINAL(0x407cc0, void *(__thiscall *)(void *, const void *))(smackable, Virtual<const void *>(victim, 0x44));
                const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
                    smackable, 0x6db7d192, 0);
                if (!value)
                    value = NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(4);
                points = *static_cast<const int *>(value);
                NFS_ORIGINAL(0x45a430, void(__thiscall *)(void *))(smackable);
            }
        }
        else if (kind == 1)
        {
            void *vehicle = NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(At<void *>(victim, 4),
                                                                                    reinterpret_cast<void *>(0x4040e0));
            if (!Virtual<bool>(vehicle, 0x7c) &&
                NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(At<void *>(victim, 4),
                                                                           reinterpret_cast<void *>(0x4038e0)))
                points = 2000;
        }
        if (points && Virtual<void *>(self - 0x714, 0xa0))
            Virtual<void, int>(self - 8, 0x18, points);
    }
    Virtual<void, unsigned int, float>(victim, 0x84, At<unsigned int>(self, 8), when);
}
