/** The cop manager: which cops and helicopters are out, roadblock setups, the vehicle cache's say on cop cars
 * (original: zAI AICopManager, PickRoadblockSetup). */
#include <float.h>
#include <math.h>

#include "game/gen/api.h"
#include "legacy/link.h"
#include "gameplay/ai/aituning.h"
#include "gameplay/vanillafixes.h"
#include "core/log.h"
#include "core/config.h"

NFS_REPLACE(0x4062b0, AICopManagerOnRemovedVehicleCache)
NFS_REPLACE(0x406300, AICopManagerMessageSetCopsEnabled)
NFS_REPLACE(0x4063d0, PickRoadblockSetupPort)
NFS_REPLACE(0x406450, AICopManagerRemoveActiveCopVehicle)
NFS_REPLACE(0x406500, AICopManagerUpdateDebug)
NFS_REPLACE(0x410d60, AICopManagerIsPendingSupportVehicle)
NFS_REPLACE(0x410d90, AICopManagerOnQueryVehicleCache)
NFS_REPLACE(0x410e20, AICopManagerVehicleSpawningEnabled)
NFS_REPLACE(0x405680, MUnspawnCopConstruct)
NFS_REPLACE(0x410e70, AICopManagerGetActiveCopVehicleFromOutOfView)
NFS_REPLACE(0x411070, AICopManagerPursuitIsEvaded)
NFS_REPLACE(0x411240, AICopManagerCommitPursuitDetails)
NFS_REPLACE(0x411410, AICopManagerLockoutCops)
NFS_REPLACE(0x41f840, AICopManagerPlayerPursuitHasCop)
NFS_REPLACE(0x426740, AICopManagerGetPursuitActivity)
NFS_REPLACE(0x426820, AICopManagerSetAllBustedTimersToZero)
NFS_REPLACE(0x426960, AICopManagerSpawnCopCarNow)
NFS_REPLACE(0x426c10, AICopManagerIsCopRequestPending)
NFS_REPLACE(0x426c90, AICopManagerCanPursueRacers)
NFS_REPLACE(0x426d10, AICopManagerIsPlayerPursuitActive)
NFS_REPLACE(0x41ecd0, AICopManagerGetAvailableCopVehicleByName)
NFS_REPLACE(0x426610, AICopManagerGetAvailableCopVehicleByClass)
NFS_REPLACE(0x426850, AICopManagerSpawnVehicleBehindTarget)
NFS_REPLACE(0x41eed0, AICopManagerGetSpawnPositionAheadOfTarget)
NFS_REPLACE(0x41f130, AICopManagerGetHeavySupportVehicles)
NFS_REPLACE(0x41f2e0, AICopManagerStartHeavySupport)
NFS_REPLACE(0x41f480, AICopManagerGetLeaderSupportVehicles)
NFS_REPLACE(0x41f650, AICopManagerStartLeaderSupport)
NFS_REPLACE(0x42e570, AICopManagerOnDetached)
NFS_REPLACE(0x42e6c0, AICopManagerTrySpawnCop)
NFS_REPLACE(0x42e8d0, AICopManagerUpdateSpawnRequests)
NFS_REPLACE(0x42e930, AICopManagerApplyBreakerZones)
NFS_REPLACE(0x42ea90, AICopManagerSpawnPursuitCar)
NFS_REPLACE(0x439dd0, AICopManagerSpawnCop)
NFS_REPLACE(0x439e60, AICopManagerMessageBreakerStopCops)
NFS_REPLACE(0x43ef00, AICopManagerOnTask)
NFS_REPLACE(0x426d50, AICopManagerUpdateCopPriorities)
NFS_REPLACE(0x42ebf0, AICopManagerPursueAtHeatLevel)
NFS_REPLACE(0x439ee0, AICopManagerUpdateRoadBlocks)
NFS_REPLACE(0x43e770, AICopManagerUpdateSupportCops)
NFS_REPLACE(0x43e8d0, AICopManagerUpdatePursuits)
NFS_REPLACE(0x4269a0, AICopManagerSpawnPursuitHelicopter)
NFS_REPLACE(0x430e60, AICopManagerSpawnPursuitIVehicle)
NFS_REPLACE(0x430d90, AICopManagerSpawnPatrolCar)
NFS_REPLACE(0x4314c0, AICopManagerUpdatePatrols)
NFS_REPLACE(0x439cf0, AICopManagerOnAttached)
NFS_REPLACE(0x43a200, AICopManagerResetCopsForRestart)
NFS_REPLACE(0x43c8b0, AICopManagerConstruct)
NFS_REPLACE(0x43cd20, AICopManagerFactory)

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

const unsigned int *const kHelicopterClass = reinterpret_cast<const unsigned int *>(0x92c4f4);
void *const kPursuitAiHandle = reinterpret_cast<void *>(0x4038e0);
int *const kCopsDisabled = reinterpret_cast<int *>(0x90d5ec);
void **const kPlayerCache = reinterpret_cast<void **>(0x92c674);
void **const kGameFlow = reinterpret_cast<void **>(0x9885c8);
char *const kRoadblockSetups = reinterpret_cast<char *>(0x8eb5e8);
char *const kSpikeRoadblockSetups = reinterpret_cast<char *>(0x8ebc68);
const int kRoadblockSetupSize = 0x68;
const float kLeastSetupWidth = 0.1f;
void *const kRenderableHandle = reinterpret_cast<void *>(0x404ff0);
const float kLeastOutOfViewTime = 5.0f;
const float kLeastOutOfViewDistance = 100.0f;
const float kDefaultLockout = 60.0f;
const unsigned int kLockoutTime = 0x6cb70d5;
const unsigned int kPlayerPort = 0x20d60dbf;
void **const kGameManager = reinterpret_cast<void **>(0x91e00c);
void **const kRaceStatus = reinterpret_cast<void **>(0x91e000);
void **const kRaceDatabase = reinterpret_cast<void **>(0x91e004);
void **const kInfractions = reinterpret_cast<void **>(0x91e014);
void **const kFrontendDatabase = reinterpret_cast<void **>(0x91cf90);
void **const kSoundAi = reinterpret_cast<void **>(0x993cc8);

void **Begin(char *list)
{
    return At<void **>(list, 0);
}

void **End(char *list)
{
    return At<void **>(list, 0) + At<int>(list, 8);
}

void Deliver(void *message)
{
    NFS_ORIGINAL(0x65faf0, void(__thiscall *)(void *))(message);
}

void Post(void *message, unsigned int port)
{
    NFS_ORIGINAL(0x654af0, void(__thiscall *)(void *, unsigned int))(message, port);
}

void TrackValue(const char *name, float value)
{
    NFS_ORIGINAL(0x5eda20, void(__thiscall *)(void *, const char *, float))(*kGameManager, name, value);
}

void *Find(void *object, void *handle)
{
    return NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(At<void *>(object, 4), handle);
}

bool IsHelicopter(void *vehicle)
{
    return *Virtual<const unsigned int *>(vehicle, 0x44) == *kHelicopterClass;
}
} // namespace

bool __fastcall AICopManagerIsPendingSupportVehicle(char *self, void *, void *vehicle);
unsigned int *__cdecl MUnspawnCopGetKind(unsigned int *kind);
void *__fastcall MUnspawnCopConstruct(unsigned int *self, void *, void *simable, int reason);

void __fastcall AICopManagerOnRemovedVehicleCache(char *self, void *, void *vehicle)
{
    if (!Virtual<bool>(vehicle, 0x88))
        return;
    if (IsHelicopter(vehicle))
        --At<int>(self, 0x40);
    else
        --At<int>(self, 0x38);
}

void __fastcall AICopManagerMessageSetCopsEnabled(char *, void *, const char *message)
{
    *kCopsDisabled = At<unsigned char>(const_cast<char *>(message), 0x10) == 0;
}

void *__cdecl PickRoadblockSetupPort(float width, int cops, bool spikes)
{
    char *setup = spikes ? kSpikeRoadblockSetups : kRoadblockSetups;
    char *best = nullptr;
    float bestSpare = FLT_MAX;
    if (!(At<float>(setup, 0) > kLeastSetupWidth))
        return nullptr;
    do
    {
        const float spare = width - At<float>(setup, 0);
        if (spare > 0.0f && cops >= At<int>(setup, 4) && spare < bestSpare)
        {
            bestSpare = spare;
            best = setup;
        }
        setup += kRoadblockSetupSize;
    } while (At<float>(setup, 0) > kLeastSetupWidth);
    return best;
}

void __fastcall AICopManagerRemoveActiveCopVehicle(char *self, void *, void *vehicle)
{
    void *ai = Virtual<void *>(vehicle, 0xac);
    if (void *pursuit = Virtual<void *>(ai, 0xa0))
        Virtual<bool, void *>(pursuit, 0x7c, vehicle);
    else if (Virtual<void *>(Virtual<void *>(vehicle, 0xac), 0xa4))
        Virtual<bool, void *>(Virtual<void *>(Virtual<void *>(vehicle, 0xac), 0xa4), 0xc, vehicle);
    const bool helicopter = IsHelicopter(vehicle);
    Virtual<void>(ai, 0x8c);
    if (helicopter)
        --At<int>(self, 0x9c);
    else
        --At<int>(self, 0x94);
}

void __fastcall AICopManagerUpdateDebug(char *self)
{
    using Queue = bool(__thiscall *)(void *);
    while (!NFS_ORIGINAL(0x633d80, Queue)(At<void *>(self, 0x13c)))
    {
        alignas(16) unsigned char action[32];
        NFS_ORIGINAL(0x633dc0, void *(__thiscall *)(void *, void *))(At<void *>(self, 0x13c), action);
        NFS_ORIGINAL(0x633d90, void(__thiscall *)(void *))(At<void *>(self, 0x13c));
    }
}

bool __fastcall AICopManagerIsPendingSupportVehicle(char *, void *, void *vehicle)
{
    void *pursuitAi = Find(vehicle, kPursuitAiHandle);
    if (!pursuitAi)
        return false;
    return *Virtual<const unsigned int *>(pursuitAi, 0x80) != 0;
}

int __fastcall AICopManagerOnQueryVehicleCache(char *self, void *, void *vehicle, char *cache)
{
    if (!Virtual<bool, void *>(self - 0x24, 0xc, vehicle))
        return 1;
    if (!Virtual<bool>(vehicle, 0x88) && !Virtual<bool>(vehicle, 0x5c) &&
        !AICopManagerIsPendingSupportVehicle(self - 0x5c, nullptr, vehicle))
        return 1;
    char *player = static_cast<char *>(*kPlayerCache);
    if (cache)
    {
        if (player && At<void *>(cache, 4) == At<void *>(player, 4))
            return 0;
    }
    else if (!player)
        return 0;
    char *me = reinterpret_cast<unsigned int>(self) != 0x5c ? self : nullptr;
    return cache != me ? 1 : 0;
}

bool __fastcall AICopManagerVehicleSpawningEnabled(char *self, void *, bool cop)
{
    if (At<unsigned char>(self, 0x60) && !cop)
        return false;
    if (At<float>(self, 0x58) > 0.0f)
        return false;
    if (*kCopsDisabled)
        return false;
    void *flow = *kGameFlow;
    if (!flow)
        return true;
    if (!cop)
        return false;
    return Virtual<bool>(flow, 0x64);
}

void *__fastcall MUnspawnCopConstruct(unsigned int *self, void *, void *simable, int reason)
{
    unsigned int kind;
    self[0] = *MUnspawnCopGetKind(&kind);
    self[1] = 0;
    self[3] = 0;
    self[5] = static_cast<unsigned int>(reason);
    self[2] = 0x18;
    self[4] = reinterpret_cast<unsigned int>(simable);
    return self;
}

void *__fastcall AICopManagerGetActiveCopVehicleFromOutOfView(char *self, void *, unsigned int kind)
{
    if (!Virtual<bool, int>(self + 0x54, 4, 0))
        return nullptr;
    void *farthest = nullptr;
    float farthestDistance = 0.0f;
    for (void **it = Begin(self + 0xc8); it != End(self + 0xc8); ++it)
    {
        void *cop = *it;
        if (*Virtual<const unsigned int *>(cop, 0x44) != kind || !Virtual<bool>(cop, 0x88))
            continue;
        if (!(Virtual<float>(cop, 0x60) > kLeastOutOfViewTime))
            continue;
        void *renderable = Find(cop, kRenderableHandle);
        if (!renderable)
            continue;
        const float distance = Virtual<float>(renderable, 0x18);
        if (distance > farthestDistance && distance > kLeastOutOfViewDistance)
        {
            farthestDistance = distance;
            farthest = cop;
        }
    }
    if (!farthest)
        return nullptr;
    AICopManagerRemoveActiveCopVehicle(self, nullptr, farthest);
    void *simable = Virtual<void *>(farthest, 4);
    const unsigned int port = stringhash32(reinterpret_cast<const char *>(0x890fc0));
    alignas(8) unsigned int message[8];
    MUnspawnCopConstruct(message, nullptr, Virtual<void *>(simable, 0x34), 5);
    message[1] = port;
    Deliver(message);
    return farthest;
}

void __fastcall AICopManagerPursuitIsEvaded(char *self, void *, void *pursuit)
{
    if (Virtual<bool>(pursuit, 0x8c))
    {
        const int evaded = ++At<int>(self, 0x50);
        TrackValue(reinterpret_cast<const char *>(0x890fec), static_cast<float>(evaded));
        void *manager = *kGameManager;
        const float length = Virtual<float>(pursuit, 0x18);
        NFS_ORIGINAL(0x5eda20, void(__thiscall *)(void *, const char *, float))(
            manager, reinterpret_cast<const char *>(0x890fd4), length);
        NFS_ORIGINAL(0x602770, void(__thiscall *)(void *, int))(*kGameManager, 1);
        NFS_ORIGINAL(0x580a40, void(__cdecl *)(int))(0);
        char *race = static_cast<char *>(*kRaceStatus);
        if (race && !At<void *>(race, 0x1960))
        {
            void *sound = *kSoundAi;
            if (sound && NFS_ORIGINAL(0x701070, bool(__thiscall *)(void *))(sound))
            {
                unsigned int port;
                NFS_ORIGINAL(0x4023c0, void *(__thiscall *)(void *, const char *))(&port,
                                                                                  reinterpret_cast<const char *>(0x890fcc));
                alignas(8) unsigned int message[12];
                NFS_ORIGINAL(0x405da0, void *(__thiscall *)(void *, int, int, int, int))(message, 0, 0xe, 0, 0);
                message[1] = port;
                Deliver(message);
            }
            if (void *event = NFS_ORIGINAL(0x627400, void *(__cdecl *)(unsigned int))(0xc))
            {
                const int milestones = Virtual<unsigned char>(pursuit, 0x134);
                NFS_ORIGINAL(0x641fe0, void *(__thiscall *)(void *, int))(event, milestones);
            }
        }
    }
    char *target = Virtual<char *>(pursuit, 8);
    if (!target)
        return;
    void *simable = At<void *>(target, 0x1c);
    if (!simable)
        return;
    void *handle = Virtual<void *>(simable, 0x34);
    unsigned int kind;
    NFS_ORIGINAL(0x405c40, unsigned int *(__cdecl *)(unsigned int *))(&kind);
    unsigned int message[5] = {kind, 0, 0x14, 0, reinterpret_cast<unsigned int>(handle)};
    Post(message, kPlayerPort);
    if (void *player = Virtual<void *>(simable, 0x20))
    {
        const int index = Virtual<int>(player, 0x1c);
        if (index == 0 || index == 1)
            NFS_ORIGINAL(0x590160, void(__cdecl *)(int))(index);
    }
}

