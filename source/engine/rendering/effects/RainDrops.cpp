/**
 * The falling rain (engine/rendering/raindrops.h, assets/shaders/rain.fx, docs/RAIN.md): drops in a box around the camera drawn as
 * streaks, from a vertex buffer that never changes: every drop's place comes from its seed, the time and the camera in
 * the vertex shader. The weather's rain says how many of the buffer's drops are drawn (the first N: they are in no
 * order), up to kDraws draws of the buffer, each shifting the seeds by a number of its own. Not in the original game.
 */
#include <math.h>
#include <string.h>
#include <windows.h>

#include "game/gen/api.h"
#include "game/platform/Device.h"
#include "game/platform/Effect.h"
#include "game/platform/Platform.h"
#include "core/config.h"
#include "legacy/link.h"
#include "core/log.h"
#include "dev/pose.h"
#include "engine/rendering/raindrops.h"
#include "gameplay/world/timeofday.h"
#include "engine/rendering/vulkan/d3dx_effect.h"
#include "engine/rendering/vulkan/renderer.h"
#include "gameplay/world/weather.h"
#include "engine/rendering/x360/frame.h"
#include "engine/rendering/x360/resources.h"

extern "C" int TheGameFlowManager_CurrentGameFlowState;

namespace
{
const int kGameFlowRacing = 6;
/** Drops in the buffer (four vertices each: the most a 16-bit index reaches) and the draws of it at most. */
const int kQuads = 16384;
const int kDraws = 3;
/** The box: half its width and height, the drops' fall speed and wind, the fade in front of the camera. */
const float kHalfWidth = 11.0f, kHalfHeight = 6.0f, kFallSpeed = 9.0f, kNearFade = 1.5f;
const float kWind[2] = {0.9f, 0.36f};
/** A streak shows this much of a drop's motion (a shutter of 1/45 s), is at least this many pixels wide, and a drop is
 * this wide (m). */
const float kExposure = 1.0f / 75.0f, kMinWidth = 1.0f, kDropWidth = 0.006f;
/** The rain (0 to kHeaviestRain) at which every drop is drawn. */
const float kFullRain = 1.3f;
/** The light the streaks take: the haze's, and how opaque the brightest streak is. */
const float kLightShare = 1.2f, kOpacity = 0.5f;
/** The rain's time wraps after this many seconds (the drops' places repeat with it, unseen). */
const double kTimeWrap = 4096.0;
/** The camera's velocity follows its movement over about this many frames; a jump above kCut m/s is a cut. */
const float kVelocityShare = 0.6f, kCut = 250.0f;
/** The longest step of the rain's clock (s): a hitch or a pause does not throw the drops. */
const float kLongestStep = 0.1f;

/**
 * The splashes (docs/RAIN.md): the water the cars' wheels throw out of puddles, a pool of particles stepped on the
 * CPU. A tyre running through standing water squeezes it out sideways in a sheet that leaves the contact patch low and
 * fast, rises a little, breaks up into drops and falls back; the finest of it hangs behind the car as mist. So a wheel
 * in water throws two things:
 * - the sheet: out to the wheel's side in a fan between kSheetLow and kSheetHigh above the road, most of it slow and
 *   low (its dense base), a little fast (its ragged edge), carried on with a share of the car's speed. It is drawn as
 *   soft drops, thick at first and thinner as they break up, in a white haze of finer water that flies with them
 *   (puffs: soft round sprites that grow as they thin out);
 * - the mist: puffs from behind the contact patch, drifting out and up, stopped by the air within a metre or two and
 *   left hanging behind the car.
 * A wheel that enters a puddle throws a burst. The front wheels throw the most: the rear ones run in their track.
 *
 * The cars' rigid bodies give position and velocity (simulation axes, Y up: the renderer's X is z, Y is -x, Z is y);
 * every car within kSplashRange of the camera splashes, the player's with its heading (so that it can be looked at
 * standing still with Debug.SplashAnywhere), the others along their motion. The puddles are lighting.hlsli's.
 */
const int kMaxSplashes = 8192;
const unsigned int kRigidBodyHandle = 0x4039f0;
const int kBodyPosition = 0x20;
const int kBodyLinearVelocity = 0x24;
/** ListableSet<IVehicle>'s list of every vehicle: its items at +4, their count at +0xc; a vehicle's objects at +4. */
const unsigned int kVehicleLists = 0x92cd18;
const int kMaxCars = 48;
const float kSplashRange = 70.0f;
const float kWheelAhead = 1.3f, kWheelSide = 0.78f, kWheelDrop = 0.35f;
const float kPuddleCover = 0.2f, kPuddleSize = 12.0f, kPuddleEdgeSize = 2.5f, kPuddleEdge = 0.1f;
const float kSplashFrom = 0.12f, kSplashSlowest = 4.0f, kFastSpeed = 25.0f, kThrowTop = 28.0f;
/** Sheet drops a second from a front wheel at kFastSpeed in deep water, the rear wheels' share, and the burst on
 * entering a puddle. */
const float kSheetRate = 520.0f, kRearShare = 0.45f, kEntryBurst = 28.0f;
/** A sheet drop's speed out of the contact patch as a share of the car's (the slowest, and how much the fastest add),
 * the fan's lowest and highest angle above the road (sine), and the share of the car's speed it is carried on with. */
const float kSheetSlow = 0.14f, kSheetFast = 0.34f, kSheetLow = 0.16f, kSheetHigh = 0.72f;
const float kCarriedLeast = 0.22f, kCarriedMost = 0.5f;
const float kLifeLeast = 0.3f, kLifeMost = 0.75f, kSplashWidthLeast = 0.04f, kSplashWidthMost = 0.1f;
/** The sheet's haze: a puff for this share of its drops, at this share of the drop's speed, its first radius and its
 * growth (m, m/s), life (s), the air's drag on it (1/s), how fast it falls (m/s2) and how opaque it is. */
const float kHazeShare = 0.35f, kHazeSpeed = 0.85f, kHazeRadius = 0.09f, kHazeGrowth = 1.3f;
const float kHazeLifeLeast = 0.3f, kHazeLifeMost = 0.6f, kHazeDrag = 1.6f, kHazeFall = 4.5f, kHazeOpacity = 0.12f;
/** How much of its width a sheet drop keeps at the end of its life. */
const float kSheetThinning = 0.35f;
const float kGravity = 9.8f, kAirDrag = 0.9f, kSplashStreak = 1.0f / 90.0f, kShortestStreak = 0.09f;
/** Mist puffs a second from a wheel at kFastSpeed, their first radius and its growth (m, m/s), life (s), the share of
 * the car's speed they start with, their drift out and up (m/s), the air's drag on them (1/s), how fast they sink
 * (m/s2) and how opaque the densest is. */
const float kMistRate = 70.0f, kMistRadius = 0.14f, kMistGrowth = 0.85f, kMistLifeLeast = 0.7f, kMistLifeMost = 1.5f;
const float kMistCarried = 0.38f, kMistOut = 2.4f, kMistUp = 1.9f, kMistDrag = 3.2f, kMistSink = 1.4f;
const float kMistOpacity = 0.15f, kMistFadeIn = 0.07f;

struct Vertex
{
    float seed[3];
    D3DCOLOR corner;
};

const D3DVERTEXELEMENT9 kElements[] = {
    {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
    D3DDECL_END(),
};

struct Splash
{
    float position[3], velocity[3];
    /** `width`: a sheet drop's width, a mist puff's first radius; `strength`: how much water it stands for (0 to 1). */
    float age, life, width, floorZ, strength;
    /** A puff (mist, the sheet's haze): how fast it grows, the air's drag on it and how fast it falls. */
    bool mist;
    float growth, drag, fall;
};

/** A car's wheels between frames: the drops and puffs owed (fractions carried on) and whether each was in water. */
struct CarWheels
{
    void *body;
    float sheet[4], puffs[4];
    bool wet[4];
    int frame;
};

struct SplashVertex
{
    float head[3];
    D3DCOLOR corner;
    float tail[3];
    float width, fade;
};

const D3DVERTEXELEMENT9 kSplashElements[] = {
    {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
    {0, 16, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    {0, 28, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
    D3DDECL_END(),
};

struct Drops
{
    bool tried;
    ID3DXEffect *effect;
    D3DXHANDLE technique, splashTechnique, worldScreen, camera, motion, box, wind, screen, tint;
    void *declaration, *splashDeclaration;
    nfs::vk::GeometryBuffer *vertices, *indices;
    double seconds;
    int frame;
    float eye[3];
    float velocity[3];
    bool haveEye;
    LARGE_INTEGER clock;
    bool haveClock;
    Splash splashes[kMaxSplashes];
    int splashCount;
    SplashVertex *splashVertices;
    CarWheels cars[kMaxCars];
    int splashFrame;
    unsigned int random;
};

Drops g_drops;

unsigned int Random(unsigned int &state)
{
    state = state * 1664525u + 1013904223u;
    return state >> 8;
}

bool Create()
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    g_drops.tried = true;
    g_drops.effect = nfs::vk::CreateEffect("NFS_rain");
    if (!g_drops.effect)
    {
        nfs::Log("rain: no effect NFS_rain");
        return false;
    }
    ID3DXEffect *e = g_drops.effect;
    g_drops.technique = e->GetTechniqueByName("drops");
    g_drops.splashTechnique = e->GetTechniqueByName("splash");
    g_drops.worldScreen = e->GetParameterByName(nullptr, "WorldScreen");
    g_drops.camera = e->GetParameterByName(nullptr, "Camera");
    g_drops.motion = e->GetParameterByName(nullptr, "CameraMotion");
    g_drops.box = e->GetParameterByName(nullptr, "Box");
    g_drops.wind = e->GetParameterByName(nullptr, "Wind");
    g_drops.screen = e->GetParameterByName(nullptr, "Screen");
    g_drops.tint = e->GetParameterByName(nullptr, "Tint");
    Vertex *vertices = static_cast<Vertex *>(::operator new(sizeof(Vertex) * kQuads * 4));
    unsigned short *indices = static_cast<unsigned short *>(::operator new(sizeof(unsigned short) * kQuads * 6));
    unsigned int state = 12345;
    for (int q = 0; q < kQuads; ++q)
    {
        float seed[3];
        for (float &s : seed)
            s = static_cast<float>(Random(state) & 0xffffff) / 16777216.0f;
        const unsigned int a = Random(state) & 0xff, b = Random(state) & 0xff;
        for (int c = 0; c < 4; ++c)
        {
            Vertex &v = vertices[q * 4 + c];
            memcpy(v.seed, seed, sizeof(seed));
            const unsigned int side = c & 1 ? 255u : 0u, tail = c & 2 ? 255u : 0u;
            v.corner = (a << 24) | (side << 16) | (tail << 8) | b;
        }
        unsigned short *i = indices + q * 6;
        const unsigned short base = static_cast<unsigned short>(q * 4);
        i[0] = base;
        i[1] = base + 1;
        i[2] = base + 2;
        i[3] = base + 2;
        i[4] = base + 1;
        i[5] = base + 3;
    }
    g_drops.vertices = r->CreateGeometryBuffer(vertices, sizeof(Vertex) * kQuads * 4, false);
    g_drops.indices = r->CreateGeometryBuffer(indices, sizeof(unsigned short) * kQuads * 6, true);
    ::operator delete(vertices);
    ::operator delete(indices);
    g_drops.declaration = r->CreateVertexLayout(kElements);
    g_drops.splashDeclaration = r->CreateVertexLayout(kSplashElements);
    g_drops.splashVertices = static_cast<SplashVertex *>(::operator new(sizeof(SplashVertex) * kMaxSplashes * 4));
    g_drops.random = 98765;
    return g_drops.vertices && g_drops.indices && g_drops.technique;
}

/** [0, 1) for a cell of a pattern (lighting.hlsli RainHash). */
float CellHash(float cx, float cy)
{
    unsigned int x = static_cast<unsigned int>(static_cast<int>(cx));
    unsigned int y = static_cast<unsigned int>(static_cast<int>(cy));
    unsigned int h = x * 1597334677u ^ y * 3812015801u;
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xffffffu) / 16777216.0f;
}

/** Smooth noise over the ground (lighting.hlsli RainNoise). */
float CellNoise(float x, float y)
{
    const float fx = floorf(x), fy = floorf(y);
    const float u = x - fx, v = y - fy;
    const float sx = u * u * (3.0f - 2.0f * u), sy = v * v * (3.0f - 2.0f * v);
    const float a = CellHash(fx, fy), b = CellHash(fx + 1.0f, fy), c = CellHash(fx, fy + 1.0f),
                d = CellHash(fx + 1.0f, fy + 1.0f);
    const float top = a + (b - a) * sx, bottom = c + (d - c) * sx;
    return top + (bottom - top) * sy;
}

/** How much water stands at (x, y) (lighting.hlsli RainPuddles on flat ground in the open), 0 to 1. */
float PuddleAt(float x, float y, float standing)
{
    const float ground = (1.0f - kPuddleEdge) * CellNoise(x / kPuddleSize, y / kPuddleSize) +
                         kPuddleEdge * CellNoise(x / kPuddleEdgeSize + 17.0f, y / kPuddleEdgeSize + 17.0f);
    const float level = standing * kPuddleCover;
    float t = (ground - (level - 0.04f)) / 0.04f;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

float Unit()
{
    return static_cast<float>(Random(g_drops.random) & 0xffff) / 65536.0f;
}

template <typename R> R BodyVirtual(void *object, int offset)
{
    typedef R(__thiscall * Function)(void *);
    return (*reinterpret_cast<Function *>(*reinterpret_cast<char **>(object) + offset))(object);
}

/** The wheels' state of the car with this rigid body, a new one (or the longest unused) when it has none. */
CarWheels &WheelsOf(void *body)
{
    CarWheels *oldest = &g_drops.cars[0];
    for (CarWheels &c : g_drops.cars)
    {
        if (c.body == body)
        {
            c.frame = g_drops.splashFrame;
            return c;
        }
        if (c.frame < oldest->frame)
            oldest = &c;
    }
    *oldest = CarWheels();
    oldest->body = body;
    oldest->frame = g_drops.splashFrame;
    return *oldest;
}

/** What one car's wheels throw this step: the car's place and velocity, the way it points (unit, on the ground), its
 * speed along the ground, how much water stands (0 to 1), and `share` of the full amount (cars further off throw less,
 * to keep the pool for the near ones). */
void SplashCar(CarWheels &wheels, const float place[3], const float velocity[3], const float forward[2], float speed,
               float standing, bool anywhere, float share, float dt)
{
    const float right[2] = {forward[1], -forward[0]};
    const float pace = speed / kFastSpeed < 1.6f ? speed / kFastSpeed : 1.6f;
    // What a tyre throws grows with speed up to a point: the sheets at 220 km/h reach not much further than at 90.
    const float thrown = speed < kThrowTop ? speed : kThrowTop;
    for (int wheel = 0; wheel < 4; ++wheel)
    {
        const bool rear = (wheel & 1) != 0;
        const float ahead = rear ? -kWheelAhead : kWheelAhead;
        const float side = wheel & 2 ? -1.0f : 1.0f;
        const float at[3] = {place[0] + forward[0] * ahead + right[0] * side * kWheelSide,
                             place[1] + forward[1] * ahead + right[1] * side * kWheelSide, place[2] - kWheelDrop};
        const float water = anywhere ? 1.0f : PuddleAt(at[0], at[1], standing);
        if (water < kSplashFrom)
        {
            wheels.sheet[wheel] = wheels.puffs[wheel] = 0.0f;
            wheels.wet[wheel] = false;
            continue;
        }
        const float amount = water * pace * share * (rear ? kRearShare : 1.0f);
        wheels.sheet[wheel] += kSheetRate * amount * dt;
        wheels.puffs[wheel] += kMistRate * amount * dt;
        if (!wheels.wet[wheel])
            wheels.sheet[wheel] += kEntryBurst * amount;
        wheels.wet[wheel] = true;
        while (wheels.sheet[wheel] >= 1.0f && g_drops.splashCount < kMaxSplashes)
        {
            wheels.sheet[wheel] -= 1.0f;
            Splash &s = g_drops.splashes[g_drops.splashCount++];
            const float along = (Unit() - 0.5f) * 0.45f, across = Unit() * 0.12f * side;
            s.position[0] = at[0] + forward[0] * along + right[0] * across;
            s.position[1] = at[1] + forward[1] * along + right[1] * across;
            s.position[2] = at[2] + 0.02f;
            s.floorZ = at[2] - 0.03f;
            // Most drops are slow and low (the sheet's base), few are fast: the square of an even number.
            const float u = Unit(), fast = u * u;
            const float out = thrown * (kSheetSlow + kSheetFast * fast);
            // The fan: its angle above the road, the faster drops a little flatter.
            const float rise = kSheetLow + (kSheetHigh - kSheetLow) * Unit() * (1.0f - 0.4f * fast);
            const float flat = sqrtf(1.0f - rise * rise);
            const float carried = kCarriedLeast + (kCarriedMost - kCarriedLeast) * Unit();
            // A little of the sheet leaves ahead of the wheel's side and a little behind it.
            const float swing = (Unit() - 0.45f) * 0.5f;
            s.velocity[0] = velocity[0] * carried + (right[0] * side + forward[0] * swing) * out * flat;
            s.velocity[1] = velocity[1] * carried + (right[1] * side + forward[1] * swing) * out * flat;
            s.velocity[2] = 0.4f + out * rise;
            s.age = 0.0f;
            s.life = kLifeLeast + (kLifeMost - kLifeLeast) * Unit();
            s.width = kSplashWidthLeast + (kSplashWidthMost - kSplashWidthLeast) * Unit() * (1.0f - 0.5f * fast);
            s.strength = 1.0f;
            s.mist = false;
            s.growth = s.drag = s.fall = 0.0f;
            if (Unit() < kHazeShare && g_drops.splashCount < kMaxSplashes)
            {
                // The haze the sheet flies in.
                Splash &h = g_drops.splashes[g_drops.splashCount++];
                h = s;
                for (int a = 0; a < 3; ++a)
                    h.velocity[a] = s.velocity[a] * kHazeSpeed;
                h.position[2] += kHazeRadius;
                h.life = kHazeLifeLeast + (kHazeLifeMost - kHazeLifeLeast) * Unit();
                h.width = kHazeRadius * (0.7f + 0.6f * Unit());
                h.strength = (water < 1.0f ? water : 1.0f) * kHazeOpacity;
                h.mist = true;
                h.growth = kHazeGrowth;
                h.drag = kHazeDrag;
                h.fall = kHazeFall;
            }
        }
        while (wheels.puffs[wheel] >= 1.0f && g_drops.splashCount < kMaxSplashes)
        {
            wheels.puffs[wheel] -= 1.0f;
            Splash &s = g_drops.splashes[g_drops.splashCount++];
            const float behind = 0.25f + Unit() * 0.35f;
            s.position[0] = at[0] - forward[0] * behind + right[0] * (Unit() - 0.5f) * 0.3f;
            s.position[1] = at[1] - forward[1] * behind + right[1] * (Unit() - 0.5f) * 0.3f;
            s.position[2] = at[2] + 0.1f + Unit() * 0.15f;
            s.floorZ = at[2];
            const float out = kMistOut * (Unit() - 0.25f) * side * pace;
            s.velocity[0] = velocity[0] * kMistCarried + right[0] * out;
            s.velocity[1] = velocity[1] * kMistCarried + right[1] * out;
            s.velocity[2] = kMistUp * (0.4f + Unit()) * pace;
            s.age = 0.0f;
            s.life = kMistLifeLeast + (kMistLifeMost - kMistLifeLeast) * Unit();
            s.width = kMistRadius * (0.7f + 0.6f * Unit());
            s.strength = (water < 1.0f ? water : 1.0f) * kMistOpacity;
            s.mist = true;
            s.growth = kMistGrowth;
            s.drag = kMistDrag;
            s.fall = kMistSink;
        }
    }
}

/** One step of the splashes: what is in the air moves on, and the wheels in puddles throw more. */
void StepSplashes(float dt)
{
    ++g_drops.splashFrame;
    for (int i = 0; i < g_drops.splashCount;)
    {
        Splash &s = g_drops.splashes[i];
        s.age += dt;
        bool gone = s.age > s.life;
        if (s.mist)
        {
            // Fine drops: the air slows them quickly, and they sink slowly.
            const float drag = 1.0f - s.drag * dt > 0.0f ? 1.0f - s.drag * dt : 0.0f;
            for (int a = 0; a < 3; ++a)
                s.velocity[a] *= drag;
            s.velocity[2] -= s.fall * dt;
            for (int a = 0; a < 3; ++a)
                s.position[a] += s.velocity[a] * dt;
            // A puff rests on the road rather than sinking into it.
            const float least = s.floorZ + (s.width + s.growth * s.age) * 0.55f;
            if (s.position[2] < least)
                s.position[2] = least;
        }
        else
        {
            const float drag = 1.0f - kAirDrag * dt;
            s.velocity[0] *= drag;
            s.velocity[1] *= drag;
            s.velocity[2] = s.velocity[2] * drag - kGravity * dt;
            for (int a = 0; a < 3; ++a)
                s.position[a] += s.velocity[a] * dt;
            gone = gone || s.position[2] < s.floorZ;
        }
        if (gone)
            s = g_drops.splashes[--g_drops.splashCount];
        else
            ++i;
    }
    // Debug.SplashAnywhere=1: every wheel of the player's car is in a puddle (to look at the splashes where no puddle
    // is).
    static const bool anywhere = nfs::config::Int("Debug", "SplashAnywhere", 0) != 0;
    const float standing = anywhere ? 1.0f : nfs::weather::Puddles();
    if (dt <= 1e-4f || standing <= 0.02f || !nfs::weather::RainShown() || !g_drops.haveEye)
        return;
    void *player = nfs::pose::PlayerInterface(kRigidBodyHandle);
    float car[4];
    if (player && nfs::pose::PlayerCar(car))
    {
        const float *sim = BodyVirtual<const float *>(player, kBodyLinearVelocity);
        const float velocity[3] = {sim[2], -sim[0], sim[1]};
        float speed = sqrtf(velocity[0] * velocity[0] + velocity[1] * velocity[1]);
        if (anywhere && speed < kSplashSlowest)
            speed = 15.0f;
        if (speed >= kSplashSlowest)
        {
            const unsigned short heading = static_cast<unsigned short>(static_cast<int>(car[3] * (65536.0f / 360.0f)));
            const float forward[2] = {bCos(heading), bSin(heading)};
            SplashCar(WheelsOf(player), car, velocity, forward, speed, standing, anywhere, 1.0f, dt);
        }
    }
    // The other cars near the camera, along their motion.
    const char *list = reinterpret_cast<const char *>(kVehicleLists);
    void *const *items = *reinterpret_cast<void *const *const *>(list + 4);
    const int count = *reinterpret_cast<const int *>(list + 0xc);
    for (int i = 0; items && i < count && i < 256; ++i)
    {
        void *objects = items[i] ? *reinterpret_cast<void **>(static_cast<char *>(items[i]) + 4) : nullptr;
        void *body = objects ? NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, unsigned int))(objects, kRigidBodyHandle)
                             : nullptr;
        if (!body || body == player)
            continue;
        const float *p = BodyVirtual<const float *>(body, kBodyPosition);
        const float place[3] = {p[2], -p[0], p[1]};
        const float dx = place[0] - g_drops.eye[0], dy = place[1] - g_drops.eye[1];
        const float distance2 = dx * dx + dy * dy;
        if (distance2 > kSplashRange * kSplashRange)
            continue;
        const float *sim = BodyVirtual<const float *>(body, kBodyLinearVelocity);
        const float velocity[3] = {sim[2], -sim[0], sim[1]};
        const float speed = sqrtf(velocity[0] * velocity[0] + velocity[1] * velocity[1]);
        if (speed < kSplashSlowest)
            continue;
        const float forward[2] = {velocity[0] / speed, velocity[1] / speed};
        // Less from the cars further off: half at the range's end.
        const float share = 1.0f - 0.5f * distance2 / (kSplashRange * kSplashRange);
        SplashCar(WheelsOf(body), place, velocity, forward, speed, standing, false, share * 0.7f, dt);
    }
}

/** The rain's time and the camera's velocity, once a frame. */
void Advance(const float eye[3])
{
    // The real clock: the world's time (WorldTimeElapsed) is the simulation's step, which a frame drawn between two
    // steps does not see move, and the rain froze.
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    float dt = g_drops.haveClock ? static_cast<float>(static_cast<double>(now.QuadPart - g_drops.clock.QuadPart) /
                                                      static_cast<double>(frequency.QuadPart))
                                 : 0.0f;
    g_drops.clock = now;
    g_drops.haveClock = true;
    dt = dt < 0.0f ? 0.0f : dt > kLongestStep ? kLongestStep : dt;
    if (dt <= 1e-5f)
        return;
    float velocity[3] = {};
    if (dt > 1e-4f && g_drops.haveEye)
    {
        for (int a = 0; a < 3; ++a)
            velocity[a] = (eye[a] - g_drops.eye[a]) / dt;
        const float speed = sqrtf(velocity[0] * velocity[0] + velocity[1] * velocity[1] + velocity[2] * velocity[2]);
        if (speed > kCut)
            velocity[0] = velocity[1] = velocity[2] = 0.0f;
    }
    for (int a = 0; a < 3; ++a)
        g_drops.velocity[a] += (velocity[a] - g_drops.velocity[a]) * kVelocityShare;
    memcpy(g_drops.eye, eye, sizeof(g_drops.eye));
    g_drops.haveEye = true;
    g_drops.seconds += dt;
    if (g_drops.seconds > kTimeWrap)
        g_drops.seconds -= kTimeWrap;
    StepSplashes(dt);
}
} // namespace

namespace nfs::x360
{
void DrawRainDrops(eView *view)
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    static const bool enabled = nfs::config::Int("World", "RainDrops", 1) != 0;
    if (!r || !enabled || TheGameFlowManager_CurrentGameFlowState != kGameFlowRacing || !view || !view->pCamera ||
        !view->PlatInfo)
        return;
    if (!g_drops.tried && !Create())
        return;
    if (!g_drops.vertices)
        return;
    const bVector3 &position = view->pCamera->CurrentKey.Position;
    const float eye[3] = {position.x, position.y, position.z};
    Advance(eye);
    float rain = weather::RainShown() ? weather::Rain() : 0.0f;
    rain = rain > kFullRain ? kFullRain : rain;
    int quads = static_cast<int>(rain / kFullRain * static_cast<float>(kQuads * kDraws));
    const int splashes = g_drops.splashCount;
    if (quads < 32 && splashes == 0)
        return;
    static const bool debug = nfs::vk::HasArgument("-weatherdebug");
    static DWORD logged;
    if (debug && GetTickCount() - logged > 2000)
    {
        logged = GetTickCount();
        nfs::Log("rain: time %d.%02d s", static_cast<int>(g_drops.seconds),
                 static_cast<int>(g_drops.seconds * 100.0) % 100);
        nfs::Log("rain: %d drops, %d splashes, camera speed %d m/s", quads, splashes,
                 static_cast<int>(sqrtf(g_drops.velocity[0] * g_drops.velocity[0] + g_drops.velocity[1] * g_drops.velocity[1] +
                                        g_drops.velocity[2] * g_drops.velocity[2])));
    }
    ID3DXEffect *e = g_drops.effect;
    float light[4];
    timeofday::HazeLight(light);
    const float tint[4] = {light[0] * kLightShare, light[1] * kLightShare, light[2] * kLightShare, kOpacity};
    const float camera[4] = {eye[0], eye[1], eye[2], static_cast<float>(g_drops.seconds)};
    const float motion[4] = {g_drops.velocity[0], g_drops.velocity[1], g_drops.velocity[2], kExposure};
    const float box[4] = {kHalfWidth, kHalfHeight, kFallSpeed, kNearFade};
    const float screenWidth = static_cast<float>(g_renderWidth), screenHeight = static_cast<float>(g_renderHeight);
    // The projection's y is flipped (negative): pixels per metre at a distance of one metre.
    const float focal = fabsf(view->PlatInfo->ViewScreenMatrix.v1.y) * screenHeight * 0.5f;
    const float screen[4] = {screenWidth, screenHeight, focal, kDropWidth};
    if (debug && logged == GetTickCount())
        nfs::Log("rain: viewport %d x %d, projection y scale %d/1000, focal %d px, x scale %d/1000",
                 static_cast<int>(screenWidth), static_cast<int>(screenHeight),
                 static_cast<int>(view->PlatInfo->ViewScreenMatrix.v1.y * 1000.0f), static_cast<int>(focal),
                 static_cast<int>(view->PlatInfo->ViewScreenMatrix.v0.x * 1000.0f));
    e->SetMatrix(g_drops.worldScreen, reinterpret_cast<const float *>(&view->PlatInfo->WorldScreenMatrix));
    e->SetVector(g_drops.camera, camera);
    e->SetVector(g_drops.motion, motion);
    e->SetVector(g_drops.box, box);
    e->SetVector(g_drops.screen, screen);
    e->SetVector(g_drops.tint, tint);
    if (quads >= 32)
    {
        e->SetTechnique(g_drops.technique);
        r->SetVertexLayout(static_cast<nfs::vk::VertexLayout *>(g_drops.declaration));
        r->SetStreamSource(0, g_drops.vertices, 0, sizeof(Vertex));
        r->SetIndices(g_drops.indices);
        e->Begin(nullptr, 0);
        e->BeginPass(0);
        for (int draw = 0; quads > 0 && draw < kDraws; ++draw, quads -= kQuads)
        {
            const int n = quads < kQuads ? quads : kQuads;
            const float wind[4] = {kWind[0], kWind[1], static_cast<float>(draw), kMinWidth};
            e->SetVector(g_drops.wind, wind);
            e->CommitChanges();
            r->DrawIndexed(D3DPT_TRIANGLELIST, 0, 0, static_cast<uint32_t>(n * 4), 0, static_cast<uint32_t>(n * 2));
        }
        e->EndPass();
        e->End();
        r->SetStreamSource(0, nullptr, 0, 0);
    }
    if (splashes > 0 && g_drops.splashTechnique)
    {
        SplashVertex *out = g_drops.splashVertices;
        for (int i = 0; i < splashes; ++i)
        {
            const Splash &s = g_drops.splashes[i];
            const float spent = s.age / s.life;
            float head[3], tail[3], width, fade;
            unsigned int kind;
            if (s.mist)
            {
                // A round sprite: the quad from its top to its bottom, as wide as it is high; the blue channel tells
                // the shaders.
                const float radius = s.width + s.growth * s.age;
                for (int a = 0; a < 3; ++a)
                    head[a] = tail[a] = s.position[a];
                head[2] += radius;
                tail[2] -= radius;
                width = radius * 2.0f;
                const float in = s.age < kMistFadeIn ? s.age / kMistFadeIn : 1.0f, left = 1.0f - spent;
                fade = s.strength * in * left * sqrtf(left);
                kind = 255u;
            }
            else
            {
                const float length = sqrtf(s.velocity[0] * s.velocity[0] + s.velocity[1] * s.velocity[1] +
                                           s.velocity[2] * s.velocity[2]);
                const float reach = length * kSplashStreak > kShortestStreak
                                        ? kSplashStreak
                                        : kShortestStreak / (length > 1e-3f ? length : 1.0f);
                for (int a = 0; a < 3; ++a)
                {
                    head[a] = s.position[a];
                    tail[a] = s.position[a] - s.velocity[a] * reach;
                }
                // The sheet breaks up: its drops thin out as they fly.
                width = s.width * (1.0f - (1.0f - kSheetThinning) * spent);
                fade = 1.0f - spent;
                kind = 0u;
            }
            for (int c = 0; c < 4; ++c, ++out)
            {
                memcpy(out->head, head, sizeof(out->head));
                memcpy(out->tail, tail, sizeof(out->tail));
                out->corner = ((c & 1 ? 255u : 0u) << 16) | ((c & 2 ? 255u : 0u) << 8) | kind;
                out->width = width;
                out->fade = fade;
            }
        }
        if (debug && logged == GetTickCount())
        {
            const SplashVertex &v = g_drops.splashVertices[0];
            nfs::Log("rain: first splash at %d %d %d (camera %d %d %d), tail %d %d %d, width %d mm, fade %d%%",
                     static_cast<int>(v.head[0]), static_cast<int>(v.head[1]), static_cast<int>(v.head[2]),
                     static_cast<int>(eye[0]), static_cast<int>(eye[1]), static_cast<int>(eye[2]),
                     static_cast<int>(v.tail[0]), static_cast<int>(v.tail[1]), static_cast<int>(v.tail[2]),
                     static_cast<int>(v.width * 1000.0f), static_cast<int>(v.fade * 100.0f));
        }
        const float wind[4] = {kWind[0], kWind[1], 0.0f, kMinWidth};
        e->SetVector(g_drops.wind, wind);
        e->SetTechnique(g_drops.splashTechnique);
        r->SetVertexLayout(static_cast<nfs::vk::VertexLayout *>(g_drops.splashDeclaration));
        e->Begin(nullptr, 0);
        e->BeginPass(0);
        e->CommitChanges();
        r->DrawIndexedUP(D3DPT_TRIANGLELIST, 0, static_cast<uint32_t>(splashes * 4), 0,
                         static_cast<uint32_t>(splashes * 2), g_drops.splashVertices, sizeof(SplashVertex),
                         g_drops.indices);
        e->EndPass();
        e->End();
    }
}

void ReleaseRainDrops()
{
    if (nfs::vk::Renderer *r = nfs::vk::g_renderer)
    {
        if (g_drops.vertices)
            r->ReleaseGeometryBuffer(g_drops.vertices);
        if (g_drops.indices)
            r->ReleaseGeometryBuffer(g_drops.indices);
    }
    if (g_drops.effect)
        g_drops.effect->Release();
    ::operator delete(g_drops.splashVertices);
    g_drops = Drops();
}
} // namespace nfs::x360
