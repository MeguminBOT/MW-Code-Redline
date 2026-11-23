/**
 * The option list screens (Options pages and the like): their widgets' rows, the cursor, and the screen's messages
 * (original: zFe2 UIWidgetMenu::NotifySoundMessage 0x573c60, SetInitialPositions 0x573dd0, IncrementStartPos 0x573e70,
 * ClearWidgets 0x588c90, RefreshWidgets 0x588d20, GetWidgetIndex 0x588d60, UpdateCursorPos 0x593b10, SetOption
 * 0x59bfb0, Reposition 0x59bff0, NotificationMessage 0x5a96c0, SetInitialOption 0x5a5ea0, SyncViewToSelection 0x59c100; the widget
 * under the mouse 0x5882b0, ScrollView 0x5a5d40, PageUp 0x5a5e40, PageDown 0x5a5e70, the mouse 0x5a8db0, the
 * scroll bar's tick 0x5a8f60).
 *
 * UIWidgetMenu: a MenuScreen (+0x10 the package's name) with its widgets in a list at +0x2c (a widget's node 4 bytes
 * in, next first; the list ends at the widget "at" +0x28), +0x34 the current widget, +0x38 the first one shown, +0x3c
 * and +0x40 the first row's two objects, +0x44 the object of a stored accept, +0x50 the cursor, +0xcc..+0xf8 the
 * rows' places and sizes, +0xe0 a row's height, +0x100 the rows' gap, +0x104 the widget count plus one, +0x108 the
 * current row (from 1), +0x10c the rows shown, +0x110..+0x118 a stored accept, +0x11c wraps, +0x11d an initial option
 * was set, +0x11e has a scroll bar, +0x120 takes input. A widget (vtable: +4 act, +0xc position, +0x10 draw, +0x14
 * show, +0x18 hide, +0x24 focus, +0x28 unfocus, +0x34 move to a height) +0x28 can take focus. Rows outside the shown
 * ones move far down (6969). The cursor plays "POS<n>" for the current row among those shown.
 */
#include "game/gen/api.h"
#include "legacy/link.h"
#include <math.h>

namespace
{
template <typename T> T &At(const void *p, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(p)) + offset);
}

const unsigned int kRefresh = 0x92b703b5;
const unsigned int kBeginText = 0x81017864;
const unsigned int kAccept = 0x0c407210;
const unsigned int kStoreAndWait = 0x911ab364;
const unsigned int kFocusCurrent = 0x35f8620b;
const unsigned int kUp = 0x72619778;
const unsigned int kDown = 0x911c0a4b;
const unsigned int kLeft = 0x9120409e;
const unsigned int kRight = 0xb5971bf1;
const unsigned int kClear = 0xaf0bbd92;
const unsigned int kMouse = 0x9803f6e2;
const unsigned int kTick = 0xc98356ba;
const unsigned int kTextCancelled = 0xc9d30688;
const unsigned int kTextDone = 0xda5b8712;
const unsigned int kSkipSoundA = 0x48122792;
const unsigned int kSkipSoundB = 0x4ac5e165;
const unsigned int kCursorHidden = 0x16a259;
const float kFarDown = 6969.0f;
const float kTicksToSeconds = 0.00025f;
const int *const kRealTimer = reinterpret_cast<const int *>(0x925b14);
int *const kKBCreationTimer = reinterpret_cast<int *>(0x91d4a8);
const float *const kKBDelaySeconds = reinterpret_cast<const float *>(0x8f42c0);
const char *const kEmpty = reinterpret_cast<const char *>(0x890978);

using WithInt = void(__thiscall *)(void *, int);
using Plain = void(__thiscall *)(void *);
using WithText = void(__thiscall *)(void *, const char *);
const Plain kReset = reinterpret_cast<Plain>(0x59c0b0);
using GetNode = char *(__thiscall *)(void *, int);
const GetNode kGetNode = reinterpret_cast<GetNode>(0x45d380);
using GetMousePos = void(__cdecl *)(float *, float *);
const GetMousePos kGetMousePos = reinterpret_cast<GetMousePos>(0x515960);
const bool *const kMousePressed = reinterpret_cast<const bool *>(0x987f71);
const int *const kMouseDeltaX = reinterpret_cast<const int *>(0x987f64);
const int *const kMouseDeltaY = reinterpret_cast<const int *>(0x987f68);
const int *const kMouseWheel = reinterpret_cast<const int *>(0x987f6c);
const unsigned int kMenuOrigin = 0x78b4e2a9;
const unsigned int kBackClicked = 0x406415e3;
const int kBarIdle = 10001;
const int kBarPageDown = 10000;
const int kBarPageUp = -10000;
const WithText kSetInputText = reinterpret_cast<WithText>(0x50e6c0);

