/**
 * Opponents and cops that drive better instead of being pulled along (gameplay/ai/aituning.h, docs/AI.md). Not in the original
 * game.
 *
 * The racers' pace comes from one skill number (AIPerpVehicle::GetSkill 0x409340: the race's or character's skill plus
 * the rubber band's "glue") read through the game's skill tables (Table objects: count, the domain's ends, its step's
 * inverse, then a pointer to the values):
 *
 *   AICorneringScaleTable 0x8eb420  share of the car's grip used in corners      0.36 .. 0.9 by skill
 *   AiSpeedScaleTable     0x8eb3bc  share of the speed the road allows            0.85 .. 1.0
 *   AiAccelScaleTable     0x8eb3e4  share of the car's acceleration              0.65 .. 1.0
 *   AiNavLookAheadTable   0x8eb434  metres the racing line looks ahead, by speed  30 .. 100
 *   AiNosScaleTable       0x8eb3a8  nitrous use                                  0.25 .. 1.0
 *
 * so a low-skill opponent took corners at a third of its grip and then needed the glue to stay in the race. Improved
 * lifts the low ends: every racer drives near its car's limits, skill still separating them. The rubber band
 * (GRaceStatus::ComputeCatchUpSkill 0x5ff990 scaled by CatchupGlueTable 0x8eb4d4 when behind and SlowDownGlueTable
 * 0x8eb4e8 when ahead; the catch-up cheat's acceleration AiCatchupAcceleration 0x8eb40c, up to 2x) is reduced to a
 * little or turned off. Cops in a pursuit drive with the same tables, so they corner and look ahead better too.
 *
 * The values are written into the tables' value arrays (some in .rdata) at start and when the page changes them; the
 * originals are kept to put back.
 */
#include <windows.h>
#include <string.h>

#include "gameplay/ai/aituning.h"
#include "core/config.h"
#include "core/log.h"

namespace
{
struct Table
{
    unsigned int address;
    int count;
    /** The improved values (or the reduced rubber band's), and with the rubber band off. */
    float improved[4];
    float off[4];
    bool rubber;
};

const Table kTables[] = {
    {0x8eb420, 2, {0.72f, 0.96f}, {}, false},                        // AICorneringScaleTable
    {0x8eb3bc, 2, {0.95f, 1.0f}, {}, false},                         // AiSpeedScaleTable
    {0x8eb3e4, 2, {0.88f, 1.0f}, {}, false},                         // AiAccelScaleTable
    {0x8eb434, 2, {40.0f, 110.0f}, {}, false},                       // AiNavLookAheadTable
    {0x8eb3a8, 2, {0.6f, 1.0f}, {}, false},                          // AiNosScaleTable
    {0x8eb4d4, 3, {0.12f, 0.24f, 0.36f}, {0.0f, 0.0f, 0.0f}, true},  // CatchupGlueTable
    {0x8eb4e8, 3, {0.3f, 0.3f, 0.2f}, {0.0f, 0.0f, 0.0f}, true},     // SlowDownGlueTable
    {0x8eb40c, 4, {1.0f, 1.08f, 1.16f, 1.25f}, {1.0f, 1.0f, 1.0f, 1.0f}, true}, // AiCatchupAcceleration
};
const int kTableCount = sizeof(kTables) / sizeof(kTables[0]);

struct State
{
    bool saved;
    float original[kTableCount][4];
    bool improved;
    int rubber;
    bool cops;
    bool traffic;
    bool read;
};

State g;

float *Values(const Table &t)
{
    return *reinterpret_cast<float **>(t.address + 0x10);
}

void Write(float *at, const float *values, int count)
{
    DWORD protection;
    if (!VirtualProtect(at, count * sizeof(float), PAGE_READWRITE, &protection))
        return;
    memcpy(at, values, count * sizeof(float));
    VirtualProtect(at, count * sizeof(float), protection, &protection);
}

void ReadSettings()
{
    char text[16];
    nfs::config::String("AI", "Driving", "improved", text, sizeof(text));
    g.improved = lstrcmpiA(text, "original") != 0;
    nfs::config::String("AI", "RubberBand", "reduced", text, sizeof(text));
    g.rubber = !lstrcmpiA(text, "original") ? nfs::aituning::kRubberOriginal
               : !lstrcmpiA(text, "off")    ? nfs::aituning::kRubberOff
                                            : nfs::aituning::kRubberReduced;
    nfs::config::String("AI", "Cops", "improved", text, sizeof(text));
    g.cops = lstrcmpiA(text, "original") != 0;
    nfs::config::String("AI", "Traffic", "improved", text, sizeof(text));
    g.traffic = lstrcmpiA(text, "original") != 0;
    g.read = true;
}
} // namespace

namespace nfs::aituning
{
bool Improved()
{
    if (!g.read)
        ReadSettings();
    return g.improved;
}

bool CopsImproved()
{
    if (!g.read)
        ReadSettings();
    return g.cops;
}
bool TrafficImproved()
{
    if (!g.read)
        ReadSettings();
    return g.traffic;
}

int Rubber()
{
    if (!g.read)
        ReadSettings();
    return g.rubber;
}

void Apply()
{
    ReadSettings();
    if (!g.saved)
    {
        for (int i = 0; i < kTableCount; ++i)
            memcpy(g.original[i], Values(kTables[i]), kTables[i].count * sizeof(float));
        g.saved = true;
    }
    for (int i = 0; i < kTableCount; ++i)
    {
        const Table &t = kTables[i];
        const float *values = g.original[i];
        if (t.rubber)
        {
            if (g.rubber == kRubberReduced)
                values = t.improved;
            else if (g.rubber == kRubberOff)
                values = t.off;
        }
        else if (g.improved)
            values = t.improved;
        Write(Values(t), values, t.count);
    }
    nfs::Log("ai: driving %s, rubber band %s, cops %s, traffic %s", g.improved ? "improved" : "original",
             g.rubber == kRubberOriginal ? "original" : g.rubber == kRubberOff ? "off" : "reduced",
             g.cops ? "improved" : "original", g.traffic ? "improved" : "original");
}
} // namespace nfs::aituning
