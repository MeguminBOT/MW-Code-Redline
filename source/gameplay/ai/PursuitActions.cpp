/** Cop pursuit actions: chasing off the road, ramming, the static roadblock wait and the helicopter's exit (original: zAI AIActionPursuitOffRoad, AIActionRam,
 * AIActionStaticRoadBlock, AIActionHeliExit, AIActionHeliPursuit, AISteer::VehicleSeperation, Seperation, SetupCapsule, Ram). */
#include <math.h>

#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x421670, AIActionRamConstruct)
NFS_REPLACE(0x4217a0, AIActionRamFactory)
NFS_REPLACE(0x413540, AIActionRamOnBehaviorChange)
NFS_REPLACE(0x4135b0, AIActionRamBeginAction)
NFS_REPLACE(0x4135f0, AIActionRamGetSeekPosition)
NFS_REPLACE(0x413ab0, AIActionRamUpdateSeek)
NFS_REPLACE(0x427c40, AIActionRamUpdate)
NFS_REPLACE(0x421ec0, AIActionStaticRoadBlockConstruct)
NFS_REPLACE(0x421fb0, AIActionStaticRoadBlockFactory)
NFS_REPLACE(0x4211b0, AIActionHeliExitConstruct)
NFS_REPLACE(0x4212f0, AIActionHeliExitFactory)
NFS_REPLACE(0x4279f0, AIActionHeliExitUpdate)
NFS_REPLACE(0x423990, AISteerVehicleSeperation)
NFS_REPLACE(0x40a430, AISteerSetupCapsule)
NFS_REPLACE(0x418a50, AISteerSeperation)
NFS_REPLACE(0x418200, AISteerRam)
NFS_REPLACE(0x420240, AIActionPursuitOffRoadConstruct)
NFS_REPLACE(0x420390, AIActionPursuitOffRoadFactory)
NFS_REPLACE(0x411f90, AIActionPursuitOffRoadOnBehaviorChange)
NFS_REPLACE(0x412200, AIActionPursuitOffRoadBeginAction)
NFS_REPLACE(0x412310, AIActionPursuitOffRoadUpdateAvoidWalls)
NFS_REPLACE(0x420400, AIActionPursuitOffRoadUpdateSeek)
NFS_REPLACE(0x427040, AIActionPursuitOffRoadUpdate)
NFS_REPLACE(0x420ec0, AIActionHeliPursuitConstruct)
NFS_REPLACE(0x421140, AIActionHeliPursuitFactory)
NFS_REPLACE(0x4210b0, AIActionHeliPursuitOnCollision)
NFS_REPLACE(0x4277e0, AIActionHeliPursuitUpdate)
NFS_REPLACE(0x412770, AIActionHeliPursuitStraightLinePursuit)
NFS_REPLACE(0x412b40, AIActionHeliPursuitSkidHitPursuit)
NFS_REPLACE(0x412f30, AIActionHeliPursuitSetNextPerpSearchDest)
NFS_REPLACE(0x413090, AIActionHeliPursuitSearchForPerp)

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

struct Vec3
{
    float x, y, z;
};

float Constant(unsigned int address)
{
    return *reinterpret_cast<const float *>(address);
}

void *Find(void *object, unsigned int handle)
{
    return NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(At<void *>(object, 4),
                                                                       reinterpret_cast<void *>(handle));
}

float Attribute(void *instance, unsigned int key)
{
    const void *value = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
        instance, key, 0);
    if (!value)
        value = NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(4);
    return *static_cast<const float *>(value);
}

float Normalize(float *vector)
{
    return NFS_ORIGINAL(0x410220, float(__cdecl *)(float *))(vector);
}

float Length(const float *vector)
{
    return NFS_ORIGINAL(0x410180, float(__cdecl *)(const float *))(vector);
}

float Distance(const float *a, const float *b)
{
    return NFS_ORIGINAL(0x4100a0, float(__cdecl *)(const float *, const float *))(a, b);
}

void *AllocateAction(unsigned int size)
{
    return NFS_ORIGINAL(0x5d29d0, void *(__thiscall *)(void *, unsigned int, const char *))(
        reinterpret_cast<void *>(0x925b30), size, nullptr);
}

void ConstructAction(char *self, void **params, float interval)
{
    NFS_ORIGINAL(0x418d90, void *(__thiscall *)(void *, void **, float))(self, params, interval);
}

const float *const kZero = reinterpret_cast<const float *>(0x8a2fb4);
const unsigned int *const kBehaviorDrive = reinterpret_cast<const unsigned int *>(0x9261b8);
const unsigned int *const kBehaviorSimable = reinterpret_cast<const unsigned int *>(0x92c5e0);
const unsigned int *const kBehaviorTarget = reinterpret_cast<const unsigned int *>(0x92c5a0);
const unsigned int kOne = 0x3f800000;

/** The target's velocity, or zero when it has no vehicle. */
const float *TargetVelocity(char *target)
{
    void *vehicle = At<void *>(target, 0x1c);
    if (!vehicle)
        return kZero;
    return Virtual<const float *>(Virtual<void *>(vehicle, 0x54), 0x24);
}
} // namespace

void *__fastcall AIActionRamConstruct(char *self, void *, void **params, float interval)
{
    ConstructAction(self, params, interval);
    At<unsigned int>(self, 0) = 0x8913ec;
    At<unsigned int>(self, 8) = 0x8913e4;
    At<void *>(self, 0x5c) = Find(*params, 0x403f90);
    At<void *>(self, 0x48) = Find(*params, 0x403700);
    At<void *>(self, 0x54) = Find(*params, 0x4038e0);
    At<void *>(self, 0x50) = Find(*params, 0x4040e0);
    At<void *>(self, 0x58) = Find(*params, 0x404010);
    At<void *>(self, 0x4c) = Virtual<void *>(*params, 0x54);
    int &rams = *reinterpret_cast<int *>(0x90d984);
    ++rams;
    At<unsigned char>(self, 0x60) = static_cast<unsigned char>(rams & 1);
    return self;
}

void *__cdecl AIActionRamFactory(void **params)
{
    void *memory = AllocateAction(0x68);
    return memory ? AIActionRamConstruct(static_cast<char *>(memory), nullptr, params, 0.1f) : nullptr;
}

void __fastcall AIActionRamOnBehaviorChange(char *self, void *, const unsigned int *behavior)
{
    if (*behavior == *kBehaviorDrive)
        At<void *>(self, 0x5c) = Find(At<void *>(self, 0x30), 0x403f90);
    if (*behavior == *kBehaviorSimable)
        At<void *>(self, 0x4c) = Find(At<void *>(self, 0x30), 0x4039f0);
    if (*behavior == *kBehaviorTarget)
        At<void *>(self, 0x58) = Find(At<void *>(self, 0x30), 0x404010);
}

void __fastcall AIActionRamBeginAction(char *self, void *, float)
{
    if (!(Virtual<float>(At<void *>(self, 0x48), 0x84) > 0.0f))
        Virtual<void, unsigned int>(At<void *>(self, 0x50), 0x94, 0x41d6930c);
    At<float>(self, 0x64) = Virtual<float>(At<void *>(self, 0x50), 0x90);
}

