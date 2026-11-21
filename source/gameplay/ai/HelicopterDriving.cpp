/** How the pursuit helicopter flies and looks: seeing the car, following the road, keeping out of buildings and
 * out of the camera (original: zAI AIVehicleHelicopter CanSeeTarget, SteerToNav, CheckHeliSheet, AvoidCamera,
 * OnDriving). */
#include <math.h>

#include "legacy/link.h"

NFS_REPLACE(0x4170d0, AIVehicleHelicopterCanSeeTarget)
NFS_REPLACE(0x417340, AIVehicleHelicopterSteerToNav)
NFS_REPLACE(0x417490, AIVehicleHelicopterCheckHeliSheet)
NFS_REPLACE(0x417790, AIVehicleHelicopterAvoidCamera)
NFS_REPLACE(0x417a20, AIVehicleHelicopterOnDriving)

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

float Normalize(float *vector)
{
    return NFS_ORIGINAL(0x410220, float(__cdecl *)(float *))(vector);
}

float Length(const float *vector)
{
    return NFS_ORIGINAL(0x410180, float(__cdecl *)(const float *))(vector);
}

void Unit(const float *from, float *to)
{
    NFS_ORIGINAL(0x40fe20, void(__cdecl *)(const float *, float *))(from, to);
}

/** The helicopter sheet's ground height under a point, with the slope and whether the sheet covers it. */
float SheetElevation(char *sheet, const Vec3 &point, float *slope, bool *covered)
{
    const float flat[2] = {point.z, -point.x};
    return NFS_ORIGINAL(0x754290, float(__thiscall *)(void *, const float *, float *, bool *))(sheet, flat, slope,
                                                                                            covered);
}

struct Segment
{
    float x, y, z, w;
};

bool WorldBlocks(const Segment *segments, int count)
{
    alignas(16) unsigned char info[0x60];
    NFS_ORIGINAL(0x4048c0, void *(__thiscall *)(void *))(info);
    unsigned int manager[2] = {0, 3};
    return NFS_ORIGINAL(0x7854b0, int(__thiscall *)(void *, const Segment *, void *, int))(manager, segments, info,
                                                                                          count) != 0;
}

bool &HeliSkidHitting()
{
    return *reinterpret_cast<bool *>(0x90d621);
}

const float *const kZero = reinterpret_cast<const float *>(0x8a2fb4);
const float kDefaultSight = 250.0f;
} // namespace

bool __fastcall AIVehicleHelicopterCanSeeTarget(char *self, void *, char *target)
{
    bool hidden = false;
    void *perp = nullptr;
    if (void *vehicle = At<void *>(target, 0x1c))
    {
        perp = Find(vehicle, 0x4037e0);
        if (perp && Virtual<bool>(perp, 0x30))
        {
            hidden = true;
            if (At<unsigned char>(self, 0x7d))
                return false;
        }
    }
    At<unsigned char>(self, 0x7d) = 0;
    float sight = kDefaultSight;
    if (Virtual<void *>(self - 0x70c, 0xa0))
    {
        if (char *instance = Virtual<char *>(perp, 0x10))
        {
            sight = At<float>(At<char *>(instance, 8), 0x50);
            if (sight < 0.0f)
                sight = kDefaultSight;
        }
    }
    const Vec3 &chased = At<Vec3>(target, 0x10);
    void *body = Virtual<void *>(At<void *>(self, -0x724), 0x54);
    const Vec3 heli = *Virtual<const Vec3 *>(body, 0x20);
    Vec3 forward;
    Virtual<void, float *>(Virtual<void *>(At<void *>(self, -0x724), 0x54), 0x34, &forward.x);
    Vec3 toward = {chased.x - heli.x, chased.y - heli.y, chased.z - heli.z};
    const float distance = Normalize(&toward.x);
    const float close = *reinterpret_cast<const float *>(0x8eb30c);
    bool clear = false;
    if (distance <= close || (distance < sight && forward.z * toward.z + forward.y * toward.y +
                                                          toward.x * forward.x >
                                                      0.0f))
    {
        const Segment line[2] = {{heli.x, heli.y, heli.z, 1.0f}, {chased.x, chased.y + 1.0f, chased.z, 1.0f}};
        clear = !NFS_ORIGINAL(0x73cfe0, int(__cdecl *)(const void *, int))(reinterpret_cast<const void *>(0x919650),
                                                                           1) &&
                !WorldBlocks(line, 1);
    }
    if (clear)
    {
        At<Vec3>(self, 0x6c) = chased;
        return true;
    }
    if (hidden)
    {
        At<unsigned char>(self, 0x7d) = 1;
        return false;
    }
    if (!(close + close > distance))
        return false;
    return NFS_ORIGINAL(0x401930, float(__cdecl *)(const float *, const float *))(&chased.x, &At<float>(self, 0x6c)) <
           400.0f;
}

