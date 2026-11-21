/**
 * Cops and a car standing where no road goes: the roof of the bus shelter at 2106,1445 and the bandstand at 781,235, or any place
 * like them. Watched in a logged pursuit (Debug.AiWatch, Debug.PursueHeat, Debug.PursueThen) with the car parked on either, the
 * original's cops circle below it until it has got away. Four rules of the original make it so:
 *
 * - A cop sees a car along a line from half a metre over its own wheels to half a metre over the car's
 *   (AIVehicleCopCar::CanSeeTarget 0x416540). The edge of a roof or a plinth a few metres up cuts that line from anywhere near.
 * - The bust meter fills only for a cop within 1.5 m of the car's height (AIPursuit::OnTask 0x443ba0, at 0x444203).
 * - A cop leaves the road only within 60 m of the car, while nothing lies between it and the car and also between it and the
 *   road (0x411fe0): once up there, the edge it came over stands between it and the road, so it turns back.
 * - On the road action a cop keeps its full speed right up to a car it cannot see, and wrecks itself on what it cannot see over
 *   (at the bandstand, 36 m/s into the plinth: the pursuit's GetNumCopsDestroyed goes up and no cop is left).
 * - Off the road a pull back to it as strong as the cop's speed goes on working (AIActionPursuitOffRoad::UpdateRoadAffinity).
 *
 * With the vanilla bug fixes on, for a car nearly standing: within 100 m a cop sees it from the height of its eyes to the car's
 * roof and on seeing it comes for it across whatever is there, off the road and slowing as it arrives; the road neither calls it
 * back nor pulls at it; on the road action it slows as it nears the car; and the bust meter runs for a cop within a roof's height
 * and does not fall back while a cop in sight of the car is near.
 * A car in a hiding place stays hidden, and a car on the move is chased as the original chases it.
 */
#include <math.h>
#include <windows.h>

#include "game/gen/api.h"
#include "legacy/link.h"
#include "core/log.h"
#include "gameplay/vanillafixes.h"
#include "gameplay/ai/aituning.h"

NFS_REPLACE(0x416540, CopCanSeeTarget)
NFS_REPLACE(0x411fe0, CopOffRoadPossible)
NFS_REPLACE(0x406b70, CopRoadAffinity)
NFS_REPLACE(0x443ba0, PursuitOnTask)
NFS_REPLACE(0x428720, CopRaceUpdate)

bool __fastcall CopCanSeeTarget(char *self, void *, char *target);
bool __fastcall CopOffRoadPossible(char *self, void *);
void __fastcall CopRoadAffinity(char *self, void *, float *out);
bool __fastcall PursuitOnTask(char *self, void *, void *task, float dt);
bool AIPursuitOnTaskBody(char *self, void *task, float dt, float bustHeight);
void __fastcall CopRaceUpdate(char *self, void *, float dt);

