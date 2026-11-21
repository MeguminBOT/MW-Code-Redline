#pragma once

/**
 * Traffic (source/gameplay/ai/TrafficDensity.cpp): the share of the game's traffic the player wants on the roads, on top of the
 * game's own (free roam, a race's setting) and the hour's (gameplay/world/timeofday.h). speed.ini `[World] Traffic`, 0 to 1;
 * Options > World Settings.
 */
namespace nfs::traffic
{
void SetScale(float scale);
}
