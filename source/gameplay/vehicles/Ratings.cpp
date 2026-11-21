/**
 * The front end's performance bars (original: zPhysics Physics::Info::EstimatePerformance 0x67c570): top speed,
 * handling and acceleration, 0 to 1.
 *
 * The original reads each car's stock and fully upgraded ratings from the table Physics::Info::Init made with
 * PerfStats::Fetch (every car's estimate, scaled between the slowest and fastest of them all), and moves between the
 * two by how far each kind of part is upgraded (Physics::Upgrades::GetPercent, weighted per bar by
 * PerformanceWeights). A junkman part then pulled the car's whole range up toward the top of the scale, so a car
 * with junkman parts in every slot showed full bars whatever it was.
 *
 * Top speed and acceleration now come from the car as configured (gameplay/vehicles/performance.h Measured: its stock figures
 * measured on the test track, changed as the model says its upgrades and junkman parts change them): the top speed
 * from the slowest stock car of the table to a full bar at 400 km/h, and the time to 100 mph between the slowest stock
 * car and the quickest with every part upgraded and junkman. Handling keeps the original's interpolation, with a junkman part counted as
 * kJunkmanLevel of the full upgrade in its slot instead of the pull to the top, over the best of the table with
 * junkman everywhere. So a car with junkman parts in every slot fills a bar only where it is the best there is. The
 * figures of the car a front-end screen last asked about are kept for the extended details (gameplay/vehicles/performance.h).
 */
#include <windows.h>

#include "game/gen/api.h"
#include "core/config.h"
#include "legacy/link.h"
#include "core/log.h"
#include "gameplay/vehicles/performance.h"

inline void *operator new(size_t, void *where)
{
    return where;
}

NFS_REPLACE(0x67c570, EstimateRatings)

