/**
 * Local lights (engine/rendering/lights/lights.h): the flares the player view draws become lights.
 *
 * An eLightFlare holds its name hash (0x08; car flares say which light they are by it), its position (0x10) and
 * direction (0x20) in the space of the matrix it is drawn with (the identity for the world's flares, the car's matrix
 * for car flares), its type (0x2c, an index into LightFlareParameterIndicies: two parameter sets of size, colour and
 * texture each, from the attribute database) and flags (0x2d; bit 0: lit from both sides).
 */
#include <math.h>
#include <string.h>

#include "game/gen/api.h"
#include "game/platform/Platform.h"
#include "engine/rendering/lights/lights.h"
#include "legacy/link.h"
#include "core/config.h"
#include "core/log.h"
#include "dev/pose.h"
#include "gameplay/vehicles/cars.h"
#include "gameplay/world/timeofday.h"
#include "engine/rendering/vulkan/renderer.h"
#include "gameplay/world/weather.h"

/** LightFlareParameterIndicies: per flare type, two pointers to SingleLightFlareParameters (11 floats: size min and
 * max, red, green, blue, alpha, unknown, offset, scale min and max, texture). */
extern "C" const float *LightFlareParameterIndicies[];
extern "C" void *ScenerySectionHeaders[];
extern "C" int TheGameFlowManager_CurrentGameFlowState;