void __fastcall AIVehicleHelicopterSteerToNav(char *self, void *, char *nav, float height, float speed, bool hold)
{
    const Vec3 &spot = At<Vec3>(nav, 0x98);
    void *vehicle = self - 0x758;
    const Vec3 goal = {spot.x, spot.y + height, spot.z};
    Virtual<void, const float *>(vehicle, 0x3c, &goal.x);
    Virtual<void, Vec3>(self, 0xc, spot);
    Virtual<void, float>(vehicle, 0x38, speed);
    if (!hold && At<unsigned char>(nav, 0x50))
    {
        Vec3 heading = At<Vec3>(nav, 0xbc);
        Unit(&heading.x, &heading.x);
        const Vec3 velocity = *Virtual<const Vec3 *>(Virtual<void *>(At<void *>(self, -0x770), 0x54), 0x24);
        const float along = velocity.z * heading.z + velocity.x * heading.x + velocity.y * heading.y;
        if (!(along < 0.0f))
        {
            const Vec3 drift = {heading.x * along, heading.y * along, heading.z * along};
            Virtual<void, const float *>(self, 0x14, &drift.x);
            return;
        }
    }
    Virtual<void, const float *>(self, 0x14, kZero);
}

bool __fastcall AIVehicleHelicopterCheckHeliSheet(char *self, void *, const float *from, const float *ahead,
                                                  const float *near, float *goal, float *push)
{
    if (HeliSkidHitting())
        return false;
    const Vec3 nearPoint = {near[0], near[1], near[2]};
    Virtual<void, float *>(self + 0x7a4, 0x28, goal);
    const Vec3 aheadPoint = {ahead[0], ahead[1], ahead[2]};
    float slope[4];
    bool covered = false;
    float ground = SheetElevation(self + 0x870, aheadPoint, slope, &covered);
    if (covered && ground > aheadPoint.y)
    {
        Vec3 climb = {aheadPoint.x - from[0], ground - from[1], aheadPoint.z - from[2]};
        Unit(&climb.x, &climb.x);
        const float across = -slope[1];
        float turn = climb.z * across - climb.x * slope[0];
        if (turn < 0.0f)
            turn = -turn;
        push[1] = climb.y;
        push[0] = climb.x + across * turn * Constant(0x891054);
        push[2] = climb.z + turn * slope[0] * Constant(0x891054);
    }
    bool blocked = false;
    ground = SheetElevation(self + 0x830, nearPoint, slope, &blocked);
    if (blocked && ground > nearPoint.y)
    {
        Vec3 lifted = {nearPoint.x, ground, nearPoint.z};
        Vec3 climb = {nearPoint.x - from[0], ground - from[1], nearPoint.z - from[2]};
        const float climbLength = Length(&climb.x);
        const Vec3 toGoal = {goal[0] - from[0], goal[1] - from[1], goal[2] - from[2]};
        const float goalLength = Length(&toGoal.x);
        if (climbLength > Constant(0x890864))
            NFS_ORIGINAL(0x401c10, void(__cdecl *)(const float *, float, const float *, float *))(
                &climb.x, goalLength / climbLength, from, &lifted.x);
        if (lifted.y > goal[1])
            goal[1] = lifted.y;
        Unit(&climb.x, &climb.x);
        const float across = -slope[1];
        const float turn = fabsf(climb.z * across - climb.x * slope[0]);
        climb.x = climb.x + across * turn * Constant(0x8906a0);
        climb.z = climb.z + turn * slope[0] * Constant(0x8906a0);
        void *body = Virtual<void *>(At<void *>(self, 0x34), 0x54);
        const float speed = Virtual<float>(body, 0x2c);
        climb.x *= speed;
        climb.y *= speed;
        climb.z *= speed;
        Virtual<void, const float *>(body, 0x60, &climb.x);
    }
    return blocked;
}