namespace
{
/** Physics::Info::Init's table: a list of nodes (next, previous, ..., stock ratings at +0x14, full upgrade at +0x20,
 * the pvehicle collection at +0x2c). */
const unsigned int kTableHead = 0x92c4e8;
const int kNodeStock = 0x14;
const int kNodeMaximum = 0x20;
const int kNodeCollection = 0x2c;
/** Per kind of part (tyres, brakes, chassis, transmission, engine, induction, nitrous): the weights of the top speed,
 * handling and acceleration bars. */
const unsigned int kPerformanceWeights = 0x8f8928;
const int kKinds = 7;
const unsigned int kJunkman = 0xCDC136E8;
const float kJunkmanLevel = 1.25f;
/** m/s: a full top speed bar (400 km/h). */
const float kTopSpeedFull = 400.0f / 3.6f;
const unsigned int kAllJunkman = (1u << kKinds) - 1;
const int kTop = 0;
const int kHandling = 1;
const int kAcceleration = 2;
const int kInstanceSize = 0x14;
const int kInstanceLayout = 8;
const int kModelOffset = 0x1C;
/** FEPlayerCarDB's car records: handle, front-end key, pvehicle key, filter bits, then the customization and career
 * record (0xff: none). */
const int kCarRecords = 200;
const int kCarRecordSize = 20;
const int kRecordVehicle = 8;
const int kRecordCustomization = 0x10;
const int kRecordCareer = 0x11;
const unsigned char kNoRecord = 0xff;

typedef bool(__cdecl *GetPerformance)(const Attrib::Instance *, float *);
typedef float(__cdecl *GetPercent)(const Attrib::Instance *, int);
typedef bool(__cdecl *SetMaximum)(Attrib::Instance *);
typedef int(__cdecl *GetMaxLevel)(const Attrib::Instance *, int);
/** Physics::Upgrades::Package: the level of each kind of part, then the kinds with a junkman part (bits). */
typedef bool(__cdecl *SetPackage)(Attrib::Instance *, const unsigned int *);

struct Scale
{
    bool ready;
    float topMin, topMax;
    float timeMin, timeMax;
    /** The table's best handling rating with junkman parts in every slot. */
    float handlingMax;
    float gripMin, gripMax;
};

Scale g_scale;

float Rate(float value, float low, float high)
{
    if (high - low <= 1e-6f)
        return 0.0f;
    const float f = (value - low) / (high - low);
    return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}

void Include(const nfs::performance::Figures &f)
{
    if (f.grip < g_scale.gripMin)
        g_scale.gripMin = f.grip;
    if (f.grip > g_scale.gripMax)
        g_scale.gripMax = f.grip;
    if (f.topSpeed < g_scale.topMin)
        g_scale.topMin = f.topSpeed;
    if (f.topSpeed > g_scale.topMax)
        g_scale.topMax = f.topSpeed;
    if (f.zeroTo100Mph > 0.0f)
    {
        if (f.zeroTo100Mph < g_scale.timeMin)
            g_scale.timeMin = f.zeroTo100Mph;
        if (f.zeroTo100Mph > g_scale.timeMax)
            g_scale.timeMax = f.zeroTo100Mph;
    }
}

/** Every part at its highest level, with junkman parts in every slot. */
bool SetFullJunkman(Attrib::Instance *vehicle)
{
    unsigned int package[kKinds + 1];
    for (int kind = 0; kind < kKinds; ++kind)
        package[kind] = static_cast<unsigned int>(NFS_ORIGINAL(0x672e80, GetMaxLevel)(vehicle, kind));
    package[kKinds] = kAllJunkman;
    return NFS_ORIGINAL(0x67f800, SetPackage)(vehicle, package);
}

/** The lowest and highest figures of every car in the table: stock, fully upgraded and with junkman everywhere. */
void BuildScale()
{
    g_scale.ready = true;
    g_scale.topMin = g_scale.timeMin = g_scale.gripMin = 1e9f;
    g_scale.topMax = g_scale.timeMax = g_scale.gripMax = 0.0f;
    g_scale.handlingMax = 1.0f;
    char *head = *reinterpret_cast<char **>(kTableHead);
    if (!head)
        return;
    const bool log = nfs::config::Int("Debug", "RatingsLog", 0) != 0;
    int cars = 0;
    for (char *node = *reinterpret_cast<char **>(head); node != head; node = *reinterpret_cast<char **>(node))
    {
        const unsigned int key = *reinterpret_cast<unsigned int *>(node + kNodeCollection);
        alignas(4) unsigned char storage[kInstanceSize];
        new (storage) Attrib::Gen::pvehicle(key, 0, nullptr);
        Attrib::Instance *vehicle = reinterpret_cast<Attrib::Instance *>(storage);
        nfs::performance::Figures f;
        int tops[3] = {}, times[3] = {};
        if (nfs::performance::Measured(*vehicle, f))
        {
            Include(f);
            tops[0] = static_cast<int>(f.topSpeed * 3.6f);
            times[0] = static_cast<int>(f.zeroTo100Mph * 100.0f);
        }
        char percents[96];
        percents[0] = 0;
        const bool maximized = NFS_ORIGINAL(0x67f950, SetMaximum)(vehicle);
        if (log)
        {
            int n = 0;
            for (int kind = 0; kind < kKinds; ++kind)
                n += wsprintfA(percents + n, "%s%d:%d", kind ? "/" : "",
                               static_cast<int>(NFS_ORIGINAL(0x672e80, GetMaxLevel)(vehicle, kind)),
                               static_cast<int>(NFS_ORIGINAL(0x677f80, GetPercent)(vehicle, kind) * 100.0f + 0.5f));
        }
        if (maximized && nfs::performance::Measured(*vehicle, f))
        {
            Include(f);
            tops[1] = static_cast<int>(f.topSpeed * 3.6f);
            times[1] = static_cast<int>(f.zeroTo100Mph * 100.0f);
            if (log)
            {
                nfs::performance::Figures e;
                if (nfs::performance::Estimate(*vehicle, e))
                    nfs::Log("ratings car %08x upgraded model: top %d, redline cap %d, power %d km/h", key,
                             static_cast<int>(e.topSpeed * 3.6f), static_cast<int>(e.capSpeed * 3.6f),
                             static_cast<int>(e.powerSpeed * 3.6f));
            }
        }
        if (SetFullJunkman(vehicle) && nfs::performance::Measured(*vehicle, f))
        {
            Include(f);
            tops[2] = static_cast<int>(f.topSpeed * 3.6f);
            times[2] = static_cast<int>(f.zeroTo100Mph * 100.0f);
        }
        if (log)
            nfs::Log("ratings car %08x: top %d/%d/%d km/h, 0-100 mph %d/%d/%d hundredths (stock/upgraded/junkman), "
                     "maximum levels:percent %s", key, tops[0], tops[1], tops[2], times[0], times[1], times[2], percents);
        vehicle->~Instance();
        const float stock = reinterpret_cast<float *>(node + kNodeStock)[kHandling];
        const float maximum = reinterpret_cast<float *>(node + kNodeMaximum)[kHandling];
        const float junkman = maximum + (maximum - stock) * (kJunkmanLevel - 1.0f);
        if (junkman > g_scale.handlingMax)
            g_scale.handlingMax = junkman;
        ++cars;
    }
    g_scale.topMax = kTopSpeedFull;
    nfs::Log("ratings: %d cars, top speed %d-%d km/h, 0-100 mph %d-%d hundredths, handling up to %d%%, grip %d-%d%%",
             cars, static_cast<int>(g_scale.topMin * 3.6f), static_cast<int>(g_scale.topMax * 3.6f),
             static_cast<int>(g_scale.timeMin * 100.0f), static_cast<int>(g_scale.timeMax * 100.0f),
             static_cast<int>(g_scale.handlingMax * 100.0f + 0.5f), static_cast<int>(g_scale.gripMin * 100.0f + 0.5f),
             static_cast<int>(g_scale.gripMax * 100.0f + 0.5f));
}

nfs::performance::Figures g_shown;
bool g_have_shown;
/** 1 while a screen waits for its first rating, 2 once it has it. */
int g_capture;
nfs::performance::Figures g_captured;
} // namespace

