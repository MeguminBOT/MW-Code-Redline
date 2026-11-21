/**
 * The test track (dev/testtrack.h): a long, wide, flat strip far from Rockport for measuring the cars, and a run that
 * drives one down it. Not in the original game.
 *
 * Nothing of Rockport reaches the strip, so the strip is ground the collision code makes up: WWorldPos::Update (the
 * wheels' ground, 0x789cc0) and WCollisionMgr::GetWorldHeightAtPoint (placing the car, 0x7897c0) answer for points
 * over it with a flat face at its height, of the surface the car last stood on in Rockport (the debug race's start,
 * asphalt). The proving ground's site (dev/proving.h) gets the same ground. A WWorldPos face is three vertices (x, y, z, w) in the simulation's axes (Y up), then its flags (bit 0:
 * it holds a face), the search height and the surface.
 *
 * The run reads the controls through PInput::GetControls (0x69cbb0, the IInput the vehicle behaviours take them
 * from): it holds the brake while the car settles after the warp, then the throttle, and steers to stay on the
 * centre line. Speed is the distance along the strip over simulation time, sampled every simulation step.
 *
 * The strip is drawn around the player's car as world quads (the skid marks' kind, in the player view's world pass):
 * asphalt, edge lines, a dashed centre line and a line across every 100 m from the run's start, wider every
 * kilometre, with in-game textures every race loads.
 */
#include <math.h>
#include <windows.h>

#include "game/gen/api.h"
#include "core/config.h"
#include "dev/dev.h"
#include "legacy/link.h"
#include "core/log.h"
#include "gameplay/vehicles/performance.h"
#include "dev/pose.h"
#include "dev/proving.h"
#include "dev/testtrack.h"

extern "C" int TheGameFlowManager_CurrentGameFlowState;

NFS_REPLACE(0x69cbb0, TestInput::GetControls)

void RenderPolyTransformed(ePoly *poly, TextureInfo *texture, bMatrix4 *transform, int unused);

