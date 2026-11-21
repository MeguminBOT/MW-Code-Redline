#pragma once

/**
 * The falling rain (source/engine/rendering/effects/RainDrops.cpp, assets/shaders/rain.fx, docs/RAIN.md): drops in a box that follows the camera,
 * drawn as streaks along each drop's motion relative to the camera and depth tested against the scene, in place of the
 * post pass's pattern (nfs_post_19). Vulkan only. The weather's rain (nfs::weather::Rain) says how many are drawn.
 *
 *     [World] RainDrops = 1   0: no streaks drawn in the world (the game's own drops and the veil stay)
 */
struct eView;

namespace nfs::x360
{
/** After the player view's scene and the game's own rain: the streaks. */
void DrawRainDrops(eView *view);
/** Releases the effect and the buffers (the renderer resets or shuts down). */
void ReleaseRainDrops();
} // namespace nfs::x360
