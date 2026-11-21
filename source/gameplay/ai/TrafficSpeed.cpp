/**
 * Traffic speeds by road (gameplay/ai/aituning.h TrafficImproved, docs/AI.md "Traffic speeds").
 *
 * The original (AIActionTraffic::ComputeSpeed 0x411820) drives traffic at the pattern's SpeedStreet (35 mph) on roads of
 * under four traffic lanes and SpeedHighway (55 mph) on the rest, everywhere. Improved, each road segment gets a class
 * from its lanes and the traffic zone it lies in (TrackPathManager zones of type 9, the trafficpattern names):
 *
 *     highway        four or more traffic lanes, and a shoulder lane (type 2) or a highway zone      120 km/h
 *     rural highway  fewer lanes with a shoulder lane                                                  90
 *     outskirts      the rest outside the city zones                                                   80
 *     city road      four or more traffic lanes in a city zone                                         70
 *     city street    fewer lanes in a city zone                                                        60
 *
 * speed.ini [Traffic]: Highway, RuralHighway, Outskirts, CityRoad, CityStreet (km/h), CityZones and HighwayZones
 * (pattern names).
 * Each car keeps its own pace within kPaceSpread, and slows into a lower class at kEaseDown. Speeds a race or script
 * sets (MSetTrafficSpeed other than the patterns' 35/55 mph) are kept as the original keeps them.
 */
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>

#include "core/config.h"
#include "core/log.h"
#include "gameplay/ai/aituning.h"
#include "legacy/link.h"

NFS_REPLACE(0x411820, AIActionTrafficComputeSpeed)

namespace
{
template <typename T> T &At(void *base, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}

template <typename R, typename... A> R Virtual(void *object, int offset, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*reinterpret_cast<void ***>(object))[offset / 4])(object, args...);
}

float Constant(unsigned int address)
{
    return *reinterpret_cast<const float *>(address);
}

enum Class : unsigned char
{
    kUnknown,
    kHighway,
    kRuralHighway,
    kCityRoad,
    kCityStreet,
    kOutskirts,
    kClasses
};

const char *const kClassNames[kClasses] = {"?", "highway", "rural highway", "city road", "city street", "outskirts"};

void *const *const kRoadNetwork = reinterpret_cast<void *const *>(0x9b38b4);
const unsigned int *const kSegmentCount = reinterpret_cast<const unsigned int *>(0x9b3874);
const unsigned char *const *const kNodes = reinterpret_cast<const unsigned char *const *>(0x9b38bc);
const unsigned char *const *const kSegments = reinterpret_cast<const unsigned char *const *>(0x9b38c0);
const unsigned char *const *const kProfiles = reinterpret_cast<const unsigned char *const *>(0x9b38b8);
void *const kTrackPaths = reinterpret_cast<void *>(0x9aa540);
const int kTrafficZone = 9;
const int kZoneKey = 0x30;
const int kSegmentSize = 0x16;
const int kNodeSize = 0x20;
const int kNodeProfile = 0xe;
const int kProfileSize = 0x40;
const unsigned int kLaneTraffic = 1;
const unsigned int kLaneShoulder = 2;
const int kWideRoad = 4;
const int kMostSegments = 32768;
const int kMostZones = 16;

const float kKmh = 1.0f / 3.6f;
const float kMph = 0.44703f;
const float kPatternStreet = 35.0f * kMph;
const float kPatternHighway = 55.0f * kMph;
const float kPaceSpread = 0.08f;
const float kEaseDown = 3.0f;
const int kMostCars = 64;

const int kNav = 0x48;
const int kNavSegment = 0x8e;
const int kNavOccludedOn = 0x55;
const int kNavOccluder = 0x5c;
const int kNavOccluderStopped = 0x60;
const int kNavOccluderGap = 0x64;
const int kNavOccluderAt = 0x158;
const int kActionVehicle = 0x3c;
const int kActionAi = 0x40;
const int kActionStreet = 0x50;
const int kActionHighway = 0x54;
const int kActionScripted = 0x4e;
const int kActionCloseFollow = 0x58;
const int kActionBody = 0x5c;
const int kVehicleClass = 0x58;
const int kAgileClass = 2;

struct Settings
{
    float speed[kClasses];
    unsigned int city[kMostZones];
    int cities;
    unsigned int highway[kMostZones];
    int highways;
    bool read;
};

struct Cache
{
    const unsigned char *segments;
    unsigned int count;
    unsigned char klass[kMostSegments];
};

struct Car
{
    void *action;
    float base;
    DWORD logged;
};

Settings g_settings;
Cache g_cache;
Car g_cars[kMostCars];
int g_nextCar;

unsigned int StringHash(const char *text)
{
    unsigned int hash = 0xffffffffu;
    for (; *text; ++text)
        hash = hash * 33 + static_cast<unsigned char>(*text);
    return hash;
}

