/** Cop cars and the helicopter as pursuit vehicles: tethering, pursuit targets, the helicopter's destination and
 * altitude, the siren (original: zAI AIVehicleCopCar, AIVehiclePursuit, AIVehicleHelicopter). */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x409590, AIVehicleCopCarIsTetheredToTarget)
NFS_REPLACE(0x416b90, AIVehiclePursuitUpdateSiren)
NFS_REPLACE(0x409820, AIVehiclePursuitEndPursuit)
NFS_REPLACE(0x409860, AIVehiclePursuitGetPursuitTarget)
NFS_REPLACE(0x409890, AIVehiclePursuitPursuitRequest)
NFS_REPLACE(0x4098e0, AIVehicleHelicopterSetDestinationVelocity)
NFS_REPLACE(0x409950, AIVehicleHelicopterStartPathToPoint)
NFS_REPLACE(0x409980, AIVehicleHelicopterFilterHeliAltitude)
NFS_REPLACE(0x416af0, AIVehiclePursuitResetInternals)
NFS_REPLACE(0x423320, AIVehiclePursuitStartPatrol)
NFS_REPLACE(0x423370, AIVehiclePursuitStartFlee)
NFS_REPLACE(0x423430, AIVehiclePursuitStartRoadBlock)
NFS_REPLACE(0x4234e0, AIVehiclePursuitStartSupportGoal)
NFS_REPLACE(0x423510, AIVehicleHelicopterUpdateFuel)
NFS_REPLACE(0x42ad50, AIVehicleHelicopterSetFuelFull)
NFS_REPLACE(0x42ab80, AIVehiclePursuitStartPursuit)
NFS_REPLACE(0x42adb0, AIVehicleHelicopterUpdate)
NFS_REPLACE(0x42aed0, AIVehicleHelicopterRestrictPointToRoadNet)
NFS_REPLACE(0x429ef0, AIVehicleCopCarCheckForPursuit)
NFS_REPLACE(0x42f570, AIVehicleCopCarWatchForPerps)
NFS_REPLACE(0x431ef0, AIVehicleCopCarUpdate)
NFS_REPLACE(0x43d270, AIVehicleCopCarConstruct)
NFS_REPLACE(0x43d2c0, AIVehicleCopCarFactory)
NFS_REPLACE(0x43b1c0, AIVehiclePursuitConstructor)
NFS_REPLACE(0x432240, AIVehiclePursuitDestruct)
NFS_REPLACE(0x43b340, AIVehicleHelicopterConstructor)
NFS_REPLACE(0x4324a0, AIVehicleHelicopterDestruct)
NFS_REPLACE(0x43b4c0, AIVehicleHelicopterFactory)

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

const int kVehicleFromPursuitAi = 0x70c;
const int kVehicleFromHelicopter = 0x758;
const unsigned int kCopLights = 0x7000;
const float kLeastTetherSpeed = 22.3515f;
const float kMostTetherDistance = 50.0f;
const float kTetherHeading = -0.2f;

char *ChaseTarget(char *vehicle)
{
    char *target = Virtual<char *>(vehicle, 0x64);
    return At<unsigned char>(target, 0x2c) ? Virtual<char *>(vehicle, 0x64) : nullptr;
}
} // namespace

bool __fastcall AIVehicleCopCarIsTetheredToTarget(char *self, void *, char *other)
{
    char *vehicle = self + 0x4c;
    if (!At<unsigned char>(Virtual<char *>(vehicle, 0x64), 0x2c))
        return false;
    char *simable = At<char *>(Virtual<char *>(vehicle, 0x64), 0x1c);
    if (simable && other && At<void *>(simable, 4) == At<void *>(other, 4))
        return false;
    if (!Virtual<bool>(self + 0x758, 0x14))
        return false;
    void *body = Virtual<void *>(Virtual<void *>(vehicle, 4), 0x54);
    if (Virtual<float>(body, 0x30) < kLeastTetherSpeed)
        return false;
    if (At<float>(Virtual<char *>(vehicle, 0x64), 0x3c) > kMostTetherDistance)
        return false;
    float velocity[3];
    body = Virtual<void *>(Virtual<void *>(vehicle, 4), 0x54);
    Virtual<void, float *>(body, 0x34, velocity);
    const float *direction = &At<float>(Virtual<char *>(vehicle, 0x64), 0x30);
    const float heading = velocity[2] * direction[2] + velocity[1] * direction[1] + velocity[0] * direction[0];
    return heading < kTetherHeading;
}

