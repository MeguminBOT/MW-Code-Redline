/**
 * Shadow debugging while playing (engine/rendering/shadows/shadowdebug.h).
 *
 * The panel (F10, drawn by the performance overlay under the position box) shows the frame number, the sun and the
 * direction shadows fall (also relative to the view), each cascade's far end, radius, texel size, frames since it was
 * drawn and why, its scenery casters and those that appeared or vanished inside it at its last drawing, and what the
 * player view's culling fades, takes into its visibility list and streams. While it is on, nfs.log gets a line for
 * each such event, prefixed with the frame number, and a summary every second.
 */
#include <math.h>
#include <string.h>

#include "game/gen/api.h"
#include "game/platform/Platform.h"
#include "core/config.h"
#include "engine/rendering/display/display.h"
#include "core/log.h"
#include "engine/rendering/shadows/shadow.h"
#include "engine/rendering/shadows/shadowdebug.h"

namespace nfs::shadow::debug
{
namespace
{
const DWORD kSummaryPeriod = 1000;
/** At most this many event lines a second, so the log stays readable. */
const int kEventsPerSecond = 40;

struct CascadeState
{
    float split;
    float radius;
    float texel;
    unsigned int drawnFrame;
    Redraw reason;
    int casters;
    int drawing;
    int appeared;
    int vanished;
    int streamedIn;
    int streamedOut;
};

struct State
{
    bool loaded;
    bool enabled;
    bool tint;
    bool panelKey;
    bool tintKey;
    unsigned int frame;
    float sunAzimuth;
    float sunElevation;
    float shadowAzimuth;
    float relative;
    float loggedSun[3];
    CascadeState cascades[kCascades];
    int current;
    int forced;
    /** Player view this frame and over the last second. */
    int fadesIn, fadesOut;
    int listIn, listOut, streamIn, streamOut;
    int secondListIn, secondListOut, secondStreamIn, secondStreamOut;
    /** The last full second's list and streaming counts, for the panel. */
    int shownListIn, shownListOut, shownStreamIn, shownStreamOut;
    DWORD second;
    int events;
};

State g_state = {};

const float kDegreesPerAngle = 360.0f / 65536.0f;

/** Compass angle of (x, y) in degrees, 0 to 360 (the game's bATan: 0 along x, 90 along y). */
float Azimuth(float x, float y)
{
    return static_cast<float>(bATan(x, y)) * kDegreesPerAngle;
}

bool Event()
{
    if (!g_state.enabled || g_state.events >= kEventsPerSecond)
        return false;
    ++g_state.events;
    return true;
}

const char *ReasonName(Redraw r)
{
    return r == kScheduled         ? "scheduled"
           : r == kViewLeft        ? "view left it"
           : r == kJump            ? "jump or sun turned"
           : r == kSceneryChanged ? "scenery changed"
           : r == kSunMoved        ? "sun moved"
                                   : "kept";
}

/** "1.7" for 0.017 m in centimetres (wsprintf has no floats). */
void Centimetres(char *out, float metres)
{
    const int tenths = static_cast<int>(metres * 1000.0f + 0.5f);
    wsprintfA(out, "%d.%d", tenths / 10, tenths % 10);
}
} // namespace

void Poll()
{
    if (!g_state.loaded)
    {
        g_state.loaded = true;
        g_state.enabled = nfs::config::Int("Debug", "ShadowHud", 0) != 0;
        g_state.tint = nfs::config::Int("Debug", "ShadowTint", 0) != 0;
    }
    const bool panel = nfs::display::DebugKeyDown(VK_F10);
    if (panel && !g_state.panelKey)
    {
        g_state.enabled = !g_state.enabled;
        nfs::Log("shadowdbg f=%u: panel and log %s", eFrameCounter, g_state.enabled ? "on" : "off");
    }
    g_state.panelKey = panel;
    const bool tint = nfs::display::DebugKeyDown(VK_F8);
    if (tint && !g_state.tintKey)
    {
        g_state.tint = !g_state.tint;
        nfs::Log("shadowdbg f=%u: cascade tint %s", eFrameCounter, g_state.tint ? "on" : "off");
    }
    g_state.tintKey = tint;
}

bool Enabled()
{
    return g_state.enabled;
}

float ViewMode()
{
    return g_state.tint ? 2.0f : 0.0f;
}

void NoteFrame(const float sun[3], const float cameraDirection[3])
{
    g_state.frame = eFrameCounter;
    g_state.sunAzimuth = Azimuth(sun[0], sun[1]);
    g_state.sunElevation =
        static_cast<float>(static_cast<short>(bASin(sun[2] > 1.0f ? 1.0f : sun[2] < -1.0f ? -1.0f : sun[2]))) *
        kDegreesPerAngle;
    g_state.shadowAzimuth = Azimuth(-sun[0], -sun[1]);
    float relative = g_state.shadowAzimuth - Azimuth(cameraDirection[0], cameraDirection[1]);
    while (relative > 180.0f)
        relative -= 360.0f;
    while (relative <= -180.0f)
        relative += 360.0f;
    g_state.relative = relative;
    g_state.fadesIn = g_state.fadesOut = 0;
    if (!g_state.enabled)
        return;
    const float dx = sun[0] - g_state.loggedSun[0], dy = sun[1] - g_state.loggedSun[1], dz = sun[2] - g_state.loggedSun[2];
    if (dx * dx + dy * dy + dz * dz > 0.0001f)
    {
        memcpy(g_state.loggedSun, sun, sizeof(g_state.loggedSun));
        nfs::Log("shadowdbg f=%u: sun azimuth %d elevation %d, shadows fall towards azimuth %d", g_state.frame,
                 static_cast<int>(g_state.sunAzimuth), static_cast<int>(g_state.sunElevation),
                 static_cast<int>(g_state.shadowAzimuth));
    }
    const DWORD now = GetTickCount();
    if (now - g_state.second >= kSummaryPeriod)
    {
        g_state.second = now;
        g_state.events = 0;
        char line[512];
        int n = wsprintfA(line, "shadowdbg f=%u: shadows fall %d deg %s of the view;", g_state.frame,
                          static_cast<int>(fabsf(relative)), relative >= 0.0f ? "left" : "right");
        for (int i = 0; i < kCascades; ++i)
        {
            const CascadeState &c = g_state.cascades[i];
            n += wsprintfA(line + n, " c%d r%d age %u casters %d (+%d -%d, streamed +%d -%d);", i,
                           static_cast<int>(c.radius), g_state.frame - c.drawnFrame, c.casters, c.appeared,
                           c.vanished, c.streamedIn, c.streamedOut);
        }
        wsprintfA(line + n, " last second: list +%d -%d, streamed +%d -%d", g_state.secondListIn,
                  g_state.secondListOut, g_state.secondStreamIn, g_state.secondStreamOut);
        nfs::Log("%s", line);
        g_state.shownListIn = g_state.secondListIn;
        g_state.shownListOut = g_state.secondListOut;
        g_state.shownStreamIn = g_state.secondStreamIn;
        g_state.shownStreamOut = g_state.secondStreamOut;
        g_state.secondListIn = g_state.secondListOut = g_state.secondStreamIn = g_state.secondStreamOut = 0;
    }
}

void NoteCascade(int index, Redraw redraw, float split, float radius, float texel)
{
    if (index < 0 || index >= kCascades)
        return;
    CascadeState &c = g_state.cascades[index];
    c.split = split;
    if (redraw == kNotRedrawn)
        return;
    c.radius = radius;
    c.texel = texel;
    c.drawnFrame = eFrameCounter;
    c.reason = redraw;
    c.casters = c.drawing;
    c.drawing = 0;
    if ((redraw == kViewLeft || redraw == kJump) && Event())
        nfs::Log("shadowdbg f=%u: cascade %d drawn out of turn (%s), radius %d m", eFrameCounter, index,
                 ReasonName(redraw), static_cast<int>(radius));
}

void NoteCaster()
{
    if (g_state.current >= 0 && g_state.current < kCascades)
        ++g_state.cascades[g_state.current].drawing;
}

void NoteCasterChanges(int cascade, int appeared, int vanished, int streamedIn, int streamedOut)
{
    if (cascade < 0 || cascade >= kCascades)
        return;
    CascadeState &c = g_state.cascades[cascade];
    c.appeared = appeared;
    c.vanished = vanished;
    c.streamedIn = streamedIn;
    c.streamedOut = streamedOut;
}

void NoteCasterEvent(int cascade, bool appeared, const char *name, int section, const float centre[3], int metres,
                     const char *cause)
{
    if (!Event())
        return;
    nfs::Log("shadowdbg f=%u: cascade %d caster %s %s: %s (section %d) at %d %d %d, %d m from the camera",
             eFrameCounter, cascade, appeared ? "appeared" : "vanished", cause, name ? name : "?", section,
             static_cast<int>(centre[0]), static_cast<int>(centre[1]), static_cast<int>(centre[2]), metres);
}

void NoteFade(bool in)
{
    ++(in ? g_state.fadesIn : g_state.fadesOut);
}

void NoteList(int section, bool entered)
{
    ++(entered ? g_state.listIn : g_state.listOut);
    ++(entered ? g_state.secondListIn : g_state.secondListOut);
    if (Event())
        nfs::Log("shadowdbg f=%u: section %d %s the player view's list", eFrameCounter, section,
                 entered ? "entered" : "left");
}

void NoteStreaming(int section, bool loaded)
{
    ++(loaded ? g_state.streamIn : g_state.streamOut);
    ++(loaded ? g_state.secondStreamIn : g_state.secondStreamOut);
    if (Event())
        nfs::Log("shadowdbg f=%u: section %d %s", eFrameCounter, section, loaded ? "loaded" : "unloaded");
}

int Text(char (*lines)[kLineLength])
{
    if (!g_state.enabled)
        return 0;
    int n = 0;
    wsprintfA(lines[n++], "SHADOWS  FRAME %u  F10 PANEL  F8 TINT %s%s", g_state.frame, g_state.tint ? "ON" : "OFF",
              g_state.forced == 1 ? "  FORCED C1" : g_state.forced == 2 ? "  FORCED C2" : g_state.forced == 3 ? "  FORCED C3" : "");
    wsprintfA(lines[n++], "SUN AZ %d EL %d  SHADOWS FALL AZ %d", static_cast<int>(g_state.sunAzimuth),
              static_cast<int>(g_state.sunElevation), static_cast<int>(g_state.shadowAzimuth));
    const float r = g_state.relative;
    const int degrees = static_cast<int>(fabsf(r));
    wsprintfA(lines[n++], "SHADOWS FALL %d DEG %s OF VIEW (%s)", degrees, r >= 0.0f ? "LEFT" : "RIGHT",
              degrees < 60 ? "AWAY" : degrees > 120 ? "TOWARD CAMERA" : "SIDEWAYS");
    wsprintfA(lines[n++], "CASC  FAR  RADIUS TEXEL  AGE  CASTERS  NEW GONE STRM");
    for (int i = 0; i < kCascades; ++i)
    {
        const CascadeState &c = g_state.cascades[i];
        char texel[16];
        Centimetres(texel, c.texel);
        wsprintfA(lines[n++], "C%d   %4dM %5dM %4sCM %4u  %7d  %3d %4d %d/%d", i, static_cast<int>(c.split),
                  static_cast<int>(c.radius), texel, g_state.frame - c.drawnFrame, c.casters, c.appeared,
                  c.vanished, c.streamedIn, c.streamedOut);
    }
    wsprintfA(lines[n++], "VIEW FADING IN %d OUT %d", g_state.fadesIn, g_state.fadesOut);
    wsprintfA(lines[n++], "LAST SEC: LIST +%d -%d  STREAMED IN %d OUT %d", g_state.shownListIn, g_state.shownListOut,
              g_state.shownStreamIn, g_state.shownStreamOut);
    return n;
}

void SetCurrentCascade(int index)
{
    g_state.current = index;
}

void NoteForcedCascade(int cascade)
{
    g_state.forced = cascade;
}

int ForcedCascade()
{
    return g_state.forced;
}
} // namespace nfs::shadow::debug
