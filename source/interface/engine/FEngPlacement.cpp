/**
 * The screens' helpers that place objects by a corner or their middle, set labels across a group, show or hide a whole
 * package, and remember each screen's last button (original: zFe FEngSetTopLeft 0x524cc0, FEngSetBottomRight
 * 0x524e20, FEngGetCenter 0x524ee0, FEngSetCenter 0x525050, FEngSetLanguageHash 0x525220,
 * FEngSetAllObjectsInPackageVisibility 0x5248c0, FEngSetMultiImageBottomRightUVs 0x524c00; zFe2 FEngFindScreen 0x591e90,
 * FEngGetLastButton 0x571e80, FEngSetLastButton 0x571eb0, FindScreenButtonDatum 0x571e00 (hash in ebx)).
 *
 * Places as FEngGeometry.cpp describes them; a position set keeps its depth (data +0x24). A string is placed by its
 * text's box at its size, without its own flags. The screens' last buttons (0x91cfc0, 50 of 0xc: the screen name's
 * hash, the button, the database flags (+0x12c) it was set under or -1 for any).
 */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
template <typename T> T &At(const void *p, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(p)) + offset);
}

const unsigned int kChanged = 0x400000;
const unsigned int kOwnText = 2;
const int kString = 2;
const int kGroup = 5;
const int kTextChars = 0x800;
void *const kGroupLanguageHash = reinterpret_cast<void *>(0x89b388);
void *const kVisibilitySetter = reinterpret_cast<void *>(0x89b380);
char *const kButtonData = reinterpret_cast<char *>(0x91cfc0);
const int kButtonEntries = 50;
char *const *const kFEDatabase = reinterpret_cast<char *const *>(0x91cf90);


bool Centred(int type)
{
    return type == 1 || type == 7 || type == 9 || type == 0xc;
}

void Place(FEObject *o, float x, float y)
{
    const float position[3] = {x, y, At<float>(o->pData, 0x24)};
    o->SetPosition(*reinterpret_cast<const FEVector3 *>(position), false);
}

/** A string's text box at its size, and its offsets from its position by its format. */
bool TextBox(FEObject *o, const short *text, float &width, float &height, float &dx, float &dy)
{
    FEngFont *font = FindFont(At<unsigned int>(o, 0x24));
    if (!font)
        return false;
    const char *d = reinterpret_cast<const char *>(o->pData);
    const unsigned int format = At<unsigned int>(o, 0x6c);
    width = font->GetTextWidth(text, 0) * At<float>(d, 0x38);
    height = font->GetTextHeight(text, At<int>(o, 0x70), 0, 0, false) * At<float>(d, 0x3c);
    dx = font->CalculateXOffset(format, width);
    dy = font->CalculateYOffset(format, height);
    return true;
}

char *FindButtonDatum(unsigned int hash)
{
    const unsigned int flags = At<unsigned int>(*kFEDatabase, 0x12c);
    for (int i = 0; i < kButtonEntries; ++i)
    {
        char *e = kButtonData + i * 0xc;
        if (At<unsigned int>(e, 0) != hash)
            continue;
        const unsigned int set = At<unsigned int>(e, 8);
        if (set == 0xffffffffu || set == flags)
            return e;
    }
    return nullptr;
}

struct Walk
{
    void *vtable;
    unsigned int value;
};
} // namespace

char *__fastcall FindPackageDataByName(FEPackageManager *m, void *, const char *name);
struct UV
{
    float u, v;
};
void __fastcall MultiImageSetUVs(FEObject *o, void *, unsigned int index, float left, float top, float right,
                                 float bottom);
void __fastcall MultiImageGetUVs(FEObject *o, void *, unsigned int index, UV *topLeft, UV *bottomRight);

void __cdecl FEngSetTopLeft(FEObject *o, float x, float y)
{
    if (!o)
        return;
    const char *d = reinterpret_cast<const char *>(o->pData);
    if (Centred(o->Type))
        return Place(o, At<float>(d, 0x38) * 0.5f + x, At<float>(d, 0x3c) * 0.5f + y);
    if (o->Type == kString)
    {
        float w, h, dx, dy;
        if (TextBox(o, At<const short *>(o, 0x64), w, h, dx, dy))
            return Place(o, x - dx, y - dy);
        return Place(o, At<float>(d, 0x1c), At<float>(d, 0x20));
    }
    Place(o, x, y);
}