void __fastcall AICopManagerCommitPursuitDetails(char *, void *, void *pursuit)
{
    char *race = static_cast<char *>(*kRaceStatus);
    if (race && At<void *>(race, 0x1968) &&
        NFS_ORIGINAL(0x5fc180, bool(__thiscall *)(void *))(At<void *>(race, 0x1968)))
        return;
    char *target = Virtual<char *>(pursuit, 8);
    if (!target)
        return;
    void *simable = At<void *>(target, 0x1c);
    if (!simable)
        return;
    void *player = Virtual<void *>(simable, 0x20);
    if (!player)
        return;
    const int index = Virtual<int>(player, 0x1c);
    if (index != 0 && index != 1)
        return;
    char *database = static_cast<char *>(*kFrontendDatabase);
    char *profile = At<char *>(database, index * 4 + 0x10);
    if (reinterpret_cast<unsigned int>(profile) + 0xa8 == 0)
        return;
    void *stable = NFS_ORIGINAL(0x4061b0, void *(__thiscall *)(void *, int))(database, index);
    if (!stable)
        return;
    char *car = static_cast<char *>(
        NFS_ORIGINAL(0x56ecf0, void *(__thiscall *)(void *, unsigned int))(stable, At<unsigned int>(profile, 0xa8)));
    if (!car)
        return;
    char *record = static_cast<char *>(
        NFS_ORIGINAL(0x56f120, void *(__thiscall *)(void *, unsigned int))(stable, At<unsigned char>(car, 0x11)));
    if (!record)
        return;
    void *perp = nullptr;
    if (!NFS_ORIGINAL(0x40e180, bool(__thiscall *)(void *, void **))(simable, &perp))
        return;
    int bounty = Virtual<int>(perp, 0x44);
    bounty += Virtual<int>(perp, 0x40);
    Virtual<int>(perp, 0x48);
    const int infractions = NFS_ORIGINAL(0x5de330, int(__thiscall *)(void *))(*kInfractions);
    NFS_ORIGINAL(0x58fd00, void(__thiscall *)(void *, void *, unsigned int, int, int))(
        profile, pursuit, At<unsigned int>(car, 4), bounty, infractions);
    const unsigned int committed = **reinterpret_cast<unsigned int **>(kInfractions);
    const bool busted = Virtual<bool>(pursuit, 0x74);
    NFS_ORIGINAL(0x56f7c0, void(__thiscall *)(void *, unsigned int, int, bool))(record, committed, bounty, !busted);
    void *reputation = nullptr;
    if (NFS_ORIGINAL(0x40e280, bool(__thiscall *)(void *, void **))(Virtual<void *>(player, 0x20), &reputation))
    {
        const int total = NFS_ORIGINAL(0x590500, int(__thiscall *)(void *))(stable);
        Virtual<void, int>(reputation, 4, total);
    }
    if (Virtual<bool>(pursuit, 0x74))
        return;
    alignas(8) unsigned int message[8];
    NFS_ORIGINAL(0x405960, void *(__thiscall *)(void *, unsigned int, unsigned int))(
        message, At<unsigned int>(simable, 8), At<unsigned int>(record, 0x10));
    Post(message, kPlayerPort);
}

void __fastcall AICopManagerLockoutCops(char *self, void *, bool lockout)
{
    if (lockout != 1)
    {
        At<float>(self, 0x58) = 0.0f;
        At<unsigned char>(self, 0x60) = 0;
        return;
    }
    At<float>(self, 0x58) = kDefaultLockout;
    if (!At<void *>(self, 0xec))
        return;
    char *race = static_cast<char *>(*kRaceStatus);
    if (!race || At<void *>(race, 0x1960))
    {
        void *races = *kRaceDatabase;
        if (!races || NFS_ORIGINAL(0x5dca00, void *(__thiscall *)(void *))(races))
            return;
    }
    alignas(8) unsigned char levels[32];
    NFS_ORIGINAL(0x402db0, void *(__thiscall *)(void *, const void *))(levels, static_cast<char *>(*kSoundAi) + 0x190);
    if (*reinterpret_cast<void **>(levels + 4))
    {
        const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
            levels, kLockoutTime, 0);
        if (!value)
            value = NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(4);
        At<unsigned int>(self, 0x58) = *static_cast<const unsigned int *>(value);
    }
    NFS_ORIGINAL(0x45a430, void(__thiscall *)(void *))(levels);
}

namespace
{
/** A std::list of the manager's: the head node, each node's next at 0 and value at 8. */
template <typename F> bool AnyInList(char *head, F test)
{
    for (char *node = At<char *>(head, 0); node != head; node = At<char *>(node, 0))
    {
        if (test(At<void *>(node, 8)))
            return true;
    }
    return false;
}

const unsigned int *const kCopCarClass = reinterpret_cast<const unsigned int *>(0x92c52c);
void **const kActivityFactories = reinterpret_cast<void **>(0x92c668);
void *const kPursuitActivityHandle = reinterpret_cast<void *>(0x4038c0);
void *const kAttachableHandle = reinterpret_cast<void *>(0x402820);
void *const kVehicleHandle = reinterpret_cast<void *>(0x4040e0);

struct ActivityParams
{
    unsigned int words[4];
};
} // namespace

void *__fastcall AICopManagerGetAvailableCopVehicleByClass(char *self, void *, unsigned int kind, bool anyName);
void __fastcall AICopManagerSpawnVehicleBehindTarget(char *self, void *, void *pursuit, void *vehicle);
void __fastcall AICopManagerUpdateDebug(char *self);
void __fastcall AICopManagerUpdateCopPriorities(char *self, void *, int wanted);
void __fastcall AICopManagerUpdateRoadBlocks(char *self);
void __fastcall AICopManagerUpdatePursuits(char *self);
bool __fastcall AICopManagerSpawnPursuitHelicopter(char *self, void *, void *pursuit);
bool __fastcall AICopManagerSpawnPursuitIVehicle(char *self, void *, void *pursuit, void *vehicle);
void __fastcall AICopManagerUpdatePatrols(char *self);
void *__fastcall AICopManagerGetActiveCopVehicleFromOutOfView(char *self, void *, unsigned int kind);

bool __fastcall AICopManagerPlayerPursuitHasCop(char *self)
{
    return AnyInList(At<char *>(self, 0xd4),
                     [](void *pursuit) { return Virtual<bool>(pursuit, 0x8c) && Virtual<bool>(pursuit, 0x90); });
}

void *__fastcall AICopManagerGetPursuitActivity(char *self, void *, char *simable)
{
    char *head = At<char *>(self, 0x128);
    for (char *node = At<char *>(head, 0); node != At<char *>(self, 0x128); node = At<char *>(node, 0))
    {
        void *activity = At<void *>(node, 8);
        char *target = Virtual<char *>(activity, 8);
        if (!target)
            continue;
        char *chased = At<char *>(target, 0x1c);
        if (!chased)
        {
            if (!simable)
                return activity;
            continue;
        }
        if (simable && At<void *>(chased, 4) == At<void *>(simable, 4))
            return activity;
    }
    const unsigned int kind = stringhash32(reinterpret_cast<const char *>(0x8915e4));
    void *made = nullptr;
    for (unsigned int *factory = static_cast<unsigned int *>(*kActivityFactories); factory;
         factory = reinterpret_cast<unsigned int *>(factory[2]))
    {
        if (factory[0] == kind)
        {
            const ActivityParams params = {};
            made = reinterpret_cast<void *(__cdecl *)(ActivityParams)>(factory[1])(params);
            break;
        }
    }
    void *pursuit = Find(made, kPursuitActivityHandle);
    Virtual<void, void *>(self + 0x2c, 8, pursuit);
    if (void *attachable = Find(simable, kAttachableHandle))
        Virtual<void, void *>(attachable, 4, made);
    return pursuit;
}

void __fastcall AICopManagerSetAllBustedTimersToZero(char *self)
{
    AnyInList(At<char *>(self, 0xd4), [](void *pursuit) {
        Virtual<void>(pursuit, 0x13c);
        return false;
    });
}

bool __fastcall AICopManagerSpawnCopCarNow(char *self, void *, void *pursuit)
{
    void *vehicle = AICopManagerGetAvailableCopVehicleByClass(self, nullptr, *kCopCarClass, false);
    if (!vehicle)
        return false;
    AICopManagerSpawnVehicleBehindTarget(self, nullptr, pursuit, vehicle);
    return true;
}

bool __fastcall AICopManagerIsCopRequestPending(char *self)
{
    if (!Virtual<bool, int>(self, 4, 0))
        return false;
    if (At<void *>(self, 0x1c) || At<void *>(self, 0x68))
        return true;
    return AnyInList(At<char *>(self, 0xd4), [self](void *pursuit) {
        if (Virtual<bool>(pursuit, 0x70))
            return false;
        if (At<int>(self, 0x40) < At<int>(self, 0x44) && Virtual<void *>(pursuit, 0x50))
            return true;
        return Virtual<bool>(pursuit, 0x54);
    });
}

bool __fastcall AICopManagerCanPursueRacers(char *self)
{
    char *race = static_cast<char *>(*kRaceStatus);
    if (!race || !At<void *>(race, 0x1968))
        return false;
    if (NFS_ORIGINAL(0x5facd0, int(__thiscall *)(void *))(At<void *>(race, 0x1968)) <= 0)
        return false;
    return !AnyInList(At<char *>(self, 0xd4), [](void *pursuit) {
        void *chased = At<void *>(Virtual<char *>(pursuit, 8), 0x1c);
        if (!chased)
            return false;
        void *vehicle = Find(chased, kVehicleHandle);
        return vehicle && Virtual<int>(vehicle, 0x58) == 3;
    });
}

bool __fastcall AICopManagerIsPlayerPursuitActive(char *self)
{
    return AnyInList(At<char *>(self, 0xd4), [](void *pursuit) { return Virtual<bool>(pursuit, 0x8c); });
}

namespace
{
void **const kVehicleFactories = reinterpret_cast<void **>(0x92c66c);
void *const kPerpHandle = reinterpret_cast<void *>(0x4037e0);
const unsigned int kVehicleClassKey = 0xa6b47fac;
const char *const kDefaultCopCar = reinterpret_cast<const char *>(0x8915d0);
const char *const kHelicopterCar = reinterpret_cast<const char *>(0x8915dc);
char *const kPlayers = reinterpret_cast<char *>(0x92d87c);
const float kSpawnBehind = -15.0f;

void *FirstPlayer()
{
    return At<int>(kPlayers, 8) ? *At<void **>(kPlayers, 0) : nullptr;
}

/** A vehicle the cache may hand out: not one that is unavailable (vtable 0x5c). */
void *Usable(void *vehicle)
{
    return Virtual<bool>(vehicle, 0x5c) ? nullptr : vehicle;
}

/** VehicleParams as AICopManager::GetAvailableCopVehicleByName lays them out for the vehicle factory. */
struct CopSpawnParams
{
    unsigned int type;
    unsigned int kind;
    void *self;
    unsigned int unused;
    int flags;
    unsigned int attributes;
    const float *direction;
    const float *position;
    unsigned int reserved;
    void *cache;
    unsigned int extra[2];
};

struct FactoryArguments
{
    unsigned int type;
    unsigned int kind;
    CopSpawnParams *params;
    unsigned int unused;
};
} // namespace

void *__fastcall AICopManagerGetAvailableCopVehicleByName(char *self, void *, const char *name)
{
    if (!Virtual<bool, int>(self + 0x54, 4, 0) || !name)
        return nullptr;
    const unsigned int wanted = stringhash32(name);
    for (void **it = Begin(self + 0xc8); it != End(self + 0xc8); ++it)
    {
        void *vehicle = *it;
        if (Virtual<bool>(vehicle, 0x88))
            continue;
        if (void *pursuitAi = Find(vehicle, kPursuitAiHandle))
        {
            if (*Virtual<const unsigned int *>(pursuitAi, 0x80))
                continue;
        }
        if (stringhash32(Virtual<const char *>(vehicle, 0x4c)) == wanted)
            return Usable(vehicle);
    }
    const float direction[3] = {0.0f, 0.0f, 1.0f};
    const float position[3] = {0.0f, 0.0f, 0.0f};
    const unsigned int attributes = NFS_ORIGINAL(0x454640, unsigned int(__cdecl *)(const char *))(name);
    CopSpawnParams params = {};
    NFS_ORIGINAL(0x4040f0, unsigned int *(__cdecl *)(unsigned int *))(&params.type);
    params.kind = kVehicleClassKey;
    params.self = &params;
    params.flags = 2;
    params.attributes = attributes;
    params.direction = direction;
    params.position = position;
    params.cache = self + 0x5c;
    const unsigned int factoryKind = stringhash32(reinterpret_cast<const char *>(0x89121c));
    unsigned int *factory = static_cast<unsigned int *>(*kVehicleFactories);
    while (factory && factory[0] != factoryKind)
        factory = reinterpret_cast<unsigned int *>(factory[2]);
    if (!factory)
        return nullptr;
    const FactoryArguments arguments = {params.type, params.kind, &params, 0};
    void *made = reinterpret_cast<void *(__cdecl *)(FactoryArguments)>(factory[1])(arguments);
    if (!made)
        return nullptr;
    Virtual<void, void *>(self + 0x2c, 8, made);
    void *vehicle = Find(made, kVehicleHandle);
    if (!vehicle)
        return nullptr;
    Virtual<void>(Virtual<void *>(vehicle, 0xac), 0x8c);
    const unsigned int port = stringhash32(reinterpret_cast<const char *>(0x890fc0));
    alignas(8) unsigned int message[8];
    MUnspawnCopConstruct(message, nullptr, Virtual<void *>(made, 0x34), 4);
    message[1] = port;
    Deliver(message);
    return Usable(vehicle);
}

void *__fastcall AICopManagerGetAvailableCopVehicleByClass(char *self, void *, unsigned int kind, bool validName)
{
    if (!Virtual<bool, int>(self + 0x54, 4, 0))
        return nullptr;
    void *perp = nullptr;
    if (void *playerVehicle = Virtual<void *>(FirstPlayer(), 4))
        perp = Find(playerVehicle, kPerpHandle);
    for (void **it = Begin(self + 0xc8); it != End(self + 0xc8); ++it)
    {
        void *vehicle = *it;
        if (*Virtual<const unsigned int *>(vehicle, 0x44) != kind || Virtual<bool>(vehicle, 0x88))
            continue;
        const char *name = Virtual<const char *>(vehicle, 0x4c);
        if (!validName || !perp || IsValidPursuitCarName(name))
            return Usable(vehicle);
    }
    if (kind == *kHelicopterClass)
        return AICopManagerGetAvailableCopVehicleByName(self, nullptr, kHelicopterCar);
    const char *name = perp ? GetRandomValidCopCar() : nullptr;
    return AICopManagerGetAvailableCopVehicleByName(self, nullptr, name ? name : kDefaultCopCar);
}

