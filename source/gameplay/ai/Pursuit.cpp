/** A pursuit: its cops, roadblocks, formations, costs and how it ends (original: zAI AIPursuit). */
#include <math.h>

#include "game/gen/api.h"
#include "legacy/link.h"
#include "core/log.h"
#include "gameplay/ai/aituning.h"

NFS_REPLACE(0x40a9b0, AIPursuitLockInPursuitAttribs)
NFS_REPLACE(0x40aa00, AIPursuitCalcTotalCostToState)
NFS_REPLACE(0x40ad80, AIPursuitIsPlayerPursuit)
NFS_REPLACE(0x40adc0, AIPursuitShouldEnd)
NFS_REPLACE(0x40ae30, AIPursuitAddRoadBlock)
NFS_REPLACE(0x40ae80, AIPursuitIsFinisherActive)
NFS_REPLACE(0x40aea0, AIPursuitTimeToFinisherAttempt)
NFS_REPLACE(0x40aec0, AIPursuitTimeUntilBusted)
NFS_REPLACE(0x40af40, AIPursuitNotifyCopDamaged)
NFS_REPLACE(0x40afc0, AIPursuitSpikesHit)
NFS_REPLACE(0x418e90, AIPursuitGetPursuitLevelAttrib)
NFS_REPLACE(0x418ee0, AIPursuitGetPursuitSupportAttrib)
NFS_REPLACE(0x418f30, AIPursuitIncNumCopsDestroyed)
NFS_REPLACE(0x4190c0, AIPursuitTrackVehicleCounts)
NFS_REPLACE(0x419160, AIPursuitAssignChopperGoal)
NFS_REPLACE(0x419200, AIPursuitIsHeliInPursuit)
NFS_REPLACE(0x419240, AIPursuitFleeCopOfType)
NFS_REPLACE(0x4194b0, AIPursuitRequestRoadBlock)
NFS_REPLACE(0x419890, AIPursuitIsSupportVehicle)
NFS_REPLACE(0x4198e0, AIPursuitBailPursuit)
NFS_REPLACE(0x419c30, AIPursuitUpdateJerk)
NFS_REPLACE(0x4240e0, AIPursuitContingentHasActiveCops)
NFS_REPLACE(0x42b820, AIPursuitRemoveUnwantedVehicles)
NFS_REPLACE(0x424110, AIPursuitGetAdjustedCopCounts)
NFS_REPLACE(0x40ae10, CopCountRecordConstruct)
NFS_REPLACE(0x42ba50, AIPursuitCopRequest)
NFS_REPLACE(0x419680, AIPursuitRequestGroundSupport)
NFS_REPLACE(0x423df0, AIPursuitOnAttached)
NFS_REPLACE(0x42b550, AIPursuitOnDetached)
NFS_REPLACE(0x40ab70, CopAndAngleSortPredicatePort)
NFS_REPLACE(0x40abb0, AIPursuitAssignCopsInCircle)
NFS_REPLACE(0x440260, AIPursuitAddVehicleToContingent)
NFS_REPLACE(0x440330, AIPursuitInitFormation)
NFS_REPLACE(0x443270, AIPursuitConstructActivity)
NFS_REPLACE(0x433760, AIPursuitDestruct)
NFS_REPLACE(0x442d00, AIPursuitConstruct)
NFS_REPLACE(0x4410a0, AIPursuitSetupCollapse)
NFS_REPLACE(0x4414e0, AIPursuitUpdateOutOfFormationOffsets)
NFS_REPLACE(0x4404f0, AIPursuitEvenOutOffsets)
NFS_REPLACE(0x4407e0, AIPursuitAssignClosestOffsets)
NFS_REPLACE(0x443300, AIPursuitUpdateFormation)

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

const int kTarget = 0x74;
const int kFormation = 0x80;
const int kRoadBlock = 0x84;
const int kHeat = 0xdc;
const int kEvade = 0xfc;
const int kBustedTime = 0x110;
const int kStatus = 0x218;
const int kStatusSafehouse = 4;
const int kSpikedCops = 0x23c;
const int kFormationKind = 0xc0;
const int kFormationRoadBlock = 4;
void **const kGameManager = reinterpret_cast<void **>(0x91e00c);
const char *const kCopsDamagedStat = reinterpret_cast<const char *>(0x890eb0);
const char *const kCopsDestroyedStat = reinterpret_cast<const char *>(0x891120);
const char *const kCopCarsStat = reinterpret_cast<const char *>(0x891150);
const char *const kHelicoptersStat = reinterpret_cast<const char *>(0x89113c);
void *const kPerpHandle = reinterpret_cast<void *>(0x4037e0);
void *const kVehicleAiHandle = reinterpret_cast<void *>(0x403700);
void *const kPursuitAiHandle = reinterpret_cast<void *>(0x4038e0);
const unsigned int *const kHelicopterClass = reinterpret_cast<const unsigned int *>(0x92c4f4);
void **const kCopManager = reinterpret_cast<void **>(0x92c67c);
const unsigned int kCopDestroyedPoints = 0xc07c8040;
const unsigned int kCopDestroyedBonusTime = 0xbef78612;
const float kNotInSightPenalty = 40.0f;

void *Find(void *object, void *handle)
{
    return NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(At<void *>(object, 4), handle);
}

const void *Attribute(void *instance, unsigned int key, unsigned int index)
{
    const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
        instance, key, index);
    return value ? value : NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(4);
}

void TrackValue(const char *name, float value)
{
    NFS_ORIGINAL(0x5eda20, void(__thiscall *)(void *, const char *, float))(*kGameManager, name, value);
}

float Random(float range)
{
    void *random = NFS_ORIGINAL(0x6ed200, void *(__cdecl *)())();
    return NFS_ORIGINAL(0x402870, float(__thiscall *)(void *, float))(random, range);
}

/** A UTL::Vector of cops: its begin, and its count two words on. */
void **CopsBegin(char *list)
{
    return At<void **>(list, 0);
}

void **CopsEnd(char *list)
{
    return At<void **>(list, 0) + At<int>(list, 8);
}

bool IsHelicopter(void *vehicle)
{
    return *Virtual<const unsigned int *>(vehicle, 0x44) == *kHelicopterClass;
}

void *PerpOf(char *target)
{
    void *simable = At<void *>(target, 0x1c);
    return simable ? Find(simable, kPerpHandle) : nullptr;
}
} // namespace

const void *__fastcall AttribGenpursuitsupportMinimumSupportDelay(void *self);

void __fastcall AIPursuitLockInPursuitAttribs(char *self)
{
    char *levels = Virtual<char *>(self, 0x140);
    if (!levels)
        return;
    At<unsigned int>(self, 0x144) = At<unsigned int>(At<char *>(levels, 8), 0x70);
    At<unsigned int>(self, 0x148) = At<unsigned int>(At<char *>(levels, 8), 0x9c);
    At<unsigned int>(self, 0x104) = At<unsigned int>(At<char *>(levels, 8), 0x90);
    At<unsigned int>(self, 0x14c) = 0;
}

unsigned int __fastcall AIPursuitCalcTotalCostToState(char *self)
{
    const auto v = [self](int offset) { return At<unsigned int>(self, offset); };
    const unsigned int events = v(0x13c) * 10 + v(0x150) * 4 + v(0x168) + v(0x158);
    const unsigned int common = (v(0x184) + events * 2 + v(0x17c) + v(0x15c)) * 250;
    const unsigned int spikes = (v(0x180) + v(0x188) * 2) * 225;
    return common + spikes + v(0x174);
}

bool __fastcall AIPursuitIsPlayerPursuit(char *self)
{
    char *target = Virtual<char *>(self, 8);
    if (!target || !At<void *>(target, 0x1c))
        return false;
    return Virtual<void *>(At<void *>(target, 0x1c), 0x20) != nullptr;
}

bool __fastcall AIPursuitShouldEnd(char *self)
{
    if (!At<unsigned char>(At<char *>(self, kTarget), 0x2c))
        return true;
    if (At<float>(self, kEvade) >= 1.0f)
        return true;
    if (At<int>(self, kStatus) == kStatusSafehouse)
        return true;
    if (At<unsigned char>(self, 0xe8) || At<unsigned char>(self, 0xe9))
        return Virtual<void *>(self, 0xc) == nullptr;
    return false;
}

void __fastcall AIPursuitAddRoadBlock(char *self, void *, void *roadBlock)
{
    At<void *>(self, kRoadBlock) = roadBlock;
    Virtual<bool, void *>(self - 0x1c, 8, roadBlock);
    At<int>(self, kSpikedCops) = 0;
    if (At<int>(self, kFormationKind) != kFormationRoadBlock && !Virtual<bool>(self, 0x58))
        Virtual<void>(self, 0x6c);
}

bool __fastcall AIPursuitIsFinisherActive(char *self)
{
    return At<float>(self, 0xb0) >= 0.0f;
}

float __fastcall AIPursuitTimeToFinisherAttempt(char *self)
{
    return Virtual<float>(At<void *>(self, kFormation), 0x10) - At<float>(self, 0xac);
}

float __fastcall AIPursuitTimeUntilBusted(char *self)
{
    const float busted = At<float>(self, kBustedTime);
    if (At<float>(self, kHeat) > 0.03f)
    {
        float scale = At<float>(self, kHeat) * 0.2f;
        if (1.0f < scale)
            scale = 1.0f;
        return scale * (1.0f - busted) + busted;
    }
    if (At<float>(self, kEvade) >= 0.05f)
        return -1.0f;
    return busted;
}

void __fastcall AIPursuitNotifyCopDamaged(char *self, void *, void *vehicle)
{
    if (!At<unsigned char>(self, 0xa8))
        return;
    ++At<int>(self, 0x15c);
    if (void *roadBlock = At<void *>(self, kRoadBlock))
    {
        void *simable = Virtual<void *>(vehicle, 4);
        void *handle = Virtual<void *>(simable, 0x34);
        if (Virtual<void *, void *>(At<void *>(self, kRoadBlock), 0x58, handle))
            Virtual<void>(At<void *>(self, kRoadBlock), 0x30);
    }
    NFS_ORIGINAL(0x5edb50, void(__thiscall *)(void *, const char *))(*kGameManager, kCopsDamagedStat);
}

void __fastcall AIPursuitSpikesHit(char *self, void *, void *vehicleAi)
{
    if (!vehicleAi || At<int>(self, kSpikedCops))
        return;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        void *roadBlock = Virtual<void *>(self, 0x3c);
        if (!roadBlock)
            continue;
        float distance;
        void *vehicle = nullptr;
        Virtual<float, float, float *, void **>(roadBlock, 0x44, 0.0f, &distance, &vehicle);
        if (vehicle && Virtual<bool, void *>(roadBlock, 0xc, vehicle))
        {
            Virtual<void, void *>(self, 0x78, vehicle);
            ++At<int>(self, kSpikedCops);
        }
    }
}

void *__fastcall AIPursuitGetPursuitLevelAttrib(char *self)
{
    char *target = Virtual<char *>(self, 8);
    if (!target)
        return nullptr;
    void *perp = PerpOf(target);
    return perp ? Virtual<void *>(perp, 0x10) : nullptr;
}

void *__fastcall AIPursuitGetPursuitSupportAttrib(char *self)
{
    char *pursuit = self + 0x48;
    char *target = Virtual<char *>(pursuit, 8);
    if (!target)
        return nullptr;
    void *perp = PerpOf(target);
    return perp ? Virtual<void *>(perp, 0x14) : nullptr;
}