void __fastcall AIActionRamGetSeekPosition(char *self, void *, float *out, bool avoid)
{
    char *target = Virtual<char *>(At<void *>(self, 0x48), 0x64);
    const Vec3 targetPosition = At<Vec3>(target, 0x10);
    const Vec3 velocity = *reinterpret_cast<const Vec3 *>(TargetVelocity(target));
    const Vec3 mine = *Virtual<const Vec3 *>(At<void *>(self, 0x4c), 0x20);
    Vec3 heading;
    void *vehicle = At<void *>(target, 0x1c);
    if (vehicle && !(Virtual<float>(Virtual<void *>(vehicle, 0x54), 0x30) < Constant(0x891080)))
        heading = velocity;
    else
        heading = At<Vec3>(target, 0x20);
    Normalize(&heading.x);
    Vec3 side = {heading.z, 0.0f, -heading.x};
    Normalize(&side.x);
    const Vec3 offset = *Virtual<const Vec3 *>(At<void *>(self, 0x54), 0x54);
    const Vec3 delta = {targetPosition.x - mine.x, targetPosition.y - mine.y, targetPosition.z - mine.z};
    const Vec3 slot = {side.x * offset.x + offset.z * heading.x, side.y * offset.x + offset.z * heading.y,
                       side.z * offset.x + offset.z * heading.z};
    const Vec3 aim = {delta.x + slot.x, delta.y + slot.y, delta.z + slot.z};
    const float slotLength = sqrtf(slot.x * slot.x + slot.z * slot.z);
    const float deltaLength = sqrtf(delta.x * delta.x + delta.z * delta.z);
    const float aimLength = sqrtf(aim.z * aim.z + aim.x * aim.x);
    const float radius = (!(slotLength < deltaLength) ? deltaLength : slotLength) - Constant(0x890864);
    if (avoid && !(radius <= Constant(0x890cd4)))
    {
        float first;
        float second;
        if (NFS_ORIGINAL(0x767400, bool(__cdecl *)(float, float, float, float, float, float, float, float *, float *))(
                -delta.x, -delta.z, slot.x, slot.z, 0.0f, 0.0f, radius, &first, &second))
        {
            Vec3 normal = {-delta.z, 0.0f, delta.x};
            const float scale = slotLength * Constant(0x891084) / Length(&normal.x);
            normal.x *= scale;
            normal.y *= scale;
            normal.z *= scale;
            Vec3 left = {normal.x + delta.x, normal.y + delta.y, normal.z + delta.z};
            Vec3 right = {delta.x - normal.x, delta.y - normal.y, delta.z - normal.z};
            const float leftScale = aimLength / Length(&left.x);
            left.x *= leftScale;
            left.y *= leftScale;
            left.z *= leftScale;
            const float rightScale = aimLength / Length(&right.x);
            right.x *= rightScale;
            right.y *= rightScale;
            right.z *= rightScale;
            const float leftDistance = Distance(&left.x, &aim.x);
            if (Distance(&right.x, &aim.x) < leftDistance)
                left = right;
            out[0] = left.x + targetPosition.x;
            out[1] = left.y + targetPosition.y;
            out[2] = left.z + targetPosition.z;
            return;
        }
    }
    out[0] = targetPosition.x + slot.x;
    out[1] = slot.y + targetPosition.y;
    out[2] = targetPosition.z + slot.z;
}

void __fastcall AIActionRamUpdateSeek(char *self, void *, float *out, const float *seek, bool)
{
    void *body = At<void *>(self, 0x4c);
    const Vec3 position = *Virtual<const Vec3 *>(body, 0x20);
    float forward[4];
    Virtual<void, float *>(body, 0x34, forward);
    char *target = Virtual<char *>(At<void *>(self, 0x48), 0x64);
    const Vec3 velocity = *reinterpret_cast<const Vec3 *>(TargetVelocity(target));
    const float speed = sqrtf(velocity.y * velocity.y + velocity.z * velocity.z + velocity.x * velocity.x);
    Vec3 aim = {seek[0], seek[1], seek[2]};
    if (speed > Constant(0x890dc4))
    {
        const Vec3 delta = {seek[0] - position.x, seek[1] - position.y, seek[2] - position.z};
        const float inverse = 1.0f / speed;
        const float lead =
            (velocity.y * delta.y + delta.z * velocity.z + delta.x * velocity.x) * inverse * inverse *
            Constant(0x89107c);
        const float x = lead * velocity.x + delta.x;
        aim.x = x + position.x;
        aim.y = position.y + (velocity.y * lead + delta.y);
        aim.z = position.z + (velocity.z * lead + delta.z);
    }
    out[0] = velocity.x + aim.x - position.x;
    out[1] = velocity.y + aim.y - position.y;
    out[2] = velocity.z + aim.z - position.z;
}

void __cdecl AISteerSeperation(float *out, void *mine, void *theirs, float radius, float reach);
void __fastcall WRoadNavUpdateOccludedPosition(char *self, void *, bool punch);

void __cdecl AISteerVehicleSeperation(float *out, void *vehicle, const char *neighbors, float radius, float reach)
{
    void *mine = Find(vehicle, 0x4039d0);
    if (!mine)
        return;
    char *head = At<char *>(const_cast<char *>(neighbors), 4);
    for (char *node = At<char *>(head, 0); node != head; node = At<char *>(node, 0))
    {
        char *avoidable = At<char *>(node, 8);
        void *other = At<void *>(avoidable, 0x14);
        if (!other)
            continue;
        void *theirs = Find(other, 0x4039d0);
        if (!theirs || !Find(At<void *>(avoidable, 0x14), 0x4040e0))
            continue;
        float push[3] = {kZero[0], kZero[1], kZero[2]};
        AISteerSeperation(push, mine, theirs, radius, reach);
        out[0] = push[0] + out[0];
        out[1] = push[1] + out[1];
        out[2] = push[2] + out[2];
    }
}

void __fastcall AIActionRamUpdate(char *self, void *, float dt)
{
    void *ai = At<void *>(self, 0x48);
    static const unsigned int ramGoal = stringhash32(reinterpret_cast<const char *>(0x891628));
    const bool ramming = *Virtual<const unsigned int *>(ai, 0xc0) == ramGoal;
    float seek[3];
    AIActionRamGetSeekPosition(self, nullptr, seek, ramming);
    float steer[3];
    AIActionRamUpdateSeek(self, nullptr, steer, seek, ramming);
    Vec3 goal = {steer[0], steer[1], steer[2]};
    float aggression = 0.0f;
    if (ramming)
    {
        char *target = Virtual<char *>(ai, 0x64);
        if (void *vehicle = At<void *>(target, 0x1c))
        {
            if (void *perp = Find(vehicle, 0x4037e0))
                aggression = Attribute(Virtual<void *>(perp, 0x10), 0x594e1492);
        }
        if (Virtual<void *>(ai, 0xa0) && Virtual<bool>(Virtual<void *>(ai, 0xa0), 0x124))
            aggression = 1.0f;
        const float radius = (1.0f - aggression) * 4.0f;
        Vec3 separation = {0.0f, 0.0f, 0.0f};
        void *neighbors = Virtual<void *>(ai, 0x6c);
        AISteerVehicleSeperation(&separation.x, At<void *>(self, 0x50), static_cast<const char *>(neighbors), radius,
                                 radius + 1.5f);
        const float along = separation.z * goal.z + goal.y * separation.y + separation.x * goal.x;
        const float closing = -(along / sqrtf(goal.z * goal.z + goal.y * goal.y + goal.x * goal.x));
        if (closing > Constant(0x891218))
        {
            const float blend = (Constant(0x891624) - closing) * Constant(0x891620);
            float weight;
            if (!(blend > 0.0f))
                weight = 0.0f;
            else
                weight = !(1.0f < blend) ? blend : 1.0f;
            const float projection =
                along / (separation.z * separation.z + separation.y * separation.y + separation.x * separation.x);
            const Vec3 onto = {separation.x * projection, separation.y * projection, separation.z * projection};
            goal.x = separation.x + ((goal.x - onto.x) + onto.x * weight);
            goal.y = separation.y + ((goal.y - onto.y) + onto.y * weight);
            goal.z = separation.z + ((goal.z - onto.z) + onto.z * weight);
        }
        else
        {
            goal.x = separation.x + goal.x;
            goal.y = goal.y + separation.y;
            goal.z = separation.z + goal.z;
        }
    }
    float speed = sqrtf(goal.z * goal.z + goal.y * goal.y + goal.x * goal.x);
    char *chased = Virtual<char *>(Virtual<void *>(ai, 0xa0), 8);
    if (void *vehicle = At<void *>(chased, 0x1c))
    {
        if (void *other = Find(vehicle, 0x403700))
        {
            const float current = Virtual<float>(At<void *>(self, 0x50), 0x90);
            float acceleration = Attribute(Virtual<void *>(ai, 0x94), 0xec57e16b);
            float braking = Attribute(Virtual<void *>(ai, 0x94), 0xcc320329);
            float top = At<float>(At<char *>(Virtual<char *>(ai, 0x94), 8), 8) * Constant(0x89098c);
            if (Virtual<bool>(Virtual<void *>(ai, 0xa0), 0x124))
            {
                acceleration *= Constant(0x890e04);
                braking *= 1.5f;
                top *= Constant(0x8910c4);
            }
            const float reachable = Virtual<float>(other, 0x74) * acceleration;
            if (!(top < reachable))
                top = reachable;
            const float slowing = Virtual<float, float>(other, 0x78, current) * braking;
            NFS_ORIGINAL(0x40a5e0, void(__thiscall *)(void *, float, float, float, float))(self + 0x64, current, top,
                                                                                          slowing, dt);
            const float limit = At<float>(self, 0x64);
            if (!(speed < limit))
                speed = limit;
        }
    }
    if (ramming)
    {
        const float distance = sqrtf(steer[2] * steer[2] + steer[1] * steer[1] + steer[0] * steer[0]);
        const float contact = (1.0f - aggression) * Constant(0x89161c) + Constant(0x8910c0);
        if (contact > distance)
        {
            void *input = At<void *>(self, 0x5c);
            Virtual<void, unsigned int>(input, 0x18, 0);
            Virtual<void, unsigned int>(input, 0x1c, kOne);
            Virtual<void, unsigned int>(input, 0x14, 0);
            Virtual<void, unsigned int>(input, 0x2c, 0);
            Virtual<void, unsigned int>(input, 0x24, kOne);
            Virtual<void, unsigned int>(input, 0x20, 0);
            return;
        }
    }
    Virtual<void, float>(ai, 0x38, speed);
    const float *position = Virtual<const float *>(At<void *>(self, 0x4c), 0x20);
    goal.x += position[0];
    goal.y += position[1];
    goal.z += position[2];
    Virtual<void, const float *>(ai, 0x3c, &goal.x);
    Virtual<void, int>(ai, 0x2c, 7);
    Virtual<void, unsigned int>(At<void *>(self, 0x5c), 0x20, 0);
}

