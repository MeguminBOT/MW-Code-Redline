/**
 * Controller rumble (docs/CONTROLS.md "Rumble"). Not in the PC game, which has the interface but not the effects.
 *
 * Every simulation step LocalPlayer::DoFFB (0x6ee490) works out what a force feedback device should feel and calls the
 * player's IFeedback, which on the PC is GameDevice's second interface (vtable 0x8a7bd4, made in GameDevice's
 * constructor 0x634b50). There all those methods are empty (only Stop resets the wheel manager's effects), so pads
 * never rumbled. This file gives the interface's vtable real methods and turns their arguments into the pad's two
 * motors (nfs::input::Rumble, SDL):
 *
 *  +0x04 Stop                       (also LocalPlayer::CanDoFFB failing: paused, a cutscene, the AI driving)
 *  +0x08 Begin, +0x10, +0x14 End    each step: Begin, then the wheels and the engine, then End
 *  +0x18 (front, surface, speed)    per wheel: front or rear, its road surface (SimSurface), the car's speed
 *  +0x1c (front, surface, b)        per wheel: ISuspension +0x38 of the wheel, magnitude
 *  +0x20 (front, surface, a)        per wheel: ISuspension +0x30 of the wheel, magnitude
 *  +0x24 (rpm, 0, gas)              the engine's revs between idle and the red line (0 to 1), the gas pedal
 *  +0x28 (x), +0x2c (x, y)          transmission and nitrous state (not used yet)
 *  +0x30 (blown)                    the engine is wrecked
 *  +0x34 (flag)                     (not used yet)
 *  +0x38 (Collision::Info, self)    LocalPlayer::OnCollision: the car hit something
 *
 * `-set Debug.RumbleLog=1` logs the arguments' ranges every two seconds (to tune what each does). `[Controls] Rumble`
 * is the strength in percent (0 off); -headless runs keep the motors still. What the arguments measured on the bench
 * drive: speed in metres a second; b 0-7 (2-3 in a fast bend); a 0-1.4; revs 0.76-0.93 on the highway; +0x2c's float
 * draining from 1 (nitrous); 30 steps a second. The surface is a copy on DoFFB's stack (the same pointer for every
 * surface): telling surfaces apart needs its record read (not done yet).
 */
#include <windows.h>
#include <math.h>
#include <string.h>

#include "core/config.h"
#include "engine/rendering/display/display.h"
#include "core/input.h"
#include "core/log.h"

namespace nfs::rumble
{
void Install();
}