void __fastcall AIPursuitIncNumCopsDestroyed(char *self, void *, void *vehicle)
{
    if (!At<unsigned char>(self, 0xa8))
        return;
    if (void *attributes = Virtual<void *>(vehicle, 0xac))
    {
        const unsigned int level = At<unsigned int>(self, 0x98);
        void *instance = Virtual<void *>(attributes, 0x94);
        At<unsigned int>(self, 0xf4) = *static_cast<const unsigned int *>(Attribute(instance, kCopDestroyedPoints, level));
        At<unsigned int>(self, 0xf8) = stringhash32(Virtual<const char *>(vehicle, 0x4c));
        int multiplier = 1;
        if (At<float>(self, 0xec) > 0.0f)
        {
            if (At<int>(self, 0xf0) < 3)
                ++At<int>(self, 0xf0);
            multiplier = At<int>(self, 0xf0);
        }
        else
        {
            At<float>(self, 0xec) = 0.0f;
            At<int>(self, 0xf0) = 1;
        }
        const int points = At<int>(self, 0xf4) * multiplier;
        if (void *levels = Virtual<void *>(self, 0x140))
            At<unsigned int>(self, 0xec) = *static_cast<const unsigned int *>(Attribute(levels, kCopDestroyedBonusTime, 0));
        if (void *perp = PerpOf(At<char *>(self, kTarget)))
            Virtual<void, int>(perp, 0x38, points);
    }
    if (At<void *>(self, kRoadBlock))
    {
        void *simable = Virtual<void *>(vehicle, 4);
        void *handle = Virtual<void *>(simable, 0x34);
        if (Virtual<void *, void *>(At<void *>(self, kRoadBlock), 0x58, handle))
            Virtual<void>(At<void *>(self, kRoadBlock), 0x34);
    }
    const int destroyed = ++At<int>(self, 0x13c);
    TrackValue(kCopsDestroyedStat, static_cast<float>(destroyed));
}

void __fastcall AIPursuitTrackVehicleCounts(char *self)
{
    int cars = 0;
    int helicopters = 0;
    for (void **cop = CopsBegin(self + 0x60); cop != CopsEnd(self + 0x60); ++cop)
    {
        if (IsHelicopter(*cop))
            ++helicopters;
        else
            ++cars;
    }
    if (!*kGameManager || !At<unsigned char>(self, 0xf0))
        return;
    TrackValue(kCopCarsStat, static_cast<float>(cars));
    TrackValue(kHelicoptersStat, static_cast<float>(helicopters));
}

void __fastcall AIPursuitAssignChopperGoal(char *self, void *, char *pursuitAi)
{
    if (At<void *>(self, 0xcc))
        return;
    static const unsigned int pursuitGoal = stringhash32(reinterpret_cast<const char *>(0x891174));
    static const unsigned int heliGoal = stringhash32(reinterpret_cast<const char *>(0x891160));
    void *ai = Find(pursuitAi, kVehicleAiHandle);
    unsigned int goal = pursuitGoal;
    if (Virtual<bool, const unsigned int *>(ai, 0xbc, &goal))
        return;
    goal = heliGoal;
    Virtual<void, const unsigned int *>(pursuitAi, 0x48, &goal);
    Virtual<void, bool>(pursuitAi, 0x30, true);
    const unsigned int *support = Virtual<const unsigned int *>(pursuitAi, 0x4c);
    if (!Virtual<bool, const unsigned int *>(ai, 0xbc, support))
        Virtual<void>(pursuitAi, 0x1c);
}

bool __fastcall AIPursuitIsHeliInPursuit(char *self)
{
    for (void **cop = CopsBegin(self + 0x18); cop != CopsEnd(self + 0x18); ++cop)
    {
        if (IsHelicopter(*cop))
            return true;
    }
    return false;
}

void __fastcall AIPursuitFleeCopOfType(char *self, void *, unsigned int type, int count)
{
    static const unsigned int fleeGoal = stringhash32(reinterpret_cast<const char *>(0x891188));
    int inSight = 0;
    int fleeing = 0;
    void *farthest = nullptr;
    void *second = nullptr;
    float farthestDistance = 0.0f;
    float secondDistance = 0.0f;
    for (void **it = CopsBegin(self + 0x60); it != CopsEnd(self + 0x60); ++it)
    {
        void *cop = *it;
        if (IsHelicopter(cop))
            continue;
        void *pursuitAi = Find(cop, kPursuitAiHandle);
        if (pursuitAi && *Virtual<const unsigned int *>(pursuitAi, 0x80))
            continue;
        if (Virtual<bool>(cop, 0x7c))
            continue;
        if (void *ai = Find(cop, kVehicleAiHandle))
        {
            if (*Virtual<const unsigned int *>(ai, 0xc0) == fleeGoal)
            {
                ++fleeing;
                continue;
            }
        }
        bool seen = false;
        if (!(Virtual<float>(pursuitAi, 0x74) > 0.0f))
        {
            ++inSight;
            seen = true;
        }
        if (stringhash32(Virtual<const char *>(cop, 0x4c)) != type)
            continue;
        const float *target = reinterpret_cast<const float *>(At<char *>(self, 0xbc) + 0x10);
        const float *position = Virtual<const float *>(cop, 0xc);
        const float dx = position[0] - target[0];
        const float dy = position[1] - target[1];
        const float dz = position[2] - target[2];
        float distance = sqrtf(dz * dz + dy * dy + dx * dx);
        if (!seen)
            distance += kNotInSightPenalty;
        if (distance > farthestDistance || !farthest)
        {
            secondDistance = farthestDistance;
            second = farthest;
            farthestDistance = distance;
            farthest = cop;
        }
        else if (distance > secondDistance)
        {
            secondDistance = distance;
            second = cop;
        }
    }
    const int leastInSight = 2;
    if (farthest)
    {
        void *pursuitAi = Find(farthest, kPursuitAiHandle);
        if (pursuitAi && count > fleeing)
        {
            bool flee = true;
            if (!(Virtual<float>(pursuitAi, 0x74) > 0.0f))
            {
                if (inSight <= leastInSight)
                    flee = false;
                else
                    --inSight;
            }
            if (flee)
                Virtual<void>(pursuitAi, 0xc);
        }
    }
    if (second)
    {
        void *pursuitAi = Find(second, kPursuitAiHandle);
        if (pursuitAi && count > fleeing + 1)
        {
            if (Virtual<float>(pursuitAi, 0x74) > 0.0f || inSight > leastInSight)
                Virtual<void>(pursuitAi, 0xc);
        }
    }
}

int __fastcall AIPursuitRequestRoadBlock(char *self)
{
    if (At<unsigned char>(self, 0xe8) || At<unsigned char>(self, 0xe9) || At<void *>(self, kRoadBlock))
        return 0;
    if (At<float>(self, 0xc8) >= 0.0f)
        return 0;
    char *support = static_cast<char *>(AIPursuitGetPursuitSupportAttrib(self - 0x48));
    if (!support)
        return 0;
    if (*static_cast<const float *>(AttribGenpursuitsupportMinimumSupportDelay(support)) > At<float>(self, 0xb4))
        return 0;
    char *levels = Virtual<char *>(self, 0x140);
    if (!levels)
        return 0;
    At<float>(self, 0xc8) = Random(4.0f) + 8.0f;
    const int cops = At<unsigned char>(self, 0x190) ? 4 : 0;
    float chance;
    if (Virtual<unsigned char>(self, 0x28) == 1)
        chance = At<float>(At<char *>(levels, 8), 0x5c);
    else
    {
        const float most = At<float>(At<char *>(levels, 8), 0xc0);
        const float distance = NFS_ORIGINAL(0x4100a0, float(__cdecl *)(const void *, const void *))(
            self + 0x118, At<char *>(self, kTarget) + 0x10);
        chance = At<float>(At<char *>(levels, 8), 0x68) / most * (most - distance);
    }
    At<unsigned char>(self, 0x190) = Random(100.0f) < chance;
    return cops;
}

bool __fastcall AIPursuitIsSupportVehicle(char *, void *, void *vehicle)
{
    void *pursuitAi = Find(vehicle, kPursuitAiHandle);
    if (!pursuitAi)
        return false;
    static const unsigned int none = stringhash32(nullptr);
    return *Virtual<const unsigned int *>(pursuitAi, 0x80) != none;
}

void __fastcall AIPursuitBailPursuit(char *self)
{
    At<unsigned char>(self, 0xe9) = 1;
    void *pursuit = reinterpret_cast<unsigned int>(self) != 0x48 ? self : nullptr;
    Virtual<void, void *>(*kCopManager, 0x10, pursuit);
}

namespace
{
/** pursuitlevels' cop car table: 0x18-byte records after the layout's length, the car's name at 0xc, its weight at 0x10. */
const char *const kNoName = reinterpret_cast<const char *>(0x890978);
const unsigned int *const kExcludedCop = reinterpret_cast<const unsigned int *>(0x90d8c8);
const int kCopRecordSize = 0x18;

unsigned int TableLength(char *levels)
{
    return NFS_ORIGINAL(0x452940, unsigned int(__thiscall *)(void *))(At<void *>(levels, 8));
}

char *CopRecord(char *levels, unsigned int index)
{
    char *table = At<char *>(levels, 8);
    if (index < NFS_ORIGINAL(0x452940, unsigned int(__thiscall *)(void *))(table))
        return table + 8 + index * kCopRecordSize;
    return static_cast<char *>(const_cast<void *>(NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(kCopRecordSize)));
}

const char *CopName(char *record)
{
    const char *name = At<const char *>(record, 0xc);
    return name ? name : kNoName;
}

/** The pursuit levels of the first perp on a list whose AI has them active, else of the last perp seen. */
bool LevelsFromList(char *list, bool skipCars, void **levels)
{
    for (void **it = CopsBegin(list); it != CopsEnd(list); ++it)
    {
        void *vehicle = *it;
        if (skipCars)
        {
            const int kind = Virtual<int>(vehicle, 0x58);
            if (kind == 0 || kind == 6)
                continue;
        }
        void *perp = Find(vehicle, kPerpHandle);
        if (!perp)
            continue;
        void *ai = Find(vehicle, kVehicleAiHandle);
        if (!ai)
            continue;
        const bool active = Virtual<void *>(ai, 0xa0) != nullptr;
        if (*levels && !active)
            continue;
        *levels = Virtual<void *>(perp, 0x10);
        if (active)
            return true;
    }
    return false;
}
} // namespace

Attrib::Gen::pursuitlevels *__cdecl GetGlobalPursuitLevelAttrib()
{
    void *levels = nullptr;
    if (LevelsFromList(reinterpret_cast<char *>(0x92cd7c), false, &levels))
        return static_cast<Attrib::Gen::pursuitlevels *>(levels);
    LevelsFromList(reinterpret_cast<char *>(0x92cf5c), true, &levels);
    return static_cast<Attrib::Gen::pursuitlevels *>(levels);
}

bool __cdecl IsValidPursuitCarName(const char *name)
{
    char *levels = reinterpret_cast<char *>(GetGlobalPursuitLevelAttrib());
    if (!levels)
        return false;
    const unsigned int wanted = stringhash32(name);
    for (unsigned int i = 0; i < TableLength(levels); ++i)
    {
        if (stringhash32(CopName(CopRecord(levels, i))) == wanted)
            return true;
    }
    return false;
}

