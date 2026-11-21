/**
 * Traffic and the time of day (docs/NIGHT.md): AITrafficManager::ComputeDensity (0x41ec20), the share of its traffic
 * the manager keeps on the roads - all of it in free roam, a race's own setting in races, none in cutscenes or when the
 * cops clear the roads - times the hour's Traffic (gameplay/world/timeofday.h): thin at night, full at the rush hours; and times
 * the player's own share (gameplay/world/traffic.h, [World] Traffic).
 * -trafficdebug logs the density every 5 s.
 */
#include <windows.h>

#include "game/gen/api.h"
#include "legacy/link.h"
#include "core/log.h"
#include "core/config.h"
#include "gameplay/world/timeofday.h"
#include "gameplay/world/traffic.h"
#include "engine/rendering/vulkan/renderer.h"

namespace
{
float g_scale = -1.0f;

float Scale()
{
    if (g_scale < 0.0f)
    {
        const float v = nfs::config::Float("World", "Traffic", 1.0f);
        g_scale = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    }
    return g_scale;
}
} // namespace

void nfs::traffic::SetScale(float scale)
{
    g_scale = scale < 0.0f ? 0.0f : scale > 1.0f ? 1.0f : scale;
}

// The original is private: its name differs from the public declaration's (game/gen/api.h).
NFS_REPLACE(0x41ec20, AITrafficManager::ComputeDensity)

float AITrafficManager::ComputeDensity() const
{
    const float game = NFS_ORIGINAL(0x41ec20, float(__thiscall *)(const AITrafficManager *))(this);
    const float hour = nfs::timeofday::Traffic() * Scale();
    static const bool debug = nfs::vk::HasArgument("-trafficdebug");
    static DWORD logged;
    if (debug && GetTickCount() - logged > 5000)
    {
        logged = GetTickCount();
        nfs::Log("traffic: density %d%% (the game's %d%% times the hour's and the player's %d%%)",
                 static_cast<int>(game * hour * 100.0f), static_cast<int>(game * 100.0f), static_cast<int>(hour * 100.0f));
    }
    return game * hour;
}