void **Vtable(const void *o)
{
    return *static_cast<void **const *>(o);
}

template <typename R, typename... A> R Virtual(void *o, int offset, A... a)
{
    return reinterpret_cast<R(__thiscall *)(void *, A...)>(Vtable(o)[offset / 4])(o, a...);
}

char *First(char *menu)
{
    char *node = At<char *>(menu, 0x2c);
    return node ? node - 4 : nullptr;
}

char *Next(char *widget)
{
    char *node = At<char *>(widget, 4);
    return node ? node - 4 : nullptr;
}

char *End(char *menu)
{
    return menu + 0x28;
}

char *Previous(char *widget)
{
    char *node = At<char *>(widget, 8);
    return node ? node - 4 : nullptr;
}

char *Last(char *menu)
{
    char *node = At<char *>(menu, 0x30);
    return node ? node - 4 : nullptr;
}

/** The back button (+0x4c) becomes the current object, no widget current. */
char *ToBackButton(char *menu)
{
    FEngSetCurrentButton(At<const char *>(menu, 0x10), At<FEObject *>(menu, 0x4c)->NameHash);
    return nullptr;
}
} // namespace

unsigned int __fastcall WidgetMenuGetWidgetIndex(char *menu, void *, char *widget);
void __fastcall WidgetMenuUpdateCursorPos(char *menu, void *);
void __fastcall WidgetMenuSetOption(char *menu, void *, char *widget);
void __fastcall WidgetMenuReposition(char *menu, void *);
void __fastcall WidgetMenuClearWidgets(char *menu, void *);
void __fastcall WidgetMenuRefreshWidgets(char *menu, void *);
void __fastcall WidgetMenuSetInitialPositions(char *menu, void *);
void __fastcall WidgetMenuIncrementStartPos(char *menu, void *);
int __fastcall WidgetMenuNotifySound(char *menu, void *, unsigned int message, int sound);
void __fastcall WidgetMenuNotification(char *menu, void *, unsigned int message, FEObject *o, unsigned int a,
                                       unsigned int b);
void __fastcall WidgetMenuScroll(char *menu, void *, int direction);
void __fastcall WidgetMenuScrollWrapped(char *menu, void *, int direction);
void __fastcall WidgetMenuSetInitialOption(char *menu, void *, int option);
void __fastcall WidgetMenuSyncViewToSelection(char *menu, void *);
char *__fastcall WidgetMenuWidgetAt(char *menu, void *, float x, float y);
void __fastcall WidgetMenuScrollView(char *menu, void *, int rows);
void __fastcall WidgetMenuPageUp(char *menu, void *);
void __fastcall WidgetMenuPageDown(char *menu, void *);
void __fastcall WidgetMenuMouse(char *menu, void *);
void __fastcall WidgetMenuScrollBarTick(char *menu, void *);
void __fastcall ScrollBarUpdateMouse(char *b, void *);
NFS_REPLACE(0x5a58c0, WidgetMenuScroll)
NFS_REPLACE(0x5a5a90, WidgetMenuScrollWrapped)
NFS_REPLACE(0x588d60, WidgetMenuGetWidgetIndex)
NFS_REPLACE(0x593b10, WidgetMenuUpdateCursorPos)
NFS_REPLACE(0x59bfb0, WidgetMenuSetOption)
NFS_REPLACE(0x59bff0, WidgetMenuReposition)
NFS_REPLACE(0x588c90, WidgetMenuClearWidgets)
NFS_REPLACE(0x588d20, WidgetMenuRefreshWidgets)
NFS_REPLACE(0x573dd0, WidgetMenuSetInitialPositions)
NFS_REPLACE(0x573e70, WidgetMenuIncrementStartPos)
NFS_REPLACE(0x573c60, WidgetMenuNotifySound)
NFS_REPLACE(0x5a96c0, WidgetMenuNotification)
NFS_REPLACE(0x5a5ea0, WidgetMenuSetInitialOption)
NFS_REPLACE(0x59c100, WidgetMenuSyncViewToSelection)
NFS_REPLACE(0x5882b0, WidgetMenuWidgetAt)
NFS_REPLACE(0x5a5d40, WidgetMenuScrollView)
NFS_REPLACE(0x5a5e40, WidgetMenuPageUp)
NFS_REPLACE(0x5a5e70, WidgetMenuPageDown)
NFS_REPLACE(0x5a8db0, WidgetMenuMouse)
NFS_REPLACE(0x5a8f60, WidgetMenuScrollBarTick)

