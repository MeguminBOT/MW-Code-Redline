/**
 * What every car in the world is doing, for a look at the AI where the world is not loaded around it:
 * Debug.AiWatch=SECONDS logs each car that often: its place, how far from the player, its speed and vertical speed, its
 * physics mode (PVehicle: 1 full, 2 simple, the AI's way of moving a car along the road with no physics) and whether it
 * counts as off the world (PVehicle::IsOffWorld: under most of its wheels no collision is loaded). Not in the original
 * game.
 *
 * Debug.AiLog=1 (on in the play sessions Claude starts for the user) writes ai_log.txt next to the exe while the game
 * is in the world: once a second the player's car, every racer (its skill, the race's or character's base and the
 * rubber band's glue, at the IVehicleAI interface's +0x75c and +0x760, which GetSkill 0x409340 adds; ComputeSkill
 * writes the base from the object's start, +0x7a8), every cop chasing and the
 * pursuit; traffic only as a count. Lines start with the seconds since the game started. A racer or cop standing
 * still for five seconds with full physics, or for three seconds off the world, gets a STUCK line.
 */
#include <math.h>
#include <stdarg.h>
#include <windows.h>

#include "game/gen/api.h"
#include "dev/aiwatch.h"
#include "core/config.h"
#include "legacy/link.h"
#include "core/log.h"
#include "dev/pose.h"

extern "C" int TheGameFlowManager_CurrentGameFlowState;

bool __fastcall AICopManagerSpawnPursuitHelicopter(char *self, void *, void *pursuit);

