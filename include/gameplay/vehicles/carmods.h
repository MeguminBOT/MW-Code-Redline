#pragma once

/**
 * Car mods as text (source/gameplay/vehicles/CarMods.cpp, docs/MODDING.md): mods\cars\NAME.ini next to the exe changes the physics
 * of the car whose pvehicle is NAME, for every car of that kind on the road (the player's, opponents', traffic's and
 * the front end's figures). Sections name the class ([engine], [tires], [vehicle] for the pvehicle's own MASS and
 * TENSOR_SCALE), "top" its fully upgraded collection ([engine top]), and "real" or "standard" one car performance
 * only ([tires real]); keys are the fields (TORQUE = 120, 150, 190, ...; pairs front, rear; vectors x, y, z).
 * tools/carmod.py writes a car's file with every value commented out, to start from.
 */
namespace nfs::carmods
{
/** Writes the mods over the loaded collections: at start and after every switch of the car performance (the
 * realistic edits overwrite the same fields). */
void Apply();
} // namespace nfs::carmods
