/**
 * The option list's rows: a label and value (FEStatWidget), with arrows (FEToggleWidget) or a slider bar
 * (FESliderWidget) (original: zFe2 FEStatWidget::Show 0x574420, SetPosY 0x5744e0, Act 0x5891a0, Position 0x589200;
 * FEToggleWidget::SetScript 0x574550, Show 0x5745b0, Hide 0x5745f0, SetFocus 0x574630, UnsetFocus 0x574660,
 * BlinkArrows 0x574670, Position 0x589390; FESliderWidget::Position 0x574950, Show 0x574aa0, SetFocus 0x574b30,
 * UpdateSlider 0x574bc0).
 *
 * A row (vtable +0x38 set its value): +0xc/+0x10 its place, +0x1c/+0x20 its backing's offset, +0x24 the backing,
 * +0x2a changed, +0x2c the label string (+0x6c its format: 1 centred, 2 right aligned), +0x30 the value string,
 * +0x34/+0x38 the label's size, +0x3c/+0x40 the value's, +0x44/+0x48 the value's place, +0x4c/+0x50 the left and
 * right arrows, +0x5c the slider (cSlider: vtable +4 take a key, +0x14 show, +0x18 highlight, +0x20 place, +0x24
 * draw), +0x60 its bar, +0x98 its height offset. Focus plays 0x249db7b7 on the row, losing it 0x7ab5521a.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
template <typename T> T &At(const void *p, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(p)) + offset);
}

const unsigned int kFocused = 0x249db7b7;
const unsigned int kUnfocused = 0x7ab5521a;

void **Vtable(const void *o)
{
    return *static_cast<void **const *>(o);
}

template <typename R, typename... A> R Virtual(void *o, int offset, A... a)
{
    return reinterpret_cast<R(__thiscall *)(void *, A...)>(Vtable(o)[offset / 4])(o, a...);
}

FEObject *Obj(const char *w, int offset)
{
    return At<FEObject *>(w, offset);
}

/** The label's x: its left edge, middle or right edge on the row as its format aligns it. */
float LabelX(const char *w)
{
    const unsigned int format = At<unsigned int>(Obj(w, 0x2c), 0x6c);
    if (format & 1)
        return At<float>(w, 0xc);
    const float width = (format & 2) ? At<float>(w, 0x34) : At<float>(w, 0x34) * 0.5f;
    return width + At<float>(w, 0xc);
}

float RowY(const char *w)
{
    return At<float>(w, 0x38) * 0.5f + At<float>(w, 0x10);
}

void PlaceBacking(const char *w)
{
    if (FEObject *backing = Obj(w, 0x24))
        FEngSetTopLeft(backing, At<float>(w, 0xc) - At<float>(w, 0x1c), At<float>(w, 0x10) - At<float>(w, 0x20));
}

void PlaceArrows(const char *w)
{
    const float y = At<float>(w, 0x38) * 0.5f + At<float>(w, 0x48);
    FEngSetCenter(Obj(w, 0x4c), At<float>(w, 0x44), y);
    FEngSetCenter(Obj(w, 0x50), At<float>(w, 0x44) + At<float>(w, 0x3c), y);
}

void ForParts(const char *w, void (*f)(FEObject *), bool arrows)
{
    f(Obj(w, 0x2c));
    f(Obj(w, 0x30));
    if (arrows)
    {
        f(Obj(w, 0x4c));
        f(Obj(w, 0x50));
    }
    if (FEObject *backing = Obj(w, 0x24))
        f(backing);
}

void ToggleScript(char *w, unsigned int script)
{
    reinterpret_cast<FEToggleWidget *>(w)->SetScript(script);
}
} // namespace

void __fastcall StatShow(char *w, void *);
void __fastcall StatSetPosY(char *w, void *, float y);
void __fastcall StatAct(char *w, void *, const char *package, unsigned int message);
void __fastcall StatPosition(char *w, void *);
void __fastcall ToggleShow(char *w, void *);
void __fastcall ToggleHide(char *w, void *);
void __fastcall ToggleSetFocus(char *w, void *, const char *package);
void __fastcall ToggleUnsetFocus(char *w, void *);
void __fastcall ToggleBlinkArrows(char *w, void *, unsigned int message);
void __fastcall TogglePosition(char *w, void *);
void __fastcall SliderPosition(char *w, void *);
void __fastcall SliderShow(char *w, void *);
void __fastcall SliderSetFocus(char *w, void *, const char *package);
NFS_REPLACE(0x574420, StatShow)
NFS_REPLACE(0x5744e0, StatSetPosY)
NFS_REPLACE(0x5891a0, StatAct)
NFS_REPLACE(0x589200, StatPosition)
NFS_REPLACE(0x5745b0, ToggleShow)
NFS_REPLACE(0x5745f0, ToggleHide)
NFS_REPLACE(0x574630, ToggleSetFocus)
NFS_REPLACE(0x574660, ToggleUnsetFocus)
NFS_REPLACE(0x574670, ToggleBlinkArrows)
NFS_REPLACE(0x589390, TogglePosition)
NFS_REPLACE(0x574950, SliderPosition)
NFS_REPLACE(0x574aa0, SliderShow)
NFS_REPLACE(0x574b30, SliderSetFocus)