namespace nfs::lights
{
namespace
{
const int kPlayerView = 1;
/** eRenderLightFlare's flare types for the player view: car flares, and the pool's world flares (lamps at scenery
 * models' position markers, engine/rendering/lights/flares.h). */
const int kFlareTypeNormal = 0;
const int kFlareTypePlayerPool = 3;
const int kFlareTypes = 22;
const int kMaxRecorded = 1024;
/** The most lights looked at for the kept ones. */
const int kMaxKept = 256;
/** The flare's colour at full intensity becomes the light's colour times this. Signal lamps (traffic lights, signs,
 * blinkers: the world's other flares) are small: they colour only what is near them, a little. */
const float kLightIntensity = 0.8f;
/** Light radius per metre of flare size, and its limits. */
const float kRadiusPerSize = 3.0f;
const float kMinRadius = 3.0f;
const float kMaxRadius = 9.0f;
/** Car headlights: spot lights along the flare's direction. */
const unsigned int kHeadlightLeft = 0x9db90133;
const unsigned int kHeadlightRight = 0xd09091c6;
const float kHeadlightIntensity = 1.2f;
const float kHeadlightOuterCosine = 0.78f;
const float kHeadlightInnerCosine = 0.93f;
/** The beam aims a little down: its slope. */
const float kHeadlightDip = 0.03f;
const float kHeadlightColor[3] = {1.0f, 0.95f, 0.85f};
/** A headlight's glow in the haze against its light: a beam lights the road far ahead, which as a glow towards an
 * oncoming camera made a wide white blob in fog. In clear air a beam barely shows (kHeadlightGlowClear); it grows to
 * kHeadlightGlow with the fog or the rain. The other lights glow as their light says. */
const float kHeadlightGlow = 0.25f;
const float kHeadlightGlowClear = 0.07f;
/** The player's own beams against the other cars' (whose brightness stays): a little softer, as the camera sits
 * behind them and sees all of their light on the road. */
const float kPlayerHeadlightScale = 0.85f;
/** Lights closer than this to one already kept merge into it (flares of one lamp drawn twice, halo and core). */
const float kMergeDistance = 0.5f;
/** The cars' other lights (tail, brake, indicator and police lights) light only what is next to them, faintly. */
const float kCarLightRadius = 4.0f;
const float kCarLightIntensity = 0.35f;
/** The flares of the frame before were drawn with the car's matrix of then: the player's car is the one nearest to where
 * it is now, within this (m; 90 m/s at 30 frames a second). */
const float kPlayerCarLag = 3.0f;

/**
 * Every car's headlights are lights each frame, from its own flares and light state, whether its flares are drawn or
 * not (RecordCarHeadlights). Recorded from the flares drawn, a car's beams came and went with its flares: an oncoming
 * car's beams lit the walls of a tunnel ahead at once when the car grew to enough pixels on the screen (RenderFlaresOnCar
 * 0x742950 draws none for a car under the view's pixel size or out of the view), and the player's own were never there
 * in the bumper and hood views (VehicleRenderConn::RenderFlares 0x744e20 draws no flares of the car the camera is in).
 * VehicleRenderConn's list (0x9b37cc, its count at 0x9b37d4); a connection's hidden flag (+0x60), state (+0x28, 3 when
 * loaded), car render info (+0x44) and render matrix (virtual +0x18); the info's flares (+0x104, a list) and its lights
 * on (+0x1620) and broken (+0x1624), bit 1 the left headlight and bit 2 the right; bit 0x4000 a cop's flashing lights,
 * which alternate the headlights (here both at half).
 */
void **const *const kConnList = reinterpret_cast<void **const *>(0x9b37cc);
const int *const kConnCount = reinterpret_cast<const int *>(0x9b37d4);
const int kConnHidden = 0x60;
const int kConnState = 0x28;
const int kConnLoaded = 3;
const int kConnInfo = 0x44;
const int kConnRenderMatrix = 0x18;
const int kInfoFlares = 0x104;
const int kInfoLights = 0x1620;
const int kInfoBrokenLights = 0x1624;
const unsigned int kLeftHeadlightBit = 1;
const unsigned int kRightHeadlightBit = 2;
const unsigned int kFlashingBit = 0x4000;
/** INIS::mInstance (a cut-scene: the headlights are off unless switched on) and the switch that turns them on. */
void *const *const kNis = reinterpret_cast<void *const *>(0x9885c8);
const int *const kHeadlightsForced = reinterpret_cast<const int *>(0x9b092c);
const int kGameFlowRacing = 6;
/** A car's beams light the world up to this far from the camera, fading out over the last kCarBeamFade metres. */
const float kCarBeamRange = 220.0f;
const float kCarBeamFade = 60.0f;

struct Recorded
{
    float position[3];
    float direction[3];
    float color[3];
    float radius;
    float intensity;
    bool spot;
    /** The player's own headlight, which the headlight shadow map shades (PlayerHeadlights). */
    bool player;
    /** Where the matrix it was drawn with stands: one car's flares share it. */
    float origin[3];
    /** Spot cone (cosines of the outer and inner angles), and a low beam's cut-off. */
    float outer, inner;
    bool cutoff;
    /** A world light (drawn with the identity matrix): it never moves, so it is followed from frame to frame. */
    bool still;
};

/**
 * The world's lights from frame to frame. A flare is drawn only while its scenery model is in the view, and a light is
 * used only among the kMaxLights kept, so a light came and went in one frame: the model turning out of the view beside
 * the car took the light it threw on the road ahead with it, and a light pushed out of those kept by a brighter one
 * vanished. A world light is followed by its position (the same each frame): it fades in over kLightFadeSeconds when it
 * comes, is held at its last light for kHoldSeconds after its flare stops being drawn, and fades out when it goes or
 * loses its place (room is kept for those fading out).
 */
const float kLightFadeSeconds = 0.35f;
const float kHoldSeconds = 4.0f;
/** A light already shown keeps its place against a new one up to this much brighter. */
const float kKeepPlace = 1.25f;
const int kMaxTracked = 512;
const int kTrackedHash = 2048;

struct Tracked
{
    Recorded r;
    float fade;
    float seen;
    unsigned int key;
    unsigned int recordedFrame;
    bool shown;
};

struct State
{
    Recorded recorded[kMaxRecorded];
    int count;
    bool dumped;
    Tracked tracked[kMaxTracked];
    int trackedCount;
    short hash[kTrackedHash];
    unsigned int frame;
    float time;
    LARGE_INTEGER last;
    /** Lights shown last frame that lost their place and are still fading out: room is kept for them. */
    int fadingOut;
    /** The cars' headlights are recorded for the coming Collect (RecordCars). */
    bool carsRecorded;
};

State g_state;

struct FlareFields
{
    char unknown_00[8];
    unsigned int name;
    char unknown_0c[4];
    float position[3];
    float reflectionHeight;
    float direction[3];
    signed char type;
    unsigned char flags;
};

void Transform(const float in[3], const bMatrix4 &m, float w, float out[3])
{
    const float *f = reinterpret_cast<const float *>(&m);
    for (int c = 0; c < 3; ++c)
        out[c] = in[0] * f[c] + in[1] * f[4 + c] + in[2] * f[8 + c] + w * f[12 + c];
}

void DumpTypes()
{
    for (int t = 0; t < kFlareTypes; ++t)
        for (int set = 0; set < 2; ++set)
            if (const float *p = LightFlareParameterIndicies[t * 2 + set])
                nfs::Log("lights: flare type %d set %d: size %d-%d colour %d %d %d alpha %d, %d, offset %d, scale %d-%d, "
                         "texture %d (x100)",
                         t, set, static_cast<int>(p[0] * 100), static_cast<int>(p[1] * 100),
                         static_cast<int>(p[2] * 100), static_cast<int>(p[3] * 100), static_cast<int>(p[4] * 100),
                         static_cast<int>(p[5] * 100), static_cast<int>(p[6] * 100), static_cast<int>(p[7] * 100),
                         static_cast<int>(p[8] * 100), static_cast<int>(p[9] * 100), static_cast<int>(p[10]));
}
} // namespace

namespace
{
/** The light a flare drawn with `matrix` gives (car: drawn as a car's flare), into the frame's recorded lights. */
void RecordLight(const FlareFields &f, const bMatrix4 *matrix, float intensity, bool car)
{
    if (intensity <= 0.0f || g_state.count >= kMaxRecorded)
        return;
    if (f.type < 0 || f.type >= kFlareTypes || (f.flags & kFlareBaked))
        return;
    const float *params = LightFlareParameterIndicies[f.type * 2];
    if (!params)
        params = LightFlareParameterIndicies[f.type * 2 + 1];
    if (!params)
        return;
    Recorded &r = g_state.recorded[g_state.count++];
    r.still = matrix == &eMathIdentityMatrix && !(f.flags & nfs::lights::kFlareMoving);
    Transform(f.position, *matrix, 1.0f, r.position);
    Transform(f.direction, *matrix, 0.0f, r.direction);
    const float length = sqrtf(r.direction[0] * r.direction[0] + r.direction[1] * r.direction[1] +
                               r.direction[2] * r.direction[2]);
    for (float &d : r.direction)
        d = length > 0.0f ? d / length : 0.0f;
    r.intensity = intensity;
    r.spot = f.name == kHeadlightLeft || f.name == kHeadlightRight;
    r.player = false;
    memcpy(r.origin, &matrix->v3, sizeof(r.origin));
    if (r.spot)
    {
        r.direction[2] -= kHeadlightDip;
        const float dipped = sqrtf(r.direction[0] * r.direction[0] + r.direction[1] * r.direction[1] +
                                   r.direction[2] * r.direction[2]);
        for (float &d : r.direction)
            d /= dipped;
        for (int c = 0; c < 3; ++c)
            r.color[c] = kHeadlightColor[c] * kHeadlightIntensity * intensity;
        r.radius = kHeadlightRadius;
        r.outer = kHeadlightOuterCosine;
        r.inner = kHeadlightInnerCosine;
        r.cutoff = true;
        return;
    }
    // Parameter colours run to 255 in some types and to 1 in others.
    const float scale = (params[2] > 1.0f || params[3] > 1.0f || params[4] > 1.0f) ? 1.0f / 255.0f : 1.0f;
    r.cutoff = false;
    for (int c = 0; c < 3; ++c)
        r.color[c] = params[2 + c] * scale * (car ? kCarLightIntensity : kLightIntensity) * intensity;
    float radius = params[1] * kRadiusPerSize;
    r.radius = car ? kCarLightRadius : radius < kMinRadius ? kMinRadius : radius > kMaxRadius ? kMaxRadius : radius;
}

bool IsHeadlight(unsigned int name)
{
    return name == kHeadlightLeft || name == kHeadlightRight;
}

/** Debug.CarBeams=0: the cars' headlights from the flares drawn only, as before (for comparisons). */
bool CarBeams()
{
    static const bool on = nfs::config::Int("Debug", "CarBeams", 1) != 0;
    return on;
}

/** Every loaded car's headlights as lights, at the car's matrix of this frame (see kConnList). */
void RecordCarHeadlights(const float camera[3])
{
    if (!CarBeams() || TheGameFlowManager_CurrentGameFlowState != kGameFlowRacing || !*kConnList)
        return;
    const float base = (*kNis ? 0.0f : 1.0f) + (*kHeadlightsForced ? 1.0f : 0.0f);
    for (int k = 0; k < *kConnCount; ++k)
    {
        char *conn = static_cast<char *>((*kConnList)[k]);
        if (!conn || conn[kConnHidden] || *reinterpret_cast<const int *>(conn + kConnState) != kConnLoaded)
            continue;
        char *info = *reinterpret_cast<char **>(conn + kConnInfo);
        if (!info || nfs::cars::PopupsShut(info))
            continue;
        alignas(16) bMatrix4 matrix;
        typedef void(__thiscall * RenderMatrix)(void *, bMatrix4 *);
        (*reinterpret_cast<RenderMatrix *>(*reinterpret_cast<char **>(conn) + kConnRenderMatrix))(conn, &matrix);
        const float dx = matrix.v3.x - camera[0], dy = matrix.v3.y - camera[1], dz = matrix.v3.z - camera[2];
        const float distance = sqrtf(dx * dx + dy * dy + dz * dz);
        if (distance >= kCarBeamRange)
            continue;
        const float within = (kCarBeamRange - distance) / kCarBeamFade;
        const float fade = within < 1.0f ? within : 1.0f;
        const unsigned int on = *reinterpret_cast<const unsigned int *>(info + kInfoLights);
        const unsigned int broken = *reinterpret_cast<const unsigned int *>(info + kInfoBrokenLights);
        float left = (on & kLeftHeadlightBit) ? 1.0f : base, right = (on & kRightHeadlightBit) ? 1.0f : base;
        left = (broken & kLeftHeadlightBit) ? 0.0f : left > 1.0f ? 1.0f : left;
        right = (broken & kRightHeadlightBit) ? 0.0f : right > 1.0f ? 1.0f : right;
        if (on & kFlashingBit)
        {
            left *= 0.5f;
            right *= 0.5f;
        }
        char *const head = info + kInfoFlares;
        for (char *node = *reinterpret_cast<char **>(head); node && node != head; node = *reinterpret_cast<char **>(node))
        {
            const FlareFields &f = *reinterpret_cast<const FlareFields *>(node);
            if (IsHeadlight(f.name))
                RecordLight(f, &matrix, (f.name == kHeadlightLeft ? left : right) * fade, true);
        }
    }
}
} // namespace

void RecordCars(const float camera[3])
{
    if (g_state.carsRecorded)
        return;
    g_state.carsRecorded = true;
    RecordCarHeadlights(camera);
}

void Record(const eView *view, const eLightFlare *flare, const bMatrix4 *matrix, float intensity, int flareType)
{
    if (view != &eViews[kPlayerView] || (flareType != kFlareTypeNormal && flareType != kFlareTypePlayerPool) || !flare ||
        !matrix || intensity <= 0.0f)
        return;
    if (!g_state.dumped)
    {
        g_state.dumped = true;
        if (nfs::vk::HasArgument("-lightdebug"))
            DumpTypes();
    }
    const FlareFields &f = *reinterpret_cast<const FlareFields *>(flare);
    // In a race the cars' headlights come from every car each frame (RecordCarHeadlights), drawn or not.
    if (CarBeams() && TheGameFlowManager_CurrentGameFlowState == kGameFlowRacing && matrix != &eMathIdentityMatrix &&
        IsHeadlight(f.name))
        return;
    RecordLight(f, matrix, intensity, flareType == kFlareTypeNormal);
}

/** Where the player's car's headlights hang (their flares' car matrix origin), or false. */
bool PlayerLightOrigin(float origin[3])
{
    float car[4];
    if (!nfs::pose::PlayerCar(car))
        return false;
    const Recorded *own = nullptr;
    float nearest = kPlayerCarLag * kPlayerCarLag;
    for (int i = 0; i < g_state.count; ++i)
    {
        const Recorded &r = g_state.recorded[i];
        if (!r.spot)
            continue;
        const float dx = r.origin[0] - car[0], dy = r.origin[1] - car[1], dz = r.origin[2] - car[2];
        const float distance2 = dx * dx + dy * dy + dz * dz;
        if (distance2 < nearest)
        {
            nearest = distance2;
            own = &r;
        }
    }
    if (!own)
        return false;
    memcpy(origin, own->origin, sizeof(own->origin));
    return true;
}

bool PlayerHeadlights(float position[3], float direction[3], float origin[3], float *spread)
{
    if (!PlayerLightOrigin(origin))
        return false;
    float aim[3] = {};
    float sum[3] = {};
    int lamps = 0;
    for (int i = 0; i < g_state.count; ++i)
    {
        Recorded &r = g_state.recorded[i];
        if (!r.spot || memcmp(r.origin, origin, sizeof(r.origin)) != 0)
            continue;
        r.player = true;
        ++lamps;
        for (int c = 0; c < 3; ++c)
        {
            sum[c] += r.position[c];
            aim[c] += r.direction[c];
        }
    }
    const float length = sqrtf(aim[0] * aim[0] + aim[1] * aim[1] + aim[2] * aim[2]);
    if (length <= 0.0f)
        return false;
    for (int c = 0; c < 3; ++c)
    {
        position[c] = sum[c] / static_cast<float>(lamps);
        direction[c] = aim[c] / length;
    }
    if (spread)
    {
        // The farthest lamp from their middle, across the beam (a car's two lamps; 0.7 m when only one is drawn).
        float most = 0.0f;
        for (int i = 0; i < g_state.count; ++i)
        {
            const Recorded &r = g_state.recorded[i];
            if (!r.spot || memcmp(r.origin, origin, sizeof(r.origin)) != 0)
                continue;
            const float d[3] = {r.position[0] - position[0], r.position[1] - position[1], r.position[2] - position[2]};
            const float along = d[0] * direction[0] + d[1] * direction[1] + d[2] * direction[2];
            const float across2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2] - along * along;
            most = across2 > most * most ? sqrtf(across2) : most;
        }
        *spread = most > 0.2f && most < 1.2f ? most : 0.7f;
    }
    return true;
}

