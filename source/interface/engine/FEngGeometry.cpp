/**
 * The screens' helpers for objects' places, sizes and texture corners, a string's label, and the callbacks run over a
 * package's objects when it loads or a group's strings change (original: zFe FEString::SetLabelHash 0x50d8b0,
 * FEngSetRotationZ 0x514e20, FEngGetTopLeft 0x514fd0, FEngGetBottomRight 0x515140, FEngGetSize 0x515520,
 * FEngGetBottomRightUV 0x515690, FEngSetBottomRightUV 0x515730, FEngSetGroupLanguageHash::Callback 0x515c50,
 * FEngGroupFEPrintf::Callback 0x515db0, FEngHidePCObjects::Callback 0x516230, FEngTransferFlagsToChildren::Callback
 * 0x516270).
 *
 * Objects drawn from their middle (images 1, multi images 7, colored images 9, type 0xc) have a size (data +0x38/+0x3c)
 * around their position (+0x1c/+0x20); a string (2) measures its text in its font (+0x24), localized from its label
 * (+0x60) unless it has its own text (flag 2, text at +0x64), with its format (+0x6c: alignment; bit 4 wraps), leading
 * (+0x70) and wrap width (+0x74), scaled by the size; groups (5) have no place of their own. An image's bottom right UV
 * (data +0x4c/+0x50) is a fraction of its texture's size (TextureInfo +0x44/+0x46). Object flag 0x40: PC only (shown
 * as hidden), 0x10000000 its mouse state.
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
const unsigned int kPCOnly = 0x40;
const unsigned int kMouseState = 0x10000000;
const int kString = 2;
const int kGroup = 5;
const int kTextChars = 0x800;
const unsigned int kFormatLimit = 0x400;
const float kDegrees = 0.017453292f;
const float kQuarterTurn = 1.5707964f;
void **const kLabelCallback = reinterpret_cast<void **>(0x91dd08);
using WideAssign = void *(__thiscall *)(void *, const char *);
const WideAssign kWideAssign = reinterpret_cast<WideAssign>(0x57e8c0);

bool Centred(int type)
{
    return type == 1 || type == 7 || type == 9 || type == 0xc;
}

void Relabel(FEObject *o)
{
    if (void *callback = *kLabelCallback)
        reinterpret_cast<void(__thiscall *)(void *, FEObject *)>((*static_cast<void ***>(callback))[0])(callback, o);
}

/** The text a string shows: its label's localized text, or its own. */
const short *Text(FEObject *o, short *buffer)
{
    if (!(o->Flags & kOwnText) && GetLocalizedWideString(buffer, kTextChars, At<unsigned int>(o, 0x60)))
        return buffer;
    return At<const short *>(o, 0x64);
}
} // namespace

void FEString::SetLabelHash(unsigned int hash)
{
    FEObject *o = reinterpret_cast<FEObject *>(this);
    o->Flags |= kChanged;
    At<unsigned int>(o, 0x60) = hash;
    Relabel(o);
}

void __cdecl FEngSetRotationZ(FEObject *o, float degrees)
{
    if (!o)
        return;
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float c = bSin(degrees * kDegrees + kQuarterTurn);
    m[0] = c;
    m[5] = c;
    const float s = bSin(degrees * kDegrees);
    m[1] = s;
    m[4] = -s;
    float q[4];
    bMatrixToQuaternion(*reinterpret_cast<bQuaternion *>(q), *reinterpret_cast<const bMatrix4 *>(m));
    o->SetRotation(*reinterpret_cast<const FEQuaternion *>(q), false);
}

void __cdecl FEngGetTopLeft(FEObject *o, float &x, float &y)
{
    if (!o)
        return;
    const char *d = reinterpret_cast<const char *>(o->pData);
    if (Centred(o->Type))
    {
        x = At<float>(d, 0x1c) - At<float>(d, 0x38) * 0.5f;
        y = At<float>(d, 0x20) - At<float>(d, 0x3c) * 0.5f;
        return;
    }
    if (o->Type == kGroup)
        return;
    if (o->Type != kString)
    {
        x = At<float>(d, 0x1c);
        y = At<float>(d, 0x20);
        return;
    }
    FEngFont *font = FindFont(At<unsigned int>(o, 0x24));
    if (!font)
        return;
    short buffer[kTextChars / 2];
    const short *text = Text(o, buffer);
    const unsigned int format = At<unsigned int>(o, 0x6c);
    const float width = font->GetTextWidth(text, o->Flags) * At<float>(d, 0x38);
    const float height = font->GetTextHeight(text, At<int>(o, 0x70), o->Flags, At<unsigned int>(o, 0x74),
                                             (format >> 4 & 1) != 0) *
                         At<float>(d, 0x3c);
    x = font->CalculateXOffset(format, width) + At<float>(d, 0x1c);
    y = font->CalculateYOffset(format, height) + At<float>(d, 0x20);
}

