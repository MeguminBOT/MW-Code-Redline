#pragma once

/**
 * Where the player is, for recreating screenshots (source/dev/Pose.cpp). Not in the original game.
 *
 * World coordinates are the renderer's (Z up). The FPS counter (F11) shows the car's position and heading and the
 * camera's position, heading and pitch in the top-right corner; F9 appends them to debug_poses.txt next to the exe as
 * the options that recreate the shot:
 *
 *     -set Debug.Warp=X,Y,Z,H        puts the car there, facing H degrees, a few seconds into a race
 *     -set Debug.Camera=X,Y,Z,H,P    holds the player camera there, looking H degrees round and P degrees up
 *
 * `-set Debug.Tour=debug_poses.txt` visits every saved pose in turn (warp, held camera), saves each as pose_NN.bmp
 * next to the exe (Vulkan) once the world has loaded, and quits after the last. With the time of day's clock on, F9
 * saves its hour too (-set World.TimeOfDay=HH:MM) and the tour puts the clock there at that stop;
 * `-set Debug.TourHours=6,12,19:30` visits every pose at each of those hours instead (pose_NN_HHMM.bmp).
 *
 * `-set Debug.AutoDrive=SECONDS` lets the AI drive the player's car from that many seconds into a race.
 * `-set Debug.WarpSpeed=KMH` leaves the car moving at that speed the way it faces after each warp,
 * `-set Debug.WarpGas=FRACTION` holds that much throttle after it (steering at `Debug.WarpSteer`, -1 left to 1
 * right, straight by default), `-set Debug.WarpLookBack=SECONDS` holds
 * the look-back button from that long after it, and `-set Debug.LogCar=SECONDS` logs the car's position that often: driving tests
 * without input (does it get through there).
 */
namespace nfs::pose
{
const int kMaxLines = 4;
const int kLineLength = 64;

/** Once a frame, before the views are drawn: the F9 key, the warp and the held camera. */
void Update();
/** The overlay's lines (car, camera); returns how many there are (none without a player car). */
int Text(char (*lines)[kLineLength]);
/** Asks for the car to be put at X, Y, Z facing `heading` degrees a few seconds into the next race, as
 * Debug.Warp does. */
void RequestWarp(float x, float y, float z, float heading);
/** True once a requested warp has placed the car and the world around it has loaded. */
bool WarpFinished();
/** The player car's position (X, Y, Z) and heading in degrees; false without a player car. */
bool PlayerCar(float out[4]);
/** The player car's simulation object (ISimable), or null. */
void *PlayerSimable();
/** One of the player car's interfaces by its _IHandle function's address (IVehicle 0x4040e0, IRigidBody 0x4039f0,
 * IInput 0x403f90, IEngine 0x404020, ITransmission 0x404010), or null. */
void *PlayerInterface(unsigned int handle);
/** A car's controls as its input gives them to its behaviours (PInput::GetControls): Debug.WarpGas's throttle. */
void Controls(void *input, unsigned char *controls);
/** Whether Debug.WarpLookBack holds the look-back button now. */
bool LookBackHeld();
/** Whether the look-back button read as held the last time the game asked. */
bool LookingBack();
extern bool g_lookingBack;
} // namespace nfs::pose
