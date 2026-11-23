/**
 * The icon menus' scroller and its screen: moving between options, the header, and the screen's messages (original:
 * zFe IconPanel::GetCurrentIndex 0x50e780, ScrollNext 0x566f10, ScrollPrev 0x566f30, Act 0x5732f0;
 * IconScrollerMenu::SetInitialOption 0x50e7f0, NotifySoundMessage 0x573810, StorePrevNotification 0x573840,
 * RefreshHeader 0x573870, NotificationMessage 0x587d30; IconScroller::ClipEdges 0x5733b0, Scale 0x573410, UpdateFade 0x5734e0, PositionOption 0x587b60, GetOptionIndex 0x587980,
 * Scroll 0x5879f0, UpdateArrows 0x593000, IsHead 0x59ba80, IsTail 0x59bab0, Update 0x59bb00, AddOption 0x5877a0,
 * SetSelection 0x5878c0, AddInitialBookEnds 0x592bc0, SetInitialPos 0x592c90).
 *
 * IconPanel (vtable: +4 update, +0x10 act, +0x18 is head, +0x1c is tail, +0x24 option index, +0x28 select, +0x2c
 * mouse, +0x34 scroll, +0x38 scroll the other way, +0x40 set the initial option): its options in a list at +4 (an
 * option's node 4 bytes in, next first), +0xc the current option, +0x10 the scroll region, +0x18 the package's name,
 * +0x24 the options' count, +0x28 scrolls by the other call, +0x29 horizontal (left and right scroll; else up and down), +0x2a moved, +0x2b takes input. IconScroller adds +0x2c the first
 * real option and +0x30 the last bookend, +0x1c the options' name stem, +0x20 their spacing, +0x34 the scroll bar,
 * +0x7c/+0x80 the arrows, +0x90 the scroll position (a tCubic1D), +0xe0 the next option's offset, +0xe4/+0xe8 the
 * fade's step and length, +0xed/+0xee fading in/out, +0xef placed, +0xf0 frozen, +0xc0 the bookends' count, +0xc8 the selected index,
 * +0xcc its width, +0xd4 its middle. IconOption: +0x10 its object, +0x20 disabled, +0x2c/+0x30 its name and
 * description, +0x45 acts itself, +0x46/+0x48 a tutorial movie. IconScrollerMenu: a MenuScreen (+0x10 the package's
 * name) with its IconScroller at +0x2c, +0x118 a flag set by 0x35f8620b, +0x110..+0x11a the initial option, +0x128 the
 * mouse button went down on the option, +0x12c/+0x130/+0x134 the header's strings, +0x138 a stored message to act on.
 */
#include "game/gen/api.h"
#include "legacy/link.h"
#include <new>

namespace
{
template <typename T> T &At(const void *p, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(p)) + offset);
}

const unsigned int kAccept = 0x0c407210;
const unsigned int kAcceptUp = 0x936a6a7f;
const unsigned int kUp = 0x72619778;
const unsigned int kDown = 0x911c0a4b;
const unsigned int kLeft = 0x9120409e;
const unsigned int kRight = 0xb5971bf1;
const unsigned int kMouse = 0x9803f6e2;
const unsigned int kTick = 0xc98356ba;
const unsigned int kActStored = 0xe1fde1d1;
const unsigned int kStoreAndWait = 0x911ab364;
const unsigned int kResetInitial = 0x84378bef;
const unsigned int kFlag118 = 0x35f8620b;
const unsigned int kTutorial = 0xc519bfc3;
const unsigned int kMovieDone = 0xc3960eb9;
const unsigned int kChosen = 0x587c018b;
const unsigned int kAtHead = 0xd7118934;
const unsigned int kAtTail = 0xb9b17747;
const unsigned int kScreenScript = 0x99344537;
const unsigned int kArrowScript = 0x9c7d33ff;
const unsigned int kShown = 0x1ca7c0;
const unsigned int kHidden = 0x16a259;
const unsigned int kInit = 0x1744b3;
const unsigned int kSkipSoundA = 0x48122792;
const unsigned int kSkipSoundB = 0x4ac5e165;
const unsigned int kBookEnd = 0x43b6310f;
const unsigned char *const kMouseLeft = reinterpret_cast<const unsigned char *>(0x987f70);
char *const *const kFEDatabase = reinterpret_cast<char *const *>(0x91cf90);
EAXSound *const *const kSound = reinterpret_cast<EAXSound *const *>(0x911fa8);
using GetMousePos = void(__cdecl *)(float *, float *);
const GetMousePos kGetMousePos = reinterpret_cast<GetMousePos>(0x515960);
using Traverse = int(__thiscall *)(void *, void *);
const Traverse kTraverse = reinterpret_cast<Traverse>(0x45d3a0);