void __fastcall StatShow(char *w, void *)
{
    FEngSetVisible(Obj(w, 0x2c));
    FEngSetVisible(Obj(w, 0x30));
    if (FEObject *backing = Obj(w, 0x24))
        FEngSetVisible(backing);
}

void __fastcall StatSetPosY(char *w, void *, float y)
{
    const float was = At<float>(w, 0x10);
    At<float>(w, 0x10) = y;
    At<float>(w, 0x48) = static_cast<float>(static_cast<double>(At<float>(w, 0x48)) - was + y);
    if (FEObject *backing = Obj(w, 0x24))
    {
        const float offset = At<float>(w, 0x20);
        float x, top;
        FEngGetTopLeft(backing, x, top);
        FEngSetTopLeft(backing, x, y - offset);
    }
}

void __fastcall StatAct(char *, void *, const char *, unsigned int)
{
}

void __fastcall StatPosition(char *w, void *)
{
    const float y = RowY(w);
    if (Obj(w, 0x2c))
        FEngSetCenter(Obj(w, 0x2c), LabelX(w), y);
    if (FEObject *value = Obj(w, 0x30))
    {
        const unsigned int format = At<unsigned int>(value, 0x6c);
        if (format & 1)
            FEngSetCenter(value, At<float>(w, 0x3c) * 0.5f + At<float>(w, 0x44), y);
        else if (format & 2)
        {
            FEngSetCenterY(value, y);
            const char *data = reinterpret_cast<const char *>(Obj(w, 0x30)->pData);
            const float position[3] = {At<float>(w, 0x44) + At<float>(w, 0x3c), At<float>(data, 0x20),
                                       At<float>(data, 0x24)};
            Obj(w, 0x30)->SetPosition(*reinterpret_cast<const FEVector3 *>(position), false);
        }
        else
            FEngSetCenter(value, At<float>(w, 0x44), y);
    }
    PlaceBacking(w);
}

void FEToggleWidget::SetScript(unsigned int script)
{
    const char *w = reinterpret_cast<const char *>(this);
    FEngSetScript(Obj(w, 0x2c), script, true);
    FEngSetScript(Obj(w, 0x30), script, true);
    FEngSetScript(Obj(w, 0x4c), script, true);
    FEngSetScript(Obj(w, 0x50), script, true);
    if (FEObject *backing = Obj(w, 0x24))
        FEngSetScript(backing, script, true);
}

void __fastcall ToggleShow(char *w, void *)
{
    ForParts(w, &FEngSetVisible, true);
}

void __fastcall ToggleHide(char *w, void *)
{
    ForParts(w, &FEngSetInvisible, true);
}

void __fastcall ToggleSetFocus(char *w, void *, const char *package)
{
    FEngSetCurrentButton(package, Obj(w, 0x2c)->NameHash);
    ToggleScript(w, kFocused);
}

void __fastcall ToggleUnsetFocus(char *w, void *)
{
    ToggleScript(w, kUnfocused);
}

void __fastcall ToggleBlinkArrows(char *, void *, unsigned int)
{
}

void __fastcall TogglePosition(char *w, void *)
{
    StatPosition(w, nullptr);
    PlaceArrows(w);
}

void __fastcall SliderPosition(char *w, void *)
{
    FEngSetCenter(Obj(w, 0x2c), LabelX(w), RowY(w));
    float barWidth, barHeight;
    FEngGetSize(Obj(w, 0x60), barWidth, barHeight);
    void *slider = w + 0x5c;
    const float x = static_cast<float>(static_cast<double>(At<float>(w, 0x3c)) * 0.5 + At<float>(w, 0x44) -
                                       static_cast<double>(barWidth) * 0.5);
    Virtual<void>(slider, 0x20, x, At<float>(w, 0x48) + At<float>(w, 0x98));
    Virtual<void>(slider, 0x24);
    PlaceArrows(w);
    PlaceBacking(w);
}

void __fastcall SliderShow(char *w, void *)
{
    FEngSetVisible(Obj(w, 0x2c));
    FEngSetVisible(Obj(w, 0x4c));
    FEngSetVisible(Obj(w, 0x50));
    Virtual<void>(w + 0x5c, 0x14, true);
    if (FEObject *backing = Obj(w, 0x24))
        FEngSetVisible(backing);
}

void __fastcall SliderSetFocus(char *w, void *, const char *package)
{
    FEngSetCurrentButton(package, Obj(w, 0x2c)->NameHash);
    FEngSetScript(Obj(w, 0x2c), kFocused, true);
    Virtual<void>(w + 0x5c, 0x18);
    if (FEObject *backing = Obj(w, 0x24))
        FEngSetScript(backing, kFocused, true);
}

void FESliderWidget::UpdateSlider(unsigned int message)
{
    char *w = reinterpret_cast<char *>(this);
    if (Virtual<bool>(w + 0x5c, 4, message))
    {
        Virtual<void>(w, 0x38, message);
        At<bool>(w, 0x2a) = true;
    }
    else
        At<bool>(w, 0x2a) = false;
}
