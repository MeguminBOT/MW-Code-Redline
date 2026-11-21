#pragma once

/**
 * The rebuild's sun shadows (Vulkan; docs/SHADOWS.md, source/engine/rendering/shadows/SunShadows.cpp): cascaded shadow maps that follow the
 * player camera, drawn with every caster (scenery including overpasses and roofs, world props, cars and traffic), and
 * the scene constants the rebuild's own receiver shaders read them with (assets/shaders/owned/lighting.hlsli).
 */
#include "engine/rendering/lights/lamps.h"
#include "engine/rendering/lights/lights.h"

namespace nfs::shadow
{
const int kCascades = 4;
const int kAtlasSize = 4096;
const int kTileSize = kAtlasSize / 2;

/** Set 2's constants (engine/rendering/vulkan/pipeline.h kSceneConstantsBinding); lighting.hlsli declares the same layout. Matrices
 * are row-vector (D3D) order. */
struct SceneConstants
{
    /** World position to (u, v) in the atlas and light-space depth, for each cascade. */
    float cascadeMatrix[kCascades][16];
    /** Each cascade's radius around the camera: receivers pick their cascade by distance from it. */
    float cascadeSplits[4];
    /** World size of one shadow map texel in each cascade. */
    float cascadeTexelWorld[4];
    /** World distance along the light covered by depth 0 to 1 in each cascade. */
    float cascadeDepthRange[4];
    /** xyz: the player camera; w: 1 while the sun shadows are on. */
    float cameraPosition[4];
    /** xyz: the camera's view direction; w: where the last cascade starts fading out (fraction of its far end). */
    float cameraForward[4];
    /** xyz: direction towards the sun; w: tangent of the sun's angular radius (penumbra width per distance). */
    float sun[4];
    /** 1 / atlas size, tile size in texels, cascade count, unused. */
    float atlas[4];
    /** x: how many of `lights` are in use; y: the sun's share of the haze's light, which the sun shadows shade
     * (gameplay/world/timeofday.h HazeSunShare; 0 when Debug.ShadowedHaze is off); z: the frame number modulo 64 (moves the fade
     * dither); w: the finest shadow detail per metre of distance (texel of the coarsest cascade at the inner edge, so
     * cascades meet without a visible step). */
    float lightInfo[4];
    /** Testing: x: the nearest cascade receivers may use (Debug.MinCascade; 0 normally), to compare cascades on one
     * view. */
    float testing[4];
    /** rgb: what a car's key light keeps in shade, the sky's share; w: how strongly the local lights shine (0 by day, 1
     * at night). From gameplay/world/timeofday.h CarShade and LocalLights. */
    float timeOfDay[4];
    /** The light smoke, dust, spray, leaves and rain take from the sun where it reaches them, and from the sky (rgb, at
     * this hour against the game's own: gameplay/world/timeofday.h ParticleLight). */
    float particleSun[4];
    float particleSky[4];
    /** The lamps' baked light (engine/rendering/lights/lamps.h): per level, xyz the world position of its first cell, w its cell size (0:
     * none). */
    float lampVolume[2][4];
    /** The hour's sky against the game's own: rgb for light the effects keep fixed (the grass blades' own), w the hour's
     * AmbientColour.w against the game's, for the reflections' cheap shaders (gameplay/world/timeofday.h SkyLight). */
    float skyLight[4];
    /** What the light baked into the world takes under a tunnel's roof: rgb the game's own sky against the hour's
     * (gameplay/world/timeofday.h TunnelLight). */
    float tunnelLight[4];
    /** The haze the lights glow in (engine/rendering/lights/lamps.h Haze): its extinction per metre, its Henyey-Greenstein g, how many of
     * hazeLamps are in use, how strongly the lights glow (0: no glow). */
    float haze[4];
    /** The weather (gameplay/world/weather.h): how hard it rains (0 to kHeaviestRain), how wet the world is, how much water stands
     * on the roads (0 to 1), and the world's time in seconds (wrapped), for the rain's animation. */
    float weather[4];
    /** The light fog and rain haze take (gameplay/world/timeofday.h HazeLight): rgb; the falling rain is lit by it. */
    float hazeLight[4];
    /** The Xbox 360 light scattering (nfs::x360::g_fog), for the shaders whose Xbox 360 versions had none (lighting.hlsli
     * HazeAt): Br + Bm, 1 / (Br + Bm), the Rayleigh and Mie terms, the phase terms and FogInLightScatter, then the
     * distance scale and the farthest distance, and the direction towards the light that casts the shadows (the
     * sun's or the moon's, LOCALLIGHTDIRVEC in world space). All 0 without light scattering. */
    float fog[7][4];
    /** World position to (u, v) in the headlight shadow map and its depth (w: divide by it). */
    float headlightMatrix[16];
    /** x: 1 / the map's size. */
    float headlightInfo[4];
    /** The surface detail option (engine/rendering/surface.h SceneConstants): x the relief's steps along the view ray (0: off), y
     * its steps towards the light (0: no shadows of its own), z how far it reaches (metres), w how strongly the
     * roads' wear and decals show (0: off). */
    float surface[4];
    nfs::lights::GpuLight lights[nfs::lights::kMaxLights];
    /** The lamps nearest in front of the camera, as lights, for the haze's glow (their light on the world is baked). */
    nfs::lights::GpuLight hazeLamps[nfs::lamps::kHazeLamps];
};

/** True when the rebuild's sun shadows replace the original shadow map this frame (Vulkan, shadows on, racing). */
bool Active();
/** The sun's share of the haze's light that the shadows shade this frame (lighting.hlsli lightInfo.y; 0 when the sun
 * shadows are off or the haze is lit from all round): the post processing's shaded haze pass. */
float HazeShading();
/** Draws the cascades for this frame from the player camera and publishes the scene constants. Call once per frame
 * before the scenery culling of the views (it culls and draws the scenery itself). */
void RenderSunShadows();
/** Releases the atlas (the renderer is reset or shut down). */
void ReleaseTargets();
} // namespace nfs::shadow
