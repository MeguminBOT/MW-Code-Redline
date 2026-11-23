/**
 * The grid menus (car lot, shop parts lists and the like): data shown in rows of slots, moved through by the pad
 * (original: zFe2 ArrayScroller::GetCurrentDatumNum 0x510680, AddSlot 0x5881f0, GetDatumAt 0x588250, RefreshHeader
 * 0x5931b0, SetSelection 0x593250, ScrollHor 0x5932c0, UpdateScrollbar 0x593410, SetInitialPosition 0x593560, AddDatum
 * 0x59bd30, ScrollVer 0x59bd70, NotificationMessage 0x5a5810; ArraySlot::Update 0x5739d0; ArrayScrollerMenu
 * NotifySoundMessage 0x573c00, NotificationMessage 0x5a58b0).
 *
 * ArrayScroller (vtable +8 refresh): +4 moved, +8 its slots (a list: first, last; a slot's node 4 bytes in), +0x10 its
 * data (the same), +0x18 the current datum, +0x1c the index of the first datum shown, +0x20 columns, +0x24 rows,
 * +0x28 the header string's hash, +0x2c the package's name, +0x34 its scroll bar, +0x94 the current datum's slot
 * takes the pads, +0x98 the slots' scripts (+4 normal, +8 enabled, +0xc highlighted, +0x10 unhighlighted). A datum
 * (vtable +4 act): +0x10 its label, +0x14 enabled, +0x15 locked. A slot: +0xc its object, +0x10 the scripts.
 * ArrayScrollerMenu: a MenuScreen with its ArrayScroller at +0x2c; +0x30 lets the next direction sound play.
 * A list position (TraversebList): a node's index plus one, or the count for none.
 */
#include "game/gen/api.h"
#include "legacy/link.h"
#include "core/config.h"
#include "core/log.h"

namespace
{
template <typename T> T &At(const void *p, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(p)) + offset);
}

const unsigned int kUp = 0x72619778;
const unsigned int kDown = 0x911c0a4b;
const unsigned int kLeft = 0x9120409e;
const unsigned int kRight = 0xb5971bf1;
const unsigned int kMouse = 0x9803f6e2;

using Traverse = int(__thiscall *)(void *, void *);
const Traverse kTraverse = reinterpret_cast<Traverse>(0x45d3a0);
using GetNode = char *(__thiscall *)(void *, int);
const GetNode kGetNode = reinterpret_cast<GetNode>(0x45d380);
using Plain = void(__thiscall *)(void *);
const Plain kUpdateMouse = reinterpret_cast<Plain>(0x593600);

void **Vtable(const void *o)
{
    return *static_cast<void **const *>(o);
}

template <typename R, typename... A> R Virtual(void *o, int offset, A... a)
{
    return reinterpret_cast<R(__thiscall *)(void *, A...)>(Vtable(o)[offset / 4])(o, a...);
}

void *Slots(char *s)
{
    return s + 8;
}

void *Data(char *s)
{
    return s + 0x10;
}

int Count(void *list)
{
    return kTraverse(list, nullptr);
}

/** A datum's or slot's list position (its node is 4 bytes in). */
int Position(void *list, char *item)
{
    return kTraverse(list, item ? item + 4 : nullptr);
}

char *Item(void *list, int index)
{
    char *node = kGetNode(list, index);
    return node ? node - 4 : nullptr;
}

char *Next(char *item)
{
    char *node = At<char *>(item, 4);
    return node ? node - 4 : nullptr;
}

char *Previous(char *item)
{
    char *node = At<char *>(item, 8);
    return node ? node - 4 : nullptr;
}

void Append(char *list, char *node)
{
    char **last = &At<char *>(list, 4);
    char *tail = *last;
    *reinterpret_cast<char **>(tail) = node;
    *last = node;
    reinterpret_cast<char **>(node)[1] = tail;
    reinterpret_cast<char **>(node)[0] = list;
}

/** The first datum shown when the datum at `index` becomes current (the rows scroll to show it). */
int TopFor(char *s, int index, int top)
{
    const int columns = At<int>(s, 0x20), rows = At<int>(s, 0x24);
    if (index < top)
        return index / columns * columns;
    if (index > rows * columns + top)
        return (index / columns - rows + 1) * columns;
    return top;
}
} // namespace

void __fastcall ArraySetSelection(char *s, void *, char *datum, int top);
void __fastcall ArrayScrollHor(char *s, void *, int direction);
void __fastcall ArrayScrollVer(char *s, void *, int direction);
void __fastcall ArrayUpdateScrollbar(char *s, void *);
void __fastcall ArrayRefreshHeader(char *s, void *);
void __fastcall ArrayNotification(char *s, void *, unsigned int message, FEObject *o, unsigned int a, unsigned int b);
void __fastcall ArrayMenuNotification(char *menu, void *, unsigned int message, FEObject *o, unsigned int a,
                                      unsigned int b);