namespace
{
unsigned int PositionKey(const float p[3])
{
    unsigned int h = 2166136261u;
    for (int c = 0; c < 3; ++c)
    {
        unsigned int bits;
        memcpy(&bits, &p[c], sizeof(bits));
        h = (h ^ bits) * 16777619u;
    }
    return h ? h : 1u;
}

/** Takes this frame's recorded world lights into the followed ones: the same position is the same light. */
void Follow()
{
    State &st = g_state;
    for (short &h : st.hash)
        h = -1;
    for (int i = 0; i < st.trackedCount; ++i)
    {
        unsigned int slot = st.tracked[i].key & (kTrackedHash - 1);
        while (st.hash[slot] >= 0)
            slot = (slot + 1) & (kTrackedHash - 1);
        st.hash[slot] = static_cast<short>(i);
    }
    for (int i = 0; i < st.count; ++i)
    {
        const Recorded &r = st.recorded[i];
        if (!r.still)
            continue;
        const unsigned int key = PositionKey(r.position);
        unsigned int slot = key & (kTrackedHash - 1);
        int found = -1;
        while (st.hash[slot] >= 0)
        {
            const Tracked &t = st.tracked[st.hash[slot]];
            if (t.key == key && memcmp(t.r.position, r.position, sizeof(r.position)) == 0)
            {
                found = st.hash[slot];
                break;
            }
            slot = (slot + 1) & (kTrackedHash - 1);
        }
        if (found < 0)
        {
            if (st.trackedCount == kMaxTracked)
                continue;
            found = st.trackedCount++;
            st.hash[slot] = static_cast<short>(found);
            Tracked &t = st.tracked[found];
            t.key = key;
            t.fade = 0.0f;
            t.shown = false;
            t.recordedFrame = 0;
        }
        Tracked &t = st.tracked[found];
        // One lamp's flares drawn twice at one place (halo and core): the brighter.
        if (t.recordedFrame != st.frame ||
            r.color[0] + r.color[1] + r.color[2] > t.r.color[0] + t.r.color[1] + t.r.color[2])
            t.r = r;
        t.recordedFrame = st.frame;
        t.seen = st.time;
    }
}

float Toward(float value, float target, float step)
{
    return target > value ? (value + step < target ? value + step : target)
                          : (value - step > target ? value - step : target);
}
} // namespace

