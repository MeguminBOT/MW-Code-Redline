/**
 * Time of day (gameplay/world/timeofday.h, docs/NIGHT.md): a clock that runs through day and night, the sun and the moon crossing
 * the sky with it, each hour's lighting from GLOBAL\TIMEOFDAY.TXT, and Carbon's night sky fading in over the game's at
 * dusk. Not in the original game.
 *
 *     [World] TimeOfDay = cycle | day | night | HH:MM   cycle (the default): the clock runs from StartHour; day: the
 *                                                       game's own lighting; night (01:00) or an hour: it stands there
 *     [World] MinutesPerHour = 5                        real minutes an hour of the clock takes (a day in two hours)
 *     [World] Clock = session | game                    session (the default): the clock runs with the time the game
 *                                                       has been on - menus, pauses, loading and races alike - so
 *                                                       going into a race never moves it and a long session goes
 *                                                       through the day; game: only while the world's time runs
 *     [World] StartHour = 14:00                         where the running clock starts (the game's own afternoon)
 *
 * F7 moves the clock an hour on (with Shift, an hour back); the pose box (F11) shows it, and F9 saves it with the pose.
 *
 * TIMEOFDAY.TXT has one value per line, "Name = numbers", # starting a comment. Before the first key, the sky's path:
 * Sunrise and Sunset (hours), SunHighest and MoonHighest (degrees above the horizon at the middle of the day and of the
 * night; the moon is full, opposite the sun) and North (the world azimuth of north in degrees, which puts the game's
 * own sun at StartHour). Then the keys: "[HH:MM]" and that hour's lighting; between two keys each value goes smoothly
 * from one to the other. The fields: the timeofdaylighting ones at their places in TimeOfDay's lighting block (+0x20,
 * the order 0x768ee0 blends them in) - DiffuseColour (the sun's or the moon's light: N.L, shadowed), AmbientColour (how
 * much of the daylight baked into the world shows), SpecularColour, CarSpecScale, EnvSkyBrightness, FogSkyColour,
 * FogHazeColour, FogSkyColourScale, FogHazeColourScale, FogInLightScatter, FogSunFalloff, FogDistanceScale,
 * FixedFunctionSkyColor - and the rebuild's own: MiddleGray and AdaptFloor (the eye adaptation), SkyBrightness and
 * SkyLayers (the game's sky: STANDARD_BRIGHTNESS and SKY_DIFFUSESCALE), NightSky (how far Carbon's night dome covers the
 * game's sky, 0 to 1), DomeBrightness, SunSprite (the sun's disc and glow, 0 to 1), CarLight (scales the light cars
 * and characters get, on top of following the hour's sun and ambient), TreeLight (the trees' own brightness),
 * LocalLights (how strongly the lamps and the cars' lights shine on the world, 0 by day) and Traffic (how much of the
 * game's traffic is on the roads, 0 to 1: thin at night).
 * Numbers after "x" scale the game's own value, which changes with the place and the weather; plain numbers set the
 * value; a field a key leaves out is the game's own there.
 *
 * Cars and characters take their light from the world's light context (a key light, the sun, and fill lights), not
 * from the time of day block: their key light follows the hour's DiffuseColour and their fill lights its AmbientColour,
 * each against the game's own.
 */
#include <math.h>
#include <string.h>
#include <windows.h>

#include "game/gen/api.h"
#include "core/config.h"
#include "engine/rendering/display/display.h"
#include "engine/rendering/lights/lamps.h"
#include "core/log.h"
#include "gameplay/world/timeofday.h"
#include "engine/rendering/vulkan/renderer.h"
#include "gameplay/world/weather.h"
#include "engine/resources/pack.h"

/** How far the camera is into one of the game's tunnel zones (0 to 1, AmIinATunnel smoothed). */
extern "C" float flt_982BA8;
extern "C" int TheGameFlowManager_CurrentGameFlowState;

