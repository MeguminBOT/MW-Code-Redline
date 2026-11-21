#pragma once

/**
 * The proving ground (source/dev/ProvingGround.cpp, docs/PROVING_GROUND.md): a 2000 m straight with a braking zone and,
 * beside it, a 1.8 km handling loop (hairpin, fast sweeper, chicane, esses, a decreasing-radius corner, a bus stop),
 * walled, far from Rockport, with timing on both and live tuning of the car (gameplay/vehicles/tuning.h). Not in the original game.
 *
 *     main menu: DEBUG PHYSICS                               choose a car, then the proving ground
 *     -skipfe -skipfecar NAME -set Debug.ProvingGround=1    the debug race's car alone on the proving ground
 *     -set Debug.ProvingStart=loop                           starts on the handling loop (the straight by default)
 *     -set Debug.ProvingAuto=1                               drives the straight hands-off once (full throttle, lane
 *                                                            keeping, braking to a stop after the 2000 m board)
 *     -set Debug.ProvingQuit=1                               quits after that run (results in nfs.log)
 *     -set Debug.ProvingTune=engine.TORQUE*1.2;pvehicle.MASS=1300   tuning edits made on arrival (gameplay/vehicles/tuning.h)
 *     -set Debug.ProvingKeys=SECONDS:KEY,...                 presses the proving ground's keys at those times into the
 *                                                            race (KEY: the names in docs/PROVING_GROUND.md)
 *
 * The keys (numeric keypad and the six-key block, so the driving keys stay free) are in docs/PROVING_GROUND.md and on
 * the panel's help page.
 */
namespace nfs::proving
{
const int kMaxLines = 40;
const int kLineLength = 72;

/** Debug.ProvingGround is on (or the front end's entry started the race). */
bool Enabled();
/** The main menu's entry (source/interface/menus/UIMain.cpp, on unless [Debug] ProvingMenu=0): sets up a quick race of the car alone
 * (a sprint, no opponents, traffic or cops) whose start is the proving ground, and opens the quick race car select. */
bool MenuShown();
void StartFromMenu();
/** The front end's package switches (cFEng::QueuePackageSwitch): the package to go to in place of `package`, or null.
 * The entry's switch goes to the car select; leaving the car select other than into the race ends the request. */
const char *PackageFor(const char *package);
/** The car select is open for the proving ground: every car is on offer. */
bool ChoosingCar();
/** Debug.CarTune's edits are given and not made yet (the test track's run waits for them). */
bool CarTunePending();
/** Once a frame, outside the simulation: keys, the arrival warp, reloading the car's behaviours. */
void Update();
/** The world file's models (TRACKS\PROVING.BIN, tools/provingworld.py) into a view: from RenderWorldModels, for the
 * player view and the sun shadow cascades. */
void RenderWorldModels(class eView *view, int flags);
/** The course around the camera, in the player view's world pass (with the world prelit effect current), when there
 * is no world file. */
void Render();
/** Every simulation step, where the player car's behaviours read its controls: walls, timing, the hands-off run. */
void Controls(void *input, unsigned char *controls);
/** A point in the renderer's axes (Z up) on the proving ground's site, while it is in use. */
bool AtSite(const float *point);
/** A point in the simulation's axes over the proving ground's flat ground. */
bool OverGround(const float *point);
/** The panel's lines (telemetry, tuning or help); returns how many (0 while it is hidden). */
int Text(char (*lines)[kLineLength]);
} // namespace nfs::proving
