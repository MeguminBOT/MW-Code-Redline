#pragma once

#include <stdint.h>

/**
 * Surface detail (source/engine/rendering/materials/SurfaceDetail.cpp, assets/shaders/owned/surface.hlsli, docs/SURFACES.md): depth the world's flat
 * polygons do not have, added where they are shaded. Not in the original game; Vulkan only.
 *
 * - Relief: stones, bricks, slabs and rock take their depth from a height in their normal map's alpha, which the view
 *   ray is marched through (parallax occlusion mapping), with the stones' own shadows from the sun.
 * - Road wear: the roads' unevenness, patches, sealed cracks, potholes, manhole covers and stains, worked out from the
 *   world position in the road shader.
 *
 * Nothing here reaches the simulation: collision and the cars read their own data.
 *
 * Data, both optional: GLOBAL\SURFACES.TXT says what each texture gets (a line per pair of textures: the diffuse
 * texture and its normal map as names or 0x hashes, the relief's depth as a share of a texture repeat, the wear
 * profile: none, asphalt, concrete, cobble or dirt). GLOBAL\SURFACES.BIN (tools/surfaces.py) holds the normal maps
 * again with a height in their alpha; they take the place of the packs' own as textures are made.
 *
 * Options, on the Video page and in speed.ini: [Display] SurfaceDetail (0 off, 1 low, 2 medium, 3 high) and RoadWear.
 */
namespace nfs::surface
{
enum Level
{
    kOff,
    kLow,
    kMedium,
    kHigh,
    kLevels,
};

/** The wear profiles (D3D9State::surface bits 8 to 11, surface.hlsli). */
enum Profile
{
    kNone,
    kAsphalt,
    kConcrete,
    kCobble,
    kDirt,
    kProfiles,
};

int DetailLevel();
void SetDetailLevel(int level);
bool RoadWear();
void SetRoadWear(bool on);
/** Whether the renderer in use can show it (the options are greyed out otherwise). */
bool Available();

/** D3D9State::surface for a draw of these two textures (name hashes) by one of the full world shaders: 0 unless the
 * option is on and SURFACES.TXT lists them. */
uint32_t DrawValue(unsigned int diffuse, unsigned int normal);
/** The scene constants' surface field (engine/rendering/shadows/shadow.h). */
void SceneConstants(float out[4]);

/** A texture's image as SURFACES.BIN holds it: every level one after the other, the top one first, each a quarter of
 * the one before, as the packs have them. */
struct Image
{
    /** D3DFORMAT. */
    uint32_t format;
    /** The size of its top level in bytes, and of the pack's image it replaces (which says they are the same size). */
    uint32_t baseBytes;
    uint32_t sourceBaseBytes;
    uint32_t levels;
    const unsigned char *data;
};

/** The image that takes the place of texture `hash`'s own, or null. */
const Image *Replacement(unsigned int hash);
} // namespace nfs::surface