void __fastcall AICopManagerSpawnVehicleBehindTarget(char *self, void *, void *pursuit, void *vehicle)
{
    Virtual<void>(vehicle, 0x80);
    const float *playerPosition = Virtual<const float *>(Virtual<void *>(Virtual<void *>(FirstPlayer(), 4), 0x54), 0x20);
    float position[3] = {playerPosition[0], playerPosition[1], playerPosition[2]};
    float direction[3];
    Virtual<void, float *>(Virtual<void *>(Virtual<void *>(FirstPlayer(), 4), 0x54), 0x34, direction);
    for (int i = 0; i < 3; ++i)
        position[i] = direction[i] * kSpawnBehind + position[i];
    Virtual<void, const float *, const float *>(vehicle, 0x68, position, direction);
    Virtual<void>(Virtual<void *>(vehicle, 0xac), 0x88);
    if (pursuit)
        Virtual<void, void *>(pursuit, 0x78, vehicle);
    else if (void *pursuitAi = Find(vehicle, kPursuitAiHandle))
        Virtual<void>(pursuitAi, 4);
    ++At<int>(self, 0x94);
}

namespace
{
/** WRoadNav (a local of 0x2d0 bytes in the original): found a road, the path flags, its position and heading. */
const int kNavFound = 0x50;
const int kNavShortcuts = 0x51;
const int kNavRaceOnly = 0x52;
const int kNavRetry = 0x53;
const int kNavMode = 0x78;
const int kNavPosition = 0x98;
const int kNavDirection = 0xbc;
const unsigned char *const kShortcutsAllowed = reinterpret_cast<const unsigned char *>(0x9b3829);

struct RoadNav
{
    alignas(16) unsigned char bytes[0x300];

    RoadNav()
    {
        NFS_ORIGINAL(0x77b9e0, void *(__thiscall *)(void *))(bytes);
    }
    ~RoadNav()
    {
        NFS_ORIGINAL(0x76e220, void(__thiscall *)(void *))(bytes);
    }
    unsigned char &Byte(int offset)
    {
        return bytes[offset];
    }
    bool InitAt(const float *point, const float *direction)
    {
        NFS_ORIGINAL(0x7862d0, void(__thiscall *)(void *, const float *, const float *, int, int))(bytes, point,
                                                                                            direction, 0, 0);
        return bytes[kNavFound] != 0;
    }
};

void Negate(float *vector)
{
    for (int i = 0; i < 3; ++i)
        vector[i] *= -1.0f;
}

const char *const kHeavySupportNames[4] = {reinterpret_cast<const char *>(0x8eb1b0),
                                           reinterpret_cast<const char *>(0x8eb1b4),
                                           reinterpret_cast<const char *>(0x8eb1bc),
                                           reinterpret_cast<const char *>(0x8eb1c4)};
const int kHeavySupportCounts[4] = {1, 2, 2, 4};
const char *const kHeavySuv = reinterpret_cast<const char *>(0x891238);
const char *const kHeavyRhino = reinterpret_cast<const char *>(0x891230);
const int kHeavyRoadBlockKind = 3;
const float kHeavyRoadBlockDelay = 3.0f;
} // namespace

bool __fastcall AICopManagerGetSpawnPositionAheadOfTarget(char *self, void *, void *pursuit, float *position,
                                                          float *direction, float distance)
{
    char *target = Virtual<char *>(pursuit, 8);
    if (!target)
        return false;
    for (int i = 0; i < 3; ++i)
        direction[i] = At<float>(target, 0x20 + i * 4);
    if (distance < 0.0f)
    {
        Negate(direction);
        distance *= -1.0f;
    }
    RoadNav nav;
    if (void *chased = At<void *>(target, 0x1c))
    {
        if (void *perp = Find(chased, kPerpHandle))
        {
            if (Virtual<bool>(perp, 0x24))
                nav.Byte(kNavShortcuts) = *kShortcutsAllowed != 0;
            else
            {
                nav.Byte(kNavShortcuts) = 0;
                nav.Byte(kNavRaceOnly) = 0;
                nav.Byte(kNavRetry) = 1;
            }
        }
    }
    NFS_ORIGINAL(0x7658a0, void(__thiscall *)(void *, int))(nav.bytes, 0);
    *reinterpret_cast<int *>(nav.bytes + kNavMode) = 2;
    const float *from = reinterpret_cast<const float *>(target + 0x10);
    if (!nav.InitAt(from, direction))
    {
        if (!nav.Byte(kNavRetry))
            return false;
        nav.Byte(kNavRetry) = 0;
        if (!nav.InitAt(from, direction))
            return false;
    }
    NFS_ORIGINAL(0x77bba0, void(__thiscall *)(void *, float, const float *, int))(nav.bytes, distance, direction, 0);
    const float *found = reinterpret_cast<const float *>(nav.bytes + kNavPosition);
    const float *heading = reinterpret_cast<const float *>(nav.bytes + kNavDirection);
    for (int i = 0; i < 3; ++i)
    {
        position[i] = found[i];
        direction[i] = heading[i];
    }
    char *manager = static_cast<char *>(*kGameManager);
    const bool forward = !At<unsigned char>(manager, 0x2b4) && !At<unsigned char>(manager, 0x111);
    if (!NFS_ORIGINAL(0x419fc0, bool(__thiscall *)(void *, const float *, int, int, int, bool))(self + 0x48, position, 0,
                                                                                            0, 0, forward))
        return false;
    if (!forward)
        Negate(direction);
    return true;
}

bool __fastcall AICopManagerGetHeavySupportVehicles(char *self, void *, char *request)
{
    const int *support = At<const int *>(request, 0);
    const int kind = support[0];
    const char *const *names = nullptr;
    int count = 0;
    if (kind >= 1 && kind <= 4)
    {
        names = reinterpret_cast<const char *const *>(kHeavySupportNames[kind - 1]);
        count = kHeavySupportCounts[kind - 1];
    }
    const int chance = support[3];
    int made = At<int>(request, 0x14);
    for (; made < count; ++made)
    {
        void *random = NFS_ORIGINAL(0x6ed200, void *(__cdecl *)())();
        const int roll = (NFS_ORIGINAL(0x627320, int(__thiscall *)(void *))(random) * 100) >> 16;
        void *vehicle = AICopManagerGetAvailableCopVehicleByName(self, nullptr, roll < chance ? kHeavySuv : kHeavyRhino);
        if (!vehicle)
            break;
        char *vehicles = request + 8;
        const unsigned int size = At<unsigned int>(request, 0x14);
        if (size >= At<unsigned int>(request, 0x10))
        {
            const unsigned int capacity = Virtual<unsigned int, unsigned int>(vehicles, 0xc, size + 1);
            NFS_ORIGINAL(0x41ae90, void(__thiscall *)(void *, unsigned int))(vehicles, capacity);
        }
        void **slot = At<void **>(vehicles, 4) + At<unsigned int>(vehicles, 0xc);
        if (slot)
            *slot = vehicle;
        ++At<unsigned int>(vehicles, 0xc);
        if (void *pursuitAi = Find(vehicle, kPursuitAiHandle))
            Virtual<void, unsigned int>(pursuitAi, 0x84, stringhash32(names[made]));
        if (support[0] == kHeavyRoadBlockKind)
        {
            At<float>(self, 0xb0) = kHeavyRoadBlockDelay;
            if (made == count - 1)
            {
                const unsigned int port = stringhash32(reinterpret_cast<const char *>(0x891228));
                alignas(8) unsigned int message[8];
                NFS_ORIGINAL(0x405b20, void *(__thiscall *)(void *, int))(message, 0x10);
                message[1] = port;
                Deliver(message);
            }
        }
    }
    return At<float>(self, 0xb0) < 0.0f && made == count;
}

namespace
{
bool InCameraView(const float *position);
const float *const kHeavySupportDistance = reinterpret_cast<const float *>(0x8eb1ac);
const float kFirstSpawnBack = 60.0f;
const float kSpawnBackStep = 10.0f;
const int kSpawnAttempts = 8;
const int kMostSupport = 5;

/** GroundSupportRequest: its support record, the vehicles it got (a UTL::Vector at 8), state and the time it ends. */
void AddSupportVehicle(char *request, void *vehicle)
{
    char *vehicles = request + 8;
    const unsigned int size = At<unsigned int>(vehicles, 0xc);
    if (size >= At<unsigned int>(vehicles, 8))
    {
        const unsigned int capacity = Virtual<unsigned int, unsigned int>(vehicles, 0xc, size + 1);
        NFS_ORIGINAL(0x41ae90, void(__thiscall *)(void *, unsigned int))(vehicles, capacity);
    }
    void **slot = At<void **>(vehicles, 4) + At<unsigned int>(vehicles, 0xc);
    if (slot)
        *slot = vehicle;
    ++At<unsigned int>(vehicles, 0xc);
}

void **SupportBegin(char *request)
{
    return At<void **>(request, 0xc);
}

void **SupportEnd(char *request)
{
    return At<void **>(request, 0xc) + At<unsigned int>(request, 0x14);
}

const char *const *const kLeaderGoals[3] = {reinterpret_cast<const char *const *>(0x8eb1d4),
                                            reinterpret_cast<const char *const *>(0x8eb1d8),
                                            reinterpret_cast<const char *const *>(0x8eb1dc)};
const int kLeaderCounts[3] = {1, 1, 3};
} // namespace

bool __fastcall AICopManagerStartHeavySupport(char *self, void *, void *pursuit, char *request)
{
    float positions[kMostSupport][4];
    float directions[kMostSupport][4];
    float back = kFirstSpawnBack;
    int slot = 0;
    for (void **it = SupportBegin(request); it != SupportEnd(request); ++it, ++slot)
    {
        bool placed = false;
        const bool improved = nfs::aituning::CopsImproved();
        for (int attempt = 0; attempt < kSpawnAttempts && !placed; ++attempt)
        {
            const float distance = improved ? *kHeavySupportDistance - kFirstSpawnBack + attempt * kSpawnBackStep
                                            : *kHeavySupportDistance - back;
            back += kSpawnBackStep;
            placed = AICopManagerGetSpawnPositionAheadOfTarget(self, nullptr, pursuit, positions[slot], directions[slot],
                                                               distance) &&
                     (!improved || !InCameraView(positions[slot]));
        }
        if (!placed)
            return false;
    }
    slot = 0;
    for (void **it = SupportBegin(request); it != SupportEnd(request); ++it, ++slot)
    {
        void *vehicle = *it;
        Virtual<void>(vehicle, 0x80);
        void *ai = Virtual<void *>(vehicle, 0xac);
        for (int i = 0; i < 4; ++i)
            directions[slot][i] *= -1.0f;
        Virtual<void, const float *, const float *>(ai, 0x5c, positions[slot], directions[slot]);
        Virtual<void>(ai, 0x88);
        Virtual<void, void *>(pursuit, 0x78, vehicle);
        ++At<int>(self, 0x94);
    }
    At<int>(request, 0x78) = 2;
    At<unsigned int>(request, 0x74) = At<unsigned int>(At<char *>(request, 0), 8);
    return true;
}

bool __fastcall AICopManagerGetLeaderSupportVehicles(char *self, void *, char *request)
{
    const char *const names[3] = {reinterpret_cast<const char *>(0x891240), reinterpret_cast<const char *>(0x89124c),
                                  reinterpret_cast<const char *>(0x89124c)};
    const float offsets[3][4] = {{0.0f, 0.0f, -20.0f, 0.0f}, {-4.0f, 0.0f, -24.0f, 0.0f}, {4.0f, 0.0f, -24.0f, 0.0f}};
    const int kind = *At<const int *>(request, 4);
    const char *const *goals = nullptr;
    int count = 0;
    if (kind >= 5 && kind <= 7)
    {
        goals = kLeaderGoals[kind - 5];
        count = kLeaderCounts[kind - 5];
    }
    int made = At<int>(request, 0x14);
    for (; made < count; ++made)
    {
        void *vehicle = AICopManagerGetAvailableCopVehicleByName(self, nullptr, names[made]);
        if (!vehicle)
            break;
        AddSupportVehicle(request, vehicle);
        if (void *pursuitAi = Find(vehicle, kPursuitAiHandle))
        {
            Virtual<void, unsigned int>(pursuitAi, 0x84, stringhash32(goals[made]));
            Virtual<void, const float *>(pursuitAi, 0x40, offsets[made]);
            const unsigned int none = stringhash32(nullptr);
            Virtual<void, const unsigned int *>(pursuitAi, 0x48, &none);
        }
    }
    return made == count;
}

namespace
{
const float *const kLeaderSupportDistance = reinterpret_cast<const float *>(0x8eb1a8);
const int kMostLeaders = 3;
} // namespace

bool __fastcall AICopManagerStartLeaderSupport(char *self, void *, void *pursuit, char *request)
{
    float positions[kMostLeaders][4];
    float directions[kMostLeaders][4];
    float back = 0.0f;
    int slot = 0;
    for (void **it = SupportBegin(request); it != SupportEnd(request); ++it, ++slot)
    {
        bool placed = false;
        for (int attempt = 0; attempt < kSpawnAttempts && !placed; ++attempt)
        {
            const float distance = (*kLeaderSupportDistance + back) * -1.0f;
            back += kSpawnBackStep;
            placed = AICopManagerGetSpawnPositionAheadOfTarget(self, nullptr, pursuit, positions[slot], directions[slot],
                                                               distance);
        }
        if (!placed)
            return false;
    }
    slot = 0;
    for (void **it = SupportBegin(request); it != SupportEnd(request); ++it, ++slot)
    {
        void *vehicle = *it;
        Virtual<void>(vehicle, 0x80);
        char *ai = Virtual<char *>(vehicle, 0xac);
        for (int i = 0; i < 4; ++i)
            directions[slot][i] *= -1.0f;
        Virtual<void, const float *, const float *>(ai, 0x5c, positions[slot], directions[slot]);
        void *pursuitAi = Find(ai, kPursuitAiHandle);
        const float *wanted = Virtual<const float *>(pursuitAi, 0x44);
        const float offset[3] = {wanted[0], wanted[1], wanted[2]};
        Virtual<void>(ai, 0x88);
        Virtual<void, const float *>(pursuitAi, 0x40, offset);
        Virtual<void, bool>(pursuitAi, 0x30, true);
        Virtual<void, void *>(pursuit, 0x78, vehicle);
        ++At<int>(self, 0x94);
    }
    At<int>(request, 0x78) = 2;
    At<unsigned int>(request, 0x74) = At<unsigned int>(At<char *>(request, 4), 8);
    return true;
}

namespace
{
char *const kVehicleList = reinterpret_cast<char *>(0x92cd1c);
const float kSpotMargin = 2.0f;

/** DoesSpotOverlapCar (0x410f90, which takes the spot in ebx): whether a simulated vehicle is within its radius, the
 * given radius and a margin of the spot. */
bool SpotOverlapsCar(const float *spot, float radius)
{
    for (void **it = Begin(kVehicleList); it != End(kVehicleList); ++it)
    {
        void *vehicle = *it;
        if (Virtual<int>(vehicle, 0x38) != 1)
            continue;
        void *simable = Virtual<void *>(vehicle, 4);
        if (!simable || !Virtual<void *>(simable, 0x54))
            continue;
        const float reach = Virtual<float>(Virtual<void *>(simable, 0x54), 0x14) + radius + kSpotMargin;
        const float *centre = Virtual<const float *>(Virtual<void *>(simable, 0x54), 0x20);
        const float dz = spot[2] - centre[2];
        const float dy = spot[1] - centre[1];
        const float dx = spot[0] - centre[0];
        if (reach * reach > dx * dx + dy * dy + dz * dz)
            return true;
    }
    return false;
}

void FreeListNode(char *node, unsigned int size)
{
    At<char *>(At<char *>(node, 4), 0) = At<char *>(node, 0);
    At<char *>(At<char *>(node, 0), 4) = At<char *>(node, 4);
    NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
        reinterpret_cast<void *>(0x925b30), node, size, nullptr);
}