void __fastcall AIVehicleHelicopterAvoidCamera(char *self, void *, float *out)
{
    void *body = Virtual<void *>(At<void *>(self, 0x34), 0x54);
    const Vec3 position = *Virtual<const Vec3 *>(body, 0x20);
    const float *velocity = Virtual<const float *>(body, 0x24);
    const char *camera = *reinterpret_cast<char **>(0x919690);
    const Vec3 ahead = {velocity[0] * Constant(0x890864) + position.x, velocity[1] * Constant(0x890864) + position.y,
                        velocity[2] * Constant(0x890864) + position.z};
    const Vec3 lens = {-At<float>(const_cast<char *>(camera), 0x54) * 4.0f - At<float>(const_cast<char *>(camera), 0x44),
                       At<float>(const_cast<char *>(camera), 0x58) * 4.0f + At<float>(const_cast<char *>(camera), 0x48),
                       At<float>(const_cast<char *>(camera), 0x50) * 4.0f + At<float>(const_cast<char *>(camera), 0x40)};
    const float dx = position.x - lens.x;
    const float dy = position.y - lens.y;
    const float dz = position.z - lens.z;
    const float distance = sqrtf(dz * dz + dy * dy + dx * dx);
    float radius = 7.0f;
    if (distance < 7.0f && distance > 2.0f)
        radius = distance - 0.5f;
    Vec3 hit;
    if (!NFS_ORIGINAL(0x40b280, bool(__cdecl *)(const Vec3 *, const Vec3 *, const Vec3 *, float, Vec3 *))(
            &position, &ahead, &lens, radius, &hit))
        return;
    Vec3 normal = {hit.x - lens.x, hit.y - lens.y, hit.z - lens.z};
    Unit(&normal.x, &normal.x);
    const float depth = (hit.z - ahead.z) * normal.z + (hit.y - ahead.y) * normal.y + (hit.x - ahead.x) * normal.x;
    out[0] = normal.x * depth + ahead.x;
    out[1] = normal.y * depth + ahead.y;
    out[2] = normal.z * depth + ahead.z;
    float away = normal.x * velocity[0] + normal.z * velocity[2] + normal.y * velocity[1];
    const float strength = (7.0f - radius) * 0.5f + Constant(0x890e04);
    if (away < 0.0f)
        away = -away;
    away *= strength;
    const Vec3 dodge = {normal.x * away + velocity[0], normal.y * away + velocity[1], normal.z * away + velocity[2]};
    Virtual<void, const float *>(body, 0x60, &dodge.x);
}