int ReadZones(const char *key, const char *fallback, unsigned int *out)
{
    char text[256];
    nfs::config::String("Traffic", key, fallback, text, sizeof(text));
    int count = 0;
    char *at = text;
    while (*at && count < kMostZones)
    {
        while (*at == ',' || *at == ' ')
            ++at;
        char *start = at;
        while (*at && *at != ',' && *at != ' ')
        {
            if (*at >= 'A' && *at <= 'Z')
                *at = static_cast<char>(*at - 'A' + 'a');
            ++at;
        }
        const char end = *at;
        *at = 0;
        if (*start)
            out[count++] = StringHash(start);
        if (end)
            ++at;
    }
    return count;
}

const Settings &Read()
{
    if (g_settings.read)
        return g_settings;
    g_settings.speed[kUnknown] = 0.0f;
    g_settings.speed[kHighway] = nfs::config::Float("Traffic", "Highway", 120.0f) * kKmh;
    g_settings.speed[kRuralHighway] = nfs::config::Float("Traffic", "RuralHighway", 90.0f) * kKmh;
    g_settings.speed[kOutskirts] = nfs::config::Float("Traffic", "Outskirts", 80.0f) * kKmh;
    g_settings.speed[kCityRoad] = nfs::config::Float("Traffic", "CityRoad", 70.0f) * kKmh;
    g_settings.speed[kCityStreet] = nfs::config::Float("Traffic", "CityStreet", 60.0f) * kKmh;
    g_settings.cities = ReadZones("CityZones", "downtown,collegesouth,coastalsouth", g_settings.city);
    g_settings.highways = ReadZones("HighwayZones", "collegehighway,cityhighway", g_settings.highway);
    g_settings.read = true;
    return g_settings;
}

bool Listed(const unsigned int *keys, int count, unsigned int key)
{
    for (int i = 0; i < count; ++i)
        if (keys[i] == key)
            return true;
    return false;
}

void Lanes(int node, int &traffic, bool &shoulder)
{
    const short index = *reinterpret_cast<const short *>(*kNodes + node * kNodeSize + kNodeProfile);
    if (index < 0)
        return;
    const unsigned char *profile = *kProfiles + index * kProfileSize;
    int count = 0;
    for (int i = 0; i < profile[0]; ++i)
    {
        const unsigned int lane = reinterpret_cast<const unsigned int *>(profile + 4)[i] & 0xf;
        count += lane == kLaneTraffic;
        shoulder |= lane == kLaneShoulder;
    }
    if (count > traffic)
        traffic = count;
}

Class Classify(int segment)
{
    const Settings &s = Read();
    const unsigned char *data = *kSegments + segment * kSegmentSize;
    const unsigned short a = *reinterpret_cast<const unsigned short *>(data);
    const unsigned short b = *reinterpret_cast<const unsigned short *>(data + 2);
    int traffic = 0;
    bool shoulder = false;
    Lanes(a, traffic, shoulder);
    Lanes(b, traffic, shoulder);
    const float *pa = reinterpret_cast<const float *>(*kNodes + a * kNodeSize);
    const float *pb = reinterpret_cast<const float *>(*kNodes + b * kNodeSize);
    const float world[2] = {(pa[2] + pb[2]) * 0.5f, -(pa[0] + pb[0]) * 0.5f};
    const char *zone = NFS_ORIGINAL(0x727480, const char *(__thiscall *)(void *, const float *, int, void *))(
        kTrackPaths, world, kTrafficZone, nullptr);
    const unsigned int key = zone ? *reinterpret_cast<const unsigned int *>(zone + kZoneKey) : 0;
    const bool wide = traffic >= kWideRoad;
    if (wide && (shoulder || Listed(s.highway, s.highways, key)))
        return kHighway;
    if (shoulder)
        return kRuralHighway;
    if (Listed(s.city, s.cities, key))
        return wide ? kCityRoad : kCityStreet;
    return kOutskirts;
}

Class ClassOf(int segment)
{
    if (g_cache.segments != *kSegments || g_cache.count != *kSegmentCount)
    {
        g_cache.segments = *kSegments;
        g_cache.count = *kSegmentCount;
        memset(g_cache.klass, kUnknown, sizeof(g_cache.klass));
    }
    if (segment < 0 || segment >= static_cast<int>(g_cache.count) || segment >= kMostSegments)
        return kOutskirts;
    unsigned char &klass = g_cache.klass[segment];
    if (klass == kUnknown)
        klass = Classify(segment);
    return static_cast<Class>(klass);
}

float Pace(void *action)
{
    unsigned int h = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(action) >> 4) * 2654435761u;
    h ^= h >> 15;
    return 1.0f + kPaceSpread * (static_cast<float>(h & 0xffff) / 32767.5f - 1.0f);
}

