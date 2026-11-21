#pragma once

/**
 * Frames between simulation steps. The game simulates in fixed 1/60 s steps (Scheduler::Run). When frames come faster
 * than that, most of them run no step, so they show the world part of the way between the last two steps instead:
 * every world body (the render-side copy of a simulated object's pose, which cars, the helicopter and props are drawn
 * from) and every view camera is placed at the fraction of a step that has passed. The render connections follow the
 * bodies every frame, so wheels, smoke and sparks move smoothly too. What is shown lags the simulation by up to one
 * step; at 60 frames per second or fewer nothing changes from the original.
 */
namespace nfs::present
{
/** True when frames may come faster than steps, so they are interpolated. */
bool Active();
/** After a step's world update: the bodies' poses at that step. */
void CaptureBodies();
/** After a step's camera update: the cameras at that step. */
void CaptureCameras();
/** Every frame while the simulation runs: places the bodies for this frame and updates the render connections. */
void AnimateRender();
/** Just before the frame is drawn: places the cameras for this frame. */
void ApplyCameras();
/** Just after the frame is drawn: puts the bodies and cameras back where the last step left them. */
void Restore();
/** For HUD readings of the simulation (the tachometer, the minimap's view of the player): `values` are this frame's
 * readings of what `key` identifies, and come back placed between the last two steps like the world. */
void Interpolate(const void *key, float *values, int count);
} // namespace nfs::present