void *const kRoadBlockHandle = reinterpret_cast<void *>(0x4038d0);
const unsigned int kSpawnRequestNode = 0x44;
} // namespace

void __fastcall AICopManagerOnDetached(char *self, void *, char *attachable)
{
    if (void *vehicle = Find(attachable, kVehicleHandle))
    {
        char *cops = self + 0x8c;
        void **it = At<void **>(cops, 4);
        void **end = it + At<int>(cops, 0xc);
        while (it != end && *it != vehicle)
            ++it;
        if (it != At<void **>(cops, 4) + At<int>(cops, 0xc))
            NFS_ORIGINAL(0x41b7b0, void(__thiscall *)(void *, void **, void **))(cops, it, it + 1);
        if (void *watcher = At<void *>(self, 0x108))
            Virtual<void, void *>(watcher, 0xc, vehicle);
    }
    else if (void *activity = Find(attachable, kPursuitActivityHandle))
    {
        char *head = At<char *>(self, 0xf0);
        char *node = At<char *>(head, 0);
        while (node != head && At<void *>(node, 8) != activity)
            node = At<char *>(node, 0);
        if (node != At<char *>(self, 0xf0))
        {
            FreeListNode(node, 0xc);
            --At<int>(self, 0xf4);
        }
    }
    else if (void *roadBlock = Find(attachable, kRoadBlockHandle))
    {
        char *head = At<char *>(self, 0xfc);
        void *found;
        void *wanted = roadBlock;
        NFS_ORIGINAL(0x425460, void **(__cdecl *)(void **, void *, void *, void *const *))(&found, At<void *>(head, 0),
                                                                                         head, &wanted);
        void *after;
        NFS_ORIGINAL(0x42cb60, void **(__thiscall *)(void *, void **, void *))(self + 0xf8, &after, found);
    }
    char *watched = At<char *>(self, 0x108);
    if (watched && At<void *>(attachable, 4) == At<void *>(watched, 4))
        At<void *>(self, 0x108) = nullptr;
}

bool __fastcall AICopManagerTrySpawnCop(char *self, void *, const char *request)
{
    float position[3] = {At<float>(const_cast<char *>(request), 0), At<float>(const_cast<char *>(request), 4),
                         At<float>(const_cast<char *>(request), 8)};
    float direction[3] = {At<float>(const_cast<char *>(request), 0xc), At<float>(const_cast<char *>(request), 0x10),
                          At<float>(const_cast<char *>(request), 0x14)};
    AICopManagerUpdateCopPriorities(self, nullptr, At<int>(self, 0x94) + 1);
    void *vehicle = AICopManagerGetAvailableCopVehicleByName(self, nullptr, request + 0x18);
    if (!vehicle)
        return false;
    void *pursuitAi = Find(vehicle, kPursuitAiHandle);
    if (!pursuitAi)
        return false;
    void *simable = Virtual<void *>(vehicle, 4);
    if (!simable || !Virtual<void *>(simable, 0x54))
        return false;
    const float radius = Virtual<float>(Virtual<void *>(simable, 0x54), 0x14);
    if (SpotOverlapsCar(position, radius))
        return false;
    position[1] += 1.0f;
    const bool grounded = Virtual<bool, const float *, const float *>(vehicle, 0x68, position, direction);
    Virtual<void>(vehicle, 0x80);
    void *ai = Virtual<void *>(vehicle, 0xac);
    Virtual<void>(ai, 0x88);
    if (!grounded)
    {
        RoadNav nav;
        nav.InitAt(position, direction);
        position[1] = *reinterpret_cast<const float *>(nav.bytes + 0x9c) + 1.0f;
        Virtual<void>(ai, 0x98);
    }
    if (At<unsigned char>(const_cast<char *>(request), 0x38))
    {
        void *player = NFS_ORIGINAL(0x425200, void *(__cdecl *)(int))(1);
        void *playerSimable = Virtual<void *>(player, 4);
        void *activity = AICopManagerGetPursuitActivity(self, nullptr, static_cast<char *>(playerSimable));
        Virtual<void, void *, void *>(pursuitAi, 0x18, nullptr, playerSimable);
        Virtual<void, void *>(activity, 0x78, vehicle);
    }
    else if (At<unsigned char>(const_cast<char *>(request), 0x39))
        Virtual<void>(pursuitAi, 8);
    else
        Virtual<void>(pursuitAi, 4);
    ++At<int>(self, 0x94);
    return true;
}

void __fastcall AICopManagerUpdateSpawnRequests(char *self)
{
    char *node = At<char *>(At<char *>(self, 0x6c), 0);
    while (node != At<char *>(self, 0x6c))
    {
        if (!AICopManagerTrySpawnCop(self, nullptr, node + 8))
        {
            node = At<char *>(node, 0);
            continue;
        }
        char *next = At<char *>(node, 0);
        FreeListNode(node, kSpawnRequestNode);
        --At<int>(self, 0x70);
        node = next;
    }
}

namespace
{
float SimTime()
{
    return NFS_ORIGINAL(0x6e8de0, float(__cdecl *)())();
}

void *const kSteeringHandle = reinterpret_cast<void *>(0x405000);
void *const kVehicleAiHandle = reinterpret_cast<void *>(0x403700);
const unsigned int kBreakerZoneNode = 0x1c;
const float kHeadOnSpawnDistance = 80.0f;
const float kSpawnShortfall = 60.0f;

bool HeadOnSpawning()
{
    char *manager = static_cast<char *>(*kGameManager);
    return At<unsigned char>(manager, 0x2b4) || At<unsigned char>(manager, 0x111);
}

/** std::list::push_back through the original list's node maker and size check. */
void PushBack(char *list, unsigned int buyNode, unsigned int incSize, const void *value)
{
    char *head = At<char *>(list, 4);
    char *node = static_cast<char *>(
        NFS_ORIGINAL(buyNode, void *(__thiscall *)(void *, void *, void *, const void *))(list, head, At<void *>(head, 4),
                                                                                          value));
    NFS_ORIGINAL(incSize, void(__thiscall *)(void *, unsigned int))(list, 1);
    At<char *>(head, 4) = node;
    At<char *>(At<char *>(node, 4), 0) = node;
}

struct SpawnCopRequest
{
    float position[3];
    float direction[3];
    char name[0x20];
    bool pursue;
    bool support;
    unsigned char pad[2];
};

struct BreakerZone
{
    float centre[3];
    float ends;
    float radius;
};
} // namespace

void __fastcall AICopManagerApplyBreakerZones(char *self)
{
    if (!At<int>(self, 0x7c))
        return;
    for (void **it = Begin(self + 0xc8); it != End(self + 0xc8); ++it)
    {
        void *vehicle = *it;
        if (Virtual<bool>(vehicle, 0x7c) || !Virtual<void *>(vehicle, 0xac) || !Find(vehicle, kPursuitAiHandle))
            continue;
        char *head = At<char *>(self, 0x78);
        for (char *zone = At<char *>(head, 0); zone != At<char *>(self, 0x78); zone = At<char *>(zone, 0))
        {
            const float *centre = &At<float>(zone, 8);
            const float *position = Virtual<const float *>(vehicle, 0xc);
            const float dz = position[2] - centre[2];
            const float dx = position[0] - centre[0];
            if (sqrtf(dx * dx + dz * dz) < At<float>(zone, 0x18))
            {
                if (void *steering = Find(vehicle, kSteeringHandle))
                    Virtual<void>(steering, 0x1c);
                break;
            }
        }
    }
    char *head = At<char *>(self, 0x78);
    for (char *zone = At<char *>(head, 0); zone != At<char *>(self, 0x78);)
    {
        char *next = At<char *>(zone, 0);
        if (SimTime() > At<float>(zone, 0x14) && zone != At<char *>(self, 0x78))
        {
            FreeListNode(zone, kBreakerZoneNode);
            --At<int>(self, 0x7c);
        }
        zone = next;
    }
}

bool __fastcall AICopManagerSpawnPursuitCar(char *self, void *, void *pursuit)
{
    void *vehicle = AICopManagerGetAvailableCopVehicleByClass(self, nullptr, *kCopCarClass, true);
    const float distance = HeadOnSpawning() ? kHeadOnSpawnDistance : *kHeavySupportDistance - kSpawnShortfall;
    if (!vehicle)
    {
        vehicle = AICopManagerGetActiveCopVehicleFromOutOfView(self, nullptr, *kCopCarClass);
        if (!vehicle)
            return false;
    }
    float position[3];
    float direction[3];
    if (!AICopManagerGetSpawnPositionAheadOfTarget(self, nullptr, pursuit, position, direction, distance))
    {
        if (Virtual<void *>(pursuit, 8))
            AICopManagerSpawnCopCarNow(self, nullptr, pursuit);
        return false;
    }
    Virtual<void>(vehicle, 0x80);
    char *ai = Virtual<char *>(vehicle, 0xac);
    Virtual<void, const float *, const float *>(ai, 0x5c, position, direction);
    Virtual<void>(ai, 0x88);
    if (void *pursuitAi = Find(ai, kPursuitAiHandle))
        Virtual<void>(pursuitAi, 0x78);
    Virtual<void, void *>(pursuit, 0x78, vehicle);
    ++At<int>(self, 0x94);
    return true;
}

void __fastcall AICopManagerSpawnCop(char *self, void *, const float *position, const float *direction,
                                     const char *name, bool pursue, bool support)
{
    SpawnCopRequest request;
    for (int i = 0; i < 3; ++i)
    {
        request.position[i] = position[i];
        request.direction[i] = direction[i];
    }
    request.pursue = pursue;
    request.support = support;
    bStrCpy(request.name, name);
    PushBack(self + 0x14, 0x41c450, 0x434100, &request);
}

void __fastcall AICopManagerMessageBreakerStopCops(char *self, void *, const char *message)
{
    BreakerZone zone;
    const float duration = At<float>(const_cast<char *>(message), 0x1c);
    zone.radius = At<float>(const_cast<char *>(message), 0x20);
    for (int i = 0; i < 3; ++i)
        zone.centre[i] = At<float>(const_cast<char *>(message), 0x10 + i * 4);
    zone.ends = SimTime() + duration;
    PushBack(self + 0x74, 0x41c490, 0x4341a0, &zone);
}

bool __fastcall AICopManagerOnTask(char *self, void *, void *task, float dt)
{
    At<float>(self, 0xa4) -= dt;
    if (task != At<void *>(self, 0x78))
        return true;
    char *manager = self - 8;
    AICopManagerUpdateDebug(manager);
    At<int>(self, 0xb8) = 0;
    AICopManagerApplyBreakerZones(manager);
    AICopManagerUpdatePatrols(manager);
    AICopManagerUpdatePursuits(manager);
    AICopManagerUpdateRoadBlocks(manager);
    AICopManagerUpdateSpawnRequests(manager);
    At<float>(self, 0xa8) -= dt;
    return true;
}

namespace
{
float Normalize(float *vector)
{
    return NFS_ORIGINAL(0x410220, float(__cdecl *)(float *))(vector);
}

void *MakeActivity()
{
    const unsigned int kind = stringhash32(reinterpret_cast<const char *>(0x8915e4));
    for (unsigned int *factory = static_cast<unsigned int *>(*kActivityFactories); factory;
         factory = reinterpret_cast<unsigned int *>(factory[2]))
    {
        if (factory[0] == kind)
        {
            const ActivityParams params = {};
            return reinterpret_cast<void *(__cdecl *)(ActivityParams)>(factory[1])(params);
        }
    }
    return nullptr;
}
} // namespace

void __fastcall AICopManagerUpdateCopPriorities(char *self, void *, int wanted)
{
    if (wanted <= At<int>(self, 0x8c))
        return;
    const float *playerPosition = Virtual<const float *>(Virtual<void *>(FirstPlayer(), 4), 0x60);
    const float position[3] = {playerPosition[0], playerPosition[1], playerPosition[2]};
    float heading[3];
    Virtual<void, float *>(Virtual<void *>(Virtual<void *>(FirstPlayer(), 4), 0x54), 0x34, heading);
    float best = 0.0f;
    bool found = false;
    int bestIndex = 0;
    int index = 0;
    for (void **it = Begin(self + 0xc8); it != End(self + 0xc8); ++it, ++index)
    {
        void *cop = *it;
        if (!Virtual<bool>(cop, 0x88))
            continue;
        const float *at = Virtual<const float *>(cop, 0xc);
        float toward[3] = {at[0] - position[0], at[1] - position[1], at[2] - position[2]};
        Normalize(toward);
        float score = heading[2] * toward[2] + heading[1] * toward[1] + toward[0] * heading[0];
        void *renderable = Find(cop, kRenderableHandle);
        if (renderable && Virtual<bool>(renderable, 4))
            score += 1.0f;
        if (!found || score < best)
        {
            best = score;
            found = true;
            bestIndex = index;
        }
    }
    index = 0;
    for (void **it = Begin(self + 0xc8); it != End(self + 0xc8); ++it, ++index)
    {
        void *cop = *it;
        if (!Virtual<bool>(cop, 0x88) || index != bestIndex)
            continue;
        if (void *pursuit = Virtual<void *>(Virtual<void *>(cop, 0xac), 0xa0))
            Virtual<bool, void *>(pursuit, 0x7c, cop);
        else if (Virtual<void *>(Virtual<void *>(cop, 0xac), 0xa4))
            Virtual<bool, void *>(Virtual<void *>(Virtual<void *>(cop, 0xac), 0xa4), 0xc, cop);
        Virtual<void>(Virtual<void *>(cop, 0xac), 0x8c);
        void *simable = Virtual<void *>(cop, 4);
        const unsigned int port = stringhash32(reinterpret_cast<const char *>(0x890fc0));
        alignas(8) unsigned int message[8];
        MUnspawnCopConstruct(message, nullptr, Virtual<void *>(simable, 0x34), 1);
        message[1] = port;
        Deliver(message);
        if (IsHelicopter(cop))
            --At<int>(self, 0x9c);
        else
            --At<int>(self, 0x94);
        return;
    }
}

void __fastcall AICopManagerPursueAtHeatLevel(char *self, void *, int heat)
{
    void *player = *At<void **>(kPlayers, 0);
    if (!player)
        return;
    char *manager = self - 0x54;
    void *existing = nullptr;
    bool found = false;
    char *head = At<char *>(self, 0xd4);
    for (char *node = At<char *>(head, 0); node != At<char *>(self, 0xd4); node = At<char *>(node, 0))
    {
        void *pursuit = At<void *>(node, 8);
        if (At<void *>(Virtual<char *>(pursuit, 8), 0x1c) == Virtual<void *>(player, 4))
        {
            existing = pursuit;
            found = true;
            break;
        }
    }
    if (void *perp = Find(Virtual<void *>(player, 4), kPerpHandle))
    {
        const float wanted = static_cast<float>(heat);
        if (Virtual<float>(perp, 4) < wanted)
        {
            const float now = Virtual<float>(perp, 4);
            Virtual<void, float>(perp, 8, now < wanted ? wanted : now);
            if (existing)
                Virtual<void>(existing, 0x138);
        }
    }
    if (found)
    {
        if (!Virtual<bool>(self, 0x20))
            AICopManagerSpawnPursuitCar(manager, nullptr, existing);
        return;
    }
    void *made = MakeActivity();
    void *pursuit = Find(made, kPursuitActivityHandle);
    Virtual<void, void *>(self - 0x28, 8, pursuit);
    if (void *attachable = Find(Virtual<void *>(player, 4), kAttachableHandle))
        Virtual<void, void *>(attachable, 4, pursuit);
    pursuit = Find(made, kPursuitActivityHandle);
    if (!pursuit)
        return;
    void *simable = Virtual<void *>(player, 4);
    NFS_ORIGINAL(0x423860, void(__thiscall *)(void *, void *))(Virtual<void *>(pursuit, 8), simable);
    AICopManagerSpawnPursuitCar(manager, nullptr, pursuit);
}

