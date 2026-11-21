#pragma once

/**
 * The world's lamps at night (source/engine/rendering/lights/Lamps.cpp, docs/NIGHT.md): the street lamps, lanterns, canopies and the
 * floodlights on signs that tools/lamps.py finds in the track's models (GLOBAL\LAMPS.TXT). Most Wanted's lamps give no
 * light; these do, as strongly as the hour has the local lights on (nfs::timeofday::LocalLights).
 *
 * Lamps never move, so their light is baked: into two volumes around the camera (2 m cells within 128 m, 8 m cells
 * within 512 m), which the rebuild's own shaders read in place of a light each (lighting.hlsli LampLight). The volumes
 * are rings: as the camera moves, the slabs it reaches are baked and uploaded, a few thousand cells at a time. The
 * lights that move or change (the cars' lights, signals) stay lights (engine/rendering/lights/lights.h).
 *
 * The volumes also hold the tunnels (LAMPS.TXT's roofs): under a tunnel's roof the light baked into the world stays as
 * at the game's own hour (nfs::timeofday::TunnelLight), since a tunnel's lamps light it day and night, and the rain
 * stays out (lighting.hlsli RainReaches). So the volumes are kept by day too.
 *
 * The lamps and the cars' lights also glow in the air (Vulkan): a very light haze on a clear night, more in fog and
 * rain (nfs::timeofday::HazeExtinction). The post processing adds the light they scatter towards the eye along each
 * pixel's ray (source/engine/rendering/post/PostProcess.cpp DrawHaze, assets/shaders/owned/nfs_post_19_ps_3_0.hlsl), from the nearest lamps in
 * front of the camera and the lights that move.
 *
 *     [World] Lamps = 1     0 leaves the lamps dark
 *     [World] Tunnels = 1   0: tunnels as dark as the hour outside (the game's tunnel zones still light the world while
 *                           the camera is in one)
 *     [World] LampHaze = 1  how strongly the lights glow in the haze (0: not at all)
 */
namespace nfs::lights
{
struct GpuLight;
}

namespace nfs::lamps
{
/** The lamps the haze glows around at most (nfs::shadow::SceneConstants::hazeLamps). */
const int kHazeLamps = 32;

/** Once a frame before the scene constants: loads the lamps, moves and bakes the volumes around the camera, and puts
 * them in the renderer's scene set. `volumes`: per level, the world position of its first cell and its cell size (0
 * while it is not ready), for the scene constants (nfs::shadow::SceneConstants::lampVolume). */
void Update(const float camera[3], float volumes[2][4]);
/** The lamps' glows for the views (light flares, engine/rendering/lights/flares.h): from the player view's scenery culling, once a frame. */
void AddGlows(const float camera[3]);
/** True when the volumes hold the tunnels' roofs (LAMPS.TXT's, with the Vulkan renderer): the rebuild's shaders keep
 * the light baked under a roof as at the game's own hour, pixel by pixel (lighting.hlsli WorldLight). */
bool Tunnels();
/** How far a tunnel's roof covers the camera of the last Update (0 to 1). */
float CameraCover();
/** Releases the volumes (the renderer resets or shuts down); the next Update bakes them anew. */
void Release();
/** Once a frame with the scene constants: the haze the lights glow in (its extinction per metre, its
 * Henyey-Greenstein g, how many of `lamps` are in use, how strongly the lights glow: 0 for no glow) and the lamps
 * nearest in front of the camera looking along `forward`, as lights (kHazeLamps at most). */
void Haze(const float camera[3], const float forward[3], float haze[4], nfs::lights::GpuLight *lamps);
/** How strongly the lights glow, from the last Haze (0: the post processing draws no glow). */
float HazeStrength();
/** [World] LampHaze: how strongly the lights glow in the haze at most (0: not at all), and setting it (saved). */
float HazeScale();
void SetHazeScale(float scale);
/** A smackable prop was knocked over, its model's origin at `position` (source/gameplay/world/SceneryChanges.cpp): a lamp post's
 * lamp (LAMPS.TXT's base) goes out until the camera has left it far behind. */
void KnockedOver(const float position[3]);
} // namespace nfs::lamps
