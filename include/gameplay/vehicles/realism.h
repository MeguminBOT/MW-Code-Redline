#pragma once

/**
 * Realistic or standard car performance (source/gameplay/vehicles/Realism.cpp). Not in the original game.
 *
 * A profile is made with one or the other, and its save keeps the choice (gameplay/progression/profileoptions.h; a save made before
 * that has it in profiles.ini, [Profiles] <name> = real or standard, standard when it has none); `[Gameplay] Physics =
 * real` or `game` makes every profile one or the other (the test track's runs).
 */
namespace nfs::realism
{
/** After the attribute database has loaded (LoadGlobalChunks): realistic from the start with Physics = real. */
void Init();
/** A profile has loaded: its car performance. */
void UseProfile(const char *name);
/** Records a new profile's choice (profiles.ini, until its next save takes it in) and switches to it. */
void ChooseForProfile(const char *name, bool real);
/** The cars have their realistic physics. */
bool Active();
} // namespace nfs::realism
