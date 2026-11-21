/**
 * The world's lamps at night (engine/rendering/lights/lamps.h, docs/NIGHT.md): GLOBAL\LAMPS.TXT (tools/lamps.py) holds every lamp head
 * of the track with its kind; the lamps' light is baked into two volumes around the camera that the rebuild's own
 * shaders read (lighting.hlsli LampLight), and their glows are light flares.
 *
 * A volume holds, for each cell corner, the lamps' light as on a surface facing it (rgb) and its direction weighted by
 * brightness (xyz): the shaders take N.L of the direction where one lamp dominates. Each lamp falls off with the square
 * of the distance and to nothing at its radius, inside its cone, as the local lights do (engine/rendering/lights/lights.h). A level is a
 * ring: cell (I, J, K) lives at texel (I, J, K) modulo the size, so as the camera moves only the slab of cells it
 * reaches is baked and uploaded. After a jump a level is baked anew, and shows once it is whole.
 *
 * The baking runs on a thread of its own: the frame queues the boxes of cells the camera reaches and uploads the boxes
 * the thread has finished, in order; each box says the window its level has once it is in, so the shaders never read
 * a cell before it is baked.
 *
 * The volumes' alpha holds the tunnels: LAMPS.TXT's roofs (the height of a tunnel's roof over points 2 m apart) give
 * each column of cells whether a roof is over it (the light's alpha) and the roof's height (the direction's), which the
 * shaders turn into how far a tunnel covers a pixel (lighting.hlsli RoofCover): the light baked under a roof stays as
 * at the game's own hour, since a tunnel's lamps light it day and night.
 */
#include <math.h>
#include <string.h>
#include <windows.h>

#include "game/gen/api.h"
#include "core/config.h"
#include "engine/rendering/lights/flares.h"
#include "engine/rendering/lights/lamps.h"
#include "engine/rendering/lights/lights.h"
#include "core/log.h"
#include "gameplay/world/timeofday.h"
#include "engine/rendering/vulkan/renderer.h"
#include "engine/resources/pack.h"