Car &Eased(void *action, float base, float dt)
{
    Car *car = nullptr;
    for (Car &c : g_cars)
        if (c.action == action)
        {
            car = &c;
            break;
        }
    if (!car)
    {
        car = &g_cars[g_nextCar];
        g_nextCar = (g_nextCar + 1) % kMostCars;
        car->action = action;
        car->base = base;
        car->logged = 0;
        return *car;
    }
    const float lowest = car->base - kEaseDown * dt;
    car->base = base < lowest ? lowest : base;
    return *car;
}

bool PatternSpeeds(char *self)
{
    return fabsf(At<float>(self, kActionStreet) - kPatternStreet) < 0.05f &&
           fabsf(At<float>(self, kActionHighway) - kPatternHighway) < 0.05f;
}

void LogCar(Car &car, Class klass, float speed)
{
    static const int every = nfs::config::Int("Debug", "TrafficLog", 0);
    if (every <= 0)
        return;
    const DWORD now = GetTickCount();
    if (car.logged && now - car.logged < static_cast<DWORD>(every) * 1000u)
        return;
    car.logged = now;
    nfs::Log("traffic: %08x %s target %d km/h, driving %d km/h", static_cast<unsigned int>(reinterpret_cast<uintptr_t>(car.action)),
             kClassNames[klass], static_cast<int>(car.base * 3.6f + 0.5f), static_cast<int>(speed * 3.6f + 0.5f));
}
} // namespace

float __fastcall AIActionTrafficComputeSpeed(char *self, void *, float speed, float dt)
{
    char *nav = Virtual<char *>(At<void *>(self, kActionAi), kNav);
    if (nav[0x2c0])
        return Constant(0x890968);
    nav = Virtual<char *>(At<void *>(self, kActionAi), kNav);
    const int segment = *reinterpret_cast<const short *>(nav + kNavSegment);
    const bool agile = Virtual<int>(At<void *>(self, kActionVehicle), kVehicleClass) == kAgileClass;
    float base;
    if (nfs::aituning::TrafficImproved() && !At<unsigned char>(self, kActionScripted) && PatternSpeeds(self))
    {
        const Class klass = ClassOf(segment);
        Car &car = Eased(self, Read().speed[klass] * Pace(self), dt);
        base = car.base;
        LogCar(car, klass, speed);
    }
    else
    {
        const int lanes = NFS_ORIGINAL(0x76dd30, int(__thiscall *)(void *, const void *))(
            *kRoadNetwork, *kSegments + segment * kSegmentSize);
        base = lanes < kWideRoad ? At<float>(self, kActionStreet) : At<float>(self, kActionHighway);
    }
    float limit = base;
    float gap = 0.0f;
    if (!At<unsigned char>(self, kActionScripted))
    {
        void *body = At<void *>(self, kActionBody);
        const void *velocity = Virtual<const void *>(body, 0x24);
        const void *position = Virtual<const void *>(body, 0x20);
        void *trail = Virtual<void *>(At<void *>(self, kActionAi), kNav);
        float curvature = NFS_ORIGINAL(0x778910, float(__thiscall *)(void *, const void *, const void *))(
            trail, position, velocity);
        const float grip = (agile ? Constant(0x890624) : Constant(0x89062c)) * Constant(0x890d58);
        curvature = fabsf(curvature);
        float bend = grip / (base * base);
        if (!(bend > curvature))
            bend = curvature;
        const float corner = sqrtf(grip / bend);
        if (!(base < corner))
            limit = corner;
        if (nav[kNavOccludedOn] && At<void *>(nav, kNavOccluder) && !nav[kNavOccluderStopped])
        {
            float reach = Virtual<float>(body, 0x18);
            if (At<unsigned char>(self, kActionCloseFollow))
                reach += reach;
            const float length = Virtual<float>(body, 0x14) * 2.0f;
            const float *at = Virtual<const float *>(body, 0x20);
            const float d = NFS_ORIGINAL(0x4100a0, float(__cdecl *)(const void *, const float *))(
                                nav + kNavOccluderAt, at) -
                            length;
            gap = d > Constant(0x890968) ? d : 0.0f;
            float scale = reach * Constant(0x891034);
            if (!(scale > Constant(0x89096c)))
                scale = Constant(0x89096c);
            const float keep = NFS_ORIGINAL(0x6533b0, float(__thiscall *)(void *, float))(
                reinterpret_cast<void *>(0x8eb394), scale * speed);
            if (gap < keep)
            {
                const float follow = At<float>(nav, kNavOccluderGap) / keep * gap;
                float clamped = follow > 0.0f ? follow : 0.0f;
                if (clamped > limit)
                    clamped = limit;
                limit = clamped < speed ? clamped : speed;
            }
        }
    }
    if (!agile && !(limit < speed + dt + dt))
        return speed + dt + dt;
    return limit;
}