const char *__cdecl GetRandomValidCopCar()
{
    char *levels = reinterpret_cast<char *>(GetGlobalPursuitLevelAttrib());
    if (!levels)
        return nullptr;
    int total = 0;
    for (unsigned int i = 0; i < TableLength(levels); ++i)
    {
        char *record = CopRecord(levels, i);
        if (stringhash32(CopName(record)) != *kExcludedCop)
            total += At<int>(record, 0x10);
    }
    void *random = NFS_ORIGINAL(0x6ed200, void *(__cdecl *)())();
    int pick = (NFS_ORIGINAL(0x627320, int(__thiscall *)(void *))(random) * total) >> 16;
    for (unsigned int i = 0; i < TableLength(levels); ++i)
    {
        char *record = CopRecord(levels, i);
        if (stringhash32(CopName(record)) == *kExcludedCop)
            continue;
        pick -= At<int>(record, 0x10);
        if (pick < 0)
            return CopName(record);
    }
    return nullptr;
}

void __fastcall AIPursuitUpdateJerk(char *self, void *, float dt)
{
    char *target = At<char *>(self, 0xbc);
    if (!At<unsigned char>(target, 0x2c))
        return;
    const float rate = dt * 0.1f;
    float *average = &At<float>(self, 0x26c);
    const float *position = &At<float>(target, 0x10);
    for (int i = 0; i < 3; ++i)
        average[i] = (position[i] - average[i]) * rate + average[i];
    const float dx = position[0] - average[0];
    const float dy = position[1] - average[1];
    const float dz = position[2] - average[2];
    const float spread = sqrtf(dz * dz + dy * dy + dx * dx);
    float &averageSpread = At<float>(self, 0x278);
    averageSpread = (spread - averageSpread) * rate + averageSpread;
    float speed = 0.0f;
    if (void *simable = At<void *>(target, 0x1c))
        speed = Virtual<float>(Virtual<void *>(simable, 0x54), 0x30);
    float &averageSpeed = At<float>(self, 0x27c);
    averageSpeed = averageSpeed + (speed - averageSpeed) * rate * 0.5f;
    const float ratio = averageSpread > 0.01f ? averageSpeed / averageSpread * 10.0f : 0.0f;
    unsigned char &jerk = At<unsigned char>(self, 0x280);
    if (jerk)
    {
        if (!(ratio > 1.75f))
            jerk = 0;
    }
    else if (ratio >= 3.0f)
        jerk = 1;
}

bool __fastcall AIPursuitContingentHasActiveCops(char *self)
{
    for (char *it = At<char *>(self, 0x8c); it != At<char *>(self, 0x90); it += 8)
    {
        if (At<void *>(it, 4))
            return true;
    }
    return false;
}

/** CopCountRecord (0x18 bytes): its cop type at 8, name at 0xc and count at 0x10. */
struct PursuitCopCount
{
    unsigned int words[3];
    const char *name;
    int count;
    unsigned int spare;
};

namespace
{

const int kMostCopTypes = 8;
const int kMostFleeingAtOnce = 2;
const unsigned int *const kNoCopType = reinterpret_cast<const unsigned int *>(0x91ddbc);
} // namespace

void __fastcall AIPursuitGetAdjustedCopCounts(char *self, void *, PursuitCopCount *records, int *count);

namespace
{
/** Cop tactics improved: how long (s) the cops a lower heat sent stay in the pursuit after the heat rises, instead of
 * the original sending them off at once. */
const float kHeatRiseStay = 45.0f;
const int kHeatLevel = 0xe0;
const int kTrackedPursuits = 8;

struct HeatRise
{
    char *pursuit;
    int level;
    float since;
};

HeatRise g_heatRises[kTrackedPursuits];

/** Whether the pursuit's heat rose within kHeatRiseStay seconds. */
bool HeatJustRose(char *self)
{
    const float now = NFS_ORIGINAL(0x6e8de0, float(__cdecl *)())();
    const int level = At<int>(self, kHeatLevel);
    HeatRise *slot = nullptr;
    for (HeatRise &rise : g_heatRises)
    {
        if (rise.pursuit == self)
        {
            slot = &rise;
            break;
        }
        if (!slot && (!rise.pursuit || now - rise.since > 600.0f || now < rise.since))
            slot = &rise;
    }
    if (!slot)
        slot = &g_heatRises[0];
    if (slot->pursuit != self)
    {
        *slot = {self, level, -kHeatRiseStay};
        return false;
    }
    if (level > slot->level)
    {
        slot->since = now;
        nfs::Log("pursuit: heat %d, the earlier cops stay %d s", level, static_cast<int>(kHeatRiseStay));
    }
    slot->level = level;
    return now >= slot->since && now - slot->since < kHeatRiseStay;
}
} // namespace

void __fastcall AIPursuitRemoveUnwantedVehicles(char *self)
{
    PursuitCopCount records[kMostCopTypes];
    for (PursuitCopCount &record : records)
    {
        record.words[0] = record.words[1] = record.words[2] = 0;
        record.name = reinterpret_cast<const char *>(0x890978);
    }
    int count = 0;
    AIPursuitGetAdjustedCopCounts(self, nullptr, records, &count);
    int total = 0;
    int fleeCount = 0;
    unsigned int fleeType = *kNoCopType;
    for (char *entry = At<char *>(self, 0xd4); entry != At<char *>(self, 0xd8); entry += 8)
    {
        int extra = At<int>(entry, 4);
        for (int i = 0; i < count; ++i)
        {
            if (records[i].words[2] == At<unsigned int>(entry, 0))
            {
                extra -= records[i].count;
                break;
            }
        }
        if (extra <= 0)
            continue;
        void *random = NFS_ORIGINAL(0x6ed200, void *(__cdecl *)())();
        const float roll = static_cast<float>(NFS_ORIGINAL(0x627320, int(__thiscall *)(void *))(random)) * 1.5258789e-05f;
        const int grown = total + extra;
        if (!(static_cast<float>(total) / static_cast<float>(grown) > roll))
        {
            fleeType = At<unsigned int>(entry, 0);
            fleeCount = extra;
            break;
        }
        total = grown;
    }
    const bool staying = nfs::aituning::CopsImproved() && HeatJustRose(self);
    if (staying)
        fleeType = *kNoCopType;
    if (fleeType != *kNoCopType)
        AIPursuitFleeCopOfType(self, nullptr, fleeType, fleeCount > kMostFleeingAtOnce ? kMostFleeingAtOnce : fleeCount);
    if (At<unsigned char>(self, 0x15c) || At<int>(self, 0x254) != 2 || !At<void *>(self, 0x1dc))
        return;
    for (void **it = CopsBegin(self + 0x60); it != CopsEnd(self + 0x60); ++it)
    {
        void *cop = *it;
        if (!Virtual<bool, void *>(self + 0x48, 0x4c, cop))
            continue;
        if (void *pursuitAi = Find(cop, kPursuitAiHandle))
            Virtual<void>(pursuitAi, 0xc);
    }
    NFS_ORIGINAL(0x42b410, void(__thiscall *)(void *))(self + 0x1dc);
}

void __fastcall AIPursuitGetAdjustedCopCounts(char *self, void *, PursuitCopCount *records, int *count)
{
    *count = 0;
    char *levels = reinterpret_cast<char *>(GetGlobalPursuitLevelAttrib());
    if (!levels)
        return;
    char *pursuit = self + 0x48;
    int most = 0x7ffffffd;
    if (!Virtual<bool>(pursuit, 0x8c))
        most = Virtual<bool>(*kCopManager, 0x28) ? 2 : 3;
    int patrols = 0;
    int limit;
    if (At<int>(self, 0x260) == 2)
    {
        const int wanted = *static_cast<const int *>(Attribute(Virtual<void *>(pursuit, 0x140), 0x24f7a1bc, 0));
        patrols = most >= wanted ? wanted : most;
        limit = patrols;
    }
    else
    {
        int open = At<int>(self, 0x18c) - At<int>(self, 0x194);
        if (open <= 0)
            open = 0;
        limit = most >= open ? open : most;
    }
    int weight = 0;
    for (unsigned int i = 0; i < TableLength(levels); ++i)
    {
        char *record = CopRecord(levels, i);
        if (At<unsigned int>(record, 8) != *kExcludedCop)
            weight += At<int>(record, 0x10);
    }
    int remaining = patrols > weight ? patrols : weight;
    if (limit < remaining)
        remaining = limit;
    for (unsigned int i = 0; i < TableLength(levels); ++i)
    {
        const char *record = CopRecord(levels, i);
        PursuitCopCount &out = records[*count];
        if (At<unsigned int>(const_cast<char *>(record), 8) == *kExcludedCop)
        {
            out = *reinterpret_cast<const PursuitCopCount *>(record);
            ++*count;
            continue;
        }
        const int share = At<int>(const_cast<char *>(record), 0x10);
        const int cops = static_cast<int>(static_cast<double>(share * remaining) / static_cast<double>(weight) + 0.5);
        if (!cops)
            continue;
        out = *reinterpret_cast<const PursuitCopCount *>(record);
        out.count = cops;
        ++*count;
        remaining -= cops;
        weight -= share;
    }
}

unsigned int __fastcall PursuitSupportNumLeaderSupportOptions(void *self);
unsigned int __fastcall PursuitSupportNumHeavySupportOptions(void *self);
const void *__fastcall AttribGenpursuitlevelsSearchModeHeliSpawnChance(void *self);
const void *__fastcall AttribGenpursuitsupportHeavySupportOptions(void *self, void *, unsigned int index);
const void *__fastcall AttribGenpursuitsupportLeaderSupportOptions(void *self, void *, unsigned int index);

namespace
{
const char *const kHelicopterName = reinterpret_cast<const char *>(0x8915dc);
void **const kSoundAi = reinterpret_cast<void **>(0x993cc8);
const void *const *const kActiveHelicopter = reinterpret_cast<const void *const *>(0x90d61c);
const unsigned int kDefaultCopWeight = 100;

const void *AttributeSized(void *instance, unsigned int key, unsigned int index, unsigned int size)
{
    const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
        instance, key, index);
    return value ? value : NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(size);
}

int Roll100()
{
    void *random = NFS_ORIGINAL(0x6ed200, void *(__cdecl *)())();
    return (NFS_ORIGINAL(0x627320, int(__thiscall *)(void *))(random) * 100) >> 16;
}

const float *MinimumSupportDelay(void *support)
{
    return static_cast<const float *>(AttribGenpursuitsupportMinimumSupportDelay(support));
}
} // namespace

void *__fastcall CopCountRecordConstruct(PursuitCopCount *self)
{
    self->words[0] = self->words[1] = self->words[2] = 0;
    self->name = reinterpret_cast<const char *>(0x890978);
    return self;
}