unsigned int __fastcall WidgetMenuGetWidgetIndex(char *menu, void *, char *widget)
{
    unsigned int index = 1;
    for (char *w = First(menu); w != End(menu); w = Next(w), ++index)
        if (w == widget)
            return index;
    return 0;
}

void __fastcall WidgetMenuUpdateCursorPos(char *menu, void *)
{
    FEObject *cursor = At<FEObject *>(menu, 0x50);
    if (!cursor)
        return;
    char *current = At<char *>(menu, 0x34);
    if (!current)
    {
        FEngSetScript(cursor, kCursorHidden, true);
        return;
    }
    const unsigned int top = WidgetMenuGetWidgetIndex(menu, nullptr, At<char *>(menu, 0x38));
    const unsigned int row = 1 - top + WidgetMenuGetWidgetIndex(menu, nullptr, current);
    if (row == 0 || row > At<unsigned int>(menu, 0x10c))
    {
        FEngSetScript(cursor, kCursorHidden, true);
        return;
    }
    FEngSetScript(At<FEObject *>(menu, 0x50), FEngHashString("POS%d", row), true);
}

void __fastcall WidgetMenuSetOption(char *menu, void *, char *widget)
{
    char *current = At<char *>(menu, 0x34);
    if (current != widget && current)
        Virtual<void>(current, 0x28);
    At<char *>(menu, 0x34) = widget;
    if (widget)
        Virtual<void>(widget, 0x24, At<const char *>(menu, 0x10));
    WidgetMenuUpdateCursorPos(menu, nullptr);
}

void __fastcall WidgetMenuReposition(char *menu, void *)
{
    const unsigned int top = WidgetMenuGetWidgetIndex(menu, nullptr, At<char *>(menu, 0x38));
    float y = At<float>(menu, 0xd0);
    unsigned int index = 1;
    for (char *w = First(menu); w != End(menu); w = Next(w), ++index)
    {
        if (index >= top && index < At<unsigned int>(menu, 0x10c) + top)
        {
            Virtual<void>(w, 0x14);
            Virtual<void>(w, 0x34, y);
            Virtual<void>(w, 0xc);
            Virtual<void>(w, 0x10);
            y = y + At<float>(menu, 0xe0);
        }
        else
        {
            Virtual<void>(w, 0x34, kFarDown);
            Virtual<void>(w, 0x18);
        }
    }
    WidgetMenuUpdateCursorPos(menu, nullptr);
}

void __fastcall WidgetMenuSetInitialPositions(char *menu, void *)
{
    FEngGetTopLeft(At<FEObject *>(menu, 0x3c), At<float>(menu, 0xcc), At<float>(menu, 0xd0));
    At<float>(menu, 0xd8) = At<float>(menu, 0xd0);
    At<float>(menu, 0xd4) = At<float>(menu, 0xcc);
    FEngGetTopLeft(At<FEObject *>(menu, 0x40), At<float>(menu, 0xf4), At<float>(menu, 0xf8));
    FEngGetSize(At<FEObject *>(menu, 0x3c), At<float>(menu, 0xe4), At<float>(menu, 0xe8));
    FEngGetSize(At<FEObject *>(menu, 0x40), At<float>(menu, 0xec), At<float>(menu, 0xf0));
    At<float>(menu, 0xe0) = At<float>(menu, 0xe8);
    At<float>(menu, 0xdc) = static_cast<float>(
        fabs(static_cast<double>(At<float>(menu, 0xcc)) -
             (static_cast<double>(At<float>(menu, 0xf4)) + At<float>(menu, 0xec))));
}

void __fastcall WidgetMenuIncrementStartPos(char *menu, void *)
{
    const float y = static_cast<float>(static_cast<double>(At<float>(menu, 0x100)) + At<float>(menu, 0xe0) +
                                       At<float>(menu, 0xd8));
    At<float>(menu, 0xd8) = y;
    At<float>(menu, 0xf8) = y;
}