void *__fastcall AIActionStaticRoadBlockConstruct(char *self, void *, void **params, float interval)
{
    ConstructAction(self, params, interval);
    At<unsigned int>(self, 0) = 0x8914f8;
    At<unsigned int>(self, 8) = 0x8914f0;
    At<void *>(self, 0x48) = Find(*params, 0x4040e0);
    At<void *>(self, 0x4c) = Find(*params, 0x403f90);
    return self;
}

void *__cdecl AIActionStaticRoadBlockFactory(void **params)
{
    void *memory = AllocateAction(0x50);
    return memory ? AIActionStaticRoadBlockConstruct(static_cast<char *>(memory), nullptr, params, 1.0f) : nullptr;
}

void *__fastcall AIActionHeliExitConstruct(char *self, void *, void **params, float interval)
{
    ConstructAction(self, params, interval);
    At<unsigned int>(self, 0) = 0x891388;
    At<unsigned int>(self, 8) = 0x891380;
    At<float>(self, 0x5c) = 0.0f;
    At<unsigned char>(self, 0x60) = 0;
    At<int>(self, 0x70) = 0;
    At<void *>(self, 0x48) = Find(*params, 0x403700);
    At<void *>(self, 0x58) = Find(*params, 0x4038e0);
    At<void *>(self, 0x4c) = Find(*params, 0x4040e0);
    At<void *>(self, 0x54) = Find(*params, 0x404060);
    At<void *>(self, 0x50) = Virtual<void *>(*params, 0x54);
    return self;
}

void *__cdecl AIActionHeliExitFactory(void **params)
{
    void *memory = AllocateAction(0x74);
    return memory ? AIActionHeliExitConstruct(static_cast<char *>(memory), nullptr, params, 0.0f) : nullptr;
}

void __fastcall AIActionHeliExitUpdate(char *self, void *, float dt)
{
    At<float>(self, 0x5c) = dt + At<float>(self, 0x5c);
    void *player = *reinterpret_cast<int *>(0x92d884) ? **reinterpret_cast<void ***>(0x92d87c) : nullptr;
    void *body = Virtual<void *>(Virtual<void *>(player, 4), 0x54);
    const Vec3 playerPosition = *Virtual<const Vec3 *>(body, 0x20);
    Vec3 mine = *Virtual<const Vec3 *>(At<void *>(self, 0x50), 0x20);
    Vec3 forward;
    Virtual<void, float *>(body, 0x34, &forward.x);
    Vec3 &destination = At<Vec3>(self, 0x64);
    const float altitude = *reinterpret_cast<const float *>(0x8eb1f8);
    switch (At<int>(self, 0x70))
    {
    case 0:
        if (mine.y - playerPosition.y > 15.0f)
        {
            destination.x = forward.x * Constant(0x891610) + playerPosition.x;
            destination.y = forward.y * Constant(0x891610) + playerPosition.y;
            destination.z = forward.z * Constant(0x891610) + playerPosition.z;
            At<int>(self, 0x70) = 1;
            destination.y = altitude + destination.y;
        }
        break;
    case 1: {
        const float dz = destination.z - mine.z;
        const float dy = destination.y - mine.y;
        const float dx = destination.x - mine.x;
        if (dx * dx + dy * dy + dz * dz < 25.0f)
        {
            Virtual<void, float *>(body, 0x38, &mine.x);
            const float *velocity = Virtual<const float *>(At<void *>(self, 0x50), 0x24);
            const float side = mine.z * velocity[2] + mine.y * velocity[1] + mine.x * velocity[0];
            const float swing = side < 0.0f ? 3.0f : Constant(0x891618);
            forward.x = mine.x * swing + forward.x;
            forward.y = mine.y * swing + forward.y;
            forward.z = mine.z * swing + forward.z;
            destination.x = forward.x * Constant(0x891614) + playerPosition.x;
            destination.y = forward.y * Constant(0x891614) + playerPosition.y;
            destination.z = forward.z * Constant(0x891614) + playerPosition.z;
            At<int>(self, 0x70) = 2;
            destination.y = altitude + playerPosition.y + 5.0f;
        }
        break;
    }
    default:
        break;
    }
    void *heli = At<void *>(self, 0x54);
    Virtual<void, Vec3>(heli, 0xc, destination);
    Virtual<void, unsigned int>(At<void *>(self, 0x48), 0x38, 0x42c80000);
    Virtual<void, const float *>(heli, 0x14, Virtual<const float *>(At<void *>(self, 0x50), 0x24));
    Virtual<void, const float *>(At<void *>(self, 0x48), 0x3c, &destination.x);
    Virtual<void, int>(At<void *>(self, 0x48), 0x2c, 7);
}

namespace
{
void Unit(const float *from, float *to)
{
    NFS_ORIGINAL(0x40fe20, void(__cdecl *)(const float *, float *))(from, to);
}

void *PlayerBody()
{
    void *player = *reinterpret_cast<int *>(0x92d884) ? **reinterpret_cast<void ***>(0x92d87c) : nullptr;
    return Virtual<void *>(Virtual<void *>(player, 4), 0x54);
}

void *PursuitSpeech()
{
    char *sound = *reinterpret_cast<char **>(0x993cc8);
    return sound ? At<void *>(sound, 0xe0) : nullptr;
}

unsigned char &HeliSkidHitting()
{
    return *reinterpret_cast<unsigned char *>(0x90d621);
}
} // namespace

void __fastcall AIActionHeliPursuitSetNextPerpSearchDest(char *self)
{
    void *ai = At<void *>(self, 0x4c);
    void *vehicle = Virtual<void *>(ai, 0xa0);
    if (!vehicle)
        return;
    const float *centre = Virtual<const float *>(vehicle, 0x38);
    char *target = Virtual<char *>(ai, 0x64);
    void *perp = At<void *>(target, 0x1c) ? Find(At<void *>(target, 0x1c), 0x4037e0) : nullptr;
    if (!perp)
        return;
    char *instance = Virtual<char *>(perp, 0x10);
    if (!instance)
        return;
    const float range = At<float>(At<char *>(instance, 8), 0xc0);
    const int roll = NFS_ORIGINAL(0x627320, int(__thiscall *)(void *))(
        NFS_ORIGINAL(0x6ed200, void *(__cdecl *)())());
    const float radius = static_cast<float>(roll) * (range * Constant(0x89107c)) * Constant(0x890980) +
                         range * Constant(0x890864);
    float turn = At<float>(self, 0x70) + 0.125f;
    At<float>(self, 0x70) = turn;
    if (!(turn < 1.0f))
        At<float>(self, 0x70) = turn - 1.0f;
    const float angle = At<float>(self, 0x70) * Constant(0x890510);
    float c;
    float s;
    __asm {
        fld angle
        fsincos
        fstp c
        fstp s
    }
    Vec3 &destination = At<Vec3>(self, 0x74);
    destination.y = 0.0f;
    destination.x = c * radius - s * 0.0f;
    destination.z = s * radius + c * 0.0f;
    destination.x = centre[0] + destination.x;
    destination.y = destination.y + centre[1];
    destination.z = destination.z + centre[2];
    Virtual<void, float *>(At<void *>(self, 0x58), 0x28, &destination.x);
    destination.y += 5.0f;
}