void **Vtable(const void *o)
{
    return *static_cast<void **const *>(o);
}

template <typename R, typename... A> R Virtual(void *o, int offset, A... a)
{
    return reinterpret_cast<R(__thiscall *)(void *, A...)>(Vtable(o)[offset / 4])(o, a...);
}

/** The option whose list node is `node` (nodes are 4 bytes into their options). */
char *OptionOf(char *node)
{
    return node ? node - 4 : nullptr;
}

char *Next(char *option)
{
    return OptionOf(At<char *>(option, 4));
}

char *Previous(char *option)
{
    return OptionOf(At<char *>(option, 8));
}

void QueueToPackage(unsigned int message, const char *package, FEObject *target)
{
    cFEng::mInstance->QueuePackageMessage(message, package, target);
}
} // namespace

void __fastcall IconPanelAct(char *panel, void *, unsigned int message, FEObject *o, unsigned int a, unsigned int b);
void __fastcall IconScrollerClipEdges(char *scroller, void *, char *option, float x);
int __fastcall IconScrollerGetOptionIndex(char *scroller, void *, char *option);
void __fastcall IconScrollerScroll(char *scroller, void *, int direction);
void __fastcall IconScrollerUpdateArrows(char *scroller, void *);
bool __fastcall IconScrollerIsHead(char *scroller, void *, char *option);
bool __fastcall IconScrollerIsTail(char *scroller, void *, char *option);
int __fastcall IconMenuNotifySound(char *menu, void *, unsigned int message, int sound);
void __fastcall IconMenuRefreshHeader(char *menu, void *);
void __fastcall IconMenuNotification(char *menu, void *, unsigned int message, FEObject *o, unsigned int a,
                                     unsigned int b);
NFS_REPLACE(0x5732f0, IconPanelAct)
NFS_REPLACE(0x5733b0, IconScrollerClipEdges)
float __fastcall IconScrollerScale(char *scroller, void *, float x, float middle, float width, float size);
void __fastcall IconScrollerUpdateFade(char *scroller, void *, char *option, float scale);
void __fastcall IconScrollerPositionOption(char *scroller, void *, char *option);
NFS_REPLACE(0x573410, IconScrollerScale)
NFS_REPLACE(0x5734e0, IconScrollerUpdateFade)
NFS_REPLACE(0x587b60, IconScrollerPositionOption)
NFS_REPLACE(0x587980, IconScrollerGetOptionIndex)
NFS_REPLACE(0x5879f0, IconScrollerScroll)
NFS_REPLACE(0x593000, IconScrollerUpdateArrows)
NFS_REPLACE(0x59ba80, IconScrollerIsHead)
NFS_REPLACE(0x59bab0, IconScrollerIsTail)
void __fastcall IconScrollerUpdate(char *scroller, void *);
FEImage *__fastcall IconScrollerAddOption(char *scroller, void *, char *option);
bool __fastcall IconScrollerSetSelection(char *scroller, void *, char *option);
void __fastcall IconScrollerAddInitialBookEnds(char *scroller, void *);
void __fastcall IconScrollerSetInitialPos(char *scroller, void *, int position);
NFS_REPLACE(0x59bb00, IconScrollerUpdate)
NFS_REPLACE(0x5877a0, IconScrollerAddOption)
NFS_REPLACE(0x5878c0, IconScrollerSetSelection)
NFS_REPLACE(0x592bc0, IconScrollerAddInitialBookEnds)
NFS_REPLACE(0x592c90, IconScrollerSetInitialPos)
NFS_REPLACE(0x573810, IconMenuNotifySound)
NFS_REPLACE(0x573870, IconMenuRefreshHeader)
NFS_REPLACE(0x587d30, IconMenuNotification)

int IconPanel::GetCurrentIndex()
{
    void *current = At<void *>(this, 0xc);
    return current ? Virtual<int>(this, 0x24, current) : 0;
}