bool __cdecl EstimateRatings(const Attrib::Instance *vehicle, float *out)
{
    static int original = -1;
    if (original < 0)
        original = nfs::config::Int("Debug", "OriginalRatings", 0);
    if (original)
        return NFS_ORIGINAL(0x67c570, GetPerformance)(vehicle, out);
    float stock[3], maximum[3];
    if (!NFS_ORIGINAL(0x679660, GetPerformance)(vehicle, stock) ||
        !NFS_ORIGINAL(0x6796c0, GetPerformance)(vehicle, maximum))
        return false;
    const float *weights = reinterpret_cast<const float *>(kPerformanceWeights);
    const void *junk = vehicle->GetAttributePointer(kJunkman, 0);
    const unsigned int junkman = junk ? *static_cast<const unsigned int *>(junk) : 0;
    float sums[3] = {}, totals[3] = {};
    for (int kind = 0; kind < kKinds; ++kind)
    {
        float level = NFS_ORIGINAL(0x677f80, GetPercent)(vehicle, kind);
        if (junkman & (1u << kind))
            level = kJunkmanLevel;
        for (int bar = 0; bar < 3; ++bar)
        {
            sums[bar] += level * weights[kind * 3 + bar];
            totals[bar] += weights[kind * 3 + bar];
        }
    }
    if (!g_scale.ready)
        BuildScale();
    for (int bar = 0; bar < 3; ++bar)
    {
        const float f = totals[bar] > 1e-6f ? sums[bar] / totals[bar] : 0.0f;
        float v = stock[bar] + (maximum[bar] - stock[bar]) * f;
        if (bar == kHandling)
            v /= g_scale.handlingMax;
        out[bar] = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }
    nfs::performance::Figures figures;
    if (nfs::performance::Measured(*vehicle, figures))
    {
        out[kTop] = Rate(figures.topSpeed, g_scale.topMin, g_scale.topMax);
        if (figures.zeroTo100Mph > 0.0f)
            out[kAcceleration] = 1.0f - Rate(figures.zeroTo100Mph, g_scale.timeMin, g_scale.timeMax);
        else
            out[kAcceleration] = 0.0f;
        g_shown = figures;
        g_have_shown = true;
        if (g_capture == 1)
        {
            g_captured = figures;
            g_capture = 2;
        }
    }
    return true;
}