namespace
{
const char kPath[] = "GLOBAL\\TIMEOFDAY.TXT";
const int kMaxValues = 4;
const int kMaxKeys = 32;
const float kNightHour = 1.0f;
/** bAngle units (a turn is 65536) per degree. */
const float kAnglePerDegree = 65536.0f / 360.0f;
/** TimeOfDay's lighting block, +0x20 to +0xc0, as floats. */
const int kBlockStart = 0x20;
const int kBlockFloats = (0xc0 - kBlockStart) / 4;
/** TimeOfDay: the sun at its distance (x, y, z, 1) and the direction to it (x, y, z, 0). */
const int kSunPosition = 0x170;
const int kSunDirection = 0x180;
/** The sun's distance in 0x769360. */
const int kGameFlowLoadingFrontEnd = 1;
const int kGameFlowFrontEnd = 3;
const float *const kSunDistance = reinterpret_cast<const float *>(0x905d44);
/** The sun casts the shadows while it is higher than this (degrees); below, the moon does. Around the switch the
 * hour's DiffuseColour is next to nothing, so it does not show. */
const float kLightSwitchElevation = -4.0f;
/** A light context (sub_6C7420): the colours of its lights at +0x10, a 4x4 matrix (LOCALCOLOURMATRIX) whose rows are
 * the lights, the key light first, and its columns red, green, blue (the effect uploads it column-major, so the
 * shaders' registers are its columns); the copy covers what the setter reads. */
const int kLightColours = 0x10;
const int kLightContextSize = 0xa0;
const int kLights = 4;
/** A light context colour follows the hour by at most this factor (a game colour of nearly nothing gives wild
 * ratios). */
const float kMaxLightRatio = 4.0f;

enum Field
{
    kDiffuse,
    kAmbient,
    kSpecular,
    kCarSpecScale,
    kEnvSkyBrightness,
    kFogSkyColour,
    kFogHazeColour,
    kFogSkyColourScale,
    kFogHazeColourScale,
    kFogInLightScatter,
    kFogSunFalloff,
    kFogDistanceScale,
    kFixedFunctionSkyColor,
    kMiddleGray,
    kAdaptFloor,
    kSkyBrightness,
    kSkyLayers,
    kNightSky,
    kDomeBrightness,
    kSunSprite,
    kCarLight,
    kTreeLight,
    kLocalLights,
    kTraffic,
    kTunnelLight,
    kRainChance,
    kFogChance,
    kFields
};

struct FieldInfo
{
    const char *name;
    /** In TimeOfDay, or -1 for the rebuild's own. */
    int offset;
    int count;
};

const FieldInfo kFieldInfo[kFields] = {
    {"DiffuseColour", 0x20, 4},     {"AmbientColour", 0x30, 4},     {"SpecularColour", 0x40, 4},
    {"CarSpecScale", 0x60, 1},      {"EnvSkyBrightness", 0x64, 1},  {"FogSkyColour", 0x70, 4},
    {"FogHazeColour", 0x80, 4},     {"FogSkyColourScale", 0x90, 1}, {"FogHazeColourScale", 0x94, 1},
    {"FogInLightScatter", 0x98, 1}, {"FogSunFalloff", 0x9c, 1},     {"FogDistanceScale", 0xa0, 1},
    {"FixedFunctionSkyColor", 0xb0, 4}, {"MiddleGray", -1, 1},      {"AdaptFloor", -1, 1},
    {"SkyBrightness", -1, 1},       {"SkyLayers", -1, 4},           {"NightSky", -1, 1},
    {"DomeBrightness", -1, 1},      {"SunSprite", -1, 1},           {"CarLight", -1, 1},
    {"TreeLight", -1, 1},           {"LocalLights", -1, 1},         {"Traffic", -1, 1},
    {"TunnelLight", -1, 1},         {"RainChance", -1, 1},          {"FogChance", -1, 1},
};

enum PathField
{
    kSunrise,
    kSunset,
    kSunHighest,
    kMoonHighest,
    kNorth,
    kGameHour,
    kRainSpell,
    kFogSpell,
    kFogVisibility,
    kRainVisibility,
    kPathFields
};

const char *const kPathNames[kPathFields] = {"Sunrise", "Sunset",   "SunHighest",    "MoonHighest",   "North",
                                             "GameHour", "RainSpell", "FogSpell", "FogVisibility", "RainVisibility"};
const float kPathDefaults[kPathFields] = {6.0f, 20.0f, 65.0f, 45.0f, 114.0f, 14.0f, 4.0f, 2.0f, 150.0f, 350.0f};
/** The haze's extinction (per metre) that leaves 2 % of the light after `metres`: what the eye calls the visibility. */
const float kVisibilityExtinction = 3.912f;
/** Fog is about as grey in every colour (droplets far larger than the light's wavelength); the haze's colour is its
 * extinction per colour. */
const float kFogHaze[3] = {0.5f, 0.5f, 0.5f};
/** Fog scatters a little forward (the Henyey-Greenstein g the scattering takes): a glow around the sun or the moon,
 * which clouds take this share of away. */
const float kFogForward = 0.25f;
const float kCloudGlowLoss = 0.6f;
/** Rain thinner than this (gameplay/world/weather.h Rain) leaves the air as it is. */
const float kRainHazeFrom = 0.15f;
/** The share of a fog that stays out of a tunnel the camera is in (the rain's haze stays out whole). */
const float kTunnelFog = 0.5f;
/** Under a full cloud cover the sun keeps this much of what the game's own overcast lighting leaves of it (that cuts it
 * by half at most, and hardly at dawn): an overcast day is lit by the sky, without shadows. */
const float kCloudSun = 0.3f;
/** Dense fog and rain haze are about as bright as a pale wall in the light they take: these shares of the hour's sky
 * light, its sun and, at night, the city's lamps (as strongly as the hour has them on), in the lamps' colour. */
const float kFogSky = 0.6f, kFogSun = 0.35f, kFogLamps = 0.06f;
const float kLampGlow[3] = {1.0f, 0.82f, 0.6f};
/** How much of that light's colour the haze takes (light scattered again and again greys). */
const float kFogColour = 0.25f;
/** The sun's share of clear air's haze light (the sky's is the rest), which the sun's shadows shade. In thick fog and
 * rain haze the light is scattered again and again and comes from all round: no shade, no shafts, and the haze over far
 * things is the sky's own colour (shaded, a far sign or pole stood out darker than the sky behind it). */
const float kClearHazeSunShare = 0.7f;
/** Dense fog keeps this share of the sun's light (it lights the world from all round instead: the haze's own light). */
const float kFogSunLight = 0.3f;
/** A lightning flash at its brightest lights the world as a sky this bright (AmbientColour times its w; bluish white)
 * and brightens the sky this many times over. */
const float kFlashLight[3] = {0.75f, 0.8f, 1.0f};
const float kFlashSky = 4.0f;
/** The Mie phase's constant (1 / 4 pi): FogInLightScatter times it and the Henyey-Greenstein term is the haze's
 * brightness. */
const float kMiePhase = 0.0795774683f;
/** What a car's key light keeps in shade (the sky's share, cooler) at the game's own hour (lighting.hlsli's
 * carShade); the time of day moves it with the hour's sky against its sun. */
const float kCarShadeAtGameHour[3] = {0.24f, 0.28f, 0.36f};
const float kMaxCarShade = 16.0f;
/** The share of a particle's light that is the sun's at the game's own hour: its particle effect gave smoke in shadow
 * half its light. */
const float kParticleSunShare = 0.5f;
/** Under a tunnel's roof the light baked into the world is the game's own hour's times TunnelLight, at most this many
 * times the hour's (a night sky of nearly nothing gives wild ratios). */
const float kMaxTunnelRatio = 8.0f;
/** The game lights each region of the city its own way, all as one fixed afternoon: the northern forest's lighting is a
 * sunset of its own (an orange sun with next to no blue, a deep blue sky light, an orange haze), which the hour's keys
 * (written against the city's) made purple and orange in the evening, the sky yellow-green. A region keeps its
 * brightness and this share of its colour against the city's (kCityColours: the game's own at the debug race's start,
 * which the keys were written against). */
const float kRegionTint = 0.3f;
struct CityColour
{
    int field;
    float rgb[3];
};
const CityColour kCityColours[] = {
    {kDiffuse, {0.6f, 0.48f, 0.22f}},          {kAmbient, {0.19f, 0.24f, 0.32f}},
    {kSpecular, {0.58f, 0.52f, 0.41f}},        {kFogSkyColour, {0.27f, 0.46f, 1.0f}},
    {kFogHazeColour, {0.18f, 0.24f, 0.37f}},   {kFixedFunctionSkyColor, {0.86f, 0.95f, 1.0f}},
};
/** The cars' light rigs go into a tunnel's light and out of it over this many seconds as the camera does. */
const float kTunnelEase = 0.3f;

/** How a key gives a field: the game's own value, the game's times its numbers, or its numbers. */
enum How : unsigned char
{
    kGame,
    kTimes,
    kSet
};

struct Key
{
    float hour;
    How how[kFields];
    float values[kFields][kMaxValues];
};

enum Mode
{
    kUnread = -1,
    kGameDay,
    kCycle,
    kFixed
};

struct Place
{
    float elevation;
    float azimuth;
};

struct State
{
    int mode;
    Key keys[kMaxKeys];
    int count;
    float path[kPathFields];
    float hour;
    float minutesPerHour;
    /** The running clock: `base` hours at `baseTick` (GetTickCount64) on the session clock; on the game's clock `base`
     * moves on by each frame's world time. */
    bool gameClock;
    double base;
    ULONGLONG baseTick;
    int frame;
    bool forward, back;
    /** The keys around the clock and how far between them (smoothed). */
    int before, after;
    float blend;
    float located;
    Place sun, moon;
    /** The game's lighting block before the hour's went over it, and what the hour made of it. */
    float game[kBlockFloats];
    /** The game's block as it set it (game holds it with the region's colour cast mostly taken out). */
    float raw[kBlockFloats];
    float hours[kBlockFloats];
    bool haveGame;
    /** The light context's key and fill lights at this hour against the game's own hour (red, green, blue), and what
     * a car's key light keeps in shade. */
    float key[3], fill[3];
    float carShade[3];
    /** The hour's sun and sky against the game's own hour (red, green, blue), for particles and the grass blades, and
     * the w of its AmbientColour against the game's own hour's. */
    float sunRatio[3], skyRatio[3];
    float skyW;
    /** What the light baked into the world takes under a tunnel's roof (red, green, blue). */
    float tunnel[3];
    /** How far the camera is under a tunnel's roof, eased, and the frame it was eased last. */
    float cameraTunnel;
    int tunnelFrame;
    /** The haze's colour against grey (fog takes the colour of the light it scatters), its extinction per metre and
     * the sun's share of its light. */
    float hazeTint[3];
    float extinction;
    float hazeSunShare;
    /** The light thick haze takes (a pale wall's in the hour's light), for the falling rain. */
    float hazeLight[3];
    /** How thick the haze is: fog's or a downpour's, whichever is thicker (0 to 1); the sky fades into it. */
    float thickness;
};

State g_state = {kUnread};

float ParseNumber(const char *&p)
{
    while (*p == ' ' || *p == '\t' || *p == ',')
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

/** "14:30" or "14.5" as hours; -1 when it is neither. */
float ParseHour(const char *p)
{
    while (*p == ' ' || *p == '\t')
        ++p;
    if (!IsNumber(*p))
        return -1.0f;
    float hour = ParseNumber(p);
    if (*p == ':')
    {
        ++p;
        hour += ParseNumber(p) / 60.0f;
    }
    return hour >= 0.0f && hour <= 24.0f ? hour : -1.0f;
}

float Wrap(float hour)
{
    hour -= 24.0f * static_cast<float>(static_cast<int>(hour / 24.0f));
    return hour < 0.0f ? hour + 24.0f : hour;
}

unsigned short Angle(float degrees)
{
    return static_cast<unsigned short>(static_cast<int>(degrees * kAnglePerDegree));
}

void ParseField(Key *key, const char *name, const char *p)
{
    for (int i = 0; i < kPathFields; ++i)
    {
        if (!key && !lstrcmpiA(name, kPathNames[i]) && IsNumber(*p))
        {
            g_state.path[i] = ParseNumber(p);
            return;
        }
    }
    if (!key)
        return;
    for (int f = 0; f < kFields; ++f)
    {
        if (lstrcmpiA(name, kFieldInfo[f].name))
            continue;
        How how = kSet;
        if (*p == 'x' || *p == 'X')
        {
            how = kTimes;
            ++p;
            while (*p == ' ' || *p == '\t')
                ++p;
        }
        int n = 0;
        for (; n < kFieldInfo[f].count && IsNumber(*p); ++n)
        {
            key->values[f][n] = ParseNumber(p);
            while (*p == ' ' || *p == '\t' || *p == ',')
                ++p;
        }
        // A colour given as rgb keeps the game's w (times: 1).
        for (; n < kFieldInfo[f].count; ++n)
            key->values[f][n] = how == kTimes ? 1.0f : -1.0f;
        key->how[f] = how;
        return;
    }
    nfs::Log("time of day: no field %s", name);
}

void Load()
{
    for (int i = 0; i < kPathFields; ++i)
        g_state.path[i] = kPathDefaults[i];
    uint32_t read = 0;
    char *text = reinterpret_cast<char *>(nfs::pack::ReadFile(kPath, read));
    if (!text)
    {
        nfs::Log("time of day: no %s, the game's own lighting", kPath);
        return;
    }
    Key *key = nullptr;
    for (char *line = text; *line;)
    {
        char *end = line;
        while (*end && *end != '\n')
            ++end;
        const char saved = *end;
        *end = 0;
        const char *p = line;
        while (*p == ' ' || *p == '\t')
            ++p;
        if (*p == '[')
        {
            const float hour = ParseHour(p + 1);
            if (hour < 0.0f || g_state.count == kMaxKeys)
                nfs::Log("time of day: key %s left out", p);
            else
            {
                key = &g_state.keys[g_state.count++];
                memset(key, 0, sizeof(*key));
                key->hour = Wrap(hour);
            }
        }
        else if (*p && *p != '#' && *p != '\r')
        {
            char name[48];
            int n = 0;
            while (*p && *p != ' ' && *p != '\t' && *p != '=' && *p != '\r' && n < 47)
                name[n++] = *p++;
            name[n] = 0;
            while (*p == ' ' || *p == '\t' || *p == '=')
                ++p;
            ParseField(key, name, p);
        }
        *end = saved;
        line = *end ? end + 1 : end;
    }
    // In hour order.
    for (int i = 1; i < g_state.count; ++i)
        for (int j = i; j > 0 && g_state.keys[j].hour < g_state.keys[j - 1].hour; --j)
        {
            Key t = g_state.keys[j];
            g_state.keys[j] = g_state.keys[j - 1];
            g_state.keys[j - 1] = t;
        }
    ::operator delete(text);
    nfs::Log("time of day: %d keys from %s", g_state.count, kPath);
}

void Init()
{
    if (g_state.mode != kUnread)
        return;
    for (int i = 0; i < kPathFields; ++i)
        g_state.path[i] = kPathDefaults[i];
    char mode[16];
    nfs::config::String("World", "TimeOfDay", "cycle", mode, sizeof(mode));
    char start[16];
    nfs::config::String("World", "StartHour", "14:00", start, sizeof(start));
    g_state.minutesPerHour = nfs::config::Float("World", "MinutesPerHour", 5.0f);
    char clock[16];
    nfs::config::String("World", "Clock", "session", clock, sizeof(clock));
    g_state.gameClock = !lstrcmpiA(clock, "game");
    g_state.hour = ParseHour(start) >= 0.0f ? ParseHour(start) : 14.0f;
    g_state.base = g_state.hour;
    g_state.baseTick = GetTickCount64();
    g_state.located = -1.0f;
    if (!lstrcmpiA(mode, "day"))
        g_state.mode = kGameDay;
    else if (!lstrcmpiA(mode, "night"))
    {
        g_state.mode = kFixed;
        g_state.hour = kNightHour;
    }
    else if (ParseHour(mode) >= 0.0f)
    {
        g_state.mode = kFixed;
        g_state.hour = Wrap(ParseHour(mode));
    }
    else
        g_state.mode = kCycle;
    if (g_state.mode != kGameDay)
        Load();
    if (!g_state.count)
        g_state.mode = kGameDay;
}

/** The keys around `hour` and how far between them, smoothed. */
void Around(float hour, int &before, int &after, float &blend)
{
    const int n = g_state.count;
    after = 0;
    while (after < n && g_state.keys[after].hour <= hour)
        ++after;
    before = (after + n - 1) % n;
    after %= n;
    const float span = Wrap(g_state.keys[after].hour - g_state.keys[before].hour);
    const float t = span > 0.0f ? Wrap(hour - g_state.keys[before].hour) / span : 0.0f;
    blend = t * t * (3.0f - 2.0f * t);
}

/** The keys around the clock's hour. */
void Locate()
{
    if (g_state.located == g_state.hour)
        return;
    g_state.located = g_state.hour;
    Around(g_state.hour, g_state.before, g_state.after, g_state.blend);
}

float KeyValue(const Key &key, int field, int i, float game)
{
    switch (key.how[field])
    {
    case kTimes:
        return game * key.values[field][i];
    case kSet:
        return key.values[field][i] >= 0.0f || kFieldInfo[field].count == 1 ? key.values[field][i] : game;
    default:
        return game;
    }
}

/** The hour's value of a field with the clock on, wherever the game is (the weather's chances run on in the front
 * end), `game` the game's own. */
float ClockValue(int field, int i, float game)
{
    if (!nfs::timeofday::Enabled())
        return game;
    Locate();
    const float a = KeyValue(g_state.keys[g_state.before], field, i, game);
    const float b = KeyValue(g_state.keys[g_state.after], field, i, game);
    return a + (b - a) * g_state.blend;
}

/** The hour's value of a field for lighting the frame, `game` the game's own (in the front end too). */
float Value(int field, int i, float game)
{
    return nfs::timeofday::Lit() ? ClockValue(field, i, game) : game;
}

/** A field's value at `hour` instead of the clock's. */
float ValueAt(float hour, int field, int i, float game)
{
    int before, after;
    float blend;
    Around(Wrap(hour), before, after, blend);
    const float a = KeyValue(g_state.keys[before], field, i, game);
    const float b = KeyValue(g_state.keys[after], field, i, game);
    return a + (b - a) * blend;
}

/** Fog and the rain's haze (gameplay/world/weather.h) thicken the air: the haze's extinction rises from the hour's towards what
 * leaves FogVisibility metres of sight in the densest fog and RainVisibility in the heaviest rain (geometrically, so
 * half a fog is halfway on the eye's scale), and thick haze turns grey and scatters a little forward. Thick haze
 * is about as bright as a pale wall in the light it takes in (kFogSky, kFogSun, kFogLamps), and of its colour (the
 * haze's tint, which the light scattering takes). Clouds dim the sun, and a lightning flash lights the world from the
 * sky for a moment. The light scattering (source/engine/rendering/materials/LightScattering.cpp) gives the world and the sky the rest. */
void ApplyWeather(float *block)
{
    for (int c = 0; c < 3; ++c)
    {
        g_state.hazeTint[c] = 1.0f;
        g_state.hazeLight[c] = 0.0f;
    }
    g_state.extinction = 0.0f;
    g_state.hazeSunShare = kClearHazeSunShare;
    g_state.thickness = 0.0f;
    const float cloud = nfs::weather::Cloud(), sunFog = nfs::weather::Fog();
    if (cloud > 0.0f || sunFog > 0.0f)
    {
        float *diffuse = block + (0x20 - kBlockStart) / 4, *specular = block + (0x40 - kBlockStart) / 4;
        const float sun = (1.0f - (1.0f - kCloudSun) * cloud) * (1.0f - (1.0f - kFogSunLight) * sunFog);
        for (int c = 0; c < 3; ++c)
        {
            diffuse[c] *= sun;
            specular[c] *= sun;
        }
    }
    // Lightning lights the world from the whole sky for a moment.
    const float flash = nfs::weather::Lightning();
    if (flash > 0.0f)
    {
        float *ambient = block + (0x30 - kBlockStart) / 4;
        const float w = ambient[3] > 0.0f ? ambient[3] : 1.0f;
        for (int c = 0; c < 3; ++c)
            ambient[c] += kFlashLight[c] * flash / w;
    }
    float *sky = block + (0x70 - kBlockStart) / 4;
    float *haze = block + (0x80 - kBlockStart) / 4;
    float &skyScale = block[(0x90 - kBlockStart) / 4];
    float &hazeScale = block[(0x94 - kBlockStart) / 4];
    float &forward = block[(0x9c - kBlockStart) / 4];
    const float distanceScale = block[(0xa0 - kBlockStart) / 4];
    if (distanceScale <= 0.0f || sky[1] <= 0.0f)
        return;
    const float rayleigh = (skyScale + 1.0f) * 1e-4f * sky[1];
    const float now = (rayleigh + (hazeScale + 1.0f) * 1e-4f * (haze[1] > 0.0f ? haze[1] : 0.0f)) * distanceScale;
    g_state.extinction = now > 0.0f ? now : 0.0f;
    const float *ambient = block + (0x30 - kBlockStart) / 4, *diffuse = block + (0x20 - kBlockStart) / 4;
    float lamps = Value(kLocalLights, 0, 0.0f);
    lamps = lamps < 0.0f ? 0.0f : lamps > 1.0f ? 1.0f : lamps;
    float light[3];
    for (int c = 0; c < 3; ++c)
    {
        light[c] = kFogSky * ambient[c] * ambient[3] + kFogSun * diffuse[c] + kFogLamps * lamps * kLampGlow[c];
        g_state.hazeLight[c] = light[c];
    }
    // In a tunnel the rain's haze is outside (the camera's cover, eased), and only some of a fog drifts in.
    const float inside = g_state.cameraTunnel;
    const float fog = nfs::weather::Fog() * (1.0f - kTunnelFog * inside);
    const float rain = nfs::weather::Rain();
    float downpour = (rain - kRainHazeFrom) / (nfs::weather::kHeaviestRain - kRainHazeFrom);
    downpour = (downpour < 0.0f ? 0.0f : downpour > 1.0f ? 1.0f : downpour) * (1.0f - inside);
    if ((fog <= 0.0f && downpour <= 0.0f) || now <= 0.0f)
        return;
    float wanted = now;
    if (fog > 0.0f)
    {
        const float dense = kVisibilityExtinction / g_state.path[kFogVisibility];
        const float f = static_cast<float>(pow(static_cast<double>(dense / now), static_cast<double>(fog)));
        wanted = now * f > wanted ? now * f : wanted;
    }
    if (downpour > 0.0f)
    {
        const float dense = kVisibilityExtinction / g_state.path[kRainVisibility];
        const float f = static_cast<float>(pow(static_cast<double>(dense / now), static_cast<double>(downpour)));
        wanted = now * f > wanted ? now * f : wanted;
    }
    // Fog droplets and raindrops are far larger than the light's wavelength: thick haze takes every colour alike (the
    // hour's haze colour put an orange cast over everything in an evening downpour) and scatters only a little forward.
    const float thick = fog > downpour ? fog : downpour;
    g_state.thickness = thick;
    for (int c = 0; c < 3; ++c)
        haze[c] += (kFogHaze[c] - haze[c]) * thick;
    // The glow towards the sun fades as the clouds hide it (a storm's haze is about as bright all round).
    forward += (kFogForward * (1.0f - kCloudGlowLoss * nfs::weather::Cloud()) - forward) * thick;
    const float mie = wanted / distanceScale - rayleigh;
    if (mie > 0.0f && haze[1] > 0.0f)
        hazeScale = mie / (1e-4f * haze[1]) - 1.0f;
    g_state.extinction = wanted;
    const float brightness = 0.3f * light[0] + 0.59f * light[1] + 0.11f * light[2];
    if (brightness <= 0.0f)
        return;
    // FogInLightScatter for that brightness at right angles to the sun, where the Xbox 360's Mie phase is
    // (1 - g)^2 / (1 + g^2)^1.5 / (4 pi).
    const float g = forward, spread = 1.0f + g * g;
    const float side = kMiePhase * (1.0f - g) * (1.0f - g) / (spread * sqrtf(spread));
    float &scatter = block[(0x98 - kBlockStart) / 4];
    scatter += (brightness / side - scatter) * thick;
    for (int c = 0; c < 3; ++c)
        g_state.hazeTint[c] = 1.0f + (light[c] / brightness - 1.0f) * kFogColour * thick;
    g_state.hazeSunShare *= 1.0f - thick;
}

/** Where the sun or the moon is at `hour` on its arc: from the eastern horizon at `rise` over the south to the western
 * at `set`, `highest` degrees up halfway; below the horizon the rest of the day. */
Place Arc(float hour, float rise, float set, float highest)
{
    const float up = Wrap(set - rise);
    const float since = Wrap(hour - rise);
    Place place;
    float compass;
    if (since <= up)
    {
        const float f = since / up;
        place.elevation = highest * bSin(Angle(180.0f * f));
        compass = 90.0f + 180.0f * f;
    }
    else
    {
        const float f = (since - up) / (24.0f - up);
        place.elevation = -highest * bSin(Angle(180.0f * f));
        compass = 270.0f + 180.0f * f;
    }
    // Compass bearings turn clockwise from north; world azimuths counterclockwise from +X.
    place.azimuth = g_state.path[kNorth] - compass;
    return place;
}

void Direction(const Place &place, float *out)
{
    const unsigned short a = Angle(place.azimuth), e = Angle(place.elevation);
    out[0] = bCos(e) * bCos(a);
    out[1] = bCos(e) * bSin(a);
    out[2] = bSin(e);
}

bool Running()
{
    return g_state.mode == kCycle && g_state.minutesPerHour > 0.0f;
}

/** Puts the clock at `hour`; a running clock goes on from there. */
void SetClock(float hour)
{
    g_state.hour = Wrap(hour);
    g_state.base = g_state.hour;
    g_state.baseTick = GetTickCount64();
}

/** The running clock's hour now, on the session clock. */
void Tick()
{
    if (!Running() || g_state.gameClock)
        return;
    double hour = g_state.base + static_cast<double>(GetTickCount64() - g_state.baseTick) /
                                     (60000.0 * static_cast<double>(g_state.minutesPerHour));
    hour -= 24.0 * static_cast<double>(static_cast<int>(hour / 24.0));
    g_state.hour = static_cast<float>(hour);
}

/** F7: an hour on, with Shift an hour back (Ctrl+F7 is the weather's). */
void PollKeys()
{
    const bool key = nfs::display::DebugKeyDown(VK_F7) && !nfs::display::DebugKeyDown(VK_CONTROL);
    const bool shift = nfs::display::DebugKeyDown(VK_SHIFT);
    const bool forward = key && !shift, back = key && shift;
    if (forward && !g_state.forward)
        SetClock(g_state.hour + 1.0f);
    if (back && !g_state.back)
        SetClock(g_state.hour - 1.0f);
    g_state.forward = forward;
    g_state.back = back;
}

void FormatHour(char *text, int size, float hour)
{
    const int minutes = static_cast<int>(Wrap(hour) * 60.0f) % (24 * 60);
    char buffer[8];
    wsprintfA(buffer, "%02d:%02d", minutes / 60, minutes % 60);
    lstrcpynA(text, buffer, size);
}
} // namespace

namespace nfs::timeofday
{
bool Enabled()
{
    Init();
    return g_state.mode != kGameDay;
}

bool Lit()
{
    // The front end's scenes (the main menu, the safehouse, the car lot, the customization shop: game flow 1 to 3, loading,
    // in and leaving the front end) are lit as the game lights them, whatever the hour.
    const int state = TheGameFlowManager_CurrentGameFlowState;
    return Enabled() && (state < kGameFlowLoadingFrontEnd || state > kGameFlowFrontEnd);
}

float Hour()
{
    Init();
    Tick();
    return g_state.hour;
}

void SetHour(float hour)
{
    Init();
    SetClock(hour);
}

void Reconfigure()
{
    Init();
    Tick();
    const float now = g_state.hour;
    char mode[16];
    nfs::config::String("World", "TimeOfDay", "cycle", mode, sizeof(mode));
    g_state.minutesPerHour = nfs::config::Float("World", "MinutesPerHour", 5.0f);
    if (!lstrcmpiA(mode, "day"))
    {
        g_state.mode = kGameDay;
        return;
    }
    if (!g_state.count)
        Load();
    if (!g_state.count)
    {
        g_state.mode = kGameDay;
        return;
    }
    if (!lstrcmpiA(mode, "night"))
    {
        g_state.mode = kFixed;
        SetClock(kNightHour);
    }
    else if (::ParseHour(mode) >= 0.0f)
    {
        g_state.mode = kFixed;
        SetClock(::ParseHour(mode));
    }
    else
    {
        g_state.mode = kCycle;
        SetClock(now);
    }
    g_state.located = -1.0f;
}

float ParseHour(const char *text)
{
    return ::ParseHour(text);
}

float Luminance(const float *rgb)
{
    return 0.3f * rgb[0] + 0.59f * rgb[1] + 0.11f * rgb[2];
}

/** The region's colours with most of their cast taken out (kRegionTint): each keeps its brightness and the share of its
 * colour against the city's. */
void NeutralizeRegion(float *block)
{
    for (const CityColour &city : kCityColours)
    {
        float *v = block + (kFieldInfo[city.field].offset - kBlockStart) / 4;
        const float own = Luminance(v), reference = Luminance(city.rgb);
        if (own <= 1e-4f || reference <= 0.0f)
            continue;
        for (int c = 0; c < 3; ++c)
        {
            const float neutral = city.rgb[c] * own / reference;
            if (neutral <= 1e-6f || v[c] <= 1e-6f)
                continue;
            v[c] = neutral * static_cast<float>(pow(static_cast<double>(v[c] / neutral), kRegionTint));
        }
    }
}

void ApplyLighting(char *time_of_day)
{
    if (!time_of_day || !Lit())
        return;
    Tick();
    float *block = reinterpret_cast<float *>(time_of_day + kBlockStart);
    memcpy(g_state.raw, block, sizeof(g_state.raw));
    memcpy(g_state.game, block, sizeof(g_state.game));
    NeutralizeRegion(g_state.game);
    if (!g_state.haveGame)
    {
        // The game's own lighting where the clock starts, for writing keys against.
        for (int f = 0; f < kFields && kFieldInfo[f].offset >= 0; ++f)
        {
            const float *v = g_state.game + (kFieldInfo[f].offset - kBlockStart) / 4;
            nfs::Log("time of day: game %s = %d %d %d %d thousandths", kFieldInfo[f].name, static_cast<int>(v[0] * 1000.0f),
                     static_cast<int>(v[1] * 1000.0f), static_cast<int>(v[2] * 1000.0f),
                     static_cast<int>(v[3] * 1000.0f));
        }
    }
    g_state.haveGame = true;
    // In a tunnel its lamps light the world as at any hour. With the tunnels' roofs (engine/rendering/lights/lamps.h) the rebuild's shaders
    // keep the light baked under them as at the game's own hour, pixel by pixel; without them the whole world's sky goes
    // over to the game's own hour as the camera goes into one of the game's tunnel zones (its tunnel factor). The cars'
    // light rigs follow the camera's tunnel either way.
    const float gameHour = g_state.path[kGameHour];
    const bool roofs = nfs::lamps::Tunnels();
    const float zone = flt_982BA8 < 0.0f ? 0.0f : flt_982BA8 > 1.0f ? 1.0f : flt_982BA8;
    if (g_state.tunnelFrame != WorldTimeFrames)
    {
        g_state.tunnelFrame = WorldTimeFrames;
        const float target = nfs::lamps::CameraCover(), step = WorldTimeElapsed / kTunnelEase;
        float &eased = g_state.cameraTunnel;
        eased = target > eased ? (eased + step < target ? eased + step : target)
                               : (eased - step > target ? eased - step : target);
    }
    const float tunnel = roofs ? g_state.cameraTunnel : zone;
    const float worldTunnel = roofs ? 0.0f : zone;
    for (int f = 0; f < kFields; ++f)
    {
        if (kFieldInfo[f].offset < 0)
            continue;
        const int first = (kFieldInfo[f].offset - kBlockStart) / 4;
        for (int i = 0; i < kFieldInfo[f].count; ++i)
        {
            float v = Value(f, i, g_state.game[first + i]);
            if (f == kAmbient && worldTunnel > 0.0f)
                v += (ValueAt(gameHour, f, i, g_state.game[first + i]) - v) * worldTunnel;
            block[first + i] = v;
        }
    }
    ApplyWeather(block);
    static const bool weatherDebug = nfs::vk::HasArgument("-weatherdebug");
    static DWORD logged;
    if (weatherDebug && GetTickCount() - logged > 4000)
    {
        logged = GetTickCount();
        const float *g = g_state.raw;
        nfs::Log("time of day: game diffuse %d %d %d ambient %d %d %d x%d haze %d %d %d scale %d; hour diffuse %d %d %d "
                 "ambient %d %d %d x%d haze scale %d (thousandths)",
                 static_cast<int>(g[0] * 1000), static_cast<int>(g[1] * 1000), static_cast<int>(g[2] * 1000),
                 static_cast<int>(g[4] * 1000), static_cast<int>(g[5] * 1000), static_cast<int>(g[6] * 1000),
                 static_cast<int>(g[7] * 1000), static_cast<int>(g[(0x80 - kBlockStart) / 4] * 1000),
                 static_cast<int>(g[(0x84 - kBlockStart) / 4] * 1000), static_cast<int>(g[(0x88 - kBlockStart) / 4] * 1000),
                 static_cast<int>(g[(0x94 - kBlockStart) / 4] * 1000), static_cast<int>(block[0] * 1000),
                 static_cast<int>(block[1] * 1000), static_cast<int>(block[2] * 1000), static_cast<int>(block[4] * 1000),
                 static_cast<int>(block[5] * 1000), static_cast<int>(block[6] * 1000), static_cast<int>(block[7] * 1000),
                 static_cast<int>(block[(0x94 - kBlockStart) / 4] * 1000));
    }
    memcpy(g_state.hours, block, sizeof(g_state.hours));
    // Cars and characters have light rigs of their own, made for the game's own lighting: at its hour (GameHour, where
    // the keys may still correct the world's colours) they stay as they are, and at any other they change as the
    // world's sun and sky change from that hour. The world takes AmbientColour times its w.
    const float car = Value(kCarLight, 0, 1.0f);
    const int diffuseAt = (0x20 - kBlockStart) / 4, ambientAt = (0x30 - kBlockStart) / 4;
    const float hourAmbientW = g_state.hours[ambientAt + 3];
    const float refAmbientW = ValueAt(gameHour, kAmbient, 3, g_state.game[ambientAt + 3]);
    const float skyW = refAmbientW > 0.001f ? hourAmbientW / refAmbientW : 1.0f;
    g_state.skyW = skyW < kMaxLightRatio ? skyW : kMaxLightRatio;
    const float tunnelLight = Value(kTunnelLight, 0, 1.0f);
    for (int c = 0; c < 3; ++c)
    {
        const float refDiffuse = ValueAt(gameHour, kDiffuse, c, g_state.game[diffuseAt + c]);
        const float refAmbient = ValueAt(gameHour, kAmbient, c, g_state.game[ambientAt + c]) * refAmbientW;
        float key = refDiffuse > 0.001f ? g_state.hours[diffuseAt + c] / refDiffuse : 1.0f;
        float fill = refAmbient > 0.001f ? g_state.hours[ambientAt + c] * hourAmbientW / refAmbient : 1.0f;
        key = key < kMaxLightRatio ? key : kMaxLightRatio;
        fill = fill < kMaxLightRatio ? fill : kMaxLightRatio;
        key += (1.0f - key) * tunnel;
        g_state.sunRatio[c] = key;
        g_state.skyRatio[c] = fill;
        const float underRoof = fill > 0.001f ? tunnelLight / fill : kMaxTunnelRatio;
        g_state.tunnel[c] = underRoof < kMaxTunnelRatio ? underRoof : kMaxTunnelRatio;
        // With the roofs the world's sky stays the hour's while the camera is in a tunnel: the rigs' fill lights go
        // over to the game's own hour by themselves.
        const float rigFill = roofs ? fill + (1.0f - fill) * tunnel : fill;
        g_state.key[c] = key * car;
        g_state.fill[c] = rigFill * car;
        // In shade a car's key light gives way to the sky: what it keeps follows the sky against the sun.
        const float shade = kCarShadeAtGameHour[c] * (key > 0.001f ? rigFill / key : kMaxCarShade);
        g_state.carShade[c] = shade < kMaxCarShade ? shade : kMaxCarShade;
    }
}

void ApplySun(char *time_of_day)
{
    if (!time_of_day || !Lit())
        return;
    if (g_state.frame != WorldTimeFrames)
    {
        g_state.frame = WorldTimeFrames;
        PollKeys();
        if (Running() && g_state.gameClock)
        {
            g_state.base += static_cast<double>(WorldTimeElapsed) / (60.0 * static_cast<double>(g_state.minutesPerHour));
            g_state.base -= 24.0 * static_cast<double>(static_cast<int>(g_state.base / 24.0));
            g_state.hour = static_cast<float>(g_state.base);
        }
    }
    Tick();
    const float rise = g_state.path[kSunrise], set = g_state.path[kSunset];
    g_state.sun = Arc(g_state.hour, rise, set, g_state.path[kSunHighest]);
    g_state.moon = Arc(g_state.hour, set, rise, g_state.path[kMoonHighest]);
    const Place &light = g_state.sun.elevation > kLightSwitchElevation ? g_state.sun : g_state.moon;
    float direction[3];
    Direction(light, direction);
    float *position = reinterpret_cast<float *>(time_of_day + kSunPosition);
    float *to = reinterpret_cast<float *>(time_of_day + kSunDirection);
    for (int i = 0; i < 3; ++i)
    {
        position[i] = direction[i] * *kSunDistance;
        to[i] = direction[i];
    }
    position[3] = 1.0f;
    to[3] = 0.0f;
}

float MiddleGray(float day)
{
    return Value(kMiddleGray, 0, day);
}

float AdaptFloor(float day)
{
    return Value(kAdaptFloor, 0, day);
}

float SkyBrightness(float day)
{
    // In fog and a downpour the sky is the haze's light alone (its layers fade), as bright as the haze over the world at
    // the game's own brightness: far mountains and buildings lost in the haze are the colour of the sky behind them.
    const float v = Value(kSkyBrightness, 0, day);
    const float thick = Lit() && g_state.haveGame ? g_state.thickness : 0.0f;
    return (v + (day - v) * thick) * (1.0f + kFlashSky * nfs::weather::Lightning());
}

float RoadReflectionSkyGain()
{
    return 2.0f - (Lit() && g_state.haveGame ? g_state.thickness : 0.0f);
}
void SkyLayers(float *scale)
{
    const float clear = 1.0f - (Lit() && g_state.haveGame ? g_state.thickness : 0.0f);
    for (int i = 0; i < 4; ++i)
        scale[i] = Value(kSkyLayers, i, scale[i]) * clear;
}

float NightSky()
{
    // Clouds and fog hide the stars and the moon: the game's sky (overcast, or lost in the haze) shows instead.
    const float v = Value(kNightSky, 0, 0.0f);
    const float cloud = nfs::weather::Cloud(), fog = nfs::weather::Fog();
    const float hidden = cloud > fog ? cloud : fog;
    return (v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v) * (1.0f - hidden);
}

float DomeBrightness()
{
    return Value(kDomeBrightness, 0, 1.0f);
}

void Moon(float &azimuth, float &elevation)
{
    azimuth = g_state.moon.azimuth;
    elevation = g_state.moon.elevation;
}

float SunSprite()
{
    if (!Lit())
        return 1.0f;
    const float cloud = nfs::weather::Cloud(), fog = nfs::weather::Fog();
    const float hidden = cloud > fog ? cloud : fog;
    return g_state.sun.elevation > 0.0f ? Value(kSunSprite, 0, 1.0f) * (1.0f - hidden) : 0.0f;
}

const void *LightContext(const void *context)
{
    if (!context || !Lit() || !g_state.haveGame)
        return context;
    static unsigned char scaled[kLightContextSize];
    memcpy(scaled, context, kLightContextSize);
    float *colours = reinterpret_cast<float *>(scaled + kLightColours);
    for (int c = 0; c < 3; ++c)
    {
        colours[c] *= g_state.key[c];
        for (int l = 1; l < kLights; ++l)
            colours[l * kLights + c] *= g_state.fill[c];
    }
    return scaled;
}

float TreeLight()
{
    return Value(kTreeLight, 0, 1.0f);
}

float LocalLights()
{
    const float v = Value(kLocalLights, 0, 0.0f);
    return v < 0.0f ? 0.0f : v;
}

float Traffic()
{
    const float v = Value(kTraffic, 0, 1.0f);
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

float RainChance(float fallback)
{
    const float v = ClockValue(kRainChance, 0, fallback);
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

float FogChance()
{
    const float v = ClockValue(kFogChance, 0, 0.0f);
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

float RainSpell()
{
    Init();
    return g_state.path[kRainSpell] > 0.1f ? g_state.path[kRainSpell] : 0.1f;
}

float FogSpell()
{
    Init();
    return g_state.path[kFogSpell] > 0.1f ? g_state.path[kFogSpell] : 0.1f;
}

float MinutesPerHour()
{
    Init();
    return g_state.minutesPerHour > 0.0f ? g_state.minutesPerHour : 5.0f;
}

void CarShade(float *rgb)
{
    for (int c = 0; c < 3; ++c)
        rgb[c] = Lit() && g_state.haveGame ? g_state.carShade[c] : kCarShadeAtGameHour[c];
}

void ParticleLight(float *sun, float *sky)
{
    const bool hour = Lit() && g_state.haveGame;
    for (int c = 0; c < 3; ++c)
    {
        sun[c] = kParticleSunShare * (hour ? g_state.sunRatio[c] : 1.0f);
        sky[c] = (1.0f - kParticleSunShare) * (hour ? g_state.skyRatio[c] : 1.0f);
    }
}

void SkyLight(float *rgbw)
{
    const bool hour = Lit() && g_state.haveGame;
    for (int c = 0; c < 3; ++c)
        rgbw[c] = hour ? g_state.skyRatio[c] : 1.0f;
    rgbw[3] = hour ? g_state.skyW : 1.0f;
}

void TunnelLight(float *rgb)
{
    const bool hour = Lit() && g_state.haveGame;
    for (int c = 0; c < 3; ++c)
        rgb[c] = hour ? g_state.tunnel[c] : 1.0f;
}

void HazeTint(float *rgb)
{
    const bool hour = Lit() && g_state.haveGame;
    for (int c = 0; c < 3; ++c)
        rgb[c] = hour ? g_state.hazeTint[c] : 1.0f;
}

float HazeExtinction()
{
    return Lit() && g_state.haveGame ? g_state.extinction : 0.0f;
}

void HazeLight(float *rgb)
{
    const bool hour = Lit() && g_state.haveGame;
    for (int c = 0; c < 3; ++c)
        rgb[c] = hour ? g_state.hazeLight[c] : 0.0f;
}

float HazeSunShare()
{
    return Lit() && g_state.haveGame ? g_state.hazeSunShare : kClearHazeSunShare;
}

float HazeTransmittance(float metres)
{
    const double lost = static_cast<double>(HazeExtinction()) * static_cast<double>(metres);
    return lost > 0.0 ? static_cast<float>(pow(2.718281828459045, -lost)) : 1.0f;
}

bool Text(char *line, int size)
{
    if (!Enabled())
        return false;
    Tick();
    char hour[8];
    FormatHour(hour, sizeof(hour), g_state.hour);
    char text[48];
    if (g_state.mode == kCycle && g_state.minutesPerHour > 0.0f)
    {
        const int tenths = static_cast<int>(g_state.minutesPerHour * 10.0f + 0.5f);
        wsprintfA(text, "TIME %s  %d.%d MIN/H", hour, tenths / 10, tenths % 10);
    }
    else
        wsprintfA(text, "TIME %s  FIXED", hour);
    lstrcpynA(line, text, size);
    return true;
}

void PoseOption(char *text, int size)
{
    text[0] = 0;
    if (!Enabled() || size < 32)
        return;
    Tick();
    char hour[8];
    FormatHour(hour, sizeof(hour), g_state.hour);
    wsprintfA(text, " -set World.TimeOfDay=%s", hour);
}
} // namespace nfs::timeofday
