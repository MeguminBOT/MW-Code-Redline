/**
 * The text lists (Scrollerina: slots showing a window of data) and their slots (original: zFe Scrollerina::
 * HighlightSelected 0x5110d0; zFe2 ScrollerSlot::FindSize 0x573170, SetScript 0x586760,
 * Show 0x5867d0, Hide 0x586830; Scrollerina::AddData 0x5868e0, Print 0x586a10, GetNodeIndex 0x586ca0, FindSize 0x586dc0, AddSlot
 * 0x5922b0, FindDatumInSlot 0x592410, FindSlotWithDatum 0x592490, Update 0x5926b0, Enable 0x592710, SetSelected
 * 0x5929d0).
 *
 * Scrollerina (vtable +4 print): its slots in a list at +8 (a slot's node 4 bytes in; the list ends at the slot "at"
 * +4), its data at +0x10 (the same; ends at +0xc), +0x18 the slot count, +0x1c the data count, +0x20 the selected row,
 * +0x24 the selected datum, +0x28 the first datum shown, +0x2c the selected slot, +0x30 its size fixed, +0x34 its
 * scroll bar, +0x90..+0x9c its box, +0xa0 has a scroll bar, +0xa1 moved, +0xb0 one more object in its box.
 * ScrollerSlot: its strings in a list at +0xc (each with its object at +0xc; ends at +8), +0x14 its backing,
 * +0x18..+0x24 its box, +0x28 can be chosen. A datum: its texts in a list at +0xc (a text: +0xc the text, +0x8c its label), +0x14 enabled.
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

const unsigned int kHighlight = 0x249db7b7;
const unsigned int kUnhighlight = 0x7ab5521a;

void **Vtable(const void *o)
{
    return *static_cast<void **const *>(o);
}

template <typename R, typename... A> R Virtual(void *o, int offset, A... a)
{
    return reinterpret_cast<R(__thiscall *)(void *, A...)>(Vtable(o)[offset / 4])(o, a...);
}

char *Item(char *node)
{
    return node ? node - 4 : nullptr;
}

char *Next(char *item)
{
    return Item(At<char *>(item, 4));
}

char *FirstSlot(char *s)
{
    return Item(At<char *>(s, 8));
}

char *SlotsEnd(char *s)
{
    return s + 4;
}

char *FirstDatum(char *s)
{
    return Item(At<char *>(s, 0x10));
}

char *DataEnd(char *s)
{
    return s + 0xc;
}

bool Empty(char *s)
{
    return At<char *>(s, 8) == s + 8 || At<char *>(s, 0x10) == s + 0x10;
}

void Append(char *list, char *node)
{
    char *tail = At<char *>(list, 4);
    *reinterpret_cast<char **>(tail) = node;
    At<char *>(list, 4) = node;
    reinterpret_cast<char **>(node)[1] = tail;
    reinterpret_cast<char **>(node)[0] = list;
}

}
unsigned int __fastcall ListGetNodeIndex(char *s, void *, char *datum);
namespace
{
void UpdateBar(char *s)
{
    if (!At<bool>(s, 0xa0))
        return;
    const int at = static_cast<int>(ListGetNodeIndex(s, nullptr, At<char *>(s, 0x24)));
    reinterpret_cast<FEScrollBar *>(s + 0x34)->Update(At<int>(s, 0x18), At<int>(s, 0x1c), At<int>(s, 0x20), at);
}

/** Grows a box (left, top, right, bottom) to take in one at (x, y) of (w, h). */
void Take(float box[4], float x, float y, float w, float h)
{
    if (y <= box[1])
        box[1] = y;
    if (box[3] <= static_cast<double>(h) + y)
        box[3] = h + y;
    if (x <= box[0])
        box[0] = x;
    if (box[2] <= static_cast<double>(w) + x)
        box[2] = w + x;
}
} // namespace

void __fastcall SlotFindSize(char *slot, void *);
void __fastcall SlotSetScript(char *slot, void *, unsigned int script);
void __fastcall SlotShow(char *slot, void *);
void __fastcall SlotHide(char *slot, void *);
void __fastcall ListPrint(char *s, void *);
NFS_REPLACE(0x586a10, ListPrint)
unsigned int __fastcall ListGetNodeIndex(char *s, void *, char *datum);
void __fastcall ListFindSize(char *s, void *);
char *__fastcall ListFindSlotWithDatum(char *s, void *, char *datum);
NFS_REPLACE(0x586ca0, ListGetNodeIndex)
NFS_REPLACE(0x586dc0, ListFindSize)
NFS_REPLACE(0x592490, ListFindSlotWithDatum)
NFS_REPLACE(0x573170, SlotFindSize)
NFS_REPLACE(0x586760, SlotSetScript)
NFS_REPLACE(0x5867d0, SlotShow)
NFS_REPLACE(0x586830, SlotHide)