void __fastcall AIActionHeliPursuitSearchForPerp(char *self)
{
    const Vec3 heli = *Virtual<const Vec3 *>(At<void *>(self, 0x54), 0x20);
    const Vec3 &destination = At<Vec3>(self, 0x74);
    const float dz = heli.z - destination.z;
    const float dx = heli.x - destination.x;
    if (sqrtf(dx * dx + dz * dz) < 30.0f)
        AIActionHeliPursuitSetNextPerpSearchDest(self);
    Virtual<void, unsigned int>(At<void *>(self, 0x4c), 0x38, 0x428c0000);
    const Vec3 velocity = *Virtual<const Vec3 *>(At<void *>(self, 0x54), 0x24);
    Virtual<void, const float *>(At<void *>(self, 0x58), 0x14, &velocity.x);
    Virtual<void, const float *>(At<void *>(self, 0x4c), 0x3c, &destination.x);
    Virtual<void, Vec3>(At<void *>(self, 0x58), 0xc, destination);
    Virtual<void, int>(At<void *>(self, 0x4c), 0x2c, 7);
}

void __fastcall AIActionHeliPursuitStraightLinePursuit(char *self)
{
    void *ai = At<void *>(self, 0x4c);
    void *perpBody = At<void *>(self, 0x80);
    const Vec3 &perp = At<Vec3>(self, 0x84);
    const Vec3 heli = *Virtual<const Vec3 *>(At<void *>(self, 0x54), 0x20);
    Vec3 heading = *Virtual<const Vec3 *>(perpBody, 0x24);
    Unit(&heading.x, &heading.x);
    const Vec3 delta = {heli.x - perp.x, heli.y - perp.y, heli.z - perp.z};
    const bool lingering =
        Virtual<bool>(Virtual<void *>(ai, 0xa0), 0x11c) && At<float>(self, 0x64) < Constant(0x890db8);
    float distance = sqrtf(delta.z * delta.z + delta.x * delta.x + delta.y * delta.y);
    if (distance < 35.0f && distance > 5.0f &&
        (heading.z * delta.z + heading.x * delta.x) / distance > Constant(0x891058) && ai && lingering &&
        delta.y < 13.0f)
    {
        At<float>(self, 0x64) = 8.0f;
        At<int>(self, 0xa4) = 2;
        At<Vec3>(self, 0x90) = *reinterpret_cast<const Vec3 *>(kZero);
        char *sound = *reinterpret_cast<char **>(0x993cc8);
        if (sound && At<void *>(sound, 0xe0))
        {
            const int coin = NFS_ORIGINAL(0x45da30, int(__cdecl *)(int))(2);
            Virtual<void, int>(At<void *>(sound, 0xe0), 0x90, coin ? 2 : 4);
        }
    }
    Vec3 forward;
    Virtual<void, float *>(perpBody, 0x34, &forward.x);
    Unit(&forward.x, &forward.x);
    float lead = At<float>(self, 0xa0) * Constant(0x891054) + 30.0f;
    if (lead > 45.0f)
        lead = 45.0f;
    if (lingering)
        lead *= 0.75f;
    Vec3 aim = {forward.x * lead + perp.x, forward.y * lead + perp.y, forward.z * lead + perp.z};
    float height;
    if (At<float>(self, 0x64) > 0.0f)
        height = aim.y + 2.0f;
    else if (distance > 55.0f || At<float>(self, 0x64) > Constant(0x890db8))
        height = aim.y + 12.0f;
    else
        height = aim.y + 6.0f;
    Vec3 toward = {aim.x - heli.x, 0.0f, aim.z - heli.z};
    distance = Normalize(&toward.x);
    const float *velocity = Virtual<const float *>(perpBody, 0x24);
    float closing = toward.z * velocity[2] + toward.y * velocity[1] + toward.x * velocity[0];
    if (closing < 0.0f)
        closing *= 0.5f;
    const Vec3 *hover = &aim;
    float speed;
    if (distance > 15.0f)
        speed = distance > 300.0f ? 50.0f : 30.0f;
    else
    {
        speed = distance > 3.0f ? 20.0f : distance;
        if (distance < 5.0f && At<float>(self, 0xa0) < 15.0f)
            hover = &perp;
    }
    Virtual<void, float>(ai, 0x38, speed + closing);
    Virtual<void, const float *>(At<void *>(self, 0x58), 0x14, Virtual<const float *>(perpBody, 0x24));
    aim.y = height;
    Virtual<void, const float *>(ai, 0x3c, &aim.x);
    Virtual<void, Vec3>(At<void *>(self, 0x58), 0xc, *hover);
    Virtual<void, int>(ai, 0x2c, 7);
}

void __fastcall AIActionHeliPursuitSkidHitPursuit(char *self)
{
    void *ai = At<void *>(self, 0x4c);
    void *perpBody = At<void *>(self, 0x80);
    const Vec3 &perp = At<Vec3>(self, 0x84);
    Vec3 &swing = At<Vec3>(self, 0x90);
    const Vec3 heli = *Virtual<const Vec3 *>(At<void *>(self, 0x54), 0x20);
    const Vec3 delta = {heli.x - perp.x, heli.y - perp.y, heli.z - perp.z};
    Vec3 forward;
    Virtual<void, float *>(perpBody, 0x34, &forward.x);
    const Vec3 velocity = *Virtual<const Vec3 *>(perpBody, 0x24);
    Vec3 right;
    Virtual<void, float *>(perpBody, 0x38, &right.x);
    const float ahead = forward.z * delta.z + delta.x * forward.x + forward.y * delta.y;
    const float distanceSquared = delta.z * delta.z + delta.x * delta.x + delta.y * delta.y;
    const float side = right.z * delta.z + right.x * delta.x + right.y * delta.y;
    Vec3 goal;
    if (At<int>(self, 0xa4) == 2)
    {
        const float reach = side < 0.0f ? Constant(0x891078) : 6.0f;
        swing.x = right.x * reach;
        swing.y = right.y * reach;
        swing.z = right.z * reach;
        const float x = velocity.x * Constant(0x89064c) + swing.x;
        float y = velocity.y * Constant(0x89064c) + swing.y;
        const float z = velocity.z * Constant(0x89064c) + swing.z;
        y += Constant(0x891074);
        if (delta.y < 2.0f)
            y += 3.0f;
        goal.x = x + perp.x;
        goal.y = y + perp.y;
        goal.z = z + perp.z;
        const float gz = heli.z - goal.z;
        const float gx = heli.x - goal.x;
        if (sqrtf(gx * gx + gz * gz) < 4.0f)
        {
            const float across = side < 0.0f ? -side : side;
            if (across > Constant(0x891070))
            {
                At<int>(self, 0xa4) = 3;
                if (At<float>(self, 0x64) < 2.0f)
                    At<float>(self, 0x64) = 2.0f;
                if (void *speech = PursuitSpeech())
                    Virtual<void>(speech, 0xf8);
            }
        }
    }
    else
    {
        const float x = swing.x * Constant(0x891068) + velocity.x * Constant(0x89106c);
        const float y = swing.y * Constant(0x891068) + velocity.y * Constant(0x89106c) + Constant(0x891054);
        const float z = swing.z * Constant(0x891068) + velocity.z * Constant(0x89106c);
        goal.x = x + perp.x;
        goal.y = y + perp.y;
        goal.z = z + perp.z;
    }
    const Vec3 lookAt = {velocity.x + perp.x, velocity.y + perp.y, velocity.z + perp.z};
    Virtual<void, Vec3>(At<void *>(self, 0x58), 0xc, lookAt);
    Virtual<void, unsigned int>(ai, 0x38, 0x42c80000);
    const Vec3 current = *Virtual<const Vec3 *>(perpBody, 0x24);
    Virtual<void, const float *>(At<void *>(self, 0x58), 0x14, &current.x);
    Virtual<void, const float *>(ai, 0x3c, &goal.x);
    Virtual<void, int>(ai, 0x2c, 7);
    if (ahead > 0.0f)
    {
        if (distanceSquared > Constant(0x891064))
            At<float>(self, 0x64) = 0.0f;
        if (heli.y < perp.y)
            At<float>(self, 0x64) = 0.0f;
    }
    else if (distanceSquared > 144.0f)
        At<float>(self, 0x64) = 0.0f;
    if (At<float>(self, 0x64) <= 0.0f)
        At<int>(self, 0xa4) = 0;
}

