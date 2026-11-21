/** Whether a pursuit can call the helicopter (skid hits), and the helicopter's shadow scale (original: zAI
 * AIPursuit::SkidHitEnabled, AIVehicleHelicopter::SetShadowScale, GetShadowScale), and cop light bars kept on for
 * every cop in a pursuit that is not leaving it (StartPursuit switches them on, StartFlee and EndPursuit off; a cop
 * taken back into the pursuit another way stayed dark).
 *
 * Roadblocks (AICopManager::CreateRoadBlock 0x43dd30): the original lays a roadblock's cars from one of the road's edges
 * outwards, so all but the first can stand past the road where the world has no collision; such a car is off the world,
 * hidden and nothing to hit, and the map shows a roadblock with one car or none on the road. With the vanilla fixes (or
 * Debug.RoadBlockFix=1) those cars go back along the roadblock's line onto the road. Each roadblock is logged ("roadblock:
 * made", each car and whether it stands on the ground); Debug.RoadBlock=SECONDS (dev/AiWatch.cpp) asks for one in a
 * pursuit. */
#include "game/gen/api.h"
#include "legacy/link.h"
#include "core/log.h"
#include "core/config.h"
#include "gameplay/vanillafixes.h"
#include <windows.h>
#include "dev/pose.h"

#include <math.h>

NFS_REPLACE(0x4195f0, PursuitSkidHitEnabled)
NFS_REPLACE(0x4325d0, HelicopterSetShadowScale)
NFS_REPLACE(0x4325e0, HelicopterGetShadowScale)
NFS_REPLACE(0x42ac90, CopPursuitUpdate)
NFS_REPLACE(0x43dd30, CopManagerCreateRoadBlock)

namespace
{
void *const kSupportHandle = reinterpret_cast<void *>(0x4037e0);
const unsigned int kSupportOptions = 0x3c6dbcb3;
const int kHelicopterOption = 2;
const int kShadowScale = 0x38;

template <typename R> R Virtual(void *object, int slot)
{
    using Fn = R(__thiscall *)(void *);
    return reinterpret_cast<Fn>((*reinterpret_cast<void ***>(object))[slot / 4])(object);
}

const Attrib::Gen::pursuitsupport *Support(void *pursuit)
{
    if (!Virtual<void *>(pursuit, 8))
        return nullptr;
    void *owner = *reinterpret_cast<void **>(static_cast<char *>(Virtual<char *>(pursuit, 8)) + 0x1c);
    if (!owner)
        return nullptr;
    void *list = *reinterpret_cast<void **>(static_cast<char *>(owner) + 4);
    void *found = NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(list, kSupportHandle);
    if (!found)
        return nullptr;
    return Virtual<const Attrib::Gen::pursuitsupport *>(found, 0x14);
}
} // namespace

bool __fastcall PursuitSkidHitEnabled(void *pursuit)
{
    const Attrib::Gen::pursuitsupport *support = Support(pursuit);
    const Attrib::Instance *instance = reinterpret_cast<const Attrib::Instance *>(support);
    for (unsigned int i = 0; static_cast<int>(i) < static_cast<int>(support->Num_AirSupportOptions()); ++i)
    {
        const int *option = static_cast<const int *>(instance->GetAttributePointer(kSupportOptions, i));
        if (!option)
            option = static_cast<const int *>(NFS_ORIGINAL(0x6269b0, void *(__cdecl *)(int))(0xc));
        if (*option == kHelicopterOption)
            return true;
    }
    return false;
}

void __fastcall HelicopterSetShadowScale(void *helicopter, void *, float scale)
{
    *reinterpret_cast<float *>(static_cast<char *>(helicopter) + kShadowScale) = scale;
}

float __fastcall HelicopterGetShadowScale(void *helicopter)
{
    return *reinterpret_cast<const float *>(static_cast<char *>(helicopter) + kShadowScale);
}