void __fastcall WidgetMenuClearWidgets(char *menu, void *)
{
    for (char *w = First(menu); w != End(menu); w = Next(w))
    {
        Virtual<void>(w, 0x18);
        Virtual<void>(w, 0x28);
    }
    char **head = &At<char *>(menu, 0x2c);
    while (*head != reinterpret_cast<char *>(head))
    {
        char **node = reinterpret_cast<char **>(*head);
        char *next = node[0];
        char **prev = reinterpret_cast<char **>(node[1]);
        *prev = next;
        reinterpret_cast<char **>(next)[1] = reinterpret_cast<char *>(prev);
        if (char *w = reinterpret_cast<char *>(node) - 4)
            Virtual<void>(w, 0, 1);
    }
    At<unsigned int>(menu, 0x104) = 1;
    At<unsigned int>(menu, 0x108) = 1;
    At<char *>(menu, 0x34) = nullptr;
    At<bool>(menu, 0x11d) = false;
    WidgetMenuSetInitialPositions(menu, nullptr);
}

void __fastcall WidgetMenuRefreshWidgets(char *menu, void *)
{
    for (char *w = First(menu); w != End(menu); w = Next(w))
        Virtual<void>(w, 0xc);
}

int __fastcall WidgetMenuNotifySound(char *menu, void *, unsigned int message, int sound)
{
    char *current = At<char *>(menu, 0x34);
    if ((message == kSkipSoundA || message == kSkipSoundB) && current && !At<bool>(current, 0x2a))
        return -1;
    return sound;
}

void __fastcall WidgetMenuNotification(char *menu, void *, unsigned int message, FEObject *o, unsigned int a,
                                       unsigned int b)
{
    bool &input = At<bool>(menu, 0x120);
    char *current = At<char *>(menu, 0x34);
    switch (message)
    {
    case kRefresh:
        WidgetMenuRefreshWidgets(menu, nullptr);
        return;
    case kBeginText:
    {
        if (static_cast<float>(*kRealTimer - *kKBCreationTimer) * kTicksToSeconds < *kKBDelaySeconds)
            return;
        kSetInputText(current, kEmpty);
        reinterpret_cast<MenuScreen *>(menu)->FEngBeginTextInput(
            At<unsigned int>(At<char *>(current, 0x30), 0x10), At<unsigned int>(current, 0x188), current + 0x4c,
            current + 0xe8, At<unsigned int>(current, 0x184));
        input = false;
        return;
    }
    case kFocusCurrent:
        if (current && At<bool>(current, 0x28))
            WidgetMenuSetOption(menu, nullptr, current);
        return;
    case kUp:
    case kDown:
        if (!input)
            return;
        if (At<bool>(menu, 0x11c))
            WidgetMenuScrollWrapped(menu, nullptr, message == kUp ? -1 : 1);
        else
            WidgetMenuScroll(menu, nullptr, message == kUp ? -1 : 1);
        return;
    case kAccept:
    case kStoreAndWait:
        At<FEObject *>(menu, 0x44) = o;
        At<unsigned int>(menu, 0x110) = message;
        At<unsigned int>(menu, 0x114) = a;
        At<unsigned int>(menu, 0x118) = b;
        break;
    case kLeft:
    case kRight:
        break;
    case kClear:
        WidgetMenuClearWidgets(menu, nullptr);
        Virtual<void>(menu, 0xc);
        return;
    case kMouse:
        WidgetMenuMouse(menu, nullptr);
        return;
    case kTick:
        WidgetMenuScrollBarTick(menu, nullptr);
        return;
    case kTextDone:
    {
        *kKBCreationTimer = *kRealTimer;
        char *edited = current;
        bStrNCpy(edited + 0x4c, reinterpret_cast<MenuScreen *>(menu)->FEngGetEditedString(), 0x9b);
        char *now = At<char *>(menu, 0x34);
        if (now && At<bool>(now, 0x28))
            Virtual<void>(now, 4, At<const char *>(menu, 0x10), kTextDone);
        Virtual<void>(edited, 0xc);
        input = true;
        return;
    }
    case kTextCancelled:
        input = true;
        return;
    default:
        return;
    }
    if (input && current && At<bool>(current, 0x28))
        Virtual<void>(current, 4, At<const char *>(menu, 0x10), message);
}