void IconPanel::ScrollNext()
{
    if (At<bool>(this, 0x28))
        Virtual<void>(this, 0x38, 1);
    else
        Virtual<void>(this, 0x34, 1);
}

void IconPanel::ScrollPrev()
{
    if (At<bool>(this, 0x28))
        Virtual<void>(this, 0x38, -1);
    else
        Virtual<void>(this, 0x34, -1);
}

void __fastcall IconPanelAct(char *panel, void *, unsigned int message, FEObject *o, unsigned int a, unsigned int b)
{
    char *current = At<char *>(panel, 0xc);
    if (current && o == At<FEObject *>(current, 0x10))
        Virtual<void>(current, 4, At<const char *>(panel, 0x18), message, o, a, b);
}

void __fastcall IconScrollerClipEdges(char *scroller, void *, char *option, float x)
{
    const float half = At<float>(scroller, 0xcc) * 0.5f;
    const float middle = At<float>(scroller, 0xd4);
    if (middle - half <= x && x <= half + middle)
        FEngSetVisible(At<FEObject *>(option, 0x10));
    else
        FEngSetInvisible(At<FEObject *>(option, 0x10));
}

int __fastcall IconScrollerGetOptionIndex(char *scroller, void *, char *option)
{
    if (!option)
        return -1;
    int index = 1;
    for (char *o = OptionOf(At<char *>(scroller, 4)); o != scroller; o = Next(o), ++index)
        if (o == option)
            return index - At<int>(scroller, 0xc0) / 2;
    return -1;
}

void __fastcall IconScrollerScroll(char *scroller, void *, int direction)
{
    if (kTraverse(scroller + 4, nullptr) - At<int>(scroller, 0xc0) < 1)
        return;
    char *current = At<char *>(scroller, 0xc);
    char *to = current;
    bool found = false;
    if (direction == -1)
    {
        char *limit = Next(At<char *>(scroller, 0x2c));
        while (to != limit)
        {
            to = Previous(to);
            if (!At<bool>(to, 0x20))
            {
                found = true;
                break;
            }
        }
    }
    else if (direction == 1)
    {
        char *limit = Previous(At<char *>(scroller, 0x30));
        while (to != limit)
        {
            to = Next(to);
            if (!At<bool>(to, 0x20))
            {
                found = true;
                break;
            }
        }
    }
    if (!found && At<bool>(to, 0x20))
        return;
    if (to != current)
    {
        Virtual<bool>(scroller, 0x28, to);
        At<bool>(scroller, 0x2a) = true;
    }
}

void __fastcall IconScrollerUpdateArrows(char *scroller, void *)
{
    char *current = At<char *>(scroller, 0xc);
    FEObject *left = At<FEObject *>(scroller, 0x7c);
    FEObject *right = At<FEObject *>(scroller, 0x80);
    FEObject *hide;
    if (current == OptionOf(At<char *>(scroller, 4)))
        hide = left;
    else if (current == OptionOf(At<char *>(scroller, 8)))
        hide = right;
    else
    {
        FEngSetVisible(left);
        FEngSetScript(left, kShown, true);
        FEngSetVisible(right);
        FEngSetScript(right, kShown, true);
        return;
    }
    FEngSetInvisible(hide);
    FEngSetScript(hide, kHidden, true);
}

bool __fastcall IconScrollerIsHead(char *scroller, void *, char *option)
{
    return option == Next(At<char *>(scroller, 0x2c));
}

bool __fastcall IconScrollerIsTail(char *scroller, void *, char *option)
{
    return option == Previous(At<char *>(scroller, 0x30));
}

void IconScrollerMenu::SetInitialOption(int option)
{
    char *self = reinterpret_cast<char *>(this);
    if (At<bool>(self, 0x129))
    {
        At<bool>(self, 0x11c) = false;
        At<bool>(self, 0x119) = true;
        At<bool>(self, 0x11a) = false;
        At<int>(self, 0x110) = 0;
    }
    Virtual<void>(self + 0x2c, 0x40, option);
}

int __fastcall IconMenuNotifySound(char *menu, void *, unsigned int message, int sound)
{
    if ((message == kSkipSoundA || message == kSkipSoundB) && !At<bool>(menu, 0x56))
        return -1;
    return sound;
}