void __cdecl FEngSetBottomRight(FEObject *o, float x, float y)
{
    if (!o)
        return;
    const char *d = reinterpret_cast<const char *>(o->pData);
    if (Centred(o->Type))
        return Place(o, x - At<float>(d, 0x38) * 0.5f, y - At<float>(d, 0x3c) * 0.5f);
    Place(o, x, y);
}

void __cdecl FEngGetCenter(FEObject *o, float &x, float &y)
{
    if (!o)
        return;
    const char *d = reinterpret_cast<const char *>(o->pData);
    if (o->Type != kString)
    {
        x = At<float>(d, 0x1c);
        y = At<float>(d, 0x20);
        return;
    }
    short buffer[kTextChars / 2];
    const short *text = At<const short *>(o, 0x64);
    if (!(o->Flags & kOwnText) && GetLocalizedWideString(buffer, kTextChars, At<unsigned int>(o, 0x60)))
        text = buffer;
    float w, h, dx, dy;
    if (!TextBox(o, text, w, h, dx, dy))
        return;
    x = w * 0.5f + dx + At<float>(d, 0x1c);
    y = h * 0.5f + dy + At<float>(d, 0x20);
}

void __cdecl FEngSetCenter(FEObject *o, float x, float y)
{
    if (!o)
        return;
    const char *d = reinterpret_cast<const char *>(o->pData);
    if (o->Type == kString)
    {
        float w, h, dx, dy;
        if (TextBox(o, At<const short *>(o, 0x64), w, h, dx, dy))
            return Place(o, x - (w * 0.5f + dx), y - (h * 0.5f + dy));
        return Place(o, At<float>(d, 0x1c), At<float>(d, 0x20));
    }
    Place(o, x, y);
}

void __cdecl FEngSetLanguageHash(char const *package, unsigned int object, unsigned int label)
{
    if (!package)
        return;
    FEPackage *p = cFEng::mInstance->FindPackage(package);
    if (!p)
        return;
    FEObject *o = p->FindObjectByHash(object);
    if (!o)
        return;
    if (o->Type == kGroup)
    {
        Walk w = {kGroupLanguageHash, label};
        cFEng::mInstance->FindPackage(package)->ForAllChildren(reinterpret_cast<FEGroup *>(o),
                                                                *reinterpret_cast<FEObjectCallback *>(&w));
    }
    else if (o->Type == kString)
        FEngSetLanguageHash(reinterpret_cast<FEString *>(o), label);
}

void __cdecl FEngSetAllObjectsInPackageVisibility(char const *package, bool visible)
{
    if (FEPackage *p = cFEng::mInstance->FindPackage(package))
    {
        Walk w = {kVisibilitySetter, visible ? 1u : 0u};
        p->ForAllObjects(*reinterpret_cast<FEObjectCallback *>(&w));
    }
}

void __cdecl FEngSetMultiImageBottomRightUVs(FEMultiImage *image, FEVector2 &uv, int index)
{
    if (!image)
        return;
    FEObject *o = reinterpret_cast<FEObject *>(image);
    UV topLeft = {0, 0}, bottomRight = {0, 0};
    MultiImageGetUVs(o, nullptr, static_cast<unsigned int>(index), &topLeft, &bottomRight);
    const UV &to = *reinterpret_cast<const UV *>(&uv);
    MultiImageSetUVs(o, nullptr, static_cast<unsigned int>(index), topLeft.u, topLeft.v, to.u, to.v);
    o->Flags |= kChanged;
}

MenuScreen *__cdecl FEngFindScreen(char const *name)
{
    char *d = FindPackageDataByName(FEPackageManager::Get(), nullptr, name);
    return d ? At<MenuScreen *>(d, 0x1c) : nullptr;
}

unsigned char __cdecl FEngGetLastButton(char const *screen)
{
    const char *e = FindButtonDatum(FEHashUpper(screen));
    return e ? At<unsigned char>(e, 4) : 0;
}

void __cdecl FEngSetLastButton(char const *screen, unsigned char button)
{
    if (char *e = FindButtonDatum(FEHashUpper(screen)))
    {
        At<unsigned char>(e, 4) = button;
        return;
    }
    for (int i = 0; i < kButtonEntries; ++i)
    {
        char *e = kButtonData + i * 0xc;
        if (At<unsigned int>(e, 0))
            continue;
        At<unsigned int>(e, 0) = FEHashUpper(screen);
        At<unsigned char>(e, 4) = button;
        At<unsigned int>(e, 8) = At<unsigned int>(*kFEDatabase, 0x12c);
        return;
    }
}