namespace nfs::aiwatch
{

namespace
{
/** ListableSet<IVehicle, 10, eVehicleList, 10>'s lists: each 0x60 long, its items at +4, their count at +0xc; list 0 is
 * all. */
const unsigned int kVehicleLists = 0x92cd18;
const int kListItems = 4;
const int kListCount = 0xc;
const int kObjects = 4;
const unsigned int kRigidBodyHandle = 0x4039f0;
const int kBodyPosition = 0x20;
const int kBodyVelocity = 0x24;
/** IVehicle: physics mode, name, speed (m/s), off the world. */
const int kVehicleMode = 0x38;
const int kVehicleName = 0x4c;
const int kVehicleSpeed = 0x90;
const int kVehicleOffWorld = 0xc4;
/** IVehicle::IsGlareOn, and the cop light bar's bits. */
const int kVehicleIsGlareOn = 0x9c;
const unsigned int kCopLights = 0x7000;
/** IRenderable's handle and its DistanceToView (the cop manager takes cops further than 100 m from the view for a
 * roadblock); the cop manager's roadblock request: cops asked and the pursuit asking. */
const unsigned int kRenderableHandle = 0x404ff0;
const int kDistanceToView = 0x18;
const int kCopManagerInterface = 0x54;
const int kRoadBlockCops = 0xb8;
const int kRoadBlockPursuit = 0xbc;
/** IVehicleAI: the name hash of its goal. */
const int kAiGoalName = 0xc0;

/** IVehicleAI's handle and its place in AIVehicle; AIVehicle: the speed asked of the car, its goal; AIGoal: its action;
 * AIActionRace: the speed it is at, the most it could be. */
const unsigned int kVehicleAiHandle = 0x403700;
const int kAiInterface = 0x4c;
const int kAiDriveSpeed = 0x84;
const int kAiGoal = 0xb8;
const int kGoalAction = 4;
const int kActionSpeed = 0x58;
const int kActionMostSpeed = 0x5c;

/** Singleton<ICopMgr>; ICopMgr::PursueAtHeatLevel. */
const unsigned int kCopManager = 0x92c67c;
const int kCopPursueAtHeat = 0x34;
const int kCopLockout = 0x58;
/** IVehicleAI::GetPursuit, and IPursuit: its cops, evade level, cool down left, in sight, busted, cops damaged and
 * destroyed, its bust meter (AIPursuit::OnTask moves it towards 5), how long since a cop saw the player's car. */
const int kAiPursuit = 0xa0;
const int kPursuitCops = 0x0c;
const int kPursuitEvade = 0x1c;
const int kPursuitCoolDown = 0x20;
const int kPursuitInSight = 0x28;
const int kPursuitBusted = 0x74;
const int kPursuitDamaged = 0x94;
const int kPursuitDestroyed = 0x98;
const int kPursuitBustTime = 0xdc;
const int kPursuitLastSeen = 0x128;
const unsigned int kPursuitAiHandle = 0x4038e0;
const int kPursuitAiLastSeen = 0x74;

/** AIActionRace's vtable: the racers' (and cops' in a race) action. */
const unsigned int kRaceAction = 0x891090;
const int kPerpBaseSkill = 0x75c;
const int kPerpGlue = 0x760;
const int kGameFlowRacing = 6;

float g_period = -1.0f;
int g_pursueHeat = -1;
float g_pursueAfter;
float g_thenAfter;
float g_then[4];
bool g_thenSet;
DWORD g_pursued;
DWORD g_warped;
DWORD g_last;
/** Debug.RoadBlock=SECONDS: that long into a pursuit the cop manager is asked for a roadblock, as a pursuit asks
 * (AIPursuit::RequestRoadBlock 0x4194b0; AICopManager::UpdatePursuits 0x43e8d0 makes it). */
float g_roadBlockAfter = -1.0f;
bool g_roadBlockAsked;
DWORD g_pursuitSeen;
const int kForcedRoadBlockCops = 4;
/** Debug.Heli=SECONDS: that long into a pursuit the cop manager sends the helicopter
 * (AICopManager::SpawnPursuitHelicopter 0x4269a0). */
float g_heliAfter = -1.0f;
bool g_heliSent;

template <typename R, typename... A> R Virtual(void *object, int offset, A... args)
{
    typedef R(__thiscall * Function)(void *, A...);
    return (*reinterpret_cast<Function *>(*reinterpret_cast<char **>(object) + offset))(object, args...);
}

/** The world's collision height under a place (world axes), -1000 where it has none. */
int GroundUnder(float x, float y, float z)
{
    const float point[3] = {-y, z + 3.0f, x};
    char manager[8] = {};
    float height = 0.0f;
    return reinterpret_cast<WCollisionMgr *>(manager)->GetWorldHeightAtPoint(
               *reinterpret_cast<const UMath::Vector3 *>(point), height, nullptr)
               ? static_cast<int>(height)
               : -1000;
}

/** Debug.PursueHeat=N: starts a pursuit at that heat, Debug.PursueAfter seconds after the car's warp.
 * Debug.PursueThen=X,Y,Z,H moves the car there Debug.PursueThenAfter seconds later: cops chasing it reach a place no
 * road goes to, where no patrol would find it and none could spawn. */
void Pursue()
{
    if (g_pursueHeat < 0)
    {
        g_pursueHeat = nfs::config::Int("Debug", "PursueHeat", 0);
        g_pursueAfter = nfs::config::Float("Debug", "PursueAfter", 8.0f);
        g_thenAfter = nfs::config::Float("Debug", "PursueThenAfter", 25.0f);
        char text[64];
        nfs::config::String("Debug", "PursueThen", "", text, sizeof(text));
        const char *at = text;
        for (int i = 0; i < 4 && *at; ++i)
        {
            char number[16];
            int n = 0;
            while (*at && *at != ',' && n < 15)
                number[n++] = *at++;
            number[n] = 0;
            g_then[i] = bStrToFloat(number);
            if (*at == ',')
                ++at;
            g_thenSet = i == 3;
        }
    }
    if (g_thenSet && g_pursued && GetTickCount() - g_pursued >= static_cast<DWORD>(g_thenAfter * 1000.0f))
    {
        nfs::Log("aiwatch: car moved on");
        nfs::pose::RequestWarp(g_then[0], g_then[1], g_then[2], g_then[3]);
        g_thenSet = false;
    }
    if (g_pursueHeat == 0 || !nfs::pose::WarpFinished())
        return;
    if (!g_warped)
        g_warped = GetTickCount();
    void *manager = *reinterpret_cast<void **>(kCopManager);
    if (!manager || GetTickCount() - g_warped < static_cast<DWORD>(g_pursueAfter * 1000.0f))
        return;
    // The cops are locked out for two minutes after a start or a pursuit (AICopManager::LockoutCops).
    *reinterpret_cast<float *>(static_cast<char *>(manager) + kCopLockout) = 0.0f;
    nfs::Log("aiwatch: pursuit at heat %d", g_pursueHeat);
    Virtual<void>(manager, kCopPursueAtHeat, g_pursueHeat);
    g_pursueHeat = 0;
    g_pursued = GetTickCount();
}
} // namespace

/** Debug.AiLog: the file, when the last lines went, and the cars standing still (by vehicle) since when. */
struct AiLog
{
    int on;
    HANDLE file;
    DWORD last;
    DWORD started;
    void *still[32];
    DWORD stillSince[32];
    bool reported[32];
};

AiLog g_log = {-1, INVALID_HANDLE_VALUE, 0, 0, {}, {}, {}};

void Write(const char *format, ...)
{
    char line[512];
    va_list args;
    va_start(args, format);
    const int seconds10 = static_cast<int>((GetTickCount() - g_log.started) / 100);
    int n = wsprintfA(line, "%d.%d ", seconds10 / 10, seconds10 % 10);
    n += wvsprintfA(line + n, format, args);
    va_end(args);
    line[n++] = '\r';
    line[n++] = '\n';
    DWORD written;
    WriteFile(g_log.file, line, n, &written, nullptr);
}

/** A car standing still: its slot (for the STUCK report), and whether it was just found so. */
void Still(void *vehicle, bool still, int mode, const char *what, const char *name)
{
    int slot = -1, empty = -1;
    for (int i = 0; i < 32; ++i)
    {
        if (g_log.still[i] == vehicle)
            slot = i;
        else if (!g_log.still[i] && empty < 0)
            empty = i;
    }
    if (!still)
    {
        if (slot >= 0)
            g_log.still[slot] = nullptr;
        return;
    }
    if (slot < 0)
    {
        if (empty < 0)
            return;
        slot = empty;
        g_log.still[slot] = vehicle;
        g_log.stillSince[slot] = GetTickCount();
        g_log.reported[slot] = false;
        return;
    }
    const DWORD limit = mode == 2 ? 3000 : 5000;
    if (!g_log.reported[slot] && GetTickCount() - g_log.stillSince[slot] >= limit)
    {
        g_log.reported[slot] = true;
        Write("STUCK %s %s standing for %d s, mode %d", what, name, (GetTickCount() - g_log.stillSince[slot]) / 1000,
              mode);
    }
}

void LogAi()
{
    if (g_log.on < 0)
    {
        g_log.on = nfs::config::Int("Debug", "AiLog", 0);
        if (g_log.on)
        {
            g_log.file = CreateFileA("ai_log.txt", FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
            g_log.started = GetTickCount();
            if (g_log.file == INVALID_HANDLE_VALUE)
                g_log.on = 0;
            else
            {
                SYSTEMTIME now;
                GetLocalTime(&now);
                Write("session %04d-%02d-%02d %02d:%02d", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute);
            }
        }
    }
    if (!g_log.on || TheGameFlowManager_CurrentGameFlowState != kGameFlowRacing || GetTickCount() - g_log.last < 1000)
        return;
    float player[4];
    if (!nfs::pose::PlayerCar(player))
        return;
    g_log.last = GetTickCount();
    const char *list = reinterpret_cast<const char *>(kVehicleLists);
    void *const *items = *reinterpret_cast<void *const *const *>(list + kListItems);
    const int count = *reinterpret_cast<const int *>(list + kListCount);
    void *pursuit = nullptr;
    int traffic = 0;
    for (int i = 0; i < count; ++i)
    {
        void *vehicle = items[i];
        void *objects = *reinterpret_cast<void **>(static_cast<char *>(vehicle) + kObjects);
        void *body = NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, unsigned int))(objects, kRigidBodyHandle);
        if (!body)
            continue;
        const float *p = Virtual<const float *>(body, kBodyPosition);
        const float x = p[2], y = -p[0];
        const float dx = x - player[0], dy = y - player[1];
        const int away = static_cast<int>(sqrtf(dx * dx + dy * dy));
        const int speed = static_cast<int>(Virtual<float>(vehicle, kVehicleSpeed) * 3.6f);
        const int mode = Virtual<int>(vehicle, kVehicleMode);
        const int off = Virtual<bool>(vehicle, kVehicleOffWorld) ? 1 : 0;
        const char *name = Virtual<const char *>(vehicle, kVehicleName);
        void *ai = NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, unsigned int))(objects, kVehicleAiHandle);
        void *chasing = NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, unsigned int))(objects, kPursuitAiHandle);
        if (away < 1)
        {
            Write("player %s at %d %d speed %d km/h", name, static_cast<int>(x), static_cast<int>(y), speed);
            continue;
        }
        unsigned int actionKind = 0;
        int drive = -1;
        const char *base = nullptr;
        if (ai)
        {
            if (void *his = Virtual<void *>(ai, kAiPursuit))
                pursuit = his;
            base = static_cast<const char *>(ai) - kAiInterface;
            drive = static_cast<int>(*reinterpret_cast<const float *>(base + kAiDriveSpeed) * 3.6f);
            const char *goal = *reinterpret_cast<const char *const *>(base + kAiGoal);
            const char *action = goal ? *reinterpret_cast<const char *const *>(goal + kGoalAction) : nullptr;
            if (action)
                actionKind = *reinterpret_cast<const unsigned int *>(action);
        }
        if (chasing)
        {
            const int seen = static_cast<int>(Virtual<float>(chasing, kPursuitAiLastSeen) * 10.0f);
            Write("cop %s away %d speed %d drive %d mode %d off %d action %08x lastseen %d.%d", name, away, speed,
                  drive, mode, off, actionKind, seen / 10, seen % 10);
            Still(vehicle, speed < 4 && away > 30, mode, "cop", name);
        }
        else if (actionKind == kRaceAction && base)
        {
            const char *perp = static_cast<const char *>(ai);
            const int skill = static_cast<int>(*reinterpret_cast<const float *>(perp + kPerpBaseSkill) * 100.0f);
            const int glue = static_cast<int>(*reinterpret_cast<const float *>(perp + kPerpGlue) * 100.0f);
            Write("racer %s away %d speed %d drive %d mode %d off %d skill %d glue %d", name, away, speed, drive, mode,
                  off, skill, glue);
            Still(vehicle, speed < 4, mode, "racer", name);
        }
        else
            ++traffic;
    }
    if (pursuit)
    {
        const char *p = static_cast<const char *>(pursuit);
        Write("pursuit cops %d insight %d evade %d cooldown %d bust %d damaged %d destroyed %d",
              Virtual<int>(pursuit, kPursuitCops), Virtual<bool>(pursuit, kPursuitInSight) ? 1 : 0,
              static_cast<int>(Virtual<float>(pursuit, kPursuitEvade) * 100.0f),
              static_cast<int>(Virtual<float>(pursuit, kPursuitCoolDown)),
              static_cast<int>(*reinterpret_cast<const float *>(p + kPursuitBustTime) * 100.0f),
              Virtual<int>(pursuit, kPursuitDamaged), Virtual<int>(pursuit, kPursuitDestroyed));
    }
    Write("traffic %d", traffic);
}