namespace nfs::lamps
{
namespace
{
const char kPath[] = "GLOBAL\\LAMPS.TXT";
const int kMaxKinds = 16;
const int kMaxLamps = 8192;
/** The kinds' radii are for a lamp this high (tools/lamps.py USUAL_HEIGHT). A higher one shining down reaches further
 * and lights the ground under it as strongly. */
const float kUsualHeight = 9.0f;
const float kDownward = -0.7f;
/** The furthest a high lamp reaches (m). A mast's 41 m head at its full height's reach, 111 m, lit whole hillsides and buildings far
 * from any road, through whatever stood between them, with no shadows to stop it. */
const float kMostLampRadius = 70.0f;

/** The volumes: cells per level (lighting.hlsli kLampCells) and their size in metres. */
const int kLevels = 2;
const int kSize[3] = {128, 128, 32};
const float kCell[kLevels] = {2.0f, 8.0f};
const int kTexels = 128 * 128 * 32;
/** D3DFMT_A16B16G16R16F. */
const unsigned int kHalfFormat = 113;
/** The brightness the directions are weighted with (lighting.hlsli kLampLuminance). */
const float kLuminance[3] = {0.3f, 0.59f, 0.11f};
/** The most cells in one box the thread bakes: four layers of a level. */
const int kMaxBox = 128 * 128 * 4;
/** Boxes queued at most (a level drawn anew takes up to 32). */
const int kMaxJobs = 128;
/** A camera move of more than this share of a level's size bakes it anew rather than slab by slab. */
const int kMoveFraction = 4;
/** A level's window follows the camera in steps of this many cells (x, y, z): fewer, larger slabs. */
const int kStep[kLevels][3] = {{4, 4, 2}, {2, 2, 1}};

/** Glows: lamps closer than this (metres) get one, a light flare of their kind's type; the flare renderer (0x505380)
 * draws none further (it fades them out from 179 m). */
const float kGlowRange = 200.0f;
const unsigned char kDefaultGlow = 13;
/** A kind's glow type for no glow, in a flare or in the air: a sign's floodlight is seen as the sign it lights. */
const unsigned char kNoGlow = 255;
const unsigned char kFlareOmni = 2;
/** A flare's reflection height (+0x1c) is where its reflection in a wet road is drawn (the head mirrored in the ground,
 * 2 ground - z); 10000 means none. The renderer asks the collision for the ground of a flare whose height is 999, each
 * time it draws it. */
const float kNoReflection = 10000.0f;
/** Heads of a mast: within this fraction of the first's height (at least kClusterMin metres) across, and within
 * kClusterHeight of its height. */
const float kClusterReach = 0.18f;
const float kClusterMin = 1.5f;
const float kClusterHeight = 1.5f;
const unsigned int kExcludeNoView = 1u;
/** A knocked-over lamp post: its prop starts where the post's scenery instance stood (within this many metres: the
 * prop is placed at the instance's own origin, so a bin or a sign knocked over next to a post is not the post). Its
 * lamp stays out until the camera is further than kRelightRange (the section is gone, and the post stands again when
 * it comes back). */
const float kKnockReach = 0.5f;
const float kRelightRange = 700.0f;
const int kMaxKnocked = 64;
/** The lamps' grid, for finding those near a place. */
const float kGridCell = 64.0f;
/** The tunnels' roofs: points this far apart (tools/lamps.py ROOF_CELL, the near level's cells), in an open hash of
 * 2^kRoofBits slots (at most half of them used); kNoRoof marks a free slot. */
const float kRoofCell = 2.0f;
const int kRoofBits = 19;
const unsigned int kRoofSlots = 1u << kRoofBits;
const unsigned int kNoRoof = 0xffffffffu;
/** Under a roof from the tunnel's floor, this far down, up to the roof's own surface (lighting.hlsli kTunnelDepth,
 * kRoofMargin). */
const float kTunnelDepth = 15.0f;
const float kRoofMargin = 1.0f;
/** Roofs of a point closer than this (m) are one. */
const float kRoofSame = 3.0f;
/** The haze's glow: the lamps within this many metres; the extinction of a clear night's light haze (about a kilometre
 * of sight: the lamps glow a little, as in a very light fog) and the densest the glow follows (in thicker fog the light
 * is lost on the way); and how much forward the haze scatters (the Henyey-Greenstein g of fine droplets, less than a
 * cloud's 0.85: the light scattered again spreads). */
const float kHazeRange = 160.0f;
const float kClearHaze = 0.004f;
const float kDensestGlow = 0.008f;
const float kHazeForward = 0.5f;

struct Kind
{
    char name[16];
    float colour[3];
    float intensity;
    float radius;
    float outer, inner;
    unsigned char glow;
};

struct Lamp
{
    float position[3];
    /** Spot lamps: the direction they shine. */
    float direction[3];
    /** Colour times intensity, and its brightness. */
    float colour[3];
    float brightness;
    float radius;
    /** 1 / distance^2 scale inside the radius (LocalLights.cpp: 4 / radius^2). */
    float falloff;
    /** The glow in the air's: the kind's own (a high mast reaches further, but its air glows as a street lamp's). */
    float hazeFalloff;
    float outer, inner;
    bool spot;
    /** The glow's flare type, and its reflection height. */
    unsigned char glow;
    float reflection;
    /** Where its scenery instance stands (LAMPS.TXT's base), for a lamp post that can be knocked over. */
    float base[3];
    bool hasBase;
    /** Knocked over: no light, no glow (the baking thread reads it). */
    volatile bool out;
};

struct Level
{
    nfs::vk::Texture *light;
    nfs::vk::Texture *direction;
    /** The window the texels hold (its first cell, world cells), from the boxes uploaded, and whether it is whole. */
    int shown[3];
    bool ready;
    /** The window the queued boxes lead to. */
    int wanted[3];
    bool queued;
};

enum JobState
{
    kQueued,
    kBaking,
    kBaked,
    kCancelled
};

/** A box of a level's cells [from, to) (world cells, inside one turn of the ring on each axis), its texels once baked
 * (light, then direction: four halves a cell each), and the level's window once they are in. */
struct Job
{
    int level;
    int from[3], to[3];
    int origin[3];
    /** The level holds its whole window once this box is in (false inside a level drawn anew). */
    bool whole;
    unsigned short *texels;
    /** JobState, under Queue::lock. */
    int state;
};

/** The boxes for the baking thread, in order: the frame takes them back from the head once baked. */
struct Queue
{
    CRITICAL_SECTION lock;
    HANDLE wake;
    HANDLE thread;
    Job jobs[kMaxJobs];
    int head, count;
    /** The thread's sums (kMaxBox cells of six floats), and its time (performance counter ticks) and boxes since the
     * last -lightdebug report. */
    float *accumulated;
    long long ticks;
    int baked;
};

struct State
{
    bool tried;
    Kind kinds[kMaxKinds];
    int kindCount;
    Lamp lamps[kMaxLamps];
    int count;
    float maxRadius;
    float gridMin[2];
    int gridSize[2];
    int *cellStart;
    int *cellLamps;
    /** The tunnels' roofs: each point's key (its x and y in roof cells, 16 bits each) and the roof's height. */
    unsigned int *roofKeys;
    float *roofHeights;
    /** Whether a roof is a tunnel's, with lamps, or an overpass's, which only keeps the rain out. */
    bool *roofLit;
    unsigned int roofs;
    /** The camera at the last Update. */
    float camera[3];
    bool haveCamera;
    Level levels[kLevels];
    bool created;
    Queue queue;
    /** How strongly the lights glow in the haze, from the last Haze. */
    float hazeStrength;
    /** The lamps knocked over (indices), and those whose cells still have to be baked again. */
    int knocked[kMaxKnocked];
    int knockedCount;
    int rebake[kMaxKnocked];
    int rebakeCount;
};

State g_state;

float ParseNumber(const char *&p)
{
    while (*p == ' ' || *p == '\t')
        ++p;
    float value = 0.0f, scale = 0.0f;
    const bool negative = *p == '-';
    if (negative)
        ++p;
    for (; (*p >= '0' && *p <= '9') || *p == '.'; ++p)
    {
        if (*p == '.')
            scale = 1.0f;
        else
        {
            value = value * 10.0f + static_cast<float>(*p - '0');
            if (scale > 0.0f)
                scale *= 10.0f;
        }
    }
    if (scale > 0.0f)
        value /= scale;
    return negative ? -value : value;
}

bool IsNumber(char c)
{
    return (c >= '0' && c <= '9') || c == '-' || c == '.';
}

/** The word at p (up to a space or '='), and p after it. */
void Word(const char *&p, char *out, int size)
{
    while (*p == ' ' || *p == '\t')
        ++p;
    int n = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '=' && *p != '\r' && *p != '\n' && n < size - 1)
        out[n++] = *p++;
    out[n] = 0;
}

void AddLamp(const Kind &kind, const char *p)
{
    if (g_state.count == kMaxLamps)
        return;
    Lamp &l = g_state.lamps[g_state.count];
    float values[7];
    for (float &v : values)
    {
        while (*p == ' ' || *p == '\t')
            ++p;
        if (!IsNumber(*p))
            return;
        v = ParseNumber(p);
    }
    for (int c = 0; c < 3; ++c)
    {
        l.position[c] = values[c];
        l.direction[c] = values[3 + c];
        l.colour[c] = kind.colour[c] * kind.intensity;
    }
    l.brightness = l.colour[0] * kLuminance[0] + l.colour[1] * kLuminance[1] + l.colour[2] * kLuminance[2];
    l.spot = kind.outer > -1.5f;
    const float length = sqrtf(l.direction[0] * l.direction[0] + l.direction[1] * l.direction[1] +
                               l.direction[2] * l.direction[2]);
    if (length > 0.5f)
        for (float &d : l.direction)
            d /= length;
    else
    {
        // The kind's own way: down for spot lamps.
        l.direction[0] = l.direction[1] = 0.0f;
        l.direction[2] = -1.0f;
    }
    const float height = values[6];
    l.glow = kind.glow;
    l.reflection = height > 0.0f ? l.position[2] - 2.0f * height : kNoReflection;
    l.radius = kind.radius;
    if (l.spot && l.direction[2] < kDownward && height > kUsualHeight)
    {
        l.radius *= height / kUsualHeight;
        l.radius = l.radius < kMostLampRadius ? l.radius : kMostLampRadius;
    }
    l.falloff = 4.0f / (l.radius * l.radius);
    l.hazeFalloff = 4.0f / (kind.radius * kind.radius);
    l.outer = kind.outer;
    l.inner = kind.inner;
    l.out = false;
    l.hasBase = false;
    for (const char *b = p; *b; ++b)
        if (b[0] == 'b' && b[1] == 'a' && b[2] == 's' && b[3] == 'e' && (b[4] == ' ' || b[4] == '\t'))
        {
            b += 4;
            int n = 0;
            for (; n < 3; ++n)
            {
                while (*b == ' ' || *b == '\t')
                    ++b;
                if (!IsNumber(*b))
                    break;
                l.base[n] = ParseNumber(b);
            }
            l.hasBase = n == 3;
            break;
        }
    g_state.maxRadius = l.radius > g_state.maxRadius ? l.radius : g_state.maxRadius;
    ++g_state.count;
}

/** A lamp's height above the ground (0 when not known). */
float HeightOf(const Lamp &l)
{
    return l.reflection == kNoReflection ? 0.0f : 0.5f * (l.position[2] - l.reflection);
}

/**
 * The heads of one mast (a stadium's or a car park's high mast has four, a twin-arm pole two) become one lamp in their
 * middle, sqrt(n) times as bright: the heads light the same ground, and n of them at full strength each burned it (and
 * the air around them) white. Heads of a kind within kClusterReach of the mast's height of the first one are one mast.
 */
void MergeClusters()
{
    int kept = 0, merged = 0;
    bool *gone = static_cast<bool *>(::operator new(g_state.count));
    memset(gone, 0, g_state.count);
    for (int i = 0; i < g_state.count; ++i)
    {
        if (gone[i])
            continue;
        Lamp &a = g_state.lamps[i];
        const float reach = HeightOf(a) * kClusterReach > kClusterMin ? HeightOf(a) * kClusterReach : kClusterMin;
        float position[3] = {a.position[0], a.position[1], a.position[2]};
        float direction[3] = {a.direction[0], a.direction[1], a.direction[2]};
        float reflection = a.reflection;
        int n = 1;
        for (int j = i + 1; j < g_state.count; ++j)
        {
            const Lamp &b = g_state.lamps[j];
            if (gone[j] || b.glow != a.glow || b.spot != a.spot || b.radius != a.radius || b.outer != a.outer)
                continue;
            const float dx = b.position[0] - a.position[0], dy = b.position[1] - a.position[1];
            const float dz = b.position[2] - a.position[2];
            if (dz * dz > kClusterHeight * kClusterHeight || dx * dx + dy * dy > reach * reach)
                continue;
            for (int c = 0; c < 3; ++c)
            {
                position[c] += b.position[c];
                direction[c] += b.direction[c];
            }
            reflection += b.reflection;
            gone[j] = true;
            ++n;
        }
        if (n > 1)
        {
            const float share = 1.0f / n, scale = sqrtf(static_cast<float>(n));
            for (int c = 0; c < 3; ++c)
            {
                a.position[c] = position[c] * share;
                a.colour[c] *= scale;
            }
            a.brightness *= scale;
            a.reflection = reflection * share;
            const float length = sqrtf(direction[0] * direction[0] + direction[1] * direction[1] +
                                       direction[2] * direction[2]);
            if (a.spot && length > 1e-3f)
                for (int c = 0; c < 3; ++c)
                    a.direction[c] = direction[c] / length;
            merged += n - 1;
        }
        g_state.lamps[kept++] = a;
    }
    ::operator delete(gone);
    if (merged)
        nfs::Log("lamps: %d heads merged into their masts' lamps", merged);
    g_state.count = kept;
}

unsigned int RoofKey(int x, int y)
{
    return (static_cast<unsigned int>(x + 32768) & 0xffffu) << 16 | (static_cast<unsigned int>(y + 32768) & 0xffffu);
}

unsigned int RoofSlot(unsigned int key)
{
    return (key * 2654435761u) >> (32 - kRoofBits);
}

void AddRoof(int x, int y, float height, bool lit)
{
    if (!g_state.roofKeys)
    {
        g_state.roofKeys = static_cast<unsigned int *>(::operator new(sizeof(unsigned int) * kRoofSlots));
        g_state.roofHeights = static_cast<float *>(::operator new(sizeof(float) * kRoofSlots));
        g_state.roofLit = static_cast<bool *>(::operator new(sizeof(bool) * kRoofSlots));
        memset(g_state.roofKeys, 0xff, sizeof(unsigned int) * kRoofSlots);
    }
    if (g_state.roofs >= kRoofSlots / 2)
        return;
    const unsigned int key = RoofKey(x, y);
    for (unsigned int s = RoofSlot(key);; s = (s + 1) & (kRoofSlots - 1))
    {
        // A point can be under several roofs, one road over another, each in a slot of its own.
        if (g_state.roofKeys[s] == key && fabsf(g_state.roofHeights[s] - height) < kRoofSame)
        {
            g_state.roofHeights[s] = height < g_state.roofHeights[s] ? height : g_state.roofHeights[s];
            g_state.roofLit[s] = g_state.roofLit[s] || lit;
            return;
        }
        if (g_state.roofKeys[s] == kNoRoof)
        {
            g_state.roofKeys[s] = key;
            g_state.roofHeights[s] = height;
            g_state.roofLit[s] = lit;
            ++g_state.roofs;
            return;
        }
    }
}

/** The roof over point (x, y) (roof cells) that a place at height z is under: the lowest that reaches it, or the highest for a
 * place over them all. False where no tunnel's roof is. */
bool RoofAt(int x, int y, float z, float &height, bool *lit = nullptr)
{
    if (!g_state.roofs)
        return false;
    const unsigned int key = RoofKey(x, y);
    bool any = false, reaches = false;
    for (unsigned int s = RoofSlot(key);; s = (s + 1) & (kRoofSlots - 1))
    {
        if (g_state.roofKeys[s] == kNoRoof)
            return any;
        if (g_state.roofKeys[s] != key)
            continue;
        const float roof = g_state.roofHeights[s];
        const bool over = z <= roof + kRoofMargin;
        if (!any || (over && (!reaches || roof < height)) || (!over && !reaches && roof > height))
        {
            height = roof;
            if (lit)
                *lit = g_state.roofLit[s];
        }
        reaches = reaches || over;
        any = true;
    }
}

/** "Roof y x z..." or "Cover y x z...": a row of roof points, from point (x, y) along x. */
void AddRoofRow(const char *p, bool lit)
{
    float values[2];
    for (float &v : values)
    {
        while (*p == ' ' || *p == '\t')
            ++p;
        if (!IsNumber(*p))
            return;
        v = ParseNumber(p);
    }
    const int y = static_cast<int>(values[0]), x = static_cast<int>(values[1]);
    for (int i = 0;; ++i)
    {
        while (*p == ' ' || *p == '\t')
            ++p;
        if (!IsNumber(*p))
            return;
        AddRoof(x + i, y, ParseNumber(p), lit);
    }
}

void BuildGrid()
{
    if (!g_state.count)
        return;
    float lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
    for (int i = 0; i < g_state.count; ++i)
        for (int a = 0; a < 2; ++a)
        {
            lo[a] = g_state.lamps[i].position[a] < lo[a] ? g_state.lamps[i].position[a] : lo[a];
            hi[a] = g_state.lamps[i].position[a] > hi[a] ? g_state.lamps[i].position[a] : hi[a];
        }
    for (int a = 0; a < 2; ++a)
    {
        g_state.gridMin[a] = lo[a];
        g_state.gridSize[a] = static_cast<int>((hi[a] - lo[a]) / kGridCell) + 1;
    }
    const int cells = g_state.gridSize[0] * g_state.gridSize[1];
    g_state.cellStart = static_cast<int *>(::operator new(sizeof(int) * (cells + 1)));
    g_state.cellLamps = static_cast<int *>(::operator new(sizeof(int) * (g_state.count ? g_state.count : 1)));
    memset(g_state.cellStart, 0, sizeof(int) * (cells + 1));
    auto cellOf = [](const Lamp &l) {
        const int x = static_cast<int>((l.position[0] - g_state.gridMin[0]) / kGridCell);
        const int y = static_cast<int>((l.position[1] - g_state.gridMin[1]) / kGridCell);
        return y * g_state.gridSize[0] + x;
    };
    for (int i = 0; i < g_state.count; ++i)
        ++g_state.cellStart[cellOf(g_state.lamps[i]) + 1];
    for (int c = 0; c < cells; ++c)
        g_state.cellStart[c + 1] += g_state.cellStart[c];
    int *fill = static_cast<int *>(::operator new(sizeof(int) * (cells + 1)));
    memcpy(fill, g_state.cellStart, sizeof(int) * (cells + 1));
    for (int i = 0; i < g_state.count; ++i)
        g_state.cellLamps[fill[cellOf(g_state.lamps[i])]++] = i;
    ::operator delete(fill);
}

/** Reads LAMPS.TXT once; false when it has neither lamps nor tunnels ([World] Lamps = 0 leaves the lamps out,
 * [World] Tunnels = 0 the tunnels). */
bool Load()
{
    if (g_state.tried)
        return g_state.count > 0 || g_state.roofs > 0;
    g_state.tried = true;
    const bool lamps = nfs::config::Int("World", "Lamps", 1) != 0;
    const bool tunnels = nfs::config::Int("World", "Tunnels", 1) != 0;
    if (!lamps && !tunnels)
        return false;
    uint32_t size = 0;
    char *text = reinterpret_cast<char *>(nfs::pack::ReadFile(kPath, size));
    if (!text)
    {
        nfs::Log("lamps: no %s (tools/night.py writes it), no lamps at night", kPath);
        return false;
    }
    for (char *line = text; *line;)
    {
        char *end = line;
        while (*end && *end != '\n')
            ++end;
        const char saved = *end;
        *end = 0;
        const char *p = line;
        char word[16];
        Word(p, word, sizeof(word));
        if (word[0] == '#' || !word[0])
        {
        }
        else if (!lstrcmpiA(word, "Roof") || !lstrcmpiA(word, "Cover"))
        {
            if (tunnels)
                AddRoofRow(p, !lstrcmpiA(word, "Roof"));
        }
        else if (!lamps)
        {
        }
        else if (!lstrcmpiA(word, "Kind"))
        {
            while (*p == ' ' || *p == '\t' || *p == '=')
                ++p;
            if (g_state.kindCount < kMaxKinds)
            {
                Kind &k = g_state.kinds[g_state.kindCount];
                Word(p, k.name, sizeof(k.name));
                float values[7];
                bool whole = true;
                for (float &v : values)
                {
                    while (*p == ' ' || *p == '\t')
                        ++p;
                    whole &= IsNumber(*p);
                    v = whole ? ParseNumber(p) : 0.0f;
                }
                if (whole)
                {
                    for (int c = 0; c < 3; ++c)
                        k.colour[c] = values[c];
                    k.intensity = values[3];
                    k.radius = values[4];
                    k.outer = values[5];
                    k.inner = values[6];
                    while (*p == ' ' || *p == '\t')
                        ++p;
                    k.glow = IsNumber(*p) ? static_cast<unsigned char>(ParseNumber(p)) : kDefaultGlow;
                    ++g_state.kindCount;
                }
            }
        }
        else
        {
            for (int k = 0; k < g_state.kindCount; ++k)
                if (!lstrcmpiA(word, g_state.kinds[k].name))
                {
                    AddLamp(g_state.kinds[k], p);
                    break;
                }
        }
        *end = saved;
        line = *end ? end + 1 : end;
    }
    ::operator delete(text);
    MergeClusters();
    BuildGrid();
    nfs::Log("lamps: %d lamps of %d kinds, %u tunnel roof points from %s", g_state.count, g_state.kindCount,
             g_state.roofs, kPath);
    return g_state.count > 0 || g_state.roofs > 0;
}

/** Calls f for each lamp whose grid cell overlaps the box [lo, hi] (x, y) widened by `reach`. */
template <typename F> void ForLampsNear(const float lo[2], const float hi[2], float reach, F f)
{
    int from[2], to[2];
    for (int a = 0; a < 2; ++a)
    {
        from[a] = static_cast<int>(floorf((lo[a] - reach - g_state.gridMin[a]) / kGridCell));
        to[a] = static_cast<int>(floorf((hi[a] + reach - g_state.gridMin[a]) / kGridCell));
        from[a] = from[a] < 0 ? 0 : from[a];
        to[a] = to[a] >= g_state.gridSize[a] ? g_state.gridSize[a] - 1 : to[a];
    }
    for (int y = from[1]; y <= to[1]; ++y)
        for (int x = from[0]; x <= to[0]; ++x)
        {
            const int cell = y * g_state.gridSize[0] + x;
            for (int k = g_state.cellStart[cell]; k < g_state.cellStart[cell + 1]; ++k)
                f(g_state.lamps[g_state.cellLamps[k]]);
        }
}

float Smoothstep(float edge0, float edge1, float x)
{
    float t = (x - edge0) / (edge1 - edge0);
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return t * t * (3.0f - 2.0f * t);
}

/** A float as a half (round to nearest; tiny values to zero, large ones to the largest half). */
unsigned short Half(float f)
{
    unsigned int x;
    memcpy(&x, &f, sizeof(x));
    const unsigned int sign = (x >> 16) & 0x8000u;
    const int exponent = static_cast<int>((x >> 23) & 0xffu) - 127 + 15;
    const unsigned int mantissa = x & 0x7fffffu;
    if (exponent <= 0)
        return static_cast<unsigned short>(sign);
    if (exponent >= 31)
        return static_cast<unsigned short>(sign | 0x7bffu);
    unsigned int h = sign | (static_cast<unsigned int>(exponent) << 10) | (mantissa >> 13);
    if (mantissa & 0x1000u)
        ++h;
    return static_cast<unsigned short>(h);
}

int FloorDiv(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

int Cells(const Job &job)
{
    return (job.to[0] - job.from[0]) * (job.to[1] - job.from[1]) * (job.to[2] - job.from[2]);
}

/** The baking thread's work: the lamps' light in a box of cells, into its texels. */
void BakeBox(Job &job, float *acc)
{
    const float cell = kCell[job.level];
    const int *from = job.from, *to = job.to;
    const int n[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
    const int cells = n[0] * n[1] * n[2];
    memset(acc, 0, sizeof(float) * 6 * cells);
    const float lo[2] = {from[0] * cell, from[1] * cell}, hi[2] = {(to[0] - 1) * cell, (to[1] - 1) * cell};
    const float zlo = from[2] * cell, zhi = (to[2] - 1) * cell;
    ForLampsNear(lo, hi, g_state.maxRadius, [&](const Lamp &l) {
        if (l.out)
            return;
        const float r = l.radius, r2 = r * r, inverseR2 = 1.0f / r2;
        if (l.position[2] + r < zlo || l.position[2] - r > zhi)
            return;
        int a0[3], a1[3];
        for (int a = 0; a < 3; ++a)
        {
            a0[a] = static_cast<int>(ceilf((l.position[a] - r) / cell));
            a1[a] = static_cast<int>(floorf((l.position[a] + r) / cell));
            a0[a] = a0[a] < from[a] ? from[a] : a0[a];
            a1[a] = a1[a] > to[a] - 1 ? to[a] - 1 : a1[a];
        }
        // A lamp shining down lights nothing above its head.
        if (l.spot && l.direction[2] < kDownward)
        {
            const int top = static_cast<int>(floorf(l.position[2] / cell)) + 1;
            a1[2] = a1[2] > top ? top : a1[2];
        }
        for (int k = a0[2]; k <= a1[2]; ++k)
        {
            const float dz = l.position[2] - k * cell;
            for (int j = a0[1]; j <= a1[1]; ++j)
            {
                const float dy = l.position[1] - j * cell;
                const float dyz = dy * dy + dz * dz;
                if (dyz >= r2)
                    continue;
                float *row = acc + 6 * (((k - from[2]) * n[1] + (j - from[1])) * n[0]);
                for (int i = a0[0]; i <= a1[0]; ++i)
                {
                    const float dx = l.position[0] - i * cell;
                    const float d2 = dx * dx + dyz;
                    if (d2 >= r2)
                        continue;
                    const float inv = 1.0f / sqrtf(d2 > 1e-4f ? d2 : 1e-4f);
                    const float lx = dx * inv, ly = dy * inv, lz = dz * inv;
                    const float window = 1.0f - d2 * inverseR2;
                    float shine = window * window / (1.0f + l.falloff * d2);
                    if (l.spot)
                    {
                        shine *= Smoothstep(l.outer, l.inner,
                                            -(lx * l.direction[0] + ly * l.direction[1] + lz * l.direction[2]));
                        if (shine <= 0.0f)
                            continue;
                    }
                    float *v = row + 6 * (i - from[0]);
                    v[0] += l.colour[0] * shine;
                    v[1] += l.colour[1] * shine;
                    v[2] += l.colour[2] * shine;
                    const float weight = l.brightness * shine;
                    v[3] += lx * weight;
                    v[4] += ly * weight;
                    v[5] += lz * weight;
                }
            }
        }
    });
    unsigned short *light = job.texels, *direction = job.texels + 4 * cells;
    for (int c = 0; c < cells; ++c)
    {
        const float *v = acc + 6 * c;
        for (int k = 0; k < 3; ++k)
        {
            light[4 * c + k] = Half(v[k]);
            direction[4 * c + k] = Half(v[3 + k]);
        }
    }
    // The tunnels: whether a roof is over a column, and for each cell the height of the roof it is under - a neighbouring
    // column's where none is, so that the height filters smoothly up to the tunnel's edge.
    static const int kAround[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    const int step = static_cast<int>(cell / kRoofCell + 0.5f);
    const unsigned short one = Half(1.0f), half = Half(0.5f), none = Half(0.0f);
    for (int j = 0; j < n[1]; ++j)
        for (int i = 0; i < n[0]; ++i)
        {
            const int x = (from[0] + i) * step, y = (from[1] + j) * step;
            float height = 0.0f;
            const bool under = RoofAt(x, y, 0.0f, height);
            bool beside = false;
            int rx = x, ry = y;
            if (!under)
                for (const int *d : kAround)
                    if (RoofAt(x + d[0] * step, y + d[1] * step, 0.0f, height))
                    {
                        rx = x + d[0] * step;
                        ry = y + d[1] * step;
                        beside = true;
                        break;
                    }
            for (int k = 0; k < n[2]; ++k)
            {
                bool lit = false;
                if (under || beside)
                    RoofAt(rx, ry, (from[2] + k) * cell, height, &lit);
                const int c = (k * n[1] + j) * n[0] + i;
                // 1 under a tunnel's roof, a half under an overpass's (lighting.hlsli RoofCover, RoofLit).
                light[4 * c + 3] = !under ? none : lit ? one : half;
                direction[4 * c + 3] = Half(height);
            }
        }
}

DWORD WINAPI BakingThread(void *)
{
    Queue &q = g_state.queue;
    for (;;)
    {
        WaitForSingleObject(q.wake, INFINITE);
        for (;;)
        {
            Job *job = nullptr;
            EnterCriticalSection(&q.lock);
            for (int k = 0; k < q.count && !job; ++k)
            {
                Job &j = q.jobs[(q.head + k) % kMaxJobs];
                if (j.state == kQueued)
                {
                    j.state = kBaking;
                    job = &j;
                }
            }
            LeaveCriticalSection(&q.lock);
            if (!job)
                break;
            LARGE_INTEGER start, end;
            QueryPerformanceCounter(&start);
            BakeBox(*job, q.accumulated);
            QueryPerformanceCounter(&end);
            EnterCriticalSection(&q.lock);
            job->state = job->state == kBaking ? kBaked : job->state;
            q.ticks += end.QuadPart - start.QuadPart;
            ++q.baked;
            LeaveCriticalSection(&q.lock);
        }
    }
}

/** The boxes' texel buffers, kept for the next boxes instead of freed: allocating them each time took the C runtime's
 * heap lock on the frame's thread, which waited on the other threads' allocations (17 % of the slow frames at
 * motorway speeds, 2026-11-13). Two sizes, the smaller for most boxes; only the frame's thread uses them. */
const int kSmallBoxCells = 16384;
const int kKeptBuffers = 48;
unsigned short *g_keptSmall[kKeptBuffers];
unsigned short *g_keptLarge[kKeptBuffers];
int g_keptSmallCount, g_keptLargeCount;

unsigned short *TakeTexels(int cells)
{
    const bool small = cells <= kSmallBoxCells;
    unsigned short **kept = small ? g_keptSmall : g_keptLarge;
    int &count = small ? g_keptSmallCount : g_keptLargeCount;
    if (count)
        return kept[--count];
    return static_cast<unsigned short *>(::operator new(sizeof(unsigned short) * 8 * (small ? kSmallBoxCells : kMaxBox)));
}

void GiveTexels(unsigned short *texels, int cells)
{
    if (!texels)
        return;
    const bool small = cells <= kSmallBoxCells;
    unsigned short **kept = small ? g_keptSmall : g_keptLarge;
    int &count = small ? g_keptSmallCount : g_keptLargeCount;
    if (count < kKeptBuffers)
        kept[count++] = texels;
    else
        ::operator delete(texels);
}

/** Queues a level's cells [from, to) for baking, in boxes: a few layers at most, split where the ring wraps; the level
 * has the window `origin` once they are in. A level drawn anew (`anew`) is whole only once its last box is in. False,
 * and nothing queued, when the queue has no room. */
bool Enqueue(int level, const int from[3], const int to[3], const int origin[3], bool anew)
{
    Job boxes[kMaxJobs];
    int count = 0;
    const int area = (to[0] - from[0]) * (to[1] - from[1]);
    const int layers = area > 0 ? (kMaxBox / area < to[2] - from[2] ? kMaxBox / area : to[2] - from[2]) : 0;
    for (int z = from[2]; layers > 0 && z < to[2]; z += layers)
    {
        const int top = z + layers < to[2] ? z + layers : to[2];
        // Each axis in one or two pieces, where the ring wraps.
        int cuts[3][3], pieces[3];
        const int lo[3] = {from[0], from[1], z}, hi[3] = {to[0], to[1], top};
        for (int a = 0; a < 3; ++a)
        {
            const int boundary = (FloorDiv(lo[a], kSize[a]) + 1) * kSize[a];
            cuts[a][0] = lo[a];
            pieces[a] = boundary < hi[a] ? 2 : 1;
            cuts[a][1] = boundary < hi[a] ? boundary : hi[a];
            cuts[a][2] = hi[a];
        }
        for (int pz = 0; pz < pieces[2]; ++pz)
            for (int py = 0; py < pieces[1]; ++py)
                for (int px = 0; px < pieces[0]; ++px)
                {
                    if (count == kMaxJobs)
                        return false;
                    Job &b = boxes[count++];
                    b.level = level;
                    const int p[3] = {px, py, pz};
                    for (int a = 0; a < 3; ++a)
                    {
                        b.from[a] = cuts[a][p[a]];
                        b.to[a] = cuts[a][p[a] + 1];
                        b.origin[a] = origin[a];
                    }
                }
    }
    Queue &q = g_state.queue;
    EnterCriticalSection(&q.lock);
    const bool room = q.count + count <= kMaxJobs;
    const int queued = q.count;
    LeaveCriticalSection(&q.lock);
    if (!room)
    {
        static const bool debug = nfs::vk::HasArgument("-lightdebug");
        static DWORD logged;
        if (debug && GetTickCount() - logged > 500)
        {
            logged = GetTickCount();
            nfs::Log("lamps: no room in the queue for %d boxes of level %d (%d queued)", count, level, queued);
        }
        return false;
    }
    for (int i = 0; i < count; ++i)
    {
        boxes[i].texels = TakeTexels(Cells(boxes[i]));
        boxes[i].whole = !anew || i == count - 1;
        boxes[i].state = kQueued;
    }
    EnterCriticalSection(&q.lock);
    for (int i = 0; i < count; ++i)
        q.jobs[(q.head + q.count++) % kMaxJobs] = boxes[i];
    LeaveCriticalSection(&q.lock);
    SetEvent(q.wake);
    return true;
}

/** Uploads the boxes the thread has baked, in order, in one copy per volume, and moves their levels' windows. */
void Collect()
{
    const int kMaxUploads = 64;
    nfs::vk::Renderer::VolumeBox light[kLevels][kMaxUploads], direction[kLevels][kMaxUploads];
    int uploads[kLevels] = {};
    unsigned short *texels[kLevels * kMaxUploads];
    int texelCells[kLevels * kMaxUploads];
    int taken = 0;
    Queue &q = g_state.queue;
    for (;;)
    {
        EnterCriticalSection(&q.lock);
        const bool baked = q.count > 0 && q.jobs[q.head].state == kBaked &&
                           uploads[q.jobs[q.head].level] < kMaxUploads;
        Job job;
        if (baked)
        {
            job = q.jobs[q.head];
            q.head = (q.head + 1) % kMaxJobs;
            --q.count;
        }
        LeaveCriticalSection(&q.lock);
        if (!baked)
            break;
        nfs::vk::Renderer::VolumeBox &l = light[job.level][uploads[job.level]];
        nfs::vk::Renderer::VolumeBox &d = direction[job.level][uploads[job.level]++];
        for (int a = 0; a < 3; ++a)
        {
            l.at[a] = d.at[a] = static_cast<uint32_t>(job.from[a] - FloorDiv(job.from[a], kSize[a]) * kSize[a]);
            l.extent[a] = d.extent[a] = static_cast<uint32_t>(job.to[a] - job.from[a]);
        }
        l.data = job.texels;
        d.data = job.texels + 4 * Cells(job);
        texelCells[taken] = Cells(job);
        texels[taken++] = job.texels;
        Level &v = g_state.levels[job.level];
        memcpy(v.shown, job.origin, sizeof(v.shown));
        static const bool debug = nfs::vk::HasArgument("-lightdebug");
        if (debug && v.ready != job.whole)
            nfs::Log("lamps: level %d %s", job.level, job.whole ? "shown again" : "hidden while it is baked anew");
        v.ready = job.whole;
    }
    for (int i = 0; i < kLevels; ++i)
    {
        nfs::vk::g_renderer->UploadVolumeBoxes(g_state.levels[i].light, light[i], static_cast<uint32_t>(uploads[i]));
        nfs::vk::g_renderer->UploadVolumeBoxes(g_state.levels[i].direction, direction[i],
                                               static_cast<uint32_t>(uploads[i]));
    }
    for (int i = 0; i < taken; ++i)
        GiveTexels(texels[i], texelCells[i]);
}

bool CreateVolumes()
{
    if (g_state.created)
        return g_state.levels[0].light != nullptr;
    g_state.created = true;
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    // Every texel written once, so the volumes are readable before their first bake.
    void *zero = ::operator new(sizeof(unsigned short) * 4 * kTexels);
    memset(zero, 0, sizeof(unsigned short) * 4 * kTexels);
    nfs::vk::Texture *volumes[2 * kLevels] = {};
    for (int i = 0; i < 2 * kLevels; ++i)
    {
        volumes[i] = r->CreateVolumeTexture(kSize[0], kSize[1], kSize[2], 1, kHalfFormat);
        if (!volumes[i])
        {
            nfs::Log("lamps: no volume textures, no lamps at night");
            ::operator delete(zero);
            for (nfs::vk::Texture *t : volumes)
                if (t)
                    r->ReleaseTexture(t);
            return false;
        }
        r->UploadTexture(volumes[i], 0, 0, zero, sizeof(unsigned short) * 4 * kTexels);
    }
    ::operator delete(zero);
    for (int i = 0; i < kLevels; ++i)
    {
        g_state.levels[i].light = volumes[2 * i];
        g_state.levels[i].direction = volumes[2 * i + 1];
    }
    r->SetLampVolumes(volumes);
    Queue &q = g_state.queue;
    if (!q.thread)
    {
        InitializeCriticalSection(&q.lock);
        q.wake = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        q.accumulated = static_cast<float *>(::operator new(sizeof(float) * 6 * kMaxBox));
        q.thread = CreateThread(nullptr, 0, BakingThread, nullptr, 0, nullptr);
        // The frame comes first.
        SetThreadPriority(q.thread, THREAD_PRIORITY_BELOW_NORMAL);
    }
    return true;
}

/** Queues the cells the camera has brought into a level's window: slab by slab, or the whole window after a jump. */
void UpdateLevel(int index, const float camera[3])
{
    Level &v = g_state.levels[index];
    const float cell = kCell[index];
    int want[3];
    for (int a = 0; a < 3; ++a)
        want[a] = static_cast<int>(floorf(camera[a] / cell)) - kSize[a] / 2;
    bool anew = !v.queued;
    for (int a = 0; a < 3 && !anew; ++a)
    {
        const int moved = want[a] - v.wanted[a];
        anew = moved > kSize[a] / kMoveFraction || -moved > kSize[a] / kMoveFraction;
    }
    if (anew)
    {
        static const bool debug = nfs::vk::HasArgument("-lightdebug");
        if (debug && v.queued)
            nfs::Log("lamps: level %d baked anew: the window lagged %d %d %d cells behind the camera", index,
                     want[0] - v.wanted[0], want[1] - v.wanted[1], want[2] - v.wanted[2]);
        const int to[3] = {want[0] + kSize[0], want[1] + kSize[1], want[2] + kSize[2]};
        if (Enqueue(index, want, to, want, true))
        {
            memcpy(v.wanted, want, sizeof(want));
            v.queued = true;
        }
        return;
    }
    // An axis at a time: the cells the window reaches along it, over the extent it has on the others.
    for (int a = 0; a < 3; ++a)
    {
        const int moved = want[a] - v.wanted[a];
        if (moved < kStep[index][a] && -moved < kStep[index][a])
            continue;
        int from[3], to[3], origin[3];
        for (int b = 0; b < 3; ++b)
        {
            from[b] = v.wanted[b];
            to[b] = v.wanted[b] + kSize[b];
            origin[b] = v.wanted[b];
        }
        origin[a] = want[a];
        if (want[a] > v.wanted[a])
        {
            from[a] = v.wanted[a] + kSize[a];
            to[a] = want[a] + kSize[a];
        }
        else
        {
            from[a] = want[a];
            to[a] = v.wanted[a];
        }
        if (!Enqueue(index, from, to, origin, false))
            return;
        v.wanted[a] = want[a];
    }
}

/** Bakes again the cells a lamp reaches in each level's window (a lamp that went out). */
bool Rebake(const Lamp &l)
{
    for (int i = 0; i < kLevels; ++i)
    {
        const Level &v = g_state.levels[i];
        if (!v.queued)
            continue;
        int from[3], to[3];
        bool empty = false;
        for (int a = 0; a < 3; ++a)
        {
            from[a] = static_cast<int>(floorf((l.position[a] - l.radius) / kCell[i]));
            to[a] = static_cast<int>(ceilf((l.position[a] + l.radius) / kCell[i])) + 1;
            from[a] = from[a] < v.wanted[a] ? v.wanted[a] : from[a];
            to[a] = to[a] > v.wanted[a] + kSize[a] ? v.wanted[a] + kSize[a] : to[a];
            empty |= from[a] >= to[a];
        }
        if (!empty && !Enqueue(i, from, to, v.wanted, false))
            return false;
    }
    return true;
}

/** Relights the knocked-over lamps the camera has left far behind, and bakes again around those that went out. */
void UpdateKnocked(const float camera[3])
{
    for (int k = 0; k < g_state.knockedCount;)
    {
        Lamp &l = g_state.lamps[g_state.knocked[k]];
        const float dx = l.position[0] - camera[0], dy = l.position[1] - camera[1];
        if (dx * dx + dy * dy > kRelightRange * kRelightRange)
        {
            l.out = false;
            g_state.knocked[k] = g_state.knocked[--g_state.knockedCount];
        }
        else
            ++k;
    }
    while (g_state.rebakeCount > 0 && Rebake(g_state.lamps[g_state.rebake[g_state.rebakeCount - 1]]))
        --g_state.rebakeCount;
}
} // namespace

void KnockedOver(const float position[3])
{
    if (!Load())
        return;
    const float at[2] = {position[0], position[1]};
    ForLampsNear(at, at, kKnockReach, [&](const Lamp &found) {
        Lamp &l = const_cast<Lamp &>(found);
        if (!l.hasBase || l.out || g_state.knockedCount == kMaxKnocked || g_state.rebakeCount == kMaxKnocked)
            return;
        const float dx = l.base[0] - position[0], dy = l.base[1] - position[1], dz = l.base[2] - position[2];
        if (dx * dx + dy * dy > kKnockReach * kKnockReach || dz * dz > 4.0f * kKnockReach * kKnockReach)
            return;
        l.out = true;
        const int index = static_cast<int>(&l - g_state.lamps);
        g_state.knocked[g_state.knockedCount++] = index;
        g_state.rebake[g_state.rebakeCount++] = index;
        nfs::Log("lamps: the lamp post at %d %d was knocked over: its lamp is out", static_cast<int>(l.base[0]),
                 static_cast<int>(l.base[1]));
    });
}

bool Tunnels()
{
    return nfs::vk::g_renderer && Load() && g_state.roofs > 0;
}

float CameraCover()
{
    if (!g_state.haveCamera || !Tunnels())
        return 0.0f;
    const int x = static_cast<int>(floorf(g_state.camera[0] / kRoofCell + 0.5f));
    const int y = static_cast<int>(floorf(g_state.camera[1] / kRoofCell + 0.5f));
    const float z = g_state.camera[2];
    float roof;
    bool lit = false;
    if (!RoofAt(x, y, z, roof, &lit) || !lit)
        return 0.0f;
    const float below = roof + kRoofMargin - z, above = z - roof + kTunnelDepth;
    return (below < 0.0f ? 0.0f : below > 1.0f ? 1.0f : below) * (above < 0.0f ? 0.0f : above > 1.0f ? 1.0f : above);
}

void Update(const float camera[3], float volumes[2][4])
{
    memset(volumes, 0, sizeof(float) * 2 * 4);
    memcpy(g_state.camera, camera, sizeof(g_state.camera));
    g_state.haveCamera = true;
    // By day too: the volumes hold the tunnels' roofs, which keep the rain out and the tunnels lit at any hour.
    if (!nfs::vk::g_renderer || !Load() || !CreateVolumes())
        return;
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    Collect();
    UpdateKnocked(camera);
    for (int i = 0; i < kLevels; ++i)
    {
        UpdateLevel(i, camera);
        const Level &v = g_state.levels[i];
        if (!v.ready)
            continue;
        for (int a = 0; a < 3; ++a)
            volumes[i][a] = static_cast<float>(v.shown[a]) * kCell[i];
        volumes[i][3] = kCell[i];
    }
    QueryPerformanceCounter(&end);
    // -lightdebug: what the baking costs, in the frame and on its thread.
    static const bool debug = nfs::vk::HasArgument("-lightdebug");
    static long long ticks, most;
    static int frames;
    static DWORD logged;
    if (!debug)
        return;
    const long long spent = end.QuadPart - start.QuadPart;
    ticks += spent;
    most = spent > most ? spent : most;
    ++frames;
    if (GetTickCount() - logged > 4000)
    {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        const double microseconds = 1e6 / static_cast<double>(frequency.QuadPart);
        Queue &q = g_state.queue;
        EnterCriticalSection(&q.lock);
        const long long thread = q.ticks;
        const int baked = q.baked;
        q.ticks = 0;
        q.baked = 0;
        LeaveCriticalSection(&q.lock);
        nfs::Log("lamps: %d us a frame on average, at most %d us, over %d frames; the baking thread %d boxes in %d us",
                 static_cast<int>(static_cast<double>(ticks) * microseconds / frames),
                 static_cast<int>(static_cast<double>(most) * microseconds), frames, baked,
                 static_cast<int>(static_cast<double>(thread) * microseconds));
        logged = GetTickCount();
        ticks = most = 0;
        frames = 0;
    }
}

void AddGlows(const float camera[3])
{
    const float strength = nfs::timeofday::LocalLights();
    if (strength <= 0.0f || !Load())
        return;
    const float intensity = strength < 1.0f ? strength : 1.0f;
    const float lo[2] = {camera[0], camera[1]}, hi[2] = {camera[0], camera[1]};
    bool full = false;
    int added = 0;
    ForLampsNear(lo, hi, kGlowRange, [&](const Lamp &l) {
        const float dx = l.position[0] - camera[0], dy = l.position[1] - camera[1], dz = l.position[2] - camera[2];
        if (full || l.out || l.glow == kNoGlow || dx * dx + dy * dy + dz * dz > kGlowRange * kGlowRange)
            return;
        struct PoolFlare
        {
            unsigned char unknown00[0x10];
            float position[3];
            float reflectionHeight;
            float direction[3];
            unsigned char type;
            unsigned char flags;
        };
        PoolFlare *flare = reinterpret_cast<PoolFlare *>(nfs::flares::Allocate(kExcludeNoView, intensity));
        if (!flare)
        {
            full = true;
            return;
        }
        memcpy(flare->position, l.position, sizeof(flare->position));
        flare->reflectionHeight = l.reflection;
        flare->type = l.glow;
        flare->flags = kFlareOmni | nfs::lights::kFlareBaked;
        ++added;
    });
    static DWORD logged;
    static const bool debug = nfs::vk::HasArgument("-lightdebug");
    if (debug && GetTickCount() - logged > 4000)
    {
        logged = GetTickCount();
        nfs::Log("lamps: %d glows within %d m%s", added, static_cast<int>(kGlowRange), full ? ", the flare pool full" : "");
    }
}

namespace
{
float s_hazeScale = -1.0f;
} // namespace

float HazeScale()
{
    if (s_hazeScale < 0.0f)
    {
        s_hazeScale = nfs::config::Float("World", "LampHaze", 1.0f);
        s_hazeScale = s_hazeScale < 0.0f ? 0.0f : s_hazeScale;
    }
    return s_hazeScale;
}

void SetHazeScale(float scale)
{
    s_hazeScale = scale < 0.0f ? 0.0f : scale;
    nfs::config::WriteFloat("World", "LampHaze", s_hazeScale);
}

void Haze(const float camera[3], const float forward[3], float haze[4], nfs::lights::GpuLight *lamps)
{
    memset(haze, 0, sizeof(float) * 4);
    g_state.hazeStrength = 0.0f;
    const float scale = HazeScale();
    float strength = nfs::timeofday::LocalLights() * scale;
    if (!nfs::vk::g_renderer || strength <= 0.0f)
        return;
    strength = strength < scale ? strength : scale;
    float extinction = nfs::timeofday::HazeExtinction();
    extinction = extinction < kClearHaze ? kClearHaze : extinction > kDensestGlow ? kDensestGlow : extinction;
    haze[0] = extinction;
    haze[1] = kHazeForward;
    haze[3] = strength;
    g_state.hazeStrength = strength;
    if (!Load() || g_state.count == 0)
        return;
    // The most important lamps first: the light that reaches the camera's surroundings (brightness times the radius
    // squared, the intensity, over the distance squared). A lamp behind the camera further than its reach lights none
    // of the air the view looks through.
    float importance[kHazeLamps];
    int count = 0;
    const float lo[2] = {camera[0], camera[1]}, hi[2] = {camera[0], camera[1]};
    ForLampsNear(lo, hi, kHazeRange, [&](const Lamp &l) {
        const float d[3] = {l.position[0] - camera[0], l.position[1] - camera[1], l.position[2] - camera[2]};
        const float d2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        if (l.out || l.glow == kNoGlow || d2 > kHazeRange * kHazeRange ||
            d[0] * forward[0] + d[1] * forward[1] + d[2] * forward[2] < -l.radius)
            return;
        const float r2 = l.radius * l.radius;
        const float weight = l.brightness * r2 / (d2 + r2);
        int slot = count;
        if (count == kHazeLamps)
        {
            if (weight <= importance[kHazeLamps - 1])
                return;
            slot = kHazeLamps - 1;
        }
        else
            ++count;
        for (; slot > 0 && importance[slot - 1] < weight; --slot)
        {
            importance[slot] = importance[slot - 1];
            lamps[slot] = lamps[slot - 1];
        }
        importance[slot] = weight;
        nfs::lights::GpuLight &g = lamps[slot];
        for (int c = 0; c < 3; ++c)
        {
            g.position[c] = l.position[c];
            g.color[c] = l.colour[c];
            g.direction[c] = l.direction[c];
        }
        g.position[3] = l.radius;
        g.color[3] = l.hazeFalloff;
        g.direction[3] = l.spot ? l.outer : -2.0f;
        g.cone[0] = l.inner;
        g.cone[1] = -1.0f;
        g.cone[2] = 0.0f;
        g.cone[3] = 1.0f;
    });
    haze[2] = static_cast<float>(count);
}

float HazeStrength()
{
    return g_state.hazeStrength;
}

void Release()
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    if (!g_state.created)
        return;
    g_state.created = false;
    // The boxes still queued are dropped; the one being baked is waited for.
    Queue &q = g_state.queue;
    for (bool baking = q.thread != nullptr; baking;)
    {
        baking = false;
        EnterCriticalSection(&q.lock);
        for (int k = 0; k < q.count; ++k)
        {
            Job &j = q.jobs[(q.head + k) % kMaxJobs];
            if (j.state == kQueued)
                j.state = kCancelled;
            baking |= j.state == kBaking;
        }
        if (!baking)
        {
            for (int k = 0; k < q.count; ++k)
            {
                const Job &j = q.jobs[(q.head + k) % kMaxJobs];
                GiveTexels(j.texels, Cells(j));
            }
            q.head = q.count = 0;
        }
        LeaveCriticalSection(&q.lock);
        if (baking)
            Sleep(1);
    }
    if (r)
        r->SetLampVolumes(nullptr);
    for (Level &v : g_state.levels)
    {
        if (r && v.light)
            r->ReleaseTexture(v.light);
        if (r && v.direction)
            r->ReleaseTexture(v.direction);
        v.light = v.direction = nullptr;
        v.ready = v.queued = false;
    }
}
} // namespace nfs::lamps
