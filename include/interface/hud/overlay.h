#pragma once

/**
 * Frame rate counter: frames per second and the renderer in use (VULKAN or D3D9), the average and longest frame time
 * of the last half second, and a graph of the last 120 frame times, drawn in the top-left corner over everything else;
 * in races also the car's and camera's position in the top-right corner (dev/pose.h). F11 turns it on and off; the
 * choice is kept in speed.ini ([Display] FpsCounter).
 */
namespace nfs::overlay
{
/** Counts the frame and, while the counter is on, draws it onto the finished frame. Call once per frame, just before
 * the frame is presented. */
void Draw();
/** Whether the counter is shown, and turning it on or off (saved to speed.ini like F11's toggle). */
bool Shown();
void SetShown(bool shown);
/** Releases what Draw keeps on the device. Call before the device is reset or released. */
void ReleaseDeviceObjects();
} // namespace nfs::overlay