void __fastcall SlotFindSize(char *slot, void *)
{
    float x = 0, y = 0, right = 0, bottom = 0;
    if (FEObject *backing = At<FEObject *>(slot, 0x14))
    {
        float a, b;
        FEngGetTopLeft(backing, a, y);
        FEngGetTopLeft(backing, x, b);
        float w, h, unused;
        FEngGetSize(backing, w, unused);
        right = w + x;
        FEngGetSize(backing, unused, h);
        bottom = h + y;
    }
    At<float>(slot, 0x18) = x;
    At<float>(slot, 0x1c) = y;
    At<float>(slot, 0x20) = static_cast<float>(fabs(static_cast<double>(x) - right));
    At<float>(slot, 0x24) = static_cast<float>(fabs(static_cast<double>(y) - bottom));
}

void __fastcall SlotSetScript(char *slot, void *, unsigned int script)
{
    for (char *s = Item(At<char *>(slot, 0xc)); s != slot + 8; s = Next(s))
        if (s && At<FEObject *>(s, 0xc))
            FEngSetScript(At<FEObject *>(s, 0xc), script, true);
    if (FEObject *backing = At<FEObject *>(slot, 0x14))
        FEngSetScript(backing, script, true);
}

void __fastcall SlotShow(char *slot, void *)
{
    if (At<char *>(slot, 0xc) == slot + 0xc)
        return;
    for (char *s = Item(At<char *>(slot, 0xc)); s != slot + 8; s = Next(s))
        FEngSetVisible(At<FEObject *>(s, 0xc));
    FEngSetVisible(At<FEObject *>(slot, 0x14));
}

void __fastcall SlotHide(char *slot, void *)
{
    if (At<char *>(slot, 0xc) == slot + 0xc)
        return;
    for (char *s = Item(At<char *>(slot, 0xc)); s != slot + 8; s = Next(s))
        FEngSetInvisible(At<FEObject *>(s, 0xc));
    FEngSetInvisible(At<FEObject *>(slot, 0x14));
}

void Scrollerina::HighlightSelected()
{
    if (char *slot = At<char *>(this, 0x2c))
        SlotSetScript(slot, nullptr, kHighlight);
}

void Scrollerina::AddData(ScrollerDatum *datum)
{
    char *s = reinterpret_cast<char *>(this);
    char *item = reinterpret_cast<char *>(datum);
    Append(s + 0x10, item ? item + 4 : nullptr);
    ++At<int>(s, 0x1c);
    if (!At<char *>(s, 0x28))
    {
        At<char *>(s, 0x28) = FirstDatum(s);
        At<int>(s, 0x20) = 1;
    }
    if (!At<char *>(s, 0x24))
        At<char *>(s, 0x24) = FirstDatum(s);
}

unsigned int __fastcall ListGetNodeIndex(char *s, void *, char *datum)
{
    unsigned int index = 1;
    for (char *d = FirstDatum(s); d != DataEnd(s); d = Next(d), ++index)
        if (d == datum)
            return index;
    return 0;
}

void __fastcall ListFindSize(char *s, void *)
{
    if (At<int>(s, 0x30))
        return;
    char *first = FirstSlot(s);
    float box[4] = {At<float>(first, 0x18), At<float>(first, 0x1c), 0, 0};
    box[3] = box[1] + At<float>(s, 0x9c);
    box[2] = box[0] + At<float>(s, 0x98);
    for (char *slot = first; slot != SlotsEnd(s); slot = Next(slot))
        Take(box, At<float>(slot, 0x18), At<float>(slot, 0x1c), At<float>(slot, 0x20), At<float>(slot, 0x24));
    if (FEObject *extra = At<FEObject *>(s, 0xb0))
    {
        float x, y, w, h;
        FEngGetTopLeft(extra, x, y);
        FEngGetSize(extra, w, h);
        Take(box, x, y, w, h);
    }
    At<float>(s, 0x90) = box[0];
    At<float>(s, 0x94) = box[1];
    At<float>(s, 0x98) = static_cast<float>(fabs(static_cast<double>(box[2]) - box[0]));
    At<float>(s, 0x9c) = static_cast<float>(fabs(static_cast<double>(box[3]) - box[1]));
}