namespace
{
/** The end of a scroll: the rows shown from `top`, `current` focused, the scroll bar told. */
void Settle(char *menu, char *top, char *current)
{
    if (At<char *>(menu, 0x38) != top)
    {
        At<char *>(menu, 0x38) = top;
        WidgetMenuReposition(menu, nullptr);
    }
    if (At<char *>(menu, 0x34) == current)
        return;
    WidgetMenuSetOption(menu, nullptr, current);
    char *now = At<char *>(menu, 0x34);
    if (At<bool>(menu, 0x11e) && now)
    {
        const unsigned int row = WidgetMenuGetWidgetIndex(menu, nullptr, now);
        reinterpret_cast<FEScrollBar *>(menu + 0x54)->Update(At<unsigned int>(menu, 0x10c), At<unsigned int>(menu, 0x104) - 1,
                         WidgetMenuGetWidgetIndex(menu, nullptr, At<char *>(menu, 0x38)), row);
    }
}

bool Focusable(char *widget)
{
    return At<bool>(widget, 0x28);
}
} // namespace

void __fastcall WidgetMenuScroll(char *menu, void *, int direction)
{
    if (At<char *>(menu, 0x2c) == menu + 0x2c)
        return;
    if (At<bool>(menu, 0x11f))
    {
        WidgetMenuSyncViewToSelection(menu, nullptr);
        return;
    }
    char *top = At<char *>(menu, 0x38);
    char *current = At<char *>(menu, 0x34);
    int &row = At<int>(menu, 0x108);
    if (direction == 1)
    {
        if (current && current == Last(menu) && At<void *>(menu, 0x4c))
            return Settle(menu, top, ToBackButton(menu));
        if (current != Last(menu))
        {
            const int lastRow = At<int>(menu, 0x104) - 1;
            do
            {
                current = Next(current);
                row = row + 1 > lastRow ? lastRow : row + 1;
            } while (current && !Focusable(current) && current != Last(menu));
        }
        if (WidgetMenuGetWidgetIndex(menu, nullptr, current) >=
            WidgetMenuGetWidgetIndex(menu, nullptr, top) + At<unsigned int>(menu, 0x10c))
            top = Next(top);
    }
    else
    {
        if (!current)
            return Settle(menu, top, Last(menu));
        if (current != First(menu))
            do
            {
                current = Previous(current);
                row = row - 1 < 1 ? 1 : row - 1;
            } while (current && !Focusable(current) && current != First(menu));
        if (current == Previous(top))
            top = Previous(top);
    }
    Settle(menu, top, current);
}

void __fastcall WidgetMenuScrollWrapped(char *menu, void *, int direction)
{
    if (At<char *>(menu, 0x2c) == menu + 0x2c)
        return;
    if (At<bool>(menu, 0x11f))
    {
        WidgetMenuSyncViewToSelection(menu, nullptr);
        return;
    }
    char *top = At<char *>(menu, 0x38);
    char *current = At<char *>(menu, 0x34);
    unsigned int &row = At<unsigned int>(menu, 0x108);
    const unsigned int shown = At<unsigned int>(menu, 0x10c);
    const unsigned int rows = At<unsigned int>(menu, 0x104) - 1;
    const bool back = At<void *>(menu, 0x4c) != nullptr;
    if (direction == 1)
    {
        for (;;)
        {
            if (!current || (current == Last(menu) && !back))
            {
                current = First(menu);
                top = current;
            }
            else if (current == Last(menu))
            {
                current = ToBackButton(menu);
                break;
            }
            else
            {
                current = Next(current);
                if (++row > rows)
                    row = 1;
            }
            if (!current || Focusable(current))
                break;
        }
        const unsigned int at = WidgetMenuGetWidgetIndex(menu, nullptr, current);
        while (at >= WidgetMenuGetWidgetIndex(menu, nullptr, top) + shown)
            top = Next(top);
    }
    else
    {
        for (;;)
        {
            if (!current || (current == First(menu) && !back))
            {
                current = Last(menu);
                const int from = static_cast<int>(rows - shown);
                top = kGetNode(menu + 0x2c, from < 0 ? 0 : from);
                top = top ? top - 4 : nullptr;
            }
            else if (current == First(menu))
            {
                current = ToBackButton(menu);
                break;
            }
            else
            {
                current = Previous(current);
                if (--row < 1)
                    row = rows;
            }
            if (!current || Focusable(current))
                break;
        }
        if (rows > shown && current == Previous(At<char *>(menu, 0x38)))
            top = Previous(At<char *>(menu, 0x38));
    }
    Settle(menu, top, current);
}