const char *__fastcall AIPursuitCopRequest(char *self)
{
    if (At<unsigned char>(self, 0xe8) || !(At<float>(self, 0xcc) < 0.0f) || At<unsigned char>(self, 0xe9))
        return nullptr;
    void *levels = Virtual<void *>(self, 0x140);
    char *base = self - 0x48;
    void *support = AIPursuitGetPursuitSupportAttrib(base);
    bool helicopterAllowed = false;
    if (At<float>(self, 0xd0) < 0.0f && support && *MinimumSupportDelay(support) < At<float>(self, 0xb4))
    {
        helicopterAllowed = true;
        if (At<unsigned char>(self, 0xd4))
        {
            At<unsigned char>(self, 0xd4) = 0;
            if (!At<unsigned char>(self, 0x114) && !*kActiveHelicopter)
            {
                const float chance = *static_cast<const float *>(
                    AttribGenpursuitlevelsSearchModeHeliSpawnChance(levels));
                if (!(Random(100.0f) > chance))
                {
                    At<unsigned char>(self, 0xd5) = 1;
                    char *sound = static_cast<char *>(*kSoundAi);
                    if (sound && At<void *>(sound, 0xe0))
                        Virtual<void>(At<void *>(sound, 0xe0), 0x218);
                }
            }
        }
    }
    if (At<unsigned char>(self, 0xd5))
        return kHelicopterName;
    PursuitCopCount records[kMostCopTypes];
    for (PursuitCopCount &record : records)
        CopCountRecordConstruct(&record);
    int count = 0;
    AIPursuitGetAdjustedCopCounts(base, nullptr, records, &count);
    struct Want
    {
        unsigned int type;
        int wanted;
        unsigned int weight;
    } wants[kMostCopTypes];
    int total = 0;
    for (int i = 0; i < count; ++i)
    {
        wants[i].type = records[i].words[2];
        wants[i].wanted = records[i].count;
        for (char *entry = At<char *>(self, 0x8c); entry != At<char *>(self, 0x90); entry += 8)
        {
            if (At<unsigned int>(entry, 0) == records[i].words[2])
            {
                const int open = records[i].count - At<int>(entry, 4);
                wants[i].wanted = open < 0 ? 0 : open;
                break;
            }
        }
        total += wants[i].wanted;
    }
    At<int>(self, 0x160) = total;
    if (!total)
        return nullptr;
    unsigned int sum = 0;
    for (int i = 0; i < count; ++i)
    {
        unsigned int weight = records[i].spare ? records[i].spare : kDefaultCopWeight;
        if (wants[i].type == *kExcludedCop && !helicopterAllowed)
            weight = 0;
        if (!wants[i].wanted)
            weight = 0;
        wants[i].weight = weight;
        sum += weight;
    }
    void *random = NFS_ORIGINAL(0x6ed200, void *(__cdecl *)())();
    int pick = (NFS_ORIGINAL(0x627320, int(__thiscall *)(void *))(random) * static_cast<int>(sum)) >> 16;
    for (int i = 0; i < count; ++i)
    {
        pick -= static_cast<int>(wants[i].weight);
        if (pick < 0)
            return records[i].name ? records[i].name : reinterpret_cast<const char *>(0x890978);
    }
    return nullptr;
}

char *__fastcall AIPursuitRequestGroundSupport(char *self)
{
    if (At<unsigned char>(self, 0xe8) || !At<unsigned char>(self, 0x114) || At<unsigned char>(self, 0xe9))
        return nullptr;
    if (At<int>(self, 0x20c))
        return self + 0x194;
    if (!(At<float>(self, 0x210) < 0.0f))
        return nullptr;
    At<float>(self, 0x210) = 10.0f;
    char *support = static_cast<char *>(AIPursuitGetPursuitSupportAttrib(self - 0x48));
    if (*MinimumSupportDelay(support) > At<float>(self, 0xb4))
        return nullptr;
    int roll = Roll100();
    if (!At<unsigned char>(self, 0x214))
    {
        for (unsigned int i = 0; static_cast<int>(i) < static_cast<int>(PursuitSupportNumLeaderSupportOptions(support));
             ++i)
        {
            const char *option = static_cast<const char *>(AttribGenpursuitsupportLeaderSupportOptions(support, nullptr, i));
            if (!(At<float>(const_cast<char *>(option), 0x10) < At<float>(self, 0xb4)))
                continue;
            At<unsigned char>(self, 0x214) = 1;
            if (At<unsigned int>(const_cast<char *>(option), 0xc) > static_cast<unsigned int>(roll))
            {
                At<const char *>(self, 0x198) = option;
                At<unsigned int>(self, 0x208) = At<unsigned int>(const_cast<char *>(option), 8);
                At<int>(self, 0x20c) = 1;
                break;
            }
        }
    }
    if (At<int>(self, 0x20c) != 1)
    {
        roll = Roll100();
        if (!At<void *>(self, kRoadBlock))
        {
            for (unsigned int i = 0; static_cast<int>(i) < static_cast<int>(PursuitSupportNumHeavySupportOptions(support));
                 ++i)
            {
                const char *option = static_cast<const char *>(AttribGenpursuitsupportHeavySupportOptions(support, nullptr, i));
                roll -= At<int>(const_cast<char *>(option), 4);
                if (roll < 0)
                {
                    At<const char *>(self, 0x194) = option;
                    At<unsigned int>(self, 0x208) = At<unsigned int>(const_cast<char *>(option), 8);
                    At<int>(self, 0x20c) = 1;
                    At<float>(self, 0xc8) = 15.0f;
                    break;
                }
            }
        }
        if (roll >= 0)
        {
            for (unsigned int i = 0; static_cast<int>(i) < static_cast<int>(PursuitSupportNumLeaderSupportOptions(support));
                 ++i)
            {
                const char *option = static_cast<const char *>(AttributeSized(support, 0xebd53935, i, 0x14));
                roll -= At<int>(const_cast<char *>(option), 4);
                if (roll < 0)
                {
                    At<const char *>(self, 0x198) = option;
                    At<unsigned int>(self, 0x208) = At<unsigned int>(const_cast<char *>(option), 8);
                    At<int>(self, 0x20c) = 1;
                    break;
                }
            }
        }
    }
    if (At<void *>(self, 0x198) && At<int>(self, 0x20c) == 1 && At<int>(self, 0x164))
    {
        At<int>(self, 0x20c) = 0;
        At<void *>(self, 0x198) = nullptr;
    }
    return roll < 0 ? self + 0x194 : nullptr;
}

const void *__fastcall AttribGenpursuitlevelsTimeBetweenFirstFourSpawn(void *self);

namespace
{
void *const kRigidBodyHandle = reinterpret_cast<void *>(0x4040e0);
void *const kHelicopterAiHandle = reinterpret_cast<void *>(0x404060);
char *const kPlayers = reinterpret_cast<char *>(0x92d87c);
const char *const kCrossName = reinterpret_cast<const char *>(0x891240);
const char *const kSuvName = reinterpret_cast<const char *>(0x891238);
const char *const kRhinoName = reinterpret_cast<const char *>(0x891230);
const char *const kSupportName = reinterpret_cast<const char *>(0x89124c);
const float kRhythmSpawn = 0.2f;

void AcquireTarget(void *target, void *simable)
{
    NFS_ORIGINAL(0x423860, void(__thiscall *)(void *, void *))(target, simable);
}
} // namespace

void __fastcall AIPursuitOnAttached(char *self, void *, char *attachable)
{
    char *base = self - 0x38;
    char *pursuit = self + 0x10;
    void *vehicle = Find(attachable, kRigidBodyHandle);
    if (!vehicle)
    {
        AIPursuitTrackVehicleCounts(base);
        return;
    }
    if (void *perp = Find(vehicle, kPerpHandle))
    {
        AcquireTarget(At<void *>(self, 0x84), Virtual<void *>(vehicle, 4));
        const float *chased = reinterpret_cast<const float *>(At<char *>(self, 0x84) + 0x10);
        for (int i = 0; i < 3; ++i)
            At<float>(self, 0x234 + i * 4) = chased[i];
        if (Virtual<bool>(pursuit, 0x8c))
        {
            NFS_ORIGINAL(0x471bf0, void(__cdecl *)(void *))(vehicle);
            NFS_ORIGINAL(0x56a540, void(__thiscall *)(void *))(reinterpret_cast<void *>(0x91d3d8));
            NFS_ORIGINAL(0x5de310, void(__thiscall *)(void *))(*reinterpret_cast<void **>(0x91e014));
            NFS_ORIGINAL(0x5edd10, void(__thiscall *)(void *))(*kGameManager);
        }
        float heat = Virtual<float>(perp, 4);
        if (heat < At<float>(self, 0xac))
            heat = At<float>(self, 0xac);
        Virtual<void, float>(perp, 8, heat);
        Virtual<void>(perp, 0x48);
        AIPursuitTrackVehicleCounts(base);
        return;
    }
    void *pursuitAi = Find(vehicle, kPursuitAiHandle);
    if (!pursuitAi)
    {
        AIPursuitTrackVehicleCounts(base);
        return;
    }
    NFS_ORIGINAL(0x41ae50, void(__thiscall *)(void *, void *const *))(self + 0x24, &vehicle);
    char *levels = Virtual<char *>(pursuit, 0x140);
    if (levels)
    {
        if (At<int>(self, 0x148) < 3 && At<int>(self, 0x228) != 2)
            At<unsigned int>(self, 0xdc) = *static_cast<const unsigned int *>(
                AttribGenpursuitlevelsTimeBetweenFirstFourSpawn(levels));
        else
        {
            At<unsigned int>(self, 0xdc) = At<unsigned int>(At<char *>(levels, 8), 0x80);
            if (At<int>(self, 0x170) > 2)
            {
                if (At<unsigned char>(self, 0x180))
                {
                    At<float>(self, 0xdc) = kRhythmSpawn;
                    At<unsigned char>(self, 0x180) = 0;
                }
                else
                    At<unsigned char>(self, 0x180) = 1;
            }
        }
    }
    else
        At<float>(self, 0xdc) = 0.0f;
    const int spawned = ++At<int>(self, 0x148);
    const unsigned int cross = stringhash32(kCrossName);
    const unsigned int suv = stringhash32(kSuvName);
    const unsigned int rhino = stringhash32(kRhinoName);
    const unsigned int support = stringhash32(kSupportName);
    const unsigned int name = stringhash32(Virtual<const char *>(vehicle, 0x4c));
    if (name == suv || name == rhino || name == cross || name == support)
    {
        ++At<int>(self, 0x198);
        if (name == cross)
            At<int>(self, 0x174) = 1;
    }
    else if (IsHelicopter(vehicle))
    {
        ++At<int>(self, 0x160);
        At<unsigned char>(self, 0xe5) = 0;
    }
    else
        ++At<int>(self, 0x194);
    TrackValue(reinterpret_cast<const char *>(0x891564), static_cast<float>(spawned));
    void *perpetrator = nullptr;
    if (NFS_ORIGINAL(0x40e200, bool(__thiscall *)(void *, void **))(At<void *>(self, 0x84), &perpetrator) &&
        !At<int>(self, 0x144))
    {
        Virtual<float>(perpetrator, 4);
        if (levels)
            At<unsigned int>(self, 0x144) = At<unsigned int>(At<char *>(levels, 8), 0x88);
    }
    Virtual<void, void *, void *>(pursuitAi, 0x18, At<void *>(self, 0x84), nullptr);
    if (Virtual<bool, void *>(pursuit, 0x4c, vehicle))
    {
        Virtual<void>(pursuitAi, 0x28);
        ++At<int>(self, 0x19c);
    }
    Virtual<void, void *>(pursuit, 0xe4, vehicle);
    AIPursuitTrackVehicleCounts(base);
}

