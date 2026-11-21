#pragma once

/**
 * The sound output's own settings (source/engine/audio/Sound.cpp, docs/VULKAN.md "No DirectX"), on the Audio page
 * (source/interface/menus/SettingsPages.cpp) and in speed.ini [Sound]: a master volume over the game's mix, the output device, and
 * whether the game is heard while another window has the focus.
 */
namespace nfs::sound
{
/** Percent (0 to 100) of the game's mix that is played. */
int MasterVolume();
void SetMasterVolume(int percent);
/** Whether the game is heard while another window has the focus (it fades out otherwise). */
bool BackgroundSound();
void SetBackgroundSound(bool on);
/** The output devices: the list made again (returns how many), a device's name, the one chosen (-1: the system's
 * default, which follows Windows' own choice), and choosing one (the sound moves to it at once). */
int RefreshDevices();
const char *DeviceName(int index);
int CurrentDevice();
void SetDevice(int index);
/** The chosen output device's SDL id (SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK for the system's default), for other streams
 * of the game's sound (the movies'). */
unsigned int OutputDevice();
/** Whether the game is heard now: its window has the focus, or BackgroundSound is on. */
bool Heard();
} // namespace nfs::sound