/** Debug.Gps=X,Y,Z: engages the GPS to that world position (UMath axes) 10 s into driving. */
void EngageGps()
{
    static int state = -1;
    static float target[3];
    static DWORD since;
    if (state < 0)
    {
        char text[64];
        nfs::config::String("Debug", "Gps", "", text, sizeof(text));
        state = 0;
        const char *at = text;
        for (int i = 0; i < 3 && *at; ++i)
        {
            char number[16];
            int n = 0;
            while (*at && *at != ',' && n < 15)
                number[n++] = *at++;
            number[n] = 0;
            target[i] = bStrToFloat(number);
            if (*at == ',')
                ++at;
            state = i == 2 ? 1 : 0;
        }
    }
    float player[4];
    if (state != 1 || TheGameFlowManager_CurrentGameFlowState != kGameFlowRacing || !nfs::pose::PlayerCar(player))
        return;
    if (!since)
        since = GetTickCount();
    if (GetTickCount() - since < 10000)
        return;
    const bool engaged = GPS_Engage(*reinterpret_cast<const UMath::Vector3 *>(target), 0.0f);
    nfs::Log("gps: engage %d %d %d (car %d %d %d, gps %p): %d", static_cast<int>(target[0]),
             static_cast<int>(target[1]), static_cast<int>(target[2]), static_cast<int>(player[0]),
             static_cast<int>(player[1]), static_cast<int>(player[2]), *reinterpret_cast<void **>(0x90d8e4), engaged);
    state = 2;
}

