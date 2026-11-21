#pragma once
/**
 * The device resources and global textures of the Xbox 360 rendering (X360 Stuff's Rendering::InitD3D 0x10035300,
 * CreateHdrRenderTargets 0x10036df0, ResetD3D 0x10036a50, LoadGlobalTextures 0x10035170) and the Attrib instances it
 * reads (DelayedInit 0x100384d0). Textures are IDirect3DTexture9 with Direct3D 9 and the renderer's textures in Vulkan
 * mode; a 2D texture's surface is the texture itself there.
 */
namespace nfs::x360
{
/** The game's render size (eGetScreenSize) and its aspect ratio. */
extern unsigned long g_renderWidth;
extern unsigned long g_renderHeight;
extern float g_renderAspect;
/** The front end is laid out for widescreen (WidescreenUIMode auto: 16:9 or wider). */
extern bool g_wideUi;
/** The width and height of the screen in the units the pixelation and the sun flares place things in. The original
 * uses 640x480 (constants at 0x8ae8f8 and 0x8af2c0); X360 Stuff overwrites them with the render size at 720 rows,
 * which the rebuild cannot do to its read-only constants, so the ported code reads these instead. */
extern float g_screenUnits[2];
/** WorldTimeSeconds when the frame's main view started. */
extern float g_time;

/** 32x32x32 noise volumes: grass (the first) and cars (the second). */
extern void *g_noiseVolume[2];
/** 1x1 textures: white alpha, and a flat normal. */
extern void *g_whiteAlpha;
extern void *g_flatNormal;
/** RAINSPLASH00-15. */
extern void *g_rainSplash[16];
/** The visual look Attrib instance (brightness, contrast, saturation; null without one). */
extern void *g_lookAttributes;

/** The render size and front end layout, every frame. */
void UpdateRenderSize();
/** After eCreateRenderTargets, at device creation and after a reset. */
void InitResources();
/** Before the device resets. */
void ReleaseResources();
/** After the global texture packs load and after a device reset (the rain splash, noise and the like). */
void LoadGlobalTextures();
/** The grass and visual look Attrib instances, once the database is loaded. */
void InitAttributes();

/** An Attrib instance's layout (its fixed attributes) and an attribute's value (the database default if missing). */
const char *AttributeLayout(void *instance);
const float *AttributeFloat(void *instance, unsigned int key);

/** fmod for positive values (the rebuild has no CRT). */
double Fmod(double x, double y);
} // namespace nfs::x360