void Scrollerina::AddSlot(ScrollerSlot *slot)
{
    char *s = reinterpret_cast<char *>(this);
    char *item = reinterpret_cast<char *>(slot);
    Append(s + 8, item ? item + 4 : nullptr);
    ++At<int>(s, 0x18);
    SlotFindSize(item, nullptr);
    ListFindSize(s, nullptr);
    if (!At<char *>(s, 0x2c))
        At<char *>(s, 0x2c) = FirstSlot(s);
}

ScrollerDatum *Scrollerina::FindDatumInSlot(ScrollerSlot *slot)
{
    char *s = reinterpret_cast<char *>(this);
    if (Empty(s) || !slot)
        return nullptr;
    char *datum = At<char *>(s, 0x28);
    for (char *at = FirstSlot(s); at != SlotsEnd(s); at = Next(at), datum = Next(datum))
    {
        if (at == reinterpret_cast<char *>(slot))
            return reinterpret_cast<ScrollerDatum *>(datum);
        if (datum == DataEnd(s))
            return nullptr;
    }
    return nullptr;
}

char *__fastcall ListFindSlotWithDatum(char *s, void *, char *datum)
{
    if (Empty(s) || !datum)
        return nullptr;
    char *d = At<char *>(s, 0x28);
    for (char *at = FirstSlot(s); at != SlotsEnd(s); at = Next(at), d = Next(d))
    {
        if (d == datum)
            return at;
        if (d == DataEnd(s))
            return nullptr;
    }
    return nullptr;
}

void Scrollerina::Update(bool print)
{
    char *s = reinterpret_cast<char *>(this);
    if (print)
        Virtual<void>(s, 4);
    UpdateBar(s);
}

void Scrollerina::Enable(ScrollerDatum *datum)
{
    char *s = reinterpret_cast<char *>(this);
    if (!datum || At<char *>(s, 8) == s + 8 || At<char *>(s, 0x10) == s + 0x10)
        return;
    At<bool>(datum, 0x14) = true;
    char *slot = ListFindSlotWithDatum(s, nullptr, reinterpret_cast<char *>(datum));
    if (slot && slot != At<char *>(s, 0x2c))
        At<bool>(slot, 0x28) = true;
}

void Scrollerina::SetSelected(ScrollerSlot *slot)
{
    char *s = reinterpret_cast<char *>(this);
    char *item = reinterpret_cast<char *>(slot);
    if (!item || !At<bool>(item, 0x28))
        return;
    ScrollerDatum *datum = FindDatumInSlot(slot);
    if (!datum)
        return;
    if (char *was = At<char *>(s, 0x2c))
        SlotSetScript(was, nullptr, kUnhighlight);
    At<char *>(s, 0x2c) = item;
    At<ScrollerDatum *>(s, 0x24) = datum;
    SlotSetScript(item, nullptr, kHighlight);
    Virtual<void>(s, 4);
    UpdateBar(s);
    At<bool>(s, 0xa1) = false;
}

/** Each slot shows the datum in its row from the first shown: its strings take the datum's texts in order (a text's
 * label, else the text itself), the strings left over when the texts run out keep what they had; a slot past the
 * data is hidden. */
void __fastcall ListPrint(char *s, void *)
{
    using Printf = int(__cdecl *)(FEString *, const char *, ...);
    const Printf printf = reinterpret_cast<Printf>(0x515d70);
    char *datum = At<char *>(s, 0x28);
    for (char *slot = FirstSlot(s); slot != SlotsEnd(s); slot = Next(slot))
    {
        if (!datum || datum == DataEnd(s))
        {
            SlotHide(slot, nullptr);
            continue;
        }
        char *text = Item(At<char *>(datum, 0xc));
        SlotShow(slot, nullptr);
        for (char *str = Item(At<char *>(slot, 0xc)); str != slot + 8; str = Next(str))
        {
            FEString *shown = At<FEString *>(str, 0xc);
            if (const unsigned int label = At<unsigned int>(text, 0x8c))
                FEngSetLanguageHash(shown, label);
            else
                printf(shown, "%s", text + 0xc);
            text = Next(text);
            if (text == datum + 8 && Next(str) != slot + 8)
                break;
        }
        datum = Next(datum);
    }
}