int __fastcall ArrayMenuNotifySound(char *menu, void *, unsigned int message, int sound);
void __fastcall ArraySlotUpdate(char *slot, void *, char *datum, bool current);
NFS_REPLACE(0x593250, ArraySetSelection)
NFS_REPLACE(0x5932c0, ArrayScrollHor)
NFS_REPLACE(0x59bd70, ArrayScrollVer)
NFS_REPLACE(0x593410, ArrayUpdateScrollbar)
NFS_REPLACE(0x5931b0, ArrayRefreshHeader)
NFS_REPLACE(0x5a5810, ArrayNotification)
NFS_REPLACE(0x5a58b0, ArrayMenuNotification)
NFS_REPLACE(0x573c00, ArrayMenuNotifySound)
NFS_REPLACE(0x5739d0, ArraySlotUpdate)

int ArrayScroller::GetCurrentDatumNum()
{
    char *s = reinterpret_cast<char *>(this);
    return Position(Data(s), At<char *>(s, 0x18)) - 1;
}

void ArrayScroller::AddSlot(ArraySlot *slot)
{
    char *s = reinterpret_cast<char *>(this);
    char *item = reinterpret_cast<char *>(slot);
    At<char *>(item, 0x10) = s + 0x98;
    Append(s + 8, item + 4);
}

ArrayDatum *ArrayScroller::GetDatumAt(int index)
{
    char *s = reinterpret_cast<char *>(this);
    return index < Count(Data(s)) ? reinterpret_cast<ArrayDatum *>(Item(Data(s), index)) : nullptr;
}

void ArrayScroller::AddDatum(ArrayDatum *datum)
{
    char *s = reinterpret_cast<char *>(this);
    char *item = reinterpret_cast<char *>(datum);
    Append(s + 0x10, item ? item + 4 : nullptr);
    if (!At<char *>(s, 0x18))
        ArraySetSelection(s, nullptr, item, 0);
}

void ArrayScroller::SetInitialPosition(int index)
{
    char *s = reinterpret_cast<char *>(this);
    if (!Count(Data(s)))
        ArrayUpdateScrollbar(s, nullptr);
    if (index >= Count(Data(s)))
        return;
    const int columns = At<int>(s, 0x20);
    const int shown = At<int>(s, 0x24) * columns;
    int top = 0;
    if (index > shown - 1)
    {
        top = ((index / columns * columns - shown + 1) / columns + 1) * columns;
        if (columns == 1)
            --top;
    }
    char *datum = index < Count(Data(s)) ? Item(Data(s), index) : nullptr;
    ArraySetSelection(s, nullptr, datum, top);
    ArrayUpdateScrollbar(s, nullptr);
}

void __fastcall ArraySetSelection(char *s, void *, char *datum, int top)
{
    if (!At<bool>(datum, 0x14))
        return;
    At<int>(s, 0x1c) = top;
    At<char *>(s, 0x18) = datum;
    if (!At<bool>(s, 0x94))
        return;
    const int slot = Position(Data(s), datum) - At<int>(s, 0x1c) - 1;
    if (slot >= Count(Slots(s)))
        return;
    if (char *item = Item(Slots(s), slot))
        FEngSetCurrentButton(At<const char *>(s, 0x2c), At<FEObject *>(item, 0xc)->NameHash);
}

void __fastcall ArrayScrollHor(char *s, void *, int direction)
{
    if (!Count(Data(s)))
        return;
    char *datum = At<char *>(s, 0x18);
    const int index = Position(Data(s), datum) - 1;
    const int columns = At<int>(s, 0x20);
    int to = index;
    if (direction == -1)
    {
        to = index - 1;
        if (index % columns == 0)
            to += columns;
        if (to >= Count(Data(s)))
            to = Count(Data(s)) - 1;
    }
    else if (direction == 1)
    {
        to = index + 1;
        if (to % columns == 0)
            to -= columns;
        if (to >= Count(Data(s)))
            to -= to % columns;
    }
    if (direction == -1 || direction == 1)
    {
        if (index < to)
        {
            if (to < Count(Data(s)))
                for (int n = to - index; n; --n)
                    datum = Next(datum);
        }
        else if (index > to && to >= 0)
            for (int n = index - to; n; --n)
                datum = Previous(datum);
    }
    if (datum != At<char *>(s, 0x18))
    {
        ArraySetSelection(s, nullptr, datum, TopFor(s, to, At<int>(s, 0x1c)));
        At<bool>(s, 4) = true;
    }
    Virtual<void>(s, 8);
}