namespace
{
const unsigned int kCopLights = 0x7000;
const int kPursuitAi = 0x758;
const int kVehicleAi = 0x4c;
const int kInPursuit = 0x760;
const int kGetVehicle = 8;
const int kIsGlareOn = 0x9c;
const int kGlareOn = 0xa0;
const int kGoal = 0xc0;

unsigned int GoalHash(const char *name)
{
    return NFS_ORIGINAL(0x5cc240, unsigned int(__cdecl *)(const char *))(name);
}

template <typename R, typename... A> R Call(void *object, int slot, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*reinterpret_cast<void ***>(object))[slot / 4])(object, args...);
}

const int kObjects = 4;
void *const kRigidBodyHandle = reinterpret_cast<void *>(0x4039f0);
const int kBodyPosition = 0x20;
const int kRoadBlockCops = 16;


/** The world's collision height under a place (world axes), or -1000 where it has none. */
float GroundUnder(float x, float y, float z)
{
    const float point[3] = {-y, z + 3.0f, x};
    char manager[8] = {};
    float height = 0.0f;
    return reinterpret_cast<WCollisionMgr *>(manager)->GetWorldHeightAtPoint(
               *reinterpret_cast<const UMath::Vector3 *>(point), height, nullptr)
               ? height
               : -1000.0f;
}

struct RoadBlockCop
{
    char *cop;
    float position[3];
    bool grounded;
    bool settled;
    DWORD since;
};

RoadBlockCop g_roadBlockCops[kRoadBlockCops];
/** A roadblock's cars are placed within this long of each other; a car with no ground gives up after it. */
const DWORD kRoadBlockWindow = 3000;
const float kRoadBlockStep = 1.0f;
const float kRoadBlockSpacing = 4.5f;
/** How far past the roadblock's cars on the ground a car off the world may go: the original lays a roadblock from one
 * of the road's edges, and where the edges it reads lie past the ground the rest of the road is beyond that car. */
const float kRoadBlockReach = 25.0f;
const int kSetVehicleOnGround = 0x68;

bool FixRoadBlocks()
{
    static const bool forced = nfs::config::Int("Debug", "RoadBlockFix", 0) != 0;
    return forced || nfs::vanillafixes::On();
}

bool Free(const float *at, const RoadBlockCop &self)
{
    for (const RoadBlockCop &other : g_roadBlockCops)
    {
        if (!other.cop || &other == &self)
            continue;
        const float dx = other.position[0] - at[0], dy = other.position[1] - at[1];
        if (dx * dx + dy * dy < kRoadBlockSpacing * kRoadBlockSpacing)
            return false;
    }
    return true;
}

/** A roadblock car the original put where the world has no collision (past a bridge's railing, behind a fence) is off the
 * world: hidden, and nothing to hit. It goes along the line to the roadblock's cars on the ground, and on past them, until it
 * stands on ground clear of them. */
void SettleRoadBlockCop(RoadBlockCop &entry, void *vehicle)
{
    const DWORD now = GetTickCount();
    float anchor[3] = {};
    int grounded = 0;
    for (const RoadBlockCop &other : g_roadBlockCops)
    {
        if (!other.cop || !other.grounded || now - other.since > kRoadBlockWindow)
            continue;
        for (int i = 0; i < 3; ++i)
            anchor[i] += other.position[i];
        ++grounded;
    }
    if (!grounded)
    {
        if (now - entry.since > kRoadBlockWindow)
            entry.settled = true;
        return;
    }
    entry.settled = true;
    for (float &v : anchor)
        v /= static_cast<float>(grounded);
    const float dx = anchor[0] - entry.position[0], dy = anchor[1] - entry.position[1];
    const float length = sqrtf(dx * dx + dy * dy);
    if (length < kRoadBlockStep)
        return;
    const float ux = dx / length, uy = dy / length;
    for (float along = kRoadBlockStep; along <= length + kRoadBlockReach; along += kRoadBlockStep)
    {
        const float at[3] = {entry.position[0] + ux * along, entry.position[1] + uy * along, anchor[2]};
        const float ground = GroundUnder(at[0], at[1], at[2]);
        if (ground < -999.0f || !Free(at, entry))
            continue;
        const float position[3] = {-at[1], ground + 0.5f, at[0]};
        const float direction[3] = {-uy, 0.0f, ux};
        Call<bool, const float *, const float *>(vehicle, kSetVehicleOnGround, position, direction);
        nfs::Log("roadblock: a cop off the world moved %d m onto the ground at %d %d %d", static_cast<int>(along),
                 static_cast<int>(at[0]), static_cast<int>(at[1]), static_cast<int>(ground));
        entry.position[0] = at[0];
        entry.position[1] = at[1];
        entry.position[2] = ground;
        entry.grounded = true;
        return;
    }
    nfs::Log("roadblock: a cop off the world found no ground towards the roadblock");
}

