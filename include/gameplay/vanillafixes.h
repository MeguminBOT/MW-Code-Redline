#pragma once

/**
 * Fixes of the original game's own bugs, for the profiles that turn on Vanilla Bug Fixes (gameplay/progression/profileoptions.h).
 * With it off, every one of them stays as the original game has it.
 *
 * - A race barrier's end that a car can drive around (source/gameplay/world/BarrierFixes.cpp).
 * - The world streaming around the camera rather than the car: a camera behind a wall (looking back, nose against
 *   it) unloads the wall's collision and the car drives through (source/gameplay/world/TrackStreaming.cpp).
 *
 * - Opponents and cops stopping dead where the world is not loaded around them (source/gameplay/ai/SimplePhysics.cpp).
 * - Cops unable to see, reach or bust a car standing where no road goes (source/gameplay/ai/CopsOffRoad.cpp).
 * - Roadblock cars laid past the road's edge, off the world's collision: hidden and nothing to hit, so a roadblock showed
 *   on the map with one car or none on the road. A roadblock is now only laid where every piece has ground under it
 *   (source/gameplay/ai/CopManager.cpp, AICopManagerCreateRoadBlock), and a car that still ends up off the world goes
 *   back onto the road (source/gameplay/ai/PursuitSupport.cpp).
 *
 * Debug.VanillaFixes=1 (or 0) turns them on (or off) whatever the profile says: the debug race (-skipfe) has no
 * profile of its own.
 */
namespace nfs::vanillafixes
{
/** Whether the fixes apply: the loaded profile's Vanilla Bug Fixes (or Debug.VanillaFixes). */
bool On();
/** A track section was activated (its scenery and collision loaded): the fixes that change them. */
void SectionActivated(int section);
} // namespace nfs::vanillafixes
