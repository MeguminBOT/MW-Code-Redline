#pragma once
/**
 * The post processing after the views (source/engine/rendering/post/PostProcess.cpp, assets/shaders/post.fx): exposure with eye adaptation, bloom,
 * motion blur, depth of field and the visual treatment's look, on both renderers.
 */
namespace nfs::x360
{
struct Shader;

/** Middle gray, which the exposure brings the adapted luminance to (cutscenes change it). */
extern float g_middleGray;

/** Creates the post effect once (with the first eEffect::Load). */
void LoadPostEffect();
/** Releases it with the game's effects at shutdown, while the textures it holds (the vignette's) are still there. */
void ReleasePostEffect();
/** The post targets for the render size, after eCreateRenderTargets (device creation and resets). */
void CreatePostTargets();
/** Before the device resets. */
void ReleasePostTargets();
/** The frame's look (curves, saturation, exposure) on another effect that draws it: the cube map mirror. */
void ApplyLook(Shader &shader);
/** Where the sun was drawn this frame (texture coordinates of the screen): the post processing measures how much of it
 * the scene hides, in place of the original's occlusion queries. */
void SetSunPosition(float u, float v);
/** The share of the sun the scene left visible, as measured a few frames ago (0 to 1). */
float SunVisibility();
} // namespace nfs::x360
