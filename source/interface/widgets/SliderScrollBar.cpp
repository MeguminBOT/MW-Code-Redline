/**
 * The option sliders and the menus' scroll bars (original: zFe cSlider::cSlider 0x56c9f0, Update 0x56ca30, InitValues
 * 0x56cb20, Draw 0x56cb70, ToggleVisible 0x56ccb0, SetValue 0x56cd20, Highlight 0x56cd60, SetPos 0x56ce00,
 * InitObjects 0x57f390; zFe2 FEScrollBar::SetPosResized 0x5752d0, SetGroupVisible 0x589770, Update 0x589870, UpdateMouse 0x589900).
 *
 * cSlider (vtable +0x24 draw): +4 its base, +8 its fill bar, +0xc its handle, +0x10/+0x14 its caps, +0x18 its value
 * string, +0x1c the maximum, +0x20 the minimum, +0x24 the value before, +0x28 the value, +0x30 a step, +0x34 the
 * bar's full width. Left and right step it within its range; the fill bar is sized to the value and its texture cut
 * to match, the caps sit at its ends, the string shows the value as a whole number.
 * FEScrollBar: +0 vertical, +2 its handle dragged, +3 mouse off, +4 shown, +8/+0xc the mouse when grabbed,
 * +0x18/+0x1c the grab's offset from the handle, +0x58 the arrow clicked (-1, 1, 10001 none), +0x10/+0x14 the handle's place, +0x20/+0x24 the track's
 * start, +0x28/+0x2c its length, +0x38 one step, +0x40 the track, +0x44 the handle, +0x48/+0x4c the arrows,
 * +0x50/+0x54 two more parts. Shown while there are more rows than fit; the arrow at an end plays 0x9e99.
 */
#include "game/gen/api.h"
#include "legacy/link.h"
#include "interface/engine/mouse.h"
#include <math.h>

namespace
{
template <typename T> T &At(const void *p, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(p)) + offset);
}

const unsigned int kLeft = 0x9120409e;
const unsigned int kRight = 0xb5971bf1;
const unsigned int kFocused = 0x249db7b7;
const unsigned int kShown = 0x1ca7c0;
const unsigned int kHidden = 0x16a259;
const unsigned int kAtEnd = 0x9e99;
using Printf = int(__cdecl *)(FEString *, const char *, ...);
const Printf kFEPrintf = reinterpret_cast<Printf>(0x515d70);

void **Vtable(const void *o)
{
    return *static_cast<void **const *>(o);
}

template <typename R, typename... A> R Virtual(void *o, int offset, A... a)
{
    return reinterpret_cast<R(__thiscall *)(void *, A...)>(Vtable(o)[offset / 4])(o, a...);
}

FEObject *Obj(const void *o, int offset)
{
    return At<FEObject *>(o, offset);
}
} // namespace

bool __fastcall SliderUpdate(char *s, void *, unsigned int message);
void __fastcall SliderInitValues(char *s, void *, float minimum, float maximum, float step, float value, float width);
void __fastcall SliderDraw(char *s, void *);
void __fastcall SliderToggleVisible(char *s, void *, bool visible);
void __fastcall SliderHighlight(char *s, void *);
void __fastcall SliderSetPos(char *s, void *, float x, float y);
void __fastcall SliderInitObjects(char *s, void *, const char *package, const char *name);
void __fastcall ScrollBarSetPosResized(char *b, void *, int shown, int total, int at);
void __fastcall ScrollBarUpdateMouse(char *b, void *);
NFS_REPLACE(0x589900, ScrollBarUpdateMouse)
NFS_REPLACE(0x56ca30, SliderUpdate)
NFS_REPLACE(0x56cb20, SliderInitValues)
NFS_REPLACE(0x56cb70, SliderDraw)
NFS_REPLACE(0x56ccb0, SliderToggleVisible)
NFS_REPLACE(0x56cd60, SliderHighlight)
NFS_REPLACE(0x56ce00, SliderSetPos)
NFS_REPLACE(0x57f390, SliderInitObjects)
NFS_REPLACE(0x5752d0, ScrollBarSetPosResized)

