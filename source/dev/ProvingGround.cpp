/**
 * The proving ground (dev/proving.h, docs/PROVING_GROUND.md). Not in the original game.
 *
 * Far from Rockport, where nothing of the city reaches, the collision code is given flat ground over the whole site
 * (source/dev/TestTrack.cpp answers WWorldPos::Update and WCollisionMgr::GetWorldHeightAtPoint for it). On it lie two
 * roads, centre lines sampled as polylines in the renderer's axes (Z up): the straight, 2000 m from its start line to
 * its timed board and on to 6000 m (top speed, then a braking zone), with a run-up behind, and the handling loop, a closed run of straights and
 * arcs (each arc cut into chords of at most kArcStep metres; two of its straights are sized so the loop closes).
 *
 * The walls are not in the collision code: every simulation step the car's four corners are measured against the road
 * the car is on, and a corner past a wall puts the car back inside and takes away (most of) its speed into the wall.
 *
 * Everything is drawn as world quads in a handful of batches (one per texture), only what lies within kDrawDistance
 * of the car: asphalt, grass, lines, kerbs, concrete walls with a painted band, tyre walls round the tightest corners,
 * distance posts and the start lines.
 */
#include <math.h>
#include <string.h>
#include <windows.h>

#include <new>

#include "game/gen/api.h"
#include "core/config.h"
#include "dev/dev.h"
#include "dev/testprofile.h"
#include "engine/rendering/display/display.h"
#include "legacy/link.h"
#include "core/log.h"
#include "dev/pose.h"
#include "dev/proving.h"
#include "gameplay/vehicles/realism.h"
#include "gameplay/world/timeofday.h"
#include "gameplay/vehicles/tuning.h"
#include "gameplay/world/weather.h"
#include "engine/rendering/worldquads.h"
#include "engine/resources/pack.h"

extern "C" int TheGameFlowManager_CurrentGameFlowState;

