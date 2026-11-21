/**
 * Speed blur (original: zPlatform 0x6dbb20), drawn by the 3D frame when the video options' MotionBlurEnable is on.
 *
 * Above SpeedBlurMinSpeed the frame is copied, blurred twice along the camera's motion as it shows on screen, and blended
 * back over itself, more the faster the camera moves (full at SpeedBlurMaxSpeed), plus whatever extra the screen
 * effects ask for (SpeedBlurBoost, faded in and out a step per frame unless a cutscene turned it off). The blend is
 * capped at SpeedBlurMaxAmount; with depth of field active it goes through the frame's alpha instead of a blend factor.
 *
 * Change from the original: the blend and the blur length are scaled by the Motion Blur option (engine/rendering/display/display.h; 100% is
 * the original, 0 draws nothing).
 */
#include <math.h>
#include <stddef.h>

#include "game/platform/Platform.h"
#include "engine/rendering/display/display.h"
#include "legacy/link.h"
#include "engine/rendering/vulkan/renderer.h"

static_assert(sizeof(CameraParams) == 0xE0 && offsetof(Camera, VelocityKey) == 0x1C0, "Camera layout");

extern "C" void *__stdcall D3DXVec4Transform(void *out, const void *v, const void *m);

/** Blurs `source` into `target` with `taps` samples weighted by `weights`, spaced by `offsets`. */
void eRenderBlurPass(IDirect3DSurface9 *target, IDirect3DTexture9 *source, int taps, const float *weights,
                     const float *offsets);
/** Draws `textures` over `target` as one screen quad tinted by `colors`, blended with `src_blend` / `dest_blend`. */
void eDrawScreenQuad(IDirect3DSurface9 *target, int texture_count, IDirect3DTexture9 **textures,
                     const unsigned int *colors, int unknown_5, int src_blend, int dest_blend, int unknown_8);

namespace
{
/** The player's view (EVIEW_PLAYER1), whose camera the blur follows. */
const int kPlayerView = 1;
/** One step of an 8-bit color channel. */
const float kOneLevel = 1.0f / 255.0f;
const float kBoostScale = 0.2f;
const float kBoostWeight = 1.1f;
const float kLengthScale = 1.1f;
const float kSecondPassSpread = 4.0f;
const int kTaps = 4;
} // namespace

void eRenderSpeedBlur()
{
    const float boost = !QueryFlushAccumulationBuffer() && SpeedBlurNisEnabled ? SpeedBlurBoost * kBoostScale : 0.0f;
    if (fabsf(SpeedBlurBoostFade - boost) > kOneLevel)
        SpeedBlurBoostFade += SpeedBlurBoostFade < boost ? SpeedBlurFadeInStep : -SpeedBlurFadeOutStep;

    const Camera *camera = eViews[kPlayerView].pCamera;
    const bVector3 &velocity = camera->VelocityKey.Position;
    const float speed = sqrtf(velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z);
    bVector4 motion(velocity.x, velocity.y, velocity.z, 0.0f);
    D3DXVec4Transform(&motion, &motion, &camera->CurrentKey.Matrix);
    float offsets[kTaps] = {};
    if (speed > 1e-5f)
    {
        bVector4 direction;
        bNormalize(&direction, &motion);
        offsets[0] = direction.x + direction.z;
        offsets[1] = direction.y + direction.z;
        offsets[2] = direction.x - direction.z;
        offsets[3] = direction.y - direction.z;
    }
    if (speed <= SpeedBlurMinSpeed)
        return;

    const float ramp =
        speed < SpeedBlurMaxSpeed ? (speed - SpeedBlurMinSpeed) / (SpeedBlurMaxSpeed - SpeedBlurMinSpeed) : 1.0f;
    float amount = ramp + SpeedBlurBoostFade * kBoostWeight;
    if (amount > SpeedBlurMaxAmount)
        amount = SpeedBlurMaxAmount;
    amount *= static_cast<float>(nfs::display::MotionBlur()) * 0.01f;
    if (amount < kOneLevel)
        return;
    // Post effects are not drawn with Vulkan yet (docs/VULKAN.md, milestone 3).
    return;
}
