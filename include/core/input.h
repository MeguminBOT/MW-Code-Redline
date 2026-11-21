#pragma once

/**
 * The game's PC input on SDL (source/core/input/GameInput.cpp, docs/CONTROLS.md).
 */
namespace nfs::input
{
/** The mouse wheel turned by this many notches (SDL's wheel event, from the message pump). */
void OnMouseWheel(float notches);
/** The wheel's turn since the last call, in DirectInput's units (120 a notch). */
int TakeMouseWheel();
/** The first controller's motors (0 to 1: the low-frequency, heavy one and the high-frequency, light one) for `ms`
 * milliseconds, or until the next call; nothing when it has none (source/core/input/Rumble.cpp drives this). */
void Rumble(float low, float high, unsigned int ms);
/** Reads the [Controls] stick and trigger shaping again (the Controller Settings page changed it). */
void ReloadSettings();
/** Whether the first controller, a gamepad, was used last (rather than the keyboard or the mouse). */
bool PadInUse();
/** The button picture (a GLOBALB texture name from tools/padglyphs.py) of the gamepad button bound to the front-end
 * action that keyboard key `key` (a DirectInput key code) has, or nullptr when it has none or no picture. */
const char *PromptPicture(int key);
/** The same picture for an action of the table (its index: 29 FE_ACTION_RTRIGGER, 30 FE_ACTION_LTRIGGER ...). */
const char *ActionPicture(int action);
/** Whether any of an action's bindings is held (a stick or trigger past half its travel). */
bool ActionHeld(int action);
} // namespace nfs::input