namespace
{
const int kGameFlowRacing = 6;

/** The strip in the renderer's axes (Z up): it starts kRunUp behind (kStartX, kCentreY) and runs kLength along +X. */
const float kStartX = 20000.0f;
const float kCentreY = 20000.0f;
const float kHeight = 0.0f;
const float kRunUp = 100.0f;
const float kLength = 15000.0f;
const float kHalfWidth = 40.0f;
/** How far under the strip a point still finds it (a wheel's probe starts above the wheel). */
const float kBelow = 20.0f;

const int kFaceFlags = 0x30;
const int kFaceSurface = 0x38;
const unsigned int kHasFace = 1;
const float kFaceSize = 200.0f;

/** InputControls through IInput (PInput::SetControlSteering, SetControlGas, SetControlBrake, SetControlHandBrake,
 * SetControlNOS). */
const int kSteering = 0x0C;
const int kGas = 0x1C;
const int kBrake = 0x20;
const int kHandBrake = 0x24;
const int kNos = 0x29;

/** How much of the strip is drawn behind and ahead of the car, and its markings. */
const float kDrawBehind = 200.0f;
const float kDrawAhead = 800.0f;
const float kAsphaltTile = 8.0f;
const float kDash = 3.0f;
const float kDashGap = 9.0f;
const float kLineWidth = 0.15f;
const float kEdgeInset = 1.0f;
const float kMarkSpacing = 100.0f;
const float kMarkWidth = 0.5f;
const float kKilometreWidth = 3.0f;
const int kMarksPerKilometre = 10;
/** The markings sit this far over the asphalt. */
const float kPaintHeight = 0.03f;
/** Vertex colours (0x80 leaves the texture as it is). */
const unsigned char kAsphaltShade = 0x50;
const unsigned char kPaintShade = 0xa0;

const float kSettleSeconds = 2.0f;
const float kFineReportSeconds = 12.0f;
/** With Debug.TestRunQuit the game quits this long after the run. */
const float kQuitSeconds = 2.0f;
const float kMph = 0.44704f;
const float kKmh = 1.0f / 3.6f;
const float kQuarterMile = 402.336f;
/** The run ends when the speed has not risen by kPlateau m/s (0.5 km/h) for kPlateauSeconds, after kMaxSeconds, or
 * near the strip's end. */
const float kPlateau = 0.5f / 3.6f;
const float kPlateauSeconds = 10.0f;
const float kMaxSeconds = 150.0f;
const float kEndMargin = 1500.0f;
/** Lane keeping: steering per metre off the centre line and per degree off the strip's direction. */
const float kSteerPerMetre = 0.04f;
const float kSteerPerDegree = 0.03f;
const float kSteerLimit = 0.25f;

enum Stage
{
    kWaiting,
    kSettling,
    kRunning,
    kStopping,
    kDone,
};

struct Milestone
{
    const char *name;
    float speed;
    float time;
};

struct State
{
    int enabled;
    int run;
    int quit;
    int launch;
    bool warp_requested;
    unsigned int surface;
    Stage stage;
    float stage_since;
    float last_time;
    float last_distance;
    float last_speed;
    float start_time;
    float top_speed;
    float top_speed_mark;
    float top_since;
    float quarter_time;
    float quarter_speed;
    float kilometre_time;
    float next_report;
    float steer;
    Milestone milestones[6];
};

State g_state = {-1, -1};

bool Enabled()
{
    if (g_state.enabled < 0)
    {
        g_state.enabled = nfs::config::Int("Debug", "TestTrack", 0) != 0;
        g_state.run = g_state.enabled && nfs::config::Int("Debug", "TestRun", 0) != 0;
        g_state.quit = g_state.run && nfs::config::Int("Debug", "TestRunQuit", 0) != 0;
        g_state.launch = g_state.run && nfs::config::Int("Debug", "TestRunLaunch", 0) != 0;
        const Milestone milestones[6] = {{"0-60 mph", 60.0f * kMph}, {"0-100 km/h", 100.0f * kKmh},
                                         {"0-100 mph", 100.0f * kMph}, {"0-200 km/h", 200.0f * kKmh},
                                         {"0-250 km/h", 250.0f * kKmh}, {"0-300 km/h", 300.0f * kKmh}};
        for (int i = 0; i < 6; ++i)
            g_state.milestones[i] = milestones[i];
    }
    return g_state.enabled != 0;
}

/** A point in the simulation's axes over the strip. */
bool OverStrip(const float *point)
{
    const float x = point[2];
    const float y = -point[0];
    return point[1] > kHeight - kBelow && x > kStartX - kRunUp && x < kStartX + kLength &&
           fabsf(y - kCentreY) < kHalfWidth;
}

/** "12.34" (wsprintf has no floats). */
void Hundredths(char *out, float value)
{
    int h = static_cast<int>(value * 100.0f + (value < 0.0f ? -0.5f : 0.5f));
    const char *sign = h < 0 ? "-" : "";
    if (h < 0)
        h = -h;
    wsprintfA(out, "%s%d.%02d", sign, h / 100, h % 100);
}

void Report(const char *what, float seconds, float speed)
{
    char t[16], kmh[16], mph[16];
    Hundredths(t, seconds);
    Hundredths(kmh, speed * 3.6f);
    Hundredths(mph, speed / kMph);
    nfs::Log("testtrack: %s %s s (%s km/h, %s mph)", what, t, kmh, mph);
}

void Finish(float now)
{
    char kmh[16], mph[16], seconds[16];
    Hundredths(kmh, g_state.top_speed * 3.6f);
    Hundredths(mph, g_state.top_speed / kMph);
    Hundredths(seconds, now - g_state.start_time);
    nfs::Log("testtrack: top speed %s km/h (%s mph) after %s s", kmh, mph, seconds);
    for (int i = 0; i < 6; ++i)
    {
        if (g_state.milestones[i].time > 0.0f)
            Report(g_state.milestones[i].name, g_state.milestones[i].time, g_state.milestones[i].speed);
        else
            nfs::Log("testtrack: %s not reached", g_state.milestones[i].name);
    }
    if (g_state.quarter_time > 0.0f)
        Report("quarter mile", g_state.quarter_time, g_state.quarter_speed);
    if (g_state.kilometre_time > 0.0f)
    {
        char t[16];
        Hundredths(t, g_state.kilometre_time);
        nfs::Log("testtrack: kilometre %s s", t);
    }
    nfs::Log("testtrack: run done");
    g_state.stage = kStopping;
    g_state.stage_since = now;
}

/** One simulation step of the run: the speed along the strip, the milestones, the end. */
void Sample(float now)
{
    float car[4];
    if (!nfs::pose::PlayerCar(car))
        return;
    const float distance = car[0] - kStartX;
    const float dt = now - g_state.last_time;
    const float speed = dt > 0.0f ? (distance - g_state.last_distance) / dt : g_state.last_speed;
    float heading = car[3];
    if (heading > 180.0f)
        heading -= 360.0f;
    const float steer = (car[1] - kCentreY) * kSteerPerMetre + heading * kSteerPerDegree;
    g_state.steer = steer > kSteerLimit ? kSteerLimit : (steer < -kSteerLimit ? -kSteerLimit : steer);

    if (g_state.stage == kSettling && now - g_state.stage_since >= kSettleSeconds)
    {
        g_state.stage = kRunning;
        g_state.start_time = now;
        g_state.top_speed = 0.0f;
        g_state.top_speed_mark = 0.0f;
        g_state.top_since = now;
        g_state.next_report = now + 0.25f;
        nfs::Log("testtrack: run started");
    }
    else if (g_state.stage == kRunning && dt > 0.0f)
    {
        const float t = now - g_state.start_time;
        for (int i = 0; i < 6; ++i)
        {
            Milestone &m = g_state.milestones[i];
            if (m.time <= 0.0f && speed >= m.speed)
            {
                const float f = speed > g_state.last_speed ? (m.speed - g_state.last_speed) / (speed - g_state.last_speed)
                                                           : 1.0f;
                m.time = t - dt + dt * f;
            }
        }
        if (g_state.quarter_time <= 0.0f && distance >= kQuarterMile)
        {
            const float f = (kQuarterMile - g_state.last_distance) / (distance - g_state.last_distance);
            g_state.quarter_time = t - dt + dt * f;
            g_state.quarter_speed = speed;
        }
        if (g_state.kilometre_time <= 0.0f && distance >= 1000.0f)
            g_state.kilometre_time = t - dt + dt * (1000.0f - g_state.last_distance) / (distance - g_state.last_distance);
        if (speed > g_state.top_speed)
        {
            if (speed > g_state.top_speed_mark + kPlateau)
            {
                g_state.top_speed_mark = speed;
                g_state.top_since = now;
            }
            g_state.top_speed = speed;
        }
        if (now >= g_state.next_report)
        {
            char kmh[16], off[16], head[16], st[16], at[16];
            Hundredths(at, t);
            Hundredths(kmh, speed * 3.6f);
            Hundredths(off, car[1] - kCentreY);
            Hundredths(head, heading);
            Hundredths(st, g_state.steer);
            nfs::Log("testtrack: t %s s, %d m, %s km/h, off centre %s m, heading %s, steer %s", at,
                     static_cast<int>(distance), kmh, off, head, st);
            // Every quarter second during the launch, then every second.
            g_state.next_report += t < kFineReportSeconds ? 0.25f : 1.0f;
        }
        const bool launched = g_state.launch && g_state.quarter_time > 0.0f && g_state.milestones[2].time > 0.0f;
        if ((now - g_state.top_since > kPlateauSeconds && t > 20.0f) || t > kMaxSeconds ||
            distance > kLength - kEndMargin || launched)
            Finish(now);
    }
    g_state.last_time = now;
    g_state.last_distance = distance;
    g_state.last_speed = speed;
}
TextureInfo *LoadedTexture(const char *name)
{
    TextureInfo *info = GetTextureInfo(bStringHash(name), 1, 0);
    return info && info != DefaultTextureInfo && info->PlatInfo ? info : nullptr;
}

/** A quad of the strip from (x0, y0) to (x1, y1) at height z: the texture repeats every `tile` metres, or stretches
 * over the quad (tile 0). */
void Quad(TextureInfo *texture, float x0, float x1, float y0, float y1, float z, float tile, unsigned char shade)
{
    ePoly poly;
    const float corners[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    for (int i = 0; i < 4; ++i)
    {
        poly.Vertices[i][0] = corners[i][0];
        poly.Vertices[i][1] = corners[i][1];
        poly.Vertices[i][2] = z;
        poly.Vertices[i][3] = 1.0f;
        poly.UVs[i][0] = tile > 0.0f ? (corners[i][0] - kStartX) / tile : (i == 1 || i == 2 ? 1.0f : 0.0f);
        poly.UVs[i][1] = tile > 0.0f ? (corners[i][1] - kCentreY) / tile : (i >= 2 ? 1.0f : 0.0f);
        poly.Colours[i][0] = shade;
        poly.Colours[i][1] = shade;
        poly.Colours[i][2] = shade;
        poly.Colours[i][3] = 0xff;
    }
    RenderPolyTransformed(&poly, texture, nullptr, 0);
}
} // namespace

namespace nfs::testtrack
{
bool Enabled()
{
    return ::Enabled();
}

void Update()
{
    static int perf_log = -1;
    if (perf_log < 0)
        perf_log = nfs::config::Int("Debug", "PerfLog", 0);
    if (perf_log > 0 && TheGameFlowManager_CurrentGameFlowState == kGameFlowRacing)
    {
        perf_log = 0;
        nfs::performance::LogAll();
    }
    if (!::Enabled())
        return;
    if (TheGameFlowManager_CurrentGameFlowState != kGameFlowRacing)
        return;
    if (!g_state.warp_requested)
    {
        g_state.warp_requested = true;
        nfs::pose::RequestWarp(kStartX, kCentreY, kHeight + 0.5f, 0.0f);
        nfs::Log("testtrack: the car goes to the strip");
    }
    if (g_state.quit && g_state.stage == kStopping && Sim::GetTime() - g_state.stage_since > kQuitSeconds)
        ExitTheGameFlag = 1;
    if (g_state.run && g_state.stage == kWaiting && nfs::pose::WarpFinished() && !nfs::proving::CarTunePending())
    {
        g_state.stage = kSettling;
        g_state.stage_since = Sim::GetTime();
        g_state.last_time = g_state.stage_since;
        float car[4];
        g_state.last_distance = nfs::pose::PlayerCar(car) ? car[0] - kStartX : 0.0f;
    }
}
void Render()
{
    if (!::Enabled() || TheGameFlowManager_CurrentGameFlowState != kGameFlowRacing)
        return;
    float car[4];
    if (!nfs::pose::PlayerCar(car) || fabsf(car[1] - kCentreY) > kDrawAhead)
        return;
    const float begin = kStartX - kRunUp, end = kStartX + kLength;
    const float from = car[0] - kDrawBehind > begin ? car[0] - kDrawBehind : begin;
    const float to = car[0] + kDrawAhead < end ? car[0] + kDrawAhead : end;
    TextureInfo *asphalt = LoadedTexture("GRAY_MID_A");
    TextureInfo *paint = LoadedTexture("WHITE_LINE");
    if (from >= to || !asphalt || !paint)
        return;
    Quad(asphalt, from, to, kCentreY - kHalfWidth, kCentreY + kHalfWidth, kHeight, kAsphaltTile, kAsphaltShade);
    const float z = kHeight + kPaintHeight;
    const float edge = kHalfWidth - kEdgeInset;
    Quad(paint, from, to, kCentreY - edge - kLineWidth, kCentreY - edge, z, 0.0f, kPaintShade);
    Quad(paint, from, to, kCentreY + edge, kCentreY + edge + kLineWidth, z, 0.0f, kPaintShade);
    const float period = kDash + kDashGap;
    for (float x = begin + floorf((from - begin) / period) * period; x < to; x += period)
        Quad(paint, x, x + kDash, kCentreY - kLineWidth * 0.5f, kCentreY + kLineWidth * 0.5f, z, 0.0f, kPaintShade);
    for (int mark = static_cast<int>(ceilf((from - kStartX) / kMarkSpacing));; ++mark)
    {
        const float x = kStartX + mark * kMarkSpacing;
        if (x >= to)
            break;
        const float width = mark % kMarksPerKilometre == 0 ? kKilometreWidth : kMarkWidth;
        Quad(paint, x, x + width, kCentreY - edge, kCentreY + edge, z, 0.0f, kPaintShade);
    }
}
} // namespace nfs::testtrack

bool WWorldPos::Update(UMath::Vector3 const &point, UMath::Vector4 &plane, bool a, WCollider const *collider, bool b)
{
    typedef bool(__thiscall * Original)(WWorldPos *, UMath::Vector3 const &, UMath::Vector4 &, bool, WCollider const *,
                                        bool);
    const float *p = reinterpret_cast<const float *>(&point);
    if (!(Enabled() && OverStrip(p)) && !nfs::proving::OverGround(p))
    {
        const bool found = NFS_ORIGINAL(0x789cc0, Original)(this, point, plane, a, collider, b);
        if (found && (g_state.enabled || nfs::proving::Enabled()) && !g_state.surface &&
            (*reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(this) + kFaceFlags) & kHasFace))
            g_state.surface = *reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(this) + kFaceSurface);
        return found;
    }
    // A face under the point whose normal (WWorldPos::UNormal) points up.
    float *face = reinterpret_cast<float *>(this);
    const float x = p[0] - kFaceSize * 0.25f;
    const float z = p[2] - kFaceSize * 0.25f;
    const float v[12] = {x, kHeight, z, 0.0f, x + kFaceSize, kHeight, z, 0.0f, x, kHeight, z + kFaceSize, 0.0f};
    for (int i = 0; i < 12; ++i)
        face[i] = v[i];
    *reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(this) + kFaceFlags) |= kHasFace;
    if (g_state.surface)
        *reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(this) + kFaceSurface) = g_state.surface;
    float *out = reinterpret_cast<float *>(&plane);
    out[0] = 0.0f;
    out[1] = 1.0f;
    out[2] = 0.0f;
    out[3] = kHeight - p[1];
    return true;
}

