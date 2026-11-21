/**
 * Weather (gameplay/world/weather.h, docs/WEATHER.md): rain that comes and goes with the clock, from a drizzle to a downpour, fog
 * on mornings and nights, and how wet the roads are. Not in the original game, where it rained only where the weather
 * map said (and the race scripts allowed), the same in every session.
 *
 * The weather has a time of its own: hours of the clock since the session began, moving on with it (MinutesPerHour
 * real minutes to the hour) and jumping with it (F7, a tour's stop). Rain comes from smooth noise over that time: where
 * the noise is over the threshold the hour's RainChance gives (GLOBAL\TIMEOFDAY.TXT), it rains, about RainChance of the
 * time, in spells of a few times RainSpell hours, and a second noise swells and eases the rain within a spell, from a
 * drizzle to a downpour. The clouds gather before the rain and stay a while after it. Fog is a third noise against the
 * hour's FogChance, thinning out as rain sets in. In the heaviest rain lightning flashes now and then.
 *
 * The game's rain (Rain::UpdateAndRender: the drops, their sound, the overcast lighting) takes the rain and the clouds
 * from the two functions it asks at the camera (0x7539d0, 0x753ac0), replaced here, and the roads' wetness from
 * ApplyToRain after each update; the light scattering takes the fog and the rain's haze (TimeOfDay.cpp ApplyWeather).
 * -weatherdebug logs the weather every few seconds.
 */
#include <math.h>
#include <windows.h>

#include "game/gen/api.h"
#include "game/platform/Platform.h"
#include "core/config.h"
#include "engine/rendering/display/display.h"
#include "legacy/link.h"
#include "core/log.h"
#include "gameplay/world/timeofday.h"
#include "engine/rendering/vulkan/renderer.h"
#include "gameplay/world/weather.h"

NFS_REPLACE(0x7539d0, sub_7539D0)
NFS_REPLACE(0x753ac0, sub_753AC0)

extern "C" int TheGameFlowManager_CurrentGameFlowState;
/** g_VideoOptions.RainEnable (the Video page's Rain Effect). */
extern "C" int lpData_00901810;

