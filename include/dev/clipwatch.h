#pragma once

/**
 * `-set Debug.ClipWatch=X,Y,R`: a log of what the streamer and the world collision do while the player's car is within
 * R metres of X, Y (source/dev/ClipWatch.cpp), for finding how a car gets through a wall there. Not in the original game.
 *
 * Ten times a second: the car (place, heading), the player camera (place, distance from the car), where the streamer
 * loads around, whether the look-back button is held, the camera setting, and which collision packs with a wall near
 * X, Y are loaded. At once: one of those packs loaded or unloaded, a track section activated, unloaded or thrown out,
 * the streamer's zones switched.
 */
namespace nfs::clipwatch
{
/** Once a frame. */
void Update();
/** The streamer did something to a section (or switched its zones to `number` and `other`). */
void Event(const char *what, int number, int other = -1);
} // namespace nfs::clipwatch
