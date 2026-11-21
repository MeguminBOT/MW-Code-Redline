#pragma once

/**
 * The player's preferences in speed.ini (source/gameplay/progression/Preferences.cpp), for every profile alike. Not in the original game,
 * which kept the video options in the registry and the audio and gameplay options in each profile.
 *
 *     [Video]     the video options (LoadVideoOptions 0x6c1c00 and SaveVideoOptions 0x6c1f10 read and write them here)
 *     [Audio]     the audio options: volumes, audio mode, interactive music, EA Trax
 *     [Gameplay]  the gameplay options: autosave, rear view mirror, damage, speedometer units, the maps, cameras
 *
 * The first start reads the registry's video options and the loaded profile's audio and gameplay options and writes
 * them here. After that a profile that loads, or a new one, takes these, and leaving an options page writes them.
 */
class UserProfile;

namespace nfs::prefs
{
/** Whether speed.ini has the video options (the registry's "FirstTime" no longer decides the first start). */
bool HaveVideo();
/** The profile takes speed.ini's audio and gameplay options (or, the first time, gives them theirs). */
void ApplyToProfile(UserProfile *profile);
/** Writes the profile's audio and gameplay options to speed.ini. */
void StoreFromProfile(const UserProfile *profile);
} // namespace nfs::prefs