bool __fastcall CopManagerCreateRoadBlock(char *manager, void *, void *pursuit, int cops, void *helicopter, void *list);

namespace
{
const int kMostRoadBlocks = 64;
void *const kSmackableOwnerHandle = reinterpret_cast<void *>(0x4028f0);

unsigned int Crc(unsigned int text)
{
    unsigned int crc;
    NFS_ORIGINAL(0x4023c0, void *(__thiscall *)(void *, const char *))(&crc, reinterpret_cast<const char *>(text));
    return crc;
}

void DeliverBackup(unsigned int port, int cops)
{
    alignas(8) unsigned int message[8];
    NFS_ORIGINAL(0x405b20, void *(__thiscall *)(void *, int))(message, cops);
    message[1] = port;
    Deliver(message);
}

void DeliverRoadBlockRequest(unsigned int port, int cops)
{
    alignas(8) unsigned int message[8];
    NFS_ORIGINAL(0x405a40, void *(__thiscall *)(void *, int))(message, cops);
    message[1] = port;
    Deliver(message);
}
} // namespace

void __fastcall AICopManagerUpdateRoadBlocks(char *self)
{
    void *finished[kMostRoadBlocks];
    int count = 0;
    char *head = At<char *>(self, 0x134);
    for (char *node = At<char *>(head, 0); node != At<char *>(self, 0x134); node = At<char *>(node, 0))
    {
        void *roadBlock = At<void *>(node, 8);
        if (!Virtual<void *>(roadBlock, 0x14) && count < kMostRoadBlocks)
            finished[count++] = roadBlock;
        if (Virtual<bool>(roadBlock, 0x24))
            continue;
        void *pursuit = Virtual<void *>(roadBlock, 0x1c);
        if (!pursuit)
            continue;
        char *target = Virtual<char *>(pursuit, 8);
        if (!target)
            continue;
        const float *chased = reinterpret_cast<const float *>(target + 0x10);
        const float *centre = Virtual<const float *>(roadBlock, 0x38);
        const float c[3] = {centre[0], centre[1], centre[2]};
        const float *facing = Virtual<const float *>(roadBlock, 0x3c);
        const float d[3] = {facing[0], facing[1], facing[2]};
        float toward[3] = {c[0] - chased[0], c[1] - chased[1], c[2] - chased[2]};
        NFS_ORIGINAL(0x40fe20, void(__cdecl *)(float *, float *))(toward, toward);
        if (!(d[2] * toward[2] + d[1] * toward[1] + toward[0] * d[0] < 0.0f))
            continue;
        Virtual<void, bool>(roadBlock, 0x20, true);
        Virtual<void>(pursuit, 0xe8);
        NFS_ORIGINAL(0x5edb50, void(__thiscall *)(void *, const char *))(*kGameManager,
                                                                         reinterpret_cast<const char *>(0x892b98));
        const unsigned int port = stringhash32(reinterpret_cast<const char *>(0x892b90));
        unsigned int kind;
        NFS_ORIGINAL(0x4059c0, unsigned int *(__cdecl *)(unsigned int *))(&kind);
        unsigned int message[5] = {kind, port, 0x14, 0, 0};
        Deliver(message);
        if (Virtual<short>(roadBlock, 0x48) <= 0)
            continue;
        const float tracked = NFS_ORIGINAL(0x5edbc0, float(__thiscall *)(void *, const char *))(
            *kGameManager, reinterpret_cast<const char *>(0x892b7c));
        const float base = 0.0f > tracked ? 0.0f : tracked;
        char *race = static_cast<char *>(*kRaceStatus);
        void *parameters = race ? At<void *>(race, 0x1968) : nullptr;
        if (!race || At<int>(race, 0x1960) != 1 ||
            (parameters && NFS_ORIGINAL(0x5fbe70, bool(__thiscall *)(void *))(parameters)))
        {
            const int spikes = Virtual<short>(roadBlock, 0x48);
            TrackValue(reinterpret_cast<const char *>(0x892b7c), static_cast<float>(spikes) + base);
        }
        Virtual<void, int>(pursuit, 0xfc, Virtual<short>(roadBlock, 0x48));
    }
    for (int i = 0; i < count; ++i)
    {
        void *roadBlock = finished[i];
        Virtual<void>(roadBlock, 0x10);
        if (void *owner = Find(roadBlock, kSmackableOwnerHandle))
            Virtual<void>(owner, 4);
        Virtual<void, void *>(self + 0x2c, 0xc, roadBlock);
    }
}

void __fastcall AICopManagerUpdateSupportCops(char *self, void *, void *pursuit)
{
    char *request = Virtual<char *>(pursuit, 0x44);
    if (!request || At<int>(request, 0x78) != 1)
        return;
    if (At<void *>(request, 0))
    {
        if (!AICopManagerGetHeavySupportVehicles(self, nullptr, request))
            return;
        if (*At<const int *>(request, 0) == 4)
        {
            if (Virtual<void *>(pursuit, 0x3c))
                return;
            const bool made = CopManagerCreateRoadBlock(self, nullptr, pursuit, 4, nullptr, request + 8);
            const unsigned int port = Crc(0x892ee8);
            if (made)
            {
                At<int>(request, 0x78) = 2;
                At<unsigned int>(request, 0x74) = At<unsigned int>(At<char *>(request, 0), 8);
            }
            DeliverRoadBlockRequest(port, made ? 1 : 0);
            return;
        }
        if (AICopManagerStartHeavySupport(self, nullptr, pursuit, request))
            return;
        DeliverBackup(Crc(0x892edc), 0x10);
        return;
    }
    if (!AICopManagerGetLeaderSupportVehicles(self, nullptr, request))
        return;
    const bool started = AICopManagerStartLeaderSupport(self, nullptr, pursuit, request);
    DeliverBackup(Crc(started ? 0x891228 : 0x892edc), 0x40);
}

namespace
{
const int kMostEndedPursuits = 64;

bool PursuitCountsAsRace()
{
    char *race = static_cast<char *>(*kRaceStatus);
    if (!race || At<int>(race, 0x1960) != 1)
        return true;
    void *parameters = At<void *>(race, 0x1968);
    return parameters && NFS_ORIGINAL(0x5fbe70, bool(__thiscall *)(void *))(parameters);
}
} // namespace

void __fastcall AICopManagerUpdatePursuits(char *self)
{
    void *helicopter = nullptr;
    for (void **it = Begin(self + 0xc8); it != End(self + 0xc8); ++it)
    {
        void *cop = *it;
        const bool leaving = Virtual<bool>(cop, 0x7c);
        if (Virtual<bool>(cop, 0x88) && !leaving)
        {
            if (IsHelicopter(cop))
                helicopter = cop;
            continue;
        }
        bool removed = false;
        if (void *pursuit = Virtual<void *>(Virtual<void *>(cop, 0xac), 0xa0))
        {
            Virtual<bool, void *>(pursuit, 0x7c, cop);
            removed = true;
        }
        else if (Virtual<void *>(Virtual<void *>(cop, 0xac), 0xa4))
        {
            if (leaving)
            {
                void *roadBlock = Virtual<void *>(Virtual<void *>(cop, 0xac), 0xa4);
                if (void *roadBlockPursuit = Virtual<void *>(roadBlock, 0x1c))
                    Virtual<void, void *>(roadBlockPursuit, 0x9c, cop);
            }
            Virtual<bool, void *>(Virtual<void *>(Virtual<void *>(cop, 0xac), 0xa4), 0xc, cop);
            removed = true;
        }
        const bool counts = PursuitCountsAsRace();
        if (leaving && removed && counts)
        {
            const int lost = ++At<int>(self, 0xa8);
            TrackValue(reinterpret_cast<const char *>(0x892ef0), static_cast<float>(lost));
        }
    }
    void *ended[kMostEndedPursuits];
    int endedCount = 0;
    char *watcher = nullptr;
    if (void *support = At<void *>(self, 0xc0))
    {
        if (void *pursuitAi = Find(support, kPursuitAiHandle))
            watcher = Virtual<char *>(pursuitAi, 0x2c);
    }
    bool roadBlockAsked = false;
    char *head = At<char *>(self, 0x128);
    for (char *node = At<char *>(head, 0); node != At<char *>(self, 0x128); node = At<char *>(node, 0))
    {
        void *pursuit = At<void *>(node, 8);
        if (At<void *>(self, 0xc0) && watcher && Virtual<bool, void *>(pursuit, 4, watcher))
        {
            Virtual<void, void *>(pursuit, 0x78, At<void *>(self, 0xc0));
            At<void *>(self, 0xc0) = nullptr;
        }
        if (Virtual<bool>(pursuit, 0x70))
        {
            if (endedCount < kMostEndedPursuits)
                ended[endedCount++] = pursuit;
            if (Virtual<bool>(pursuit, 0x74))
            {
                At<float>(self, 0xa4) = 0.0f;
                NFS_ORIGINAL(0x602770, void(__thiscall *)(void *, int))(*kGameManager, 0);
            }
            if (!Virtual<bool>(pursuit, 0x74))
                Virtual<void, void *>(self + 0x54, 0x10, pursuit);
            AICopManagerCommitPursuitDetails(self, nullptr, pursuit);
            continue;
        }
        AICopManagerUpdateSupportCops(self, nullptr, pursuit);
        if (At<int>(self, 0x94) < At<int>(self, 0x98) && !At<void *>(self, 0xbc))
        {
            if (const char *name = Virtual<const char *>(pursuit, 0x50))
            {
                if (stringhash32(name) == stringhash32(kHelicopterCar))
                    AICopManagerSpawnPursuitHelicopter(self, nullptr, pursuit);
                else if (void *vehicle = AICopManagerGetAvailableCopVehicleByName(self, nullptr, name))
                    AICopManagerSpawnPursuitIVehicle(self, nullptr, pursuit, vehicle);
            }
        }
        if (!At<void *>(self, 0xbc))
        {
            const int cops = Virtual<int>(pursuit, 0x84);
            if (cops > 0)
            {
                At<int>(self, 0xb8) = cops;
                At<void *>(self, 0xbc) = pursuit;
            }
        }
        if (At<void *>(self, 0xbc) != pursuit || Virtual<void *>(pursuit, 0x3c))
            continue;
        roadBlockAsked = true;
        const bool made = CopManagerCreateRoadBlock(self, nullptr, pursuit, At<int>(self, 0xb8), helicopter, nullptr);
        const unsigned int port = stringhash32(reinterpret_cast<const char *>(0x892ee8));
        unsigned int kind;
        NFS_ORIGINAL(0x4059c0, unsigned int *(__cdecl *)(unsigned int *))(&kind);
        unsigned int message[5] = {kind, port, 0x14, 0, made ? 1u : 0u};
        Deliver(message);
        if (made)
        {
            At<void *>(self, 0xbc) = nullptr;
            At<int>(self, 0xb8) = 0;
        }
    }
    if (!roadBlockAsked)
    {
        At<void *>(self, 0xbc) = nullptr;
        At<int>(self, 0xb8) = 0;
    }
    for (int i = 0; i < endedCount; ++i)
    {
        void *pursuit = ended[i];
        Virtual<void, void *>(self + 0x2c, 0xc, pursuit);
        char *target = Virtual<char *>(pursuit, 8);
        if (At<unsigned char>(target, 0x2c))
        {
            if (void *attachable = Find(pursuit, kAttachableHandle))
                Virtual<void, void *>(attachable, 8, At<void *>(Virtual<char *>(pursuit, 8), 0x1c));
        }
        for (void **it = Begin(self + 0xc8); it != End(self + 0xc8); ++it)
        {
            void *cop = *it;
            char *copPursuit = Virtual<char *>(Virtual<void *>(cop, 0xac), 0xa0);
            if (copPursuit && At<void *>(copPursuit, 4) == At<void *>(pursuit, 4))
            {
                Virtual<bool, void *>(pursuit, 0x7c, cop);
                if (void *pursuitAi = Find(cop, kPursuitAiHandle))
                    Virtual<void>(pursuitAi, 0xc);
            }
        }
        if (void *owner = Find(pursuit, kSmackableOwnerHandle))
            Virtual<void>(owner, 4);
    }
    if (!At<void *>(self, 0xc0))
        return;
    void *made = MakeActivity();
    void *activity = Find(made, kPursuitActivityHandle);
    Virtual<void, void *>(self + 0x2c, 8, activity);
    void *chased = At<void *>(watcher, 0x1c);
    if (chased)
    {
        if (void *attachable = Find(chased, kAttachableHandle))
            Virtual<void, void *>(attachable, 4, activity);
    }
    if (void *attachable = Find(activity, kAttachableHandle))
        Virtual<void, void *>(attachable, 4, At<void *>(watcher, 0x1c));
    Virtual<void, void *>(activity, 0x78, At<void *>(self, 0xc0));
}

namespace
{
void *const *const kActiveHelicopter = reinterpret_cast<void *const *>(0x90d61c);
const float kHelicopterAhead = 250.0f;
const float kHelicopterHeight = 20.0f;
} // namespace

bool __fastcall AICopManagerSpawnPursuitHelicopter(char *self, void *, void *pursuit)
{
    if (*kActiveHelicopter)
        return false;
    void *helicopter = AICopManagerGetAvailableCopVehicleByClass(self, nullptr, *kHelicopterClass, false);
    if (!helicopter)
        return false;
    char *target = Virtual<char *>(pursuit, 8);
    if (!target)
        return false;
    const float *chased = reinterpret_cast<const float *>(target + 0x10);
    const float *heading = reinterpret_cast<const float *>(target + 0x20);
    const float from[3] = {chased[0], chased[1], chased[2]};
    float back[3] = {heading[0] * -1.0f, heading[1] * -1.0f, heading[2] * -1.0f};
    float position[3];
    float facing[3];
    {
        RoadNav nav;
        NFS_ORIGINAL(0x7658a0, void(__thiscall *)(void *, int))(nav.bytes, 0);
        *reinterpret_cast<int *>(nav.bytes + kNavMode) = 2;
        if (nav.InitAt(from, back))
        {
            NFS_ORIGINAL(0x77bba0, void(__thiscall *)(void *, float, const float *, int))(nav.bytes, kHelicopterAhead,
                                                                                         back, 0);
            const float *found = reinterpret_cast<const float *>(nav.bytes + kNavPosition);
            const float *along = reinterpret_cast<const float *>(nav.bytes + kNavDirection);
            for (int i = 0; i < 3; ++i)
            {
                position[i] = found[i];
                facing[i] = along[i];
            }
            Normalize(facing);
        }
        else
        {
            for (int i = 0; i < 3; ++i)
            {
                position[i] = chased[i];
                facing[i] = heading[i];
            }
        }
        position[1] += kHelicopterHeight;
        Virtual<void>(helicopter, 0x80);
        alignas(16) float made[16];
        alignas(16) float matrix[16];
        const float *generated = NFS_ORIGINAL(0x6f1220, const float *(__cdecl *)(float *, const float *, const float *))(
            made, facing, nullptr);
        for (int i = 0; i < 16; ++i)
            matrix[i] = generated[i];
        void *body = Virtual<void *>(Virtual<void *>(helicopter, 4), 0x54);
        Virtual<void, const float *, const float *>(body, 0x98, matrix, position);
        Virtual<void>(Virtual<void *>(helicopter, 0xac), 0x88);
        Virtual<void, void *>(pursuit, 0x78, helicopter);
        ++At<int>(self, 0x9c);
    }
    return true;
}

