#pragma once

/**
 * The rebuild's own options pages (source/interface/menus/SettingsPages.cpp): Controller Settings (vibration, the sticks' and
 * triggers' dead zones, the steering response; docs/CONTROLS.md) and World Settings (time of day, clock speed,
 * weather, lightning, lamp glow; docs/NIGHT.md) and Camera Settings (presets of height, distance, tilt and field of
 * view per view; engine/rendering/camera/camera.h). Each is an entry of the options menu that opens a page of rows; every
 * row is a speed.ini key, in use and saved as soon as it changes.
 */
#include "game/gen/api.h"

namespace nfs::settingspages
{
/** The pages' options categories (UIOptionsScreen; the new profile's page is 10). */
const int kControllerCategory = 11;
const int kWorldCategory = 12;
const int kCameraCategory = 13;
const int kAiCategory = 14;

/** Whether `category` is one of these pages. */
bool Owns(int category);
/** Builds the rows of the page of `category` (UIOptionsScreen::Setup). */
void Setup(UIOptionsScreen *screen, int category);
/** Every setting of the page back to its default (the screen's "restore defaults"). */
void Defaults(int category);
/** The Audio page's rows of the rebuild's own sound output (engine/audio/sound.h): Master Volume before the game's rows
 * (`first`), Output Device and Sound in Background after them. */
void AddAudioRows(UIOptionsScreen *screen, bool first);
/** Their defaults (the output device stays). */
void AudioDefaults();
} // namespace nfs::settingspages
