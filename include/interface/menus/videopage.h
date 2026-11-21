#pragma once

/**
 * The Video options page (source/interface/menus/VideoPage.cpp), made for the PC: the original's page was the consoles' list of
 * sliders with the PC's display options put on top.
 *
 * - Headings (Display, Quality, Effects) over a list that scrolls, with larger rows than the list that showed every
 *   row at once, the names on the left and the values on the right.
 * - Settings that belong to another appear under it only while it is on: Resolution with a display mode that has one,
 *   Refresh Rate in exclusive fullscreen (Direct3D 9), Frame Rate Limit unless V-Sync holds the rate, FXAA Quality and
 *   Supersampling by the kind of Anti-Aliasing, Anisotropy with anisotropic filtering, Dynamic Shadows with shadows,
 *   the reflections' detail and resolution with the reflections.
 * - One Sync option for tearing: Off, V-Sync, or G-Sync / FreeSync (core/timing.h), in place of a V-Sync switch beside a
 *   frame rate list that held a "VRR" entry.
 * - One Anti-Aliasing option: Off, FXAA, Supersampling or both (engine/rendering/display/display.h).
 * - A line under the list says what the highlighted setting does.
 * - Accept applies what changed and stays on the page. Only a change of display mode, resolution or refresh rate asks
 *   to keep it within ten seconds (a mode the monitor cannot show goes back by itself); the original asked that for
 *   every change and left the page. Defaults fills the page in without applying it.
 * - The mouse: a click on a row selects it, a click left or right of its value changes it, the wheel scrolls.
 *
 * The page works on a copy of the settings; nothing changes until Accept.
 */
class UIOptionsScreen;

namespace nfs::videopage
{
/** Builds the page's rows (UIOptionsScreen::SetupVideo); a screen coming to the page starts from the settings in use. */
void Setup(UIOptionsScreen *screen);
/** After the screen has picked its first row: the list starts at its top. */
void AfterSetup(UIOptionsScreen *screen);
/** Whether the page shows what was last applied or kept (UIOptionsScreen::OptionsDidNotChange). */
bool Unchanged();
/** Back to what was last kept, applied again if something else is in use (UIOptionsScreen::RestoreOriginals). */
void Revert();
/** The page filled in with the defaults, not applied (UIOptionsScreen::RestoreDefaults). */
void Defaults();
/** Puts the page's settings in use (UIOptionsScreen::ApplyVideoOptions). */
void Apply();
/** A message for the screen while it shows the page, before the screen's own handling: true when the page has dealt
 * with it and the screen must not. */
bool BeforeMessage(UIOptionsScreen *screen, unsigned int message);
/** After the screen's handling: rows that came or went with a setting, the list's view and its scroll bar. */
void AfterMessage(UIOptionsScreen *screen, unsigned int message);
} // namespace nfs::videopage