namespace
{
const float kChaseSpawnExtra = 80.0f;
const float *const kZeroVectorFloat = reinterpret_cast<const float *>(0x8a2fb4);
const float kSpawnSwing = 1.0f / 6.0f;
const float kFallbackAhead = 200.0f;
const float kFallbackScatter = 190.0f;
const int kSegmentFlagsSize = 0x16;
const unsigned char *const *const kRoadSegments = reinterpret_cast<const unsigned char *const *>(0x9b38c0);

float Length(const float *v)
{
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

/** sub_4050A0: turn a vector about the up axis by a fraction of a full turn. */
void Turn(float turns, const float *in, float *out)
{
    const float angle = turns * 6.2831855f;
    float c;
    float s;
    __asm {
        fld angle
        fsincos
        fstp c
        fstp s
    }
    const float x = c * in[0] - s * in[2];
    const float z = c * in[2] + s * in[0];
    out[0] = x;
    out[2] = z;
    out[1] = in[1];
}

/** Whether a road segment lets the navigation retry the other way. */
bool SegmentRetry(int segment)
{
    const unsigned char flags = (*kRoadSegments)[segment * kSegmentFlagsSize + 0xa];
    return ((~(flags >> 5)) ^ (flags >> 1)) & 1;
}

bool CheckSpawnPosition(char *self, const float *position, bool forward)
{
    return NFS_ORIGINAL(0x419fc0, bool(__thiscall *)(void *, const float *, int, int, int, bool))(self + 0x48, position,
                                                                                            0, 0, 0, forward);
}

/** Cop tactics improved: a cop does not appear where the camera sees it: nearer than this (m) and inside this cosine
 * of the camera's heading, its spawn moves on along the road (kOutOfViewStep at a time, up to kOutOfViewFarthest). */
const float kInViewDistance = 320.0f;
const float kInViewCosine = 0.5f;
const float kOutOfViewStep = 50.0f;
const float kOutOfViewFarthest = 400.0f;

/** Whether the main camera would show a car at `position` (simulation space). */
bool InCameraView(const float *position)
{
    const char *camera = *reinterpret_cast<char *const *>(0x919690);
    if (!camera)
        return false;
    const float eye[3] = {-At<float>(const_cast<char *>(camera), 0x44), At<float>(const_cast<char *>(camera), 0x48),
                          At<float>(const_cast<char *>(camera), 0x40)};
    const float look[3] = {-At<float>(const_cast<char *>(camera), 0x54), At<float>(const_cast<char *>(camera), 0x58),
                           At<float>(const_cast<char *>(camera), 0x50)};
    const float d[3] = {position[0] - eye[0], position[1] - eye[1], position[2] - eye[2]};
    const float distance = Length(d);
    if (distance > kInViewDistance || distance < 1.0f)
        return distance < 1.0f;
    const float lookLength = Length(look);
    if (lookLength < 0.001f)
        return true;
    return (d[0] * look[0] + d[1] * look[1] + d[2] * look[2]) / (distance * lookLength) > kInViewCosine;
}

/** GetSpawnPositionAheadOfTarget at `distance`, and with cop tactics improved further on while the camera would see
 * the spot. */
bool SpawnOutOfView(char *self, void *pursuit, float *position, float *direction, float distance)
{
    if (!nfs::aituning::CopsImproved())
        return AICopManagerGetSpawnPositionAheadOfTarget(self, nullptr, pursuit, position, direction, distance);
    for (float at = distance; at <= kOutOfViewFarthest; at += kOutOfViewStep)
    {
        if (!AICopManagerGetSpawnPositionAheadOfTarget(self, nullptr, pursuit, position, direction, at))
            return false;
        if (!InCameraView(position))
            return true;
    }
    nfs::Log("cops: no spawn out of view up to %d m", static_cast<int>(kOutOfViewFarthest));
    return false;
}

float Random01()
{
    void *random = NFS_ORIGINAL(0x6ed200, void *(__cdecl *)())();
    return static_cast<float>(NFS_ORIGINAL(0x627320, int(__thiscall *)(void *))(random)) * 1.5258789e-05f;
}
} // namespace

bool __fastcall AICopManagerSpawnPursuitIVehicle(char *self, void *, void *pursuit, void *vehicle)
{
    const bool roaming = At<int>(static_cast<char *>(*kRaceStatus), 0x1960) == 1;
    char *ai = Virtual<char *>(vehicle, 0xac);
    {
        void *random = NFS_ORIGINAL(0x6ed200, void *(__cdecl *)())();
        NFS_ORIGINAL(0x627320, int(__thiscall *)(void *))(random);
    }
    bool spawned = false;
    if (Virtual<int>(pursuit, 0x114) == 2)
    {
        unsigned int location = 0;
        unsigned int reversed = 0;
        unsigned int lane = 0;
        if (!NFS_ORIGINAL(0x42fa40, bool(__thiscall *)(void *, unsigned int *, unsigned int *, unsigned int *))(
                self + 0x48, &location, &reversed, &lane))
            return false;
        if (!Find(ai, kPursuitAiHandle))
            return false;
        Virtual<void, unsigned int, unsigned int, unsigned int>(ai, 0x54, location, reversed & 0xff, lane);
        spawned = true;
    }
    else if (roaming)
    {
        float position[3];
        float direction[3];
        if (!SpawnOutOfView(self, pursuit, position, direction, *kLeaderSupportDistance + kChaseSpawnExtra))
            return false;
        if (!Virtual<bool, const float *, const float *>(ai, 0x5c, position, direction))
            return false;
        spawned = true;
    }
    else
    {
        char *target = Virtual<char *>(pursuit, 8);
        void *chased = At<void *>(target, 0x1c);
        void *chasedAi = chased ? Find(chased, kVehicleAiHandle) : nullptr;
        void *chasedPursuit = Virtual<void *>(chasedAi, 0xa0);
        bool retry = chasedPursuit && Virtual<int>(chasedPursuit, 0x114) == 2;
        float heading[3];
        if (!At<unsigned char>(static_cast<char *>(*kGameManager), 0x2b4))
        {
            const float *at = Virtual<const float *>(chasedAi, 0xa8);
            const float from[3] = {at[0], at[1], at[2]};
            const float *goal = reinterpret_cast<const float *>(target + 0x10);
            heading[0] = goal[0] - from[0];
            heading[1] = goal[1] - from[1];
            heading[2] = goal[2] - from[2];
            const float distance = Length(heading);
            const float swing = Random01() > 0.5f ? -kSpawnSwing : kSpawnSwing;
            Turn(swing, heading, heading);
            RoadNav nav;
            NFS_ORIGINAL(0x7658a0, void(__thiscall *)(void *, int))(nav.bytes, 0);
            *reinterpret_cast<int *>(nav.bytes + kNavMode) = 2;
            if (!retry)
            {
                const char *segment = Virtual<const char *>(chasedAi, 0xb4);
                retry = segment && SegmentRetry(*reinterpret_cast<const short *>(segment + 0x8e));
            }
            nav.Byte(kNavRetry) = retry;
            if (nav.InitAt(from, heading))
            {
                nav.Byte(kNavRetry) = SegmentRetry(*reinterpret_cast<const short *>(nav.bytes + 0x8e));
                NFS_ORIGINAL(0x77bba0, void(__thiscall *)(void *, float, const float *, int))(nav.bytes, distance, heading,
                                                                                             0);
                const float *found = reinterpret_cast<const float *>(nav.bytes + kNavPosition);
                const float position[3] = {found[0], found[1], found[2]};
                if (CheckSpawnPosition(self, position, true))
                {
                    NFS_ORIGINAL(0x77bac0, void(__thiscall *)(void *))(nav.bytes);
                    spawned = Virtual<bool, void *>(ai, 0x58, nav.bytes);
                }
            }
        }
        if (!spawned)
        {
            char *chasing = Virtual<char *>(chasedPursuit, 8);
            const float *goal = reinterpret_cast<const float *>(chasing + 0x10);
            const float from[3] = {goal[0], goal[1], goal[2]};
            const float *velocity = kZeroVectorFloat;
            if (void *simable = At<void *>(chasing, 0x1c))
                velocity = Virtual<const float *>(Virtual<void *>(simable, 0x54), 0x24);
            heading[0] = velocity[0];
            heading[1] = 0.0f;
            heading[2] = velocity[2];
            void *facing = nullptr;
            if (Length(heading) < 1.0f &&
                NFS_ORIGINAL(0x41ce30, bool(__thiscall *)(void *, void **))(chasing, &facing))
                Virtual<void, float *>(facing, 0x34, heading);
            else
                Normalize(heading);
            const float ahead[3] = {heading[0] * kFallbackAhead + from[0], heading[1] * kFallbackAhead + from[1],
                                    heading[2] * kFallbackAhead + from[2]};
            float scatter[3];
            const float *unit = NFS_ORIGINAL(0x406340, const float *(__cdecl *)(float *))(scatter);
            const float position[3] = {ahead[0] + unit[0] * kFallbackScatter, unit[1] * kFallbackScatter + ahead[1],
                                       unit[2] * kFallbackScatter + ahead[2]};
            RoadNav nav;
            NFS_ORIGINAL(0x7658a0, void(__thiscall *)(void *, int))(nav.bytes, 0);
            *reinterpret_cast<int *>(nav.bytes + kNavMode) = 2;
            nav.Byte(kNavRetry) = retry;
            if (!nav.InitAt(position, heading))
                return false;
            if (!CheckSpawnPosition(self, reinterpret_cast<const float *>(nav.bytes + kNavPosition), true))
                return false;
            if (!Virtual<bool, void *>(ai, 0x58, nav.bytes))
                return false;
            spawned = true;
        }
    }
    Virtual<void>(vehicle, 0x80);
    Virtual<void>(ai, 0x88);
    Virtual<void, void *>(pursuit, 0x78, vehicle);
    ++At<int>(self, 0x94);
    return true;
}

namespace
{
const unsigned int kPatrolCountKey = 0x24f7a1bc;
const float kPatrolLeaveAhead = 75.0f;
const float kPatrolKeepHelicopter = 600.0f;
const float kPatrolKeepCar = 375.0f;
const float kLeastFleeTime = 3.0f;

bool GetSpawnLocation(char *self, unsigned int *location, unsigned int *reversed, unsigned int *lane)
{
    *location = 0;
    *reversed = 0;
    *lane = 0;
    return NFS_ORIGINAL(0x42fa40, bool(__thiscall *)(void *, unsigned int *, unsigned int *, unsigned int *))(
        self + 0x48, location, reversed, lane);
}
} // namespace

bool __fastcall AICopManagerSpawnPatrolCar(char *self)
{
    void *vehicle = AICopManagerGetAvailableCopVehicleByClass(self, nullptr, *kCopCarClass, true);
    if (!vehicle)
        return false;
    unsigned int location;
    unsigned int reversed;
    unsigned int lane;
    if (!GetSpawnLocation(self, &location, &reversed, &lane))
        return false;
    void *ai = Virtual<void *>(vehicle, 0xac);
    void *pursuitAi = Find(ai, kPursuitAiHandle);
    if (!pursuitAi)
        return false;
    Virtual<void>(vehicle, 0x80);
    Virtual<void, unsigned int, unsigned int, unsigned int>(ai, 0x54, location, reversed & 0xff, lane);
    Virtual<void>(ai, 0x88);
    Virtual<void>(pursuitAi, 4);
    ++At<int>(self, 0x94);
    return true;
}

void __fastcall AICopManagerUpdatePatrols(char *self)
{
    int wanted = 1;
    if (char *levels = reinterpret_cast<char *>(GetGlobalPursuitLevelAttrib()))
    {
        const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
            levels, kPatrolCountKey, 0);
        if (!value)
            value = NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(4);
        wanted = *static_cast<const int *>(value);
    }
    const unsigned int mode = At<unsigned int>(self, 0x12c);
    if (mode > 1)
        wanted = 0;
    else if (mode == 1)
    {
        wanted = 0;
        char *race = static_cast<char *>(*kRaceStatus);
        if (race && At<void *>(race, 0x1968) &&
            NFS_ORIGINAL(0x5facd0, int(__thiscall *)(void *))(At<void *>(race, 0x1968)) > 0)
            wanted = 1;
    }
    if (At<int>(self, 0x70))
        wanted = 0;
    static const unsigned int patrolGoal = stringhash32(reinterpret_cast<const char *>(0x891540));
    int patrolling = 0;
    for (void **it = Begin(self + 0xc8); it != End(self + 0xc8); ++it)
    {
        if (void *ai = Virtual<void *>(*it, 0xac))
        {
            if (*Virtual<const unsigned int *>(ai, 0xc0) == patrolGoal)
                ++patrolling;
        }
    }
    if (wanted > patrolling)
        AICopManagerSpawnPatrolCar(self);
    for (void **it = Begin(self + 0xc8); it != End(self + 0xc8); ++it)
    {
        void *cop = *it;
        if (!Virtual<bool>(cop, 0x88))
            continue;
        void *ai = Virtual<void *>(cop, 0xac);
        void *pursuitAi = Find(ai, kPursuitAiHandle);
        if (!pursuitAi)
            continue;
        const float *at = Virtual<const float *>(Virtual<void *>(cop, 4), 0x60);
        const float position[3] = {at[0], at[1], at[2]};
        const bool respawnable = NFS_ORIGINAL(0x419e20, bool(__thiscall *)(void *, const float *, int))(self + 0x48,
                                                                                                    position, 0);
        const bool hidden = Virtual<bool, unsigned int>(Virtual<void *>(cop, 0xac), 0x90, respawnable);
        bool despawn = hidden && respawnable;
        const bool helicopter = IsHelicopter(cop);
        if (Virtual<bool>(cop, 0x7c))
        {
            if (hidden && Virtual<float>(cop, 0x60) > kLeastFleeTime)
                despawn = true;
        }
        else if (despawn)
        {
            if (void *pursuit = Virtual<void *>(ai, 0xa0))
            {
                const float *goal = reinterpret_cast<const float *>(Virtual<char *>(pursuit, 8) + 0x10);
                const float chased[3] = {goal[0], goal[1], goal[2]};
                char *target = Virtual<char *>(pursuit, 8);
                float heading[3];
                void *simable = At<void *>(target, 0x1c);
                if (simable && Virtual<float>(Virtual<void *>(simable, 0x54), 0x30) > 1.0f)
                {
                    void *moving = At<void *>(Virtual<char *>(pursuit, 8), 0x1c);
                    const float *velocity = moving ? Virtual<const float *>(Virtual<void *>(moving, 0x54), 0x24)
                                                   : kZeroVectorFloat;
                    heading[0] = velocity[0];
                    heading[1] = velocity[1];
                    heading[2] = velocity[2];
                }
                else
                {
                    const float *facing = reinterpret_cast<const float *>(Virtual<char *>(pursuit, 8) + 0x20);
                    heading[0] = facing[0];
                    heading[1] = facing[1];
                    heading[2] = facing[2];
                }
                Normalize(heading);
                const float ahead[3] = {heading[0] * kPatrolLeaveAhead + chased[0],
                                        heading[1] * kPatrolLeaveAhead + chased[1],
                                        heading[2] * kPatrolLeaveAhead + chased[2]};
                const float dx = position[0] - ahead[0];
                const float dy = position[1] - ahead[1];
                const float dz = position[2] - ahead[2];
                const float distance = sqrtf(dz * dz + dy * dy + dx * dx);
                if (distance < (helicopter ? kPatrolKeepHelicopter : kPatrolKeepCar))
                    despawn = false;
            }
        }
        const bool offWorld = helicopter ? false : Virtual<bool>(cop, 0xc4);
        if (Virtual<bool, int>(self + 0x54, 4, 1) && !despawn)
        {
            if (offWorld || Virtual<bool>(cop, 0x7c) || !Virtual<void *>(pursuitAi, 0x2c))
                continue;
            At<void *>(self, 0xc0) = cop;
            return;
        }
        AICopManagerRemoveActiveCopVehicle(self, nullptr, cop);
        void *simable = Virtual<void *>(cop, 4);
        const unsigned int port = stringhash32(reinterpret_cast<const char *>(0x890fc0));
        void *handle = Virtual<void *>(simable, 0x34);
        unsigned int kind;
        MUnspawnCopGetKind(&kind);
        unsigned int message[6] = {kind, port, 0x18, 0, reinterpret_cast<unsigned int>(handle), 6};
        Deliver(message);
    }
}