bool WCollisionMgr::GetWorldHeightAtPoint(UMath::Vector3 const &point, float &height, UMath::Vector3 *normal)
{
    typedef bool(__thiscall * Original)(WCollisionMgr *, UMath::Vector3 const &, float &, UMath::Vector3 *);
    const float *p = reinterpret_cast<const float *>(&point);
    if (!(Enabled() && OverStrip(p)) && !nfs::proving::OverGround(p))
        return NFS_ORIGINAL(0x7897c0, Original)(this, point, height, normal);
    height = kHeight;
    if (normal)
    {
        float *n = reinterpret_cast<float *>(normal);
        n[0] = 0.0f;
        n[1] = 1.0f;
        n[2] = 0.0f;
    }
    return true;
}

/** IInput (PInput's, 0x69cbb0): the controls the vehicle behaviours read. */
struct TestInput
{
    unsigned char *GetControls();
};

unsigned char *TestInput::GetControls()
{
    unsigned char *self = reinterpret_cast<unsigned char *>(this);
    // The dev tools' path recording and playback (dev/dev.h).
    nfs::dev::Controls(this, self + 8);
    nfs::pose::Controls(this, self + 8);
    nfs::proving::Controls(this, self + 8);
    // -set Debug.AutoNitrous=1 (tests): from 300 km/h on, nitrous held down as a driver going for top speed on a motorway
    // would (not off the line); let go under 290 km/h, or on the brakes or off the throttle.
    static const bool autoNitrous = nfs::config::Int("Debug", "AutoNitrous", 0) != 0;
    if (autoNitrous)
    {
        static float lastTime = -1.0f, lastPlace[2], speed;
        static bool boosting;
        float car[4];
        const float now = Sim::GetTime();
        if (now != lastTime && nfs::pose::PlayerCar(car))
        {
            const float dt = now - lastTime;
            if (lastTime >= 0.0f && dt > 0.0f && dt < 0.5f)
            {
                const float dx = car[0] - lastPlace[0], dy = car[1] - lastPlace[1];
                speed = sqrtf(dx * dx + dy * dy) / dt;
            }
            lastTime = now;
            lastPlace[0] = car[0];
            lastPlace[1] = car[1];
        }
        const float kmh = speed * 3.6f;
        boosting = kmh >= 300.0f || (boosting && kmh >= 290.0f);
        unsigned char *controls = self + 8;
        const float steering = *reinterpret_cast<const float *>(controls + 0x04);
        const float gas = *reinterpret_cast<const float *>(controls + 0x14);
        const float brake = *reinterpret_cast<const float *>(controls + 0x18);
        // Held down, not tapped: once on it stays on until the speed falls under 290 km/h or the brakes go on.
        if (brake > 0.1f || gas < 0.3f)
            boosting = false;
        (void)steering;
        if (boosting)
            controls[0x21] = 1;
    }
    if (g_state.run > 0 && g_state.stage != kWaiting)
    {
        const float now = Sim::GetTime();
        if (now != g_state.last_time)
            Sample(now);
        const bool driving = g_state.stage == kRunning;
        *reinterpret_cast<float *>(self + kGas) = driving ? 1.0f : 0.0f;
        *reinterpret_cast<float *>(self + kBrake) = driving ? 0.0f : 1.0f;
        *reinterpret_cast<float *>(self + kHandBrake) = 0.0f;
        *reinterpret_cast<float *>(self + kSteering) = g_state.steer;
        self[kNos] = 0;
    }
    return self + 8;
}