void __fastcall WidgetMenuSyncViewToSelection(char *menu, void *)
{
    if (At<char *>(menu, 0x2c) == menu + 0x2c)
        return;
    char *current = At<char *>(menu, 0x34);
    if (!current && !At<FEObject *>(menu, 0x4c))
    {
        kReset(menu);
        return;
    }
    const unsigned int shown = At<unsigned int>(menu, 0x10c);
    const unsigned int count = At<unsigned int>(menu, 0x104);
    if (count - 1 > shown && WidgetMenuGetWidgetIndex(menu, nullptr, current) <= count - shown)
        At<char *>(menu, 0x38) = current;
    else
    {
        const int from = static_cast<int>(count - shown - 1);
        char *node = kGetNode(menu + 0x2c, from < 0 ? 0 : from);
        At<char *>(menu, 0x38) = node ? node - 4 : nullptr;
    }
    WidgetMenuReposition(menu, nullptr);
    At<bool>(menu, 0x11f) = false;
}

void __fastcall WidgetMenuSetInitialOption(char *menu, void *, int option)
{
    if (At<char *>(menu, 0x2c) != menu + 0x2c && !At<bool>(menu, 0x11d))
    {
        bool chosen = false;
        bool search = true;
        if (option)
        {
            char *node = kGetNode(menu + 0x2c, option - 1);
            char *widget = node ? node - 4 : nullptr;
            if (widget && widget != End(menu) && At<bool>(widget, 0x28))
            {
                WidgetMenuSetOption(menu, nullptr, widget);
                At<int>(menu, 0x108) = option;
                chosen = true;
                search = false;
            }
        }
        else if (At<FEObject *>(menu, 0x4c))
        {
            char *focused = At<char *>(menu, 0x34);
            if (focused)
            {
                Virtual<void>(focused, 0x28);
                At<char *>(menu, 0x34) = nullptr;
            }
            FEngSetCurrentButton(At<const char *>(menu, 0x10), At<FEObject *>(menu, 0x4c)->NameHash);
            search = false;
        }
        if (search)
        {
            At<int>(menu, 0x108) = 1;
            for (char *w = First(menu); w; w = Next(w), ++At<int>(menu, 0x108))
                if (At<bool>(w, 0x28) || w == End(menu))
                {
                    WidgetMenuSetOption(menu, nullptr, w);
                    chosen = true;
                    break;
                }
        }
        if (chosen)
            At<bool>(menu, 0x11d) = true;
        WidgetMenuSyncViewToSelection(menu, nullptr);
    }
    if (At<bool>(menu, 0x11e))
    {
        const int current = static_cast<int>(WidgetMenuGetWidgetIndex(menu, nullptr, At<char *>(menu, 0x34)));
        const int top = static_cast<int>(WidgetMenuGetWidgetIndex(menu, nullptr, At<char *>(menu, 0x38)));
        reinterpret_cast<FEScrollBar *>(menu + 0x54)->Update(At<int>(menu, 0x10c), At<int>(menu, 0x104) - 1, top,
                                                             current);
    }
}

/** The widget whose box (+0xc/+0x10 its place, +0x14/+0x18 its size) holds x, y, skipping hidden ones (+0x29). */
char *__fastcall WidgetMenuWidgetAt(char *menu, void *, float x, float y)
{
    for (char *w = First(menu); w != End(menu); w = Next(w))
    {
        if (At<bool>(w, 0x29))
            continue;
        const bVector2 at(At<float>(w, 0xc), At<float>(w, 0x10));
        const bVector2 size(At<float>(w, 0x14), At<float>(w, 0x18));
        if (FEngTestForIntersection(x, y, at, size))
            return w;
    }
    return nullptr;
}

