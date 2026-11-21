#pragma once

/**
 * The pursuit panel (source/dev/PursuitPanel.cpp), with -dev: during the player's pursuit, the heat and every cop's
 * speed and distance, against the overlay's right edge. Not in the original game.
 *
 *     F2          the panel off and on
 *     F3          heat one level up (1 to 5); Ctrl+F3 one level down; also outside a pursuit
 */
namespace nfs::pursuitpanel
{
const int kMaxLines = 24;
const int kLineLength = 48;

/** Once a frame: the keys. */
void Update();
/** The panel's lines; 0 when it is not shown. */
int Text(char (*lines)[kLineLength]);
} // namespace nfs::pursuitpanel