void __fastcall AIActionHeliPursuitUpdate(char *self, void *, float dt)
{
    At<float>(self, 0x60) = dt + At<float>(self, 0x60);
    At<float>(self, 0x64) = At<float>(self, 0x64) - dt;
    if (At<int>(self, 0x9c))
        --At<int>(self, 0x9c);
    void *body = PlayerBody();
    At<void *>(self, 0x80) = body;
    At<Vec3>(self, 0x84) = *Virtual<const Vec3 *>(body, 0x20);
    At<float>(self, 0xa0) = Virtual<float>(At<void *>(self, 0x80), 0x2c);
    void *vehicle = Virtual<void *>(At<void *>(self, 0x4c), 0xa0);
    if (vehicle && !Virtual<bool>(vehicle, 0x28))
    {
        if (At<int>(self, 0xa4) != 1)
        {
            AIActionHeliPursuitSetNextPerpSearchDest(self);
            At<int>(self, 0xa4) = 1;
        }
    }
    else if (At<int>(self, 0xa4) == 1)
        At<int>(self, 0xa4) = 0;
    HeliSkidHitting() = 0;
    switch (At<int>(self, 0xa4))
    {
    case 0:
        AIActionHeliPursuitStraightLinePursuit(self);
        break;
    case 1:
        AIActionHeliPursuitSearchForPerp(self);
        break;
    case 2:
    case 3:
        if (!*reinterpret_cast<const unsigned char *>(0x8eb1f4))
            HeliSkidHitting() = 1;
        AIActionHeliPursuitSkidHitPursuit(self);
        break;
    default:
        break;
    }
}

void __fastcall AIActionHeliPursuitOnCollision(char *listener, void *, const char *info)
{
    char *self = listener - 0x48;
    if (!(At<float>(self, 0x64) > 0.0f))
        return;
    const int kind = static_cast<int>(At<unsigned int>(const_cast<char *>(info), 0x1c) << 29) >> 29;
    if (kind == 3)
        At<int>(self, 0x9c) += 2;
    else if (kind == 1)
    {
        using FindInstance = void *(__cdecl *)(void *);
        void *first = NFS_ORIGINAL(0x41ad40, FindInstance)(At<void *>(const_cast<char *>(info), 0x3c));
        bool hit = first && Virtual<bool>(first, 0x24);
        if (!hit)
        {
            void *second = NFS_ORIGINAL(0x41ad40, FindInstance)(At<void *>(const_cast<char *>(info), 0x4c));
            hit = second && Virtual<bool>(second, 0x24);
        }
        if (hit)
            At<int>(self, 0x9c) += 5;
    }
    if (At<int>(self, 0x9c) > 20)
        At<float>(self, 0x64) = 0.0f;
}

void *__fastcall AIActionHeliPursuitConstruct(char *self, void *, void **params, float interval)
{
    ConstructAction(self, params, interval);
    At<unsigned int>(self, 0x48) = 0x890d38;
    At<unsigned int>(self, 0) = 0x891358;
    At<unsigned int>(self, 8) = 0x891350;
    At<unsigned int>(self, 0x48) = 0x89134c;
    At<float>(self, 0x60) = 0.0f;
    At<float>(self, 0x64) = 0.0f;
    At<float>(self, 0x68) = 0.0f;
    At<unsigned char>(self, 0x6c) = 0;
    At<float>(self, 0x70) = 0.0f;
    At<Vec3>(self, 0x74) = *reinterpret_cast<const Vec3 *>(kZero);
    At<int>(self, 0x9c) = 0;
    At<float>(self, 0xa0) = 0.0f;
    At<int>(self, 0xa4) = 0;
    At<void *>(self, 0x4c) = Find(*params, 0x403700);
    At<void *>(self, 0x5c) = Find(*params, 0x4038e0);
    At<void *>(self, 0x50) = Find(*params, 0x4040e0);
    At<void *>(self, 0x58) = Find(*params, 0x404060);
    void *body = Virtual<void *>(*params, 0x54);
    At<void *>(self, 0x54) = body;
    NFS_ORIGINAL(0x6fbea0, void(__cdecl *)(void *, void *, const char *))(self + 0x48, body,
                                                                          reinterpret_cast<const char *>(0x891338));
    return self;
}

void *__cdecl AIActionHeliPursuitFactory(void **params)
{
    void *memory = AllocateAction(0xa8);
    return memory ? AIActionHeliPursuitConstruct(static_cast<char *>(memory), nullptr, params, 0.0f) : nullptr;
}

void __fastcall CopRoadAffinity(char *self, void *, float *out);

namespace
{
void ResetTrail(char *nav)
{
    At<int>(nav, 0x78) = 2;
    At<int>(nav, 0x80) = 3;
    NFS_ORIGINAL(0x76e2d0, void(__thiscall *)(void *, int))(nav, 1);
    NFS_ORIGINAL(0x774230, void(__thiscall *)(void *))(nav);
}

void IncNavPosition(char *nav, float distance, const Vec3 *heading)
{
    NFS_ORIGINAL(0x77bba0, void(__thiscall *)(void *, float, const Vec3 *, int))(nav, distance, heading, 0);
}

/** How far a point lies ahead of the navigation along its heading, with the heading and the offset it used. */
float NavAhead(char *nav, const Vec3 &point, Vec3 &heading, Vec3 &offset)
{
    const Vec3 at = At<Vec3>(nav, 0x98);
    heading = At<Vec3>(nav, 0xbc);
    Normalize(&heading.x);
    offset = {point.x - at.x, point.y - at.y, point.z - at.z};
    return offset.z * heading.z + offset.x * heading.x + heading.y * offset.y;
}
} // namespace

void *__fastcall AIActionPursuitOffRoadConstruct(char *self, void *, void **params, float interval)
{
    ConstructAction(self, params, interval);
    At<unsigned int>(self, 0) = 0x89130c;
    At<unsigned int>(self, 8) = 0x891304;
    At<int>(self, 0xa8) = 0;
    At<int>(self, 0xac) = 0;
    At<void *>(self, 0x48) = Find(*params, 0x403700);
    At<void *>(self, 0x54) = Find(*params, 0x4038e0);
    At<void *>(self, 0x50) = Find(*params, 0x4040e0);
    At<void *>(self, 0x58) = Find(*params, 0x404010);
    At<void *>(self, 0x5c) = Find(*params, 0x403f90);
    At<void *>(self, 0x4c) = Virtual<void *>(*params, 0x54);
    int &count = *reinterpret_cast<int *>(0x90d97c);
    ++count;
    At<unsigned char>(self, 0xb4) = static_cast<unsigned char>(count & 1);
    At<unsigned char>(self, 0xbc) = 0;
    At<float>(self, 0xb8) = -1.0f;
    return self;
}

void *__cdecl AIActionPursuitOffRoadFactory(void **params)
{
    void *memory = AllocateAction(0xc0);
    return memory ? AIActionPursuitOffRoadConstruct(static_cast<char *>(memory), nullptr, params, 0.01f) : nullptr;
}

void __fastcall AIActionPursuitOffRoadOnBehaviorChange(char *self, void *, const unsigned int *behavior)
{
    if (*behavior == *kBehaviorSimable)
        At<void *>(self, 0x4c) = Find(At<void *>(self, 0x30), 0x4039f0);
    if (*behavior == *kBehaviorTarget)
        At<void *>(self, 0x58) = Find(At<void *>(self, 0x30), 0x404010);
}

