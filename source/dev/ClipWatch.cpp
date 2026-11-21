/**
 * What the streamer and the world collision do around a spot while the player's car is there (dev/clipwatch.h). Not in
 * the original game.
 */
#include <math.h>
#include <windows.h>

#include "game/gen/api.h"
#include "game/platform/Platform.h"
#include "dev/clipwatch.h"
#include "gameplay/world/collision.h"
#include "core/config.h"
#include "core/log.h"
#include "dev/pose.h"

namespace nfs::clipwatch
{
namespace
{
const int kPlayerView = 1;
/** TheTrackStreamer's first streaming position (StreamingPositionEntry at +0xc0: x, y, z). */
const unsigned int kStreamingPosition = 0x9aa9d0 + 0xc0;
/** UserProfile's first PlayerSettings::CurCam: the camera setting (0 bumper, 1 hood, 2 hasWall, 3 far). */
const int kProfileCamera = 0x8c;
const DWORD kStatusPeriodMs = 100;
/** The packs near the spot are looked for again this often (a section that loads later brings its own). */
const DWORD kRescanPeriodMs = 2000;
const int kMaxWatched = 16;
/** Streamer events are logged while the car is within this many watch radii of the spot. */
const float kEventReach = 4.0f;
const int kOff = 2;

struct State
{
    int mode;
    float x, y, radius;
    bool inside;
    DWORD start;
    DWORD status;
    DWORD rescan;
    short packs[kMaxWatched];
    bool loaded[kMaxWatched];
    int packCount;
};

State g;

/** "-12.3" (wsprintf has no floats). */
void Tenths(char *out, float value)
{
    int tenths = static_cast<int>(value * 10.0f + (value < 0.0f ? -0.5f : 0.5f));
    const char *sign = tenths < 0 ? "-" : "";
    if (tenths < 0)
        tenths = -tenths;
    wsprintfA(out, "%s%d.%d", sign, tenths / 10, tenths % 10);
}

bool On()
{
    if (!g.mode)
    {
        g.mode = kOff;
        char text[64];
        nfs::config::String("Debug", "ClipWatch", "", text, sizeof(text));
        float v[3];
        int n = 0;
        for (const char *p = text; *p && n < 3;)
        {
            char number[24];
            int k = 0;
            while (*p && *p != ',' && k < 23)
                number[k++] = *p++;
            number[k] = 0;
            v[n++] = bStrToFloat(number);
            if (*p == ',')
                ++p;
        }
        if (n == 3)
        {
            g.mode = 1;
            g.x = v[0];
            g.y = v[1];
            g.radius = v[2];
        }
    }
    return g.mode == 1;
}

/** The car's, the camera's and the streamer's places, the look-back button and the camera setting, as one line. */
void Describe(char *line, int size, const float car[4])
{
    char cx[16], cy[16], cz[16], ch[16];
    Tenths(cx, car[0]);
    Tenths(cy, car[1]);
    Tenths(cz, car[2]);
    Tenths(ch, car[3]);
    char ex[16] = "-", ey[16] = "-", ez[16] = "-", ed[16] = "-";
    if (const Camera *camera = eViews[kPlayerView].pCamera)
    {
        const bVector3 &eye = camera->CurrentKey.Position;
        const float dx = eye.x - car[0], dy = eye.y - car[1], dz = eye.z - car[2];
        Tenths(ex, eye.x);
        Tenths(ey, eye.y);
        Tenths(ez, eye.z);
        Tenths(ed, sqrtf(dx * dx + dy * dy + dz * dz));
    }
    const float *stream = reinterpret_cast<const float *>(kStreamingPosition);
    char sx[16], sy[16];
    Tenths(sx, stream[0]);
    Tenths(sy, stream[1]);
    const UserProfile *profile = FEDatabase ? FEDatabase->CurrentUserProfile : nullptr;
    const int setting = profile ? *reinterpret_cast<const int *>(reinterpret_cast<const char *>(profile) + kProfileCamera)
                                : -1;
    int length = wsprintfA(line, "car %s %s %s h %s; camera %s %s %s (%s m); stream %s %s; look back %d; camera %d", cx,
                           cy, cz, ch, ex, ey, ez, ed, sx, sy, nfs::pose::LookingBack() ? 1 : 0, setting);
    for (int i = 0; i < g.packCount && length < size - 24; ++i)
        length += wsprintfA(line + length, "%s %d %s", i ? "," : "; packs", g.packs[i], g.loaded[i] ? "on" : "OFF");
}

/** The loaded collision packs with a wall within the watch radius of the spot, added to the watched ones. */
void FindPacks()
{
    using namespace nfs::collision;
    for (int k = 0; k < nfs::collision::kMaxPacks && g.packCount < kMaxWatched; ++k)
    {
        const Pack *pack = LoadedPack(k);
        if (!pack || !pack->instances)
            continue;
        bool known = false;
        for (int i = 0; i < g.packCount; ++i)
            known |= g.packs[i] == k;
        if (known)
            continue;
        bool hasWall = false;
        for (unsigned int i = 0; i < pack->count && !hasWall; ++i)
        {
            const Instance &c = pack->instances[i];
            if (c.flags & kLeans)
                continue;
            int count = 0;
            const float *walls = Walls(c, count);
            for (int w = 0; w < count && !hasWall; ++w)
                hasWall = WallNear(c, walls + w * kWallFloats, g.x, g.y, g.radius);
        }
        if (hasWall)
        {
            g.packs[g.packCount] = static_cast<short>(k);
            g.loaded[g.packCount++] = true;
        }
    }
}

DWORD Since()
{
    return GetTickCount() - g.start;
}
} // namespace

void Update()
{
    if (!On())
        return;
    float car[4];
    if (!nfs::pose::PlayerCar(car))
        return;
    const float dx = car[0] - g.x, dy = car[1] - g.y;
    const bool inside = dx * dx + dy * dy < g.radius * g.radius;
    char line[512];
    if (inside != g.inside)
    {
        g.inside = inside;
        if (inside)
        {
            if (!g.start)
                g.start = GetTickCount();
            FindPacks();
            g.rescan = GetTickCount();
        }
        Describe(line, sizeof(line), car);
        nfs::Log("clip: +%u ms car %s the watched spot; %s", Since(), inside ? "at" : "left", line);
    }
    if (!inside)
        return;
    for (int i = 0; i < g.packCount; ++i)
    {
        const bool loaded = nfs::collision::LoadedPack(g.packs[i]) != nullptr;
        if (loaded == g.loaded[i])
            continue;
        g.loaded[i] = loaded;
        Describe(line, sizeof(line), car);
        nfs::Log("clip: +%u ms collision pack %d %s; %s", Since(), g.packs[i], loaded ? "loaded" : "UNLOADED", line);
    }
    if (GetTickCount() - g.rescan >= kRescanPeriodMs)
    {
        g.rescan = GetTickCount();
        FindPacks();
    }
    if (GetTickCount() - g.status >= kStatusPeriodMs)
    {
        g.status = GetTickCount();
        Describe(line, sizeof(line), car);
        nfs::Log("clip: +%u ms %s", Since(), line);
    }
}

void Event(const char *what, int number, int other)
{
    if (!On() || !g.start)
        return;
    float car[4];
    if (!nfs::pose::PlayerCar(car))
        return;
    const float dx = car[0] - g.x, dy = car[1] - g.y, reach = g.radius * kEventReach;
    if (dx * dx + dy * dy > reach * reach)
        return;
    char line[512];
    Describe(line, sizeof(line), car);
    if (other >= 0)
        nfs::Log("clip: +%u ms %s %d %d; %s", Since(), what, number, other, line);
    else
        nfs::Log("clip: +%u ms %s %d; %s", Since(), what, number, line);
}
} // namespace nfs::clipwatch