namespace
{
/** A car slower than this (m/s) counts as standing. */
const float kStanding = 5.5f;
/** How far (m) a cop sees a standing car over an edge and goes off the road for it, and the heights of the line it looks along
 * over the ground. */
const float kSightRange = 100.0f;
const float kCopEyes = 1.5f;
const float kCarRoof = 1.3f;
/** How far under or over a car a cop may be (m) for the bust meter: the original's, and with the fix a roof's height. */
const float kBustHeight = 1.5f;
const float kBustHeightFixed = 5.5f;
/** The most (m/s) a cop and its target may differ by to go off the road, as in the original. */
const float kOffRoadSpeed = 38.88889f;
/** Cop tactics improved: how much further than its heat's sight range a cop in a pursuit sees the car, along a line
 * nothing blocks (the line test stays). */
const float kPursuitSightScale = 1.5f;

/** With a cop this near (m) and in sight of a standing car, the bust meter holds where it would fall: a cop's reach is 15 m (from
 * the car or the plinth it stands on), and it fell back each time one drove off to find its way round. */
const float kBustHold = 35.0f;

/** The speed (m/s) a cop on the road comes up to a standing car it cannot see at: this at the car, and this more a metre away. */
const float kArriveSpeed = 8.0f;
const float kArriveSpeedPerMetre = 0.5f;

/** AITarget: its position, the simable it follows; GetSpeed, GetLinearVelocity. */
const int kTargetPosition = 0x10;
const int kTargetSimable = 0x1c;
const unsigned int kTargetSpeed = 0x409ed0;
const unsigned int kTargetVelocity = 0x409ef0;
/** AIVehicleCopCar's IPursuitAI: where its simable lies from it. */
const int kPursuitAiSimable = -0x724;
/** AIActionPursuitOffRoad and AIActionRace: their IVehicleAI and IRigidBody; IVehicleAI: its target, whether it can drive straight there. */
const int kActionAi = 0x48;
const int kActionBody = 0x4c;
const int kRaceAi = 0x40;
const int kRaceBody = 0x48;
const int kAiTarget = 0x64;
const int kAiDriveSpeed = 0x34;
const int kAiSetDriveSpeed = 0x38;
const int kAiSimable = 0x04;
/** ISimable's rigid body, its COM list; IRigidBody: position, velocity. */
const int kSimableBody = 0x54;
const int kSimableObjects = 4;
const int kBodyPosition = 0x20;
const int kBodyVelocity = 0x24;
const unsigned int kFind = 0x5d59f0;
const unsigned int kPerpetratorHandle = 0x4037e0;
const unsigned int kPursuitAiHandle = 0x4038e0;
const int kPerpetratorHidden = 0x2c;
const unsigned int kCollisionInfoCtor = 0x4048c0;
const unsigned int kCheckHitWorld = 0x7854b0;
const unsigned int kCollisionFlags = 3;
const int kCollisionInfoSize = 128;
/** AIPursuit, as OnTask has it: its target, bust meter's change, whether a cop has it in sight, nearest cop. */
const int kPursuitTarget = 0xb4;
const int kPursuitBustRate = 0x120;
const int kPursuitInSight = 0x154;
const int kPursuitNearest = 0x260;

template <typename R, typename... A> R Virtual(void *object, int offset, A... args)
{
    typedef R(__thiscall * Function)(void *, A...);
    return (*reinterpret_cast<Function *>(*reinterpret_cast<char **>(object) + offset))(object, args...);
}

bool Standing(char *target)
{
    return *reinterpret_cast<void **>(target + kTargetSimable) &&
           NFS_ORIGINAL(kTargetSpeed, float(__thiscall *)(char *))(target) < kStanding;
}

bool Hidden(char *target)
{
    char *simable = *reinterpret_cast<char **>(target + kTargetSimable);
    void *objects = *reinterpret_cast<void **>(simable + kSimableObjects);
    void *perpetrator = NFS_ORIGINAL(kFind, void *(__thiscall *)(void *, unsigned int))(objects, kPerpetratorHandle);
    return perpetrator && Virtual<bool>(perpetrator, kPerpetratorHidden);
}

float Distance(const float *a, const float *b)
{
    const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrtf(x * x + y * y + z * z);
}

/** Whether a cop at `from` has a standing car in sight over what lies between them, within kSightRange. */
bool EyesOn(const float *from, const float *to)
{
    if (Distance(from, to) > kSightRange)
        return false;
    const float line[8] = {from[0], from[1] + kCopEyes, from[2], 1.0f, to[0], to[1] + kCarRoof, to[2], 1.0f};
    alignas(16) char info[kCollisionInfoSize];
    NFS_ORIGINAL(kCollisionInfoCtor, void(__thiscall *)(char *))(info);
    unsigned int collision[2] = {0, kCollisionFlags};
    return NFS_ORIGINAL(kCheckHitWorld, int(__thiscall *)(unsigned int *, const float *, char *, unsigned int))(
               collision, line, info, kCollisionFlags) == 0;
}


/** AIVehicleCopCar::CanSeeTarget as the original has it (0x416540): within the sight range of the perp's heat, in front
 * of the cop or near it, and nothing between them half a metre over the ground; a hidden car stays unseen. */
bool CopSeesTarget(char *self, char *target)
{
    bool hidden = false;
    void *perp = nullptr;
    if (void *simable = *reinterpret_cast<void **>(target + kTargetSimable))
    {
        perp = NFS_ORIGINAL(kFind, void *(__thiscall *)(void *, unsigned int))(*reinterpret_cast<void **>(
                                                                               static_cast<char *>(simable) + 4),
                                                                           kPerpetratorHandle);
        if (perp && Virtual<bool>(perp, 0x2c))
        {
            hidden = true;
            if (self[0x50])
                return false;
        }
    }
    self[0x50] = 0;
    void *vehicle = Virtual<void *>(self - 0x70c, 0xa0);
    float range = 150.0f;
    float close = 50.0f;
    if (vehicle && perp)
    {
        if (char *instance = Virtual<char *>(perp, 0x10))
        {
            const char *levels = *reinterpret_cast<char **>(instance + 8);
            const float sight = *reinterpret_cast<const float *>(levels + 0xb0);
            if (!(sight < 0.0f))
            {
                range = sight;
                close = *reinterpret_cast<const float *>(levels + 0xb8);
            }
        }
    }
    if (vehicle && nfs::aituning::CopsImproved())
        range *= kPursuitSightScale;
    const float *chased = reinterpret_cast<const float *>(target + kTargetPosition);
    const float aim[3] = {chased[0], chased[1], chased[2]};
    void *body = Virtual<void *>(*reinterpret_cast<void **>(self + kPursuitAiSimable), kSimableBody);
    float forward[4];
    Virtual<void, float *>(body, 0x34, forward);
    const float *at = Virtual<const float *>(Virtual<void *>(*reinterpret_cast<void **>(self + kPursuitAiSimable),
                                                             kSimableBody),
                                             kBodyPosition);
    const float eye[3] = {at[0], at[1], at[2]};
    float toward[3] = {aim[0] - eye[0], aim[1] - eye[1], aim[2] - eye[2]};
    NFS_ORIGINAL(0x40fe20, void(__cdecl *)(const float *, float *))(toward, toward);
    const float dx = eye[0] - aim[0];
    const float dy = eye[1] - aim[1];
    const float dz = eye[2] - aim[2];
    const float distance = sqrtf(dz * dz + dy * dy + dx * dx);
    bool blocked = true;
    if (!(distance >= range))
    {
        if (!(distance >= close))
            blocked = false;
        else
        {
            const float facing = forward[2] * toward[2] + forward[1] * toward[1] + forward[0] * toward[0];
            const float fov = *reinterpret_cast<const float *>(self + 0x4c);
            blocked = facing < fov || facing == fov;
        }
    }
    bool seen = false;
    if (!blocked && (!vehicle || Virtual<bool>(vehicle, 0x64)))
    {
        struct Segment
        {
            float x, y, z, w;
        } line[2] = {{eye[0], eye[1] + 0.5f, eye[2], 1.0f}, {aim[0], aim[1] + 0.5f, aim[2], 1.0f}};
        alignas(16) unsigned char info[kCollisionInfoSize];
        NFS_ORIGINAL(kCollisionInfoCtor, void *(__thiscall *)(void *))(info);
        unsigned int manager[2] = {0, 3};
        seen = !NFS_ORIGINAL(kCheckHitWorld, int(__thiscall *)(void *, const Segment *, void *, unsigned int))(
            manager, line, info, kCollisionFlags);
    }
    if (!seen && hidden)
        self[0x50] = 1;
    return seen;
}
/** The original test for leaving the road (0x411fe0): the cop within 60 m of the car's path ahead, close to its speed, and
 * the AI willing. */
bool OffRoadAllowed(char *self)
{
    void *ai = *reinterpret_cast<void **>(self + kActionAi);
    void *body = *reinterpret_cast<void **>(self + kActionBody);
    char *target = Virtual<char *>(ai, kAiTarget);
    const float *chased = reinterpret_cast<const float *>(target + kTargetPosition);
    const float aim[3] = {chased[0], chased[1], chased[2]};
    const float *at = Virtual<const float *>(body, kBodyPosition);
    const float dx = at[0] - aim[0];
    const float dy = at[1] - aim[1];
    const float dz = at[2] - aim[2];
    const float distance = sqrtf(dz * dz + dy * dy + dx * dx);
    float reach = 60.0f;
    void *simable = *reinterpret_cast<void **>(target + kTargetSimable);
    if (simable)
    {
        if (void *chasedAi = NFS_ORIGINAL(kFind, void *(__thiscall *)(void *, unsigned int))(
                *reinterpret_cast<void **>(static_cast<char *>(simable) + kSimableObjects), 0x403700))
        {
            const float *ahead = reinterpret_cast<const float *>(Virtual<char *>(chasedAi, 0xb4) + 0x98);
            const float ax = aim[0] - ahead[0];
            const float ay = aim[1] - ahead[1];
            const float az = aim[2] - ahead[2];
            reach = sqrtf(az * az + ay * ay + ax * ax) + 60.0f;
        }
    }
    if (distance > reach)
        return false;
    const float *velocity = simable ? Virtual<const float *>(Virtual<void *>(simable, kSimableBody), kBodyVelocity)
                                    : reinterpret_cast<const float *>(0x8a2fb4);
    const float *mine = Virtual<const float *>(body, kBodyVelocity);
    const float vx = velocity[0] - mine[0];
    const float vy = velocity[1] - mine[1];
    const float vz = velocity[2] - mine[2];
    if (sqrtf(vz * vz + vy * vy + vx * vx) > kOffRoadSpeed)
        return false;
    if (!Virtual<bool>(ai, 0x68))
        return false;
    return Virtual<bool>(ai, 0x4c);
}

/** AIActionPursuitOffRoad::UpdateRoadAffinity as the original has it (0x406b70): a pull across the road segment the
 * cop's navigation is on, stronger the further out and the faster it drifts outward, at most 100 km/h a side. */
void RoadAffinity(char *self, float *out)
{
    const float *road = reinterpret_cast<const float *>(
        Virtual<char *>(*reinterpret_cast<void **>(self + kActionAi), 0x48) + 0xc);
    void *body = *reinterpret_cast<void **>(self + kActionBody);
    const float *at = Virtual<const float *>(body, kBodyPosition);
    const float *velocity = Virtual<const float *>(body, kBodyVelocity);
    const float most = *reinterpret_cast<const float *>(0x890d40);
    const float nx = road[5];
    const float nz = -road[4];
    const float ax = at[0] - road[0];
    const float az = at[2] - road[1];
    const float bx = at[0] - road[2];
    const float bz = at[2] - road[3];
    const float outward = velocity[2] * nz + velocity[0] * nx;
    float nearSide = az * nz + ax * nx + 1.0f;
    if (0.1f > nearSide)
        nearSide = 0.1f;
    float farSide = -(bz * nz + bx * nx) + 1.0f;
    if (0.1f > farSide)
        farSide = 0.1f;
    const float drift = 0.0f > -outward ? 0.0f : -outward;
    const float nearDrift = drift / nearSide;
    const float farDrift = (0.0f > outward ? 0.0f : outward) / farSide;
    float nearPull = 0.5f / (nearSide * nearSide) + (nearDrift * nearDrift + nearDrift * nearDrift);
    float farPull = 0.5f / (farSide * farSide) + (farDrift * farDrift + farDrift * farDrift);
    if (most < nearPull)
        nearPull = most;
    if (most < farPull)
        farPull = most;
    out[0] = nx * -farPull + nx * nearPull;
    out[1] = 0.0f * -farPull + 0.0f * nearPull;
    out[2] = nz * -farPull + nz * nearPull;
}
} // namespace

