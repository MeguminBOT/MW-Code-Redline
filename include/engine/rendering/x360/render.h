#pragma once
/**
 * The Xbox 360 rendering state the ported X360 Stuff code shares (source/engine/rendering, docs/X360.md). The options are the plugin's
 * X360Stuff.ini settings, fixed at their shipped values.
 */
#include "game/platform/Effect.h"

class TextureInfo;

namespace nfs::x360
{
/** X360Stuff.ini, as shipped. */
namespace options
{
const bool kGrassShader = true;
const bool kDoParallax = true;
const bool kCubemapRVM = true;
const bool kCarsInAllCubemapFaces = false;
/** 1: depth-masked motion blur, 2: without the depth mask, 0: off. */
const int kMotionBlur = 1;
const bool kFixedTreeRenderOrder = true;
/** On (X360Stuff shipped it off): the low detail car technique has no local lights or surroundings, which left traffic
 * and cops black at night next to lit racers. */
const bool kUseFullLODShaderOnTrafficAndCops = true;
const int kResetOvercastOverrideAfterNIS = 1;
const float kEnvMapFarPlane = 100.0f;
const float kMirrorFarPlane = 400.0f;
/** 0: Xbox 360 shaper lights. */
const int kCarShaperLightType = 0;
const bool kCarSelfShadow = false;
const float kDirectionalMotionBlurStrength = 1.0f;
const float kAngularMotionBlurStrength = 0.0f;
const float kMinimumShadowAngleHeight = 0.7f;
const bool kXbox360ShadowAngle = true;
const bool kNISTimeOfDayEvent = true;
const bool kCurrentGenSunFlare = false;
const float kMiddleGrayValue = 0.14f;
const float kForcedLuminance = 0.0f;
} // namespace options

/** What the world draws are for: the main view, the low detail reflection views, or depth only (shadow map, the depth
 * pass before the main view). */
enum PassMode
{
    kPassMain = 0,
    kPassLowLod = 1,
    kPassDepth = 2,
};

extern int g_passMode;
/** Set once the main view's depth pass is done this frame (the view is then drawn again in colour). */
extern bool g_depthPassDone;
/** Begin flags for the Xbox 360 effects (D3DXFX_*). */
extern unsigned long g_beginFlags;
/** The light scattering (fog) constants of the current view: FOG_BR_PLUS_BM, FOG_ONE_OVER_BRBM, FOG_CONST_1-3 and the
 * distance scale; null without light scattering. */
extern const float *g_fog;
/** The same before the world's haze is matched to the hour's sky (source/engine/rendering/materials/LightScattering.cpp): the sky's own. */
extern const float *g_skyFog;
/** The grass Attrib instance (null if the database has none). */
extern void *g_grassAttributes;
/** The depth texture the main view's depth pass writes (INTZ) and its surface; null if the device has no INTZ. */
extern void *g_depthTexture;
extern void *g_depthSurface;
/** Motion blur's depth-masked mode; 0 when depth effects are off. */
extern int g_motionBlur;
extern bool g_depthOfField;

/** Motion blur or depth of field needs the main view's depth this frame (Rendering::ShouldDrawDepth). */
bool ShouldDrawDepth();
/** A camera with depth of field (Rendering::DepthOfFieldIsActive). */
bool DepthOfFieldActive();
/** ShouldDrawDepth, and the motion blur video option on: the main view's colour pass is the depth pass's second. */
bool DepthPassActive();

/** The texture setup after the diffuse texture is bound (Rendering::TextureAssignmentHook). */
void AssignTextures();
/** Forgets what AssignTextures last set up (an effect was recreated: video options, device reset). */
void ResetMaterialCache();
/** X360 Stuff's changes to the game's data tables, once at start (source/engine/rendering/frame/Misc.cpp). */
void ApplyDataChanges();
/** The dynamic shadow pass (sub_6e44c0) with the Xbox 360 light direction and shadowless scenery
 * (source/engine/rendering/shadows/Shadows.cpp). */
void RenderShadowPass(class eView *view, void *cull);
/** The dynamic shadow pass's changes to the scenery a view draws (source/engine/rendering/shadows/Shadows.cpp): the garage's casters in the
 * front end, and the scenery that casts no shadow marked. Nothing outside that pass. */
void PrepareShadowScenery(class GrandSceneryCullInfo *cull, class eView *view);
} // namespace nfs::x360
