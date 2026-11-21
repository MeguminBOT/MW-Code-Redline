/**
 * The rows of an options page laid out at run time (interface/menus/optionsrows.h).
 */
#include "game/gen/api.h"
#include "interface/engine/feng.h"
#include "interface/menus/optionsrows.h"

namespace
{
/** A panel image stretched over the rows: from `top` above the first row to `bottom` below the last, or with
 * `whole_panel` below the room under the rows as well (the Video page's description). `parent` is the group it is in
 * (positions are relative to it), 0 for none. */
struct StretchedImage
{
    unsigned int guid;
    unsigned int parent;
    float top;
    float bottom;
    bool whole_panel;
};

/** An image moved to `offset` from the rows: from the top of the first row (`below_rows` false) or from the bottom of
 * the last, or with `whole_panel` from the bottom of the room under them. */
struct MovedImage
{
    unsigned int guid;
    unsigned int parent;
    bool below_rows;
    float offset;
    bool whole_panel;
};

/** An image inside the cursor: its height from the cursor's centre and its size (width 0: kept), for 24-unit rows. */
struct CursorImage
{
    unsigned int hash;
    float y;
    float width;
    float height;
};

/** How one package's rows are laid out (interface/menus/optionsrows.h). Objects are given by GUID, the package file's numbering. */
struct PageLayout
{
    const char *package;
    /** Top of the first row, and the row pitch that puts 19 rows on screen (the packed pages). */
    float rows_top;
    float row_height;
    /** The Video page: rows on screen, their pitch, and the room under them for the description. */
    int video_rows;
    float video_row_height;
    float video_below;
    /** The package's text scales for row titles and values, at its own 24-unit rows. */
    float title_scale;
    float value_scale;
    /** The group the cursor is in, and how far above a row's centre the package puts it for 24-unit rows. */
    unsigned int cursor_parent;
    float cursor_raise;
    const StretchedImage *stretched;
    int stretched_count;
    const MovedImage *moved;
    int moved_count;
    const CursorImage *cursor_images;
    int cursor_image_count;
    /** The Basic/Advanced button. */
    unsigned int view_button;
    /** The rows' ends and the value column's middle (between the cursor's arrows), on the screen. */
    float left;
    float right;
    float value_x;
    /** The scroll bar: its group, track, handle and two arrows. */
    unsigned int scroll_group;
    unsigned int scroll_track;
    unsigned int scroll_handle;
    unsigned int scroll_arrows[2];
    /** Headings' colour. */
    int accent[3];
};

/** The rows' group, the cursor's arrows and the package's row height, the same in both packages. */
const unsigned int kRowsGroup = 0x78B4E2A9;
const unsigned int kCursorArrowGroups[] = {0xD1CDA594, 0x9345E4A7};
const float kPackageRowHeight = 24.0f;
/** On the Video page, how far under the last row the pieces that end with the rows reach (the description follows). */
const float kVideoRowsTail = 3.0f;

/** OptionsPCDisplay.fng: a panel with a line above and below the rows, inside a frame of four corners. The package's
 * header above the top line (37 units for the Basic/Advanced button) goes; the other spacing is kept. */
const unsigned int kMainMenuPanel = 0x0FE883;
const StretchedImage kMainMenuStretched[] = {
    {0x0FE885, kMainMenuPanel, -20.68f, 24.32f, true}, // panel
    {0x0FE88E, kMainMenuPanel, -21.68f, 25.32f, true}, // its shade
    {0x0FE886, kMainMenuPanel, -9.68f, 12.32f, false}, // the darker band between the lines
};
const MovedImage kMainMenuMoved[] = {
    {0x0FE884, kMainMenuPanel, false, -8.68f, false}, // line above the rows
    {0x0FE887, kMainMenuPanel, true, 12.32f, false},  // line below the rows
    {0x0FE893, 0, false, -21.68f, false},             // frame corners
    {0x0FE894, 0, false, -21.68f, false},
    {0x0FE895, 0, true, 26.32f, true},
    {0x0FE896, 0, true, 26.32f, true},
};
const CursorImage kMainMenuCursor[] = {
    {0xDDBE0300, 15.0f, 10.0f, 10.0f},
    {0xDDBE0321, -14.0f, 10.0f, 10.0f},
    {0x6FB79A55, 15.0f, 10.0f, 10.0f},
    {0xDDBDFFE8, -14.0f, 10.0f, 10.0f},
};

/** Pause_PCDisplayOptions.fng: the rows sit on a backdrop (in the scroll bar's group) between the header and the
 * button bar, and the cursor is in the rows' group. */
const StretchedImage kPauseMenuStretched[] = {
    {0x104C1F, 0x104C1C, -4.0f, 4.0f, true}, // backdrop
};
const CursorImage kPauseMenuCursor[] = {
    {0xDDBE0300, 14.0f, 32.0f, -32.0f},
    {0xDDBE0321, -12.0f, 32.0f, -32.0f},
    {0x6FB79A55, 14.0f, 32.0f, -32.0f},
    {0xDDBDFFE8, -12.0f, 32.0f, -32.0f},
    {0xC6AFDD7E, 1.0f, 0.0f, 25.0f}, // highlight bar
};

const PageLayout kLayouts[] = {
    {"OptionsPCDisplay.fng", -159.32f, 17.05f, 13, 21.6f, 43.2f, 0.9f, 1.0f, kMainMenuPanel, 1.68f, kMainMenuStretched,
     sizeof(kMainMenuStretched) / sizeof(kMainMenuStretched[0]), kMainMenuMoved,
     sizeof(kMainMenuMoved) / sizeof(kMainMenuMoved[0]), kMainMenuCursor,
     sizeof(kMainMenuCursor) / sizeof(kMainMenuCursor[0]), 0x0F98E7, -256.0f, 262.0f, 148.5f, 0x0FE956, 0x0FE95B,
     0x0FE95A, {0x0FE957, 0x0FE95C}, {255, 174, 64}},
    {"Pause_PCDisplayOptions.fng", -116.0f, 15.16f, 12, 20.5f, 42.0f, 0.85f, 0.9f, 0x0F3381, 1.0f, kPauseMenuStretched,
     sizeof(kPauseMenuStretched) / sizeof(kPauseMenuStretched[0]), nullptr, 0, kPauseMenuCursor,
     sizeof(kPauseMenuCursor) / sizeof(kPauseMenuCursor[0]), 0x104CBF, -256.0f, 240.0f, 116.5f, 0x104C1C, 0x104C1E,
     0x104C1D, {0x104C20, 0x104C23}, {255, 236, 190}},
};

const PageLayout *LayoutFor(const char *package)
{
    for (const PageLayout &layout : kLayouts)
        if (package && !bStrICmp(package, layout.package))
            return &layout;
    return nullptr;
}

unsigned int NameHash(const char *prefix, int index)
{
    char name[32];
    bSNPrintf(name, sizeof(name), "%s%d", prefix, index);
    return FEHashUpper(name);
}

FEObject *AsObject(FEString *string)
{
    return reinterpret_cast<FEObject *>(string);
}

FEObject *FindRowString(UIWidgetMenu *menu, const char *prefix, int row)
{
    return AsObject(FEngFindString(menu->PackageName, static_cast<int>(NameHash(prefix, row))));
}

/** The height of a group's origin, 0 for no group. */
float GroupY(const char *package, unsigned int guid)
{
    FEObject *group = guid ? nfs::feng::FindByGuid(package, guid) : nullptr;
    return group ? group->pData->Pos[1] : 0.0f;
}
} // namespace