namespace nfs::performance
{
void LogCareerRatings(FEPlayerCarDB *cars)
{
    const unsigned char *records = reinterpret_cast<const unsigned char *>(cars);
    int count = 0;
    for (int i = 0; i < kCarRecords; ++i)
    {
        const unsigned char *record = records + i * kCarRecordSize;
        const int handle = *reinterpret_cast<const int *>(record);
        if (handle == -1 || record[kRecordCareer] == kNoRecord)
            continue;
        alignas(4) unsigned char storage[kInstanceSize];
        Attrib::Gen::pvehicle *vehicle = new (storage)
            Attrib::Gen::pvehicle(*reinterpret_cast<const unsigned int *>(record + kRecordVehicle), 0, nullptr);
        if (record[kRecordCustomization] != kNoRecord)
            cars->WriteRecordIntoPhysics(static_cast<unsigned int>(handle), *vehicle);
        const Attrib::Instance *instance = reinterpret_cast<const Attrib::Instance *>(storage);
        float before[3] = {}, after[3] = {};
        NFS_ORIGINAL(0x67c570, GetPerformance)(instance, before);
        EstimateRatings(instance, after);
        char levels[64];
        int n = 0;
        for (int kind = 0; kind < kKinds; ++kind)
            n += wsprintfA(levels + n, "%s%d", kind ? "/" : "",
                           static_cast<int>(NFS_ORIGINAL(0x677f80, GetPercent)(instance, kind) * 100.0f + 0.5f));
        const void *junk = instance->GetAttributePointer(kJunkman, 0);
        const unsigned char *layout = *reinterpret_cast<unsigned char *const *>(storage + kInstanceLayout);
        const char *model = layout ? *reinterpret_cast<const char *const *>(layout + kModelOffset) : nullptr;
        Figures f = {};
        const bool measured = Measured(*instance, f);
        nfs::Log("ratings compare: %s (record %d): original top %d handling %d acceleration %d, now top %d handling %d "
                 "acceleration %d; upgrades %s (tyres/brakes/chassis/gearbox/engine/induction/nitrous), junkman %02x; "
                 "%d km/h, 0-100 mph %d hundredths, grip %d",
                 model ? model : "?", i, static_cast<int>(before[kTop] * 100.0f + 0.5f),
                 static_cast<int>(before[kHandling] * 100.0f + 0.5f),
                 static_cast<int>(before[kAcceleration] * 100.0f + 0.5f), static_cast<int>(after[kTop] * 100.0f + 0.5f),
                 static_cast<int>(after[kHandling] * 100.0f + 0.5f),
                 static_cast<int>(after[kAcceleration] * 100.0f + 0.5f), levels,
                 junk ? *static_cast<const unsigned int *>(junk) : 0u,
                 measured ? static_cast<int>(f.topSpeed * 3.6f + 0.5f) : 0,
                 measured ? static_cast<int>(f.zeroTo100Mph * 100.0f + 0.5f) : 0,
                 measured ? static_cast<int>(GripRating(f) * 100.0f + 0.5f) : 0);
        reinterpret_cast<Attrib::Instance *>(storage)->~Instance();
        ++count;
    }
    nfs::Log("ratings compare: %d career cars", count);
}

bool Shown(Figures &out)
{
    out = g_shown;
    return g_have_shown;
}

void ResetScale()
{
    g_scale.ready = false;
}

float GripRating(const Figures &figures)
{
    if (!g_scale.ready)
        BuildScale();
    return Rate(figures.grip, g_scale.gripMin, g_scale.gripMax);
}

void BeginCapture()
{
    g_capture = 1;
}

bool EndCapture(Figures &out)
{
    const bool have = g_capture == 2;
    g_capture = 0;
    out = g_captured;
    return have;
}
} // namespace nfs::performance