void IconScrollerMenu::StorePrevNotification(unsigned int message, FEObject *o, unsigned int a, unsigned int b)
{
    char *self = reinterpret_cast<char *>(this);
    At<unsigned int>(self, 0x138) = message;
    At<FEObject *>(self, 0x13c) = o;
    At<unsigned int>(self, 0x140) = a;
    At<unsigned int>(self, 0x144) = b;
}

void __fastcall IconMenuRefreshHeader(char *menu, void *)
{
    char *panel = menu + 0x2c;
    char *current = At<char *>(menu, 0x38);
    FEngSetLanguageHash(At<FEString *>(menu, 0x12c), current ? At<unsigned int>(current, 0x2c) : 0);
    FEngSetLanguageHash(At<FEString *>(menu, 0x130), current ? At<unsigned int>(current, 0x2c) : 0);
    FEngSetLanguageHash(At<FEString *>(menu, 0x134), current ? At<unsigned int>(current, 0x30) : 0);
    const char *package = At<const char *>(menu, 0x10);
    if (Virtual<bool>(panel, 0x18, At<char *>(panel, 0xc)))
        QueueToPackage(kAtHead, package, nullptr);
    if (Virtual<bool>(panel, 0x1c, At<char *>(panel, 0xc)))
        QueueToPackage(kAtTail, package, nullptr);
    FEngSetScript(package, kArrowScript, At<bool>(At<char *>(menu, 0x38), 0x46) ? kShown : kHidden, true);
}

void __fastcall IconMenuNotification(char *menu, void *, unsigned int message, FEObject *o, unsigned int a,
                                     unsigned int b)
{
    IconScrollerMenu *self = reinterpret_cast<IconScrollerMenu *>(menu);
    IconPanel *panel = reinterpret_cast<IconPanel *>(menu + 0x2c);
    const char *package = At<const char *>(menu, 0x10);
    const bool horizontal = At<bool>(menu, 0x55);
    const bool input = At<bool>(menu, 0x57);
    char *current = At<char *>(menu, 0x38);
    switch (message)
    {
    case kLeft:
        if (!horizontal || !input)
            return;
        if (At<bool>(menu, 0x54))
            Virtual<void>(panel, 0x38, -1);
        else
            Virtual<void>(panel, 0x34, -1);
        Virtual<void>(menu, 0xc);
        return;
    case kUp:
        if (horizontal || !input)
            return;
        if (!Virtual<bool>(panel, 0x18, current))
            panel->ScrollPrev();
        Virtual<void>(menu, 0xc);
        return;
    case kDown:
        if (horizontal || !input)
            return;
        panel->ScrollNext();
        Virtual<void>(menu, 0xc);
        return;
    case kRight:
        if (!horizontal || !input)
            return;
        panel->ScrollNext();
        Virtual<void>(menu, 0xc);
        return;
    case kAccept:
    {
        if (!input || At<bool>(current, 0x20) || o != At<FEObject *>(current, 0x10))
            return;
        FEngSetLastButton(package, static_cast<unsigned char>(panel->GetCurrentIndex()));
        char *chosen = At<char *>(panel, 0xc);
        if (chosen && At<bool>(chosen, 0x45))
        {
            Virtual<void>(panel, 0x10, kAccept, o, a, b);
            return;
        }
        self->StorePrevNotification(kAccept, o, a, b);
        At<bool>(menu, 0x57) = false;
        QueueToPackage(kChosen, package, nullptr);
        return;
    }
    case kFlag118:
        At<bool>(menu, 0x118) = true;
        return;
    case kResetInitial:
        At<bool>(menu, 0x119) = false;
        At<bool>(menu, 0x11a) = true;
        At<int>(menu, 0x110) = At<int>(menu, 0x114);
        return;
    case kStoreAndWait:
        At<unsigned int>(menu, 0x13c) = reinterpret_cast<unsigned int>(o);
        At<unsigned int>(menu, 0x138) = kStoreAndWait;
        At<unsigned int>(menu, 0x140) = a;
        At<unsigned int>(menu, 0x144) = b;
        At<bool>(menu, 0x57) = false;
        FEngSetLastButton(package, 0);
        return;
    case kMovieDone:
        FEngSetScript(package, kScreenScript, kInit, true);
        return;
    case kMouse:
    {
        float x, y;
        kGetMousePos(&x, &y);
        if (FEngTestForIntersection(x, y, At<FEObject *>(menu, 0x3c)))
        {
            if (*kMouseLeft)
            {
                QueueToPackage(kAccept, package, At<FEObject *>(At<char *>(menu, 0x38), 0x10));
                At<bool>(menu, 0x128) = true;
            }
            else if (At<bool>(menu, 0x128))
                QueueToPackage(kAcceptUp, package, At<FEObject *>(At<char *>(menu, 0x38), 0x10));
        }
        if (Virtual<bool>(panel, 0x2c))
            Virtual<void>(menu, 0xc);
        return;
    }
    case kTutorial:
    {
        if (!At<bool>(current, 0x46))
            return;
        FEngSetScript(package, kScreenScript, kHidden, true);
        (*kSound)->PlayUISoundFX(static_cast<eMenuSoundTriggers>(4));
        FEAnyTutorialScreen::LaunchMovie(At<const char *>(current, 0x48), package);
        unsigned int &seen = At<unsigned int>(At<char *>(*kFEDatabase, 0x10), 0xac);
        switch (At<unsigned int>(current, 0x2c))
        {
        case 0x6f547e4c:
            seen |= 0x40;
            break;
        case 0xa15e4505:
            seen |= 0x100;
            break;
        case 0xee1edc76:
            seen |= 0x80;
            break;
        }
        return;
    }
    case kTick:
        Virtual<void>(panel, 4);
        return;
    case kActStored:
        Virtual<void>(panel, 0x10, At<unsigned int>(menu, 0x138), At<FEObject *>(menu, 0x13c),
                      At<unsigned int>(menu, 0x140), At<unsigned int>(menu, 0x144));
        return;
    }
}