void __cdecl FEngGetBottomRight(FEObject *o, float &x, float &y)
{
    if (!o)
        return;
    const char *d = reinterpret_cast<const char *>(o->pData);
    if (Centred(o->Type))
    {
        x = At<float>(d, 0x38) * 0.5f + At<float>(d, 0x1c);
        y = At<float>(d, 0x3c) * 0.5f + At<float>(d, 0x20);
        return;
    }
    if (o->Type != kString)
    {
        x = At<float>(d, 0x1c);
        y = At<float>(d, 0x20);
        return;
    }
    FEngFont *font = FindFont(At<unsigned int>(o, 0x24));
    if (!font)
        return;
    short buffer[kTextChars / 2];
    const short *text = Text(o, buffer);
    const unsigned int format = At<unsigned int>(o, 0x6c);
    const float width = font->GetTextWidth(text, 0) * At<float>(d, 0x38);
    const float height = font->GetTextHeight(text, At<int>(o, 0x70), 0, 0, false) * At<float>(d, 0x3c);
    x = font->CalculateXOffset(format, width) + At<float>(d, 0x1c) + width;
    y = font->CalculateYOffset(format, height) + At<float>(d, 0x20) + height;
}

void __cdecl FEngGetSize(FEObject *o, float &x, float &y)
{
    if (!o)
        return;
    const char *d = reinterpret_cast<const char *>(o->pData);
    if (Centred(o->Type))
    {
        x = At<float>(d, 0x38);
        y = At<float>(d, 0x3c);
        return;
    }
    if (o->Type != kString)
    {
        x = 0;
        y = 0;
        return;
    }
    FEngFont *font = FindFont(At<unsigned int>(o, 0x24));
    if (!font)
        return;
    const short *text = At<const short *>(o, 0x64);
    x = font->GetTextWidth(text, 0) * At<float>(d, 0x38);
    y = font->GetTextHeight(text, At<int>(o, 0x70), 0, 0, false) * At<float>(d, 0x3c);
}

void __cdecl FEngGetBottomRightUV(FEImage *image, float &u, float &v)
{
    if (!image)
        return;
    FEObject *o = reinterpret_cast<FEObject *>(image);
    const TextureInfo *t = GetTextureInfo(At<unsigned int>(o, 0x24), 1, 0);
    u = static_cast<float>(At<short>(t, 0x44)) * At<float>(o->pData, 0x4c);
    v = static_cast<float>(At<short>(t, 0x46)) * At<float>(o->pData, 0x50);
}

void __cdecl FEngSetBottomRightUV(FEImage *image, float u, float v)
{
    if (!image)
        return;
    FEObject *o = reinterpret_cast<FEObject *>(image);
    const TextureInfo *t = GetTextureInfo(At<unsigned int>(o, 0x24), 1, 0);
    At<float>(o->pData, 0x4c) = u / static_cast<float>(At<short>(t, 0x44));
    At<float>(o->pData, 0x50) = v / static_cast<float>(At<short>(t, 0x46));
    o->Flags |= kChanged;
}

bool __fastcall SetGroupLanguageHashCallback(void *self, void *, FEObject *o);
bool __fastcall GroupFEPrintfCallback(void *self, void *, FEObject *o);
bool __fastcall HidePCObjectsCallback(void *self, void *, FEObject *o);
bool __fastcall TransferFlagsCallback(void *self, void *, FEObject *o);
NFS_REPLACE(0x515c50, SetGroupLanguageHashCallback)
NFS_REPLACE(0x515db0, GroupFEPrintfCallback)
NFS_REPLACE(0x516230, HidePCObjectsCallback)
NFS_REPLACE(0x516270, TransferFlagsCallback)

bool __fastcall SetGroupLanguageHashCallback(void *self, void *, FEObject *o)
{
    if (o->Type != kString)
        return true;
    o->Flags |= kChanged;
    At<unsigned int>(o, 0x60) = At<unsigned int>(self, 4);
    Relabel(o);
    o->Flags = (o->Flags & ~kOwnText) | kChanged;
    return true;
}

bool __fastcall GroupFEPrintfCallback(void *self, void *, FEObject *o)
{
    if (o->Type != kString)
        return true;
    const unsigned int limit = At<unsigned int>(o, 0x68);
    char *text = At<char *>(self, 4);
    if (limit < kFormatLimit)
        text[limit - 1] = 0;
    kWideAssign(reinterpret_cast<char *>(o) + 0x64, text);
    o->Flags |= kChanged | kOwnText;
    return true;
}

bool __fastcall HidePCObjectsCallback(void *, void *, FEObject *o)
{
    if (!(o->Flags & kPCOnly))
        return true;
    FEngSetInvisible(o);
    o->Flags &= ~kMouseState;
    o->Flags |= kChanged;
    return true;
}

bool __fastcall TransferFlagsCallback(void *self, void *, FEObject *o)
{
    const unsigned int flags = At<unsigned int>(self, 4);
    if (!(flags & o->Flags) || o->Type != kGroup)
        return true;
    FEObject *child = At<FEObject *>(o, 0x64);
    for (int n = At<int>(o, 0x60); n > 0; --n, child = child->Next)
    {
        child->Flags |= flags;
        using Callback = bool(__thiscall *)(void *, FEObject *);
        reinterpret_cast<Callback>((*static_cast<void ***>(self))[1])(self, child);
    }
    return true;
}