bool __fastcall CopCanSeeTarget(char *self, void *, char *target)
{
    const bool seen = CopSeesTarget(self, target);
    if (seen || !nfs::vanillafixes::On() || !Standing(target) || Hidden(target))
        return seen;
    void *simable = *reinterpret_cast<void **>(self + kPursuitAiSimable);
    const float *from = Virtual<const float *>(Virtual<void *>(simable, kSimableBody), kBodyPosition);
    return EyesOn(from, reinterpret_cast<const float *>(target + kTargetPosition));
}

bool __fastcall CopOffRoadPossible(char *self, void *)
{
    if (OffRoadAllowed(self))
        return true;
    if (!nfs::vanillafixes::On())
        return false;
    void *ai = *reinterpret_cast<void **>(self + kActionAi);
    void *body = *reinterpret_cast<void **>(self + kActionBody);
    char *target = Virtual<char *>(ai, kAiTarget);
    if (!Standing(target))
        return false;
    const float *velocity = NFS_ORIGINAL(kTargetVelocity, const float *(__thiscall *)(char *))(target);
    if (Distance(velocity, Virtual<const float *>(body, kBodyVelocity)) > kOffRoadSpeed)
        return false;
    return EyesOn(Virtual<const float *>(body, kBodyPosition), reinterpret_cast<const float *>(target + kTargetPosition));
}