/** How much of an option shows at `x` (0 to 1): nothing outside the scroller's width around its middle, all of it within
 * 1.5 of the middle, in between a straight ramp over half the width. */
float __fastcall IconScrollerScale(char *, void *, float x, float middle, float width, float)
{
    const float half = width * 0.5f;
    const float high = static_cast<float>(static_cast<double>(half) + middle);
    const float low = static_cast<float>(static_cast<double>(middle) - half);
    if (x < low || x > high)
        return 0.0f;
    if (static_cast<double>(middle) - 1.5 > x)
        return static_cast<float>((static_cast<double>(x) - low) / half);
    if (x <= high && static_cast<double>(middle) + 1.5 < x)
        return static_cast<float>((static_cast<double>(high) - x) / half);
    return 1.0f;
}

/** An option's colour from the fade colour (+0xf8) towards the idle one (+0xf4) by `scale`; a disabled one keeps
 * alpha 0x96. */
void __fastcall IconScrollerUpdateFade(char *scroller, void *, char *option, float scale)
{
    if (!option)
        return;
    FEObject *o = At<FEObject *>(option, 0x10);
    if (!o || !o->pData)
        return;
    const unsigned int idle = At<unsigned int>(scroller, 0xf4), fade = At<unsigned int>(scroller, 0xf8);
    const float rest = 1.0f - scale;
    auto mix = [&](int shift) {
        const double v = static_cast<double>((fade >> shift) & 0xff) * rest + static_cast<double>((idle >> shift) & 0xff) * scale;
        return static_cast<unsigned int>(static_cast<int>(v)) & 0xff;
    };
    const unsigned int alpha = At<bool>(option, 0x20) ? 0x96 : mix(24);
    const unsigned int color = ((alpha << 8 | mix(16)) << 8 | mix(8)) << 8 | mix(0);
    FEngSetColor(o, color);
}

/** An option placed along the scroller: its offset (+0x14) from the scroll position (+0x90) and middle (+0xd4), pulled
 * in by the cube of how far it is from showing whole, clipped, centred on the row (+0xd8), sized by how much shows
 * (shrunk further while fading in or out), aligned by +0xbc (0 top, 1 middle, 2 bottom), and faded. */