int Collect(GpuLight *out, int capacity, const float camera[3], const float forward[3], bool behindToo)
{
    State &st = g_state;
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    float seconds = st.last.QuadPart ? static_cast<float>(static_cast<double>(now.QuadPart - st.last.QuadPart) /
                                                          static_cast<double>(frequency.QuadPart))
                                     : 0.0f;
    st.last = now;
    seconds = seconds < 0.25f ? seconds : 0.25f;
    st.time += seconds;
    ++st.frame;
    const float step = seconds / kLightFadeSeconds;
    Follow();
    RecordCars(camera);

    float playerOrigin[3];
    const bool havePlayer = PlayerLightOrigin(playerOrigin);
    {
        static const bool debug = nfs::vk::HasArgument("-lightdebug");
        static int had = -1;
        if (debug && had != static_cast<int>(havePlayer))
        {
            int spots = 0;
            for (int i = 0; i < st.count; ++i)
                spots += st.recorded[i].spot;
            nfs::Log("lights: the player's headlights %s at %d %d %d (%d headlight flares recorded)",
                     havePlayer ? "found" : "missing", static_cast<int>(camera[0]), static_cast<int>(camera[1]),
                     static_cast<int>(camera[2]), spots);
            had = static_cast<int>(havePlayer);
        }
    }
    const float thick = nfs::weather::Fog() > nfs::weather::Rain() ? nfs::weather::Fog() : nfs::weather::Rain();
    const float glow = kHeadlightGlowClear + (kHeadlightGlow - kHeadlightGlowClear) * (thick < 1.0f ? thick : 1.0f);
    // Most important first: brightness over distance, the radius keeping near and far lights comparable. A few spare for the
    // ones that merge; every pixel loops over those kept, so a light wholly behind the camera, which lights nothing in view, is
    // left out (unless the mirror looks that way). The candidates: this frame's car lights (index >= 0) and the world's
    // lights followed (-1 - index).
    struct Scored
    {
        int index;
        float score;
    };
    static Scored scored[kMaxKept];
    static unsigned char placed[kMaxTracked];
    const int most = capacity * 2 < kMaxKept ? capacity * 2 : kMaxKept;
    int n = 0;
    for (int i = 0; i < st.count + st.trackedCount; ++i)
    {
        const bool car = i < st.count;
        if (car && st.recorded[i].still)
            continue;
        const Recorded &r = car ? st.recorded[i] : st.tracked[i - st.count].r;
        if (!car)
            placed[i - st.count] = 0;
        const float dx = r.position[0] - camera[0], dy = r.position[1] - camera[1], dz = r.position[2] - camera[2];
        if (!behindToo && dx * forward[0] + dy * forward[1] + dz * forward[2] < -r.radius)
            continue;
        const float brightness = r.color[0] + r.color[1] + r.color[2];
        float score = brightness * r.radius * r.radius / (dx * dx + dy * dy + dz * dz + r.radius * r.radius);
        if (!car && st.tracked[i - st.count].shown)
            score *= kKeepPlace;
        if (n == most && score <= scored[n - 1].score)
            continue;
        int j = n < most ? n++ : most - 1;
        for (; j > 0 && score > scored[j - 1].score; --j)
            scored[j] = scored[j - 1];
        scored[j] = {car ? i : -1 - (i - st.count), score};
    }
    int count = 0;
    auto emit = [&](const Recorded &r, float fade) {
        for (int j = 0; j < count; ++j)
        {
            const float dx = out[j].position[0] - r.position[0], dy = out[j].position[1] - r.position[1],
                        dz = out[j].position[2] - r.position[2];
            if (dx * dx + dy * dy + dz * dz < kMergeDistance * kMergeDistance)
                return false;
        }
        GpuLight &l = out[count++];
        memcpy(l.position, r.position, sizeof(r.position));
        l.position[3] = r.radius;
        for (int c = 0; c < 3; ++c)
            l.color[c] = r.color[c] * fade;
        if (havePlayer && r.spot && memcmp(r.origin, playerOrigin, sizeof(playerOrigin)) == 0)
            for (int c = 0; c < 3; ++c)
                l.color[c] *= kPlayerHeadlightScale;
        l.color[3] = 4.0f / (r.radius * r.radius);
        memcpy(l.direction, r.direction, sizeof(r.direction));
        l.direction[3] = r.spot ? r.outer : -2.0f;
        l.cone[0] = r.spot ? r.inner : 1.0f;
        l.cone[1] = r.player ? 0.0f : -1.0f;
        l.cone[2] = r.cutoff ? 1.0f : 0.0f;
        l.cone[3] = r.cutoff ? glow : 1.0f;
        return true;
    };
    // The places: the most important lights, up to the room left by those fading out; the world's lights among them
    // fade in (or stay in full), or out once held past kHoldSeconds.
    const int reserve = st.fadingOut < capacity / 4 ? st.fadingOut : capacity / 4;
    int k = 0;
    for (; k < n && count < capacity - reserve; ++k)
    {
        const int index = scored[k].index;
        if (index >= 0)
        {
            emit(st.recorded[index], 1.0f);
            continue;
        }
        Tracked &t = st.tracked[-1 - index];
        placed[-1 - index] = 1;
        t.fade = Toward(t.fade, st.time - t.seen <= kHoldSeconds ? 1.0f : 0.0f, step);
        if (t.fade > 0.0f)
            emit(t.r, t.fade);
    }
    // Those that lost their place fade out in the room kept for them, most important first.
    int fadingOut = 0;
    for (; k < n; ++k)
    {
        const int index = scored[k].index;
        if (index >= 0)
            continue;
        Tracked &t = st.tracked[-1 - index];
        placed[-1 - index] = 2;
        t.fade = Toward(t.fade, 0.0f, step);
        if (t.fade > 0.0f && count < capacity && emit(t.r, t.fade))
            ++fadingOut;
    }
    st.fadingOut = fadingOut;
    // Behind the camera a light keeps its fade (it lights nothing in view); a gone one is forgotten once faded.
    int kept = 0;
    for (int i = 0; i < st.trackedCount; ++i)
    {
        Tracked &t = st.tracked[i];
        const bool gone = st.time - t.seen > kHoldSeconds;
        if (!placed[i] && gone)
            t.fade = Toward(t.fade, 0.0f, step);
        t.shown = placed[i] == 1 && t.fade > 0.0f;
        if (gone && t.fade <= 0.0f)
            continue;
        st.tracked[kept++] = t;
    }
    st.trackedCount = kept;
    static DWORD logged;
    if (nfs::vk::HasArgument("-lightdebug") && GetTickCount() - logged > 4000)
    {
        logged = GetTickCount();
        nfs::Log("lights: %d flares recorded, %d lights kept", g_state.count, count);
        // The world's flares, section by section (ScenerySectionHeaders[n] + 8: bounds at 0x30, list at 0x58).
        int sections = 0, flares = 0;
        struct Node { Node *next; };
        const unsigned int listHead = 0x9aa478;
        struct SectionNode { SectionNode *next; SectionNode *prev; int unknown8; int number; };
        const SectionNode *head = reinterpret_cast<const SectionNode *>(listHead);
        for (const SectionNode *s = head->next; s && s != head; s = s->next)
        {
            char *entry = static_cast<char *>(ScenerySectionHeaders[s->number]);
            char *flareSection = entry ? *reinterpret_cast<char **>(entry + 8) : nullptr;
            if (!flareSection)
                continue;
            ++sections;
            Node *list = reinterpret_cast<Node *>(flareSection + 0x58);
            for (Node *n = list->next; n && n != list; n = n->next)
                ++flares;
        }
        nfs::Log("lights: %d loaded sections have flares, %d world flares; DrawLightFlares %d", sections, flares,
                 DrawLightFlares);
        for (int i = 0; i < count && i < 8; ++i)
            nfs::Log("lights:   %d at %d %d %d radius %d colour %d %d %d dir %d %d %d cone %d (x100 for colour, dir)", i,
                     static_cast<int>(out[i].position[0]), static_cast<int>(out[i].position[1]),
                     static_cast<int>(out[i].position[2]), static_cast<int>(out[i].position[3]),
                     static_cast<int>(out[i].color[0] * 100), static_cast<int>(out[i].color[1] * 100),
                     static_cast<int>(out[i].color[2] * 100), static_cast<int>(out[i].direction[0] * 100),
                     static_cast<int>(out[i].direction[1] * 100), static_cast<int>(out[i].direction[2] * 100),
                     static_cast<int>(out[i].direction[3] * 100));
    }
    g_state.count = 0;
    g_state.carsRecorded = false;
    return count;
}