void __fastcall AIPursuitOnDetached(char *self, void *, char *attachable)
{
    char *base = self - 0x38;
    char *pursuit = self + 0x10;
    char *watched = At<char *>(self, 0x94);
    if (attachable ? (watched && At<void *>(attachable, 4) == At<void *>(watched, 4)) : !watched)
    {
        At<void *>(self, 0x94) = nullptr;
        AIPursuitTrackVehicleCounts(base);
        return;
    }
    char *target = Virtual<char *>(pursuit, 8);
    bool targetLeft = false;
    if (At<unsigned char>(target, 0x2c))
    {
        char *chased = At<char *>(Virtual<char *>(pursuit, 8), 0x1c);
        if (chased)
            targetLeft = attachable && At<void *>(chased, 4) == At<void *>(attachable, 4);
        else
            targetLeft = !attachable;
    }
    if (targetLeft)
    {
        void *player = At<int>(kPlayers, 8) ? *At<void **>(kPlayers, 0) : nullptr;
        void *playerVehicle = Virtual<void *>(player, 4);
        for (void **it = CopsBegin(self + 0x28); it != CopsEnd(self + 0x28); ++it)
            AcquireTarget(Virtual<void *>(Virtual<void *>(*it, 0xac), 0x64), playerVehicle);
        NFS_ORIGINAL(0x409e60, void(__thiscall *)(void *))(At<void *>(self, 0x84));
        AIPursuitTrackVehicleCounts(base);
        return;
    }
    void *vehicle = Find(attachable, kRigidBodyHandle);
    if (!vehicle)
    {
        AIPursuitTrackVehicleCounts(base);
        return;
    }
    const bool cross = stringhash32(Virtual<const char *>(vehicle, 0x4c)) == stringhash32(kCrossName);
    if (Virtual<bool>(vehicle, 0x7c))
    {
        Virtual<void, void *>(pursuit, 0x9c, vehicle);
        if (cross)
            At<int>(self, 0x174) = 2;
    }
    else if (cross)
        At<int>(self, 0x174) = 0;
    if (Find(vehicle, kHelicopterAiHandle))
    {
        if (char *levels = Virtual<char *>(pursuit, 0x140))
            At<unsigned int>(self, 0xe0) = At<unsigned int>(At<char *>(levels, 8), 0x74);
    }
    void **it = CopsBegin(self + 0x28);
    while (it != CopsEnd(self + 0x28) && *it != vehicle)
        ++it;
    if (it != CopsEnd(self + 0x28))
    {
        const bool supporting = Virtual<bool, void *>(pursuit, 0x4c, vehicle);
        if (supporting && --At<int>(self, 0x19c) == 0)
            NFS_ORIGINAL(0x42b410, void(__thiscall *)(void *))(self + 0x1a4);
        NFS_ORIGINAL(0x41b7b0, void(__thiscall *)(void *, void **, void **))(self + 0x24, it, it + 1);
        if (void *pursuitAi = Find(vehicle, kPursuitAiHandle))
        {
            if (Virtual<bool>(pursuitAi, 0x8c) && !supporting && At<unsigned char>(self, 0xb8))
                ++At<int>(self, 0x15c);
            Virtual<void>(pursuitAi, 0x20);
        }
        const unsigned int name = stringhash32(Virtual<const char *>(vehicle, 0x4c));
        unsigned int *entry = At<unsigned int *>(self, 0x9c);
        while (entry[0] != name)
            entry += 2;
        --reinterpret_cast<int &>(entry[1]);
    }
    AIPursuitTrackVehicleCounts(base);
}

/** CopAndAngle: a cop's pursuit AI and its angle around the target. */
struct PursuitCopAngle
{
    void *pursuitAi;
    float angle;
    unsigned int spare;
};

namespace
{

const unsigned int *const kInPositionGoal = reinterpret_cast<const unsigned int *>(0x90d6f0);
const float kLeastAngle = 4.0f;
} // namespace

int __cdecl CopAndAngleSortPredicatePort(const void *a, const void *b)
{
    return static_cast<const PursuitCopAngle *>(a)->angle > static_cast<const PursuitCopAngle *>(b)->angle ? 1 : -1;
}

void __fastcall AIPursuitAssignCopsInCircle(char *, void *, PursuitCopAngle *cops, int count, float radius, const float *,
                                            const float *)
{
    NFS_ORIGINAL(0x7c5250, void(__cdecl *)(void *, unsigned int, unsigned int, int(__cdecl *)(const void *, const void *)))(
        cops, count, sizeof(PursuitCopAngle), CopAndAngleSortPredicatePort);
    float least = kLeastAngle;
    int first = 0;
    for (int i = 0; i < count; ++i)
    {
        float angle = cops[i].angle;
        if (angle < 0.0f)
            angle = -angle;
        if (angle < least)
        {
            least = angle;
            first = i;
        }
    }
    const float step = static_cast<float>(6.2831852 / count);
    for (int i = 0; i < count; ++i)
    {
        PursuitCopAngle &cop = cops[(i + first) % count];
        float angle = static_cast<float>(i) * step;
        angle *= 1.0f;
        float s;
        float c;
        __asm {
            fld angle
            fsincos
            fstp c
            fstp s
        }
        const float offset[3] = {s * radius, 0.0f, c * radius};
        Virtual<void, const float *>(cop.pursuitAi, 0x50, offset);
        Virtual<void, const unsigned int *>(cop.pursuitAi, 0x48, kInPositionGoal);
        Virtual<void>(cop.pursuitAi, 0x1c);
    }
}

void __fastcall AIPursuitAddVehicleToContingent(char *self, void *, void *vehicle)
{
    const unsigned int type = stringhash32(Virtual<const char *>(vehicle, 0x4c));
    for (unsigned int *entry = At<unsigned int *>(self, 0x8c); entry != At<unsigned int *>(self, 0x90); entry += 2)
    {
        if (entry[0] == type)
        {
            ++entry[1];
            return;
        }
    }
    char *contingent = self + 0x88;
    unsigned int value[2] = {type, 1};
    unsigned int *begin = At<unsigned int *>(contingent, 4);
    unsigned int *end = At<unsigned int *>(contingent, 8);
    if (begin && end < At<unsigned int *>(contingent, 0xc))
    {
        end[0] = value[0];
        end[1] = value[1];
        At<unsigned int *>(contingent, 8) = end + 2;
        return;
    }
    NFS_ORIGINAL(0x4364e0, void(__thiscall *)(void *, unsigned int *, unsigned int, const unsigned int *))(contingent, end,
                                                                                                       1, value);
}

namespace
{
void *SimAlloc(unsigned int size)
{
    return NFS_ORIGINAL(0x5d29d0, void *(__thiscall *)(void *, unsigned int, const char *))(
        reinterpret_cast<void *>(0x925b30), size, nullptr);
}

} // namespace

void *__fastcall PitFormationConstruct(char *self, void *, int);
void *__fastcall HerdFormationConstruct(char *self, void *, int);
void *__fastcall FollowFormationConstruct(char *self, void *, int);
void *__fastcall StaggerFollowFormationConstruct(char *self, void *, int);
void *__fastcall BoxInFormationConstruct(char *self, void *, int, void *pursuit);
void *__fastcall RollingBlockFormationConstruct(char *self, void *, int, void *pursuit);

void __fastcall AIPursuitInitFormation(char *self, void *, int cops)
{
    if (void *formation = At<void *>(self, 0xc8))
        Virtual<void *, unsigned int>(formation, 0, 1);
    void *made = nullptr;
    switch (At<int>(self, 0x108))
    {
    case 1:
        if (void *memory = SimAlloc(0x20))
            made = PitFormationConstruct(static_cast<char *>(memory), nullptr, cops);
        break;
    case 2:
        if (void *memory = SimAlloc(0x28))
            made = BoxInFormationConstruct(static_cast<char *>(memory), nullptr, cops, self + 0x48);
        break;
    case 3:
        if (void *memory = SimAlloc(0x28))
            made = RollingBlockFormationConstruct(static_cast<char *>(memory), nullptr, cops, self + 0x48);
        break;
    case 4:
    case 5:
        if (void *memory = SimAlloc(0x20))
            made = FollowFormationConstruct(static_cast<char *>(memory), nullptr, cops);
        break;
    case 6:
        if (void *memory = SimAlloc(0x20))
            made = HerdFormationConstruct(static_cast<char *>(memory), nullptr, cops);
        break;
    case 7:
        if (void *memory = SimAlloc(0x20))
            made = StaggerFollowFormationConstruct(static_cast<char *>(memory), nullptr, cops);
        break;
    default:
        return;
    }
    At<void *>(self, 0xc8) = made;
}

void *__fastcall AIPursuitConstruct(char *self, void *, unsigned int, unsigned int, unsigned int, unsigned int);
void *__fastcall IPursuitConstruct(char *self, void *, void *list);

void *__cdecl AIPursuitConstructActivity(unsigned int type, unsigned int name, unsigned int data, unsigned int extra)
{
    void *memory = SimAlloc(0x28c);
    if (!memory)
        return nullptr;
    char *pursuit = static_cast<char *>(
        AIPursuitConstruct(static_cast<char *>(memory), nullptr, type, name, data, extra));
    return pursuit ? pursuit + 0x2c : nullptr;
}

void __fastcall AIPursuitDestruct(char *self)
{
    char *pursuit = self + 0x48;
    At<unsigned int>(self, 0) = 0x8928ec;
    At<unsigned int>(self, 8) = 0x8928e4;
    At<unsigned int>(self, 0x2c) = 0x8928d0;
    At<unsigned int>(self, 0x38) = 0x8928b4;
    At<unsigned int>(pursuit, 0) = 0x892770;
    NFS_ORIGINAL(0x6e9c40, void(__thiscall *)(void *))(self);
    NFS_ORIGINAL(0x6e9c90, void(__thiscall *)(void *, void *))(self, At<void *>(self, 0x54));
    NFS_ORIGINAL(0x6e9c90, void(__thiscall *)(void *, void *))(self, At<void *>(self, 0x58));
    if (void *formation = At<void *>(self, 0xc8))
        Virtual<void *, unsigned int>(formation, 0, 1);
    if (void *other = At<void *>(self, 0xbc))
        Virtual<void *, unsigned int>(other, 0, 1);
    NFS_ORIGINAL(0x4330f0, void(__thiscall *)(void *))(self + 0x1dc);
    if (char *begin = At<char *>(self, 0xd4))
    {
        const unsigned int bytes = static_cast<unsigned int>((At<char *>(self, 0xdc) - begin) >> 3) * 8;
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
            reinterpret_cast<void *>(0x925b30), begin, bytes, nullptr);
    }
    At<void *>(self, 0xd4) = nullptr;
    At<void *>(self, 0xd8) = nullptr;
    At<void *>(self, 0xdc) = nullptr;
    NFS_ORIGINAL(0x42c990, void(__thiscall *)(void *))(self + 0x5c);
    At<unsigned int>(pursuit, 0) = 0x8916d8;
    NFS_ORIGINAL(0x4257d0, void(__thiscall *)(void *))(pursuit + 8);
    At<unsigned int>(pursuit, 0) = 0x890970;
    NFS_ORIGINAL(0x5d5a50, void(__thiscall *)(void *, void *))(At<void *>(pursuit, 4), pursuit);
    NFS_ORIGINAL(0x6f82f0, void(__thiscall *)(void *))(self);
}