void __fastcall AIVehiclePursuitEndPursuit(char *self)
{
    Virtual<void, bool>(self, 0x10, false);
    void *vehicle = Virtual<void *>(self - kVehicleFromPursuitAi, 8);
    Virtual<void, unsigned int>(vehicle, 0xa4, kCopLights);
}

char *__fastcall AIVehiclePursuitGetPursuitTarget(char *self)
{
    return ChaseTarget(self - kVehicleFromPursuitAi);
}

char *__fastcall AIVehiclePursuitPursuitRequest(char *self)
{
    if (Virtual<bool>(self, 0x14))
        return nullptr;
    return ChaseTarget(self - kVehicleFromPursuitAi);
}

void __fastcall AIVehicleHelicopterSetDestinationVelocity(char *self, void *, const float *velocity)
{
    float *destination = &At<float>(self, 8);
    for (int i = 0; i < 3; ++i)
        destination[i] = velocity[i];
    for (int i = 0; i < 3; ++i)
        destination[i] *= 9.0f;
    for (int i = 0; i < 3; ++i)
        destination[i] = (destination[i] + velocity[i]) * 0.1f;
}

bool __fastcall AIVehicleHelicopterStartPathToPoint(char *self, void *, float *point)
{
    char *vehicle = self - kVehicleFromHelicopter;
    Virtual<void, int>(vehicle, 0x50, 0);
    void *navigation = Virtual<void *>(vehicle, 0x48);
    return NFS_ORIGINAL(0x788f30, bool(__thiscall *)(void *, float *, int, int))(navigation, point, 0, 0);
}

bool __fastcall AIVehicleHelicopterFilterHeliAltitude(char *self, void *, float *point)
{
    float coordinate[2] = {point[2], -point[0]};
    bool found = false;
    const float ground = NFS_ORIGINAL(0x754290, float(__thiscall *)(void *, const float *, int, bool *))(
        self + 0x4c, coordinate, 0, &found);
    if (!found)
        return false;
    if (ground > point[1])
        point[1] = ground;
    return true;
}