void __fastcall CopRoadAffinity(char *self, void *, float *out)
{
    RoadAffinity(self, out);
    if (!nfs::vanillafixes::On())
        return;
    void *ai = *reinterpret_cast<void **>(self + kActionAi);
    if (Standing(Virtual<char *>(ai, kAiTarget)))
        out[0] = out[1] = out[2] = 0.0f;
}

void __fastcall CopRaceUpdate(char *self, void *, float dt)
{
    NFS_ORIGINAL(0x428720, void(__thiscall *)(char *, float))(self, dt);
    if (!nfs::vanillafixes::On())
        return;
    void *ai = *reinterpret_cast<void **>(self + kRaceAi);
    char *target = Virtual<char *>(ai, kAiTarget);
    if (!target || !Standing(target))
        return;
    char *simable = Virtual<char *>(ai, kAiSimable);
    void *objects = *reinterpret_cast<void **>(simable + kSimableObjects);
    if (!NFS_ORIGINAL(kFind, void *(__thiscall *)(void *, unsigned int))(objects, kPursuitAiHandle))
        return;
    const float *from = Virtual<const float *>(*reinterpret_cast<void **>(self + kRaceBody), kBodyPosition);
    const float most = kArriveSpeed + kArriveSpeedPerMetre * Distance(from, reinterpret_cast<const float *>(target + kTargetPosition));
    if (Virtual<float>(ai, kAiDriveSpeed) > most)
        Virtual<void>(ai, kAiSetDriveSpeed, most);
}

bool __fastcall PursuitOnTask(char *self, void *, void *task, float dt)
{
    const bool fixed = nfs::vanillafixes::On();
    const bool result = AIPursuitOnTaskBody(self, task, dt, fixed ? kBustHeightFixed : kBustHeight);
    if (!fixed)
        return result;
    float &rate = *reinterpret_cast<float *>(self + kPursuitBustRate);
    char *target = *reinterpret_cast<char **>(self + kPursuitTarget);
    if (rate < 0.0f && *(self + kPursuitInSight) && *reinterpret_cast<const float *>(self + kPursuitNearest) < kBustHold && target &&
        Standing(target))
        rate = 0.0f;
    return result;
}