void __fastcall AIVehicleHelicopterOnDriving(char *self, void *, float)
{
    void *body = Virtual<void *>(At<void *>(self, 0x34), 0x54);
    const Vec3 velocity = *Virtual<const Vec3 *>(body, 0x24);
    const Vec3 position = *Virtual<const Vec3 *>(body, 0x20);
    const Vec3 &drift = At<Vec3>(self, 0x7ac);
    Vec3 soon = {velocity.x * Constant(0x890840) + position.x, velocity.y * Constant(0x890840) + position.y,
                 velocity.z * Constant(0x890840) + position.z};
    Vec3 later = {velocity.x * Constant(0x8910e4) + position.x, velocity.y * Constant(0x8910e4) + position.y,
                  velocity.z * Constant(0x8910e4) + position.z};
    Vec3 goal = {drift.x * Constant(0x890840) + At<float>(self, 0x88), At<float>(self, 0x8c),
                 drift.z * Constant(0x890840) + At<float>(self, 0x90)};
    const Vec3 driftNow = drift;
    AIVehicleHelicopterAvoidCamera(self, nullptr, &goal.x);
    Vec3 push = *reinterpret_cast<const Vec3 *>(kZero);
    AIVehicleHelicopterCheckHeliSheet(self, nullptr, &position.x, &later.x, &soon.x, &goal.x, &push.x);
    Vec3 steer = {goal.x - soon.x, goal.y - soon.y, goal.z - soon.z};
    const float remaining = sqrtf(steer.z * steer.z + steer.x * steer.x + steer.y * steer.y);
    float speed = Virtual<float>(body, 0x2c);
    const float rise = steer.y;
    steer.y = 0.0f;
    const Vec3 relative = {velocity.x - driftNow.x, velocity.y, velocity.z - driftNow.z};
    Vec3 flat;
    Unit(&steer.x, &flat.x);
    void *controller = At<void *>(self, 0x8b0);
    const float closing = flat.z * relative.z + flat.y * relative.y + flat.x * relative.x;
    const float along = flat.x * velocity.x + flat.z * velocity.z + flat.y * velocity.y;
    Virtual<void, int>(controller, 0xc, 0);
    if (closing > 3.0f)
    {
        speed = speed * Constant(0x890864) + *reinterpret_cast<const float *>(0x8f8dd0);
        const float most = *reinterpret_cast<const float *>(0x8f8dcc);
        if (speed > most)
            speed = most;
        float braking = *reinterpret_cast<const float *>(0x8f8dd4) * closing;
        if (braking > speed)
            braking = speed;
        if (!(closing * closing / (braking * Constant(0x8910fc)) < remaining))
        {
            At<float>(self, 0x84) = At<float>(self, 0x84) * Constant(0x891054);
            Virtual<void, int>(controller, 0xc, 1);
        }
    }
    else if (along < -1.0f && remaining < 15.0f)
        Virtual<void, int>(controller, 0xc, 1);
    if (HeliSkidHitting())
        Virtual<void, int>(controller, 0xc, 1);
    if (remaining > 2.0f)
    {
        const float scale = At<float>(self, 0x84) / remaining;
        steer.x *= scale;
        steer.z *= scale;
    }
    float climb = rise;
    const float bias = At<float>(self, 0x7b0);
    if (!(rise < 0.0f))
    {
        if (rise < 7.0f)
        {
            climb = rise * 5.0f;
            if (bias > 0.0f)
                climb = climb + (bias + bias);
        }
    }
    else if (rise > Constant(0x890db8))
    {
        climb = rise * 3.0f;
        if (bias < 0.0f)
            climb = climb + bias;
    }
    steer.y = climb;
    const float pushLength = sqrtf(push.z * push.z + push.y * push.y + push.x * push.x);
    if (pushLength > Constant(0x890cd4))
    {
        const float scale = At<float>(self, 0x84) / pushLength * Constant(0x8906a0);
        steer.x = steer.x * Constant(0x89107c) + push.x * scale;
        steer.y = steer.y * Constant(0x89107c) + push.y * scale;
        steer.z = steer.z * Constant(0x89107c) + push.z * scale;
    }
    Virtual<void, const float *>(controller, 4, &steer.x);
    const float *now = Virtual<const float *>(body, 0x20);
    const Vec3 look = {At<float>(self, 0x7b8) - now[0], At<float>(self, 0x7bc) - now[1], At<float>(self, 0x7c0) - now[2]};
    Virtual<void, const float *>(controller, 0x10, &look.x);
}