namespace
{
float RaceBin(void *bin, unsigned int function, float otherwise)
{
    return bin ? NFS_ORIGINAL(function, float(__thiscall *)(void *))(bin) : otherwise;
}

float RaceParameter(void *parameters, unsigned int function)
{
    return NFS_ORIGINAL(function, float(__thiscall *)(void *))(parameters);
}

void *AddTask(char *self, float period)
{
    unsigned int task = stringhash32(reinterpret_cast<const char *>(0x8915e4));
    return NFS_ORIGINAL(0x6ee9b0, void *(__thiscall *)(void *, const unsigned int *, float, float, int))(self, &task,
                                                                                                    period, 0.0f, 1);
}
} // namespace

void *__fastcall AIPursuitConstruct(char *self, void *, unsigned int, unsigned int, unsigned int, unsigned int)
{
    NFS_ORIGINAL(0x6f94d0, void *(__thiscall *)(void *, unsigned int))(self, 1);
    char *pursuit = self + 0x48;
    IPursuitConstruct(pursuit, nullptr, self ? self + 0x10 : nullptr);
    At<unsigned int>(pursuit, 0) = 0x892770;
    char *cops = self + 0x5c;
    At<unsigned int>(self, 0) = 0x8928ec;
    At<unsigned int>(self, 8) = 0x8928e4;
    At<unsigned int>(self, 0x2c) = 0x8928d0;
    At<unsigned int>(self, 0x38) = 0x8928b4;
    At<void *>(cops, 4) = nullptr;
    At<void *>(cops, 8) = nullptr;
    At<int>(cops, 0xc) = 0;
    At<unsigned int>(cops, 0) = 0x8918d8;
    static const int kCleared[] = {0xbc, 0xc8, 0xcc, 0xd4, 0xd8, 0xdc, 0x120, 0x124, 0x128, 0x12c,
                                   0x140, 0x144, 0x148, 0x150, 0x154, 0x158};
    for (const int offset : kCleared)
        At<unsigned int>(self, offset) = 0;
    At<unsigned char>(self, 0x130) = 0;
    At<unsigned char>(self, 0x131) = 0;
    At<float>(self, 0x14c) = 60.0f;
    At<unsigned char>(self, 0x15c) = 1;
    At<unsigned int>(self, 0x16c) = 0;
    At<float>(self, 0x170) = -5.0f;
    for (int offset = 0x17c; offset <= 0x1b4; offset += 4)
    {
        if (offset != 0x188)
            At<unsigned int>(self, offset) = 0;
    }
    At<unsigned char>(self, 0x1b8) = 0;
    for (int offset = 0x1bc; offset <= 0x1d4; offset += 4)
        At<unsigned int>(self, offset) = 0;
    At<unsigned char>(self, 0x1d8) = 0;
    NFS_ORIGINAL(0x433080, void *(__thiscall *)(void *))(self + 0x1dc);
    At<unsigned int>(self, 0x260) = 0;
    At<unsigned int>(self, 0x264) = 0;
    At<unsigned char>(self, 0x288) = 0;
    At<void *>(self, 0x54) = AddTask(self, 0.25f);
    At<void *>(self, 0x58) = AddTask(self, 1.0f);
    NFS_ORIGINAL(0x6e8e90, void(__cdecl *)(void *, const char *))(At<void *>(self, 0x54),
                                                                  reinterpret_cast<const char *>(0x8915e4));
    if (At<int>(cops, 0xc) > 0)
        At<int>(cops, 0xc) = 0;
    if (void *begin = At<void *>(cops, 4))
    {
        Virtual<void, void *, unsigned int>(cops, 8, begin, At<unsigned int>(cops, 8));
        At<void *>(cops, 4) = nullptr;
        At<void *>(cops, 8) = nullptr;
        At<int>(cops, 0xc) = 0;
    }
    NFS_ORIGINAL(0x41ae90, void(__thiscall *)(void *, unsigned int))(cops, 0x14);
    At<unsigned int>(self, 0x110) = 0;
    At<unsigned int>(self, 0xc4) = 0;
    At<unsigned int>(self, 0xc0) = 0;
    void *target = SimAlloc(0x40);
    if (target)
        target = NFS_ORIGINAL(0x418000, void *(__thiscall *)(void *, void *))(target, nullptr);
    At<void *>(self, 0xbc) = target;
    NFS_ORIGINAL(0x409e60, void(__thiscall *)(void *))(target);
    At<unsigned int>(self, 0x160) = 0;
    At<unsigned int>(self, 0x164) = 0;
    At<unsigned int>(self, 0xf4) = 0;
    At<float>(self, 0xf8) = -1.0f;
    At<unsigned int>(self, 0xfc) = 0;
    At<unsigned char>(self, 0x100) = 0;
    At<unsigned int>(self, 0x104) = 0;
    At<unsigned int>(self, 0x168) = 0;
    NFS_ORIGINAL(0x438430, void(__thiscall *)(void *, unsigned int))(self + 0xd0, 5);
    At<unsigned char>(self, 0xf0) = 0;
    char *race = static_cast<char *>(*reinterpret_cast<void **>(0x91e000));
    void *parameters = At<void *>(race, 0x1968);
    if (parameters && At<int>(race, 0x1964) == 2)
    {
        if (NFS_ORIGINAL(0x5fc560, bool(__thiscall *)(void *))(parameters))
        {
            At<float>(self, 0xe8) = 6.0f;
            At<float>(self, 0xe4) = 6.0f;
        }
        else
        {
            At<float>(self, 0xe4) = RaceBin(At<void *>(race, 0x196c), 0x5dcee0, 0.0f);
            At<float>(self, 0xe8) = RaceBin(At<void *>(race, 0x196c), 0x5dcf00, 10.0f);
        }
    }
    else if (At<int>(race, 0x1964) == 0 &&
             !(parameters && NFS_ORIGINAL(0x5faa20, int(__thiscall *)(void *))(parameters) == 8))
    {
        At<float>(self, 0xe4) = 1.0f;
        At<float>(self, 0xe8) = 5.0f;
    }
    else
    {
        At<float>(self, 0xe4) = RaceBin(At<void *>(race, 0x196c), 0x5dcee0, 0.0f);
        At<float>(self, 0xe8) = RaceBin(At<void *>(race, 0x196c), 0x5dcf00, 10.0f);
        At<float>(self, 0xec) = RaceBin(At<void *>(race, 0x196c), 0x5dcf20, 1.0f);
        race = static_cast<char *>(*reinterpret_cast<void **>(0x91e000));
        void *settings = race ? At<void *>(race, 0x1968) : nullptr;
        if (settings && NFS_ORIGINAL(0x5fc380, bool(__thiscall *)(void *))(settings))
        {
            At<float>(self, 0xe4) = RaceParameter(settings, 0x5fc3e0);
            const float most = RaceParameter(settings, 0x5fc440);
            At<float>(self, 0xec) = 1.0f;
            At<float>(self, 0xe8) = most;
        }
        else
        {
            settings = At<void *>(race, 0x1968);
            if (settings && RaceParameter(settings, 0x5fc2c0) < At<float>(self, 0xe8))
                At<float>(self, 0xe8) = RaceParameter(settings, 0x5fc2c0);
        }
    }
    At<unsigned int>(self, 0xe0) = 0;
    At<unsigned int>(self, 0x10c) = 0;
    At<int>(self, 0x108) = 7;
    AIPursuitInitFormation(self, nullptr, 0);
    At<unsigned int>(self, 0x114) = 0;
    At<float>(self, 0x118) = 10.0f;
    At<unsigned char>(self, 0x11c) = 0;
    At<unsigned char>(self, 0x11d) = 0;
    At<unsigned int>(self, 0x134) = 0;
    At<unsigned int>(self, 0x138) = 1;
    At<unsigned int>(self, 0x13c) = 0;
    At<unsigned int>(self, 0x140) = stringhash32(nullptr);
    At<unsigned char>(self, 0x174) = 0;
    At<unsigned int>(self, 0x178) = 0;
    At<float>(self, 0x258) = 10.0f;
    At<unsigned char>(self, 0x25c) = 0;
    NFS_ORIGINAL(0x42b410, void(__thiscall *)(void *))(self + 0x1dc);
    At<unsigned int>(self, 0x26c) = 0;
    At<unsigned int>(self, 0x270) = 0;
    At<unsigned int>(self, 0x274) = 0;
    At<unsigned int>(self, 0x27c) = 0;
    At<unsigned char>(self, 0x280) = 0;
    At<unsigned int>(self, 0x284) = 0;
    At<float>(self, 0x278) = 1000.0f;
    At<float>(self, 0x268) = 100000.0f;
    return self;
}

namespace
{
const int kMostCollapsing = 64;
const float kLeastInnerRadius = 3.0f;
const float kCollapseReach = 60.0f;
const float kMovingTarget = 1.388889f;

int __cdecl CopFartherFirstFloat(const void *a, const void *b)
{
    const float da = *reinterpret_cast<const float *>(&static_cast<const PursuitCopAngle *>(a)->spare);
    const float db = *reinterpret_cast<const float *>(&static_cast<const PursuitCopAngle *>(b)->spare);
    return da > db ? 1 : -1;
}
} // namespace

bool __fastcall AIPursuitSetupCollapse(char *self, void *, char *cops, unsigned int inner, float innerRadius,
                                       float outerRadius)
{
    if (innerRadius < kLeastInnerRadius)
        innerRadius = kLeastInnerRadius;
    if (outerRadius < innerRadius + 1.0f)
        outerRadius = innerRadius + 1.0f;
    PursuitCopAngle found[kMostCollapsing];
    unsigned int count = 0;
    char *target = Virtual<char *>(self + 0x48, 8);
    const float *heading = reinterpret_cast<const float *>(target + 0x20);
    if (void *simable = At<void *>(target, 0x1c))
    {
        if (Virtual<float>(Virtual<void *>(simable, 0x54), 0x30) >= kMovingTarget)
        {
            void *moving = At<void *>(target, 0x1c);
            heading = moving ? Virtual<const float *>(Virtual<void *>(moving, 0x54), 0x24)
                             : reinterpret_cast<const float *>(0x8a2fb4);
        }
    }
    float forward[3] = {heading[0], heading[1], heading[2]};
    NFS_ORIGINAL(0x410220, float(__cdecl *)(float *))(forward);
    float right[3] = {forward[2], 0.0f, -forward[0]};
    NFS_ORIGINAL(0x410220, float(__cdecl *)(float *))(right);
    const float centre[3] = {At<float>(target, 0x10), At<float>(target, 0x14), At<float>(target, 0x18)};
    const unsigned int flee = stringhash32("AIGoalFleePursuit");
    for (void **it = At<void **>(cops, 4); it != At<void **>(cops, 8); ++it)
    {
        void *pursuitAi = *it;
        void *ai = Find(pursuitAi, kVehicleAiHandle);
        if (!ai)
            continue;
        const float *at = Virtual<const float *>(Virtual<void *>(ai, 8), 0xc);
        const char *chased = At<char *>(self, 0xbc) + 0x10;
        const float cx = at[0] - At<float>(const_cast<char *>(chased), 0);
        const float cy = at[1] - At<float>(const_cast<char *>(chased), 4);
        const float cz = at[2] - At<float>(const_cast<char *>(chased), 8);
        if (!(sqrtf(cx * cx + cy * cy + cz * cz) <= kCollapseReach))
            continue;
        if (!Virtual<bool>(ai, 0x68))
            continue;
        if (*Virtual<const unsigned int *>(pursuitAi, 0x80) != *kNoCopType)
            continue;
        if (*Virtual<const unsigned int *>(ai, 0xc0) == flee)
            continue;
        if (IsHelicopter(Virtual<void *>(ai, 8)))
            continue;
        const float *position = Virtual<const float *>(Virtual<void *>(ai, 8), 0xc);
        const float relative[3] = {position[0] - centre[0], position[1] - centre[1], position[2] - centre[2]};
        const float distance = sqrtf(relative[0] * relative[0] + relative[1] * relative[1] + relative[2] * relative[2]);
        const float angle = NFS_ORIGINAL(0x40aaa0, float(__cdecl *)(float, float))(
            relative[0] * forward[0] + relative[2] * forward[2] + forward[1] * relative[1],
            relative[0] * right[0] + right[1] * relative[1] + right[2] * relative[2]);
        if (count < kMostCollapsing)
        {
            found[count].pursuitAi = pursuitAi;
            found[count].angle = angle;
            *reinterpret_cast<float *>(&found[count].spare) = distance;
            ++count;
        }
    }
    if (!count)
        return false;
    unsigned int outer = 0;
    unsigned int innerCount = count;
    if (static_cast<int>(inner) < static_cast<int>(count))
    {
        NFS_ORIGINAL(0x7c5250, void(__cdecl *)(void *, unsigned int, unsigned int, int(__cdecl *)(const void *, const void *)))(
            found, count, sizeof(PursuitCopAngle), CopFartherFirstFloat);
        outer = count - inner;
        AIPursuitAssignCopsInCircle(self, nullptr, found, outer, outerRadius, forward, right);
        innerCount = inner;
    }
    AIPursuitAssignCopsInCircle(self, nullptr, found + outer, innerCount, innerRadius, forward, right);
    return true;
}