void __fastcall IconScrollerPositionOption(char *scroller, void *, char *option)
{
    if (!option)
        return;
    FEObject *o = At<FEObject *>(option, 0x10);
    const float w = At<float>(option, 0x24), h = At<float>(option, 0x28);
    const float middle = At<float>(scroller, 0xd4);
    const float at = static_cast<float>(static_cast<double>(At<float>(scroller, 0x90)) + At<float>(option, 0x14) + middle);
    FEngSetSize(o, w, h);
    float scale = IconScrollerScale(scroller, nullptr, at, middle, At<float>(scroller, 0xcc), w);
    const double rest = 1.0 - static_cast<double>(scale);
    const double pull = rest * rest * rest * w;
    const float x = static_cast<float>(at < middle ? pull + at : static_cast<double>(at) - pull);
    IconScrollerClipEdges(scroller, nullptr, option, x);
    FEngSetCenter(o, x, At<float>(scroller, 0xd8));
    if (At<bool>(scroller, 0xed) || At<bool>(scroller, 0xee))
        scale = static_cast<float>(static_cast<double>(At<float>(scroller, 0xe4)) / At<float>(scroller, 0xe8) * scale);
    float y = 0.0f, left;
    switch (At<int>(scroller, 0xbc))
    {
    case 0:
    {
        float top;
        FEngGetTopLeft(o, left, top);
        y = top;
        break;
    }
    case 1:
        FEngGetTopLeft(o, left, y);
        y = static_cast<float>((static_cast<double>(h) - static_cast<double>(scale) * h) * 0.5 + y);
        break;
    case 2:
        FEngGetTopLeft(o, left, y);
        y = static_cast<float>(static_cast<double>(h) - static_cast<double>(scale) * h + y);
        break;
    }
    FEngSetSize(o, scale * w, scale * h);
    float top;
    FEngGetTopLeft(o, left, top);
    FEngSetTopLeft(o, left, y);
    if (At<bool>(scroller, 0xec))
        IconScrollerUpdateFade(scroller, nullptr, option, scale);
}

namespace
{
/** The list's end marker as an option pointer (the list head is 4 bytes into the scroller). */
char *End(char *scroller)
{
    return scroller;
}

void PlaceAll(char *scroller)
{
    for (char *o = OptionOf(At<char *>(scroller, 4)); o && o != End(scroller); o = Next(o))
        IconScrollerPositionOption(scroller, nullptr, o);
}

void AddBookEnds(char *scroller)
{
    const int count = At<int>(scroller, 0xc0) / 2;
    for (int i = 0; i < count; ++i)
    {
        void *memory = ::operator new(0x4c);
        char *bookEnd = memory ? reinterpret_cast<char *>(new (memory) FEScrollyBookEnd(kBookEnd)) : nullptr;
        FEImage *image = Virtual<FEImage *>(scroller, 8, bookEnd);
        if (image)
            FEngSetTextureHash(image, At<unsigned int>(bookEnd, 0xc));
    }
}
} // namespace

void __fastcall IconScrollerUpdate(char *scroller, void *)
{
    if (At<char *>(scroller, 4) == scroller + 4 || !At<char *>(scroller, 0xc) || At<bool>(scroller, 0xf0))
        return;
    if (At<bool>(scroller, 0x2a))
    {
        const int bookEnds = At<int>(scroller, 0xc0);
        const int selected = At<int>(scroller, 0xc8) - bookEnds;
        At<bool>(scroller, 0x2a) = false;
        reinterpret_cast<FEScrollBar *>(scroller + 0x34)->Update(1, At<int>(scroller, 0x24) - bookEnds - 1, selected,
                                                                 selected);
        reinterpret_cast<tCubic1D *>(scroller + 0x90)->SetValDesired(-At<float>(At<char *>(scroller, 0xc), 0x14));
        IconScrollerUpdateArrows(scroller, nullptr);
    }
    PlaceAll(scroller);
    float &step = At<float>(scroller, 0xe4);
    if (At<bool>(scroller, 0xed))
    {
        step += 1.0f;
        if (step >= At<float>(scroller, 0xe8))
        {
            At<bool>(scroller, 0xed) = false;
            step = At<float>(scroller, 0xe8);
        }
    }
    else if (At<bool>(scroller, 0xee))
    {
        step -= 1.0f;
        if (step <= 0.0f)
            step = 0.0f;
    }
    cPoint::SplineSeek(reinterpret_cast<tCubic1D *>(scroller + 0x90), RealTimeElapsed, 0.0f, 0.0f);
}