void __fastcall AIActionPursuitOffRoadBeginAction(char *self, void *, float)
{
    void *ai = At<void *>(self, 0x48);
    if (!(Virtual<float>(ai, 0x84) > 0.0f))
        Virtual<void, unsigned int>(At<void *>(self, 0x50), 0x94, 0x41d6930c);
    ResetTrail(Virtual<char *>(ai, 0x48));
    Virtual<void, int>(At<void *>(self, 0x48), 0x50, 1);
    char *target = Virtual<char *>(At<void *>(self, 0x48), 0x64);
    float speed = 0.0f;
    if (void *vehicle = At<void *>(target, 0x1c))
        speed = Virtual<float>(Virtual<void *>(vehicle, 0x54), 0x30);
    At<int>(self, 0xac) = 0;
    At<float>(self, 0xa4) = speed;
    At<int>(self, 0xa8) = 0;
    for (int offset = 0x64; offset <= 0xa0; offset += 4)
        At<float>(self, offset) = speed;
    At<float>(self, 0xb0) = Virtual<float>(At<void *>(self, 0x50), 0x90);
}

void __fastcall AIActionPursuitOffRoadUpdateAvoidWalls(char *self, void *, float *out)
{
    void *body = At<void *>(self, 0x4c);
    const Vec3 velocity = *Virtual<const Vec3 *>(body, 0x24);
    const float speed = sqrtf(velocity.z * velocity.z + velocity.y * velocity.y + velocity.x * velocity.x);
    const Vec3 position = *Virtual<const Vec3 *>(body, 0x20);
    if (speed < 2.0f)
        return;
    float look = speed + 10.0f;
    if (Constant(0x891040) < look)
        look = Constant(0x891040);
    const float inverse = 1.0f / speed;
    const float reach = look * inverse;
    struct Segment
    {
        float x, y, z, w;
    } segments[2] = {{position.x, position.y, position.z, 1.0f},
                     {velocity.x * reach + position.x, velocity.y * reach + position.y,
                      velocity.z * reach + position.z, 1.0f}};
    alignas(16) unsigned char info[0x60];
    NFS_ORIGINAL(0x4048c0, void *(__thiscall *)(void *))(info);
    unsigned int manager[2] = {0, 3};
    if (!NFS_ORIGINAL(0x7854b0, int(__thiscall *)(void *, const Segment *, void *, int))(manager, segments, info, 2))
        return;
    const Vec3 hit = *reinterpret_cast<const Vec3 *>(info);
    const Vec3 normal = *reinterpret_cast<const Vec3 *>(info + 0x10);
    const float dx = hit.x - position.x;
    const float dy = hit.y - position.y;
    const float dz = hit.z - position.z;
    const float time = sqrtf(dx * dx + dy * dy + dz * dz) * inverse;
    const float closing = (normal.z * velocity.z + normal.y * velocity.y + normal.x * velocity.x) * inverse;
    if (closing >= 0.0f)
        return;
    const float push = closing * closing * Constant(0x89103c) / (time * time);
    out[0] = normal.x * push;
    out[1] = normal.y * push;
    out[2] = normal.z * push;
}

void __fastcall AIActionPursuitOffRoadUpdateSeek(char *self, void *, float *out)
{
    void *body = At<void *>(self, 0x4c);
    const Vec3 position = *Virtual<const Vec3 *>(body, 0x20);
    float facing[4];
    Virtual<void, float *>(body, 0x34, facing);
    void *ai = At<void *>(self, 0x48);
    char *target = Virtual<char *>(ai, 0x64);
    const Vec3 targetPosition = At<Vec3>(target, 0x10);
    const Vec3 velocity = *reinterpret_cast<const Vec3 *>(TargetVelocity(target));
    Vec3 heading = velocity;
    void *vehicle = At<void *>(target, 0x1c);
    if (vehicle && !(Virtual<float>(Virtual<void *>(vehicle, 0x54), 0x30) < 1.0f))
        Normalize(&heading.x);
    else
        heading = At<Vec3>(target, 0x20);
    void *perp = vehicle ? Find(vehicle, 0x4037e0) : nullptr;
    void *chased = vehicle ? Find(vehicle, 0x403700) : nullptr;
    char *front = Virtual<char *>(chased, 0xb4);
    char *back = Virtual<char *>(chased, 0xb8);
    Vec3 from;
    Vec3 to;
    Vec3 axis;
    if (At<unsigned char>(front, 0x50) && At<unsigned char>(back, 0x50))
    {
        from = At<Vec3>(front, 0x98);
        to = At<Vec3>(back, 0x98);
        axis = {to.x - from.x, to.y - from.y, to.z - from.z};
    }
    else
    {
        from = targetPosition;
        to = {targetPosition.x + velocity.x, velocity.y + targetPosition.y, velocity.z + targetPosition.z};
        axis = heading;
    }
    const Vec3 offset = *Virtual<const Vec3 *>(At<void *>(self, 0x54), 0x44);
    Vec3 side = {axis.z, 0.0f, -axis.x};
    Normalize(&side.x);
    Normalize(&axis.x);
    const Vec3 back3 = {offset.z * axis.x, axis.y * offset.z, offset.z * axis.z};
    const Vec3 side3 = {side.x * offset.x, side.y * offset.x, side.z * offset.x};
    const Vec3 nearSlot = {from.x + back3.x + side3.x, from.y + back3.y + side3.y, from.z + back3.z + side3.z};
    const Vec3 farSlot = {to.x + back3.x + side3.x, to.y + back3.y + side3.y, to.z + back3.z + side3.z};
    const unsigned int lead = *static_cast<const unsigned int *>(
        NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
            Virtual<void *>(perp, 0x10), 0x0f575b64, 0));
    const float reach = NFS_ORIGINAL(0x41d130, float(__thiscall *)(void *, unsigned int))(self + 0x60, lead);
    Vec3 span = {farSlot.x - nearSlot.x, farSlot.y - nearSlot.y, farSlot.z - nearSlot.z};
    float spanLength = sqrtf(span.x * span.x + span.z * span.z + span.y * span.y);
    if (spanLength > Constant(0x8910cc))
    {
        const float scale = reach / spanLength;
        spanLength = reach;
        span.x *= scale;
        span.y *= scale;
        span.z *= scale;
    }
    Vec3 ahead = velocity;
    const float aheadLength = sqrtf(velocity.x * velocity.x + velocity.z * velocity.z + velocity.y * velocity.y);
    if (aheadLength > Constant(0x8910cc))
    {
        const float scale = reach / aheadLength;
        ahead.x *= scale;
        ahead.y *= scale;
        ahead.z *= scale;
    }
    Vec3 toSlot = {nearSlot.x - position.x, nearSlot.y - position.y, nearSlot.z - position.z};
    const float slotDistance = sqrtf(toSlot.x * toSlot.x + toSlot.z * toSlot.z + toSlot.y * toSlot.y);
    if (slotDistance > Constant(0x8910cc))
    {
        const float inverse = 1.0f / slotDistance;
        const float cosine = (toSlot.z * axis.z + toSlot.y * axis.y + toSlot.x * axis.x) * inverse;
        float near = reach * Constant(0x890864);
        if (Constant(0x891334) > near)
            near = Constant(0x891334);
        float shrink = inverse * near;
        if (1.0f < shrink)
            shrink = 1.0f;
        const float tenfold = cosine * 10.0f;
        float facingAway;
        if (!(tenfold > -1.0f))
            facingAway = -1.0f;
        else
            facingAway = 1.0f < tenfold ? 1.0f : tenfold;
        const float mix = (facingAway + 1.0f) * 0.5f;
        const float squeeze = shrink * (1.0f - mix) + mix * Constant(0x891074);
        if (!(spanLength < Constant(0x8910cc)))
        {
            const float along =
                (toSlot.x * span.x + toSlot.z * span.z + toSlot.y * span.y) / (spanLength * spanLength);
            const Vec3 part = {span.x * along, span.y * along, span.z * along};
            toSlot.x = part.x * squeeze + (toSlot.x - part.x);
            toSlot.y = part.y * squeeze + (toSlot.y - part.y);
            toSlot.z = part.z * squeeze + (toSlot.z - part.z);
        }
    }
    const float blend = *At<float *>(Virtual<char *>(ai, 0x94), 8) * Constant(0x890dc4);
    Vec3 goal = {(ahead.x - span.x) * blend + span.x + toSlot.x + position.x,
                 (ahead.y - span.y) * blend + span.y + toSlot.y + position.y,
                 (ahead.z - span.z) * blend + span.z + toSlot.z + position.z};
    bool resetTrail = true;
    char *nav = Virtual<char *>(At<void *>(self, 0x48), 0x48);
    At<unsigned char>(nav, 0x51) =
        Virtual<bool>(perp, 0x24) && *reinterpret_cast<const unsigned char *>(0x9b3829) ? 1 : 0;
    if (At<unsigned char>(nav, 0x50))
    {
        At<int>(nav, 0x80) = Virtual<float>(perp, 4) >= 3.0f ? 4 : 3;
        Vec3 navHeading;
        Vec3 navOffset;
        float navAhead = NavAhead(nav, goal, navHeading, navOffset);
        if (navAhead >= Constant(0x890db8))
        {
            Vec3 direction = axis;
            Normalize(&direction.x);
            while (navAhead > 5.0f)
            {
                IncNavPosition(nav, 5.0f, &direction);
                navAhead = NavAhead(nav, goal, navHeading, navOffset);
            }
            const float step = Constant(0x890cd4) > navAhead ? Constant(0x890cd4) : navAhead;
            Vec3 lateral = {navHeading.z, 0.0f, -navHeading.x};
            Normalize(&lateral.x);
            const float wanted = lateral.z * navOffset.z + lateral.y * navOffset.y + lateral.x * navOffset.x +
                                 At<float>(nav, 0x2c4);
            const float lane = NFS_ORIGINAL(0x7749a0, float(__thiscall *)(void *, float))(nav, wanted);
            if (fabsf(lane - wanted) < 4.0f)
            {
                resetTrail = false;
                NFS_ORIGINAL(0x778250, void(__thiscall *)(void *, float, int))(nav, lane, 0);
                IncNavPosition(nav, step, &direction);
                WRoadNavUpdateOccludedPosition(static_cast<char *>(nav), nullptr, true);
                const Vec3 *spot = &At<Vec3>(nav, 0x98);
                if (At<unsigned char>(nav, 0x55) && (At<void *>(nav, 0x58) || At<void *>(nav, 0x5c)))
                    spot = &At<Vec3>(nav, 0x164);
                Vec3 toSpot = {spot->x - position.x, spot->y - position.y, spot->z - position.z};
                const float spotDistance = Length(&toSpot.x);
                if (spotDistance > Constant(0x8910cc))
                {
                    const float scale = Distance(&goal.x, &position.x) / spotDistance;
                    toSpot.x *= scale;
                    toSpot.y *= scale;
                    toSpot.z *= scale;
                }
                goal = {toSpot.x + position.x, toSpot.y + position.y, toSpot.z + position.z};
            }
        }
    }
    out[0] = goal.x - position.x;
    out[1] = goal.y - position.y;
    out[2] = goal.z - position.z;
    if (resetTrail)
    {
        ResetTrail(nav);
        Virtual<void, int>(At<void *>(self, 0x48), 0x50, 2);
    }
}

