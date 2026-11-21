#pragma once

/**
 * The test track (source/dev/TestTrack.cpp). Not in the original game.
 *
 *     -skipfe -skipfecar NAME -set Debug.TestTrack=1     the debug race's car on a long, wide, flat strip far from
 *                                                         Rockport (no opponents, traffic or cops)
 *     -set Debug.TestRun=1                                 and drives it: full throttle from a standstill in a
 *                                                         straight line until the speed stops rising, then logs the
 *                                                         times to 60 mph, 100 km/h, 100 mph and 200 km/h, the
 *                                                         quarter mile and the top speed to nfs.log
 *     -set Debug.TestRunQuit=1                             and quits after the run (tools/testtrack.py runs every car)
 *     -set Debug.TestRunLaunch=1                           and ends the run at 100 mph and the quarter mile (the launch
 *                                                         figures, not the top speed: tools/testtrack.py --launch)
 */
namespace nfs::testtrack
{
/** Debug.TestTrack is on: called before the debug race starts, sets its options. */
bool Enabled();
/** Once a frame, after the pose update (the warp, the run). */
void Update();
/** The strip around the player's car, in the player view's world pass (with the world prelit effect current). */
void Render();
} // namespace nfs::testtrack
