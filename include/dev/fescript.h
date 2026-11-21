#pragma once

/**
 * Scripted front-end buttons for headless runs (source/dev/ScriptedInput.cpp). Not in the original game.
 *
 *     -set Debug.FEKeys=12:accept,15:right,15.5:right,17:accept
 *
 * presses those front-end buttons that many seconds after the game started: up, down, left, right, accept, back,
 * start, button0-button5, ltrigger, rtrigger (button0-5 are the screens' extra actions, shown as 1-6 on the PC); or
 * enters a career shop zone from free roam: safehouse, carlot, customshop; or (shot) has the Vulkan renderer save
 * the next frame as feshot_NN.bmp in the game folder.
 *
 *     -set Debug.FEClicks=80:490:417,84:384:418
 *
 * clicks the front end's left mouse button at a point of its 640 by 480 screen that many seconds after the start.
 */
namespace nfs::fescript
{
/** Once a frame. */
void Update();
/** Presses a front-end button now (its name as in Debug.FEKeys), held as a scripted press is; false for no such
 * button. */
bool Press(const char *button);
/** Enters a career shop zone now (safehouse, carlot, customshop); false for no such zone. */
bool Zone(const char *name);
/** Has the Vulkan renderer save the next frame as NAME.bmp beside the exe; false without the Vulkan renderer. */
bool Shot(const char *name);
/** After the front end's mouse is read each frame (eDisplayFrame): a scripted click due puts the cursor there, down. */
void Mouse();
} // namespace nfs::fescript