float SeenThroughHaze(const eView *view, const eLightFlare *flare, const bMatrix4 *matrix)
{
    if (nfs::timeofday::HazeExtinction() <= 0.0f || !view || !view->pCamera || !flare || !matrix)
        return 1.0f;
    float p[3];
    Transform(reinterpret_cast<const FlareFields *>(flare)->position, *matrix, 1.0f, p);
    const bVector3 &eye = view->pCamera->CurrentKey.Position;
    const float d[3] = {p[0] - eye.x, p[1] - eye.y, p[2] - eye.z};
    return nfs::timeofday::HazeTransmittance(sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
}
} // namespace nfs::lights

/** Every light flare drawn (original 0x505380), recorded as a light for the player view's next frame, and dimmed by the
 * haze between it and the camera (thick in fog). */
int __cdecl eRenderLightFlare(eView *view, eLightFlare *flare, bMatrix4 *matrix, float intensity,
                              eLightReflexionType reflexion, flareType type, float reflectionHeight,
                              unsigned int color, float scale)
{
    nfs::lights::Record(view, flare, matrix, intensity, static_cast<int>(type));
    intensity *= nfs::lights::SeenThroughHaze(view, flare, matrix);
    return NFS_ORIGINAL(0x505380, int(__cdecl *)(eView *, eLightFlare *, bMatrix4 *, float, eLightReflexionType, flareType,
                                                 float, unsigned int, float))(view, flare, matrix, intensity, reflexion,
                                                                               type, reflectionHeight, color, scale);
}