/** Logs each cop that takes its place in a roadblock (where, and how far from the player's car), and with the vanilla
 * fixes settles the ones off the world. */
void NoteRoadBlock(char *cop, void *vehicle, bool inRoadBlock)
{
    int free = -1;
    for (int i = 0; i < kRoadBlockCops; ++i)
    {
        RoadBlockCop &entry = g_roadBlockCops[i];
        if (entry.cop == cop)
        {
            if (!inRoadBlock)
                entry.cop = nullptr;
            else if (!entry.settled && !entry.grounded && FixRoadBlocks())
                SettleRoadBlockCop(entry, vehicle);
            return;
        }
        if (!entry.cop && free < 0)
            free = i;
    }
    if (!inRoadBlock || free < 0)
        return;
    void *objects = *reinterpret_cast<void **>(static_cast<char *>(vehicle) + kObjects);
    void *body = NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(objects, kRigidBodyHandle);
    if (!body)
        return;
    const float *p = Call<const float *>(body, kBodyPosition);
    RoadBlockCop &entry = g_roadBlockCops[free];
    entry = {cop, {p[2], -p[0], p[1]}, false, false, GetTickCount()};
    entry.grounded = GroundUnder(entry.position[0], entry.position[1], entry.position[2]) > -999.0f;
    float player[4] = {};
    nfs::pose::PlayerCar(player);
    const float dx = entry.position[0] - player[0], dy = entry.position[1] - player[1];
    nfs::Log("roadblock: a cop at %d %d %d (%s), the player at %d %d %d (%d m away)",
             static_cast<int>(entry.position[0]), static_cast<int>(entry.position[1]),
             static_cast<int>(entry.position[2]), entry.grounded ? "on the ground" : "off the world",
             static_cast<int>(player[0]), static_cast<int>(player[1]), static_cast<int>(player[2]),
             static_cast<int>(sqrtf(dx * dx + dy * dy)));
}

void *const kDamageableVehicleHandle = reinterpret_cast<void *>(0x620fd0);
const int kBrokenLights = 0x5c;
const int kWatchedCops = 32;
const int kMaxLightLogs = 200;

struct CopLightState
{
    char *cop;
    unsigned int state;
};

CopLightState g_copLights[kWatchedCops];
int g_lightLogs;

/** Logs a cop's light bar whenever what decides it changes: in the pursuit, its goal, the lights' state bits and the
 * lights its damage broke (DamageVehicle::DamageLight 0x6b8910; a broken light gives no flare). */
void WatchCopLights(char *cop, void *vehicle, unsigned int goal)
{
    if (g_lightLogs >= kMaxLightLogs)
        return;
    void *objects = *reinterpret_cast<void **>(static_cast<char *>(vehicle) + kObjects);
    char *damage = static_cast<char *>(
        NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(objects, kDamageableVehicleHandle));
    const unsigned int broken = damage ? *reinterpret_cast<const unsigned int *>(damage + kBrokenLights) & kCopLights : 0;
    const bool lit = Call<bool, unsigned int>(vehicle, kIsGlareOn, kCopLights);
    const unsigned int state = (cop[kInPursuit] ? 1u : 0u) | (lit ? 2u : 0u) | broken | (goal & 0xffff0000u);
    int free = -1;
    for (int i = 0; i < kWatchedCops; ++i)
    {
        if (g_copLights[i].cop == cop)
        {
            if (g_copLights[i].state == state)
                return;
            free = i;
            break;
        }
        if (!g_copLights[i].cop && free < 0)
            free = i;
    }
    if (free < 0)
        free = static_cast<int>(reinterpret_cast<unsigned int>(cop) >> 4) % kWatchedCops;
    g_copLights[free] = {cop, state};
    ++g_lightLogs;
    nfs::Log("cop lights: cop %p in pursuit %d goal %08x lights %d broken %04x", cop, cop[kInPursuit] ? 1 : 0, goal,
             lit ? 1 : 0, broken);
}