namespace
{
/** A UTL::Std vector as the pursuit code keeps them: an allocator word, then begin, end and capacity. */
} // namespace

struct GameVector
{
    unsigned int allocator;
    char *begin;
    char *end;
    char *capacity;
};

namespace
{

void FreeGameVector(GameVector &vector, unsigned int element)
{
    if (!vector.begin)
        return;
    const unsigned int bytes = static_cast<unsigned int>((vector.capacity - vector.begin) / static_cast<int>(element)) * element;
    NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
        reinterpret_cast<void *>(0x925b30), vector.begin, bytes, nullptr);
}

/** AIPursuit::FormationTarget: a slot offset, a second vector, and the goal. */
struct FormationTarget
{
    float offset[3];
    float extra[3];
    unsigned int goal;
};

void *const kTargetBodyHandle = reinterpret_cast<void *>(0x4039f0);
const float kOutOfFormationLane = 3.5f;
const float kOutOfFormationRow = 5.0f;
const float kOutOfFormationBack = 25.0f;
} // namespace

void __fastcall AIPursuitAssignClosestOffsets(char *self, void *, GameVector *positions, GameVector *pursuers,
                                              GameVector *targets, bool final);

void __fastcall AIPursuitUpdateOutOfFormationOffsets(char *self)
{
    void *body = nullptr;
    if (void *simable = At<void *>(At<char *>(self, 0xbc), 0x1c))
        body = Find(simable, kTargetBodyHandle);
    GameVector pursuers = {};
    GameVector offsets = {};
    NFS_ORIGINAL(0x438920, void(__thiscall *)(void *, unsigned int))(&pursuers, At<unsigned int>(self, 0x68));
    NFS_ORIGINAL(0x438b20, void(__thiscall *)(void *, unsigned int))(&offsets, At<unsigned int>(self, 0x68));
    for (void **it = CopsBegin(self + 0x60); it != CopsEnd(self + 0x60); ++it)
    {
        void *cop = *it;
        if (IsHelicopter(cop))
            continue;
        void *pursuitAi = Find(cop, kPursuitAiHandle);
        if (!pursuitAi || Virtual<bool>(pursuitAi, 0x34) || Virtual<bool, void *>(self + 0x48, 0x4c, cop))
            continue;
        const float *at = Virtual<const float *>(Virtual<void *>(cop, 4), 0x60);
        const float *chased = &At<float>(At<char *>(self, 0xbc), 0x10);
        float relative[3] = {at[0] - chased[0], at[1] - chased[1], at[2] - chased[2]};
        if (body)
            Virtual<void, float *, int>(body, 0xa4, relative, 0);
        Virtual<void, bool>(pursuitAi, 0x38, false);
        NFS_ORIGINAL(0x43c550, void(__thiscall *)(void *, void *const *))(&pursuers, &pursuitAi);
        NFS_ORIGINAL(0x43c630, void(__thiscall *)(void *, const float *))(&offsets, relative);
    }
    if (!pursuers.begin || pursuers.end == pursuers.begin)
    {
        FreeGameVector(offsets, 0xc);
        FreeGameVector(pursuers, 4);
        return;
    }
    GameVector targets = {};
    const int count = static_cast<int>((pursuers.end - pursuers.begin) >> 2);
    NFS_ORIGINAL(0x438c60, void(__thiscall *)(void *, unsigned int))(&targets, count);
    for (int i = 0; i < count; ++i)
    {
        const int row = i / 3;
        const int side = 1 - 2 * (i % 2);
        const int lane = (i / 2 + 1) % 3 - 1;
        FormationTarget target = {};
        target.offset[0] = static_cast<float>(lane) * kOutOfFormationLane;
        target.offset[2] = (static_cast<float>(row) * kOutOfFormationRow + kOutOfFormationBack) * static_cast<float>(side);
        target.goal = *kNoCopType;
        NFS_ORIGINAL(0x43c6b0, void(__thiscall *)(void *, const FormationTarget *))(&targets, &target);
    }
    AIPursuitAssignClosestOffsets(self, nullptr, &offsets, &pursuers, &targets, false);
    FreeGameVector(targets, 0x1c);
    FreeGameVector(offsets, 0xc);
    FreeGameVector(pursuers, 4);
}

namespace
{
/** PursuitFormation::TargetOffset: the slot, a second vector, its priority and its goal. */
struct TargetOffset
{
    float offset[3];
    float extra[3];
    int priority;
    unsigned int goal;
};

float Constant(unsigned int address)
{
    return *reinterpret_cast<const float *>(address);
}

const int kMostSlots = 64;
const int kMostCells = 1024;
const unsigned int kMinusOne = 0xbf800000;
const unsigned int kRoadBlockChaseSpeed = 0x420ae38f;

int VectorCount(const GameVector &vector, int element)
{
    return vector.begin ? static_cast<int>((vector.end - vector.begin) / element) : 0;
}

void PushTarget(GameVector &targets, const FormationTarget &target)
{
    NFS_ORIGINAL(0x43c6b0, void(__thiscall *)(void *, const FormationTarget *))(&targets, &target);
}

float FormationHorizontalGap(const float *a, const float *b)
{
    const float dz = a[2] - b[2];
    const float dx = a[0] - b[0];
    return sqrtf(dx * dx + dz * dz);
}
} // namespace

void __fastcall AIPursuitEvenOutOffsets(char *self, void *, GameVector *positions, GameVector *targets)
{
    char *formation = At<char *>(self, 0xc8);
    GameVector &offsets = *reinterpret_cast<GameVector *>(formation + 0x10);
    const int slotCount = VectorCount(offsets, 0x20);
    if (slotCount > kMostSlots)
    {
        NFS_ORIGINAL(0x4404f0, void(__thiscall *)(void *, GameVector *, GameVector *))(self, positions, targets);
        return;
    }
    TargetOffset *slots[kMostSlots];
    for (int i = 0; i < slotCount; ++i)
        slots[i] = reinterpret_cast<TargetOffset *>(offsets.begin) + i;
    while (VectorCount(*positions, 0xc) > VectorCount(*targets, 0x1c) &&
           VectorCount(*targets, 0x1c) < static_cast<int>(At<unsigned int>(formation, 4)))
    {
        int best = -1;
        int bestPriority = 0;
        float bestSum = 0.0f;
        for (int i = 0; i < slotCount; ++i)
        {
            TargetOffset *slot = slots[i];
            if (!slot || (best >= 0 && slot->priority > bestPriority))
                continue;
            float sum = 0.0f;
            for (const float *at = reinterpret_cast<const float *>(positions->begin);
                 at != reinterpret_cast<const float *>(positions->end); at += 3)
            {
                const float dx = at[0] - slot->offset[0];
                const float dy = at[1] - slot->offset[1];
                const float dz = at[2] - slot->offset[2];
                sum = sqrtf(dz * dz + dy * dy + dx * dx) + sum;
            }
            if (best >= 0 && sum > bestSum)
                continue;
            bestSum = sum;
            bestPriority = slot->priority;
            best = i;
        }
        if (best < 0)
            break;
        const TargetOffset *chosen = slots[best];
        FormationTarget target;
        for (int k = 0; k < 3; ++k)
        {
            target.offset[k] = chosen->offset[k];
            target.extra[k] = chosen->extra[k];
        }
        target.goal = chosen->goal;
        PushTarget(*targets, target);
        slots[best] = nullptr;
    }
}

void __fastcall AIPursuitAssignClosestOffsets(char *self, void *, GameVector *positions, GameVector *pursuers,
                                              GameVector *targets, bool final)
{
    const int rows = VectorCount(*positions, 0xc);
    const int columns = VectorCount(*targets, 0x1c);
    if (rows > kMostSlots || rows * columns > kMostCells)
    {
        NFS_ORIGINAL(0x4407e0, void(__thiscall *)(void *, GameVector *, GameVector *, GameVector *, bool))(
            self, positions, pursuers, targets, final);
        return;
    }
    float gap[kMostCells];
    float rowMost[kMostSlots];
    const float *at = reinterpret_cast<const float *>(positions->begin);
    const FormationTarget *slots = reinterpret_cast<const FormationTarget *>(targets->begin);
    for (int i = 0; i < rows; ++i)
    {
        const float *position = at + i * 3;
        const float height = position[2] * Constant(0x8910f0);
        float most = 0.0f;
        for (int j = 0; j < columns; ++j)
        {
            const float *slot = slots[j].offset;
            const float dx = position[0] - slot[0];
            const float dz = height - slot[2] * Constant(0x8910f0);
            const float distance = sqrtf(dz * dz + dx * dx);
            gap[i * columns + j] = distance;
            if (distance > most)
                most = distance;
        }
        rowMost[i] = most;
    }
    int rounds = columns;
    do
    {
        float most = 0.0f;
        int row = -1;
        for (int i = 0; i < rows; ++i)
        {
            const float value = rowMost[i];
            if (value != -1.0f && value > most)
            {
                most = value;
                row = i;
            }
        }
        if (row < 0 || columns <= 0)
            continue;
        int column = -1;
        for (int j = 0; j < columns; ++j)
        {
            const float value = gap[row * columns + j];
            if (value != -1.0f && value == most)
            {
                column = j;
                break;
            }
        }
        if (column < 0)
            continue;
        float least = Constant(0x892fa4);
        int best = -1;
        for (int k = 0; k < rows; ++k)
        {
            const float value = gap[k * columns + column];
            if (value != -1.0f && *reinterpret_cast<const unsigned int *>(&rowMost[k]) != kMinusOne && value < least)
            {
                least = value;
                best = k;
            }
        }
        if (best < 0)
            continue;
        char *slot = targets->begin + column * 0x1c;
        if (best < VectorCount(*pursuers, 4))
        {
            void *pursuitAi = reinterpret_cast<void **>(pursuers->begin)[best];
            Virtual<void, const float *>(pursuitAi, 0x50, reinterpret_cast<const float *>(slot + 0xc));
            Virtual<void, const float *>(pursuitAi, 0x40, reinterpret_cast<const float *>(slot));
            Virtual<void, bool>(pursuitAi, 0x30, final);
            Virtual<void, const unsigned int *>(pursuitAi, 0x48, reinterpret_cast<const unsigned int *>(slot + 0x18));
        }
        rowMost[best] = -1.0f;
        for (int k = 0; k < rows; ++k)
        {
            gap[k * columns + column] = -1.0f;
            if (rowMost[k] < 0.0f)
                continue;
            float rowBest = 0.0f;
            for (int j = 0; j < columns; ++j)
            {
                if (gap[k * columns + j] > rowBest)
                    rowBest = gap[k * columns + j];
            }
            rowMost[k] = rowBest;
        }
    } while (--rounds > 0);
}