bool __fastcall SliderUpdate(char *s, void *, unsigned int message)
{
    bool changed = false;
    if (message == kLeft || message == kRight)
    {
        const float was = At<float>(s, 0x28);
        At<float>(s, 0x24) = was;
        const float end = message == kLeft ? At<float>(s, 0x20) : At<float>(s, 0x1c);
        double value = message == kLeft ? static_cast<double>(was) - At<float>(s, 0x30)
                                        : static_cast<double>(At<float>(s, 0x30)) + was;
        if (message == kLeft ? end > value : end < value)
            value = end;
        At<float>(s, 0x28) = static_cast<float>(value);
        changed = value != end || At<float>(s, 0x24) != end;
    }
    Virtual<void>(s, 0x24);
    return changed;
}

void __fastcall SliderInitValues(char *s, void *, float minimum, float maximum, float step, float value, float width)
{
    At<float>(s, 0x1c) = maximum;
    At<float>(s, 0x20) = minimum;
    float v = minimum < value ? value : minimum;
    if (maximum <= v)
        v = maximum;
    At<float>(s, 0x28) = v;
    At<float>(s, 0x2c) = v;
    At<float>(s, 0x30) = step;
    At<float>(s, 0x34) = width;
}

void __fastcall SliderDraw(char *s, void *)
{
    double range = static_cast<double>(At<float>(s, 0x1c)) - At<float>(s, 0x20);
    if (range == 0.0)
    {
        At<float>(s, 0x1c) = 1.0f;
        range = 1.0;
        At<float>(s, 0x20) = 0.0f;
    }
    FEObject *fill = Obj(s, 8);
    const float width =
        static_cast<float>((static_cast<double>(At<float>(s, 0x28)) - At<float>(s, 0x20)) / range * At<float>(s, 0x34));
    float w, h;
    FEngGetSize(fill, w, h);
    FEngSetSize(fill, width, h);
    float x, y, unused;
    FEngGetTopLeft(Obj(s, 4), x, y);
    FEngGetTopLeft(Obj(s, 4), unused, y);
    const float top = y - 12.0f;
    FEngSetTopLeft(fill, x + 2.0f, top);
    float u, v;
    FEngGetBottomRightUV(reinterpret_cast<FEImage *>(fill), u, v);
    FEngSetBottomRightUV(reinterpret_cast<FEImage *>(fill), width, v);
    FEngSetTopLeft(Obj(s, 0x10), x - 1.5f, top);
    FEngSetTopLeft(Obj(s, 0x14), static_cast<float>(static_cast<double>(x) + width - 7.0), top);
    if (FEString *value = At<FEString *>(s, 0x18))
        kFEPrintf(value, "%d", static_cast<int>(At<float>(s, 0x28)));
}

void __fastcall SliderToggleVisible(char *s, void *, bool visible)
{
    if (visible)
    {
        FEngSetVisible(Obj(s, 4));
        FEngSetVisible(Obj(s, 8));
        FEngSetVisible(Obj(s, 0x18));
        FEngSetVisible(Obj(s, 0xc));
        return;
    }
    FEngSetInvisible(Obj(s, 0x18));
    FEngSetInvisible(Obj(s, 4));
    FEngSetInvisible(Obj(s, 8));
    FEngSetInvisible(Obj(s, 0xc));
}

void cSlider::SetValue(float value)
{
    char *s = reinterpret_cast<char *>(this);
    At<float>(s, 0x24) = At<float>(s, 0x28);
    const float v = At<float>(s, 0x20) < value ? value : At<float>(s, 0x20);
    At<float>(s, 0x28) = v < At<float>(s, 0x1c) ? v : At<float>(s, 0x1c);
}

void __fastcall SliderHighlight(char *s, void *)
{
    FEngSetScript(Obj(s, 4), kFocused, true);
    FEngSetScript(Obj(s, 8), kFocused, true);
    FEngSetScript(Obj(s, 0x18), kFocused, true);
    FEngSetScript(Obj(s, 0xc), kFocused, true);
}

void __fastcall SliderSetPos(char *s, void *, float x, float y)
{
    FEngSetTopLeft(Obj(s, 4), x, y);
    FEngSetTopLeft(Obj(s, 8), x, y);
}

