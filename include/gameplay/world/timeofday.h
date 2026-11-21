#pragma once

/**
 * Time of day (source/gameplay/world/TimeOfDay.cpp, docs/NIGHT.md). Not in the original game.
 *
 *     [World] TimeOfDay = cycle | day | night | HH:MM
 *     [World] MinutesPerHour = 5
 *     [World] StartHour = 14:00
 *
 * A clock runs through day and night (cycle, the default) or stands at an hour; the sun and the moon cross the sky
 * with it, GLOBAL\TIMEOFDAY.TXT gives each hour its lighting, and Carbon's night sky fades in over the game's at dusk.
 * `day` keeps the game's own lighting.
 */
namespace nfs::timeofday
{
/** The clock is on (any mode but `day`, with TIMEOFDAY.TXT loaded). */
bool Enabled();
/** The clock lights the frame: it is on, and the game is not in the front end, whose scenes keep the game's own
 * lighting and weather. */
bool Lit();
/** The clock's hour (0 to 24), and putting it at one (a tour's stop). */
float Hour();
void SetHour(float hour);
/** Reads [World] TimeOfDay and MinutesPerHour again (the World Settings page): a running clock goes on from the hour
 * it shows. Turning `day` off or on takes full effect with the next world loaded (the lamps are found at load). */
void Reconfigure();
/** "14:30" or "14.5" as hours; -1 when it is neither. */
float ParseHour(const char *text);
/** After the game's time of day lighting (0x769580): the hour's over it (TimeOfDayPointer +0x20). */
void ApplyLighting(char *time_of_day);
/** Once a frame, after the sun's place (0x769360): the clock moves on, and the light that casts the shadows (the sun,
 * or at night the moon) takes the sun's place (TimeOfDayPointer +0x170, +0x180). */
void ApplySun(char *time_of_day);
/** The eye adaptation's middle gray and the darkest luminance it adapts to: the hour's, `day` the game's. */
float MiddleGray(float day);
float AdaptFloor(float day);
/** The game's sky: its brightness (STANDARD_BRIGHTNESS) and its layers' intensities (SKY_DIFFUSESCALE). */
float SkyBrightness(float day);
void SkyLayers(float *scale);
/** How far Carbon's night dome covers the game's sky (0: not drawn, 1: the game's sky is not drawn), and its
 * brightness (1 as Carbon has it). */
float NightSky();
float DomeBrightness();
/** The moon's place: world azimuth and elevation in degrees. */
void Moon(float &azimuth, float &elevation);
/** How much of the sun's disc and glow show (0 below the horizon). */
float SunSprite();
/** A light context (cars, characters) lit as the hour lights the world: a scaled copy, or `context` itself. */
const void *LightContext(const void *context);
/** The scale of the trees' own brightness and light (1 by day). */
float TreeLight();
/** What a car's key light keeps in shade, the sky's share (lighting.hlsli CarKeyLight): red, green, blue. */
void CarShade(float *rgb);
/** The light smoke, dust, spray, leaves and rain take from the sun where it reaches them and from the sky, against
 * the game's own hour, which their colours are made for (lighting.hlsli ParticleSunlight, particleSky): red, green,
 * blue each. */
void ParticleLight(float *sun, float *sky);
/** The hour's sky against the game's own (lighting.hlsli skyLight): red, green, blue, then the w of its AmbientColour
 * against the game's own hour's (the reflections' cheap shaders take AmbientColour without it). */
void SkyLight(float *rgbw);
/** What the light baked into the world takes under a tunnel's roof (lighting.hlsli tunnelLight): the game's own sky
 * against the hour's, times the hour's TunnelLight, so that tunnels keep their lamps' light at night. Red, green,
 * blue. */
void TunnelLight(float *rgb);
/** The haze's colour against grey (1 1 1 but in fog and rain haze, which take the colour of the light they scatter):
 * the light scattering's Mie term takes it (source/engine/rendering/materials/LightScattering.cpp). */
void HazeTint(float *rgb);
/** How much of the light the haze takes away per metre now (green; 0 with the clock off): the lamps' glow in it
 * (nfs/haze.h). */
float HazeExtinction();
/** How much light gets through `metres` of the haze now (1 with the clock off). */
float HazeTransmittance(float metres);
/** The sun's share of the haze's light (the rest is the sky's), which the sun shadows shade (lighting.hlsli
 * InscatterVisibility): most of it in clear air, less in fog and rain haze, which the sky lights too. */
float HazeSunShare();
/** The light thick haze takes at this hour (a pale wall's: the sky's, some of the sun's and at night the city's lamps'):
 * red, green, blue; the falling rain is lit by it (lighting.hlsli hazeLight). */
void HazeLight(float *rgb);
/** How strongly the local lights (lamps, the cars' lights) shine on the world: 0 by day, 1 at night. */
float LocalLights();
/** How much of the game's traffic is on the roads at this hour (0 to 1; 1, the game's own, at the busy hours, thin at
 * night): source/gameplay/ai/TrafficDensity.cpp. */
float Traffic();
/** The weather's chances at this hour (gameplay/world/weather.h): the share of the time it rains (`fallback` without the keys)
 * and the share of it in fog. */
float RainChance(float fallback);
float FogChance();
/** TIMEOFDAY.TXT's weather shape: how long a spell of rain and of fog lasts, about (hours of the clock). */
float RainSpell();
float FogSpell();
/** Real minutes an hour of the clock takes. */
float MinutesPerHour();
/** "14:05" and how fast the clock runs, for the pose box; false when the clock is off. */
bool Text(char *line, int size);
/** "-set World.TimeOfDay=14:05" for a saved pose; empty when the clock is off. */
void PoseOption(char *text, int size);

/** Carbon's night dome of `view` over the game's sky as far as NightSky() says (source/engine/rendering/sky/NightSky.cpp); false when
 * none was drawn. */
bool DrawNightSky(class eView *view, float gain = 1.0f, bool mirrored = false);
/** How many times as bright as the player view the wet road's mirror draws the sky: the original's 2 (4 against 2, for
 * the road's weak sheen), 1 in thick haze, where a puddle would otherwise shine whiter than the air around it. */
float RoadReflectionSkyGain();
/** Direct3D 9: before the device resets (lost) and after it. */
void NightSkyDeviceLost(bool lost);
/** With the game's effects at shutdown. */
void ReleaseNightSky();
} // namespace nfs::timeofday