void __fastcall AIActionPursuitOffRoadUpdate(char *self, void *, float dt)
{
    void *ai = At<void *>(self, 0x48);
    char *target = Virtual<char *>(ai, 0x64);
    float targetSpeed = 0.0f;
    if (void *vehicle = At<void *>(target, 0x1c))
        targetSpeed = Virtual<float>(Virtual<void *>(vehicle, 0x54), 0x30);
    NFS_ORIGINAL(0x41cf40, void(__thiscall *)(void *, float, float))(self + 0x60, targetSpeed, dt);
    const unsigned char wasStopped = At<unsigned char>(self, 0xbc);
    if (void *vehicle = At<void *>(target, 0x1c))
    {
        if (void *input = Find(vehicle, 0x403f90))
            At<unsigned char>(self, 0xbc) = At<unsigned char>(Virtual<char *>(input, 8), 0x21);
    }
    Vec3 avoid = *reinterpret_cast<const Vec3 *>(kZero);
    AIActionPursuitOffRoadUpdateAvoidWalls(self, nullptr, &avoid.x);
    Vec3 seek = *reinterpret_cast<const Vec3 *>(kZero);
    AIActionPursuitOffRoadUpdateSeek(self, nullptr, &seek.x);
    const float seekLength = sqrtf(seek.z * seek.z + seek.y * seek.y + seek.x * seek.x);
    float limit = At<float>(At<char *>(Virtual<char *>(ai, 0x94), 8), 8) * Constant(0x89098c);
    char *chased = Virtual<char *>(Virtual<void *>(ai, 0xa0), 8);
    if (void *vehicle = At<void *>(chased, 0x1c))
    {
        if (void *other = Find(vehicle, 0x403700))
        {
            const float current = Virtual<float>(At<void *>(self, 0x50), 0x90);
            float acceleration = Attribute(Virtual<void *>(ai, 0x94), 0xec57e16b);
            float braking = Attribute(Virtual<void *>(ai, 0x94), 0xcc320329);
            if (Virtual<void *>(ai, 0xa0) && Virtual<bool>(Virtual<void *>(ai, 0xa0), 0x124))
            {
                acceleration *= Constant(0x890e04);
                braking *= 1.5f;
                limit *= Constant(0x8910c4);
            }
            const float reachable = Virtual<float>(other, 0x74) * acceleration;
            if (!(limit < reachable))
                limit = reachable;
            float slowing = Virtual<float, float>(other, 0x78, current) * braking;
            Vec3 forward;
            Virtual<void, float *>(At<void *>(self, 0x4c), 0x34, &forward.x);
            slowing = slowing - forward.y * Constant(0x891608);
            if (void *ground = Find(other, 0x404020))
            {
                if (Virtual<bool>(ground, 0x20))
                    slowing *= Constant(0x890864);
            }
            NFS_ORIGINAL(0x40a5e0, void(__thiscall *)(void *, float, float, float, float))(self + 0xb0, current, limit,
                                                                                          slowing, dt);
            limit = At<float>(self, 0xb0);
        }
    }
    if (seekLength > limit)
    {
        const float scale = limit / seekLength;
        seek.x *= scale;
        seek.y *= scale;
        seek.z *= scale;
    }
    Vec3 separation = *reinterpret_cast<const Vec3 *>(kZero);
    AISteerVehicleSeperation(&separation.x, At<void *>(self, 0x50), Virtual<const char *>(ai, 0x6c), 2.7f, 5.3f);
    Vec3 affinity = *reinterpret_cast<const Vec3 *>(kZero);
    CopRoadAffinity(self, nullptr, &affinity.x);
    Vec3 steer = {affinity.x + (separation.x + avoid.x), affinity.y + (separation.y + avoid.y),
                  affinity.z + (separation.z + avoid.z)};
    const float along = steer.z * seek.z + steer.y * seek.y + steer.x * seek.x;
    const float closing = -1.0f / sqrtf(seek.z * seek.z + seek.y * seek.y + seek.x * seek.x) * along;
    if (closing > Constant(0x891218))
    {
        const float blend = (Constant(0x891604) - closing) * Constant(0x891600);
        float weight;
        if (!(blend > 0.0f))
            weight = 0.0f;
        else
            weight = !(1.0f < blend) ? blend : 1.0f;
        const float projection = along / (steer.z * steer.z + steer.y * steer.y + steer.x * steer.x);
        const Vec3 onto = {steer.x * projection, steer.y * projection, steer.z * projection};
        seek.x = (seek.x - onto.x) + onto.x * weight;
        seek.y = (seek.y - onto.y) + onto.y * weight;
        seek.z = (seek.z - onto.z) + onto.z * weight;
    }
    steer.x = steer.x + seek.x;
    steer.y = steer.y + seek.y;
    steer.z = steer.z + seek.z;
    const float *velocity = Virtual<const float *>(At<void *>(self, 0x4c), 0x24);
    const float speed = sqrtf(velocity[0] * velocity[0] + velocity[1] * velocity[1] + velocity[2] * velocity[2]);
    const float wanted = sqrtf(steer.z * steer.z + steer.y * steer.y + steer.x * steer.x);
    if (speed > Constant(0x890cd4) && wanted > Constant(0x890cd4))
    {
        const float cosine =
            (steer.z * velocity[2] + steer.x * velocity[0] + steer.y * velocity[1]) / (wanted * speed);
        float most = Constant(0x8915f8) -
                     (1.0f - cosine) / sqrtf(steer.z * steer.z + steer.y * steer.y + steer.x * steer.x) * speed *
                         Constant(0x8915fc);
        if (Constant(0x8915f4) > most)
            most = Constant(0x8915f4);
        if (most < wanted)
        {
            const float scale = most / wanted;
            steer.x *= scale;
            steer.y *= scale;
            steer.z *= scale;
        }
    }
    Virtual<void, float>(ai, 0x38, sqrtf(steer.z * steer.z + steer.y * steer.y + steer.x * steer.x));
    const float *position = Virtual<const float *>(At<void *>(self, 0x4c), 0x20);
    steer.x += position[0];
    steer.y += position[1];
    steer.z += position[2];
    Virtual<void, const float *>(At<void *>(self, 0x48), 0x3c, &steer.x);
    Virtual<void, int>(At<void *>(self, 0x48), 0x2c, 7);
    void *input = At<void *>(self, 0x5c);
    if (At<float>(self, 0xb8) > 0.0f)
    {
        At<float>(self, 0xb8) = At<float>(self, 0xb8) - dt;
        Virtual<void, unsigned int>(input, 0x20, 0);
        return;
    }
    if (!At<unsigned char>(Virtual<char *>(input, 8), 0x21) && At<unsigned char>(self, 0xbc) == 1 && !wasStopped)
    {
        At<float>(self, 0xb8) = 1.5f;
        return;
    }
    Virtual<void>(At<void *>(self, 0x48), 0x30);
}