void __fastcall SliderInitObjects(char *s, void *, const char *package, const char *name)
{
    if (!package || !name)
        return;
    At<FEImage *>(s, 4) = FEngFindImage(package, static_cast<int>(FEngHashString("BASE_%s", name)));
    At<FEImage *>(s, 8) = FEngFindImage(package, static_cast<int>(FEngHashString("FILLBAR_%s", name)));
    At<FEImage *>(s, 0xc) = FEngFindImage(package, static_cast<int>(FEngHashString("HANDLE_%s", name)));
    At<FEImage *>(s, 0x10) = FEngFindImage(package, static_cast<int>(FEngHashString("LEFT_CAP_%s", name)));
    At<FEImage *>(s, 0x14) = FEngFindImage(package, static_cast<int>(FEngHashString("RIGHT_CAP_%s", name)));
    At<FEString *>(s, 0x18) = FEngFindString(package, static_cast<int>(FEngHashString("VALUE_%s", name)));
}

void FEScrollBar::SetGroupVisible(bool visible)
{
    char *b = reinterpret_cast<char *>(this);
    At<bool>(b, 4) = visible;
    const int parts = visible ? 4 : 6;
    for (int i = 0; i < parts; ++i)
    {
        FEObject *o = Obj(b, 0x40 + i * 4);
        if (visible)
            FEngSetVisible(o);
        else
            FEngSetInvisible(o);
        FEngSetScript(o, visible ? kShown : kHidden, true);
    }
}

void FEScrollBar::Update(int shown, int total, int top, int at)
{
    if (at == -1)
        at = top;
    if (shown < total && total)
    {
        SetGroupVisible(true);
        ScrollBarSetPosResized(reinterpret_cast<char *>(this), nullptr, shown, total, top);
        if (at == 1)
            FEngSetScript(Obj(this, 0x48), kAtEnd, true);
        else if (at == total)
            FEngSetScript(Obj(this, 0x4c), kAtEnd, true);
        return;
    }
    SetGroupVisible(false);
}

void __fastcall ScrollBarSetPosResized(char *b, void *, int shown, int total, int at)
{
    const float rows = static_cast<float>(shown);
    const float all = static_cast<float>(total);
    const double share = static_cast<double>(rows) / all;
    FEObject *handle = Obj(b, 0x44);
    const bool vertical = At<bool>(b, 0);
    const float length = vertical ? At<float>(b, 0x2c) : At<float>(b, 0x28);
    const float size = static_cast<float>(share * length);
    float w, h;
    FEngGetSize(handle, w, h);
    if (vertical)
        FEngSetSize(handle, w, size);
    else
        FEngSetSize(handle, size, h);
    double steps = static_cast<double>(all) - rows;
    if (!(1.0 <= steps))
        steps = 1.0;
    const double step = (static_cast<double>(length) - size) / steps;
    At<float>(b, 0x38) = static_cast<float>(step);
    const double place = step * (static_cast<double>(at) - 1.0) + (vertical ? At<float>(b, 0x24) : At<float>(b, 0x20));
    At<float>(b, vertical ? 0x14 : 0x10) = static_cast<float>(place);
    if (At<bool>(b, 2))
        return;
    float x, y;
    FEngGetTopLeft(Obj(b, 0x44), x, y);
    if (vertical)
        FEngSetTopLeft(Obj(b, 0x44), x, static_cast<float>(place));
    else
        FEngSetTopLeft(Obj(b, 0x44), static_cast<float>(place), y);
}

void __fastcall ScrollBarUpdateMouse(char *b, void *)
{
    FEScrollBar *bar = reinterpret_cast<FEScrollBar *>(b);
    At<int>(b, 0x58) = 10001;
    if (!At<bool>(b, 3))
    {
        if (FEngMousePressedOver(Obj(b, 0x44)))
        {
            At<bool>(b, 2) = true;
            FEngGetMousePos(&At<float>(b, 8), &At<float>(b, 0xc));
            float x, y;
            FEngGetTopLeft(Obj(b, 0x44), x, y);
            At<float>(b, 0x18) = fabsf(x - At<float>(b, 8));
            FEngGetTopLeft(Obj(b, 0x44), x, y);
            At<float>(b, 0x1c) = fabsf(y - At<float>(b, 0xc));
        }
        else if (FEngMousePressedOver(Obj(b, 0x40)))
            bar->UpdateBackingMouse();
        if (FEngMouseReleased())
            At<bool>(b, 2) = false;
    }
    if (At<bool>(b, 2))
    {
        bar->UpdateHandleMouse();
        return;
    }
    if (FEngMouseReleasedOver(Obj(b, 0x48)))
    {
        At<int>(b, 0x58) = -1;
        return;
    }
    if (FEngMouseReleasedOver(Obj(b, 0x4c)))
        At<int>(b, 0x58) = 1;
}
