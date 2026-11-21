#pragma once

/**
 * Exact replays (source/dev/Replay.cpp, docs/REPLAY.md), with -dev: every car in the world at every simulation step.
 *
 *     F6                          recording on and off: replays\replay_DATE_TIME_CAR.nfsreplay in the dev folder
 *     -set Dev.Replay=1           recording on from the start
 */
namespace nfs::replay
{
/** Once a frame: the key, the start and end of a drive's recording. */
void Update();
/** A vehicle's controls for this simulation step (PInput::GetControls 0x69cbb0): each vehicle's are kept for the
 * step's record, and the first call of a step writes the step before it. */
void Controls(void *input, unsigned char *controls);
/** At the game's end: closes the recording. */
void Shutdown();
/** The overlay's status line; false when nothing is recorded. */
bool Status(char *line, int size);
} // namespace nfs::replay