void __cdecl AISteerSeparation(float *out, const float *a0, const float *a1, float radiusA, const float *b0,
                               const float *b1, float radiusB);

void __cdecl AISteerSetupCapsule(void *body, float *front, float *back, float *radius)
{
    float extent[4];
    Virtual<void, float *>(body, 0x10, extent);
    alignas(16) float matrix[16];
    Virtual<void, float *>(body, 4, matrix);
    const Vec3 centre = {matrix[12], matrix[13], matrix[14]};
    const float *axis = extent[2] > extent[0] ? &matrix[8] : &matrix[0];
    const float side = extent[2] < extent[0] ? extent[2] : extent[0];
    const float length = extent[2] > extent[0] ? extent[2] : extent[0];
    const float squared = side * side;
    const float cap = squared / length;
    const float reach = length - cap;
    front[0] = axis[0] * reach + centre.x;
    front[1] = axis[1] * reach + centre.y;
    front[2] = axis[2] * reach + centre.z;
    back[0] = axis[0] * -reach + centre.x;
    back[1] = axis[1] * -reach + centre.y;
    back[2] = axis[2] * -reach + centre.z;
    *radius = sqrtf(cap * cap + squared);
}

void __cdecl AISteerSeperation(float *out, void *mine, void *theirs, float radius, float reach)
{
    Vec3 myFront;
    Vec3 myBack;
    float myRadius;
    AISteerSetupCapsule(mine, &myFront.x, &myBack.x, &myRadius);
    Vec3 theirFront;
    Vec3 theirBack;
    float theirRadius;
    AISteerSetupCapsule(theirs, &theirFront.x, &theirBack.x, &theirRadius);
    AISteerSeparation(out, &myFront.x, &myBack.x, myRadius, &theirFront.x, &theirBack.x, theirRadius);
    Vec3 theirVelocity;
    Virtual<void, float *>(theirs, 8, &theirVelocity.x);
    Vec3 myVelocity;
    Virtual<void, float *>(mine, 8, &myVelocity.x);
    float strength = radius;
    const Vec3 myCentre = {(myFront.x + myBack.x) * 0.5f, (myFront.y + myBack.y) * 0.5f, (myFront.z + myBack.z) * 0.5f};
    const Vec3 theirCentre = {(theirFront.x + theirBack.x) * 0.5f, (theirFront.y + theirBack.y) * 0.5f,
                              (theirFront.z + theirBack.z) * 0.5f};
    const float rx = myVelocity.x - theirVelocity.x;
    const float ry = myVelocity.y - theirVelocity.y;
    const float rz = myVelocity.z - theirVelocity.z;
    const float closingSquared = rz * rz + ry * ry + rx * rx;
    if (closingSquared > Constant(0x8910cc) || closingSquared < Constant(0x891108))
    {
        const float time = -(((myCentre.z - theirCentre.z) * rz + (myCentre.y - theirCentre.y) * ry) +
                             (myCentre.x - theirCentre.x) * rx) /
                           closingSquared;
        if (time > Constant(0x890ea8))
        {
            const float mx = myVelocity.x * time + myCentre.x;
            const float my = myVelocity.y * time + myCentre.y;
            const float mz = myVelocity.z * time + myCentre.z;
            const float tx = theirVelocity.x * time + theirCentre.x;
            const float ty = theirVelocity.y * time + theirCentre.y;
            const float tz = theirVelocity.z * time + theirCentre.z;
            const float dx = mx - tx;
            const float dy = my - ty;
            const float dz = mz - tz;
            const float meeting = (dz * dz + dy * dy + dx * dx) * 0.5f + time;
            strength = reach / (meeting * meeting) + radius;
        }
    }
    const float length = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    if (!(length > Constant(0x8910cc)))
        return;
    float scale = Constant(0x891104) / length;
    if (strength < scale)
        scale = strength;
    out[0] *= scale;
    out[1] *= scale;
    out[2] *= scale;
}

void __cdecl AISteerRam(float *out, const float *from, float speed, const float *target, const float *velocity)
{
    Vec3 heading = {velocity[0], 0.0f, velocity[2]};
    Vec3 side = {velocity[2], 0.0f, -velocity[0]};
    const Vec3 delta = {target[0] - from[0], target[1] - from[1], target[2] - from[2]};
    const float flat = sqrtf(heading.z * heading.z + heading.y * heading.y + heading.x * heading.x);
    float time = 0.0f;
    if (flat > Constant(0x8910cc))
    {
        const float inverse = 1.0f / flat;
        heading.x *= inverse;
        heading.y *= inverse;
        const float headingZ = inverse * heading.z;
        side.x *= inverse;
        side.y *= inverse;
        side.z *= inverse;
        const float ahead = delta.z * headingZ + delta.y * heading.y + delta.x * heading.x;
        const float across = delta.z * side.z + delta.y * side.y + delta.x * side.x;
        const float quadratic = flat * flat - speed * speed;
        const float linear = ahead * flat;
        const float constant = across * across + ahead * ahead;
        if (fabsf(quadratic) > Constant(0x8910cc))
        {
            const float discriminant = linear * linear - constant * quadratic;
            if (discriminant >= 0.0f)
            {
                const float root = sqrtf(discriminant);
                const float inverseQuadratic = 1.0f / quadratic;
                const float first = (-linear - root) * inverseQuadratic;
                const float second = (root - linear) * inverseQuadratic;
                if (first >= 0.0f)
                    time = !(second >= 0.0f) ? first
                                         : NFS_ORIGINAL(0x401da0, float(__cdecl *)(float, float))(first, second);
                else if (second >= 0.0f)
                    time = second;
            }
        }
        else if (fabsf(linear) > Constant(0x8910cc))
        {
            const float meet = -(constant / (linear + linear));
            if (meet > 0.0f)
                time = meet;
        }
    }
    const float aimX = time * velocity[0] + target[0];
    const float aimY = time * velocity[1] + target[1];
    const float aimZ = time * velocity[2] + target[2];
    const float rx = aimX - from[0];
    const float ry = aimY - from[1];
    const float rz = aimZ - from[2];
    const float length = sqrtf(rz * rz + ry * ry + rx * rx);
    if (length > Constant(0x8910cc))
    {
        const float scale = speed / length;
        out[0] = rx * scale;
        out[1] = ry * scale;
        out[2] = rz * scale;
        return;
    }
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = 0.0f;
}