void __fastcall ArrayScrollVer(char *s, void *, int direction)
{
    if (!Count(Data(s)))
        return;
    char *datum = At<char *>(s, 0x18);
    int index = Position(Data(s), datum) - 1;
    const int columns = At<int>(s, 0x20), rows = At<int>(s, 0x24);
    const bool clamp = At<bool>(s, 0x94);
    int top = At<int>(s, 0x1c);
    if (direction == -1)
    {
        index -= columns;
        if (!clamp)
        {
            const int lastRow = (rows - 1) * columns;
            if (index >= lastRow && index < rows * columns)
                index -= lastRow;
            top -= columns;
        }
        else if (index < top)
            top -= columns;
        if (index >= 0)
            for (int k = 0; k < Position(Data(s), At<char *>(s, 0x18)) - 1 - index; ++k)
                datum = Previous(datum);
    }
    else if (direction == 1)
    {
        index += columns;
        if (!clamp)
        {
            if (index >= columns && index < columns * 2)
                index += (rows - 1) * columns;
            top += columns;
        }
        else
        {
            if (index >= Count(Data(s)))
            {
                const int pages = ((Count(Data(s)) - 1) / columns + 1) * rows;
                const int at = (reinterpret_cast<ArrayScroller *>(s)->GetCurrentDatumNum() / columns + 1) * rows;
                if (pages > at)
                    index = Count(Data(s)) - 1;
            }
            if (Count(Slots(s)) + top <= index)
                top += columns;
        }
        if (index < Count(Data(s)))
            for (int k = 0; k < index - Position(Data(s), At<char *>(s, 0x18)) + 1; ++k)
                datum = Next(datum);
    }
    if (datum != At<char *>(s, 0x18))
    {
        ArraySetSelection(s, nullptr, datum, TopFor(s, index, top));
        ArrayUpdateScrollbar(s, nullptr);
        At<bool>(s, 4) = true;
    }
    Virtual<void>(s, 8);
}

void __fastcall ArrayUpdateScrollbar(char *s, void *)
{
    const int columns = At<int>(s, 0x20);
    reinterpret_cast<FEScrollBar *>(s + 0x34)->Update(At<int>(s, 0x24), (Count(Data(s)) - 1) / columns + 1, At<int>(s, 0x1c) / columns + 1,
                     (Position(Data(s), At<char *>(s, 0x18)) - 1) / columns + 1);
}

void __fastcall ArrayRefreshHeader(char *s, void *)
{
    for (int i = 0; i < Count(Slots(s)); ++i)
    {
        const int at = At<int>(s, 0x1c) + i;
        char *datum = at < Count(Data(s)) ? Item(Data(s), at) : nullptr;
        if (i < Count(Slots(s)))
            if (char *slot = Item(Slots(s), i))
                Virtual<void>(slot, 4, datum, At<char *>(s, 0x18) == datum);
    }
    if (char *current = At<char *>(s, 0x18))
        FEngSetLanguageHash(At<const char *>(s, 0x2c), At<unsigned int>(s, 0x28), At<unsigned int>(current, 0x10));
}

void __fastcall ArrayNotification(char *s, void *, unsigned int message, FEObject *o, unsigned int a, unsigned int b)
{
    static const bool keyLog = nfs::config::Int("Debug", "KeyLog", 0) != 0;
    if (keyLog && (message == kLeft || message == kRight || message == kUp || message == kDown))
        nfs::Log("array scroller: %s %08x", At<const char *>(s, 0x2c), message);
    if (char *current = At<char *>(s, 0x18))
        Virtual<void>(current, 4, message, o, a, b);
    switch (message)
    {
    case kLeft:
        ArrayScrollHor(s, nullptr, -1);
        return;
    case kUp:
        ArrayScrollVer(s, nullptr, -1);
        return;
    case kDown:
        ArrayScrollVer(s, nullptr, 1);
        return;
    case kRight:
        ArrayScrollHor(s, nullptr, 1);
        return;
    case kMouse:
        kUpdateMouse(s);
        return;
    }
}

void __fastcall ArrayMenuNotification(char *menu, void *, unsigned int message, FEObject *o, unsigned int a,
                                      unsigned int b)
{
    ArrayNotification(menu + 0x2c, nullptr, message, o, a, b);
}

int __fastcall ArrayMenuNotifySound(char *menu, void *, unsigned int message, int sound)
{
    switch (message)
    {
    case kLeft:
    case kRight:
    case kDown:
    case kUp:
    case 0x480df13f:
    case 0xb205316c:
    case 0x48122792:
    case 0x4ac5e165:
    {
        const int played = At<bool>(menu, 0x30) ? sound : -1;
        At<bool>(menu, 0x30) = false;
        return played;
    }
    default:
        return sound;
    }
}

void __fastcall ArraySlotUpdate(char *slot, void *, char *datum, bool current)
{
    FEObject *o = At<FEObject *>(slot, 0xc);
    if (!datum)
    {
        FEngSetInvisible(o);
        return;
    }
    FEngSetVisible(o);
    const char *scripts = At<const char *>(slot, 0x10);
    unsigned int script = At<unsigned int>(scripts, 4);
    if (!At<bool>(datum, 0x15) && At<bool>(datum, 0x14))
    {
        if (current)
            script = At<unsigned int>(scripts, 0xc);
        else if (FEngIsScriptSet(o, At<unsigned int>(scripts, 0xc)))
        {
            FEngSetScript(o, At<unsigned int>(scripts, 0x10), true);
            return;
        }
        else
            script = At<unsigned int>(scripts, 8);
    }
    if (!FEngIsScriptSet(o, script))
        FEngSetScript(o, script, true);
}
