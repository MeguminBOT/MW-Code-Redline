#pragma once

/**
 * Weather (source/gameplay/world/Weather.cpp, docs/WEATHER.md): rain that comes and goes with the clock, from a drizzle to a
 * downpour, fog on mornings and nights, and how wet the roads are. Not in the original game, where it rained only where
 * its weather map said, the same in every session.
 *
 *     [World] Weather = cycle | clear | cloudy | rain | storm | fog | game
 *     [World] WeatherSeed = 0
 *
 * cycle (the default): rain and fog come and go, as often at each hour as GLOBAL\TIMEOFDAY.TXT's RainChance and
 * FogChance say; clear, cloudy, rain, storm and fog hold that weather; game: the original's weather map. A seed of 0
 * gives each session a sky of its own. Ctrl+F7 steps through the kinds.
 *
 *     [World] Lightning = 1   0: storms without lightning
 */
namespace nfs::weather
{
/** The heaviest rain Rain gives: the game's own rain goes up to 1, the rebuild's effects show the rest. */
const float kHeaviestRain = 1.5f;

/** Once a frame, before the views: moves the weather on with the clock. */
void Update();
/** The kind of weather (cycle, clear, cloudy, rain, storm, fog, game): the next or previous, as Ctrl+F7 steps, and
 * its name. */
void StepMode(int direction);
/** Reads [World] Weather and Lightning again (the World Settings page); the weather eases to the new kind. */
void Reconfigure();
const char *ModeName();
/** How hard it rains: 0 not at all, 1 the original's heaviest, up to kHeaviestRain a downpour. */
float Rain();
/** Whether the falling rain is drawn: the Video page's Rain Effect (VideoOptions::RainEnable). Off, the weather goes
 * on (wet roads, puddles, clouds, haze, the rain's sound) without the drops: the game's near the camera and on the
 * screen, the rebuild's streaks and splashes. */
bool RainShown();
/** How overcast the sky is, 0 to 1. */
float Cloud();
/** How dense the fog is, 0 to 1. */
float Fog();
/** A lightning flash in a storm: how bright it is now (0 between flashes, up to 1). */
float Lightning();
/** Thunder for the weather's own lightning: OwnThunder while the weather makes its lightning (then the game's rain
 * sound plays its thunder only for the flashes, not on its own timer); ThunderDue from 0 to 5 s after each flash until
 * ThunderPlayed (or until it is 3 s late). */
bool OwnThunder();
bool ThunderDue();
void ThunderPlayed();
/** How wet the roads are (0 dry, 1 soaked) and how much water stands on them (0 to 1): both rise in the rain and dry
 * after it, the puddles slower. */
float Wetness();
float Puddles();
/** Before the game's rain object of a view moves on (Rain::UpdateAndRender): the rain and clouds it eases towards
 * (its drops, their sound, the drops on the screen). The game asks for them (0x7539d0, 0x753ac0) only while the
 * original's weather map layers are loaded, which they are not everywhere. */
void BeforeRain(void *rain);
/** After the game's rain object of a view moved on (Rain::UpdateAndRender): gives it the weather's wet roads. */
void ApplyToRain(void *rain);
/** "WEATHER RAIN 0.82 FOG 0.00 WET 0.95" for the pose box; false when the weather is the original's. */
bool Text(char *line, int size);
/** " -set World.Weather=fog" for a saved pose (F9): the kind held, or in the cycle the fixed kind nearest the weather
 * at the moment; empty with the original's weather. */
void PoseOption(char *text, int size);
} // namespace nfs::weather
