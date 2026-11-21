#pragma once

/**
 * Test profiles (source/dev/TestProfile.cpp). Not in the original game.
 *
 *     [UnlockAll]
 *     TEST = 1                      the profile TEST has everything unlocked and ten million in its career
 *
 *     -set Debug.FEText=TEST        the next name the front end asks for starts as TEST (headless runs)
 */
namespace nfs::testprofile
{
/** The new career held back for a new alias's options page (nfs::profileoptions::HoldCareer), started now. */
void StartHeldCareer();
/** A profile has loaded (its UserProfile): everything unlocked if [UnlockAll] names it, or it is -dev's Developer. */
void Apply(void *profile);
/** The profile loaded is -dev's Developer. */
bool Developer();
} // namespace nfs::testprofile