namespace nfs::proving
{
namespace
{
const int kGameFlowFrontEnd = 3;
const int kGameFlowRacing = 6;
/** cFrontendDatabase::FEGameMode's quick race bit, and the quick race's sprint (GRace::Type). */
const unsigned int kGameModeQuickRace = 4;
const int kRaceTypeSprint = 0;
const char kCarSelectPackage[] = "Car_Select.fng";
const char kMainMenuPackage[] = "MainMenu.fng";
/** Packages that come and go over the car select (dialogs, the memory card, loading screens). */
const char *const kPassingPackages[] = {"MC_", "InGame_MC_", "Keyboard", "Dialog", "GenericDialog", "InGameDialog",
                                        "Autosave_Overlay", "Loading", "LOADING", "PC_Loading", "WS_Loading",
                                        "FadeScreen", "LS_", "MW_LS_"};

/** The straight: its start line at (kStraightX, kStraightY), running along +X. */
const float kStraightX = 20000.0f;
const float kStraightY = 21000.0f;
const float kStraightLength = 2000.0f;
/** The straight goes on past its 2000 m board to kStraightEnd, so a car reaches its top speed (drag or the rev limiter
 * in top gear); its last kBrakeZone metres are for stopping. */
const float kStraightEnd = 6000.0f;
const float kBrakeZone = 1000.0f;
/** A run's top speed: the speed has not risen by kTopRise for kTopSeconds. */
const float kTopRise = 0.5f / 3.6f;
const float kTopSeconds = 8.0f;
const float kRunUp = 80.0f;
const float kRunOff = kStraightEnd - kStraightLength;
const float kStraightHalf = 12.0f;
const float kStraightWall = 15.0f;
const float kStraightStep = 10.0f;
/** The loop: its start line at (kLoopX, kLoopY), heading +X. */
const float kLoopX = 20100.0f;
const float kLoopY = 21080.0f;
const float kLoopHalf = 6.0f;
const float kLoopWall = 8.5f;
const float kArcStep = 3.0f;
const float kLoopStraightStep = 10.0f;
/** The flat ground: the whole site. */
const float kGroundMinX = 19850.0f;
const float kGroundMaxX = 26200.0f;
const float kGroundMinY = 20940.0f;
const float kGroundMaxY = 21540.0f;
const float kHeight = 0.0f;
const float kBelow = 20.0f;

/** One piece of the loop: a straight (radius 0) or an arc turning `degrees` (left positive). */
struct Piece
{
    float length;
    float radius;
    float degrees;
    const char *name;
};

const Piece kLoop[] = {
    {576.907f, 0.0f, 0.0f, "MAIN STRAIGHT"},
    {0.0f, 16.0f, 180.0f, "HAIRPIN"},
    {140.0f, 0.0f, 0.0f, "BACK STRAIGHT"},
    {0.0f, 160.0f, -50.0f, "FAST SWEEPER"},
    {50.0f, 0.0f, 0.0f, "FAST SWEEPER"},
    {0.0f, 16.0f, 55.0f, "CHICANE"},
    {0.0f, 16.0f, -55.0f, "CHICANE"},
    {70.0f, 0.0f, 0.0f, "ESSES"},
    {0.0f, 40.0f, -60.0f, "ESSES"},
    {0.0f, 40.0f, 120.0f, "ESSES"},
    {0.0f, 40.0f, -60.0f, "ESSES"},
    {50.0f, 0.0f, 0.0f, "DECREASING RADIUS"},
    {0.0f, 100.0f, 50.0f, "DECREASING RADIUS"},
    {0.0f, 55.0f, 45.0f, "DECREASING RADIUS"},
    {0.0f, 28.0f, 45.0f, "DECREASING RADIUS"},
    {180.366f, 0.0f, 0.0f, "WEST STRAIGHT"},
    {0.0f, 20.0f, -50.0f, "BUS STOP"},
    {0.0f, 20.0f, 100.0f, "BUS STOP"},
    {0.0f, 20.0f, -50.0f, "BUS STOP"},
    {60.0f, 0.0f, 0.0f, "FINAL CORNER"},
    {0.0f, 35.0f, 90.0f, "FINAL CORNER"},
};
const int kLoopPieces = sizeof(kLoop) / sizeof(kLoop[0]);
const int kMaxSamples = 1024;

/** Corners of the car about its centre (half length, half width), for the walls. */
const float kCarHalfLength = 2.25f;
const float kCarHalfWidth = 0.95f;
/** Of the speed into a wall: what comes back out, and the friction along it. */
const float kWallBounce = 0.15f;
const float kWallFriction = 0.35f;
const float kWallSpin = 0.6f;
/** Friction while scraping along a wall; the yaw rate the wall gives, per radian between the car and the wall
 * (rad/s), and its most; what is kept of the yaw rate when the car leaves the wall. */
const float kWallScrape = 0.1f;
const float kWallTurnRate = 3.0f;
const float kWallMaxTurn = 1.5f;
const float kWallLeaveSpin = 0.3f;

/** Drawing. */
const float kDrawDistance = 450.0f;
const float kWallHeight = 1.1f;
const float kBandBottom = 0.8f;
const float kTyreRadius = 21.0f;
const float kKerbCurvature = 1.0f / 150.0f;
const float kKerbWidth = 1.0f;
const float kLineInset = 0.3f;
const float kLineWidth = 0.2f;
const float kPaintHeight = 0.02f;
const float kGroundCell = 100.0f;
const float kGroundBelow = 0.12f;
const float kPostEvery = 100.0f;

const unsigned int kAsphalt = 0xff505050;
const unsigned int kGrass = 0xff3c5a30;
const unsigned int kGround = 0xff34502a;
const unsigned int kWhite = 0xffb0b0b0;
const unsigned int kRed = 0xffb01818;
const unsigned int kBlue = 0xff1838b0;
const unsigned int kYellow = 0xffc0a010;
const unsigned int kGreen = 0xff18a030;
const unsigned int kOrange = 0xffd06010;
const unsigned int kConcrete = 0xff8c8c88;
const unsigned int kTyre = 0xff303030;
const unsigned int kBlack = 0xff101010;
const float kCheckerSquare = 0.75f;

/** Interfaces of the player's car (UTL::COM handles) and their methods (vtable offsets). */
const unsigned int kVehicleHandle = 0x4040e0;
const unsigned int kRigidBodyHandle = 0x4039f0;
const unsigned int kInputHandle = 0x403f90;
const unsigned int kEngineHandle = 0x404020;
const unsigned int kTransmissionHandle = 0x404010;
const int kBodyPosition = 0x20;
const int kBodyLinearVelocity = 0x24;
const int kBodyAngularVelocity = 0x28;
const int kBodyOrientation = 0x44;
const int kBodySetPosition = 0x5c;
const int kBodySetLinearVelocity = 0x60;
const int kBodySetAngularVelocity = 0x64;
const int kEngineRpm = 0x04;
const int kTransmissionGear = 0x04;
const int kSetVehicleOnGround = 0x68;
/** ISimable::GetWPos and GetPosition; WWorldPos's flags (bit 0: it holds a face). */
const int kSimableWorldPos = 0x4c;
const int kSimablePosition = 0x60;
const int kWorldPosFlags = 0x30;
/** A car's middle over the ground, until one is measured, and the range taken as measured; a car is put down this much
 * higher, to settle onto its wheels. */
const float kDefaultRestHeight = 0.9f;
const float kMinRestHeight = 0.3f;
const float kMaxRestHeight = 2.5f;
const float kPutAbove = 0.03f;
/** ISimable::GetRigidBody, and where the ISimable sits in PVehicle (PVehicle::ReloadBehaviors 0x6863b0 calls it at
 * this +0x2c). */
const int kSimableRigidBody = 0x54;
const int kSimableInVehicle = 0x2c;
/** PVehicle: behaviours being made (PVehicle::LoadBehaviors 0x685240 does nothing while it is set). */
const int kVehicleLoading = 0x48;
/** InputControls. */
const int kSteering = 0x04;
const int kGas = 0x14;
const int kBrake = 0x18;
const int kHandBrake = 0x1c;
const int kNitrous = 0x21;

const float kMph = 0.44704f;
const float kKmh = 1.0f / 3.6f;
const float kGravity = 9.81f;
const float kPi = 3.14159265f;

enum RoadId
{
    kNoRoad = -1,
    kStraight,
    kLoopRoad,
};

struct Sample
{
    float x, y;
    /** Unit direction and curvature (1/radius, left positive) of the piece from this sample to the next. */
    float dx, dy, curvature;
    /** Distance along the road. */
    float s;
    unsigned char piece;
};

struct Road
{
    Sample samples[kMaxSamples];
    int count;
    bool closed;
    float half;
    float wall;
    float length;
};

enum Panel
{
    kPanelTelemetry,
    kPanelTuning,
    kPanelHelp,
    kPanelHidden,
    kPanelCount,
};

/** The straight's measurements: times to speeds, times and speeds over distances. */
const int kSpeedMarks = 6;
const float kSpeedMarkValues[kSpeedMarks] = {60.0f * kMph,  100.0f * kKmh, 100.0f * kMph,
                                             200.0f * kKmh, 250.0f * kKmh, 300.0f * kKmh};
const char *const kSpeedMarkNames[kSpeedMarks] = {"0-60 MPH", "0-100 KM/H", "0-100 MPH",
                                                  "0-200 KM/H", "0-250 KM/H", "0-300 KM/H"};
const int kDistanceMarks = 5;
const float kDistanceMarkValues[kDistanceMarks] = {18.288f, 201.168f, 402.336f, 1000.0f, 2000.0f};
const char *const kDistanceMarkNames[kDistanceMarks] = {"60 FT", "1/8 MILE", "1/4 MILE", "1000 M", "2000 M"};

struct Run
{
    float speedTimes[kSpeedMarks];
    float distanceTimes[kDistanceMarks];
    float distanceSpeeds[kDistanceMarks];
    float topSpeed;
    /** When and where the top speed came, and the engine then. */
    float topTime;
    float topDistance;
    float topRpm;
    int topGear;
    bool revLimited;
    /** 80-120 km/h at full throttle; 100-0 and 200-0 km/h braking distances. */
    float rolling;
    float brake100;
    float brake200;
};

enum StraightStage
{
    kIdle,
    kReady,
    kRunning,
};

enum AutoStage
{
    kAutoOff,
    kAutoSettle,
    kAutoDrive,
    kAutoBrake,
    kAutoDone,
};

struct Braking
{
    float threshold;
    bool armed;
    bool active;
    float start;
};

struct Lap
{
    bool started;
    bool sector[2];
    float start;
    float sectorStart;
    float current[3];
    float last;
    float best;
    float bestSectors[3];
    float lastS;
    int laps;
};

struct Key
{
    int vk;
    bool down;
    DWORD repeatAt;
};

struct Scripted
{
    float at;
    char key[16];
};

enum MenuStage
{
    kMenuNone,
    /** The entry was taken: the main menu's next switch goes to the car select. */
    kMenuOpening,
    kMenuChoosing,
};

struct State
{
    int enabled;
    bool requested;
    MenuStage menu;
    bool raced;
    bool built;
    Road roads[2];
    float loopLength;
    // Arrival.
    bool warpRequested;
    bool arrived;
    bool startOnLoop;
    char tune[256];
    char car[32];
    // Stepping.
    float stepTime;
    void *input;
    bool haveLast;
    float lastVelocity[2];
    float lastStepTime;
    // What the car does now (renderer axes).
    float position[2];
    float heading;
    float speed;
    float longG, latG;
    float rpm;
    int gear;
    float gas, brake, steering;
    RoadId road;
    float s, lateral;
    int sample;
    int wallHits;
    bool touching;
    float restHeight;
    /** Sideways speed over speed. */
    float slip;
    /** The controls the loop driver set this step, by offset / 4. */
    float driverControls[16];
    // The straight.
    StraightStage stage;
    float runStart;
    float runOrigin;
    float lastRunSpeed;
    float lastRunDistance;
    float lastRunTime;
    Run run;
    float topMark;
    float topSince;
    bool autoRunEnded;
    Run lastRun;
    Run best;
    bool haveLastRun;
    Braking braking[2];
    bool rollingArmed;
    float rollingStart;
    // The loop.
    Lap lap;
    // Hands-off run.
    AutoStage autoStage;
    float autoSince;
    bool autoQuit;
    bool autoFromConfig;
    float steer;
    // Keys and the menu.
    bool menuOpen;
    int page;
    int item;
    int carChoice;
    int swapTo;
    bool refreshTuning;
    bool restoreKept;
    bool telemetry;
    Panel panel;
    Key keys[32];
    /** Arrows held on the tuning page: the car's controls are let go meanwhile. */
    bool arrowsHeld;
    int keyCount;
    bool reloadWanted;
    char status[kLineLength];
    DWORD statusUntil;
    // Debug.ProvingKeys.
    Scripted script[32];
    int scriptCount;
    int scriptNext;
    float raceStart;
    // Teleport requests made by keys, done in Update.
    int teleport;
    float boost;
    bool stop;
};

State g_state = {-1};

void ResetStraight();
void ResetLap();
void StartAuto();

/** Sine and cosine in double precision (the loop is laid out with them, so it closes), and the heading of a
 * direction (radians, from bATan's 16-bit angle). */
double Sine(double x)
{
    const double kTwoPi = 6.283185307179586;
    x -= kTwoPi * static_cast<double>(static_cast<int>(x / kTwoPi));
    if (x > 3.141592653589793)
        x -= kTwoPi;
    else if (x < -3.141592653589793)
        x += kTwoPi;
    const double x2 = x * x;
    double term = x, sum = x;
    for (int n = 1; n < 12; ++n)
    {
        term *= -x2 / ((2 * n) * (2 * n + 1));
        sum += term;
    }
    return sum;
}

double Cosine(double x)
{
    return Sine(x + 1.5707963267948966);
}

float Heading(float x, float y)
{
    return static_cast<float>(bATan(x, y)) * (6.2831853f / 65536.0f);
}

template <typename R, typename... A> R Virtual(void *object, int offset, A... args)
{
    typedef R(__thiscall * Function)(void *, A...);
    return (*reinterpret_cast<Function *>(*reinterpret_cast<char **>(object) + offset))(object, args...);
}

/** "-12.34" with `decimals` places. */
void Format(char *out, float value, int decimals)
{
    static const int kScale[] = {1, 10, 100, 1000};
    const int scale = kScale[decimals];
    const bool negative = value < 0.0f;
    const int scaled = static_cast<int>((negative ? -value : value) * scale + 0.5f);
    if (!decimals)
        wsprintfA(out, "%s%d", negative && scaled ? "-" : "", scaled);
    else if (decimals == 1)
        wsprintfA(out, "%s%d.%d", negative && scaled ? "-" : "", scaled / scale, scaled % scale);
    else if (decimals == 2)
        wsprintfA(out, "%s%d.%02d", negative && scaled ? "-" : "", scaled / scale, scaled % scale);
    else
        wsprintfA(out, "%s%d.%03d", negative && scaled ? "-" : "", scaled / scale, scaled % scale);
}

/** "1:12.345" or "12.345". */
void FormatTime(char *out, float seconds)
{
    if (seconds <= 0.0f)
    {
        lstrcpyA(out, "-");
        return;
    }
    const int ms = static_cast<int>(seconds * 1000.0f + 0.5f);
    if (ms >= 60000)
        wsprintfA(out, "%d:%02d.%03d", ms / 60000, (ms / 1000) % 60, ms % 1000);
    else
        wsprintfA(out, "%d.%03d", ms / 1000, ms % 1000);
}

void SetStatus(const char *text)
{
    lstrcpynA(g_state.status, text, kLineLength);
    g_state.statusUntil = GetTickCount() + 4000;
    nfs::Log("proving: %s", text);
}

// ---------------------------------------------------------------------------------------------------------------
// The course.

void AddSample(Road &road, float x, float y, unsigned char piece)
{
    if (road.count >= kMaxSamples)
        return;
    Sample &s = road.samples[road.count++];
    s.x = x;
    s.y = y;
    s.piece = piece;
    s.dx = 1.0f;
    s.dy = 0.0f;
    s.curvature = 0.0f;
    s.s = 0.0f;
}

/** Directions, curvatures and distances from the sample positions. */
void Finish(Road &road, const float *curvatures)
{
    float s = 0.0f;
    const int segments = road.closed ? road.count : road.count - 1;
    for (int i = 0; i < road.count; ++i)
    {
        Sample &a = road.samples[i];
        a.s = s;
        if (i < segments)
        {
            const Sample &b = road.samples[(i + 1) % road.count];
            const float dx = b.x - a.x, dy = b.y - a.y;
            const float length = sqrtf(dx * dx + dy * dy);
            a.dx = length > 0.0f ? dx / length : 1.0f;
            a.dy = length > 0.0f ? dy / length : 0.0f;
            a.curvature = curvatures ? curvatures[i] : 0.0f;
            s += length;
        }
        else if (i > 0)
        {
            a.dx = road.samples[i - 1].dx;
            a.dy = road.samples[i - 1].dy;
        }
    }
    road.length = s;
}

void Build()
{
    g_state.built = true;
    Road &straight = g_state.roads[kStraight];
    straight.closed = false;
    straight.half = kStraightHalf;
    straight.wall = kStraightWall;
    for (float x = -kRunUp; x <= kStraightLength + kRunOff + 0.01f; x += kStraightStep)
        AddSample(straight, kStraightX + x, kStraightY, 0);
    Finish(straight, nullptr);

    Road &loop = g_state.roads[kLoopRoad];
    static float curvatures[kMaxSamples];
    loop.closed = true;
    loop.half = kLoopHalf;
    loop.wall = kLoopWall;
    float x = kLoopX, y = kLoopY, h = 0.0f;
    AddSample(loop, x, y, 0);
    for (int p = 0; p < kLoopPieces; ++p)
    {
        const Piece &piece = kLoop[p];
        if (piece.radius <= 0.0f)
        {
            const int n = static_cast<int>(ceilf(piece.length / kLoopStraightStep));
            for (int i = 0; i < n; ++i)
            {
                curvatures[loop.count - 1] = 0.0f;
                loop.samples[loop.count - 1].piece = static_cast<unsigned char>(p);
                x += static_cast<float>(Cosine(h)) * piece.length / n;
                y += static_cast<float>(Sine(h)) * piece.length / n;
                AddSample(loop, x, y, static_cast<unsigned char>(p));
            }
            continue;
        }
        const float angle = piece.degrees * kPi / 180.0f;
        const int n = static_cast<int>(ceilf(fabsf(angle) * piece.radius / kArcStep)) < 2
                          ? 2
                          : static_cast<int>(ceilf(fabsf(angle) * piece.radius / kArcStep));
        const float dh = angle / n;
        const float chord = 2.0f * piece.radius * static_cast<float>(Sine(fabsf(dh) * 0.5f));
        for (int i = 0; i < n; ++i)
        {
            curvatures[loop.count - 1] = (angle > 0.0f ? 1.0f : -1.0f) / piece.radius;
            loop.samples[loop.count - 1].piece = static_cast<unsigned char>(p);
            const float middle = h + dh * 0.5f;
            x += static_cast<float>(Cosine(middle)) * chord;
            y += static_cast<float>(Sine(middle)) * chord;
            h += dh;
            AddSample(loop, x, y, static_cast<unsigned char>(p));
        }
    }
    // The last sample is the first again.
    --loop.count;
    Finish(loop, curvatures);
    g_state.loopLength = loop.length;
    char length[16], gap[16];
    Format(length, loop.length, 1);
    Format(gap, sqrtf((x - kLoopX) * (x - kLoopX) + (y - kLoopY) * (y - kLoopY)), 3);
    nfs::Log("proving: straight %d samples, loop %d samples, %s m (closes within %s m)", straight.count, loop.count,
             length, gap);
}

/** Where a point lies on a road: distance along it, offset to the left of its centre line, the segment. */
struct Place
{
    float s;
    float lateral;
    float distance;
    int segment;
};

Place Locate(const Road &road, float px, float py)
{
    Place best = {0.0f, 0.0f, 1e30f, 0};
    const int segments = road.closed ? road.count : road.count - 1;
    for (int i = 0; i < segments; ++i)
    {
        const Sample &a = road.samples[i];
        const Sample &b = road.samples[(i + 1) % road.count];
        const float sx = b.x - a.x, sy = b.y - a.y;
        const float length = sqrtf(sx * sx + sy * sy);
        const float rx = px - a.x, ry = py - a.y;
        float t = rx * a.dx + ry * a.dy;
        t = t < 0.0f ? 0.0f : (t > length ? length : t);
        const float cx = a.x + a.dx * t, cy = a.y + a.dy * t;
        const float d = sqrtf((px - cx) * (px - cx) + (py - cy) * (py - cy));
        if (d < best.distance)
        {
            best.distance = d;
            best.s = a.s + t;
            best.lateral = a.dx * ry - a.dy * rx;
            best.segment = i;
        }
    }
    // Beyond the straight's ends: along its line.
    if (!road.closed)
    {
        const Sample &first = road.samples[0];
        const Sample &last = road.samples[road.count - 1];
        if (best.segment == 0)
            best.s = (px - first.x) * first.dx + (py - first.y) * first.dy;
        else if (best.segment == road.count - 2)
            best.s = last.s + (px - last.x) * last.dx + (py - last.y) * last.dy;
    }
    return best;
}

bool InSite(float x, float y)
{
    return x > kGroundMinX && x < kGroundMaxX && y > kGroundMinY && y < kGroundMaxY;
}

/** The road a point is on (the nearer centre line), or none. */
RoadId RoadAt(float x, float y, Place &place)
{
    if (!InSite(x, y))
        return kNoRoad;
    const Place a = Locate(g_state.roads[kStraight], x, y);
    const Place b = Locate(g_state.roads[kLoopRoad], x, y);
    if (a.distance <= b.distance && a.distance < kStraightWall + 6.0f)
    {
        place = a;
        return kStraight;
    }
    if (b.distance < kLoopWall + 6.0f)
    {
        place = b;
        return kLoopRoad;
    }
    return kNoRoad;
}

// ---------------------------------------------------------------------------------------------------------------
// The car.

struct Car
{
    void *simable;
    void *vehicle;
    void *body;
    void *engine;
    void *transmission;
};

Car FindCar()
{
    Car c = {};
    c.simable = nfs::pose::PlayerSimable();
    if (!c.simable)
        return c;
    c.vehicle = nfs::pose::PlayerInterface(kVehicleHandle);
    c.body = nfs::pose::PlayerInterface(kRigidBodyHandle);
    c.engine = nfs::pose::PlayerInterface(kEngineHandle);
    c.transmission = nfs::pose::PlayerInterface(kTransmissionHandle);
    return c;
}

/** The car's forward direction in the renderer's axes from its orientation (simulation axes, +Z forward). */
void Forward(const float *q, float &fx, float &fy)
{
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float sx = 2.0f * (x * z + w * y);
    const float sz = 1.0f - 2.0f * (x * x + y * y);
    fx = sz;
    fy = -sx;
    const float length = sqrtf(fx * fx + fy * fy);
    if (length > 0.0f)
    {
        fx /= length;
        fy /= length;
    }
}

/** The camera starts afresh where the car now is (CameraAI::Reset 0x47cc50, as the game does when it resets a car):
 * its smoothing otherwise keeps part of a long jump for good, half a metre to the side after the 26 km from
 * Rockport. */
void ResetCamera()
{
    NFS_ORIGINAL(0x47cc50, void(__cdecl *)())();
}

/** Puts the car on the ground at (x, y) facing `heading` (radians from +X, left positive), standing still. */
/** How high the car's middle rides over the ground it stands on (the face under it, ISimable::GetWPos, in the
 * simulation's axes), kept from the last time it stood still: a car put down lower than that sinks into the ground and
 * springs up when it moves. */
float RestHeight()
{
    void *simable = nfs::pose::PlayerSimable();
    if (!simable || g_state.speed > 0.5f)
        return g_state.restHeight;
    const char *wpos = Virtual<const char *>(simable, kSimableWorldPos);
    const float *position = Virtual<const float *>(simable, kSimablePosition);
    if (!wpos || !position || !(*reinterpret_cast<const unsigned int *>(wpos + kWorldPosFlags) & 1))
        return g_state.restHeight;
    const float *face = reinterpret_cast<const float *>(wpos);
    const float height = position[1] - (face[1] + face[5] + face[9]) / 3.0f;
    if (height > kMinRestHeight && height < kMaxRestHeight)
        g_state.restHeight = height;
    return g_state.restHeight;
}

void PutCar(float x, float y, float heading)
{
    const Car c = FindCar();
    if (!c.vehicle || !c.body)
        return;
    const float rest = RestHeight();
    const float position[3] = {-y, kHeight + rest + kPutAbove, x};
    const float direction[3] = {-static_cast<float>(Sine(heading)), 0.0f, static_cast<float>(Cosine(heading))};
    Virtual<bool>(c.vehicle, kSetVehicleOnGround, position, direction);
    // Its middle where it rides (SetVehicleOnGround leaves it lower).
    Virtual<void>(c.body, kBodySetPosition, position);
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    Virtual<void>(c.body, kBodySetLinearVelocity, zero);
    Virtual<void>(c.body, kBodySetAngularVelocity, zero);
    g_state.haveLast = false;
    ResetCamera();
}

void ResetStraight()
{
    g_state.stage = kIdle;
    g_state.run = Run();
    for (Braking &b : g_state.braking)
        b.armed = b.active = false;
    g_state.rollingArmed = false;
}

void ResetLap()
{
    Lap &l = g_state.lap;
    l.started = false;
    l.sector[0] = l.sector[1] = false;
    l.current[0] = l.current[1] = l.current[2] = 0.0f;
    l.lastS = -1.0f;
}

void Teleport(int where)
{
    if (where == 1)
    {
        PutCar(kStraightX - kCarHalfLength - 0.5f, kStraightY, 0.0f);
        ResetStraight();
        SetStatus("TO THE STRAIGHT'S START");
    }
    else if (where == 2)
    {
        // Behind the loop's line, so the first lap starts there.
        PutCar(kLoopX - 30.0f, kLoopY, 0.0f);
        ResetLap();
        SetStatus("TO THE LOOP");
    }
    else if (where == 3)
    {
        Place place;
        const RoadId id = RoadAt(g_state.position[0], g_state.position[1], place);
        if (id == kNoRoad)
        {
            Teleport(g_state.startOnLoop ? 2 : 1);
            return;
        }
        const Sample &a = g_state.roads[id].samples[place.segment];
        const float t = place.s - a.s;
        PutCar(a.x + a.dx * t, a.y + a.dy * t, Heading(a.dx, a.dy));
        if (id == kStraight)
            ResetStraight();
        else
            ResetLap();
        SetStatus("CAR RESET ON THE ROAD");
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Walls.

void CountDriverHit();

/** Keeps the car between the walls of the road it is on. */
void Walls(const Car &c, const float *q)
{
    Place place;
    const RoadId id = RoadAt(g_state.position[0], g_state.position[1], place);
    if (id == kNoRoad)
        return;
    const Road &road = g_state.roads[id];
    float fx, fy;
    Forward(q, fx, fy);
    const float lx = -fy, ly = fx;
    // The deepest corner past a wall (or an end of the straight).
    float depth = 0.0f, nx = 0.0f, ny = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        const float along = (i & 1) ? kCarHalfLength : -kCarHalfLength;
        const float across = (i & 2) ? kCarHalfWidth : -kCarHalfWidth;
        const float px = g_state.position[0] + fx * along + lx * across;
        const float py = g_state.position[1] + fy * along + ly * across;
        const Place p = Locate(road, px, py);
        const Sample &a = road.samples[p.segment];
        const float over = fabsf(p.lateral) - road.wall;
        if (over > depth)
        {
            depth = over;
            // Out of the road: the wall's normal points to the side the corner went.
            const float side = p.lateral > 0.0f ? 1.0f : -1.0f;
            nx = -a.dy * side;
            ny = a.dx * side;
        }
        if (!road.closed)
        {
            const float before = road.samples[0].s - p.s;
            const float after = p.s - road.samples[road.count - 1].s;
            if (before > depth)
            {
                depth = before;
                nx = -a.dx;
                ny = -a.dy;
            }
            if (after > depth)
            {
                depth = after;
                nx = a.dx;
                ny = a.dy;
            }
        }
    }
    const bool touching = g_state.touching;
    g_state.touching = depth > 0.0f;
    const float *angular = Virtual<const float *>(c.body, kBodyAngularVelocity);
    if (depth <= 0.0f)
    {
        // Off the wall: what the wall's turn left of the yaw rate dies down.
        if (touching)
        {
            const float spin[3] = {angular[0], angular[1] * kWallLeaveSpin, angular[2]};
            Virtual<void>(c.body, kBodySetAngularVelocity, spin);
        }
        return;
    }
    const float *position = Virtual<const float *>(c.body, kBodyPosition);
    const float *velocity = Virtual<const float *>(c.body, kBodyLinearVelocity);
    // Renderer (x, y) is simulation (z, -x).
    const float moved[3] = {position[0] + ny * depth, position[1], position[2] - nx * depth};
    Virtual<void>(c.body, kBodySetPosition, moved);
    const float vx = velocity[2], vy = -velocity[0];
    const float into = vx * nx + vy * ny;
    if (into <= 0.0f)
        return;
    // A blow (the first step against the wall) costs speed along it in proportion; scraping along it, less.
    const bool blow = !touching;
    const float tx = vx - into * nx, ty = vy - into * ny;
    const float along = sqrtf(tx * tx + ty * ty);
    const float loss = (blow ? kWallFriction * (1.0f + kWallBounce) : kWallScrape) * into;
    const float scrub = along > loss ? (along - loss) / along : 0.0f;
    const float bounce = blow ? kWallBounce : 0.0f;
    const float rx = tx * scrub - into * bounce * nx;
    const float ry = ty * scrub - into * bounce * ny;
    const float out[3] = {-ry, velocity[1], rx};
    Virtual<void>(c.body, kBodySetLinearVelocity, out);
    // The wall turns the car along it (a yaw rate toward the way it slides; the renderer's turn to the left is a
    // negative turn about the simulation's Y).
    float yaw = angular[1] * (blow ? kWallSpin : 1.0f);
    if (along > 1.0f)
    {
        float ux = tx / along, uy = ty / along;
        if (ux * fx + uy * fy < 0.0f)
        {
            ux = -ux;
            uy = -uy;
        }
        const float turn = Heading(ux * fx + uy * fy, fx * uy - fy * ux);
        float rate = (turn > kPi ? turn - 2.0f * kPi : turn) * kWallTurnRate;
        rate = rate > kWallMaxTurn ? kWallMaxTurn : (rate < -kWallMaxTurn ? -kWallMaxTurn : rate);
        yaw = -rate;
    }
    const float spin[3] = {angular[0] * (blow ? kWallSpin : 1.0f), yaw, angular[2] * (blow ? kWallSpin : 1.0f)};
    Virtual<void>(c.body, kBodySetAngularVelocity, spin);
    if (blow && into > 3.0f)
    {
        ++g_state.wallHits;
        CountDriverHit();
        char kmh[16];
        Format(kmh, into * 3.6f, 1);
        nfs::Log("proving: wall hit at %s km/h into it", kmh);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Timing.

float BestOf(float a, float b)
{
    if (a <= 0.0f)
        return b;
    if (b <= 0.0f)
        return a;
    return a < b ? a : b;
}

void LogRun(const Run &r)
{
    char t[16], v[16];
    for (int i = 0; i < kSpeedMarks; ++i)
        if (r.speedTimes[i] > 0.0f)
        {
            FormatTime(t, r.speedTimes[i]);
            nfs::Log("proving: %s %s s", kSpeedMarkNames[i], t);
        }
    for (int i = 0; i < kDistanceMarks; ++i)
        if (r.distanceTimes[i] > 0.0f)
        {
            FormatTime(t, r.distanceTimes[i]);
            Format(v, r.distanceSpeeds[i] * 3.6f, 1);
            nfs::Log("proving: %s %s s at %s km/h", kDistanceMarkNames[i], t, v);
        }
    char at[16], rpm[16];
    Format(v, r.topSpeed * 3.6f, 1);
    FormatTime(t, r.topTime);
    Format(at, r.topDistance, 0);
    Format(rpm, r.topRpm, 0);
    nfs::Log("proving: top speed %s km/h after %s s and %s m, %s rpm in gear %d of %d: %s", v, t, at, rpm, r.topGear,
             nfs::tuning::ForwardGears(), r.revLimited ? "the rev limiter" : "drag (below the red line)");
}

void EndRun()
{
    g_state.stage = kIdle;
    g_state.autoRunEnded = true;
    const float redLine = nfs::tuning::RedLine();
    g_state.run.revLimited = redLine > 0.0f && g_state.run.topRpm >= redLine * 0.97f;
    g_state.lastRun = g_state.run;
    g_state.haveLastRun = true;
    Run &b = g_state.best;
    const Run &r = g_state.run;
    for (int i = 0; i < kSpeedMarks; ++i)
        b.speedTimes[i] = BestOf(b.speedTimes[i], r.speedTimes[i]);
    for (int i = 0; i < kDistanceMarks; ++i)
        if (r.distanceTimes[i] > 0.0f && (b.distanceTimes[i] <= 0.0f || r.distanceTimes[i] < b.distanceTimes[i]))
        {
            b.distanceTimes[i] = r.distanceTimes[i];
            b.distanceSpeeds[i] = r.distanceSpeeds[i];
        }
    if (r.topSpeed > b.topSpeed)
        b.topSpeed = r.topSpeed;
    nfs::Log("proving: run on the straight with %s", nfs::tuning::CarName());
    LogRun(r);
}

/** The straight: the standing start, the timed run, rolling acceleration and braking distances. */
void TimeStraight(float now, float dt, float distance)
{
    const float speed = g_state.speed;
    Run &r = g_state.run;
    if (g_state.stage != kRunning && distance <= 0.5f && distance > -kRunUp && speed < 0.3f)
    {
        if (g_state.stage != kReady)
        {
            g_state.stage = kReady;
            g_state.runOrigin = distance;
        }
    }
    else if (g_state.stage == kReady && speed >= 0.3f && g_state.gas > 0.05f)
    {
        g_state.stage = kRunning;
        g_state.run = Run();
        g_state.runStart = now - dt;
        g_state.lastRunSpeed = 0.0f;
        g_state.lastRunDistance = 0.0f;
        g_state.lastRunTime = 0.0f;
        g_state.topMark = 0.0f;
        g_state.topSince = now;
    }
    if (g_state.stage == kRunning)
    {
        const float t = now - g_state.runStart;
        const float d = distance - g_state.runOrigin;
        for (int i = 0; i < kSpeedMarks; ++i)
            if (r.speedTimes[i] <= 0.0f && speed >= kSpeedMarkValues[i])
            {
                const float f = speed > g_state.lastRunSpeed
                                    ? (kSpeedMarkValues[i] - g_state.lastRunSpeed) / (speed - g_state.lastRunSpeed)
                                    : 1.0f;
                r.speedTimes[i] = g_state.lastRunTime + (t - g_state.lastRunTime) * f;
            }
        for (int i = 0; i < kDistanceMarks; ++i)
            if (r.distanceTimes[i] <= 0.0f && d >= kDistanceMarkValues[i])
            {
                const float f = d > g_state.lastRunDistance
                                    ? (kDistanceMarkValues[i] - g_state.lastRunDistance) / (d - g_state.lastRunDistance)
                                    : 1.0f;
                r.distanceTimes[i] = g_state.lastRunTime + (t - g_state.lastRunTime) * f;
                r.distanceSpeeds[i] = speed;
            }
        if (speed > r.topSpeed)
        {
            r.topSpeed = speed;
            r.topTime = t;
            r.topDistance = d;
            r.topRpm = g_state.rpm;
            r.topGear = g_state.gear - 1;
        }
        if (speed > g_state.topMark + kTopRise)
        {
            g_state.topMark = speed;
            g_state.topSince = now;
        }
        g_state.lastRunSpeed = speed;
        g_state.lastRunDistance = d;
        g_state.lastRunTime = t;
        // Over: the top speed held past the 2000 m board, the braking zone, off the throttle, stopped or turned round.
        const bool top = d > kStraightLength && now - g_state.topSince > kTopSeconds;
        if (top || d >= kStraightEnd - kBrakeZone || (d > kStraightLength && g_state.gas < 0.5f) ||
            (t > 1.0f && speed < 0.2f) || distance < g_state.runOrigin - 1.0f)
            EndRun();
    }
    // 80-120 km/h at full throttle.
    if (!g_state.rollingArmed && g_state.gas > 0.9f && speed >= 80.0f * kKmh && speed < 82.0f * kKmh)
    {
        g_state.rollingArmed = true;
        g_state.rollingStart = now;
    }
    if (g_state.rollingArmed)
    {
        if (g_state.gas < 0.9f || speed < 78.0f * kKmh)
            g_state.rollingArmed = false;
        else if (speed >= 120.0f * kKmh)
        {
            g_state.rollingArmed = false;
            r.rolling = now - g_state.rollingStart;
            g_state.lastRun.rolling = r.rolling;
            g_state.best.rolling = BestOf(g_state.best.rolling, r.rolling);
            char t[16];
            FormatTime(t, r.rolling);
            nfs::Log("proving: 80-120 km/h %s s", t);
        }
    }
    // Braking from 100 and 200 km/h to a stop.
    for (int i = 0; i < 2; ++i)
    {
        Braking &b = g_state.braking[i];
        b.threshold = (i ? 200.0f : 100.0f) * kKmh;
        if (!b.active)
        {
            if (speed > b.threshold && g_state.brake > 0.5f)
                b.armed = true;
            else if (g_state.brake < 0.2f)
                b.armed = false;
            if (b.armed && speed <= b.threshold)
            {
                b.armed = false;
                b.active = true;
                b.start = distance;
            }
            continue;
        }
        if (g_state.brake < 0.2f)
            b.active = false;
        else if (speed < 0.3f)
        {
            b.active = false;
            const float metres = distance - b.start;
            float &last = i ? g_state.lastRun.brake200 : g_state.lastRun.brake100;
            float &best = i ? g_state.best.brake200 : g_state.best.brake100;
            last = metres;
            best = BestOf(best, metres);
            char m[16];
            Format(m, metres, 1);
            nfs::Log("proving: %s-0 km/h in %s m", i ? "200" : "100", m);
        }
    }
}

/** The loop: laps and sectors by the distance along it. */
void TimeLoop(float now, float s)
{
    Lap &l = g_state.lap;
    const float length = g_state.loopLength;
    const float third = length / 3.0f;
    if (l.lastS < 0.0f)
    {
        l.lastS = s;
        return;
    }
    const bool crossedLine = l.lastS > length - 100.0f && s < 100.0f;
    const bool crossedBack = s > length - 100.0f && l.lastS < 100.0f;
    if (crossedBack)
        l.started = false;
    for (int k = 0; k < 2; ++k)
    {
        const float mark = third * (k + 1);
        if (l.started && l.lastS < mark && s >= mark && s - l.lastS < 100.0f && (k == 0 || l.sector[0]))
        {
            l.sector[k] = true;
            l.current[k] = now - l.sectorStart;
            l.bestSectors[k] = BestOf(l.bestSectors[k], l.current[k]);
            l.sectorStart = now;
        }
    }
    if (crossedLine)
    {
        if (l.started && l.sector[0] && l.sector[1])
        {
            l.current[2] = now - l.sectorStart;
            l.bestSectors[2] = BestOf(l.bestSectors[2], l.current[2]);
            l.last = now - l.start;
            l.best = BestOf(l.best, l.last);
            ++l.laps;
            char t[16], b[16];
            FormatTime(t, l.last);
            FormatTime(b, l.best);
            nfs::Log("proving: lap %d %s (best %s) with %s, %d wall hits", l.laps, t, b, nfs::tuning::CarName(),
                     g_state.wallHits);
        }
        l.started = true;
        l.sector[0] = l.sector[1] = false;
        l.start = now;
        l.sectorStart = now;
        g_state.wallHits = 0;
    }
    l.lastS = s;
}

// ---------------------------------------------------------------------------------------------------------------
// The hands-off run.

void AutoControls(unsigned char *controls, float now)
{
    if (g_state.autoStage == kAutoOff || g_state.autoStage == kAutoDone)
        return;
    float gas = 0.0f, brake = 0.0f;
    if (g_state.autoStage == kAutoSettle)
    {
        brake = 1.0f;
        if (now - g_state.autoSince > 2.0f && g_state.speed < 0.2f)
        {
            g_state.autoStage = kAutoDrive;
            g_state.autoSince = now;
            nfs::Log("proving: hands-off run started");
        }
    }
    else if (g_state.autoStage == kAutoDrive)
    {
        gas = 1.0f;
        if (g_state.road == kStraight &&
            (g_state.autoRunEnded || g_state.s - kRunUp > kStraightEnd - kBrakeZone + 20.0f))
        {
            g_state.autoStage = kAutoBrake;
            g_state.autoSince = now;
        }
    }
    else if (g_state.autoStage == kAutoBrake)
    {
        brake = 1.0f;
        if (g_state.speed < 0.2f && now - g_state.autoSince > 1.0f)
        {
            g_state.autoStage = kAutoDone;
            g_state.autoSince = now;
            nfs::Log("proving: hands-off run done");
        }
    }
    // Lane keeping on the straight's centre line.
    float heading = g_state.heading * 180.0f / kPi;
    if (heading > 180.0f)
        heading -= 360.0f;
    float steer = g_state.lateral * 0.04f + heading * 0.03f;
    steer = steer > 0.25f ? 0.25f : (steer < -0.25f ? -0.25f : steer);
    *reinterpret_cast<float *>(controls + kGas) = gas;
    *reinterpret_cast<float *>(controls + kBrake) = brake;
    *reinterpret_cast<float *>(controls + kHandBrake) = 0.0f;
    *reinterpret_cast<float *>(controls + kSteering) = steer;
    controls[kNitrous] = 0;
}

void HaltDriver();

void StartAuto()
{
    HaltDriver();
    g_state.teleport = 1;
    g_state.autoRunEnded = false;
    g_state.autoStage = kAutoSettle;
    g_state.autoSince = Sim::GetTime();
    SetStatus("HANDS-OFF RUN");
}


// ---------------------------------------------------------------------------------------------------------------
// The loop driven as fast as the car allows.
//
// The racing line: the loop's centre line resampled every kLineStep metres, each point free to move across the road
// within the room the car leaves (kLoopHalf less half the car and a margin); the offsets that make the line's second
// differences smallest (Gauss-Seidel on the five-point stencil that minimises the summed squared curvature) are the
// line. Its curvature gives each point a cornering speed sqrt(a_lat / k); a backward pass round the loop (twice, it is
// closed) brakes into every corner no harder than a_brake within the grip the corner leaves (friction circle).
//
// The driver follows the line by pure pursuit (a point Ld ahead, Ld growing with speed: the curvature 2 sin(a) / Ld
// turned into steering through a gain it learns from the yaw rate the car answers with) and holds the speed of the
// point a reaction time ahead: full throttle below it, a lift near it, braking in proportion above it. After every lap
// the limits move: a clean lap (no wall, never far off the line) raises a_lat and a_brake by a few percent, a lap that
// was not lowers them, so the profile settles at what the car (and its tuning) can do.

const int kLineMax = 1024;
/** The controls the driver sets (InputControls offsets). */
const int kDriverControls[4] = {kGas, kBrake, kHandBrake, kSteering};
const float kLineStep = 2.0f;
const float kLineMargin = 0.35f;
const int kLineIterations = 1500;
const float kTopSpeed = 120.0f;
const float kWheelBase = 2.6f;
const float kReaction = 0.25f;
/** Steering: how far ahead the line's curvature is taken (seconds), and how hard the car is brought back onto it. */
const float kSteerPreview = 0.12f;
/** The steering gain is learnt per kSpeedBand m/s of speed. */
const int kSpeedBands = 13;
const float kSpeedBand = 10.0f;
const float kCrossTrack = 1.5f;
const float kStartLateral = 1.0f;
/** The braking the profile starts from (g) and the limits of what is learnt: each car's own is measured on the laps,
 * so that it stays on the gas as late as it can stop from. The profile uses kBrakingMargin of it. */
const float kBrakingG = 1.0f;
const float kBrakingMin = 0.5f;
const float kBrakingMax = 6.0f;
const float kBrakingMargin = 0.9f;
const float kLateralMin = 0.4f;
const float kLateralMax = 6.0f;
/** Lifting off before a bend: the deceleration assumed until the car's own is learnt (m/s2, per speed band), its
 * limits, and the most seconds before the brakes would be needed that the driver lifts: the foot stays down until
 * then, because a car slowed more than it has to says nothing about its handling. */
const float kCoastStart = 1.5f;
const float kCoastMin = 0.5f;
const float kCoastMax = 6.0f;
const float kLiftSeconds = 0.5f;
/** How far over the braking profile (m/s) the car must be before the brakes come on: less than this is left to
 * lifting off. */
const float kBrakeOver = 2.5f;
const float kBrakeOff = -1.0f;
/** The grip search: where it starts, its first step, when it stops (g); the laps then measured; the most laps it
 * searches; a lap this much quicker (s) counts as better, and this many clean laps without one end the search (the car
 * is not using the grip the profile now assumes). */
const float kStartGrip = 1.6f;
const float kStartStep = 0.32f;
const float kGripResolution = 0.03f;
const int kMeasureLaps = 3;
const int kMaxSearchLaps = 24;
const float kPlateau = 0.15f;
const int kPlateauLaps = 2;

struct RacingLine
{
    bool built;
    int count;
    float length;
    float cx[kLineMax], cy[kLineMax];
    float nx[kLineMax], ny[kLineMax];
    float offset[kLineMax];
    float x[kLineMax], y[kLineMax];
    float curvature[kLineMax];
    float speed[kLineMax];
    /** The speed from which lifting off alone reaches every bend ahead. */
    float coast[kLineMax];
    /** What the plan does from each point (PlanAction). */
    unsigned char action[kLineMax];
};

RacingLine g_line;

struct Driver
{
    bool active;
    int lapsWanted;
    int lapsAtStart;
    bool quit;
    float lateralG;
    float brakingG;
    float steerGain[kSpeedBands];
    int index;
    float worstError;
    int hitsAtLap;
    int lastLaps;
    float quitAt;
    int worstAt;
    float worstSpeed;
    int worstPiece;
    int hits;
    float worstOver;
    /** The grip search (g), its step, the last grip a lap was clean at, the last lap's answer (-1 none). */
    float grip;
    float step;
    float cleanGrip;
    int lastClean;
    bool measuring;
    bool finished;
    float measured;
    float measuredBest;
    int measuredLaps;
    int measuredFailures;
    /** The deceleration of the car lifted off, learnt per speed band (m/s2). */
    float coastRate[kSpeedBands];
    /** The last step's speed and time, and whether it was lifted off with no brakes. */
    float lastSpeed;
    float lastTime;
    bool coasting;
    bool braking;
    float lastBrake;
    /** This lap: the times the brakes came on, the seconds on them, lifted off and with the throttle wide open. */
    int brakeZones;
    float brakeTime;
    float coastTime;
    float fullTime;
    bool fullGas;
    /** The search's quickest clean lap, the grip it was at, the clean laps since one was better. */
    float searchBest;
    float searchBestGrip;
    int flatLaps;
    /** The car as measured on the laps, per speed band (m/s2): full throttle, full brakes; the steps seen of each and of
     * lifting off. */
    float accelRate[kSpeedBands];
    float brakeRate[kSpeedBands];
    int accelSeen[kSpeedBands];
    int brakeSeen[kSpeedBands];
    int coastSeen[kSpeedBands];
    /** Sliding (the car's direction of travel well off its nose): its cornering force as a part of the grip, its
     * acceleration on full throttle (m/s2), the steps seen, the seconds of this lap. */
    float slideRatio;
    float slideAccel;
    int slideSeen;
    float slideTime;
    /** The laps follow the plan (BuildPlan), not the warm-up's line and profile. */
    bool planned;
};

Driver g_driver = {false, 0, 0, false, kStartLateral, kBrakingG,
                   {2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f}};

int SpeedBand(float v)
{
    const int band = static_cast<int>(v / kSpeedBand);
    return band < 0 ? 0 : (band >= kSpeedBands ? kSpeedBands - 1 : band);
}

/** atan2(y, x) in (-pi, pi]. */
float SignedAngle(float x, float y)
{
    const float a = Heading(x, y);
    return a > kPi ? a - 2.0f * kPi : a;
}

int Wrap(int i)
{
    return (i % g_line.count + g_line.count) % g_line.count;
}

void LinePoint(int i)
{
    g_line.x[i] = g_line.cx[i] + g_line.nx[i] * g_line.offset[i];
    g_line.y[i] = g_line.cy[i] + g_line.ny[i] * g_line.offset[i];
}

void FinishLine();

void BuildRacingLine()
{
    g_line.built = true;
    const Road &loop = g_state.roads[kLoopRoad];
    // The centre line every kLineStep metres.
    const float length = loop.length;
    int n = static_cast<int>(length / kLineStep);
    if (n > kLineMax)
        n = kLineMax;
    int segment = 0;
    for (int i = 0; i < n; ++i)
    {
        const float s = length * i / n;
        while (segment + 1 < loop.count && loop.samples[segment + 1].s <= s)
            ++segment;
        const Sample &a = loop.samples[segment];
        const float t = s - a.s;
        g_line.cx[i] = a.x + a.dx * t;
        g_line.cy[i] = a.y + a.dy * t;
    }
    g_line.count = n;
    for (int i = 0; i < n; ++i)
    {
        const float dx = g_line.cx[Wrap(i + 1)] - g_line.cx[Wrap(i - 1)];
        const float dy = g_line.cy[Wrap(i + 1)] - g_line.cy[Wrap(i - 1)];
        const float d = sqrtf(dx * dx + dy * dy);
        g_line.nx[i] = -dy / d;
        g_line.ny[i] = dx / d;
        g_line.offset[i] = 0.0f;
        LinePoint(i);
    }
    // Least curvature within the road, coarse to fine (the long bends settle on the coarse points, where few sweeps
    // reach them; each finer level starts from the coarser one's offsets).
    const float room = kLoopWall - kCarHalfWidth - kLineMargin;
    for (int stride = 16; stride >= 1; stride /= 2)
    {
        for (int it = 0; it < kLineIterations; ++it)
            for (int i = 0; i + stride <= n; i += stride)
            {
                const int a = Wrap(i - 2 * stride), b = Wrap(i - stride), c = Wrap(i + stride), d = Wrap(i + 2 * stride);
                const float tx = (-g_line.x[a] + 4.0f * g_line.x[b] + 4.0f * g_line.x[c] - g_line.x[d]) / 6.0f;
                const float ty = (-g_line.y[a] + 4.0f * g_line.y[b] + 4.0f * g_line.y[c] - g_line.y[d]) / 6.0f;
                float o = (tx - g_line.cx[i]) * g_line.nx[i] + (ty - g_line.cy[i]) * g_line.ny[i];
                o = g_line.offset[i] + (o - g_line.offset[i]) * 0.8f;
                g_line.offset[i] = o > room ? room : (o < -room ? -room : o);
                LinePoint(i);
            }
        // The points between, from their coarse neighbours.
        if (stride > 1)
            for (int i = 0; i < n; i += stride)
            {
                const int j = Wrap(i + stride);
                for (int k = 1; k < stride && i + k < n; ++k)
                {
                    const float t = static_cast<float>(k) / stride;
                    g_line.offset[i + k] = g_line.offset[i] * (1.0f - t) + g_line.offset[j] * t;
                    LinePoint(i + k);
                }
            }
    }
    FinishLine();
}

void FinishLine()
{
    const int n = g_line.count;
    // Curvature (signed, left positive) from each point and its neighbours, smoothed.
    float total = 0.0f;
    static float raw[kLineMax];
    for (int i = 0; i < n; ++i)
    {
        const int a = Wrap(i - 1), c = Wrap(i + 1);
        const float ax = g_line.x[i] - g_line.x[a], ay = g_line.y[i] - g_line.y[a];
        const float bx = g_line.x[c] - g_line.x[i], by = g_line.y[c] - g_line.y[i];
        const float cx = g_line.x[c] - g_line.x[a], cy = g_line.y[c] - g_line.y[a];
        const float la = sqrtf(ax * ax + ay * ay), lb = sqrtf(bx * bx + by * by), lc = sqrtf(cx * cx + cy * cy);
        raw[i] = la * lb * lc > 0.0f ? 2.0f * (ax * by - ay * bx) / (la * lb * lc) : 0.0f;
        total += lb;
    }
    for (int i = 0; i < n; ++i)
        g_line.curvature[i] = (raw[Wrap(i - 1)] + 2.0f * raw[i] + raw[Wrap(i + 1)]) * 0.25f;
    g_line.length = total;
    char m[16], widest[16];
    Format(m, total, 1);
    float most = 0.0f;
    for (int i = 0; i < n; ++i)
        most = fabsf(g_line.offset[i]) > most ? fabsf(g_line.offset[i]) : most;
    Format(widest, most, 2);
    nfs::Log("proving: racing line of %d points, %s m, out to %s m from the centre", n, m, widest);
}

/** Each point's speed for the limits now. */
void BuildSpeedProfile()
{
    const int n = g_line.count;
    const float lateral = g_driver.lateralG * kGravity, braking = g_driver.brakingG * kBrakingMargin * kGravity;
    for (int i = 0; i < n; ++i)
    {
        const float k = fabsf(g_line.curvature[i]);
        const float v = k > 1e-5f ? sqrtf(lateral / k) : kTopSpeed;
        g_line.speed[i] = v < kTopSpeed ? v : kTopSpeed;
    }
    for (int pass = 0; pass < 2; ++pass)
        for (int j = n - 1; j >= 0; --j)
        {
            const int next = Wrap(j + 1);
            const float v = g_line.speed[next];
            const float used = v * v * fabsf(g_line.curvature[next]) / lateral;
            const float left = used < 1.0f ? sqrtf(1.0f - used * used) : 0.0f;
            const float reach = sqrtf(v * v + 2.0f * braking * (0.15f + 0.85f * left) * kLineStep);
            if (reach < g_line.speed[j])
                g_line.speed[j] = reach;
        }
    for (int i = 0; i < n; ++i)
        g_line.coast[i] = g_line.speed[i];
    for (int pass = 0; pass < 2; ++pass)
        for (int j = n - 1; j >= 0; --j)
        {
            const float v = g_line.coast[Wrap(j + 1)];
            const float reach = sqrtf(v * v + 2.0f * g_driver.coastRate[SpeedBand(v)] * kLineStep);
            if (reach < g_line.coast[j])
                g_line.coast[j] = reach;
        }
}

// ---------------------------------------------------------------------------------------------------------------
// The plan: every way round the lap on a grid.

/** Stations along the road (close together in bends, far apart on straights), kPlanLateral places across it from wall
 * to wall, the last sideways move (kPlanDeltas, which with the next gives the curvature), kPlanBins of speed: from each
 * state every next place and every action is tried, and the quickest way into each state is kept with its exact speed,
 * so the search covers every path on the grid without enumerating them one by one. */
const int kPlanStationsMax = 300;
const int kPlanLateral = 29;
const float kPlanLateralStep = 0.5f;
const int kPlanDeltas = 9;
const int kPlanCentre = 4;
const int kPlanBins = 60;
const float kPlanBin = 2.0f;
const int kPlanStates = kPlanLateral * kPlanDeltas * kPlanBins;
/** Braking costs this part of its time more, so that where lifting off is about as quick it is chosen. */
const float kBrakeBias = 0.03f;
/** Sliding counts from this sideways speed over speed. */
const float kSlideFrom = 0.12f;
const int kSlideStepsToTrust = 60;
const int kRateStepsToTrust = 30;

enum PlanAction
{
    kActGas,
    kActLift,
    kActBrake,
    kActSlide,
};

struct Plan
{
    int stations;
    int index[kPlanStationsMax];
    /** |curvature| by station, place, last move, next move; < 0: leaves the road. */
    float *kappa;
    /** Length by station, place, next move. */
    float *length;
    /** How each state was reached: the action, the speed bin and the last move before. */
    unsigned short *parents;
    float *time[2];
    float *speed[2];
    float accel[kSpeedBands];
    float coast[kSpeedBands];
    float brake[kSpeedBands];
};

Plan g_plan;

inline float PlanPlace(int j)
{
    return (static_cast<float>(j) - (kPlanLateral - 1) * 0.5f) * kPlanLateralStep;
}

float &Kappa(int s, int j, int in, int out)
{
    return g_plan.kappa[((s * kPlanLateral + j) * kPlanDeltas + in) * kPlanDeltas + out];
}

float &PlanLength(int s, int j, int out)
{
    return g_plan.length[(s * kPlanLateral + j) * kPlanDeltas + out];
}

float CentreCurvature(int i)
{
    const int a = Wrap(i - 2), c = Wrap(i + 2);
    const float ax = g_line.cx[i] - g_line.cx[a], ay = g_line.cy[i] - g_line.cy[a];
    const float bx = g_line.cx[c] - g_line.cx[i], by = g_line.cy[c] - g_line.cy[i];
    const float cx = g_line.cx[c] - g_line.cx[a], cy = g_line.cy[c] - g_line.cy[a];
    const float d = sqrtf(ax * ax + ay * ay) * sqrtf(bx * bx + by * by) * sqrtf(cx * cx + cy * cy);
    return d > 0.0f ? fabsf(2.0f * (ax * by - ay * bx) / d) : 0.0f;
}

void PlanPoint(int s, int j, float &x, float &y)
{
    const int i = g_plan.index[(s + g_plan.stations) % g_plan.stations];
    const float o = PlanPlace(j);
    x = g_line.cx[i] + g_line.nx[i] * o;
    y = g_line.cy[i] + g_line.ny[i] * o;
}

void *PlanMemory(unsigned int bytes)
{
    return VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

bool BuildPlanTables()
{
    if (g_plan.kappa)
        return true;
    const int n = g_line.count;
    int stations = 0;
    for (int i = 0; i < n && stations < kPlanStationsMax;)
    {
        g_plan.index[stations++] = i;
        float k = 0.0f;
        for (int m = 0; m <= 10; ++m)
        {
            const float c = CentreCurvature(Wrap(i + m));
            k = c > k ? c : k;
        }
        i += k > 0.02f ? 3 : (k > 0.005f ? 5 : 10);
    }
    g_plan.stations = stations;
    g_plan.kappa = static_cast<float *>(PlanMemory(kPlanStationsMax * kPlanLateral * kPlanDeltas * kPlanDeltas * 4));
    g_plan.length = static_cast<float *>(PlanMemory(kPlanStationsMax * kPlanLateral * kPlanDeltas * 4));
    g_plan.parents = static_cast<unsigned short *>(PlanMemory((kPlanStationsMax + 1) * kPlanStates * 2));
    for (int layer = 0; layer < 2; ++layer)
    {
        g_plan.time[layer] = static_cast<float *>(PlanMemory(kPlanStates * 4));
        g_plan.speed[layer] = static_cast<float *>(PlanMemory(kPlanStates * 4));
    }
    if (!g_plan.kappa || !g_plan.length || !g_plan.parents || !g_plan.time[0] || !g_plan.time[1] || !g_plan.speed[0] ||
        !g_plan.speed[1])
    {
        nfs::Log("proving: no memory for the plan");
        g_plan.kappa = nullptr;
        return false;
    }
    for (int s = 0; s < stations; ++s)
        for (int j = 0; j < kPlanLateral; ++j)
        {
            float bx, by;
            PlanPoint(s, j, bx, by);
            for (int out = 0; out < kPlanDeltas; ++out)
            {
                const int next = j + out - kPlanCentre;
                if (next < 0 || next >= kPlanLateral)
                {
                    PlanLength(s, j, out) = -1.0f;
                    for (int in = 0; in < kPlanDeltas; ++in)
                        Kappa(s, j, in, out) = -1.0f;
                    continue;
                }
                float cx, cy;
                PlanPoint(s + 1, next, cx, cy);
                const float ox = cx - bx, oy = cy - by;
                const float lo = sqrtf(ox * ox + oy * oy);
                PlanLength(s, j, out) = lo;
                for (int in = 0; in < kPlanDeltas; ++in)
                {
                    const int last = j - (in - kPlanCentre);
                    if (last < 0 || last >= kPlanLateral)
                    {
                        Kappa(s, j, in, out) = -1.0f;
                        continue;
                    }
                    float ax, ay;
                    PlanPoint(s - 1, last, ax, ay);
                    const float ix = bx - ax, iy = by - ay;
                    const float tx = cx - ax, ty = cy - ay;
                    const float d = sqrtf(ix * ix + iy * iy) * lo * sqrtf(tx * tx + ty * ty);
                    Kappa(s, j, in, out) = d > 0.0f ? fabsf(2.0f * (ix * oy - iy * ox) / d) : 0.0f;
                }
            }
        }
    nfs::Log("proving: plan grid of %d stations", stations);
    return true;
}

/** A measured rate where it was seen enough; where not, the band below's (decayed), or the starting guess. */
void FillRates(float *out, const float *rate, const int *seen, float decay)
{
    int last = -1;
    for (int band = 0; band < kSpeedBands; ++band)
    {
        out[band] = rate[band];
        if (seen[band] >= kRateStepsToTrust)
            last = band;
        else if (last >= 0)
            out[band] = out[band - 1] * decay;
    }
}

/** The grip and sliding limits for one step of the search or the final run. */
struct PlanLimits
{
    float grip;
    float slide;
    float slideAccel;
};

/** The speed at the end of a piece for an action; < 0 when the piece cannot be driven like that. */
inline float PlanStep(const PlanLimits &limits, float v, float k, float length, int action)
{
    const int band = SpeedBand(v);
    const float lateral = v * v * k;
    float a;
    if (action == kActSlide)
    {
        if (limits.slide <= 0.0f || lateral > limits.slide)
            return -1.0f;
        a = limits.slideAccel;
    }
    else
    {
        if (lateral > limits.grip)
            return -1.0f;
        const float used = lateral / limits.grip;
        const float left = sqrtf(1.0f - used * used);
        a = action == kActGas ? g_plan.accel[band] * left
                              : (action == kActLift ? -g_plan.coast[band] : -g_plan.brake[band] * (0.15f + 0.85f * left));
    }
    const float squared = v * v + 2.0f * a * length;
    return squared > 9.0f ? sqrtf(squared) : 3.0f;
}

/** One forward sweep from the given starts; returns the lap's time and leaves the last layer's best state. */
float PlanSweep(const PlanLimits &limits, int startPlace, float startSpeed, int &endState)
{
    const int stations = g_plan.stations;
    float *time = g_plan.time[0], *speed = g_plan.speed[0], *nextTime = g_plan.time[1], *nextSpeed = g_plan.speed[1];
    for (int i = 0; i < kPlanStates; ++i)
        time[i] = 1e30f;
    const int startBin = static_cast<int>(startSpeed / kPlanBin);
    for (int j = 0; j < kPlanLateral; ++j)
    {
        if (startPlace >= 0 && j != startPlace)
            continue;
        const int state = (j * kPlanDeltas + kPlanCentre) * kPlanBins + startBin;
        time[state] = 0.0f;
        speed[state] = startSpeed;
    }
    for (int s = 0; s < stations; ++s)
    {
        for (int i = 0; i < kPlanStates; ++i)
            nextTime[i] = 1e30f;
        unsigned short *parents = g_plan.parents + s * kPlanStates;
        for (int j = 0; j < kPlanLateral; ++j)
            for (int in = 0; in < kPlanDeltas; ++in)
                for (int bin = 0; bin < kPlanBins; ++bin)
                {
                    const int state = (j * kPlanDeltas + in) * kPlanBins + bin;
                    const float t = time[state];
                    if (t >= 1e29f)
                        continue;
                    const float v = speed[state];
                    for (int out = 0; out < kPlanDeltas; ++out)
                    {
                        const float k = Kappa(s, j, in, out);
                        if (k < 0.0f)
                            continue;
                        const float length = PlanLength(s, j, out);
                        for (int action = kActGas; action <= kActSlide; ++action)
                        {
                            const float end = PlanStep(limits, v, k, length, action);
                            if (end < 0.0f)
                                continue;
                            float took = 2.0f * length / (v + end);
                            if (action == kActBrake)
                                took *= 1.0f + kBrakeBias;
                            int to = static_cast<int>(end / kPlanBin);
                            to = to >= kPlanBins ? kPlanBins - 1 : to;
                            const int place = j + out - kPlanCentre;
                            const int reached = (place * kPlanDeltas + out) * kPlanBins + to;
                            const float total = t + took;
                            if (total < nextTime[reached])
                            {
                                nextTime[reached] = total;
                                nextSpeed[reached] = end;
                                parents[reached] = static_cast<unsigned short>(action | (bin << 2) | (in << 8));
                            }
                        }
                    }
                }
        float *swap = time;
        time = nextTime;
        nextTime = swap;
        swap = speed;
        speed = nextSpeed;
        nextSpeed = swap;
    }
    float best = 1e30f;
    endState = -1;
    for (int i = 0; i < kPlanStates; ++i)
        if (time[i] < best)
        {
            best = time[i];
            endState = i;
        }
    return best;
}

/** The best way round for the grip now, from the car as measured: the line, the speeds and the actions. Returns false
 * when there is none (the warm-up's line and profile stay). */
bool BuildPlan()
{
    if (!BuildPlanTables())
        return false;
    FillRates(g_plan.accel, g_driver.accelRate, g_driver.accelSeen, 0.85f);
    FillRates(g_plan.coast, g_driver.coastRate, g_driver.coastSeen, 1.0f);
    FillRates(g_plan.brake, g_driver.brakeRate, g_driver.brakeSeen, 1.0f);
    PlanLimits limits;
    limits.grip = g_driver.grip * kGravity;
    limits.slide = g_driver.slideSeen >= kSlideStepsToTrust ? g_driver.grip * g_driver.slideRatio * kGravity : 0.0f;
    limits.slideAccel = g_driver.slideAccel;
    const DWORD began = GetTickCount();
    // The start's place and speed are the lap's end: the first sweep finds them, the second starts from them.
    int end = -1;
    const int stations = g_plan.stations;
    float lap = PlanSweep(limits, -1, 50.0f, end);
    if (end < 0)
        return false;
    int place = end / kPlanBins / kPlanDeltas;
    const float closing = static_cast<float>(end % kPlanBins) * kPlanBin + kPlanBin * 0.5f;
    lap = PlanSweep(limits, place, closing, end);
    if (end < 0)
        return false;
    static int places[kPlanStationsMax + 1];
    static unsigned char actions[kPlanStationsMax];
    int state = end;
    for (int s = stations; s > 0; --s)
    {
        const int here = state / kPlanBins / kPlanDeltas;
        const int in = state / kPlanBins % kPlanDeltas;
        places[s] = here;
        const unsigned short from = g_plan.parents[(s - 1) * kPlanStates + state];
        actions[s - 1] = static_cast<unsigned char>(from & 3);
        const int before = here - (in - kPlanCentre);
        state = (before * kPlanDeltas + (from >> 8)) * kPlanBins + ((from >> 2) & 63);
    }
    places[0] = place;
    // The speeds again from the start, exactly, along the actions chosen.
    static float speeds[kPlanStationsMax + 1];
    speeds[0] = closing;
    int lastIn = kPlanCentre;
    int braking = 0, sliding = 0;
    for (int s = 0; s < stations; ++s)
    {
        const int out = places[s + 1] - places[s] + kPlanCentre;
        const float reached = PlanStep(limits, speeds[s], Kappa(s, places[s], lastIn, out), PlanLength(s, places[s], out),
                                       actions[s]);
        speeds[s + 1] = reached > 0.0f ? reached : speeds[s];
        lastIn = out;
        braking += actions[s] == kActBrake;
        sliding += actions[s] == kActSlide;
    }
    const int n = g_line.count;
    for (int s = 0; s < stations; ++s)
    {
        const int a = g_plan.index[s], b = s + 1 < stations ? g_plan.index[s + 1] : n;
        for (int i = a; i < b; ++i)
        {
            const float t = static_cast<float>(i - a) / static_cast<float>(b - a);
            g_line.offset[i] = PlanPlace(places[s]) + (PlanPlace(places[s + 1]) - PlanPlace(places[s])) * t;
            g_line.speed[i] = speeds[s] + (speeds[s + 1] - speeds[s]) * t;
            g_line.action[i] = actions[s];
        }
    }
    // The grid's corners rounded off; the speeds kept within what the rounded line allows.
    static float smooth[kLineMax];
    for (int pass = 0; pass < 12; ++pass)
    {
        for (int i = 0; i < n; ++i)
            smooth[i] = g_line.offset[Wrap(i - 1)] * 0.25f + g_line.offset[i] * 0.5f + g_line.offset[Wrap(i + 1)] * 0.25f;
        for (int i = 0; i < n; ++i)
            g_line.offset[i] = smooth[i];
    }
    for (int i = 0; i < n; ++i)
        LinePoint(i);
    FinishLine();
    for (int i = 0; i < n; ++i)
    {
        const float k = fabsf(g_line.curvature[i]);
        const float limit = g_line.action[i] == kActSlide && limits.slide > 0.0f ? limits.slide : limits.grip;
        const float most = k > 1e-5f ? sqrtf(limit / k) : kTopSpeed;
        g_line.speed[i] = g_line.speed[i] < most ? g_line.speed[i] : most;
    }
    for (int pass = 0; pass < 2; ++pass)
        for (int j = n - 1; j >= 0; --j)
        {
            const float v = g_line.speed[Wrap(j + 1)];
            const float reach = sqrtf(v * v + 2.0f * g_plan.brake[SpeedBand(v)] * kBrakingMargin * kLineStep);
            if (reach < g_line.speed[j])
                g_line.speed[j] = reach;
        }
    g_driver.planned = true;
    char t[16], g[16];
    FormatTime(t, lap);
    Format(g, g_driver.grip, 3);
    nfs::Log("proving: plan at %s g predicts %s s (%d of %d stations braking, %d sliding, sliding %s) in %d ms", g, t, braking,
             stations, sliding, limits.slide > 0.0f ? "considered" : "not seen on this car",
             static_cast<int>(GetTickCount() - began));
    return true;
}

void StartLaps(int laps)
{
    if (!g_line.built)
        BuildRacingLine();
    g_driver.active = true;
    g_driver.lapsWanted = laps;
    g_driver.lapsAtStart = g_state.lap.laps;
    g_driver.lastLaps = g_state.lap.laps;
    g_driver.index = -1;
    g_driver.worstError = 0.0f;
    g_driver.worstOver = -10.0f;
    g_driver.grip = g_driver.lateralG = kStartGrip;
    g_driver.brakingG = kBrakingG;
    for (int band = 0; band < kSpeedBands; ++band)
    {
        g_driver.coastRate[band] = kCoastStart;
        g_driver.accelRate[band] = 8.0f - 0.6f * static_cast<float>(band);
        g_driver.brakeRate[band] = kBrakingG * kGravity;
        g_driver.accelSeen[band] = g_driver.brakeSeen[band] = g_driver.coastSeen[band] = 0;
    }
    g_driver.slideRatio = 0.9f;
    g_driver.slideAccel = -3.0f;
    g_driver.slideSeen = 0;
    g_driver.slideTime = 0.0f;
    g_driver.planned = false;
    g_driver.lastTime = 0.0f;
    g_driver.coasting = g_driver.braking = g_driver.fullGas = false;
    g_driver.lastBrake = 0.0f;
    g_driver.brakeZones = 0;
    g_driver.brakeTime = g_driver.coastTime = g_driver.fullTime = 0.0f;
    g_driver.searchBest = g_driver.searchBestGrip = 0.0f;
    g_driver.flatLaps = 0;
    g_driver.step = kStartStep;
    g_driver.cleanGrip = 0.0f;
    g_driver.lastClean = -1;
    g_driver.measuring = g_driver.finished = false;
    g_driver.measured = g_driver.measuredBest = 0.0f;
    g_driver.measuredLaps = g_driver.measuredFailures = 0;
    g_driver.hitsAtLap = g_driver.hits;
    BuildSpeedProfile();
    g_state.autoStage = kAutoOff;
    g_state.teleport = 2;
    SetStatus("AUTO LAPS ON THE LOOP");
}

void CountDriverHit()
{
    ++g_driver.hits;
}

void HaltDriver()
{
    g_driver.active = false;
}

void StopLaps(const char *why)
{
    g_driver.active = false;
    SetStatus(why);
}

/** The nearest line point, searched round the last one (or everywhere at first). */
int NearestLinePoint(float px, float py)
{
    int best = 0;
    float bestDistance = 1e30f;
    const int from = g_driver.index < 0 ? 0 : g_driver.index - 20;
    const int to = g_driver.index < 0 ? g_line.count : g_driver.index + 40;
    for (int k = from; k < to; ++k)
    {
        const int i = Wrap(k);
        const float dx = g_line.x[i] - px, dy = g_line.y[i] - py;
        const float d = dx * dx + dy * dy;
        if (d < bestDistance)
        {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

/** The handling test's result: the grip the car drives at cleanly and its laps there, logged and added to
 * handling\results.csv in the dev folder, nfs::dev::Folder (date, car, performance, grip g, measured laps' mean and best, laps run,
 * the laps at the grip that were not clean). */
void WriteHandlingResult(float mean, float best, int laps, int failures)
{
    char path[MAX_PATH];
    nfs::dev::Folder(path, "handling");
    lstrcatA(path, "\\results.csv");
    HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    char line[1024], grip[16], m[16], b[16];
    if (GetFileSize(file, nullptr) == 0)
    {
        lstrcpyA(line, "date,car,performance,grip_g,mean_lap_s,best_lap_s,laps,failed_measured_laps,tune\r\n");
        DWORD w = 0;
        WriteFile(file, line, lstrlenA(line), &w, nullptr);
    }
    SYSTEMTIME now;
    GetLocalTime(&now);
    Format(grip, g_driver.grip, 3);
    Format(m, mean, 3);
    Format(b, best, 3);
    wsprintfA(line, "%04d-%02d-%02d %02d:%02d,%s,%s,%s,%s,%s,%d,%d,%s\r\n", now.wYear, now.wMonth, now.wDay, now.wHour,
              now.wMinute, nfs::tuning::CarName(), nfs::realism::Active() ? "real" : "standard", grip, m, b, laps,
              failures, g_state.tune[0] ? g_state.tune : "stock");
    DWORD w = 0;
    WriteFile(file, line, lstrlenA(line), &w, nullptr);
    CloseHandle(file);
    nfs::Log("proving: handling of %s: %s g, laps %s s mean, %s s best (%d laps, %d of the measured not clean)",
             nfs::tuning::CarName(), grip, m, b, laps, failures);
}

/** After a lap. The first (from a standstill) only warms up. Then the cornering grip the profile assumes (in g) is
 * searched: up by a step after a clean lap, down after one that was not, the step halved each time the answer turns,
 * until it is under kGripResolution, or until the clean laps stop getting quicker (a car can stay on the road at a
 * grip it does not have, by simply not reaching the speeds asked for); then kMeasureLaps laps at the grip found are the
 * result (their mean time, which repeats from run to run, rather than one lucky lap). */
void AdaptLimits()
{
    const int lap = g_state.lap.laps - g_driver.lapsAtStart;
    const int hits = g_driver.hits - g_driver.hitsAtLap;
    // Clean: no wall. The whole width up to them is road for this test.
    const bool clean = !hits;
    char grip[16], err[16], t[16], at[16], target[16], over[16], braked[16], lifted[16];
    Format(braked, g_driver.brakeTime, 1);
    Format(lifted, g_driver.coastTime, 1);
    Format(grip, g_driver.grip, 3);
    Format(err, g_driver.worstError, 2);
    FormatTime(t, g_state.lap.last);
    Format(at, g_driver.worstSpeed * 3.6f, 0);
    Format(target, g_line.speed[g_driver.worstAt] * 3.6f, 0);
    Format(over, g_driver.worstOver, 2);
    char stops[16], slid[16];
    Format(stops, g_driver.brakingG, 2);
    Format(slid, g_driver.slideTime, 1);
    nfs::Log("proving: auto lap %d %s s at %s g, %s (full throttle %d%%, lifted %s s, slid %s s, braked %s s in %d places, "
             "brakes %s g; wide of the edge %s m; off the line %s m at %s, %s km/h for %s; %d wall hits)",
             lap, t, grip, lap == 1 ? "warm-up" : (clean ? "clean" : "not clean"),
             g_state.lap.last > 0.0f ? static_cast<int>(g_driver.fullTime * 100.0f / g_state.lap.last) : 0, lifted, slid,
             braked, g_driver.brakeZones, stops, over, err,
             kLoop[g_state.roads[kLoopRoad].samples[g_driver.worstPiece].piece].name, at, target, hits);
    g_driver.worstError = 0.0f;
    g_driver.worstOver = -10.0f;
    g_driver.hitsAtLap = g_driver.hits;
    g_driver.brakeZones = 0;
    g_driver.brakeTime = g_driver.coastTime = g_driver.fullTime = g_driver.slideTime = 0.0f;
    if (lap == 1)
    {
        BuildPlan();
        return;
    }
    if (g_driver.measuring)
    {
        g_driver.measured += g_state.lap.last;
        g_driver.measuredBest = BestOf(g_driver.measuredBest, g_state.lap.last);
        g_driver.measuredFailures += clean ? 0 : 1;
        if (++g_driver.measuredLaps >= kMeasureLaps)
        {
            WriteHandlingResult(g_driver.measured / g_driver.measuredLaps, g_driver.measuredBest, lap,
                                g_driver.measuredFailures);
            g_driver.finished = true;
        }
        return;
    }
    if (clean)
    {
        g_driver.cleanGrip = g_driver.grip;
        if (g_driver.searchBest <= 0.0f || g_state.lap.last < g_driver.searchBest - kPlateau)
        {
            g_driver.searchBest = g_state.lap.last;
            g_driver.searchBestGrip = g_driver.grip;
            g_driver.flatLaps = 0;
        }
        else
            ++g_driver.flatLaps;
    }
    if (g_driver.lastClean >= 0 && (clean ? 1 : 0) != g_driver.lastClean)
        g_driver.step *= 0.5f;
    g_driver.lastClean = clean ? 1 : 0;
    if (g_driver.flatLaps >= kPlateauLaps)
    {
        g_driver.grip = g_driver.searchBestGrip;
        g_driver.measuring = true;
        nfs::Log("proving: lap times stopped improving, %d measured laps follow", kMeasureLaps);
    }
    else if (g_driver.step < kGripResolution && g_driver.cleanGrip > 0.0f)
    {
        g_driver.grip = g_driver.cleanGrip;
        g_driver.measuring = true;
        nfs::Log("proving: grip found, %d measured laps follow", kMeasureLaps);
    }
    else
        g_driver.grip += clean ? g_driver.step : -g_driver.step;
    g_driver.grip = g_driver.grip < kLateralMin ? kLateralMin : (g_driver.grip > kLateralMax ? kLateralMax : g_driver.grip);
    if (lap >= kMaxSearchLaps && !g_driver.measuring)
    {
        g_driver.grip = g_driver.cleanGrip > 0.0f ? g_driver.cleanGrip : kLateralMin;
        g_driver.measuring = true;
        nfs::Log("proving: no settled grip after %d laps; measuring at the last clean one", lap);
    }
    g_driver.lateralG = g_driver.grip;
    if (!BuildPlan())
        BuildSpeedProfile();
}

/** One simulation step of the loop driver. */
void DriveLoop(unsigned char *controls, const float *angular, float fx, float fy, float now)
{
    if (!g_driver.active || g_state.teleport)
        return;
    if (g_state.road != kLoopRoad)
    {
        // Not on the loop (yet): the teleport puts it there.
        return;
    }
    const float px = g_state.position[0], py = g_state.position[1];
    const int i = NearestLinePoint(px, py);
    g_driver.index = i;
    const float v = g_state.speed;
    // Off the line, sideways.
    const float ex = px - g_line.x[i], ey = py - g_line.y[i];
    const float error = fabsf(ex * g_line.nx[i] + ey * g_line.ny[i]);
    // How far the car ran wide past the road's edge (its side over the edge, on the outside of the bend: running wide
    // is the grip's limit; a wheel over the inside at an apex is the line's).
    const float bend = g_state.roads[kLoopRoad].samples[g_state.sample].curvature;
    const bool outside = fabsf(bend) < kKerbCurvature || g_state.lateral * bend < 0.0f;
    const float over = outside ? fabsf(g_state.lateral) + kCarHalfWidth - kLoopHalf : -10.0f;
    if (over > g_driver.worstOver && g_state.lap.started)
        g_driver.worstOver = over;
    if (error > g_driver.worstError && g_state.lap.started)
    {
        g_driver.worstError = error;
        g_driver.worstAt = i;
        g_driver.worstSpeed = v;
        g_driver.worstPiece = g_state.sample;
    }
    // Steering (Stanley): the line's own curvature a little ahead (feed-forward), turned to the line's heading, and
    // back onto it in proportion to the offset over the speed; through a gain learnt from the yaw rate the car answers
    // with (the renderer's turn to the left is a negative turn about the simulation's Y).
    const int next = Wrap(i + 1);
    float tx = g_line.x[next] - g_line.x[i], ty = g_line.y[next] - g_line.y[i];
    const float tl = sqrtf(tx * tx + ty * ty);
    tx /= tl > 0.0f ? tl : 1.0f;
    ty /= tl > 0.0f ? tl : 1.0f;
    const float side = ex * -ty + ey * tx;
    const float headingError = SignedAngle(fx * tx + fy * ty, fx * ty - fy * tx);
    const int ahead = Wrap(i + static_cast<int>(v * kSteerPreview / kLineStep + 0.5f));
    const float feedForward = SignedAngle(1.0f, kWheelBase * g_line.curvature[ahead]);
    const float angle = feedForward + headingError + SignedAngle(1.0f, -kCrossTrack * side / (v + 2.0f));
    // The steering input turns the wheels by less the faster the car goes: the angle a unit of input gives is learnt
    // for each band of speed, from the yaw rate the car answers the input it had with (the wheels' angle through the
    // wheelbase, the renderer's turn to the left a negative turn about the simulation's Y), while it is not sliding.
    const int band = SpeedBand(v);
    const float yawRate = -angular[1];
    const float input = g_state.steering;
    if (v > 5.0f && fabsf(input) > 0.05f && fabsf(input) < 0.9f && fabsf(g_state.slip) < 0.08f)
    {
        const float answered = -kWheelBase * yawRate / v;
        const float perInput = answered / input;
        if (perInput > 0.02f && perInput < 2.0f)
        {
            float &gain = g_driver.steerGain[band];
            gain += (1.0f / perInput - gain) * 0.02f;
        }
    }
    float steer = -angle * g_driver.steerGain[band];
    steer = steer > 1.0f ? 1.0f : (steer < -1.0f ? -1.0f : steer);
    // Speed: the profile a reaction time ahead. Off the gas when lifting off alone is what the bends ahead need, but no
    // earlier than kLiftSeconds before the brakes would be; the brakes only when lifting off has not been enough.
    const int at = Wrap(i + static_cast<int>(v * kReaction / kLineStep + 1.0f));
    const int beyond = Wrap(at + static_cast<int>(v * kLiftSeconds / kLineStep + 0.5f));
    const float liftAt = g_line.coast[at] > g_line.speed[beyond] ? g_line.coast[at] : g_line.speed[beyond];
    const float room = liftAt - v;
    const float excess = v - g_line.speed[at];
    float gas = 0.0f, brake = 0.0f;
    if (room > 0.0f)
        gas = 1.0f;
    else if (room > -1.5f)
        gas = 1.0f + room / 1.5f;
    const int action = g_driver.planned ? g_line.action[at] : -1;
    if (g_driver.planned)
        gas = action == kActLift || action == kActBrake || excess > 0.3f ? 0.0f : 1.0f;
    // Once on, the brakes stay on down the profile until it stops falling, rather than pulsing between it and coasting.
    if (excess > kBrakeOver || (action == kActBrake && excess > kBrakeOff) ||
        (!g_driver.planned && g_driver.braking && excess > kBrakeOff && g_line.speed[beyond] < g_line.speed[at]))
    {
        gas = 0.0f;
        const float pedal = action == kActBrake ? 0.8f + 0.2f * excess : 0.25f + 0.35f * excess;
        brake = pedal > 1.0f ? 1.0f : (pedal < 0.25f ? 0.25f : pedal);
    }
    const float dt = now - g_driver.lastTime;
    if (dt > 0.0f && dt < 0.1f && g_state.lap.started)
    {
        if (g_driver.fullGas)
        {
            g_driver.fullTime += dt;
            if (v > 3.0f && fabsf(g_line.curvature[i]) < 0.01f && fabsf(g_state.slip) < kSlideFrom)
            {
                const int rateBand = SpeedBand(v);
                g_driver.accelRate[rateBand] += ((v - g_driver.lastSpeed) / dt - g_driver.accelRate[rateBand]) * 0.02f;
                ++g_driver.accelSeen[rateBand];
            }
        }
        if (fabsf(g_state.slip) > kSlideFrom && v > 10.0f)
        {
            g_driver.slideTime += dt;
            const float ratio = fabsf(yawRate) * v / (g_driver.grip * kGravity);
            g_driver.slideRatio += ((ratio > 1.5f ? 1.5f : (ratio < 0.5f ? 0.5f : ratio)) - g_driver.slideRatio) * 0.02f;
            if (g_driver.fullGas)
                g_driver.slideAccel += ((v - g_driver.lastSpeed) / dt - g_driver.slideAccel) * 0.02f;
            ++g_driver.slideSeen;
        }
        if (g_driver.braking)
        {
            g_driver.brakeTime += dt;
            if (v > 10.0f && g_driver.lastBrake >= 0.99f && fabsf(g_line.curvature[i]) < 0.01f)
            {
                const float seen = (g_driver.lastSpeed - v) / dt / kGravity;
                float &stops = g_driver.brakingG;
                stops += (seen - stops) * 0.03f;
                stops = stops < kBrakingMin ? kBrakingMin : (stops > kBrakingMax ? kBrakingMax : stops);
                const int rateBand = SpeedBand(v);
                g_driver.brakeRate[rateBand] += ((g_driver.lastSpeed - v) / dt - g_driver.brakeRate[rateBand]) * 0.05f;
                ++g_driver.brakeSeen[rateBand];
            }
        }
        else if (g_driver.coasting)
        {
            g_driver.coastTime += dt;
            if (v > 5.0f && fabsf(g_line.curvature[i]) < 0.01f)
            {
                float &rate = g_driver.coastRate[SpeedBand(v)];
                const float seen = (g_driver.lastSpeed - v) / dt;
                rate += (seen - rate) * 0.01f;
                rate = rate < kCoastMin ? kCoastMin : (rate > kCoastMax ? kCoastMax : rate);
                ++g_driver.coastSeen[SpeedBand(v)];
            }
        }
    }
    if (brake > 0.0f && !g_driver.braking)
        ++g_driver.brakeZones;
    g_driver.lastSpeed = v;
    g_driver.lastTime = now;
    g_driver.braking = brake > 0.0f;
    g_driver.lastBrake = brake;
    g_driver.fullGas = gas >= 1.0f;
    g_driver.coasting = gas <= 0.0f && brake <= 0.0f;
    *reinterpret_cast<float *>(controls + kGas) = gas;
    *reinterpret_cast<float *>(controls + kBrake) = brake;
    *reinterpret_cast<float *>(controls + kHandBrake) = 0.0f;
    *reinterpret_cast<float *>(controls + kSteering) = steer;
    controls[kNitrous] = 0;
    // The laps: the limits after each, the end after the last.
    if (g_state.lap.laps != g_driver.lastLaps)
    {
        g_driver.lastLaps = g_state.lap.laps;
        AdaptLimits();
        if (g_driver.finished ||
            (g_driver.lapsWanted > 0 && g_state.lap.laps - g_driver.lapsAtStart >= g_driver.lapsWanted))
        {
            char best[16];
            FormatTime(best, g_state.lap.best);
            nfs::Log("proving: auto laps done with %s, best %s s", nfs::tuning::CarName(), best);
            StopLaps("AUTO LAPS DONE");
            g_driver.quitAt = now + 2.0f;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Keys.

enum KeyId
{
    kKeyMenu,
    kKeyMenuNumpad,
    kKeyStraight,
    kKeyLoop,
    kKeyReset,
    kKeyAuto,
    kKeyBoost,
    kKeyStop,
    kKeyClear,
    kKeyReload,
    kKeyExport,
    kKeyEarlier,
    kKeyLater,
    // From here on, keys of the open menu.
    kKeyUp,
    kKeyDown,
    kKeyLeft,
    kKeyRight,
    kKeyEnter,
    kKeyBack,
    kKeyIncrease,
    kKeyDecrease,
    kKeyPageUp,
    kKeyPageDown,
    kKeyRevert,
    kKeyGear,
    kKeyCount,
};
const int kFirstMenuKey = kKeyUp;

struct KeyInfo
{
    int vk;
    const char *name;
    /** Repeats while held. */
    bool repeats;
};

const KeyInfo kKeys[kKeyCount] = {
    {VK_F1, "f1", false},          {VK_NUMPAD0, "num0", false},  {VK_NUMPAD1, "num1", false},
    {VK_NUMPAD2, "num2", false},   {VK_NUMPAD3, "num3", false},  {VK_NUMPAD5, "num5", false},
    {VK_NUMPAD6, "num6", false},   {VK_NUMPAD4, "num4", false},  {VK_DECIMAL, "num.", false},
    {VK_DIVIDE, "num/", false},    {VK_MULTIPLY, "num*", false}, {VK_NUMPAD7, "num7", true},
    {VK_NUMPAD9, "num9", true},    {VK_UP, "up", true},          {VK_DOWN, "down", true},
    {VK_LEFT, "left", true},       {VK_RIGHT, "right", true},    {VK_RETURN, "enter", false},
    {VK_BACK, "back", false},      {VK_ADD, "num+", true},       {VK_SUBTRACT, "num-", true},
    {VK_PRIOR, "pgup", true},      {VK_NEXT, "pgdn", true},      {VK_DELETE, "del", false},
    {VK_INSERT, "ins", false},
};

enum MenuPage
{
    kPageMain,
    kPageUpgrades,
    kPageTuning,
};

enum MenuItem
{
    kItemStraight,
    kItemLoop,
    kItemReset,
    kItemAuto,
    kItemLaps,
    kItemCar,
    kItemUpgrades,
    kItemTuning,
    kItemApply,
    kItemExport,
    kItemRevert,
    kItemTelemetry,
    kItemTime,
    kItemWeather,
    kItemClear,
    kItemClose,
    kItemCount,
};

/** The game's car list for swapping (DebugVehicleSelection: every pvehicle with a model under the default parent). */
const unsigned int kVehicleSelection = 0x9b08f8;
const int kSelectionBegin = 0x20;
const int kSelectionEnd = 0x24;

void *Selection()
{
    void *selection = *reinterpret_cast<void **>(kVehicleSelection);
    if (!selection)
    {
        NFS_ORIGINAL(0x75f560, void(__cdecl *)())();
        selection = *reinterpret_cast<void **>(kVehicleSelection);
    }
    return selection;
}

const int kMaxCars = 512;
const int kInstanceSize = 0x14;

struct CarList
{
    bool built;
    const char *names[kMaxCars];
    int count;
};

CarList g_cars;

/** The game's list, kept to the cars a player drives: PlayerUsable, and a frontend collection of their own (no cops,
 * traffic or test rigs). */
void BuildCarList()
{
    g_cars.built = true;
    char *selection = static_cast<char *>(Selection());
    if (!selection)
        return;
    const char *const *begin = *reinterpret_cast<const char *const *const *>(selection + kSelectionBegin);
    const char *const *end = *reinterpret_cast<const char *const *const *>(selection + kSelectionEnd);
    const unsigned int usableKey = Attrib::StringToKey("PlayerUsable");
    const unsigned int frontendKey = Attrib::StringToKey("frontend");
    for (const char *const *p = begin; begin && p < end && g_cars.count < kMaxCars; ++p)
    {
        if (!*p)
            continue;
        alignas(4) unsigned char storage[kInstanceSize];
        new (storage) Attrib::Gen::pvehicle(Attrib::StringToKey(*p), 0, nullptr);
        Attrib::Instance *vehicle = reinterpret_cast<Attrib::Instance *>(storage);
        const bool *usable = static_cast<const bool *>(vehicle->GetAttributePointer(usableKey, 0));
        const unsigned int *frontend = static_cast<const unsigned int *>(vehicle->GetAttributePointer(frontendKey, 0));
        if ((!usable || *usable) && frontend && frontend[1] == Attrib::StringToKey(*p))
            g_cars.names[g_cars.count++] = *p;
        vehicle->~Instance();
    }
    nfs::Log("proving: %d cars to swap to (of %d)", g_cars.count, static_cast<int>(end - begin));
}

int CarCount()
{
    if (!g_cars.built)
        BuildCarList();
    return g_cars.count;
}

const char *CarAt(int index)
{
    const int count = CarCount();
    if (!count)
        return "";
    index = (index % count + count) % count;
    return g_cars.names[index];
}

int CarIndex(const char *name)
{
    const int count = CarCount();
    for (int i = 0; i < count; ++i)
        if (!lstrcmpiA(CarAt(i), name))
            return i;
    return 0;
}

/** The player's car made anew as another (DebugVehicleSelection::SwitchPlayerVehicle 0x755340: the new car where the
 * old one stood, the old one gone). */
void SwapCar(const char *name)
{
    void *selection = Selection();
    if (!selection || !name[0])
        return;
    nfs::tuning::Keep();
    const bool done = NFS_ORIGINAL(0x755340, bool(__thiscall *)(void *, const char *))(selection, name);
    nfs::tuning::Reloaded();
    g_state.refreshTuning = true;
    g_state.restoreKept = true;
    g_state.haveLast = false;
    ResetStraight();
    ResetLap();
    char text[kLineLength];
    wsprintfA(text, done ? "NOW DRIVING %s" : "COULD NOT MAKE %s", name);
    SetStatus(text);
}

void ChangeHour(float delta)
{
    if (!nfs::timeofday::Enabled())
        return;
    float hour = nfs::timeofday::Hour() + delta;
    hour = hour < 0.0f ? hour + 24.0f : (hour >= 24.0f ? hour - 24.0f : hour);
    nfs::timeofday::SetHour(hour);
}

void Export()
{
    char path[MAX_PATH];
    if (!nfs::tuning::Export(path))
        return;
    const char *name = path + lstrlenA(path);
    while (name > path && name[-1] != '\\')
        --name;
    char text[kLineLength];
    wsprintfA(text, "EXPORTED DEV\\TUNING\\%s", name);
    SetStatus(text);
}

void ClearResults()
{
    g_state.best = Run();
    g_state.lastRun = Run();
    g_state.haveLastRun = false;
    g_state.lap.best = 0.0f;
    g_state.lap.last = 0.0f;
    for (float &b : g_state.lap.bestSectors)
        b = 0.0f;
    ResetStraight();
    ResetLap();
    SetStatus("RESULTS CLEARED");
}

void OpenPage(MenuPage page)
{
    if (g_state.page != kPageMain)
        nfs::tuning::Keep();
    g_state.page = page;
    if (page == kPageUpgrades)
        nfs::tuning::SetView(nfs::tuning::kViewUpgrades);
    else if (page == kPageTuning)
        nfs::tuning::SetView(nfs::tuning::kViewSettings);
}

void ToggleMenu()
{
    g_state.menuOpen = !g_state.menuOpen;
    if (!g_state.menuOpen && g_state.page != kPageMain)
        nfs::tuning::Keep();
    if (g_state.menuOpen)
    {
        OpenPage(kPageMain);
        g_state.carChoice = CarIndex(nfs::tuning::CarName());
    }
}

/** Enter (or Right on an entry that opens a page) on the main page. */
void Activate(int item)
{
    switch (item)
    {
    case kItemStraight:
        g_driver.active = false;
        g_state.teleport = 1;
        g_state.autoStage = kAutoOff;
        g_state.menuOpen = false;
        break;
    case kItemLoop:
        g_state.teleport = 2;
        g_state.autoStage = kAutoOff;
        g_state.menuOpen = false;
        break;
    case kItemReset:
        g_state.teleport = 3;
        g_state.menuOpen = false;
        break;
    case kItemAuto:
        StartAuto();
        g_state.menuOpen = false;
        break;
    case kItemLaps:
        if (g_driver.active)
            StopLaps("AUTO LAPS STOPPED");
        else
            StartLaps(0);
        g_state.menuOpen = false;
        break;
    case kItemCar:
        if (lstrcmpiA(CarAt(g_state.carChoice), nfs::tuning::CarName()))
            g_state.swapTo = g_state.carChoice;
        break;
    case kItemUpgrades:
        OpenPage(kPageUpgrades);
        break;
    case kItemTuning:
        OpenPage(kPageTuning);
        break;
    case kItemApply:
        g_state.reloadWanted = true;
        break;
    case kItemExport:
        Export();
        break;
    case kItemRevert:
        nfs::tuning::SetView(nfs::tuning::kViewSettings);
        nfs::tuning::Apply(nfs::tuning::kRevertAll);
        g_state.reloadWanted = true;
        break;
    case kItemTelemetry:
        g_state.telemetry = !g_state.telemetry;
        break;
    case kItemClear:
        ClearResults();
        break;
    case kItemClose:
        g_state.menuOpen = false;
        break;
    default:
        break;
    }
}

/** Left (-1) or Right (+1) on the main page: the entry's value. */
void Change(int item, int direction)
{
    switch (item)
    {
    case kItemCar:
        g_state.carChoice += direction;
        break;
    case kItemTelemetry:
        g_state.telemetry = !g_state.telemetry;
        break;
    case kItemTime:
        ChangeHour(static_cast<float>(direction));
        break;
    case kItemWeather:
        nfs::weather::StepMode(direction);
        break;
    case kItemUpgrades:
    case kItemTuning:
        if (direction > 0)
            Activate(item);
        break;
    default:
        break;
    }
}

nfs::tuning::Action Step(bool up, bool shift, bool control)
{
    if (shift)
        return up ? nfs::tuning::kIncreaseLarge : nfs::tuning::kDecreaseLarge;
    if (control)
        return up ? nfs::tuning::kIncreaseFine : nfs::tuning::kDecreaseFine;
    return up ? nfs::tuning::kIncrease : nfs::tuning::kDecrease;
}

void Press(int key, bool shift, bool control)
{
    if (key == kKeyMenu || key == kKeyMenuNumpad)
    {
        ToggleMenu();
        return;
    }
    if (g_state.menuOpen && key >= kFirstMenuKey)
    {
        const bool list = g_state.page != kPageMain;
        switch (key)
        {
        case kKeyUp:
            if (list)
                nfs::tuning::Apply(nfs::tuning::kPreviousField);
            else
                g_state.item = (g_state.item + kItemCount - 1) % kItemCount;
            break;
        case kKeyDown:
            if (list)
                nfs::tuning::Apply(nfs::tuning::kNextField);
            else
                g_state.item = (g_state.item + 1) % kItemCount;
            break;
        case kKeyLeft:
        case kKeyRight:
            if (list)
                nfs::tuning::Apply(Step(key == kKeyRight, shift, control));
            else
                Change(g_state.item, key == kKeyRight ? 1 : -1);
            // A level makes new collections for the car: its behaviours are made again at once.
            if (g_state.page == kPageUpgrades)
                g_state.reloadWanted = true;
            break;
        case kKeyIncrease:
        case kKeyDecrease:
            if (list)
                nfs::tuning::Apply(Step(key == kKeyIncrease, shift, control));
            else
                Change(g_state.item, key == kKeyIncrease ? 1 : -1);
            if (g_state.page == kPageUpgrades)
                g_state.reloadWanted = true;
            break;
        case kKeyEnter:
            if (list)
                g_state.reloadWanted = true;
            else
                Activate(g_state.item);
            break;
        case kKeyBack:
            if (list)
                OpenPage(kPageMain);
            else
                g_state.menuOpen = false;
            break;
        case kKeyPageUp:
        case kKeyPageDown:
            if (list)
                nfs::tuning::Apply(key == kKeyPageUp ? nfs::tuning::kPreviousGroup : nfs::tuning::kNextGroup);
            break;
        case kKeyRevert:
            if (list)
                nfs::tuning::Apply(control ? nfs::tuning::kRevertAll : nfs::tuning::kRevertField);
            break;
        case kKeyGear:
            if (g_state.page == kPageTuning)
            {
                const int gears = nfs::tuning::ChangeGears(shift ? -1 : 1);
                char text[kLineLength];
                if (gears > 0)
                    wsprintfA(text, "%d FORWARD GEARS (ENTER APPLIES)", gears);
                else
                    lstrcpyA(text, "GEARS: 2 TO 7 FORWARD");
                SetStatus(text);
            }
            break;
        default:
            break;
        }
        return;
    }
    switch (key)
    {
    case kKeyStraight:
        Activate(kItemStraight);
        break;
    case kKeyLoop:
        Activate(kItemLoop);
        break;
    case kKeyReset:
        Activate(kItemReset);
        break;
    case kKeyAuto:
        if (g_state.autoStage == kAutoOff || g_state.autoStage == kAutoDone)
            StartAuto();
        else
        {
            g_state.autoStage = kAutoOff;
            SetStatus("HANDS-OFF RUN STOPPED");
        }
        break;
    case kKeyBoost:
        g_state.boost += 20.0f * kKmh;
        break;
    case kKeyStop:
        g_state.stop = true;
        break;
    case kKeyClear:
        ClearResults();
        break;
    case kKeyReload:
        g_state.reloadWanted = true;
        break;
    case kKeyExport:
        Export();
        break;
    case kKeyEarlier:
    case kKeyLater:
        ChangeHour(key == kKeyLater ? 1.0f : -1.0f);
        break;
    default:
        break;
    }
}

void ReadKeys()
{
    const bool shift = nfs::display::DebugKeyDown(VK_SHIFT);
    const bool control = nfs::display::DebugKeyDown(VK_CONTROL);
    const DWORD now = GetTickCount();
    for (int k = 0; k < kKeyCount; ++k)
    {
        Key &key = g_state.keys[k];
        const bool down = nfs::display::DebugKeyDown(kKeys[k].vk);
        // The menu's keys only act while it is open (the arrows drive the car otherwise).
        if (k >= kFirstMenuKey && !g_state.menuOpen)
        {
            key.down = down;
            continue;
        }
        if (down && !key.down)
        {
            Press(k, shift, control);
            key.repeatAt = now + 400;
        }
        else if (down && kKeys[k].repeats && now >= key.repeatAt)
        {
            Press(k, shift, control);
            key.repeatAt = now + 60;
        }
        key.down = down;
    }
}

/** Debug.ProvingKeys: "SECONDS:KEY,..." (KEY a kKeyNames entry, "shift+" or "ctrl+" before it). */
void LoadScript()
{
    char text[512];
    nfs::config::String("Debug", "ProvingKeys", "", text, sizeof(text));
    for (const char *p = text; *p && g_state.scriptCount < 32;)
    {
        Scripted &s = g_state.script[g_state.scriptCount];
        float value = 0.0f, scale = 0.0f;
        for (; (*p >= '0' && *p <= '9') || *p == '.'; ++p)
        {
            if (*p == '.')
                scale = 1.0f;
            else if (scale > 0.0f)
                value += (*p - '0') * (scale *= 0.1f);
            else
                value = value * 10.0f + (*p - '0');
        }
        if (*p++ != ':')
            break;
        int n = 0;
        while (*p && *p != ',' && n < 15)
            s.key[n++] = *p++;
        s.key[n] = 0;
        s.at = value;
        ++g_state.scriptCount;
        if (*p == ',')
            ++p;
    }
}

bool StartsWith(const char *text, const char *prefix)
{
    const int n = lstrlenA(prefix);
    return lstrlenA(text) >= n &&
           CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, text, n, prefix, n) == CSTR_EQUAL;
}

void RunScript(float sinceArrival)
{
    while (g_state.scriptNext < g_state.scriptCount && sinceArrival >= g_state.script[g_state.scriptNext].at)
    {
        const char *name = g_state.script[g_state.scriptNext++].key;
        const bool shift = StartsWith(name, "shift+"), control = StartsWith(name, "ctrl+");
        if (shift)
            name += 6;
        else if (control)
            name += 5;
        for (int k = 0; k < kKeyCount; ++k)
            if (!lstrcmpiA(name, kKeys[k].name))
            {
                nfs::Log("proving: key %s%s%s", shift ? "shift+" : "", control ? "ctrl+" : "", kKeys[k].name);
                Press(k, shift, control);
            }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Reloading the car's behaviours (tuning edits they copy when made).

void ReloadCar()
{
    // Edits given on the command line are that run's; they do not become the car's kept tuning.
    if (!g_state.tune[0])
        nfs::tuning::Keep();
    const Car c = FindCar();
    if (!c.simable || !c.body)
        return;
    char *vehicle = static_cast<char *>(c.simable) - kSimableInVehicle;
    // The ISimable is where PVehicle::ReloadBehaviors expects it if it gives the same rigid body.
    if (Virtual<void *>(c.simable, kSimableRigidBody) != c.body || vehicle[kVehicleLoading])
    {
        SetStatus("RELOAD: CAR NOT READY");
        return;
    }
    nfs::tuning::PrepareReload();
    // PVehicle::ReloadBehaviors keeps every behaviour of the same kind as before (PhysicsObject::LoadBehavior
    // 0x684bb0), so the engine and the suspension (which read the engine, gearbox, nitrous, induction, tyre, brake and
    // chassis collections and copy some of them when made) are let go first (PhysicsObject::UnloadBehavior 0x684ae0,
    // which tells the other behaviours) and made anew from the car's collections as they are now. The rigid body
    // stays: the reload places the new behaviours by it.
    static const char *const kMechanics[] = {"BEHAVIOR_MECHANIC_ENGINE", "BEHAVIOR_MECHANIC_SUSPENSION"};
    for (const char *mechanic : kMechanics)
    {
        alignas(4) unsigned int key;
        new (&key) UCrc32(mechanic);
        NFS_ORIGINAL(0x684ae0, void(__thiscall *)(void *, const void *))(vehicle, &key);
    }
    NFS_ORIGINAL(0x6863b0, void(__thiscall *)(void *))(vehicle);
    nfs::tuning::Reloaded();
    g_state.refreshTuning = true;
    g_state.haveLast = false;
    SetStatus("CAR RELOADED");
}

// ---------------------------------------------------------------------------------------------------------------
// The world files (tools/provingworld.py): the site's textures, then TRACKS\PROVING.BIN, world solids PROVING_0000...
// placed as world models, so the site is drawn, lit and shadowed as the world is. Without them the site is drawn as
// world quads (Render).

const char kWorldTextures[] = "TRACKS\\PROVINGTEX.BIN";
const char kWorldFile[] = "TRACKS\\PROVING.BIN";
const char kWorldSolid[] = "PROVING_%04d";
const int kMaxWorldModels = 1024;
/** bTList<WorldModel> WorldModelList: its head (next, prev) links the models. */
const unsigned int kWorldModelList = 0x9b0f90;
/** ResourceFileType of the game's own bundles (GLOBAL\\GLOBALB.BUN). */
const int kResourceBundle = 1;
/** WorldModel: drawn by the game's loop (+0x28), its eModel (+8), its matrix (+0x40). */
const int kWorldModelDrawn = 0x28;
const int kWorldModelModel = 0x08;
const int kWorldModelMatrix = 0x40;
/** eSolid: the matrix of its chunk header. */
const int kSolidMatrix = 0x40;

struct WorldFile
{
    bool tried;
    bool loading;
    bool loaded;
    void *models[kMaxWorldModels];
    int count;
};

WorldFile g_world;
/** Debug.ProvingLit: the models get a light context (the sun and the lights round them, as cars do). */
const bool g_worldLit = nfs::config::Int("Debug", "ProvingLit", 1) != 0;

bool InWorldModelList(void *model)
{
    char *const head = reinterpret_cast<char *>(kWorldModelList);
    for (char *m = *reinterpret_cast<char **>(head); m != head; m = *reinterpret_cast<char **>(m))
        if (m == model)
            return true;
    return false;
}

/** Loads the world file once a session and keeps its models in the world's list. */
void UpdateWorld()
{
    if (!g_world.tried)
    {
        g_world.tried = true;
        if (!nfs::pack::Exists(kWorldTextures) || !nfs::pack::Exists(kWorldFile))
        {
            nfs::Log("proving: no %s, the site is drawn as quads", kWorldFile);
            return;
        }
        LoadResourceFile(kWorldTextures, static_cast<ResourceFileType>(kResourceBundle), 0);
        LoadResourceFile(kWorldFile, static_cast<ResourceFileType>(kResourceBundle), 0);
        g_world.loading = true;
    }
    if (g_world.loading && IsResourceLoadingComplete())
    {
        g_world.loading = false;
        g_world.loaded = true;
    }
    if (!g_world.loaded || (g_world.count && InWorldModelList(g_world.models[0])))
        return;
    // The models: anew whenever the world's list lost them (a track unload clears it).
    g_world.count = 0;
    char name[32];
    for (int i = 0; i < kMaxWorldModels; ++i)
    {
        wsprintfA(name, kWorldSolid, i);
        const unsigned int hash = bStringHash(name);
        eSolid *solid = eFindSolid(hash, nullptr);
        if (!solid)
            break;
        void *memory = bOMalloc(WorldModelSlotPool);
        if (!memory)
            break;
        // The solid's own matrix places it (tools/provingworld.py: the middle of its tile).
        bMatrix4 placement = *reinterpret_cast<const bMatrix4 *>(reinterpret_cast<const char *>(solid) + kSolidMatrix);
        void *model = new (memory) WorldModel(hash, &placement, g_worldLit);
        // Left out of the game's own drawing of the world models (RenderWorldModels draws them, dev/proving.h): the
        // game culls a model by the size on screen of a 35 m sphere at its place, which leaves out a tile whose
        // middle is behind the camera though its ground is in front.
        static_cast<char *>(model)[kWorldModelDrawn] = 0;
        g_world.models[g_world.count++] = model;
    }
    nfs::Log("proving: %d world models from %s", g_world.count, kWorldFile);
}

// ---------------------------------------------------------------------------------------------------------------
// Drawing.

struct Batch
{
    nfs::worldquads::Vertex *corners;
    int quads;
    int room;
};

enum BatchId
{
    kBatchAsphalt,
    kBatchGround,
    kBatchPaint,
    kBatchWall,
    kBatchTyre,
    kBatchCount,
};

const char *const kBatchTextures[kBatchCount] = {"GRAY_MID_A", "GRAY_MID_A", "WHITE_LINE", "GRAY_MID_A", "TREAD"};
Batch g_batches[kBatchCount];

TextureInfo *LoadedTexture(const char *name)
{
    TextureInfo *info = GetTextureInfo(bStringHash(name), 1, 0);
    return info && info != DefaultTextureInfo && info->PlatInfo ? info : nullptr;
}

nfs::worldquads::Vertex *NewQuad(BatchId id)
{
    Batch &b = g_batches[id];
    if (b.quads == b.room)
    {
        const int room = b.room ? b.room * 2 : 1024;
        nfs::worldquads::Vertex *grown =
            static_cast<nfs::worldquads::Vertex *>(::operator new(sizeof(nfs::worldquads::Vertex) * 4 * room));
        if (b.corners)
        {
            memcpy(grown, b.corners, sizeof(nfs::worldquads::Vertex) * 4 * b.quads);
            ::operator delete(b.corners);
        }
        b.corners = grown;
        b.room = room;
    }
    return b.corners + 4 * b.quads++;
}

void Corner(nfs::worldquads::Vertex &v, float x, float y, float z, float u, float t, unsigned int color)
{
    v.x = x;
    v.y = y;
    v.z = z;
    v.color = color;
    v.u = u;
    v.v = t;
    v.maskU = 0.0f;
    v.maskV = 0.0f;
}

/** A flat strip between two cross sections, `from` and `to` metres left of the centre line at each end. */
void Flat(BatchId id, const Sample &a, const Sample &b, float from, float to, float z, float tile, unsigned int color,
          const float *na, const float *nb)
{
    nfs::worldquads::Vertex *q = NewQuad(id);
    const float u0 = tile > 0.0f ? from / tile : 0.0f, u1 = tile > 0.0f ? to / tile : 1.0f;
    const float v0 = tile > 0.0f ? a.s / tile : 0.0f, v1 = tile > 0.0f ? (a.s + (b.s > a.s ? b.s - a.s : 0.0f)) / tile
                                                                         : 1.0f;
    Corner(q[0], a.x + na[0] * from, a.y + na[1] * from, z, u0, v0, color);
    Corner(q[1], b.x + nb[0] * from, b.y + nb[1] * from, z, u0, v1, color);
    Corner(q[2], b.x + nb[0] * to, b.y + nb[1] * to, z, u1, v1, color);
    Corner(q[3], a.x + na[0] * to, a.y + na[1] * to, z, u1, v0, color);
}

/** A vertical face `offset` metres left of the centre line from z0 to z1, seen from the road. */
void Face(BatchId id, const Sample &a, const Sample &b, float offset, float z0, float z1, float tile,
          unsigned int color, const float *na, const float *nb, float length)
{
    nfs::worldquads::Vertex *q = NewQuad(id);
    const float ax = a.x + na[0] * offset, ay = a.y + na[1] * offset;
    const float bx = b.x + nb[0] * offset, by = b.y + nb[1] * offset;
    const float u0 = tile > 0.0f ? a.s / tile : 0.0f, u1 = tile > 0.0f ? (a.s + length) / tile : 1.0f;
    const float v0 = tile > 0.0f ? z0 / tile : 0.0f, v1 = tile > 0.0f ? z1 / tile : 1.0f;
    Corner(q[0], ax, ay, z0, u0, v0, color);
    Corner(q[1], bx, by, z0, u1, v0, color);
    Corner(q[2], bx, by, z1, u1, v1, color);
    Corner(q[3], ax, ay, z1, u0, v1, color);
}

/** A post (two crossed vertical quads) at (x, y). */
void Post(float x, float y, float height, float width, unsigned int color)
{
    for (int i = 0; i < 2; ++i)
    {
        nfs::worldquads::Vertex *q = NewQuad(kBatchPaint);
        const float dx = i ? width * 0.5f : 0.0f, dy = i ? 0.0f : width * 0.5f;
        Corner(q[0], x - dx, y - dy, 0.0f, 0.0f, 0.0f, color);
        Corner(q[1], x + dx, y + dy, 0.0f, 1.0f, 0.0f, color);
        Corner(q[2], x + dx, y + dy, height, 1.0f, 1.0f, color);
        Corner(q[3], x - dx, y - dy, height, 0.0f, 1.0f, color);
    }
}

/** The left normal at a sample: the mean of its two pieces' (a mitre), so neighbouring quads meet. */
void Normal(const Road &road, int i, float *n)
{
    const Sample &s = road.samples[i];
    float x = -s.dy, y = s.dx;
    const bool hasPrevious = road.closed || i > 0;
    if (hasPrevious)
    {
        const Sample &p = road.samples[(i + road.count - 1) % road.count];
        const float px = -p.dy, py = p.dx;
        const float mx = x + px, my = y + py;
        const float length = sqrtf(mx * mx + my * my);
        const float c = (mx * x + my * y) / (length > 0.0f ? length : 1.0f);
        if (length > 0.0f && c > 0.3f)
        {
            x = mx / length / c;
            y = my / length / c;
        }
    }
    n[0] = x;
    n[1] = y;
}

void DrawRoad(const Road &road, RoadId id, float cx, float cy)
{
    const int segments = road.closed ? road.count : road.count - 1;
    const float far2 = kDrawDistance * kDrawDistance;
    for (int i = 0; i < segments; ++i)
    {
        const Sample &a = road.samples[i];
        const Sample &b = road.samples[(i + 1) % road.count];
        const float mx = (a.x + b.x) * 0.5f - cx, my = (a.y + b.y) * 0.5f - cy;
        if (mx * mx + my * my > far2)
            continue;
        float na[2], nb[2];
        Normal(road, i, na);
        Normal(road, (i + 1) % road.count, nb);
        const float length = sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
        const float half = road.half, wall = road.wall;
        Flat(kBatchAsphalt, a, b, -half, half, kHeight, 8.0f, kAsphalt, na, nb);
        Flat(kBatchGround, a, b, half, wall, kHeight, 8.0f, kGrass, na, nb);
        Flat(kBatchGround, a, b, -wall, -half, kHeight, 8.0f, kGrass, na, nb);
        const float z = kHeight + kPaintHeight;
        const float curvature = a.curvature;
        const bool kerb = fabsf(curvature) > kKerbCurvature;
        const int stripe = static_cast<int>(a.s / (kerb ? kArcStep : 10.0f));
        if (kerb)
        {
            const unsigned int colour = (stripe & 1) ? kRed : kWhite;
            Flat(kBatchPaint, a, b, half - kKerbWidth, half, z, 0.0f, colour, na, nb);
            Flat(kBatchPaint, a, b, -half, -half + kKerbWidth, z, 0.0f, colour, na, nb);
        }
        else
        {
            Flat(kBatchPaint, a, b, half - kLineInset - kLineWidth, half - kLineInset, z, 0.0f, kWhite, na, nb);
            Flat(kBatchPaint, a, b, -half + kLineInset, -half + kLineInset + kLineWidth, z, 0.0f, kWhite, na, nb);
        }
        // Centre dashes on the straight; lane lines a third of the way across.
        if (id == kStraight && (static_cast<int>(a.s / 10.0f) & 1))
            for (int lane = -1; lane <= 1; lane += 2)
                Flat(kBatchPaint, a, b, lane * half / 3.0f - 0.1f, lane * half / 3.0f + 0.1f, z, 0.0f, kWhite, na,
                     nb);
        // Walls: concrete with a painted band, tyres round the outside of the tightest corners.
        for (int side = -1; side <= 1; side += 2)
        {
            const float offset = wall * side;
            const bool outside = curvature * side < 0.0f;
            const bool tyres = curvature != 0.0f && 1.0f / fabsf(curvature) < kTyreRadius && outside;
            if (tyres)
            {
                Face(kBatchTyre, a, b, offset, kHeight, kHeight + kWallHeight, 0.6f, kTyre, na, nb, length);
                continue;
            }
            Face(kBatchWall, a, b, offset, kHeight, kHeight + kBandBottom, 2.0f, kConcrete, na, nb, length);
            const unsigned int band = kerb ? ((stripe & 1) ? kRed : kWhite) : ((stripe & 1) ? kBlue : kWhite);
            Face(kBatchPaint, a, b, offset, kHeight + kBandBottom, kHeight + kWallHeight, 0.0f, band, na, nb, length);
        }
    }
}

void DrawStraightMarks(float cx, float cy)
{
    const float z = kHeight + kPaintHeight * 2.0f;
    // The start line (checkers) and a line across every 100 m, the measured distances in colour.
    struct Mark
    {
        float at;
        float width;
        unsigned int color;
    };
    for (float at = 0.0f; at <= kStraightLength + kRunOff; at += kPostEvery * 0.5f)
    {
        const float x = kStraightX + at;
        if (fabsf(x - cx) > kDrawDistance || fabsf(kStraightY - cy) > kDrawDistance)
            continue;
        const bool hundred = fmodf(at, kPostEvery) < 1.0f;
        const bool braking = at > kStraightEnd - kBrakeZone;
        unsigned int color = braking ? kOrange : (fmodf(at, 500.0f) < 1.0f ? kGreen : kYellow);
        if (hundred || braking)
        {
            const float height = at == kStraightLength || at == kStraightEnd - kBrakeZone ? 4.0f : 2.5f;
            Post(x, kStraightY + kStraightWall - 0.5f, height, 0.3f, at == kStraightLength ? kRed : color);
            Post(x, kStraightY - kStraightWall + 0.5f, height, 0.3f, at == kStraightLength ? kRed : color);
        }
        if (hundred && at > 0.0f && !braking)
        {
            nfs::worldquads::Vertex *q = NewQuad(kBatchPaint);
            const float w = fmodf(at, 500.0f) < 1.0f ? 1.0f : 0.3f;
            Corner(q[0], x, kStraightY - kStraightHalf, z, 0.0f, 0.0f, kWhite);
            Corner(q[1], x + w, kStraightY - kStraightHalf, z, 1.0f, 0.0f, kWhite);
            Corner(q[2], x + w, kStraightY + kStraightHalf, z, 1.0f, 1.0f, kWhite);
            Corner(q[3], x, kStraightY + kStraightHalf, z, 0.0f, 1.0f, kWhite);
        }
    }
    const Mark marks[] = {{402.336f, 0.5f, kRed}, {201.168f, 0.3f, kYellow}};
    for (const Mark &m : marks)
    {
        const float x = kStraightX + m.at;
        if (fabsf(x - cx) > kDrawDistance)
            continue;
        nfs::worldquads::Vertex *q = NewQuad(kBatchPaint);
        Corner(q[0], x, kStraightY - kStraightHalf, z, 0.0f, 0.0f, m.color);
        Corner(q[1], x + m.width, kStraightY - kStraightHalf, z, 1.0f, 0.0f, m.color);
        Corner(q[2], x + m.width, kStraightY + kStraightHalf, z, 1.0f, 1.0f, m.color);
        Corner(q[3], x, kStraightY + kStraightHalf, z, 0.0f, 1.0f, m.color);
        Post(x, kStraightY + kStraightWall - 0.5f, 3.5f, 0.4f, m.color);
        Post(x, kStraightY - kStraightWall + 0.5f, 3.5f, 0.4f, m.color);
    }
    // Checkered lines: the straight's start and its 2000 m board, the loop's start.
    struct Line
    {
        float x, y, half;
    };
    const Line lines[] = {{kStraightX, kStraightY, kStraightHalf},
                          {kStraightX + kStraightLength, kStraightY, kStraightHalf},
                          {kLoopX, kLoopY, kLoopHalf}};
    for (const Line &l : lines)
    {
        if (fabsf(l.x - cx) > kDrawDistance || fabsf(l.y - cy) > kDrawDistance)
            continue;
        const int rows = static_cast<int>(l.half * 2.0f / kCheckerSquare);
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < 3; ++column)
            {
                nfs::worldquads::Vertex *q = NewQuad(kBatchPaint);
                const float x0 = l.x - (column + 1) * kCheckerSquare, y0 = l.y - l.half + row * kCheckerSquare;
                const unsigned int color = ((row + column) & 1) ? kWhite : kBlack;
                Corner(q[0], x0, y0, z, 0.0f, 0.0f, color);
                Corner(q[1], x0 + kCheckerSquare, y0, z, 1.0f, 0.0f, color);
                Corner(q[2], x0 + kCheckerSquare, y0 + kCheckerSquare, z, 1.0f, 1.0f, color);
                Corner(q[3], x0, y0 + kCheckerSquare, z, 0.0f, 1.0f, color);
            }
    }
}

/** Grass over the whole site, under the roads. */
void DrawGround(float cx, float cy)
{
    const float z = kHeight - kGroundBelow;
    for (float x = kGroundMinX; x < kGroundMaxX; x += kGroundCell)
        for (float y = kGroundMinY; y < kGroundMaxY; y += kGroundCell)
        {
            const float x1 = x + kGroundCell < kGroundMaxX ? x + kGroundCell : kGroundMaxX;
            const float y1 = y + kGroundCell < kGroundMaxY ? y + kGroundCell : kGroundMaxY;
            const float mx = (x + x1) * 0.5f - cx, my = (y + y1) * 0.5f - cy;
            if (mx * mx + my * my > (kDrawDistance + kGroundCell) * (kDrawDistance + kGroundCell))
                continue;
            nfs::worldquads::Vertex *q = NewQuad(kBatchGround);
            Corner(q[0], x, y, z, x / 8.0f, y / 8.0f, kGround);
            Corner(q[1], x1, y, z, x1 / 8.0f, y / 8.0f, kGround);
            Corner(q[2], x1, y1, z, x1 / 8.0f, y1 / 8.0f, kGround);
            Corner(q[3], x, y1, z, x / 8.0f, y1 / 8.0f, kGround);
        }
    // The straight's ends.
    const float ends[2] = {kStraightX - kRunUp, kStraightX + kStraightLength + kRunOff};
    for (int i = 0; i < 2; ++i)
    {
        if (fabsf(ends[i] - cx) > kDrawDistance)
            continue;
        nfs::worldquads::Vertex *q = NewQuad(kBatchTyre);
        const float x = ends[i];
        Corner(q[0], x, kStraightY - kStraightWall, 0.0f, 0.0f, 0.0f, kTyre);
        Corner(q[1], x, kStraightY + kStraightWall, 0.0f, kStraightWall * 2.0f / 0.6f, 0.0f, kTyre);
        Corner(q[2], x, kStraightY + kStraightWall, kWallHeight, kStraightWall * 2.0f / 0.6f, kWallHeight / 0.6f, kTyre);
        Corner(q[3], x, kStraightY - kStraightWall, kWallHeight, 0.0f, kWallHeight / 0.6f, kTyre);
    }
}

// ---------------------------------------------------------------------------------------------------------------

bool Racing()
{
    return TheGameFlowManager_CurrentGameFlowState == kGameFlowRacing;
}

void LoadSettings()
{
    char text[64];
    nfs::config::String("Debug", "ProvingStart", "straight", text, sizeof(text));
    g_state.startOnLoop = !lstrcmpiA(text, "loop");
    g_state.autoFromConfig = nfs::config::Int("Debug", "ProvingAuto", 0) != 0;
    g_state.autoQuit = nfs::config::Int("Debug", "ProvingQuit", 0) != 0;
    nfs::config::String("Debug", "ProvingTune", "", g_state.tune, sizeof(g_state.tune));
    nfs::config::String("Debug", "ProvingCar", "", g_state.car, sizeof(g_state.car));
    g_state.telemetry = true;
    g_state.restHeight = kDefaultRestHeight;
    g_state.swapTo = -1;
    LoadScript();
}
} // namespace

bool Enabled()
{
    if (g_state.enabled < 0)
    {
        g_state.enabled = nfs::config::Int("Debug", "ProvingGround", 0) != 0;
        LoadSettings();
    }
    return g_state.enabled != 0 || g_state.requested;
}

bool MenuShown()
{
    return nfs::config::Int("Debug", "ProvingMenu", 1) != 0;
}

void StartFromMenu()
{
    Enabled();
    GRaceDatabase *races = GRaceDatabase::mObj;
    if (!races || !FEDatabase)
        return;
    GRaceParameters *sprint = nullptr;
    const unsigned int count = races->GetRaceCount();
    for (unsigned int i = 0; i < count && !sprint; ++i)
    {
        GRaceParameters *race = races->GetRaceParameters(i);
        if (race && static_cast<int>(race->GetRaceType()) == kRaceTypeSprint)
            sprint = race;
    }
    if (!sprint)
    {
        nfs::Log("proving: no sprint race to start from");
        return;
    }
    GRaceCustom *custom = races->AllocCustomRace(sprint);
    custom->SetCopsEnabled(false);
    custom->SetNumOpponents(0);
    custom->SetTrafficDensity(0);
    custom->SetNumLaps(1);
    races->SetStartupRace(custom, static_cast<GRace::Context>(0));
    races->FreeCustomRace(custom);
    FEDatabase->iNumPlayers = 1;
    FEDatabase->FEGameMode |= kGameModeQuickRace;
    g_state.requested = true;
    g_state.raced = false;
    g_state.menu = kMenuOpening;
    g_state.warpRequested = false;
    g_state.arrived = false;
    nfs::Log("proving: from the main menu, on %s's start", sprint->GetEventID());
}

const char *PackageFor(const char *package)
{
    if (!package || g_state.menu == kMenuNone)
        return nullptr;
    for (const char *prefix : kPassingPackages)
        if (!_strnicmp(package, prefix, lstrlenA(prefix)))
            return nullptr;
    if (g_state.menu == kMenuOpening)
    {
        g_state.menu = kMenuChoosing;
        nfs::Log("proving: the car select in place of %s", package);
        return kCarSelectPackage;
    }
    if (!lstrcmpiA(package, kCarSelectPackage))
        return nullptr;
    // Backed out of the car select: no proving ground, back to the main menu.
    g_state.menu = kMenuNone;
    g_state.requested = false;
    FEDatabase->FEGameMode &= ~kGameModeQuickRace;
    nfs::Log("proving: car select left for %s, back to the main menu", package);
    return kMainMenuPackage;
}

bool ChoosingCar()
{
    return g_state.menu == kMenuChoosing;
}

/** The dev test car (-dev): the car driven made into a 500 km/h car that gets there at once, for profiling the game at
 * speeds no car of the game reaches (streaming, culling, shadows, mip levels). On a Ford GT it does 0-300 km/h in 5.7 s
 * and 516 km/h on the proving ground's straight (drag holds it, below the red line). Four times the torque, a 30 %
 * taller final drive, 40 % less drag, grip and downforce up for the launch and the stability, twice the brakes, three
 * times the nitrous. */
/** The Developer profile's test car (-dev): its car of this type gets kTestCarTune in every race. */
const char kDeveloperTestCar[] = "fordgt";
/** IVehicle's name (its vtable slot: +0x4c, as source/dev/DevTools.cpp reads it). */
const int kVehicleNameSlot = 0x4c / 4;

const char kTestCarTune[] = "engine.TORQUE*4;engine.SPEED_LIMITER=1000;transmission.FINAL_GEAR*0.7;"
                            "chassis.DRAG_COEFFICIENT*0.6;tires.STATIC_GRIP*1.8;tires.DYNAMIC_GRIP*1.5;"
                            "chassis.AERO_COEFFICIENT*1.5;brakes.BRAKES*2;nos.NOS_CAPACITY*3";

/** Edits made on the player's car now, and its behaviours made again; a tune given for the run is not the car's kept
 * tuning (ReloadCar keeps it when g_state.tune is empty). */
int TuneCarNow(const char *text, bool revert)
{
    nfs::tuning::SetView(nfs::tuning::kViewAll);
    nfs::tuning::Refresh();
    int made = 0;
    if (revert)
        nfs::tuning::Apply(nfs::tuning::kRevertAll);
    else
        made = nfs::tuning::ApplyText(text);
    char kept[sizeof(g_state.tune)];
    lstrcpynA(kept, g_state.tune, sizeof(kept));
    lstrcpynA(g_state.tune, revert ? "revert" : text, sizeof(g_state.tune));
    ReloadCar();
    lstrcpynA(g_state.tune, kept, sizeof(g_state.tune));
    return made;
}

/**
 * Tuning edits made on the player's car in any race, three seconds after it is there:
 *     -set Debug.CarTune=TEXT     the edits (gameplay/vehicles/tuning.h ApplyText, e.g. upgrades.ENGINE=9;upgrades.NOS=9)
 *     -dev -set Dev.TestCar=1     the dev test car (kTestCarTune) on whatever car the race gives
 *     -dev, F4                    the dev test car on, and off again (every setting put back)
 */
int g_carTuneState = -1;

bool CarTunePending()
{
    if (g_carTuneState >= 0)
        return g_carTuneState == 0;
    char text[4];
    nfs::config::String("Debug", "CarTune", "", text, sizeof(text));
    return text[0] != 0;
}

void DebugCarTune()
{
    int &state = g_carTuneState;
    static DWORD seen;
    static char text[sizeof(g_state.tune)];
    static bool keyDown, testCar;
    if (state < 0)
    {
        nfs::config::String("Debug", "CarTune", "", text, sizeof(text));
        if (!text[0] && nfs::dev::Enabled() && nfs::config::Int("Dev", "TestCar", 0))
            lstrcpynA(text, kTestCarTune, sizeof(text));
        state = text[0] ? 0 : 1;
    }
    static bool developerTried;
    if (!Racing())
    {
        seen = 0;
        developerTried = false;
        return;
    }
    const Car c = FindCar();
    if (!c.simable || !c.body)
        return;
    // -dev's Developer profile: its Ford GT is the dev test car in every race (F4 still takes it off).
    if (!developerTried && !testCar && nfs::testprofile::Developer() && c.vehicle)
    {
        if (!seen)
            seen = GetTickCount();
        if (GetTickCount() - seen >= 3000)
        {
            developerTried = true;
            typedef const char *(__thiscall * NameOf)(void *);
            const char *car = reinterpret_cast<NameOf>((*reinterpret_cast<void ***>(c.vehicle))[kVehicleNameSlot])(c.vehicle);
            if (car && !lstrcmpiA(car, kDeveloperTestCar))
            {
                testCar = true;
                const int made = TuneCarNow(kTestCarTune, false);
                nfs::Log("dev: the Developer profile's %s is the test car (%d edits)", car, made);
                SetStatus("DEV TEST CAR ON");
            }
        }
    }
    if (nfs::dev::Enabled())
    {
        const bool key = nfs::display::DebugKeyDown(VK_F4);
        if (key && !keyDown)
        {
            testCar = !testCar;
            const int made = TuneCarNow(kTestCarTune, !testCar);
            nfs::Log("dev: test car %s (%d edits)", testCar ? "on" : "off", made);
            SetStatus(testCar ? "DEV TEST CAR ON" : "DEV TEST CAR OFF");
        }
        keyDown = key;
    }
    if (state)
        return;
    if (!seen)
        seen = GetTickCount();
    if (GetTickCount() - seen < 3000)
        return;
    state = 1;
    const int made = TuneCarNow(text, false);
    testCar = !lstrcmpA(text, kTestCarTune);
    nfs::Log("car tune: %d edits (%s)", made, testCar ? "the dev test car" : "Debug.CarTune");
}

void Update()
{
    DebugCarTune();
    if (!Enabled())
        return;
    if (!g_state.built)
        Build();
    if (!Racing())
    {
        g_state.warpRequested = false;
        g_state.arrived = false;
        // Back in the front end after the menu's proving ground: later races are the game's again.
        if (g_state.requested && g_state.raced &&
            TheGameFlowManager_CurrentGameFlowState == kGameFlowFrontEnd)
        {
            g_state.requested = false;
            nfs::Log("proving: back in the front end");
        }
        return;
    }
    g_state.menu = kMenuNone;
    g_state.raced = true;
    UpdateWorld();
    if (!g_state.warpRequested)
    {
        g_state.warpRequested = true;
        // Debug.Warp (a test's own spot on the site) goes first.
        if (nfs::config::Has("Debug", "Warp"))
            ;
        else if (g_state.startOnLoop)
            nfs::pose::RequestWarp(kLoopX - 30.0f, kLoopY, kHeight + RestHeight() + kPutAbove, 0.0f);
        else
            nfs::pose::RequestWarp(kStraightX - kCarHalfLength - 0.5f, kStraightY, kHeight + RestHeight() + kPutAbove,
                                   0.0f);
        nfs::Log("proving: the car goes to the proving ground");
    }
    if (!g_state.arrived && nfs::pose::WarpFinished())
    {
        g_state.arrived = true;
        ResetCamera();
        g_state.raceStart = Sim::GetTime();
        nfs::tuning::Refresh();
        g_state.refreshTuning = true;
        // The handling test measures the car as asked for, not as it was last tuned by hand.
        g_state.restoreKept = !nfs::config::Int("Debug", "ProvingHandling", 0);
        if (g_state.car[0])
            SwapCar(g_state.car);
        if (g_state.tune[0])
        {
            nfs::tuning::SetView(nfs::tuning::kViewAll);
            nfs::tuning::ApplyText(g_state.tune);
            nfs::tuning::SetView(nfs::tuning::kViewSettings);
            g_state.reloadWanted = nfs::tuning::NeedsReload();
        }
        if (g_state.autoFromConfig)
            StartAuto();
        const int laps = nfs::config::Int("Debug", "ProvingLaps", 0);
        if (laps || nfs::config::Int("Debug", "ProvingHandling", 0))
        {
            StartLaps(laps);
            g_driver.quit = g_state.autoQuit;
        }
        const Car c = FindCar();
        if (c.simable && c.vehicle)
            nfs::Log("proving: arrived; simable %08x, vehicle %08x (%d after it), rigid body %s", c.simable,
                     c.vehicle, static_cast<int>(static_cast<char *>(c.vehicle) - static_cast<char *>(c.simable)),
                     Virtual<void *>(c.simable, kSimableRigidBody) == c.body ? "matches" : "differs");
    }
    if (!g_state.arrived)
        return;
    ReadKeys();
    RunScript(Sim::GetTime() - g_state.raceStart);
    if (g_state.teleport)
    {
        Teleport(g_state.teleport);
        g_state.teleport = 0;
    }
    const Car c = FindCar();
    if (c.body && (g_state.boost > 0.0f || g_state.stop))
    {
        const float *v = Virtual<const float *>(c.body, kBodyLinearVelocity);
        const float *q = Virtual<const float *>(c.body, kBodyOrientation);
        float fx, fy;
        Forward(q, fx, fy);
        const float along = v[2] * fx - v[0] * fy + g_state.boost;
        const float out[3] = {g_state.stop ? 0.0f : -fy * along, v[1], g_state.stop ? 0.0f : fx * along};
        Virtual<void>(c.body, kBodySetLinearVelocity, out);
        if (g_state.stop)
        {
            const float zero[3] = {0.0f, 0.0f, 0.0f};
            Virtual<void>(c.body, kBodySetAngularVelocity, zero);
        }
        g_state.boost = 0.0f;
        g_state.stop = false;
    }
    if (g_state.refreshTuning)
    {
        g_state.refreshTuning = false;
        nfs::tuning::Refresh();
        // A car this session tuned before comes back as it was left.
        if (g_state.restoreKept && nfs::tuning::Restore())
            g_state.reloadWanted = true;
        g_state.restoreKept = false;
    }
    if (g_state.reloadWanted)
    {
        g_state.reloadWanted = false;
        ReloadCar();
    }
    if (g_state.swapTo >= 0)
    {
        const int index = g_state.swapTo;
        g_state.swapTo = -1;
        SwapCar(CarAt(index));
    }
    if (g_state.autoStage == kAutoDone && g_state.autoQuit && Sim::GetTime() - g_state.autoSince > 2.0f)
        ExitTheGameFlag = 1;
    if (g_driver.quit && !g_driver.active && g_driver.quitAt > 0.0f && Sim::GetTime() > g_driver.quitAt)
        ExitTheGameFlag = 1;
}

/** While the menu is open the arrows move through it: the car is held. */
void HoldForTuning(unsigned char *controls)
{
    if (!g_state.menuOpen)
        return;
    *reinterpret_cast<float *>(controls + kGas) = 0.0f;
    *reinterpret_cast<float *>(controls + kBrake) = 0.0f;
    *reinterpret_cast<float *>(controls + kSteering) = 0.0f;
}

void Controls(void *input, unsigned char *controls)
{
    if (!g_state.arrived || !Racing())
        return;
    const float now = Sim::GetTime();
    if (now == g_state.stepTime)
    {
        if (input == g_state.input)
        {
            HoldForTuning(controls);
            AutoControls(controls, now);
            if (g_driver.active)
                for (int b : kDriverControls)
                    *reinterpret_cast<float *>(controls + b) = g_state.driverControls[b / 4];
        }
        return;
    }
    if (input != nfs::pose::PlayerInterface(kInputHandle))
        return;
    g_state.stepTime = now;
    g_state.input = input;
    HoldForTuning(controls);
    const Car c = FindCar();
    if (!c.body)
        return;
    const float *position = Virtual<const float *>(c.body, kBodyPosition);
    const float *velocity = Virtual<const float *>(c.body, kBodyLinearVelocity);
    const float *q = Virtual<const float *>(c.body, kBodyOrientation);
    g_state.position[0] = position[2];
    g_state.position[1] = -position[0];
    float fx, fy;
    Forward(q, fx, fy);
    const float vx = velocity[2], vy = -velocity[0];
    g_state.speed = sqrtf(vx * vx + vy * vy);
    g_state.slip = g_state.speed > 1.0f ? (-vx * fy + vy * fx) / g_state.speed : 0.0f;
    g_state.rpm = c.engine ? Virtual<float>(c.engine, kEngineRpm) : 0.0f;
    g_state.gear = c.transmission ? Virtual<int>(c.transmission, kTransmissionGear) : 1;
    g_state.gas = *reinterpret_cast<const float *>(controls + kGas);
    g_state.brake = *reinterpret_cast<const float *>(controls + kBrake);
    g_state.steering = *reinterpret_cast<const float *>(controls + kSteering);
    const float dt = g_state.haveLast ? now - g_state.lastStepTime : 0.0f;
    if (dt > 0.0f)
    {
        const float ax = (vx - g_state.lastVelocity[0]) / dt, ay = (vy - g_state.lastVelocity[1]) / dt;
        const float longG = (ax * fx + ay * fy) / kGravity, latG = (-ax * fy + ay * fx) / kGravity;
        g_state.longG += (longG - g_state.longG) * 0.2f;
        g_state.latG += (latG - g_state.latG) * 0.2f;
    }
    g_state.lastVelocity[0] = vx;
    g_state.lastVelocity[1] = vy;
    g_state.lastStepTime = now;
    g_state.haveLast = true;

    Place place;
    g_state.road = RoadAt(g_state.position[0], g_state.position[1], place);
    if (g_state.road != kNoRoad)
    {
        const Sample &a = g_state.roads[g_state.road].samples[place.segment];
        g_state.s = place.s;
        g_state.lateral = place.lateral;
        g_state.sample = place.segment;
        g_state.heading = Heading(fx * a.dx + fy * a.dy, a.dx * fy - a.dy * fx);
        if (g_state.heading < 0.0f)
            g_state.heading += 2.0f * kPi;
    }
    Walls(c, q);
    if (g_state.road == kStraight)
        TimeStraight(now, dt, g_state.s - kRunUp);
    else if (g_state.road == kLoopRoad)
        TimeLoop(now, g_state.s);
    AutoControls(controls, now);
    DriveLoop(controls, Virtual<const float *>(c.body, kBodyAngularVelocity), fx, fy, now);
    for (int b : kDriverControls)
        g_state.driverControls[b / 4] = *reinterpret_cast<const float *>(controls + b);
}

bool AtSite(const float *point)
{
    return Enabled() && InSite(point[0], point[1]);
}

bool OverGround(const float *point)
{
    if (!Enabled())
        return false;
    const float x = point[2], y = -point[0];
    return point[1] > kHeight - kBelow && InSite(x, y);
}

void RenderWorldModels(eView *view, int flags)
{
    // Only while the models are still the world's (a track unload frees them; Update makes them again).
    if (!g_world.count || !Racing() || !Enabled() || !InWorldModelList(g_world.models[0]))
        return;
    typedef void(__thiscall * RenderModel)(void *, void *, eView *, int, void *, const bMatrix4 *);
    for (int i = 0; i < g_world.count; ++i)
    {
        char *model = static_cast<char *>(g_world.models[i]);
        void *eModel = *reinterpret_cast<void **>(model + kWorldModelModel);
        if (eModel)
            NFS_ORIGINAL(0x73b9c0, RenderModel)(model, eModel, view, flags, nullptr,
                                                reinterpret_cast<const bMatrix4 *>(model + kWorldModelMatrix));
    }
}

void Render()
{
    if (!Enabled() || !Racing())
        return;
    if (!g_state.built)
        Build();
    if (g_world.count)
        return;
    float car[4];
    if (!nfs::pose::PlayerCar(car) || !InSite(car[0], car[1]))
        return;
    TextureInfo *textures[kBatchCount];
    for (int i = 0; i < kBatchCount; ++i)
    {
        textures[i] = LoadedTexture(kBatchTextures[i]);
        g_batches[i].quads = 0;
    }
    if (!textures[kBatchAsphalt] || !textures[kBatchPaint])
        return;
    DrawGround(car[0], car[1]);
    DrawRoad(g_state.roads[kStraight], kStraight, car[0], car[1]);
    DrawRoad(g_state.roads[kLoopRoad], kLoopRoad, car[0], car[1]);
    DrawStraightMarks(car[0], car[1]);
    for (int i = 0; i < kBatchCount; ++i)
    {
        // A texture that is not loaded: the plain grey one stands in.
        TextureInfo *texture = textures[i] ? textures[i] : textures[kBatchAsphalt];
        nfs::worldquads::Draw(g_batches[i].corners, g_batches[i].quads, texture);
    }
}

/** "14:30". */
void FormatHour(char *out)
{
    if (!nfs::timeofday::Enabled())
    {
        lstrcpyA(out, "GAME");
        return;
    }
    const int minutes = static_cast<int>(nfs::timeofday::Hour() * 60.0f + 0.5f) % (24 * 60);
    wsprintfA(out, "%02d:%02d", minutes / 60, minutes % 60);
}

int MenuText(char (*lines)[kLineLength])
{
    int n = 0;
    if (g_state.page != kPageMain)
    {
        n = nfs::tuning::Text(reinterpret_cast<char(*)[nfs::tuning::kLineLength]>(lines), kMaxLines - 4);
        lstrcpyA(lines[n++], "UP/DOWN CHOOSE   LEFT/RIGHT OR NUM-/NUM+ CHANGE");
        lstrcpyA(lines[n++], "SHIFT X10  CTRL X0.1  DEL PUT BACK  ENTER APPLY");
        lstrcpyA(lines[n++], g_state.page == kPageTuning ? "PGUP/PGDN GROUP  INS ADD GEAR (SHIFT REMOVES)  BACKSPACE"
                                                         : "BACKSPACE BACK TO THE MENU");
        return n;
    }
    char value[40];
    wsprintfA(lines[n++], "PROVING GROUND  %s", nfs::tuning::CarName());
    lstrcpyA(lines[n++], "");
    for (int i = 0; i < kItemCount; ++i)
    {
        const char *label = "";
        value[0] = 0;
        switch (i)
        {
        case kItemStraight:
            label = "GO TO THE STRAIGHT";
            break;
        case kItemLoop:
            label = "GO TO THE HANDLING LOOP";
            break;
        case kItemReset:
            label = "PUT THE CAR BACK ON THE ROAD";
            break;
        case kItemAuto:
            label = "HANDS-OFF RUN TO TOP SPEED";
            break;
        case kItemLaps:
            label = g_driver.active ? "STOP THE HANDLING TEST" : "HANDLING TEST (AUTO LAPS ON THE LOOP)";
            break;
        case kItemCar: {
            label = "CAR";
            const char *choice = CarAt(g_state.carChoice);
            wsprintfA(value, "< %s >%s", choice, lstrcmpiA(choice, nfs::tuning::CarName()) ? "  ENTER: DRIVE IT" : "");
            break;
        }
        case kItemUpgrades:
            label = "UPGRADES >";
            break;
        case kItemTuning:
            label = "TUNING >";
            break;
        case kItemApply:
            label = nfs::tuning::NeedsReload() ? "APPLY CHANGES (RELOAD THE CAR) *" : "APPLY CHANGES (RELOAD THE CAR)";
            break;
        case kItemExport:
            label = "EXPORT TUNING";
            break;
        case kItemRevert:
            label = "PUT ALL TUNING BACK";
            break;
        case kItemTelemetry:
            label = "TELEMETRY";
            lstrcpyA(value, g_state.telemetry ? "< ON >" : "< OFF >");
            break;
        case kItemTime: {
            label = "TIME";
            char hour[16];
            FormatHour(hour);
            wsprintfA(value, "< %s >", hour);
            break;
        }
        case kItemWeather:
            label = "WEATHER";
            wsprintfA(value, "< %s >", nfs::weather::ModeName());
            break;
        case kItemClear:
            label = "CLEAR RESULTS";
            break;
        case kItemClose:
            label = "CLOSE";
            break;
        default:
            break;
        }
        wsprintfA(lines[n++], "%s %-30s %s", i == g_state.item ? ">" : " ", label, value);
    }
    lstrcpyA(lines[n++], "");
    lstrcpyA(lines[n++], "UP/DOWN CHOOSE  LEFT/RIGHT CHANGE  ENTER SELECT");
    lstrcpyA(lines[n++], "BACKSPACE OR F1 CLOSES  (THE CAR WAITS MEANWHILE)");
    return n;
}

int Text(char (*lines)[kLineLength])
{
    if (!Enabled() || !g_state.arrived || !Racing())
        return 0;
    int n = 0;
    char a[16], b[16], c[16], d[16];
    if (g_state.menuOpen)
        n = MenuText(lines);
    else if (g_state.telemetry)
    {
        wsprintfA(lines[n++], "PROVING GROUND  %s   F1 MENU", nfs::tuning::CarName());
        Format(a, g_state.speed * 3.6f, 1);
        Format(b, g_state.speed / kMph, 1);
        wsprintfA(lines[n++], "SPEED %s KM/H  %s MPH", a, b);
        const int gear = g_state.gear - 1;
        wsprintfA(lines[n++], "RPM %d  GEAR %s%d  THR %d%%  BRK %d%%  STR %d%%", static_cast<int>(g_state.rpm),
                  gear < 0 ? "R" : (gear == 0 ? "N" : ""), gear > 0 ? gear : 0, static_cast<int>(g_state.gas * 100.0f),
                  static_cast<int>(g_state.brake * 100.0f), static_cast<int>(g_state.steering * 100.0f));
        Format(a, g_state.longG, 2);
        Format(b, g_state.latG, 2);
        wsprintfA(lines[n++], "G LONG %s  LAT %s", a, b);
        lstrcpyA(lines[n++], "-- STRAIGHT          LAST      BEST");
        const Run &last = g_state.stage == kRunning ? g_state.run : g_state.lastRun;
        for (int i = 0; i < kSpeedMarks; ++i)
        {
            FormatTime(a, last.speedTimes[i]);
            FormatTime(b, g_state.best.speedTimes[i]);
            wsprintfA(lines[n++], "%-16s %9s %9s", kSpeedMarkNames[i], a, b);
        }
        for (int i = 1; i < kDistanceMarks; ++i)
        {
            FormatTime(a, last.distanceTimes[i]);
            Format(c, last.distanceSpeeds[i] * 3.6f, 1);
            FormatTime(b, g_state.best.distanceTimes[i]);
            wsprintfA(lines[n++], "%-10s %8s @ %6s  BEST %s", kDistanceMarkNames[i], a,
                      last.distanceTimes[i] > 0.0f ? c : "-", b);
        }
        Format(a, last.topSpeed * 3.6f, 1);
        Format(b, g_state.best.topSpeed * 3.6f, 1);
        wsprintfA(lines[n++], "%-16s %9s %9s", "TOP KM/H", a, b);
        if (last.topSpeed > 0.0f)
        {
            Format(a, last.topDistance, 0);
            Format(c, last.topRpm, 0);
            wsprintfA(lines[n++], "  AT %s M, %s RPM, GEAR %d: %s", a, c, last.topGear,
                      g_state.stage == kRunning ? "RUNNING" : (last.revLimited ? "REV LIMITER" : "DRAG"));
        }
        FormatTime(a, g_state.lastRun.rolling);
        FormatTime(b, g_state.best.rolling);
        wsprintfA(lines[n++], "%-16s %9s %9s", "80-120 KM/H", a, b);
        Format(a, g_state.lastRun.brake100, 1);
        Format(b, g_state.best.brake100, 1);
        wsprintfA(lines[n++], "%-16s %9s %9s", "100-0 KM/H M", g_state.lastRun.brake100 > 0.0f ? a : "-",
                  g_state.best.brake100 > 0.0f ? b : "-");
        Format(a, g_state.lastRun.brake200, 1);
        Format(b, g_state.best.brake200, 1);
        wsprintfA(lines[n++], "%-16s %9s %9s", "200-0 KM/H M", g_state.lastRun.brake200 > 0.0f ? a : "-",
                  g_state.best.brake200 > 0.0f ? b : "-");
        const Lap &l = g_state.lap;
        const char *corner = g_state.road == kLoopRoad
                                 ? kLoop[g_state.roads[kLoopRoad].samples[g_state.sample].piece].name
                                 : "";
        wsprintfA(lines[n++], "-- LOOP %s", corner);
        FormatTime(a, l.started ? Sim::GetTime() - l.start : 0.0f);
        FormatTime(b, l.last);
        FormatTime(c, l.best);
        wsprintfA(lines[n++], "LAP %s  LAST %s  BEST %s", a, b, c);
        for (int k = 0; k < 3; ++k)
        {
            FormatTime(a, l.current[k]);
            FormatTime(d, l.bestSectors[k]);
            wsprintfA(lines[n++], "S%d %9s  BEST %9s", k + 1, a, d);
        }
        if (g_state.autoStage != kAutoOff && g_state.autoStage != kAutoDone)
            lstrcpyA(lines[n++], "HANDS-OFF RUN (NUM5 STOPS)");
        if (g_driver.active)
        {
            Format(a, g_driver.grip, 3);
            wsprintfA(lines[n++], "HANDLING TEST: %s AT %s G", g_driver.measuring ? "MEASURING" : "SEARCHING", a);
        }
        if (nfs::tuning::NeedsReload())
            lstrcpyA(lines[n++], "TUNING WAITS FOR A RELOAD (NUM/)");
        }
    else
        wsprintfA(lines[n++], "%s   F1 MENU", nfs::tuning::CarName());
    if (g_state.status[0] && GetTickCount() < g_state.statusUntil && n < kMaxLines)
        lstrcpynA(lines[n++], g_state.status, kLineLength);
    return n;
}
} // namespace nfs::proving