namespace
{
const unsigned int *const kHelicopterClass = reinterpret_cast<const unsigned int *>(0x92c4f4);
const unsigned int *const kZeroVector = reinterpret_cast<const unsigned int *>(0x8a2fb4);
const unsigned int *const kSimTime = reinterpret_cast<const unsigned int *>(0x925ae8);
const unsigned int *const kFleeStartTime = reinterpret_cast<const unsigned int *>(0x91ddbc);
void **const kActiveHelicopter = reinterpret_cast<void **>(0x90d61c);
char *const kPlayers = reinterpret_cast<char *>(0x92d87c);
void *const kRigidBodyHandle = reinterpret_cast<void *>(0x4040e0);
void *const kPerpHandle = reinterpret_cast<void *>(0x4037e0);
const int kAiFromPursuitAi = 0x758;

unsigned int Goal(unsigned int address)
{
    return stringhash32(reinterpret_cast<const char *>(address));
}

void SetGoal(char *ai, unsigned int goal)
{
    NFS_ORIGINAL(0x422480, void(__thiscall *)(void *, const unsigned int *))(ai, &goal);
}

void ClearTarget(char *vehicle)
{
    NFS_ORIGINAL(0x409e60, void(__thiscall *)(void *))(Virtual<void *>(vehicle, 0x64));
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

void __fastcall AIVehiclePursuitResetInternals(char *self)
{
    NFS_ORIGINAL(0x414ce0, void(__thiscall *)(void *))(self);
    static const int kFlags[] = {0x760, 0x761, 0x762, 0x763, 0x778, 0x779};
    for (const int offset : kFlags)
        At<unsigned char>(self, offset) = 0;
    At<unsigned int>(self, 0x77c) = kZeroVector[0];
    At<unsigned int>(self, 0x780) = kZeroVector[1];
    At<unsigned char>(self, 0x77a) = 0;
    At<float>(self, 0x798) = 99.0f;
    At<float>(self, 0x79c) = 0.25f;
    At<unsigned int>(self, 0x784) = kZeroVector[2];
    const unsigned int now = *kSimTime;
    At<unsigned int>(self, 0x774) = now;
    At<unsigned int>(self, 0x770) = now;
    At<unsigned int>(self, 0x76c) = now;
    At<int>(self, 0x764) = -1;
    At<unsigned char>(self, 0x768) = 0;
}

void __fastcall AIVehiclePursuitStartPatrol(char *self)
{
    Virtual<void, bool>(self, 0x10, false);
    ClearTarget(self - kVehicleFromPursuitAi);
    SetGoal(self - kAiFromPursuitAi, Goal(0x891540));
}

void __fastcall AIVehiclePursuitStartFlee(char *self)
{
    char *vehicle = self - kVehicleFromPursuitAi;
    Virtual<void, unsigned int>(Virtual<void *>(vehicle, 8), 0xa4, kCopLights);
    unsigned int goal = Goal(0x891188);
    if (void *body = Find(Virtual<void *>(vehicle, 4), kRigidBodyHandle))
    {
        if (IsHelicopter(body))
            goal = Goal(0x891174);
    }
    if (*Virtual<const unsigned int *>(vehicle, 0xc0) == goal)
        return;
    char *ai = self - kAiFromPursuitAi;
    if (void *action = At<void *>(ai, 0xb8))
    {
        Virtual<void *, unsigned int>(action, 0, 1);
        At<void *>(ai, 0xb8) = nullptr;
    }
    At<unsigned int>(ai, 0xc4) = *kFleeStartTime;
    SetGoal(ai, goal);
}

void __fastcall AIVehiclePursuitStartRoadBlock(char *self)
{
    char *vehicle = self - kVehicleFromPursuitAi;
    Virtual<void, unsigned int>(Virtual<void *>(vehicle, 8), 0xa0, kCopLights);
    Virtual<void, bool>(self, 0x10, true);
    ClearTarget(vehicle);
    const bool helicopter = IsHelicopter(Virtual<void *>(vehicle, 8));
    SetGoal(self - kAiFromPursuitAi, Goal(helicopter ? 0x891550 : 0x890904));
}

bool __fastcall AIVehiclePursuitStartSupportGoal(char *self)
{
    if (At<unsigned int>(self, 0x48) == stringhash32(nullptr))
        return false;
    NFS_ORIGINAL(0x422480, void(__thiscall *)(void *, const unsigned int *))(self - kAiFromPursuitAi,
                                                                              &At<unsigned int>(self, 0x48));
    return true;
}

void __fastcall AIVehicleHelicopterUpdateFuel(char *self, void *, float dt)
{
    float &fuel = At<float>(self, 0x7d8);
    fuel -= dt;
    if (!(fuel < 0.0f))
        return;
    fuel = 0.0f;
    unsigned int goal = Goal(0x891174);
    if (!Virtual<bool, const unsigned int *>(self + 0x4c, 0xbc, &goal))
        SetGoal(self, Goal(0x891174));
}

void __fastcall AIVehicleHelicopterSetFuelFull(char *self)
{
    *kActiveHelicopter = self - 0x7a4;
    void *player = At<int>(kPlayers, 8) ? *At<void **>(kPlayers, 0) : nullptr;
    void *vehicle = Virtual<void *>(player, 4);
    if (!vehicle)
        return;
    void *perp = Find(vehicle, kPerpHandle);
    if (!perp)
        return;
    char *levels = Virtual<char *>(perp, 0x10);
    if (levels)
        At<unsigned int>(self, 0x34) = At<unsigned int>(At<char *>(levels, 8), 0x6c);
}

void __fastcall CopPursuitUpdate(char *cop, void *, float dt);
void __fastcall AIVehicleHelicopterUpdateFuel(char *self, void *, float dt);

namespace
{
void Acquire(void *target, void *simable)
{
    NFS_ORIGINAL(0x423860, void(__thiscall *)(void *, void *))(target, simable);
}

/** AIVehicle: whether nothing in the world stands between it and its target. */
void UpdateClearView(char *ai)
{
    char *target = At<char *>(ai, 0xa0);
    if (!At<unsigned char>(target, 0x2c))
        return;
    void *position = Virtual<void *>(At<void *>(ai, 0xd0), 4);
    const bool blocked = NFS_ORIGINAL(0x408810, bool(__thiscall *)(void *, void *, void *))(ai, position,
                                                                                         At<char *>(ai, 0xa0) + 0x10);
    At<unsigned char>(ai, 0xa6) = !blocked;
}

void *FirstPlayer()
{
    return At<int>(kPlayers, 8) ? *At<void **>(kPlayers, 0) : nullptr;
}
} // namespace

void __fastcall AIVehiclePursuitStartPursuit(char *self, void *, char *target, void *simable)
{
    char *vehicle = self - kVehicleFromPursuitAi;
    Virtual<void, unsigned int>(Virtual<void *>(vehicle, 8), 0xa0, kCopLights);
    if (target)
    {
        void *mine = Virtual<void *>(vehicle, 0x64);
        if (target != mine && At<unsigned char>(target, 0x2c))
        {
            if (void *other = At<void *>(target, 0x1c))
                Acquire(mine, other);
            else
                NFS_ORIGINAL(0x4238b0, void(__thiscall *)(void *, const void *))(mine, target + 0x10);
        }
    }
    else if (simable)
        Acquire(Virtual<void *>(vehicle, 0x64), simable);
    char *ai = self - kAiFromPursuitAi;
    UpdateClearView(ai);
    Virtual<void, bool>(self, 0x10, true);
    const bool helicopter = IsHelicopter(Virtual<void *>(vehicle, 8));
    SetGoal(ai, Goal(helicopter ? 0x891160 : 0x89092c));
}

void __fastcall AIVehicleHelicopterUpdate(char *self, void *, float dt)
{
    CopPursuitUpdate(self, nullptr, dt);
    void *player = FirstPlayer();
    void *playerVehicle = Virtual<void *>(player, 4);
    char *vehicle = self + 0x4c;
    Acquire(Virtual<void *>(vehicle, 0x64), playerVehicle);
    At<float>(self, 0xb0) += dt;
    UpdateClearView(self);
    AIVehicleHelicopterUpdateFuel(self, nullptr, dt);
    if (void *action = At<void *>(self, 0xb8))
        Virtual<void, float>(action, 8, dt);
    const float *position = Virtual<const float *>(Virtual<void *>(At<void *>(self, 0x34), 0x54), 0x20);
    alignas(4) unsigned int place[15];
    const unsigned int *worldPosition = Virtual<const unsigned int *>(Virtual<void *>(vehicle, 4), 0x4c);
    for (int i = 0; i < 15; ++i)
        place[i] = worldPosition[i];
    const float ground = NFS_ORIGINAL(0x768200, float(__thiscall *)(void *, const float *))(place, position);
    if (ground == 0.0f)
        return;
    const float dust = 1.0f - (position[1] - (ground + 5.0f)) * 0.025f;
    Virtual<void, float>(self + 0x7a4, 0x38, dust);
}

void __fastcall AIVehicleHelicopterRestrictPointToRoadNet(char *self, void *, float *point)
{
    char *navigation = Virtual<char *>(self - kAiFromPursuitAi, 0x48);
    At<int>(navigation, 0x78) = 1;
    At<int>(navigation, 0x80) = 1;
    float direction[3];
    Virtual<void, float *>(Virtual<void *>(Virtual<void *>(FirstPlayer(), 4), 0x54), 0x34, direction);
    NFS_ORIGINAL(0x7862d0, void(__thiscall *)(void *, float *, const float *, int, int))(navigation, point, direction,
                                                                                        0, 0);
    if (At<unsigned char>(navigation, 0x50))
    {
        point[0] = At<float>(navigation, 0x98);
        point[2] = At<float>(navigation, 0xa0);
    }
}

namespace
{
char *const kPlayerVehicles = reinterpret_cast<char *>(0x92cd7c);
char *const kRacerVehicles = reinterpret_cast<char *>(0x92cf5c);
void **const kCopManager = reinterpret_cast<void **>(0x92c67c);
void *const kVehicleAiHandle = reinterpret_cast<void *>(0x403700);
const float kRecent911 = 1.0f;
const int kLeastHeatForAnyone = 4;
const float kLeastSuspectSpeed = 29.056772f;
const float kRespawnLift = 1.5f;

void **ListBegin(char *list)
{
    return At<void **>(list, 0);
}

void **ListEnd(char *list)
{
    return At<void **>(list, 0) + At<int>(list, 8);
}

/** AITarget as a local: built on its owner, unlinked from the targets list when done. */
struct LocalTarget
{
    alignas(8) unsigned char bytes[0x40];

    explicit LocalTarget(void *owner)
    {
        NFS_ORIGINAL(0x418000, void *(__thiscall *)(void *, void *))(bytes, owner);
    }
    ~LocalTarget()
    {
        char *next = *reinterpret_cast<char **>(bytes + 4);
        char *previous = *reinterpret_cast<char **>(bytes + 8);
        *reinterpret_cast<char **>(previous) = next;
        *reinterpret_cast<char **>(next + 4) = previous;
    }
};

float SimTime()
{
    return NFS_ORIGINAL(0x6e8de0, float(__cdecl *)())();
}
} // namespace

bool __fastcall AIVehicleCopCarCheckForPursuit(char *self, void *, void *vehicle)
{
    void *simable = Virtual<void *>(vehicle, 4);
    void *perp = Find(vehicle, kPerpHandle);
    if (!perp)
        return false;
    char *car = self + 0x4c;
    LocalTarget target(Virtual<void *>(car, 4));
    Acquire(target.bytes, simable);
    if (!Virtual<bool, void *>(self + 0x758, 0x7c, target.bytes))
        return false;
    void *ai = Find(vehicle, kVehicleAiHandle);
    const float called = Virtual<float>(perp, 0x54);
    const bool recentCall = called > 0.0f && SimTime() - called < kRecent911;
    const bool wanted = Virtual<float>(perp, 0x5c) > 0.0f;
    const bool pursued = ai && Virtual<void *>(ai, 0xa0);
    const int heat = static_cast<int>(Virtual<float>(perp, 4));
    if (!pursued && !wanted && !recentCall && heat < kLeastHeatForAnyone &&
        Virtual<float>(vehicle, 0x90) < kLeastSuspectSpeed)
        return false;
    Acquire(Virtual<void *>(car, 0x64), simable);
    return true;
}

void __fastcall AIVehicleCopCarWatchForPerps(char *self)
{
    if (Virtual<bool>(self + 0x758, 0x14))
        return;
    for (void **it = ListBegin(kPlayerVehicles); it != ListEnd(kPlayerVehicles); ++it)
    {
        if (AIVehicleCopCarCheckForPursuit(self, nullptr, *it))
            return;
    }
    void *manager = *kCopManager;
    if (!manager || !Virtual<bool>(manager, 0x24))
        return;
    for (void **it = ListBegin(kRacerVehicles); it != ListEnd(kRacerVehicles); ++it)
    {
        const int kind = Virtual<int>(*it, 0x58);
        if (kind == 0 || kind == 6)
            continue;
        if (AIVehicleCopCarCheckForPursuit(self, nullptr, *it))
            return;
    }
}

void __fastcall AIVehicleCopCarUpdate(char *self, void *, float dt)
{
    char *car = self + 0x4c;
    const bool simple = Virtual<int>(Virtual<void *>(car, 8), 0x38) == 2;
    if (Virtual<bool>(Virtual<void *>(car, 8), 0xc4))
    {
        if (!simple)
            Virtual<void>(car, 0x98);
    }
    else if (simple)
    {
        float velocity[3];
        Virtual<void, float *>(Virtual<void *>(Virtual<void *>(car, 4), 0x54), 0x34, velocity);
        const float *at = Virtual<const float *>(Virtual<void *>(Virtual<void *>(car, 4), 0x54), 0x20);
        float position[3] = {at[0], at[1] + kRespawnLift, at[2]};
        if (!Virtual<bool, float *, float *>(Virtual<void *>(car, 8), 0x68, position, velocity))
            Virtual<bool, float *, float *>(Virtual<void *>(car, 8), 0x68, position, velocity);
        Virtual<void>(car, 0x9c);
    }
    CopPursuitUpdate(self, nullptr, dt);
    At<float>(self, 0xb0) += dt;
    NFS_ORIGINAL(0x408730, void(__thiscall *)(void *, float))(self, dt);
    UpdateClearView(self);
    NFS_ORIGINAL(0x415080, void(__thiscall *)(void *))(self);
    if (void *action = At<void *>(self, 0xb8))
        Virtual<void, float>(action, 8, dt);
    AIVehicleCopCarWatchForPerps(self);
}

void *__fastcall AIVehiclePursuitConstructor(char *self, void *, const void *params);

void *__fastcall AIVehicleCopCarConstruct(char *self, void *, const void *params)
{
    AIVehiclePursuitConstructor(self, nullptr, params);
    At<unsigned int>(self, 0) = 0x892560;
    At<unsigned int>(self, 8) = 0x892558;
    At<unsigned int>(self, 0x4c) = 0x892480;
    At<unsigned int>(self, 0x54) = 0x892478;
    At<unsigned int>(self, 0x758) = 0x8923e8;
    At<float>(self, 0x7a4) = -0.3f;
    At<unsigned char>(self, 0x7a8) = 0;
    return self;
}

void *__cdecl AIVehicleCopCarFactory(const void *params)
{
    void *memory = NFS_ORIGINAL(0x5d29d0, void *(__thiscall *)(void *, unsigned int, const char *))(
        reinterpret_cast<void *>(0x925b30), 0x7ac, nullptr);
    return memory ? AIVehicleCopCarConstruct(static_cast<char *>(memory), nullptr, params) : nullptr;
}

namespace
{
float *const kPursuitPhase = reinterpret_cast<float *>(0x90d618);
float *const kHelicopterStagger = reinterpret_cast<float *>(0x90d9a8);

void AttachInterface(char *iface, const void *params, unsigned int handle)
{
    char *object = At<char *>(const_cast<void *>(params), 4);
    At<unsigned int>(iface, 0) = 0x890970;
    At<char *>(iface, 4) = object ? object + 0x10 : nullptr;
    NFS_ORIGINAL(0x5d7930, void(__thiscall *)(void *, void *, void *))(At<void *>(iface, 4),
                                                                        reinterpret_cast<void *>(handle), iface);
}

void DetachInterface(char *iface)
{
    At<unsigned int>(iface, 0) = 0x890970;
    NFS_ORIGINAL(0x5d5a50, void(__thiscall *)(void *, void *))(At<void *>(iface, 4), iface);
}

void *Allocate(unsigned int size)
{
    return NFS_ORIGINAL(0x5d29d0, void *(__thiscall *)(void *, unsigned int, const char *))(
        reinterpret_cast<void *>(0x925b30), size, nullptr);
}

void SetZero(char *self, int offset)
{
    for (int i = 0; i < 3; ++i)
        At<unsigned int>(self, offset + i * 4) = kZeroVector[i];
}
} // namespace

void *__fastcall AIVehiclePursuitConstructor(char *self, void *, const void *params)
{
    NFS_ORIGINAL(0x43ada0, void *(__thiscall *)(void *, const void *, float, float, int))(self, params, 0.125f,
                                                                                         *kPursuitPhase, 0);
    char *pursuitAi = self + 0x758;
    AttachInterface(pursuitAi, params, 0x4038e0);
    At<unsigned int>(pursuitAi, 0) = 0x890b00;
    At<unsigned int>(pursuitAi, 0) = 0x891d48;
    At<unsigned int>(self, 0) = 0x891ec0;
    At<unsigned int>(self, 8) = 0x891eb8;
    At<unsigned int>(self, 0x4c) = 0x891de0;
    At<unsigned int>(self, 0x54) = 0x891dd8;
    for (int offset = 0x760; offset <= 0x763; ++offset)
        At<unsigned char>(self, offset) = 0;
    At<int>(self, 0x764) = -1;
    At<unsigned char>(self, 0x768) = 0;
    At<unsigned int>(self, 0x76c) = 0;
    At<unsigned int>(self, 0x770) = 0;
    At<unsigned int>(self, 0x774) = 0;
    At<unsigned char>(self, 0x778) = 0;
    At<unsigned char>(self, 0x779) = 0;
    At<unsigned char>(self, 0x77a) = 0;
    SetZero(self, 0x77c);
    At<unsigned int>(self, 0x794) = 0;
    At<unsigned int>(self, 0x7a0) = 0;
    *kPursuitPhase += 0.125f;
    if (!(*kPursuitPhase < 1.0f))
        *kPursuitPhase = 0.0f;
    At<unsigned int>(self, 0x79c) = 0;
    At<float>(self, 0x798) = 99.0f;
    At<unsigned int>(self, 0x7a0) = stringhash32(nullptr);
    const unsigned int now = *kSimTime;
    At<unsigned int>(self, 0x774) = now;
    At<unsigned int>(self, 0x770) = now;
    At<unsigned int>(self, 0x76c) = now;
    return self;
}

void __fastcall AIVehiclePursuitDestruct(char *self)
{
    At<unsigned int>(self, 0) = 0x891ec0;
    At<unsigned int>(self, 8) = 0x891eb8;
    At<unsigned int>(self, 0x4c) = 0x891de0;
    At<unsigned int>(self, 0x54) = 0x891dd8;
    At<unsigned int>(self, 0x758) = 0x891d48;
    DetachInterface(self + 0x758);
    NFS_ORIGINAL(0x432060, void(__thiscall *)(void *))(self);
}

void *__fastcall AIVehicleHelicopterConstructor(char *self, void *, const void *params)
{
    AIVehiclePursuitConstructor(self, nullptr, params);
    char *helicopter = self + 0x7a4;
    AttachInterface(helicopter, params, 0x404060);
    At<unsigned int>(helicopter, 0) = 0x890b98;
    *kHelicopterStagger += 10.0f;
    At<unsigned int>(self, 0) = 0x8920d8;
    At<unsigned int>(self, 8) = 0x8920d0;
    At<unsigned int>(self, 0x4c) = 0x891ff8;
    At<unsigned int>(self, 0x54) = 0x891ff0;
    At<unsigned int>(self, 0x758) = 0x891f60;
    At<unsigned int>(helicopter, 0) = 0x891f18;
    At<unsigned char>(self, 0x7d5) = 0;
    At<float>(self, 0x7d8) = 30.0f;
    At<unsigned int>(self, 0x7dc) = 0;
    At<unsigned int>(self, 0x7e0) = 0;
    At<unsigned char>(self, 0x7f4) = 0;
    At<float>(self, 0x7f0) = 1000.0f;
    At<float>(self, 0x830) = 1000.0f;
    At<unsigned char>(self, 0x834) = 0;
    At<float>(self, 0x870) = 1000.0f;
    At<unsigned char>(self, 0x874) = 0;
    At<void *>(self, 0x8b0) = Find(At<void *>(self, 0x34), reinterpret_cast<void *>(0x404050));
    At<unsigned char>(self, 0x7d4) = 0;
    SetZero(self, 0x7b8);
    SetZero(self, 0x7c4);
    SetZero(self, 0x7ac);
    return self;
}

void __fastcall AIVehicleHelicopterDestruct(char *self)
{
    At<unsigned int>(self, 0) = 0x8920d8;
    At<unsigned int>(self, 8) = 0x8920d0;
    At<unsigned int>(self, 0x4c) = 0x891ff8;
    At<unsigned int>(self, 0x54) = 0x891ff0;
    At<unsigned int>(self, 0x758) = 0x891f60;
    At<unsigned int>(self, 0x7a4) = 0x891f18;
    *kActiveHelicopter = nullptr;
    DetachInterface(self + 0x7a4);
    AIVehiclePursuitDestruct(self);
}

void *__cdecl AIVehicleHelicopterFactory(const void *params)
{
    void *memory = Allocate(0x8c0);
    return memory ? AIVehicleHelicopterConstructor(static_cast<char *>(memory), nullptr, params) : nullptr;
}

namespace
{
int Now()
{
    return *reinterpret_cast<const int *>(0x925ae8);
}

float SecondsSince(int then)
{
    return static_cast<float>(Now() - then) * 0.00025f;
}

float RandomSirenPause(char *tuning)
{
    return NFS_ORIGINAL(0x45da70, float(__cdecl *)(float))(At<float>(tuning, 0xa4)) + At<float>(tuning, 0xac);
}

bool CopSpeechPlaying(int line)
{
    return NFS_ORIGINAL(0x6ff320, bool(__cdecl *)(int))(line);
}
} // namespace

void __fastcall AIVehiclePursuitUpdateSiren(char *self, void *, float)
{
    char *ai = self + 0x4c;
    void *pursuit = Virtual<void *>(ai, 0xa0);
    char *sound = *reinterpret_cast<char **>(0x993cc8);
    void *vehicle = Virtual<void *>(ai, 8);
    bool dday = false;
    bool silenced = false;
    bool scripted = true;
    char *race = *reinterpret_cast<char **>(0x91e000);
    if (void *parameters = At<void *>(race, 0x1968))
    {
        NFS_ORIGINAL(0x5fbe70, bool(__thiscall *)(void *))(parameters);
        dday = NFS_ORIGINAL(0x5fbd20, bool(__thiscall *)(void *))(At<void *>(race, 0x1968));
    }
    for (void **it = *reinterpret_cast<void ***>(0x92fd5c);
         it != *reinterpret_cast<void ***>(0x92fd5c) + *reinterpret_cast<int *>(0x92fd64); ++it)
    {
        void *watcher = *it;
        if (!watcher)
            continue;
        void *handle = Virtual<void *>(Virtual<void *>(ai, 4), 0x34);
        if (Virtual<void *, void *>(watcher, 0x58, handle) == Virtual<void *>(ai, 8))
        {
            silenced = true;
            break;
        }
    }
    int &state = At<int>(self, 0x764);
    if (!sound || !pursuit || !At<unsigned char>(self, 0x760) || !At<void *>(sound, 0x194))
    {
        state = -1;
        if (silenced)
            state = 0;
        return;
    }
    char *tuning = At<char *>(sound, 0x198);
    if (Virtual<bool>(pursuit, 0x8c) && !dday && At<int>(sound, 0x158) <= 1)
    {
        const int mode = At<int>(sound, 0x138);
        if (mode == 1 && At<float>(sound, 0x140) < 5.0f)
        {
            const float quiet = SecondsSince(At<int>(self, 0x76c));
            scripted = false;
            unsigned int &seeded = *reinterpret_cast<unsigned int *>(0x90d974);
            float &pause = *reinterpret_cast<float *>(0x90d970);
            if (!(seeded & 1))
            {
                seeded |= 1;
                pause = RandomSirenPause(At<char *>(sound, 0x198));
            }
            if (quiet > pause && state == -1)
            {
                At<int>(self, 0x770) = Now();
                state = 1;
            }
            if (state == 1 && SecondsSince(At<int>(self, 0x770)) > 0.5f)
            {
                state = -1;
                pause = RandomSirenPause(At<char *>(sound, 0x198));
                At<int>(self, 0x76c) = Now();
            }
        }
        else if (mode == 0x29a)
        {
            state = -1;
            scripted = false;
            At<int>(self, 0x76c) = Now();
        }
    }
    if (!Virtual<bool, int>(vehicle, 0x9c, 0x7000))
        state = Virtual<bool>(vehicle, 0x7c) ? 3 : -1;
    if (scripted)
    {
        const float since = SecondsSince(At<int>(self, 0x76c));
        const bool intense = NFS_ORIGINAL(0x701510, bool(__thiscall *)(void *))(sound);
        bool fresh;
        if (since < 6.0f)
            fresh = true;
        else
        {
            fresh = false;
            if (At<unsigned char>(self, 0x768))
            {
                At<int>(self, 0x770) = Now();
                state = intense ? 1 : 0;
            }
        }
        At<unsigned char>(self, 0x768) = fresh;
        if (fresh)
        {
            if (!intense)
                state = 1;
            else
                state = since < 3.0f ? 2 : 1;
        }
        else if ((Virtual<bool>(pursuit, 0x30) || Virtual<bool>(pursuit, 0x58) || CopSpeechPlaying(0xa0) ||
                  CopSpeechPlaying(0xd6) || CopSpeechPlaying(0xde) || CopSpeechPlaying(0x40)) &&
                 At<unsigned char>(self, 0x778))
        {
            if (!intense)
            {
                state = 1;
                At<int>(self, 0x770) = Now();
            }
            else
            {
                const float wail = SecondsSince(At<int>(self, 0x774));
                if (state != 2 && wail > At<float>(tuning, 0xbc))
                {
                    state = 2;
                    At<int>(self, 0x774) = Now();
                    At<int>(self, 0x770) = Now();
                }
                else if (wail > At<float>(tuning, 0x64))
                {
                    state = 1;
                    At<int>(self, 0x774) = Now();
                    At<int>(self, 0x770) = Now();
                }
                else
                    At<int>(self, 0x770) = Now();
            }
        }
        else
        {
            const float since770 = SecondsSince(At<int>(self, 0x770));
            if (since770 > At<float>(tuning, 0x7c) + At<float>(tuning, 0x58))
            {
                state = 0;
                At<int>(self, 0x770) = Now();
            }
            else if (since770 > At<float>(tuning, 0x7c))
                state = 1;
        }
    }
    if (silenced)
        state = 0;
}