FEImage *__fastcall IconScrollerAddOption(char *scroller, void *, char *option)
{
    char name[0x20];
    FEngSNPrintf(name, sizeof(name), "%s%d", At<const char *>(scroller, 0x1c), At<int>(scroller, 0x24));
    FEImage *image = FEngFindImage(At<const char *>(scroller, 0x18), FEHashUpper(name));
    if (!image)
    {
        if (option)
            Virtual<void>(option, 0, 1);
        return nullptr;
    }
    if (!option)
        return nullptr;
    ++At<int>(scroller, 0x24);
    reinterpret_cast<IconOption *>(option)->SetFEngObject(reinterpret_cast<FEObject *>(image));
    At<float>(option, 0x14) = At<float>(scroller, 0xe0);
    At<unsigned int>(option, 0x1c) = At<unsigned int>(scroller, 0xf4);
    FEObject *o = At<FEObject *>(option, 0x10);
    FEngGetSize(o, At<float>(option, 0x24), At<float>(option, 0x28));
    float w, h;
    FEngGetSize(o, w, h);
    At<float>(scroller, 0xe0) = At<float>(scroller, 0x20) + At<float>(scroller, 0xe0) + w;
    char *node = option + 4;
    char *head = scroller + 4;
    char *last = At<char *>(head, 4);
    At<char *>(last, 0) = node;
    At<char *>(head, 4) = node;
    At<char *>(node, 4) = last;
    At<char *>(node, 0) = head;
    if (!At<char *>(scroller, 0xc) && At<int>(scroller, 0x24) > At<int>(scroller, 0xc0) + 1)
    {
        char *first = Next(At<char *>(scroller, 0x2c));
        At<char *>(scroller, 0xc) = first;
        FEngSetCurrentButton(At<const char *>(scroller, 0x18), At<unsigned int>(At<FEObject *>(first, 0x10), 0x10));
    }
    return image;
}

bool __fastcall IconScrollerSetSelection(char *scroller, void *, char *option)
{
    const int index = Virtual<int>(scroller, 0x24, option);
    if (index < 0 || index >= At<int>(scroller, 0x24))
        return false;
    const char *package = At<const char *>(scroller, 0x18);
    for (char *o = OptionOf(At<char *>(scroller, 4)); o != End(scroller); o = Next(o))
        FEngSetButtonState(package, At<unsigned int>(At<FEObject *>(o, 0x10), 0x10), false);
    if (At<bool>(option, 0x20))
        return false;
    At<char *>(scroller, 0xc) = option;
    FEngSetButtonState(package, At<unsigned int>(At<FEObject *>(option, 0x10), 0x10), true);
    FEngSetCurrentButton(package, At<unsigned int>(At<FEObject *>(At<char *>(scroller, 0xc), 0x10), 0x10));
    At<int>(scroller, 0xc8) = Virtual<int>(scroller, 0x24, option);
    return true;
}

void __fastcall IconScrollerAddInitialBookEnds(char *scroller, void *)
{
    AddBookEnds(scroller);
    At<char *>(scroller, 0x2c) = OptionOf(At<char *>(scroller, 8));
}

void __fastcall IconScrollerSetInitialPos(char *scroller, void *, int position)
{
    At<char *>(scroller, 0x30) = OptionOf(At<char *>(scroller, 8));
    AddBookEnds(scroller);
    At<char *>(scroller, 0x30) = Next(At<char *>(scroller, 0x30));
    if (position > 0)
        position += At<int>(scroller, 0xc0) / 2;
    char *at = OptionOf(reinterpret_cast<char *>(reinterpret_cast<bList *>(scroller + 4)->GetNode(position - 1)));
    char *select;
    if (!position || !at)
        select = Next(At<char *>(scroller, 0x2c));
    else if (At<unsigned int>(at, 0xc) == kBookEnd)
        select = Previous(At<char *>(scroller, 0x30));
    else
        select = at;
    Virtual<bool>(scroller, 0x28, select);
    char *current = At<char *>(scroller, 0xc);
    const float to = -(At<bool>(scroller, 0x29) ? At<float>(current, 0x14) : At<float>(current, 0x18));
    At<float>(scroller, 0x98) = to;
    if (to != At<float>(scroller, 0x90))
        At<short>(scroller, 0xb8) = 2;
    At<float>(scroller, 0x90) = At<float>(scroller, 0x98);
    At<float>(scroller, 0x94) = At<float>(scroller, 0x9c);
    At<short>(scroller, 0xb8) = 0;
    if (!At<bool>(scroller, 0xf0))
        PlaceAll(scroller);
    At<bool>(scroller, 0xef) = true;
}