void __fastcall AIPursuitUpdateFormation(char *self, void *, float dt)
{
    char *target = At<char *>(self, 0xbc);
    if (!At<unsigned char>(target, 0x2c) || !At<void *>(target, 0x1c))
        return;
    void *chasedAi = Find(At<void *>(target, 0x1c), reinterpret_cast<void *>(0x403700));
    if (!chasedAi || !At<void *>(At<char *>(self, 0xbc), 0x1c))
        return;
    void *body = Find(At<void *>(At<char *>(self, 0xbc), 0x1c), kTargetBodyHandle);
    if (!body)
        return;
    char *formation = At<char *>(self, 0xc8);
    Virtual<void, float, void *>(formation, 4, dt, self + 0x48);
    GameVector pursuers = {};
    GameVector positions = {};
    NFS_ORIGINAL(0x438920, void(__thiscall *)(void *, unsigned int))(&pursuers, At<unsigned int>(self, 0x68));
    NFS_ORIGINAL(0x438b20, void(__thiscall *)(void *, unsigned int))(&positions, At<unsigned int>(self, 0x68));
    const float reach =
        NFS_ORIGINAL(0x4100a0, float(__cdecl *)(const float *, const float *))(
            &At<float>(At<char *>(self, 0xbc), 0x10), &At<float>(Virtual<char *>(chasedAi, 0xb4), 0x98)) +
        60.0f;
    for (void **it = CopsBegin(self + 0x60); it != CopsEnd(self + 0x60); ++it)
    {
        void *cop = *it;
        void *pursuitAi = Find(cop, kPursuitAiHandle);
        if (!pursuitAi || Virtual<bool, void *>(self + 0x48, 0x4c, cop))
            continue;
        const bool helicopter = IsHelicopter(cop);
        const float *at = Virtual<const float *>(Virtual<void *>(cop, 4), 0x60);
        const float *chased = &At<float>(At<char *>(self, 0xbc), 0x10);
        float relative[3] = {at[0] - chased[0], at[1] - chased[1], at[2] - chased[2]};
        Virtual<void, bool>(pursuitAi, 0x30, false);
        if (helicopter)
        {
            if (!At<unsigned char>(self, 0x130) && !At<unsigned char>(self, 0x131))
                AIPursuitAssignChopperGoal(self, nullptr, static_cast<char *>(pursuitAi));
            continue;
        }
        if (!Virtual<bool>(Virtual<void *>(cop, 0xac), 0x68))
            continue;
        if (NFS_ORIGINAL(0x410180, float(__cdecl *)(const float *))(relative) > reach)
            continue;
        Virtual<void, float *, int>(body, 0xa4, relative, 0);
        NFS_ORIGINAL(0x43c550, void(__thiscall *)(void *, void *const *))(&pursuers, &pursuitAi);
        NFS_ORIGINAL(0x43c630, void(__thiscall *)(void *, const float *))(&positions, relative);
    }
    GameVector targets = {};
    NFS_ORIGINAL(0x438c60, void(__thiscall *)(void *, unsigned int))(
        &targets, VectorCount(*reinterpret_cast<GameVector *>(formation + 0x10), 0x20));
    AIPursuitEvenOutOffsets(self, nullptr, &positions, &targets);
    if (VectorCount(positions, 0xc) && VectorCount(targets, 0x1c))
        AIPursuitAssignClosestOffsets(self, nullptr, &positions, &pursuers, &targets, true);
    AIPursuitUpdateOutOfFormationOffsets(self);
    unsigned int inPosition = 0;
    unsigned int settled = 0;
    float settledGap = 0.0f;
    const float *position = reinterpret_cast<const float *>(positions.begin);
    for (void **it = reinterpret_cast<void **>(pursuers.begin); it != reinterpret_cast<void **>(pursuers.end);
         ++it, position += 3)
    {
        void *pursuitAi = *it;
        if (!Virtual<bool>(pursuitAi, 0x34))
        {
            Virtual<void, bool>(pursuitAi, 0x38, false);
            continue;
        }
        ++inPosition;
        const float *slot = Virtual<const float *>(pursuitAi, 0x44);
        const float distance = FormationHorizontalGap(slot, position);
        if (distance < 4.0f)
        {
            settledGap = distance + settledGap;
            Virtual<void, bool>(pursuitAi, 0x38, true);
            ++settled;
        }
        else
            Virtual<void, bool>(pursuitAi, 0x38, false);
    }
    void *levels = nullptr;
    if (void *vehicle = At<void *>(At<char *>(self, 0xbc), 0x1c))
    {
        if (void *perp = Find(vehicle, reinterpret_cast<void *>(0x4037e0)))
            levels = Virtual<void *>(perp, 0x10);
    }
    float slowEnough = (levels ? *static_cast<const float *>(Attribute(levels, 0xdb66950c, 0))
                               : *static_cast<const float *>(NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(4))) *
                       Constant(0x89098c);
    if (At<unsigned char>(self, 0x280))
        slowEnough = *reinterpret_cast<const float *>(&kRoadBlockChaseSpeed);
    float &timer = At<float>(self, 0xf8);
    float &collapse = At<float>(self, 0xf4);
    const bool roadBlock = At<unsigned char>(self, 0x130);
    if (timer >= 0.0f && Virtual<float>(formation, 0xc) > timer && !roadBlock && !At<unsigned char>(self, 0x131))
        timer = dt + timer;
    else if (levels &&
             (At<void *>(At<char *>(self, 0xbc), 0x1c)
                  ? Virtual<float>(Virtual<void *>(At<void *>(At<char *>(self, 0xbc), 0x1c), 0x54), 0x30)
                  : 0.0f) < slowEnough &&
             static_cast<int>(inPosition) > 0 && !roadBlock && At<unsigned char>(self, 0x15c) &&
             !At<unsigned char>(self, 0x131))
    {
        const float outer = *NFS_ORIGINAL(0x402f50, const float *(__thiscall *)(void *))(levels);
        const int inner = *NFS_ORIGINAL(0x402f30, const int *(__thiscall *)(void *))(levels);
        const unsigned int most = *NFS_ORIGINAL(0x403120, const unsigned int *(__thiscall *)(void *))(levels);
        const bool collapsing = AIPursuitSetupCollapse(self, nullptr, reinterpret_cast<char *>(&pursuers), most,
                                                       static_cast<float>(inner), outer);
        At<unsigned char>(self, 0x100) = collapsing;
        if (collapsing && At<int>(self, 0x254) == 2 && At<void *>(self, 0x1dc))
        {
            for (void **it = CopsBegin(self + 0x60); it != CopsEnd(self + 0x60); ++it)
            {
                void *cop = *it;
                void *pursuitAi = nullptr;
                if (Virtual<bool, void *>(self + 0x48, 0x4c, cop) &&
                    NFS_ORIGINAL(0x40e100, bool(__thiscall *)(void *, void **))(cop, &pursuitAi))
                    Virtual<void>(pursuitAi, 0xc);
            }
            NFS_ORIGINAL(0x42b410, void(__thiscall *)(void *))(self + 0x1dc);
        }
    }
    else if (!roadBlock && (timer >= 0.0f || At<unsigned char>(self, 0x100)))
    {
        collapse = 0.0f;
        timer = -1.0f;
        At<unsigned char>(self, 0x100) = 0;
        for (void **it = reinterpret_cast<void **>(pursuers.begin); it != reinterpret_cast<void **>(pursuers.end);
             ++it)
        {
            if (void *pursuitAi = *it)
            {
                Virtual<void, const void *>(pursuitAi, 0x48, reinterpret_cast<const void *>(0x91ddbc));
                Virtual<void, void *, int>(pursuitAi, 0x18, At<void *>(self, 0xbc), 0);
            }
        }
        for (void **it = CopsBegin(self + 0x60); it != CopsEnd(self + 0x60); ++it)
        {
            void *cop = *it;
            void *pursuitAi = Find(cop, kPursuitAiHandle);
            if (!pursuitAi)
                continue;
            const unsigned int goal = *Virtual<const unsigned int *>(Virtual<void *>(cop, 0xac), 0xc0);
            if (goal == *Virtual<const unsigned int *>(pursuitAi, 0x4c) ||
                goal == *reinterpret_cast<const unsigned int *>(0x90d6f0))
            {
                Virtual<void, const void *>(pursuitAi, 0x48, reinterpret_cast<const void *>(0x91ddbc));
                Virtual<void, void *, int>(pursuitAi, 0x18, At<void *>(self, 0xbc), 0);
            }
        }
    }
    else if (!At<unsigned char>(formation, 0xc) || !settled || At<unsigned char>(self, 0x100) || roadBlock ||
             At<unsigned char>(self, 0x131))
        collapse = 0.0f;
    else
    {
        const float average = settledGap / static_cast<float>(settled);
        const float width = Virtual<float>(formation, 8) * 4.0f;
        const float lean = NFS_ORIGINAL(0x402060, float(__cdecl *)(float, float, float))(
            (width + width - average) / width, -1.0f, 1.0f);
        collapse = lean * dt + collapse;
        if (collapse < 0.0f)
            collapse = 0.0f;
        if (Virtual<float>(formation, 0x10) <= collapse)
        {
            if (settled < At<unsigned int>(formation, 8))
                collapse = Virtual<float>(formation, 0x10) - Constant(0x890dc4);
            else
            {
                timer = 0.0f;
                for (void **it = reinterpret_cast<void **>(pursuers.begin);
                     it != reinterpret_cast<void **>(pursuers.end); ++it)
                {
                    void *pursuitAi = *it;
                    if (pursuitAi && Virtual<bool>(pursuitAi, 0x34) &&
                        *Virtual<const unsigned int *>(pursuitAi, 0x4c) != *reinterpret_cast<const unsigned int *>(0x91ddbc))
                        Virtual<void>(pursuitAi, 0x1c);
                }
            }
        }
    }
    FreeGameVector(targets, 0x1c);
    FreeGameVector(positions, 0xc);
    FreeGameVector(pursuers, 4);
}
