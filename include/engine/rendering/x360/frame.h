#pragma once
/**
 * The Xbox 360 frame (source/engine/rendering/frame/Frame.cpp, Passes.cpp): the per-pass effect setup and the hooks the views call.
 */
namespace nfs::x360
{
/** The environment map face being drawn, counted up from 0x11 (0x14 is the rear face the mirror shows). */
extern int g_envFace;
/** Seconds since the previous frame. */
extern float g_frameSeconds;

/** The views and the post processing, in place of the views alone (Rendering::eRenderWorldMultiPlayer). */
void RenderWorld();
/** The post processing and final render after the views (Rendering::ExecutePostProcessingAndFinalRender). */
void ExecutePostProcessing();

/** Loads the next generation sky's solid (FRONTEND\PLATFORMS\NEXTGENSKY.BIN) and waits for it (LoadGlobalChunks). */
void LoadNextGenSky();
/** The sun is in front of the view's camera (Rendering::SunPlat__Render_Hook draws it only then). */
bool SunVisible(class eView *view);
/** The sun of the player view (in place of zPlatform 0x6d04e0; source/engine/rendering/sky/Sun.cpp). */
void RenderSun(class eView *view);
/** Before the views (Rendering::ePreDisplay in place of ePreDisplay): model animation, light flares and the render
 * size; the tunnel bloom, tinting, screen effects and sun flare of the original are left out. */
void PreDisplay();
/** The front end effects' constants for the Xbox 360 shaders (the parameter part of Rendering::FESetMatrix1): white
 * diffuse colour, BASEPOLY as the mask and the standard brightness. */
void SetFrontEndConstants();
/** BASEPOLY (the front end's plain mask) as of this frame's front end pass. */
void *FrontEndBasePoly();
/** The fog constants for the frame, in place of the original fog update sub_6D6CF0 (eLightScattering::Update). */
void UpdateLightScattering();
/** Collects the player views' raindrops on the screen for this frame (eDoOnScreenRain); true when there are some to
 * draw. Runs every frame. */
bool CollectOnScreenRain();
/** Draws the collected raindrops into `target` (which holds the frame), each distorting `frame` behind it
 * (XRainDropManager::DrawBatch). */
void DrawOnScreenRain(void *frame, void *target);
/** The rear-view mirror from the rear environment map face (Rendering::DrawRVM). */
void DrawRvm();
/** The low detail views' setup, then the original sub_6C83D0 it replaces (Rendering::DoLowLOD). */
void DoLowLod();
/** Depth techniques for the shadow map draws in the player view, then a flush (Rendering::DoShadowMapLOD). */
void DoShadowMapLod();
/** The main view's setup, or the depth pass's (Rendering::DoMainViewLOD). */
void DoMainViewLod();
/** The player view's setup after its target is bound (Rendering::PreRender). */
void PreRender();
/** Before the road reflection's scenery (Rendering::RoadReflectionPreRender): the sky's brightness at the hour. */
void RoadReflectionPreRender();
/** -reflectdebug: saves the road reflection's target as the named bmp next to the exe when it has next been drawn. */
void RequestReflectionDump(const char *name);
/** Before environment map face `face` (its view, 18 to 23; Rendering::EnvMapPreRender). */
void EnvMapPreRender(int face);
/** Set when the environment map targets are made: the next frame draws every face. */
extern bool g_envMapsStale;
/** After each environment map face's scenery: the cars, in the rear face (Rendering::DrawCarsInCubemaps). */
void DrawCarsInCubemaps();
} // namespace nfs::x360