namespace nfs::weather
{
namespace
{
enum Mode
{
    kCycle,
    kClear,
    kCloudy,
    kRain,
    kStorm,
    kFog,
    kGame,
    kModes
};

const char *const kModeNames[kModes] = {"cycle", "clear", "cloudy", "rain", "storm", "fog", "game"};
/** RainChance without TIMEOFDAY.TXT's keys. */
const float kDefaultRainChance = 0.35f;
/** The noises: the rain's two octaves (the second this much finer, with this share), the rain's strength, the fog. */
const unsigned int kRainNoise = 1, kRainFineNoise = 2, kStrengthNoise = 3, kFogNoise = 4;
const float kFineScale = 0.37f, kFineShare = 0.35f;
/** Rain sets in and eases off over this much of its noise's range (a quarter of an hour or so); the clouds gather over
 * kCloudLead of it before. */
const float kRainEdge = 0.06f, kCloudLead = 0.2f;
/** The rain's strength changes over about this many hours, from kLightest (a drizzle) to kHeaviestRain. */
const float kStrengthHours = 0.9f, kLightest = 0.2f;
/** Fog sets in over this much of its noise's range; rain from kFogRainFrom to kFogRainTo clears it. */
const float kFogEdge = 0.08f, kFogRainFrom = 0.05f, kFogRainTo = 0.3f;
/** The fixed kinds' weather. */
const float kCloudyCloud = 0.85f, kSteadyRain = 0.6f, kFogCloud = 0.3f;
/** Hours of the clock: the roads soak in a few minutes of rain and dry in about forty after it; water stands on them in
 * rain over kPuddleRain, gathering over kPuddlingHours and drying in kPuddleDryingHours. */
const float kWettingHours = 0.05f, kDryingHours = 0.6f, kPuddleRain = 0.4f, kPuddlingHours = 0.3f;
const float kPuddleDryingHours = 2.0f;
/** A change of kind eases over this many real seconds. */
const float kEaseSeconds = 2.0f;
/** Lightning: in rain at least this hard, a flash every kLightningInterval real seconds or so (0.4 to 1.6 times it),
 * each a few flickers (kFlickers, at these times into the flash), each rising over kFlickerRise and fading over
 * kFlickerSeconds. A flicker that came on in one frame lit the whole scene at once, a jump rather than a flash. */
const float kLightningRain = 1.1f, kLightningInterval = 14.0f;
/** A flash's thunder rolls in this long after it at most (real seconds, evenly spread from 0: the bolt near or far);
 * one still waiting this long past its time (the rain's sound could not start it) is dropped. */
const float kThunderDelay = 5.0f, kThunderLate = 3.0f;
const int kFlickers = 3;
const float kFlickerAt[kFlickers] = {0.0f, 0.2f, 0.42f};
const float kFlickerRise[kFlickers] = {0.07f, 0.05f, 0.06f};
const float kFlickerSeconds = 0.22f;
/** The noise's distribution, for turning a chance into a threshold. */
const int kBins = 256;
const int kSamples = 8192;
/** The game's rain object: the rain at 0x28c, the roads' wetness at 0x36a0. The scripts' rain (Lua's
 * Game_SetChanceOfRain, SetOverRideRainIntensity) is at 0x9b0a48: above 0 it rains that hard. */
const int kRainIntensity = 0x28c;
const int kRoadWetness = 0x36a0;
float *const kScriptedRain = reinterpret_cast<float *>(0x9b0a48);
/** The rain and clouds the rain object eases towards (0x3694, 0x3698), which Rain::UpdateAndRender sets from 0x7539d0
 * and 0x753ac0 only while the weather map's rain and cloud layers (0x9b2a9c, 0x9b0c48) are loaded. In the views above
 * kChaseViews (bumper, hood) it takes kCarCameraRain of the rain. */
const int kRainTarget = 0x3694, kCloudTarget = 0x3698;
void *const *const kRainLayer = reinterpret_cast<void *const *>(0x9b2a9c);
void *const *const kCloudLayer = reinterpret_cast<void *const *>(0x9b0c48);
const int kChaseViews = 2;
const float kCarCameraRain = 0.125f;
const int kPlayerView = 1;
const int kGameFlowLoadingFrontEnd = 1;
const int kGameFlowFrontEnd = 3;
const int kViewMover = 0x68;

struct State
{
    bool started;
    int mode;
    unsigned int seed;
    /** The weather's time (hours of the clock), the clock's hour at the last update, and the last update's count. */
    double hours;
    float clockHour;
    LARGE_INTEGER last;
    LARGE_INTEGER frequency;
    float rain, cloud, fog, wet, puddles;
    /** Real seconds since the start; the lightning: when the next flash comes, when the last began, its brightness now
     * and its count (which varies the flashes). */
    double seconds;
    double nextFlash, flashStart;
    /** When the last flash's thunder rolls in (0: none waiting). */
    double thunderAt;
    float flash;
    unsigned int flashes;
    bool lightning;
    /** The share of the time the rain's and the fog's noise is over each bin's lower edge. */
    float rainOver[kBins + 1];
    float fogOver[kBins + 1];
    bool key;
};

State g_state;

float Hash(unsigned int n)
{
    n = (n << 13) ^ n;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    return static_cast<float>(n & 0x7fffffffu) / 2147483648.0f;
}

/** Smooth value noise in [0, 1]: random values at whole numbers, eased between. */
float Noise(double x, unsigned int which)
{
    const double whole = floor(x);
    const float f = static_cast<float>(x - whole);
    const unsigned int i = static_cast<unsigned int>(static_cast<int>(whole));
    const unsigned int salt = g_state.seed * 0x85ebca6bu + which * 0xc2b2ae35u;
    const float a = Hash(i * 0x9e3779b1u ^ salt), b = Hash((i + 1u) * 0x9e3779b1u ^ salt);
    return a + (b - a) * f * f * (3.0f - 2.0f * f);
}

float Smoothstep(float edge0, float edge1, float x)
{
    float t = (x - edge0) / (edge1 - edge0);
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return t * t * (3.0f - 2.0f * t);
}

float RainSignal(double hours)
{
    const double spell = nfs::timeofday::RainSpell();
    return (1.0f - kFineShare) * Noise(hours / spell, kRainNoise) +
           kFineShare * Noise(hours / (spell * kFineScale) + 31.7, kRainFineNoise);
}

float FogSignal(double hours)
{
    return Noise(hours / nfs::timeofday::FogSpell(), kFogNoise);
}

/** The share of the time `signal` is over each bin's lower edge, from samples over a long stretch of weather. */
template <typename F> void Distribution(F signal, float *over)
{
    int counts[kBins] = {};
    for (int i = 0; i < kSamples; ++i)
    {
        const float v = signal(i * 0.731);
        int bin = static_cast<int>(v * kBins);
        bin = bin < 0 ? 0 : bin >= kBins ? kBins - 1 : bin;
        ++counts[bin];
    }
    int above = kSamples;
    for (int b = 0; b <= kBins; ++b)
    {
        over[b] = static_cast<float>(above) / kSamples;
        if (b < kBins)
            above -= counts[b];
    }
}

/** The level a signal is over `chance` of the time. */
float Threshold(const float *over, float chance)
{
    if (chance <= 0.0f)
        return 2.0f;
    for (int b = 0; b < kBins; ++b)
        if (over[b + 1] < chance)
        {
            const float span = over[b] - over[b + 1];
            const float t = span > 0.0f ? (over[b] - chance) / span : 0.0f;
            return (static_cast<float>(b) + t) / kBins;
        }
    return 1.0f;
}

void Start()
{
    g_state.started = true;
    char mode[16];
    nfs::config::String("World", "Weather", "cycle", mode, sizeof(mode));
    g_state.mode = kCycle;
    for (int m = 0; m < kModes; ++m)
        if (!lstrcmpiA(mode, kModeNames[m]))
            g_state.mode = m;
    QueryPerformanceFrequency(&g_state.frequency);
    QueryPerformanceCounter(&g_state.last);
    g_state.lightning = nfs::config::Int("World", "Lightning", 1) != 0;
    g_state.flashStart = -100.0;
    const int seed = nfs::config::Int("World", "WeatherSeed", 0);
    g_state.seed = seed ? static_cast<unsigned int>(seed) : static_cast<unsigned int>(g_state.last.LowPart * 2654435761u);
    g_state.clockHour = nfs::timeofday::Hour();
    Distribution(RainSignal, g_state.rainOver);
    Distribution(FogSignal, g_state.fogOver);
    nfs::Log("weather: %s, seed %u", kModeNames[g_state.mode], g_state.seed);
}

/** The weather the kind and the time give: rain, clouds, fog. */
void Targets(float &rain, float &cloud, float &fog)
{
    rain = cloud = fog = 0.0f;
    switch (g_state.mode)
    {
    case kCloudy:
        cloud = kCloudyCloud;
        return;
    case kRain:
        rain = kSteadyRain;
        cloud = 1.0f;
        return;
    case kStorm:
        rain = kHeaviestRain;
        cloud = 1.0f;
        return;
    case kFog:
        fog = 1.0f;
        cloud = kFogCloud;
        return;
    case kCycle:
        break;
    default:
        return;
    }
    const double hours = g_state.hours;
    const float signal = RainSignal(hours);
    const float threshold = Threshold(g_state.rainOver, nfs::timeofday::RainChance(kDefaultRainChance));
    const float spell = Smoothstep(threshold, threshold + kRainEdge, signal);
    const float strength = Noise(hours / kStrengthHours + 7.3, kStrengthNoise);
    rain = spell * (kLightest + (kHeaviestRain - kLightest) * strength * sqrtf(strength));
    cloud = Smoothstep(threshold - kCloudLead, threshold, signal);
    const float fogThreshold = Threshold(g_state.fogOver, nfs::timeofday::FogChance());
    fog = Smoothstep(fogThreshold, fogThreshold + kFogEdge, FogSignal(hours)) *
          (1.0f - Smoothstep(kFogRainFrom, kFogRainTo, rain));
}

void PollKey()
{
    const bool key = nfs::display::DebugKeyDown(VK_F7) && nfs::display::DebugKeyDown(VK_CONTROL);
    if (key && !g_state.key)
    {
        g_state.mode = (g_state.mode + 1) % kModes;
        nfs::Log("weather: %s", kModeNames[g_state.mode]);
    }
    g_state.key = key;
}

float Clamp01(float v)
{
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

/** The lightning: flashes come while it rains hard enough, at uneven times; a flash is a few flickers, the first the
 * brightest, of a strength of its own (the bolt near or far). */
void UpdateLightning(float seconds)
{
    g_state.seconds += seconds;
    const double now = g_state.seconds;
    if (!g_state.lightning || g_state.mode == kGame || g_state.rain < kLightningRain)
        g_state.nextFlash = 0.0;
    else if (g_state.nextFlash <= 0.0)
        g_state.nextFlash = now + kLightningInterval * (0.4f + 1.2f * Hash(g_state.seed + g_state.flashes * 7919u));
    else if (now >= g_state.nextFlash)
    {
        g_state.flashStart = now;
        ++g_state.flashes;
        g_state.thunderAt = now + kThunderDelay * Hash(g_state.seed ^ (g_state.flashes * 31337u));
        g_state.nextFlash = now + kLightningInterval * (0.4f + 1.2f * Hash(g_state.seed + g_state.flashes * 7919u));
        static const bool debug = nfs::vk::HasArgument("-weatherdebug");
        if (debug)
            nfs::Log("weather: lightning %u", g_state.flashes);
    }
    const float age = static_cast<float>(now - g_state.flashStart);
    const float strength = 0.4f + 0.6f * Hash(g_state.seed ^ (g_state.flashes * 104729u));
    float flash = 0.0f;
    for (int k = 0; k < kFlickers; ++k)
    {
        const float x = age - kFlickerAt[k];
        if (x < 0.0f || x >= kFlickerRise[k] + kFlickerSeconds)
            continue;
        float shape;
        if (x < kFlickerRise[k])
        {
            const float r = x / kFlickerRise[k];
            shape = r * r * (3.0f - 2.0f * r);
        }
        else
        {
            const float t = 1.0f - (x - kFlickerRise[k]) / kFlickerSeconds;
            shape = t * t * (3.0f - 2.0f * t);
        }
        const float fade = shape * strength / (1.0f + 0.6f * static_cast<float>(k));
        flash = fade > flash ? fade : flash;
    }
    g_state.flash = flash;
}

/** The front end's scenes (the main menu, the safehouse, the car lot, the customization shop) have the game's own
 * clear weather; the weather goes on meanwhile and is back when the world is. */
bool FrontEnd()
{
    const int state = TheGameFlowManager_CurrentGameFlowState;
    return state >= kGameFlowLoadingFrontEnd && state <= kGameFlowFrontEnd;
}

/** The player view's rain object (the camera's mover), or null. */
char *PlayerRain()
{
    return *reinterpret_cast<char **>(reinterpret_cast<char *>(&eViews[kPlayerView]) + kViewMover);
}
} // namespace

void Update()
{
    if (!g_state.started)
        Start();
    PollKey();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const float seconds =
        static_cast<float>(static_cast<double>(now.QuadPart - g_state.last.QuadPart) / g_state.frequency.QuadPart);
    g_state.last = now;
    const double step = seconds / (60.0 * nfs::timeofday::MinutesPerHour());
    // The clock's jumps (F7, a tour's stop) move the weather as far.
    const float hour = nfs::timeofday::Hour();
    float jump = hour - g_state.clockHour - static_cast<float>(step);
    jump -= 24.0f * floorf(jump / 24.0f + 0.5f);
    if (nfs::timeofday::Enabled() && (jump > 0.05f || jump < -0.05f))
        g_state.hours += jump;
    g_state.clockHour = hour;
    g_state.hours += step;

    float rain, cloud, fog;
    Targets(rain, cloud, fog);
    const float ease = seconds >= kEaseSeconds ? 1.0f : seconds / kEaseSeconds;
    g_state.rain += (rain - g_state.rain) * ease;
    g_state.cloud += (cloud - g_state.cloud) * ease;
    g_state.fog += (fog - g_state.fog) * ease;
    UpdateLightning(seconds);
    const float hours = static_cast<float>(step);
    const bool raining = g_state.rain > kFogRainFrom;
    g_state.wet = Clamp01(g_state.wet + (raining ? g_state.rain / kWettingHours : -1.0f / kDryingHours) * hours);
    g_state.puddles = Clamp01(g_state.puddles + (g_state.rain > kPuddleRain
                                                     ? (g_state.rain - kPuddleRain) / kPuddlingHours
                                                     : -1.0f / kPuddleDryingHours) *
                                                    hours);
    // Debug.Wet and Debug.Puddles hold the roads at that wetness, in any weather (reflections without waiting for rain).
    static const float heldWet = nfs::config::Float("Debug", "Wet", -1.0f);
    static const float heldPuddles = nfs::config::Float("Debug", "Puddles", -1.0f);
    if (heldWet >= 0.0f)
        g_state.wet = heldWet;
    if (heldPuddles >= 0.0f)
        g_state.puddles = heldPuddles;

    static const bool debug = nfs::vk::HasArgument("-weatherdebug");
    static DWORD logged;
    if (debug && GetTickCount() - logged > 4000)
    {
        logged = GetTickCount();
        char line[96];
        Text(line, sizeof(line));
        const char *object = PlayerRain();
        nfs::Log("weather: %s, time %d.%02d h; the game's rain object %d%% (wet %d%%)", line,
                 static_cast<int>(g_state.hours), static_cast<int>(g_state.hours * 100.0) % 100,
                 object ? static_cast<int>(*reinterpret_cast<const float *>(object + kRainIntensity) * 100.0f) : -1,
                 object ? static_cast<int>(*reinterpret_cast<const float *>(object + kRoadWetness) * 100.0f) : -1);
    }
}

float Rain()
{
    if (g_state.mode == kGame)
    {
        const char *rain = PlayerRain();
        return rain ? *reinterpret_cast<const float *>(rain + kRainIntensity) : 0.0f;
    }
    return FrontEnd() ? 0.0f : g_state.rain;
}

void Reconfigure()
{
    if (!g_state.started)
        return;
    char mode[16];
    nfs::config::String("World", "Weather", "cycle", mode, sizeof(mode));
    g_state.mode = kCycle;
    for (int m = 0; m < kModes; ++m)
        if (!lstrcmpiA(mode, kModeNames[m]))
            g_state.mode = m;
    g_state.lightning = nfs::config::Int("World", "Lightning", 1) != 0;
    nfs::Log("weather: %s (options)", kModeNames[g_state.mode]);
}

void StepMode(int direction)
{
    g_state.mode = (g_state.mode + kModes + (direction < 0 ? -1 : 1)) % kModes;
    nfs::Log("weather: %s", kModeNames[g_state.mode]);
}

const char *ModeName()
{
    return kModeNames[g_state.mode];
}

bool RainShown()
{
    return lpData_00901810 != 0;
}

float Cloud()
{
    return g_state.mode == kGame || FrontEnd() ? 0.0f : g_state.cloud;
}

float Fog()
{
    return g_state.mode == kGame || FrontEnd() ? 0.0f : g_state.fog;
}

float Lightning()
{
    return FrontEnd() ? 0.0f : g_state.flash;
}

bool OwnThunder()
{
    return g_state.lightning && g_state.mode != kGame;
}

bool ThunderDue()
{
    if (g_state.thunderAt <= 0.0 || g_state.seconds < g_state.thunderAt)
        return false;
    if (g_state.seconds > g_state.thunderAt + kThunderLate)
    {
        g_state.thunderAt = 0.0;
        return false;
    }
    return !FrontEnd();
}

void ThunderPlayed()
{
    g_state.thunderAt = 0.0;
    static const bool debug = nfs::vk::HasArgument("-weatherdebug");
    if (debug)
        nfs::Log("weather: thunder %d ms after lightning %u", static_cast<int>((g_state.seconds - g_state.flashStart) * 1000.0),
                 g_state.flashes);
}

float Wetness()
{
    if (g_state.mode == kGame)
    {
        const char *rain = PlayerRain();
        return rain ? *reinterpret_cast<const float *>(rain + kRoadWetness) : 0.0f;
    }
    return FrontEnd() ? 0.0f : g_state.wet;
}

float Puddles()
{
    return g_state.mode == kGame || FrontEnd() ? 0.0f : g_state.puddles;
}

void BeforeRain(void *rain)
{
    if (!rain || !g_state.started || g_state.mode == kGame || (*kRainLayer && *kCloudLayer))
        return;
    char *r = static_cast<char *>(rain);
    float target = RainShown() ? Rain() : 0.0f;
    target = target < 1.0f ? target : 1.0f;
    // As the game has it: the drops thin out to an eighth in the cameras on the car.
    if (eGetCurrentViewMode() > kChaseViews)
        target *= kCarCameraRain;
    *reinterpret_cast<float *>(r + kRainTarget) = target;
    *reinterpret_cast<float *>(r + kCloudTarget) = target > 0.0f ? target : Cloud();
}

void ApplyToRain(void *rain)
{
    if (rain && g_state.started && g_state.mode != kGame)
        *reinterpret_cast<float *>(static_cast<char *>(rain) + kRoadWetness) = g_state.wet;
}

/** This file's: whether the weather is the original's (the replaced functions call it then), and the scripts' rain. */
bool GameWeather()
{
    return g_state.started && g_state.mode == kGame;
}

float ScriptedRain()
{
    return *kScriptedRain;
}

bool Text(char *line, int size)
{
    if (!g_state.started || g_state.mode == kGame)
        return false;
    char text[96];
    wsprintfA(text, "WEATHER %s RAIN %d.%02d FOG %d.%02d WET %d.%02d", kModeNames[g_state.mode],
              static_cast<int>(g_state.rain), static_cast<int>(g_state.rain * 100.0f) % 100,
              static_cast<int>(g_state.fog), static_cast<int>(g_state.fog * 100.0f) % 100,
              static_cast<int>(g_state.wet), static_cast<int>(g_state.wet * 100.0f) % 100);
    for (char *c = text; *c; ++c)
        if (*c >= 'a' && *c <= 'z')
            *c = static_cast<char>(*c - 'a' + 'A');
    lstrcpynA(line, text, size);
    return true;
}
void PoseOption(char *text, int size)
{
    text[0] = 0;
    if (!g_state.started || g_state.mode == kGame || size < 32)
        return;
    int kind = g_state.mode;
    if (kind == kCycle)
        kind = g_state.fog > 0.5f              ? kFog
               : g_state.rain > kLightningRain ? kStorm
               : g_state.rain > 0.2f           ? kRain
               : g_state.cloud > 0.5f          ? kCloudy
                                               : kClear;
    wsprintfA(text, " -set World.Weather=%s", kModeNames[kind]);
}
} // namespace nfs::weather

/** The rain at the camera (x, y) the game's rain object asks for each frame: the scripts' when they make it rain, else
 * the weather's (the game's rain goes up to 1). */
extern "C" float __cdecl sub_7539D0(float x, float y)
{
    if (nfs::weather::GameWeather())
        return NFS_ORIGINAL(0x7539d0, float(__cdecl *)(float, float))(x, y);
    // The race scripts' rain (Game_SetChanceOfRain, SetOverRideRainIntensity) belonged to the original's weather map:
    // the weather is the cycle's alone now.
    if (!nfs::weather::RainShown())
        return 0.0f;
    const float rain = nfs::weather::Rain();
    return rain < 1.0f ? rain : 1.0f;
}

/** The clouds at the camera (x, y), which the game's overcast lighting follows: the weather's. */
extern "C" float __cdecl sub_753AC0(float x, float y)
{
    if (nfs::weather::GameWeather())
        return NFS_ORIGINAL(0x753ac0, float(__cdecl *)(float, float))(x, y);
    return nfs::weather::Cloud();
}
