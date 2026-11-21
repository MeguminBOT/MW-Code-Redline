#pragma once

/**
 * The rows of an options page (UIWidgetMenu), laid out at run time for pages the packages have no layout for
 * (source/interface/menus/OptionsRows.cpp): the Video page (interface/menus/videopage.h) and the new profile's page (gameplay/progression/profileoptions.h).
 *
 * The page's packages (OptionsPCDisplay.fng from the main menu, Pause_PCDisplayOptions.fng from the pause menu) show 9
 * or 10 rows of 24 units and have row objects for 15. Rows beyond those get copies of the last row's objects; the rows'
 * pitch, the text's scale, the cursor's position scripts (one per row on screen), its brackets and arrows, and the
 * panel behind the rows are changed to suit. Positions are in front-end units: 640 x 480 with the centre at 0, y down.
 */
#include "game/gen/api.h"

namespace nfs::optionsrows
{
/** Where a page's rows are after Pack, in the rows' group's coordinates (the widgets' own). */
struct Metrics
{
    /** The first row's top, the row pitch, and how many rows show at once. */
    float top;
    float rowHeight;
    int visibleRows;
    /** The rows' left and right ends, and the middle of the value column (between the cursor's arrows). */
    float left;
    float right;
    float valueX;
    /** The room under the rows inside the panel (the Video page's description), from `belowTop` down. */
    float belowTop;
    float belowHeight;
    /** The text scale of a row's title and of its value. */
    float titleScale;
    float valueScale;
    /** The rows' group, and its origin on the screen. */
    FEObject *group;
    float groupX;
    float groupY;
    /** The scroll bar's track and handle (images), their parent's height on the screen, and its arrows; null without. */
    FEObject *scrollTrack;
    FEObject *scrollHandle;
    FEObject *scrollArrows[2];
    float scrollParentY;
    /** The colour headings take: red, green, blue. */
    int accent[3];
};

/** Gives rows 1 to `rows` title and value strings, copying the row above where the package has none. Returns how many
 * rows have them. */
int ProvideRowStrings(UIWidgetMenu *menu, int rows);
/**
 * Lays the page out. `videoPage`: the Video page's rows (fewer and larger, with room for a description under them and
 * a scroll bar); otherwise `rows` rows packed so that all show at once. Call after ClearWidgets and before the rows'
 * widgets are added: it sets the menu's row metrics. False for a package without a layout (a mod's): the rows keep the
 * package's own layout and scroll.
 */
bool Pack(UIWidgetMenu *menu, int rows, bool videoPage, Metrics *metrics);
} // namespace nfs::optionsrows