void Update()
{
    Pursue();
    EngageGps();
    LogAi();
    if (g_period < 0.0f)
    {
        g_period = nfs::config::Float("Debug", "AiWatch", 0.0f);
        g_roadBlockAfter = nfs::config::Float("Debug", "RoadBlock", 0.0f);
        g_heliAfter = nfs::config::Float("Debug", "Heli", 0.0f);
    }
    if (g_period <= 0.0f || GetTickCount() - g_last < static_cast<DWORD>(g_period * 1000.0f))
        return;
    float player[4];
    if (!nfs::pose::PlayerCar(player))
        return;
    g_last = GetTickCount();
    const char *list = reinterpret_cast<const char *>(kVehicleLists);
    void *const *items = *reinterpret_cast<void *const *const *>(list + kListItems);
    const int count = *reinterpret_cast<const int *>(list + kListCount);
    const int *effects = reinterpret_cast<const int *>(&gEmitterSystem);
    nfs::Log("aiwatch: %d cars, player at %d %d %d, particles %d in %d effect groups", count,
             static_cast<int>(player[0]), static_cast<int>(player[1]), static_cast<int>(player[2]), effects[0],
             effects[0x390 / 4]);
    void *pursuit = nullptr;
    for (int i = 0; i < count; ++i)
    {
        void *vehicle = items[i];
        void *objects = *reinterpret_cast<void **>(static_cast<char *>(vehicle) + kObjects);
        void *body = NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, unsigned int))(objects, kRigidBodyHandle);
        if (!body)
            continue;
        const float *p = Virtual<const float *>(body, kBodyPosition);
        const float *v = Virtual<const float *>(body, kBodyVelocity);
        const float x = p[2], y = -p[0], z = p[1];
        const float dx = x - player[0], dy = y - player[1];
        const int mode = Virtual<int>(vehicle, kVehicleMode);
        const int off = Virtual<bool>(vehicle, kVehicleOffWorld) ? 1 : 0;
        // Its AI: the speed asked of the car, its goal's action (by its class's vtable) and that action's two speeds.
        void *ai = NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, unsigned int))(objects, kVehicleAiHandle);
        int drive = -1, actionKind = 0, actionNow = -1, actionMost = -1, seen = -1, lights = -1, view = -1;
        if (void *render = NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, unsigned int))(objects, kRenderableHandle))
            view = static_cast<int>(Virtual<float>(render, kDistanceToView));
        unsigned int goalName = 0;
        void *chasing = NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, unsigned int))(objects, kPursuitAiHandle);
        if (chasing)
        {
            seen = static_cast<int>(Virtual<float>(chasing, kPursuitAiLastSeen) * 10.0f);
            lights = Virtual<bool, unsigned int>(vehicle, kVehicleIsGlareOn, kCopLights) ? 1 : 0;
        }
        if (ai)
        {
            if (void *his = Virtual<void *>(ai, kAiPursuit))
                pursuit = his;
            const char *base = static_cast<const char *>(ai) - kAiInterface;
            drive = static_cast<int>(*reinterpret_cast<const float *>(base + kAiDriveSpeed));
            if (const unsigned int *name = Virtual<const unsigned int *>(ai, kAiGoalName))
                goalName = *name;
            const char *goal = *reinterpret_cast<const char *const *>(base + kAiGoal);
            const char *action = goal ? *reinterpret_cast<const char *const *>(goal + kGoalAction) : nullptr;
            if (action)
            {
                actionKind = *reinterpret_cast<const int *>(action);
                actionNow = static_cast<int>(*reinterpret_cast<const float *>(action + kActionSpeed));
                actionMost = static_cast<int>(*reinterpret_cast<const float *>(action + kActionMostSpeed));
            }
        }
        nfs::Log("aiwatch:   %s at %d %d %d away %d speed %d up %d mode %d off %d drive %d action %08x %d %d seen %d lights %d goal %08x view %d ground %d",
                 Virtual<const char *>(vehicle, kVehicleName), static_cast<int>(x), static_cast<int>(y),
                 static_cast<int>(z), static_cast<int>(sqrtf(dx * dx + dy * dy)),
                 static_cast<int>(Virtual<float>(vehicle, kVehicleSpeed)), static_cast<int>(v[1]), mode, off, drive,
                 actionKind, actionNow, actionMost, seen, lights, goalName, view, GroundUnder(x, y, z));
    }
    if (char *copInterface = *reinterpret_cast<char *const *>(kCopManager))
    {
        char *manager = copInterface - kCopManagerInterface;
        if (pursuit && !g_pursuitSeen)
            g_pursuitSeen = GetTickCount();
        if (pursuit && g_roadBlockAfter > 0.0f && !g_roadBlockAsked)
        {
            if (static_cast<float>(GetTickCount() - g_pursuitSeen) >= g_roadBlockAfter * 1000.0f &&
                !*reinterpret_cast<void **>(manager + kRoadBlockPursuit))
            {
                *reinterpret_cast<int *>(manager + kRoadBlockCops) = kForcedRoadBlockCops;
                *reinterpret_cast<void **>(manager + kRoadBlockPursuit) = pursuit;
                g_roadBlockAsked = true;
                nfs::Log("aiwatch: roadblock asked for (Debug.RoadBlock)");
            }
        }
        if (pursuit && g_heliAfter > 0.0f && !g_heliSent && g_pursuitSeen &&
            static_cast<float>(GetTickCount() - g_pursuitSeen) >= g_heliAfter * 1000.0f)
        {
            g_heliSent = true;
            const bool sent = AICopManagerSpawnPursuitHelicopter(manager, nullptr, pursuit);
            nfs::Log("aiwatch: helicopter %s (Debug.Heli), active helicopter %p", sent ? "sent" : "not available",
                     *reinterpret_cast<void *const *>(0x90d61c));
        }
        nfs::Log("aiwatch:   roadblock asked: %d cops for %p", *reinterpret_cast<const int *>(manager + kRoadBlockCops),
                 *reinterpret_cast<void *const *>(manager + kRoadBlockPursuit));
    }
    if (pursuit)
    {
        const char *p = static_cast<const char *>(pursuit);
        nfs::Log("aiwatch:   pursuit cops %d insight %d lastseen %d evade %d cooldown %d bust %d busted %d damaged %d "
                 "destroyed %d",
                 Virtual<int>(pursuit, kPursuitCops), Virtual<bool>(pursuit, kPursuitInSight) ? 1 : 0,
                 static_cast<int>(*reinterpret_cast<const float *>(p + kPursuitLastSeen) * 10.0f),
                 static_cast<int>(Virtual<float>(pursuit, kPursuitEvade) * 100.0f),
                 static_cast<int>(Virtual<float>(pursuit, kPursuitCoolDown)),
                 static_cast<int>(*reinterpret_cast<const float *>(p + kPursuitBustTime) * 100.0f),
                 Virtual<bool>(pursuit, kPursuitBusted) ? 1 : 0, Virtual<int>(pursuit, kPursuitDamaged),
                 Virtual<int>(pursuit, kPursuitDestroyed));
    }
}
} // namespace nfs::aiwatch