void __fastcall AICopManagerOnAttached(char *self, void *, char *attachable)
{
    if (void *vehicle = Find(attachable, kVehicleHandle))
    {
        char *cops = self + 0x8c;
        const unsigned int size = At<unsigned int>(cops, 0xc);
        if (size >= At<unsigned int>(cops, 8))
        {
            const unsigned int capacity = Virtual<unsigned int, unsigned int>(cops, 0xc, size + 1);
            NFS_ORIGINAL(0x41ae90, void(__thiscall *)(void *, unsigned int))(cops, capacity);
        }
        void **slot = At<void **>(cops, 4) + At<unsigned int>(cops, 0xc);
        if (slot)
            *slot = vehicle;
        ++At<unsigned int>(cops, 0xc);
        if (void *watcher = At<void *>(self, 0x108))
            Virtual<void, void *>(watcher, 8, vehicle);
        return;
    }
    if (void *activity = Find(attachable, kPursuitActivityHandle))
    {
        PushBack(self + 0xec, 0x41c4e0, 0x434240, &activity);
        return;
    }
    if (void *roadBlock = Find(attachable, kRoadBlockHandle))
        NFS_ORIGINAL(0x438610, void(__thiscall *)(void *, void *const *))(self + 0xf8, &roadBlock);
}

void __fastcall AICopManagerMessageSetAutoSpawnMode(char *, void *, unsigned int);
void __fastcall AICopManagerMessageSetCopsEnabled(char *, void *, const char *message);
void __fastcall AICopManagerMessageBreakerStopCops(char *self, void *, const char *message);
void __fastcall AICopManagerMessageForcePursuitStart(char *self, void *, const char *message);

namespace
{
float *const kMostChaseDistance = reinterpret_cast<float *>(0x90d640);
unsigned int *const kCopSlots = reinterpret_cast<unsigned int *>(0x90d520);
const int kCopSlotCount = 0x32;
void **const kCopManagerInstance = reinterpret_cast<void **>(0x90d5f4);

void *CreateHandler(unsigned int creator, char *self, const void *member)
{
    const unsigned int port = stringhash32(reinterpret_cast<const char *>(0x891664));
    return NFS_ORIGINAL(creator, void *(__cdecl *)(void *, const void *, int, unsigned int, int))(self, member, 0, port, 0);
}

void *BuyNode(char *list, unsigned int function)
{
    return NFS_ORIGINAL(function, void *(__thiscall *)(void *))(list);
}
} // namespace

void *__fastcall AICopManagerConstruct(char *self, void *, unsigned int, unsigned int, unsigned int, unsigned int)
{
    NFS_ORIGINAL(0x6f94d0, void *(__thiscall *)(void *, unsigned int))(self, 2);
    const float most = *kHeavySupportDistance;
    At<unsigned int>(self, 0x48) = 0x890ec0;
    At<float>(self, 0x4c) = *kLeaderSupportDistance;
    At<float>(self, 0x50) = most;
    *kMostChaseDistance = most > *kMostChaseDistance ? most : *kMostChaseDistance;
    for (int i = 0; i < kCopSlotCount; ++i)
        kCopSlots[i] = 0xffffffff;
    char *list = self ? self + 0x10 : nullptr;
    char *manager = self + 0x54;
    At<unsigned int>(manager, 0) = 0x890970;
    At<char *>(manager, 4) = list;
    NFS_ORIGINAL(0x5d7930, void(__thiscall *)(void *, void *, void *))(list, reinterpret_cast<void *>(0x404d80), manager);
    *reinterpret_cast<char **>(0x92c67c) = manager;
    At<unsigned int>(manager, 0) = 0x890f50;
    NFS_ORIGINAL(0x42e340, void *(__thiscall *)(void *, void *))(self + 0x5c, list);
    At<unsigned int>(self, 0x5c) = 0x8918f0;
    At<unsigned int>(self, 0) = 0x891978;
    At<unsigned int>(self, 8) = 0x891970;
    At<unsigned int>(self, 0x2c) = 0x89195c;
    At<unsigned int>(self, 0x38) = 0x891940;
    At<unsigned int>(self, 0x48) = 0x89193c;
    At<unsigned int>(manager, 0) = 0x891900;
    At<void *>(self, 0x6c) = BuyNode(self + 0x68, 0x41b990);
    At<int>(self, 0x70) = 0;
    At<void *>(self, 0x78) = BuyNode(self + 0x74, 0x41b9e0);
    At<int>(self, 0x7c) = 0;
    At<int>(self, 0x84) = 8;
    At<int>(self, 0x88) = 1;
    At<int>(self, 0x8c) = 8;
    At<int>(self, 0x90) = 2;
    At<int>(self, 0x94) = 0;
    At<int>(self, 0x98) = 8;
    At<int>(self, 0x9c) = 0;
    At<int>(self, 0xa0) = 1;
    At<int>(self, 0xa4) = 0;
    At<int>(self, 0xa8) = 0;
    At<float>(self, 0xac) = 60.0f;
    At<int>(self, 0xb0) = 0;
    At<unsigned char>(self, 0xb4) = 0;
    At<int>(self, 0xb8) = 0;
    At<int>(self, 0xbc) = 0;
    At<int>(self, 0xc0) = 0;
    char *cops = self + 0xc4;
    At<int>(cops, 4) = 0;
    At<int>(cops, 8) = 0;
    At<int>(cops, 0xc) = 0;
    At<unsigned int>(cops, 0) = 0x8918d8;
    At<void *>(self, 0x128) = BuyNode(self + 0x124, 0x41ba30);
    At<int>(self, 0x12c) = 0;
    At<void *>(self, 0x134) = BuyNode(self + 0x130, 0x41ba80);
    At<int>(self, 0x138) = 0;
    At<void *>(self, 0x140) = nullptr;
    const unsigned int activityKind = stringhash32(reinterpret_cast<const char *>(0x890fc0));
    void *activity = nullptr;
    for (unsigned int *factory = static_cast<unsigned int *>(*kActivityFactories); factory;
         factory = reinterpret_cast<unsigned int *>(factory[2]))
    {
        if (factory[0] == activityKind)
        {
            const ActivityParams params = {};
            activity = reinterpret_cast<void *(__cdecl *)(ActivityParams)>(factory[1])(params);
            break;
        }
    }
    At<void *>(self, 0x140) = activity;
    if (activity && At<void *>(self, 0x44))
        NFS_ORIGINAL(0x6fbf80, void(__thiscall *)(void *, void *))(At<void *>(self, 0x44), activity);
    At<void *>(self, 0x14c) = CreateHandler(0x41cbf0, self, reinterpret_cast<const void *>(&AICopManagerMessageSetAutoSpawnMode));
    At<void *>(self, 0x150) = CreateHandler(0x41cc80, self, reinterpret_cast<const void *>(&AICopManagerMessageSetCopsEnabled));
    At<void *>(self, 0x154) = CreateHandler(0x41cd10, self, reinterpret_cast<const void *>(&AICopManagerMessageBreakerStopCops));
    At<void *>(self, 0x158) = CreateHandler(0x41cda0, self, reinterpret_cast<const void *>(&AICopManagerMessageForcePursuitStart));
    if (At<int>(cops, 0xc) > 0)
        At<int>(cops, 0xc) = 0;
    if (void *begin = At<void *>(cops, 4))
    {
        Virtual<void, void *, unsigned int>(cops, 8, begin, At<unsigned int>(cops, 8));
        At<void *>(cops, 4) = nullptr;
        At<void *>(cops, 8) = nullptr;
        At<int>(cops, 0xc) = 0;
    }
    NFS_ORIGINAL(0x41b0d0, void(__thiscall *)(void *))(self + 0x124);
    NFS_ORIGINAL(0x41ae90, void(__thiscall *)(void *, unsigned int))(cops, 0x14);
    unsigned int task = stringhash32(reinterpret_cast<const char *>(0x891664));
    At<void *>(self, 0x80) = NFS_ORIGINAL(0x6ee9b0, void *(__thiscall *)(void *, const unsigned int *, float, float, int))(
        self, &task, 0.5f, 0.0f, 0);
    NFS_ORIGINAL(0x6e8e90, void(__cdecl *)(void *, const char *))(At<void *>(self, 0x80),
                                                                  reinterpret_cast<const char *>(0x891664));
    if (At<int>(self, 0x8c) < At<int>(self, 0x84))
        At<int>(self, 0x84) = At<int>(self, 0x8c);
    const int cars = *reinterpret_cast<const int *>(0x926088);
    const int helicopters = *reinterpret_cast<const int *>(0x92608c);
    if (cars > 0 || helicopters > 0)
    {
        At<int>(self, 0x98) = cars < At<int>(self, 0x84) ? cars : At<int>(self, 0x84);
        At<int>(self, 0xa0) = helicopters < At<int>(self, 0x88) ? helicopters : At<int>(self, 0x88);
    }
    *kCopsDisabled = *reinterpret_cast<const int *>(0x926064) && *reinterpret_cast<const int *>(0x8f86c0) ? 1 : 0;
    void *queue = NFS_ORIGINAL(0x5d29d0, void *(__thiscall *)(void *, unsigned int, const char *))(
        reinterpret_cast<void *>(0x925b30), 0x28c, nullptr);
    if (queue)
        queue = NFS_ORIGINAL(0x648db0, void *(__thiscall *)(void *, int, unsigned int, const char *, int))(
            queue, 0, 0x98c7a2f5, reinterpret_cast<const char *>(0x891664), 0);
    At<void *>(self, 0x13c) = queue;
    At<int>(self, 0x144) = 0;
    *kCopManagerInstance = self;
    return self;
}

void *__cdecl AICopManagerFactory(unsigned int type, unsigned int name, unsigned int data, unsigned int extra)
{
    void *memory = NFS_ORIGINAL(0x5d29d0, void *(__thiscall *)(void *, unsigned int, const char *))(
        reinterpret_cast<void *>(0x925b30), 0x15c, nullptr);
    if (!memory)
        return nullptr;
    char *made = static_cast<char *>(AICopManagerConstruct(static_cast<char *>(memory), nullptr, type, name, data, extra));
    return made ? made + 0x2c : nullptr;
}

namespace
{
const int kMostListed = 256;

int CollectList(char *head, void **out)
{
    int count = 0;
    for (char *node = At<char *>(head, 0); node != head && count < kMostListed; node = At<char *>(node, 0))
        out[count++] = At<void *>(node, 8);
    return count;
}
} // namespace

void __fastcall AICopManagerResetCopsForRestart(char *self, void *, bool forget)
{
    At<float>(self, 0x58) = kDefaultLockout;
    char *race = static_cast<char *>(*kRaceStatus);
    if (race && At<void *>(race, 0x1968) && NFS_ORIGINAL(0x5faa20, int(__thiscall *)(void *))(At<void *>(race, 0x1968)) == 8)
        At<float>(self, 0x58) = 0.0f;
    if (At<void *>(self, 0xec))
    {
        char *sound = static_cast<char *>(*kSoundAi);
        race = static_cast<char *>(*kRaceStatus);
        bool roaming = race && !At<void *>(race, 0x1960);
        if (!roaming)
        {
            void *races = *kRaceDatabase;
            roaming = races && !NFS_ORIGINAL(0x5dca00, void *(__thiscall *)(void *))(races);
        }
        if (roaming)
        {
            alignas(8) unsigned char levels[32];
            NFS_ORIGINAL(0x402db0, void *(__thiscall *)(void *, const void *))(levels, sound + 0x190);
            if (*reinterpret_cast<void **>(levels + 4))
            {
                const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
                    levels, kLockoutTime, 0);
                if (!value)
                    value = NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(4);
                At<unsigned int>(self, 0x58) = *static_cast<const unsigned int *>(value);
            }
            NFS_ORIGINAL(0x45a430, void(__thiscall *)(void *))(levels);
        }
        NFS_ORIGINAL(0x71cb10, void(__thiscall *)(void *, int))(sound, 1);
        NFS_ORIGINAL(0x70e570, void(__cdecl *)())();
        if (void *speech = NFS_ORIGINAL(0x6ff2d0, void *(__cdecl *)(int))(1))
            Virtual<void>(speech, 0x48);
    }
    void *roadBlocks[kMostListed];
    const int roadBlockCount = CollectList(At<char *>(self, 0xe0), roadBlocks);
    void *pursuits[kMostListed];
    char *attachments = self - 0x28;
    for (int i = 0; i < roadBlockCount; ++i)
    {
        void *roadBlock = roadBlocks[i];
        Virtual<void>(roadBlock, 0x10);
        if (void *owner = Find(roadBlock, kSmackableOwnerHandle))
            Virtual<void>(owner, 4);
        Virtual<void, void *>(attachments, 0xc, roadBlock);
    }
    const int pursuitCount = CollectList(At<char *>(self, 0xd4), pursuits);
    char *cops = self + 0x70;
    for (int i = 0; i < pursuitCount; ++i)
    {
        void *pursuit = pursuits[i];
        Virtual<void, void *>(attachments, 0xc, pursuit);
        if (Virtual<void *>(pursuit, 8) && At<void *>(Virtual<char *>(pursuit, 8), 0x1c))
        {
            void *chased = At<void *>(Virtual<char *>(pursuit, 8), 0x1c);
            if (void *attachable = Find(chased, kAttachableHandle))
            {
                if (void *perp = Find(At<void *>(Virtual<char *>(pursuit, 8), 0x1c), kPerpHandle))
                    Virtual<void>(perp, 0x48);
                Virtual<void, void *>(attachable, 8, pursuit);
            }
        }
        for (void **it = At<void **>(cops, 4); it != At<void **>(cops, 4) + At<int>(cops, 0xc); ++it)
        {
            void *cop = *it;
            char *copPursuit = Virtual<char *>(Virtual<void *>(cop, 0xac), 0xa0);
            if (copPursuit && At<void *>(copPursuit, 4) == At<void *>(pursuit, 4))
                Virtual<void, void *>(pursuit, 0x7c, cop);
        }
        if (void *owner = Find(pursuit, kSmackableOwnerHandle))
            Virtual<void>(owner, 4);
    }
    for (void **it = At<void **>(cops, 4); it != At<void **>(cops, 4) + At<int>(cops, 0xc); ++it)
    {
        void *cop = *it;
        if (!Virtual<bool>(cop, 0x88))
            continue;
        if (IsHelicopter(cop))
            --At<int>(self, 0x48);
        else
            --At<int>(self, 0x40);
        Virtual<void>(Virtual<void *>(cop, 0xac), 0x8c);
        void *simable = Virtual<void *>(cop, 4);
        const unsigned int port = stringhash32(reinterpret_cast<const char *>(0x890fc0));
        void *handle = Virtual<void *>(simable, 0x34);
        unsigned int kind;
        MUnspawnCopGetKind(&kind);
        unsigned int message[6] = {kind, port, 0x18, 0, reinterpret_cast<unsigned int>(handle), 0};
        Deliver(message);
    }
    if (forget)
    {
        void **end = At<void **>(cops, 4) + At<int>(cops, 0xc);
        for (void **it = At<void **>(cops, 4); it != end; ++it)
        {
            if (void *owner = Find(*it, reinterpret_cast<void *>(0x402830)))
                Virtual<void>(owner, 8);
        }
        if (At<int>(cops, 0xc) > 0)
            At<int>(cops, 0xc) = 0;
        if (void *begin = At<void *>(cops, 4))
        {
            Virtual<void, void *, unsigned int>(cops, 8, begin, At<unsigned int>(cops, 8));
            At<void *>(cops, 4) = nullptr;
            At<void *>(cops, 8) = nullptr;
            At<int>(cops, 0xc) = 0;
        }
    }
    At<unsigned char>(self, 0x60) = 0;
}

