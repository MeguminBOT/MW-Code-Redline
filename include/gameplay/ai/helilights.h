#pragma once

/**
 * Lights on the pursuit helicopter at night (source/gameplay/ai/HelicopterLights.cpp), with [AI] Cops improved: a
 * blinking red beacon, white strobes and a searchlight on the player's car. The original flies it dark.
 */
namespace nfs::helilights
{
/** Once a frame for the player's view, next to the lamps' glows. */
void AddFlares();
} // namespace nfs::helilights
