#pragma once

/**
 * The player's own camera adjustments (source/engine/rendering/camera/CameraOffsets.cpp, docs/CONTROLS.md "Camera Settings"): per view
 * (Bumper, Hood, Close, Far) a height, a distance, a tilt and a field of view added to what the car's camera data
 * gives, in presets the player keeps (speed.ini [Camera] Preset, [CameraPreset1] to [CameraPreset3]). Preset 0 is the
 * game's own cameras. A car can have adjustments of its own on top of the preset's, whatever preset is chosen
 * ([CameraCar NAME], NAME the car's type, as CARS/ names it): for a car whose roof hides the road.
 */
namespace nfs::camera
{
/** The views the player chooses between in a race, in the game's order (CameraAnchor::GetPov's POV types 0 to 3). */
enum View
{
    kBumper,
    kHood,
    kClose,
    kFar,
    kViews
};

/** What a preset changes in a view: metres up, metres back, degrees down, degrees wider. */
enum Adjustment
{
    kHeight,
    kDistance,
    kTilt,
    kFov,
    kAdjustments
};

const int kPresets = 3;

/** The preset in use (0: the game's cameras) and choosing one (saved). */
int Preset();
void SetPreset(int preset);
/** An adjustment of a view in a preset (0 for preset 0), and changing it (saved; in use at once). */
float Get(int preset, int view, int adjustment);
void Set(int preset, int view, int adjustment, float value);
/** The view the player's camera shows now (kFar when there is none). */
int CurrentView();
/** The car the player's camera last followed (its type's name, upper case; empty before the first drive). */
const char *CurrentCar();
/** An adjustment of a view for the current car alone, and changing it (saved; in use at once). */
float GetCar(int view, int adjustment);
void SetCar(int view, int adjustment, float value);
} // namespace nfs::camera