namespace
{
const int kMostRoadBlockCops = 40;
const int kRoadBlockCopsByWidth[7] = {2, 2, 3, 3, 4, 4, 5};

void *MakeActivityOf(unsigned int kind)
{
    for (unsigned int *factory = static_cast<unsigned int *>(*kActivityFactories); factory;
         factory = reinterpret_cast<unsigned int *>(factory[2]))
    {
        if (factory[0] == kind)
        {
            const ActivityParams params = {};
            return reinterpret_cast<void *(__cdecl *)(ActivityParams)>(factory[1])(params);
        }
    }
    return nullptr;
}

float SquaredGap(const float *a, const float *b)
{
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    return dz * dz + dy * dy + dx * dx;
}

const void *Attribute(void *instance, unsigned int key)
{
    const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
        instance, key, 0);
    return value ? value : NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(4);
}

const float *GenerateMatrix(float *out, const float *forward, const float *up)
{
    return NFS_ORIGINAL(0x6f1220, const float *(__cdecl *)(float *, const float *, const float *))(out, forward, up);
}

/** How far (m) the ground under a roadblock piece may be from the road's height there. */
const float kRoadBlockGroundReach = 4.0f;

bool RoadBlockNeedsGround()
{
    static const bool forced = nfs::config::Int("Debug", "RoadBlockFix", 0) != 0;
    return forced || nfs::vanillafixes::On();
}

/** Whether every piece of the roadblock `setup` would stand on the world's ground: the original lays the pieces from the
 * road's edge without looking, and a piece past the edge where the world has no collision is off the map. */
bool RoadBlockOnGround(const float *setup, const float *centre, float stretch, const float *forward)
{
    alignas(16) float frame[16];
    const float *made = GenerateMatrix(frame, forward, nullptr);
    alignas(16) float orientation[16];
    for (int i = 0; i < 16; ++i)
        orientation[i] = made[i];
    const float *piece = setup + 2;
    for (int i = 0; i < 6; ++i, piece += 4)
    {
        const int kind = *reinterpret_cast<const int *>(piece);
        if (!kind)
            break;
        if (kind < 1 || kind > 3)
            continue;
        float local[3] = {stretch * piece[1], 0.0f, piece[2]};
        NFS_ORIGINAL(0x401670, void(__cdecl *)(const float *, const float *, float *))(local, orientation, local);
        const float spot[3] = {local[0] + centre[0], local[1] + centre[1], local[2] + centre[2]};
        unsigned int manager[2] = {0, 3};
        float height = 0.0f;
        float up[3];
        if (!NFS_ORIGINAL(0x789870, bool(__thiscall *)(void *, const float *, float *, float *))(manager, spot, &height,
                                                                                              up) ||
            fabsf(height - spot[1]) > kRoadBlockGroundReach)
        {
            nfs::Log("roadblock: not laid, piece %d at %d %d %d has no ground", i, static_cast<int>(spot[2]),
                     static_cast<int>(-spot[0]), static_cast<int>(spot[1]));
            return false;
        }
    }
    return true;
}
} // namespace

bool __fastcall AICopManagerCreateRoadBlock(char *self, void *, void *pursuit, int cops, void *helicopter, char *list)
{
    if (list && At<int>(list, 0xc) > kMostRoadBlockCops - 8)
        return NFS_ORIGINAL(0x43dd30, bool(__thiscall *)(char *, void *, int, void *, void *))(self, pursuit, cops,
                                                                                            helicopter, list);
    char *target = Virtual<char *>(pursuit, 8);
    void *simable = At<void *>(target, 0x1c);
    if (!simable)
        return false;
    void *perp = Find(simable, kPerpHandle);
    if (!perp)
        return false;
    void *levels = Virtual<void *>(perp, 0x10);
    if (!levels)
        return false;
    const float heading[3] = {At<float>(target, 0x20), At<float>(target, 0x24), At<float>(target, 0x28)};
    const float from[3] = {At<float>(target, 0x10), At<float>(target, 0x14), At<float>(target, 0x18)};
    RoadNav nav;
    NFS_ORIGINAL(0x76e2d0, void(__thiscall *)(void *, int))(nav.bytes, 1);
    NFS_ORIGINAL(0x7658a0, void(__thiscall *)(void *, int))(nav.bytes, 0);
    *reinterpret_cast<int *>(nav.bytes + kNavMode) = 2;
    if (!nav.InitAt(from, heading))
        return false;
    NFS_ORIGINAL(0x77bba0, void(__thiscall *)(void *, float, const float *, int))(
        nav.bytes, 250.0f, reinterpret_cast<const float *>(0x8a2fb4), 0);
    const short index = *reinterpret_cast<const short *>(nav.bytes + 0x8e);
    const signed char way = *reinterpret_cast<const signed char *>(nav.bytes + 0x8c);
    const char *segment = *reinterpret_cast<char **>(0x9b38c0) + index * 0x16;
    if (!segment || (segment[0xa] & 2) || (segment[0xa] & 1))
        return false;
    const float length =
        static_cast<float>(*reinterpret_cast<const unsigned short *>(segment + 4)) *
        *reinterpret_cast<const float *>(0x8eb34c);
    if (length < 40.0f)
        return false;
    const char *profile = NFS_ORIGINAL(0x765410, const char *(__thiscall *)(void *, const void *, int))(
        *reinterpret_cast<void **>(0x9b38b4), segment, way);
    if (!profile || !*profile)
        return false;
    const float *leftEdge = reinterpret_cast<const float *>(nav.bytes + 0xb0);
    const float *rightEdge = reinterpret_cast<const float *>(nav.bytes + 0xa4);
    const float edgeA[3] = {leftEdge[0], leftEdge[1], leftEdge[2]};
    const float edgeB[3] = {rightEdge[0], rightEdge[1], rightEdge[2]};
    const float width = NFS_ORIGINAL(0x4100a0, float(__cdecl *)(const float *, const float *))(edgeB, edgeA);
    const float span = width + 1.0f;
    int band = static_cast<int>(span * 0.25f);
    if (band > 6)
        band = 6;
    const int needed = kRoadBlockCopsByWidth[band] + 1;
    void *vehicles[kMostRoadBlockCops];
    int count = 0;
    if (list)
    {
        for (void **it = At<void **>(list, 4); it != At<void **>(list, 4) + At<int>(list, 0xc); ++it)
        {
            void *vehicle = *it;
            void *pursuitAi = nullptr;
            if (NFS_ORIGINAL(0x40e100, bool(__thiscall *)(void *, void **))(vehicle, &pursuitAi))
            {
                unsigned int none;
                NFS_ORIGINAL(0x4023c0, void *(__thiscall *)(void *, const char *))(&none, nullptr);
                Virtual<void, unsigned int>(pursuitAi, 0x84, none);
            }
            Virtual<void>(vehicle, 0x80);
            vehicles[count++] = vehicle;
        }
    }
    else
    {
        for (int i = needed; i > 0; --i)
        {
            if (void *vehicle = AICopManagerGetAvailableCopVehicleByClass(self, nullptr, *kCopCarClass, true))
            {
                Virtual<void>(vehicle, 0x80);
                vehicles[count++] = vehicle;
            }
        }
    }
    for (int i = 0; i < count; ++i)
    {
        Virtual<void>(Virtual<void *>(vehicles[i], 0xac), 0x8c);
        void *vehicleSimable = Virtual<void *>(vehicles[i], 4);
        const unsigned int port = stringhash32(reinterpret_cast<const char *>(0x890fc0));
        unsigned int message[8];
        MUnspawnCopConstruct(message, nullptr, Virtual<void *>(vehicleSimable, 0x34), 3);
        message[1] = port;
        Deliver(message);
    }
    for (int i = needed - count; i > 0; --i)
    {
        if (void *vehicle = AICopManagerGetActiveCopVehicleFromOutOfView(self, nullptr, *kCopCarClass))
            vehicles[count++] = vehicle;
    }
    if (count < needed - 1)
        return false;
    const float chance = *static_cast<const float *>(Attribute(levels, 0x5a318af6));
    const int roll = NFS_ORIGINAL(0x627320, int(__thiscall *)(void *))(NFS_ORIGINAL(0x6ed200, void *(__cdecl *)())());
    bool spikes = static_cast<float>(roll) * 100.0f * *reinterpret_cast<const float *>(0x890980) < chance;
    if (Virtual<int>(pursuit, 0x114) == 2)
        spikes = false;
    const float *setup = static_cast<const float *>(PickRoadblockSetupPort(span, count, spikes));
    if (!setup)
        return false;
    const bool nearB = !(SquaredGap(edgeB, from) > SquaredGap(edgeA, from));
    const float *closer = nearB ? edgeB : edgeA;
    const float *further = nearB ? edgeA : edgeB;
    const float across[3] = {further[0] - closer[0], further[1] - closer[1], further[2] - closer[2]};
    const float part = setup[0] / width * 0.5f;
    const float centre[3] = {across[0] * part + closer[0], across[1] * part + closer[1], across[2] * part + closer[2]};
    const float ratio = span / setup[0];
    float stretch = 1.0f;
    if (ratio > 1.0f)
        stretch = *reinterpret_cast<const float *>(0x892ed8) >= ratio ? ratio : 1.14f;
    if (RoadBlockNeedsGround() && !RoadBlockOnGround(setup, centre, stretch, reinterpret_cast<const float *>(nav.bytes + 0xbc)))
        return false;
    void *activity = MakeActivityOf(stringhash32(reinterpret_cast<const char *>(0x892ecc)));
    void *roadBlock = Find(activity, reinterpret_cast<void *>(0x4038d0));
    Virtual<void, void *>(self + 0x2c, 8, roadBlock);
    Virtual<void, void *>(pursuit, 0x80, roadBlock);
    Virtual<void, void *>(roadBlock, 0x18, pursuit);
    const float *forward = reinterpret_cast<const float *>(nav.bytes + 0xbc);
    Virtual<void, const float *, const float *>(roadBlock, 0x40, centre, forward);
    alignas(16) float frame[16];
    const float *made = GenerateMatrix(frame, forward, nullptr);
    alignas(16) float orientation[16];
    for (int i = 0; i < 16; ++i)
        orientation[i] = made[i];
    void **nextVehicle = vehicles;
    const float *piece = setup + 2;
    for (int i = 0; i < 6; ++i, piece += 4)
    {
        const int kind = *reinterpret_cast<const int *>(piece);
        if (!kind)
            break;
        float local[3] = {stretch * piece[1], 0.0f, piece[2]};
        NFS_ORIGINAL(0x401670, void(__cdecl *)(const float *, const float *, float *))(local, orientation, local);
        const float angle = piece[3] * *reinterpret_cast<const float *>(0x890510);
        float c;
        float s;
        __asm {
            fld angle
            fsincos
            fstp c
            fstp s
        }
        const float facing[3] = {forward[0] * c - forward[2] * s, forward[1], forward[2] * c + forward[0] * s};
        const float spot[3] = {local[0] + centre[0], local[1] + centre[1], local[2] + centre[2]};
        if (kind == 1)
        {
            if (nextVehicle == vehicles + count)
                continue;
            void *vehicle = *nextVehicle++;
            Virtual<void>(vehicle, 0x80);
            void *vehicleAi = Virtual<void *>(vehicle, 0xac);
            Virtual<void, const float *, const float *>(vehicleAi, 0x5c, spot, facing);
            Virtual<void>(vehicleAi, 0x88);
            Virtual<void, void *>(roadBlock, 4, vehicle);
            ++At<int>(self, 0x94);
            continue;
        }
        if (kind < 1 || kind > 3)
            continue;
        void *scenery;
        if (kind == 2)
            scenery = NFS_ORIGINAL(0x6816a0, void *(__cdecl *)(const char *, unsigned int))(
                reinterpret_cast<const char *>(0x892eb8), 0x9663ad06);
        else
        {
            scenery = NFS_ORIGINAL(0x6816a0, void *(__cdecl *)(const char *, unsigned int))(
                reinterpret_cast<const char *>(0x892ea0), 0xca89ef8f);
            unsigned int port;
            NFS_ORIGINAL(0x4023c0, void *(__thiscall *)(void *, const char *))(&port,
                                                                               reinterpret_cast<const char *>(0x892e94));
            DeliverRoadBlockRequest(port, static_cast<int>(piece[1]));
        }
        if (!scenery)
            continue;
        unsigned int manager[2] = {0, 3};
        float height;
        float up[3];
        alignas(16) float placed[16];
        const float *matrix;
        if (NFS_ORIGINAL(0x789870, bool(__thiscall *)(void *, const float *, float *, float *))(manager, spot, &height,
                                                                                             up))
        {
            if (up[1] < 0.0f)
            {
                up[0] *= -1.0f;
                up[1] *= -1.0f;
                up[2] *= -1.0f;
            }
            matrix = GenerateMatrix(placed, facing, up);
        }
        else
            matrix = GenerateMatrix(placed, facing, nullptr);
        alignas(16) float world[16];
        for (int k = 0; k < 16; ++k)
            world[k] = matrix[k];
        Virtual<void, void *, bool>(roadBlock, 8, scenery, kind == 3);
        Virtual<void>(pursuit, 0x100);
        world[12] = spot[0];
        world[13] = spot[1];
        world[14] = spot[2];
        Virtual<void, const float *, int>(scenery, 8, world, 1);
    }
    Virtual<void>(pursuit, 0xec);
    return true;
}
