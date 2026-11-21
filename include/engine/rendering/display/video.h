#pragma once

/**
 * Quality presets of the Video page. The original's basic view had one "Level Of Detail" slider (performance levels 1
 * to 5, SetPerformanceLevelOptions) beside the advanced view's separate options; the page now shows the options and
 * offers the levels as presets over them.
 */
#include "game/gen/api.h"

namespace nfs::video
{
/** The original's performance levels 1 to 4, then 5: every quality option at the highest the device allows, which is
 * also the first-start default (SetDefaultVideoOptions). */
const int kLowestPreset = 1;
const int kHighestPreset = 5;

/** `options` with the quality options of preset `level`. Display, VSync and motion blur settings are kept. */
VideoOptions Preset(int level, const VideoOptions &options);
/** The preset that gives `options` its quality options, or 0 if none does. */
int MatchingPreset(const VideoOptions &options);
/** -testvideo (tools/rungame.py passes it to every test run): every quality option at the highest the device allows,
 * but motion blur low, overbright on and the visual treatment off, whatever the player has chosen; nothing of it is
 * saved, and the Video page saves nothing while it is on. */
bool TestOptions();
/** The motion blur strength of -testvideo (percent of the original's): the Video page's Low. */
const int kTestMotionBlur = 35;
/** Puts the test options in place of the loaded ones (after DetectVideoOptionLimits). */
void ApplyTestOptions();
} // namespace nfs::video