bool Leaving(char *cop)
{
    static const unsigned int flee = GoalHash("AIGoalFleePursuit");
    static const unsigned int exit = GoalHash("AIGoalHeliExit");
    const unsigned int *goal = Call<const unsigned int *>(cop + kVehicleAi, kGoal);
    return goal && (*goal == flee || *goal == exit);
}
/** AIVehiclePursuit::Update (original 0x42ac90): the vehicle update and the siren, then four times a second whether
 * the cop sees its target. */
void PursuitVehicleUpdate(char *cop, float dt)
{
    NFS_ORIGINAL(0x4292a0, void(__thiscall *)(char *, float))(cop, dt);
    Call<void, float>(cop, 0x50, dt);
    float &look = *reinterpret_cast<float *>(cop + 0x79c);
    float &seen = *reinterpret_cast<float *>(cop + 0x798);
    if (!cop[kInPursuit] || !Call<char *>(cop + kVehicleAi, 0x64)[0x2c])
    {
        seen = 0.25f;
        look = 0.25f;
        return;
    }
    look = dt + look;
    seen = dt + seen;
    if (look < 0.25f)
        return;
    look = look - 0.25f;
    if (Call<bool, void *>(cop + 0x758, 0x7c, Call<void *>(cop + kVehicleAi, 0x64)))
        seen = -0.25f;
}
} // namespace

void __fastcall CopPursuitUpdate(char *cop, void *, float dt)
{
    PursuitVehicleUpdate(cop, dt);
    static const unsigned int roadBlock = GoalHash("AIGoalStaticRoadBlock");
    void *vehicle = Call<void *>(cop + kVehicleAi, kGetVehicle);
    if (!vehicle)
        return;
    const unsigned int *goal = Call<const unsigned int *>(cop + kVehicleAi, kGoal);
    NoteRoadBlock(cop, vehicle, goal && *goal == roadBlock);
    WatchCopLights(cop, vehicle, goal ? *goal : 0);
    if (!cop[kInPursuit] || Leaving(cop) || Call<bool, unsigned int>(vehicle, kIsGlareOn, kCopLights))
        return;
    Call<void, unsigned int>(vehicle, kGlareOn, kCopLights);
    static int logged = 0;
    if (logged++ < 20)
        nfs::Log("cop lights: a cop in the pursuit had its lights off; back on");
}
/** AICopManager::CreateRoadBlock (original 0x43dd30): logs each roadblock made, and the attempts that fail (the cop
 * manager tries every frame while a pursuit asks for one). */
bool __fastcall AICopManagerCreateRoadBlock(char *self, void *, void *pursuit, int cops, void *helicopter, char *list);

bool __fastcall CopManagerCreateRoadBlock(char *manager, void *, void *pursuit, int cops, void *helicopter, void *list)
{
    const bool made = AICopManagerCreateRoadBlock(manager, nullptr, pursuit, cops, helicopter, static_cast<char *>(list));
    static int failed = 0;
    if (!made && failed++ % 300)
        return made;
    float player[4] = {};
    nfs::pose::PlayerCar(player);
    nfs::Log("roadblock: %s (%d cops asked, %d failed attempts), the player at %d %d %d", made ? "made" : "not made",
             cops, failed, static_cast<int>(player[0]), static_cast<int>(player[1]), static_cast<int>(player[2]));
    return made;
}
