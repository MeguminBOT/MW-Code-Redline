#pragma once

/**
 * Local lights (docs/SHADOWS.md, source/engine/rendering/lights/LocalLights.cpp): the light sources the game draws as flares - signs,
 * traffic lights, and the cars' head, brake and police lights - turned into lights that the rebuild's own receiver
 * shaders shade with (assets/shaders/owned/lighting.hlsli, MovingLights). The world's lamps, which never move, are baked
 * instead (engine/rendering/lights/lamps.h).
 *
 * Every flare the player view draws passes through eRenderLightFlare with its final intensity (blinking, flickering,
 * headlights on or off already applied by the game), so the lights are recorded there and used the next frame.
 */
class eView;
class eLightFlare;
struct bMatrix4;

namespace nfs::lights
{
const int kMaxLights = 64;
/** How far a car's headlights reach (m). */
const float kHeadlightRadius = 75.0f;
/** A pool flare's flag (eLightFlare +0x2d) for a lamp's glow whose light is baked (engine/rendering/lights/lamps.h): not recorded. The flare
 * renderer only reads bit 2 (directional). */
const unsigned char kFlareBaked = 0x40;
/** A pool flare's flag for a light that moves (the helicopter's): recorded as a car's light, not tracked in place. */
const unsigned char kFlareMoving = 0x20;

/** One light as the shaders read it (lighting.hlsli declares the same layout). */
struct GpuLight
{
    /** xyz: position; w: radius (no light beyond). */
    float position[4];
    /** rgb: colour times intensity; w: how fast the light falls off inside the radius (1 / distance^2 scale). */
    float color[4];
    /** xyz: spot direction; w: cosine of the outer cone angle, or -2 for a light that shines all round. */
    float direction[4];
    /** x: cosine of the inner cone angle; y: 0 for a light the headlight shadow map shades (-1 for none); z: 1 for a headlight's beam shape;
     * w: how strongly it glows in the haze against its light (engine/rendering/lights/lamps.h Haze). */
    float cone[4];
};

/** From the eRenderLightFlare replacement: a flare the game drew, with its transform and intensity. */
void Record(const eView *view, const eLightFlare *flare, const bMatrix4 *matrix, float intensity, int flareType);
/** Every loaded car's headlights as lights at its matrix of now, drawn or not (once until the next Collect, which
 * records them itself when nothing has): before the player's headlights are looked for (PlayerHeadlights). */
void RecordCars(const float camera[3]);
/** The player's car's headlights among the flares recorded since the last Collect: where they stand between them, where
 * they aim, and the position of the car they were drawn with. They are marked for the shadow map (GpuLight::cone y) in the
 * next Collect. `spread`: how far each lamp is from their middle (metres). False when the car has none on. */
bool PlayerHeadlights(float position[3], float direction[3], float origin[3], float *spread = nullptr);
/** Fills `out` with the most important lights of the flares recorded since the last call, seen from `camera`; returns
 * how many. A light wholly behind the camera (it looks along `forward`) is left out unless behindToo. Call once per frame. */
int Collect(GpuLight *out, int capacity, const float camera[3], const float forward[3], bool behindToo);
/** How much of a flare's light reaches the camera of `view` through the haze (1 in clear air, little in thick fog). */
float SeenThroughHaze(const eView *view, const eLightFlare *flare, const bMatrix4 *matrix);
} // namespace nfs::lights
