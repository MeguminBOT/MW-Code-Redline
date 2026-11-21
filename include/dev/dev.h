#pragma once

/**
 * Development tools (source/dev/DevTools.cpp, docs/DEV.md), on with -dev on the command line (a terminal's, a shortcut's
 * or a launcher's launch options). Not in the original game.
 *
 *     F5                         path recording on and off: the player's car at every simulation step, to
 *                                paths\path_DATE_TIME_CAR.nfspath in the dev folder (Folder: dev next to the exe,
 *                                or [Dev] Folder)
 *     -set Dev.Record=1          recording on from the start
 *     -set Dev.PlayPath=FILE     plays a recording (absolute, or in the dev folder; a leading dev\ is dropped): the car
 *                                goes to its start and follows it exactly, the frame times logged at its end and
 *                                written to FILE.frames.csv
 *     -set Dev.Folder=PATH       where the dev tools write (Folder)
 *     -set Dev.PlayPathQuit=1    quits at the end of the playback (profiling runs)
 *
 * A recording is a replica of a drive: the car's position, orientation and velocities, and the controls, gear, engine
 * speed and nitrous of every step, 60 a second - the states and actions to train a driver on, and a real drive to
 * repeat for profiling. tools/paths.py reads them (CSV).
 */
namespace nfs::dev
{
/** -dev was given. */
bool Enabled();
/** Once a frame: the dev keys, playback's start and end, and its frame times. */
void Update();
/** A vehicle's controls for this simulation step (PInput::GetControls 0x69cbb0; `input` its IInput, `controls` its
 * InputControls): the player car's are recorded, or played from a recording. */
void Controls(void *input, unsigned char *controls);
/** At the game's end: closes the recording. */
void Shutdown();
/** The overlay's status line (recording, playing); false when there is none. */
bool Status(char *line, int size);
/** The folder the dev tools write to and read relative files from, created when missing: [Dev] Folder (absolute, or
 * relative to the exe), else dev next to the exe. tools/rungame.py points its runs at the workspace's dev folder, so
 * the game folder holds only the game's files. `sub` (a subfolder, may be empty) is appended and created too. No
 * trailing backslash. */
void Folder(char *out, const char *sub);
} // namespace nfs::dev
