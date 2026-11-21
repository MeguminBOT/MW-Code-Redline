#pragma once

/**
 * The test channel (source/dev/TestBridge.cpp, docs/TESTING.md): with -set Debug.TestBridge=1 the game answers on the
 * named pipe \\.\pipe\nfsmw_test_<pid> what it shows (the game flow's state, the front-end packages, the texts of a
 * screen, a menu's options and the one chosen) and takes presses, shop zones and screenshots, so tools drive it by what
 * is on the screen instead of by timed key presses (tools/gametest). Not in the original game.
 */
namespace nfs::testbridge
{
/** Once a frame, on the game's thread: answers a request the pipe's thread has waiting. */
void Update();
/** The front-end package with the pads, or "" (game thread). */
const char *ControlPackage();
/** What the pads are on: an icon scroller's chosen option, or the one string drawn in the highlight colour (a list's
 * row, a dialog's button); false when it cannot tell (game thread). */
bool Chosen(char *out, int size);
} // namespace nfs::testbridge