namespace
{
/** GameDevice's IFeedback vtable in .rdata. */
void **const kFeedbackVtable = reinterpret_cast<void **>(0x8a7bd4);
const int kSlots = 16;

struct Range
{
    float low, high, sum;
    int count;
    void Add(float v)
    {
        if (!count || v < low)
            low = v;
        if (!count || v > high)
            high = v;
        sum += v;
        ++count;
    }
};

struct State
{
    bool logging;
    float strength;
    /** This step's inputs. */
    int wheels;
    float speed, slipA, slipB, rpm, gas;
    bool blown;
    /** The speed a step before (for knocks), and the knock's rumble, which dies away. */
    float lastSpeed, knock;
    bool hit;
    /** Surfaces met: their pointers, for telling a change of surface under the wheels. */
    const void *surface[4];
    int changes;
    DWORD stepAt;
    /** Logging. */
    DWORD loggedAt;
    Range ranges[kSlots];
    int calls[kSlots];
    int collisions;
    float collisionWords[8];
};

State g;

float Min(float a, float b)
{
    return a < b ? a : b;
}

float Max(float a, float b)
{
    return a > b ? a : b;
}

float Smooth(float from, float to, float x)
{
    const float t = Min(Max((x - from) / (to - from), 0.0f), 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float Arg(float v)
{
    return v == v ? v : 0.0f;
}

void Apply(float low, float high)
{
    nfs::input::Rumble(low * g.strength, high * g.strength, 200);
}

void __fastcall Stop(void *, void *)
{
    ++g.calls[1];
    g.knock = 0.0f;
    Apply(0.0f, 0.0f);
}

void __fastcall Begin(void *, void *)
{
    ++g.calls[2];
    g.wheels = 0;
    g.slipA = g.slipB = 0.0f;
}

void __fastcall Nothing(void *, void *)
{
    ++g.calls[4];
}

void LogRanges()
{
    static const char *const kNames[kSlots] = {"", "stop", "begin", "", "+10", "end", "speed", "b", "a", "rpm",
                                               "+28", "+2c", "blown", "+34", "hits", ""};
    char line[512];
    int n = wsprintfA(line, "rumble:");
    for (int i = 1; i < kSlots; ++i)
    {
        if (!g.calls[i])
            continue;
        const Range &r = g.ranges[i];
        if (r.count)
            n += wsprintfA(line + n, " %s x%d %d..%d (avg %d)", kNames[i], g.calls[i], static_cast<int>(r.low * 100),
                           static_cast<int>(r.high * 100), static_cast<int>(r.sum * 100 / r.count));
        else
            n += wsprintfA(line + n, " %s x%d", kNames[i], g.calls[i]);
    }
    nfs::Log("%s (x100; surface changes %d)", line, g.changes);
    if (g.collisions)
    {
        char words[256];
        int m = 0;
        for (int i = 0; i < 8; ++i)
            m += wsprintfA(words + m, " %d", static_cast<int>(g.collisionWords[i] * 100));
        nfs::Log("rumble: %d hits; the last one's floats at +0x00..+0x1c (x100):%s", g.collisions, words);
    }
    memset(g.ranges, 0, sizeof(g.ranges));
    memset(g.calls, 0, sizeof(g.calls));
    g.changes = 0;
    g.collisions = 0;
}

/** End of a step: the motors from what it said. */
void __fastcall End(void *, void *)
{
    ++g.calls[5];
    const DWORD now = GetTickCount();
    const float dt = g.stepAt && now > g.stepAt ? (now - g.stepAt) * 0.001f : 1.0f / 60.0f;
    g.stepAt = now;
    // A knock: the speed falling faster than braking can (metres a second lost in one step, at the game's 30 steps a
    // second; hard braking loses about 0.35), or a collision, dying away.
    const float lost = g.lastSpeed - g.speed;
    g.lastSpeed = g.speed;
    if (lost > 0.8f)
        g.knock = Min(1.0f, g.knock + (lost - 0.8f) * 0.15f);
    if (g.hit)
        g.knock = Min(1.0f, g.knock + 0.25f + 0.02f * g.speed);
    g.hit = false;
    g.knock *= expf(-dt * 7.0f);
    // The engine: a low hum with the revs under load, more near the red line.
    const float rev = g.blown ? 0.0f : g.rpm * (0.35f + 0.65f * g.gas);
    const float engine = 0.03f + 0.07f * rev + (g.rpm > 0.92f ? 0.08f * g.gas : 0.0f);
    // The tyres: b is the slip that cornering builds (2-3 m/s in a fast bend, 6 and more sliding), a the other
    // (wheelspin and locking: 0.2 to 1.4); felt in the light motor, more with speed.
    const float speedShare = Min(g.speed / 40.0f, 1.0f);
    const float slide = Min(Smooth(1.5f, 6.0f, g.slipB) + 0.6f * Smooth(0.2f, 1.2f, g.slipA), 1.0f) *
                        (0.3f + 0.7f * speedShare);
    const float low = engine + g.knock;
    const float high = 0.3f * slide + 0.6f * g.knock;
    Apply(low, high);
    if (g.logging && now - g.loggedAt >= 2000)
    {
        g.loggedAt = now;
        LogRanges();
    }
}

/** +0x18: per wheel, (front, surface, the car's speed). */
void __fastcall WheelSpeed(void *, void *, int front, const void *surface, float speed)
{
    ++g.calls[6];
    g.ranges[6].Add(speed);
    g.speed = fabsf(Arg(speed));
    const int w = g.wheels < 4 ? g.wheels : 3;
    if (g.surface[w] && g.surface[w] != surface)
        ++g.changes;
    g.surface[w] = surface;
    ++g.wheels;
    (void)front;
}

/** +0x1c: per wheel, (front, surface, b). */
void __fastcall WheelB(void *, void *, int, const void *, float b)
{
    ++g.calls[7];
    g.ranges[7].Add(b);
    g.slipB = Max(g.slipB, fabsf(Arg(b)));
}

/** +0x20: per wheel, (front, surface, a). */
void __fastcall WheelA(void *, void *, int, const void *, float a)
{
    ++g.calls[8];
    g.ranges[8].Add(a);
    g.slipA = Max(g.slipA, fabsf(Arg(a)));
}

/** +0x24: (revs 0..1, 0, gas). */
void __fastcall Engine(void *, void *, float rpm, int, float gas)
{
    ++g.calls[9];
    g.ranges[9].Add(rpm);
    g.rpm = Arg(rpm);
    g.gas = Arg(gas);
}

void __fastcall One28(void *, void *, int v)
{
    ++g.calls[10];
    g.ranges[10].Add(static_cast<float>(v & 0xff));
}

void __fastcall Two2c(void *, void *, int v, float f)
{
    ++g.calls[11];
    g.ranges[11].Add(Arg(f));
    (void)v;
}

void __fastcall Blown(void *, void *, int blown)
{
    ++g.calls[12];
    g.blown = (blown & 0xff) != 0;
    g.ranges[12].Add(g.blown ? 1.0f : 0.0f);
}

void __fastcall One34(void *, void *, int v)
{
    ++g.calls[13];
    g.ranges[13].Add(static_cast<float>(v & 0xff));
}

/** +0x38: LocalPlayer::OnCollision's (Sim::Collision::Info, whether the other object is the player's own). */
void __fastcall Collision(void *, void *, const float *info, int)
{
    ++g.calls[14];
    ++g.collisions;
    g.hit = true;
    if (info)
        memcpy(g.collisionWords, info, sizeof(g.collisionWords));
}
} // namespace

namespace nfs::rumble
{
/** The motors' strength from now on, in percent (the Controller Settings page); 0 stops them. */
void SetStrength(int percent)
{
    if (nfs::display::Headless())
        return;
    g.strength = (percent < 0 ? 0 : percent > 200 ? 200 : percent) / 100.0f;
    if (!g.strength)
        nfs::input::Rumble(0.0f, 0.0f, 0);
    // Turned on from Off: the methods go in now (Install left the vtable alone at 0%).
    Install();
}

/** Puts the methods above into GameDevice's IFeedback vtable (once). */
void Install()
{
    static bool done;
    if (done)
        return;
    // A headless test run never shakes the pad of whoever is using the computer.
    const int percent = nfs::display::Headless() ? 0 : nfs::config::Int("Controls", "Rumble", 100);
    g.strength = (percent < 0 ? 0 : percent > 200 ? 200 : percent) / 100.0f;
    g.logging = nfs::config::Int("Debug", "RumbleLog", 0) != 0;
    if (!percent && !g.logging)
        return;
    void *const methods[kSlots] = {kFeedbackVtable[0],
                                   reinterpret_cast<void *>(&Stop),
                                   reinterpret_cast<void *>(&Begin),
                                   kFeedbackVtable[3],
                                   reinterpret_cast<void *>(&Nothing),
                                   reinterpret_cast<void *>(&End),
                                   reinterpret_cast<void *>(&WheelSpeed),
                                   reinterpret_cast<void *>(&WheelB),
                                   reinterpret_cast<void *>(&WheelA),
                                   reinterpret_cast<void *>(&Engine),
                                   reinterpret_cast<void *>(&One28),
                                   reinterpret_cast<void *>(&Two2c),
                                   reinterpret_cast<void *>(&Blown),
                                   reinterpret_cast<void *>(&One34),
                                   reinterpret_cast<void *>(&Collision),
                                   kFeedbackVtable[15]};
    DWORD protection;
    if (!VirtualProtect(kFeedbackVtable, sizeof(methods), PAGE_READWRITE, &protection))
        return;
    done = true;
    memcpy(kFeedbackVtable, methods, sizeof(methods));
    VirtualProtect(kFeedbackVtable, sizeof(methods), protection, &protection);
    nfs::Log("rumble: on at %d%%", percent);
}
} // namespace nfs::rumble