void __fastcall WidgetMenuScrollView(char *menu, void *, int rows)
{
    if (At<char *>(menu, 0x2c) == menu + 0x2c)
        return;
    const unsigned int count = At<unsigned int>(menu, 0x104);
    const unsigned int shown = At<unsigned int>(menu, 0x10c);
    if (count < shown)
        return;
    char *const before = At<char *>(menu, 0x38);
    for (; rows >= 1; --rows)
    {
        char *top = At<char *>(menu, 0x38);
        if (WidgetMenuGetWidgetIndex(menu, nullptr, top) >= count - shown)
            break;
        At<char *>(menu, 0x38) = Next(top);
    }
    for (; rows <= -1; ++rows)
    {
        if (At<char *>(menu, 0x38) == First(menu))
            break;
        At<char *>(menu, 0x38) = Previous(At<char *>(menu, 0x38));
    }
    char *top = At<char *>(menu, 0x38);
    if (before == top)
        return;
    const unsigned int current = WidgetMenuGetWidgetIndex(menu, nullptr, At<char *>(menu, 0x34));
    const unsigned int first = WidgetMenuGetWidgetIndex(menu, nullptr, top);
    At<bool>(menu, 0x11f) = !(current >= first && current < At<unsigned int>(menu, 0x10c) + first);
    WidgetMenuReposition(menu, nullptr);
}

void __fastcall WidgetMenuPageUp(char *menu, void *)
{
    for (unsigned int i = 0; i < At<unsigned int>(menu, 0x10c); ++i)
        WidgetMenuScrollView(menu, nullptr, -1);
}

void __fastcall WidgetMenuPageDown(char *menu, void *)
{
    for (unsigned int i = 0; i < At<unsigned int>(menu, 0x10c); ++i)
        WidgetMenuScrollView(menu, nullptr, 1);
}

void __fastcall WidgetMenuMouse(char *menu, void *)
{
    float x, y, cx, cy;
    kGetMousePos(&x, &y);
    FEngGetCenter(FEngFindObject(At<const char *>(menu, 0x10), kMenuOrigin), cx, cy);
    x -= cx;
    y -= cy;
    FEObject *back = At<FEObject *>(menu, 0x4c);
    if (*kMousePressed)
    {
        char *w = WidgetMenuWidgetAt(menu, nullptr, x, y);
        if (w && At<bool>(w, 0x28))
        {
            char *current = At<char *>(menu, 0x34);
            if (current && w == current)
                Virtual<void>(current, 8, At<const char *>(menu, 0x10), x, y);
            else
                WidgetMenuSetOption(menu, nullptr, w);
            return;
        }
        if (At<bool>(menu, 0x120) && FEngTestForIntersection(x, y, back))
            cFEng::mInstance->QueueGameMessage(kBackClicked, At<const char *>(menu, 0x10), 0xff);
        return;
    }
    if (*kMouseWheel)
    {
        const float wheel = static_cast<float>(*kMouseWheel);
        if (wheel > 0.0f)
            WidgetMenuScrollView(menu, nullptr, -1);
        else if (wheel < 0.0f)
            WidgetMenuScrollView(menu, nullptr, 1);
        return;
    }
    if (!*kMouseDeltaX && !*kMouseDeltaY)
        return;
    if (WidgetMenuWidgetAt(menu, nullptr, x, y) || !FEngTestForIntersection(x, y, back) || !At<bool>(menu, 0x120))
        return;
    char *current = At<char *>(menu, 0x34);
    if (current)
    {
        Virtual<void>(current, 0x28);
        At<char *>(menu, 0x34) = nullptr;
    }
    FEngSetCurrentButton(At<const char *>(menu, 0x10), back->NameHash);
}

void __fastcall WidgetMenuScrollBarTick(char *menu, void *)
{
    FEScrollBar *bar = reinterpret_cast<FEScrollBar *>(menu + 0x54);
    ScrollBarUpdateMouse(menu + 0x54, nullptr);
    const int asked = At<int>(menu, 0xac);
    if (asked == kBarIdle)
    {
        const int current = static_cast<int>(WidgetMenuGetWidgetIndex(menu, nullptr, At<char *>(menu, 0x34)));
        const int top = static_cast<int>(WidgetMenuGetWidgetIndex(menu, nullptr, At<char *>(menu, 0x38)));
        bar->Update(At<int>(menu, 0x10c), At<int>(menu, 0x104) - 1, top, current);
    }
    else if (asked == kBarPageDown)
        WidgetMenuPageDown(menu, nullptr);
    else if (asked == kBarPageUp)
        WidgetMenuPageUp(menu, nullptr);
    else
        WidgetMenuScrollView(menu, nullptr, asked);
}