namespace nfs::optionsrows
{
int ProvideRowStrings(UIWidgetMenu *menu, int rows)
{
    FEObject *group = FEngFindObject(menu->PackageName, kRowsGroup);
    const char *const prefixes[] = {menu->pTitleName, menu->pDataName};
    for (int row = 1; row <= rows; ++row)
        for (const char *prefix : prefixes)
            if (!FindRowString(menu, prefix, row) &&
                !nfs::feng::Clone(row > 1 ? FindRowString(menu, prefix, row - 1) : nullptr, group,
                                  NameHash(prefix, row)))
                return row - 1;
    return rows;
}

bool Pack(UIWidgetMenu *menu, int rows, bool videoPage, Metrics *metrics)
{
    const PageLayout *found = LayoutFor(menu->PackageName);
    const char *package = menu->PackageName;
    FEObject *rows_group = FEngFindObject(package, kRowsGroup);
    FEObject *cursor = menu->pCursor;
    if (!found || !rows_group || !cursor)
        return false;
    const PageLayout &layout = *found;
    const int shown = videoPage ? layout.video_rows : rows;
    const float row_height = videoPage ? layout.video_row_height : layout.row_height;
    const float below = videoPage ? layout.video_below : 0.0f;
    const float scale = row_height / kPackageRowHeight;
    const float rows_top = layout.rows_top;
    const float rows_end = rows_top + static_cast<float>(shown) * row_height;
    const float rows_bottom = rows_end + below;

    // The menu stacks its rows from these, in the rows' group's coordinates.
    const float first_row = rows_top - rows_group->pData->Pos[1];
    menu->vWidgetStartPos.y = first_row;
    menu->vLastWidgetPos.y = first_row;
    menu->vDataPos.y = first_row;
    menu->vWidgetSize.y = row_height;
    menu->vMaxTitleSize.y = row_height;
    menu->vMaxDataSize.y = row_height;
    menu->iMaxWidgetsOnScreen = shown;
    for (int row = 1; row <= rows; ++row)
    {
        FEObject *title = FindRowString(menu, menu->pTitleName, row);
        FEObject *value = FindRowString(menu, menu->pDataName, row);
        FEngSetScaleX(title, layout.title_scale * scale);
        FEngSetScaleY(title, layout.title_scale * scale);
        FEngSetScaleX(value, layout.value_scale * scale);
        FEngSetScaleY(value, layout.value_scale * scale);
    }

    // The menu moves the cursor with its POS<n> script for the n-th row on screen.
    const float cursor_parent_y = GroupY(package, layout.cursor_parent);
    if (FEScript *first_slot = cursor->FindScript(NameHash("POS", 1)))
        for (int row = 1; row <= shown; ++row)
        {
            FEScript *slot = nfs::feng::FindOrCloneScript(cursor, NameHash("POS", row), first_slot);
            FEKeyTrack *track = slot ? slot->FindTrack(static_cast<FEKeyTrack_Indices>(nfs::feng::kPosition)) : nullptr;
            if (track)
                track->BaseValue[1] = rows_top + (static_cast<float>(row) - 0.5f) * row_height -
                                      layout.cursor_raise * scale - cursor_parent_y;
        }
    const FEGroup *cursor_group = reinterpret_cast<FEGroup *>(cursor);
    for (int i = 0; i < layout.cursor_image_count; ++i)
    {
        const CursorImage &piece = layout.cursor_images[i];
        FEObject *image = cursor_group->FindChildRecursive(piece.hash);
        nfs::feng::SetTrack(image, nfs::feng::kPosition, 1, piece.y * scale);
        nfs::feng::SetTrack(image, nfs::feng::kSize, 1, piece.height * scale);
        if (piece.width != 0.0f)
            nfs::feng::SetTrack(image, nfs::feng::kSize, 0, piece.width * scale);
    }
    for (unsigned int arrow : kCursorArrowGroups)
    {
        FEObject *group = cursor_group->FindChildRecursive(arrow);
        FEngSetScaleX(group, scale);
        FEngSetScaleY(group, scale);
    }

    // The panel behind the rows.
    for (int i = 0; i < layout.stretched_count; ++i)
    {
        const StretchedImage &piece = layout.stretched[i];
        const float top = rows_top + piece.top;
        const float bottom = piece.whole_panel || !videoPage ? rows_bottom + piece.bottom : rows_end + kVideoRowsTail;
        nfs::feng::StretchImageY(nfs::feng::FindByGuid(package, piece.guid),
                                 (top + bottom) * 0.5f - GroupY(package, piece.parent), bottom - top);
    }
    for (int i = 0; i < layout.moved_count; ++i)
    {
        const MovedImage &piece = layout.moved[i];
        const float y = !piece.below_rows                     ? rows_top + piece.offset
                        : piece.whole_panel || !videoPage ? rows_bottom + piece.offset
                                                          : rows_end + kVideoRowsTail;
        nfs::feng::SetTrack(nfs::feng::FindByGuid(package, piece.guid), nfs::feng::kPosition, 1,
                            y - GroupY(package, piece.parent));
    }
    FEngSetInvisible(nfs::feng::FindByGuid(package, layout.view_button));

    if (metrics)
    {
        Metrics &m = *metrics;
        m.group = rows_group;
        m.groupX = rows_group->pData->Pos[0];
        m.groupY = rows_group->pData->Pos[1];
        m.top = first_row;
        m.rowHeight = row_height;
        m.visibleRows = shown;
        m.left = layout.left - m.groupX;
        m.right = layout.right - m.groupX;
        m.valueX = layout.value_x - m.groupX;
        m.belowTop = rows_end - m.groupY;
        m.belowHeight = below;
        m.titleScale = layout.title_scale * scale;
        m.valueScale = layout.value_scale * scale;
        m.scrollTrack = nfs::feng::FindByGuid(package, layout.scroll_track);
        m.scrollHandle = nfs::feng::FindByGuid(package, layout.scroll_handle);
        m.scrollArrows[0] = nfs::feng::FindByGuid(package, layout.scroll_arrows[0]);
        m.scrollArrows[1] = nfs::feng::FindByGuid(package, layout.scroll_arrows[1]);
        m.scrollParentY = GroupY(package, layout.scroll_group);
        for (int i = 0; i < 3; ++i)
            m.accent[i] = layout.accent[i];
    }
    return true;
}
} // namespace nfs::optionsrows
