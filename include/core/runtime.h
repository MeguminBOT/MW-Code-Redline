#pragma once

namespace nfs
{
/** Runs static constructors of code compiled from src/. Called first thing in main(). */
void RunStaticInitializers();
/** Logs access violations in game code to nfs.log (address, registers, return addresses; see tools/symbolize.py). */
void InstallCrashLog();
/** With -mute, mutes the process's audio session (test runs); without it, unmutes it (Windows keeps the mute). */
void MuteAudioIfAsked();
} // namespace nfs
