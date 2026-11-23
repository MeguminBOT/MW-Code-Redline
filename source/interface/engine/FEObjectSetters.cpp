/**
 * Front-end objects' values set by the screens, names, and the messages a script sends as it plays (original: zFEng
 * FEObject::GetDataOffset 0x5b13d0, SetTrackValue 0x5b7de0 (vector), 0x5b7ee0 (2D vector) and 0x5b7fe0 (colour),
 * SetPosition 0x5b8190, SetRotation 0x5b8200, SetColor 0x5b8390, SetScript 0x5b8400, SetCurrentScript 0x5b7dc0,
 * SetName 0x5b12f0; FEScript::FindTrack 0x5b2910, SetName 0x5b2810; FENode::SetName 0x5b59c0; FEPackage::SetFilename
 * 0x5b1480, IssueScriptMessages 0x5bc530).
 *
 * A value set on an object goes to the track of that value in every one of its scripts (its base value, the track
 * marked changed) and to the object's data, or is added to them. A track (0x38): +3 its flags (0x80 changed), +7 its
 * value's place in the data in words, +0x18 its base value. FEScript: +0xc length, +0x1c/+0x20 tracks, +0x24/+0x28
 * events (0xc each: message, target, time), +0x2c name, +0x30 its name's hash. Object flag 0x400000: changed. Objects
 * of type 0x100 and up take no values.
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
const int kNoValues = 0x100;
const int kColorTrack = 0;
const int kPositionTrack = 2;
const signed char kRotationWord = 0x0a;
const int kTrackBytes = 0x38;
const unsigned int kSetButton = 0x1b3909aa;
FEObject *const kToPackageAndTargets = reinterpret_cast<FEObject *>(0xfffffffc);
FEObject *const kToGame = reinterpret_cast<FEObject *>(0xfffffffb);
FEObject *const kInputSwitch = reinterpret_cast<FEObject *>(0xfffffffa);

const unsigned int kDataOffsets[11] = {0, 0x10, 0x1c, 0x28, 0x38, 0x44, 0x4c, 0x54, 0x64, 0x74, 0x84};
const signed char kTrackWords[11] = {0x0, 0x4, 0x7, 0xa, 0xe, 0x11, 0x13, 0x15, 0x19, 0x1d, 0x21};

using Malloc = void *(__cdecl *)(unsigned int);
using Free = void(__cdecl *)(void *);
const Malloc kMalloc = reinterpret_cast<Malloc>(0x652ac0);
const Free kFree = reinterpret_cast<Free>(0x652b10);

char *Track(const char *script, signed char word)
{
    char *tracks = At<char *>(script, 0x20);
    for (unsigned int i = 0, n = At<unsigned int>(script, 0x1c); i < n; ++i)
        if (At<signed char>(tracks + i * kTrackBytes, 7) == word)
            return tracks + i * kTrackBytes;
    return nullptr;
}

/** Each script's track of the value, then the object's data: `words` words set, or added to. */
template <typename T> void SetValue(FEObject *o, int index, const T *value, int words, bool add)
{
    for (char *s = At<char *>(o, 0x4c); s; s = At<char *>(s, 4))
        if (char *t = Track(s, kTrackWords[index]))
        {
            T *base = reinterpret_cast<T *>(t + 0x18);
            for (int i = 0; i < words; ++i)
                base[i] = add ? base[i] + value[i] : value[i];
            At<unsigned char>(t, 3) &= 0x7f;
        }
    T *data = reinterpret_cast<T *>(reinterpret_cast<char *>(o->pData) + kDataOffsets[index]);
    for (int i = 0; i < words; ++i)
        data[i] = add ? data[i] + value[i] : value[i];
}

/** A quaternion set, or multiplied by (the original's FEQuaternion::operator*, 16 bytes through its result pointer). */
void SetQuaternion(void *to, const void *value, bool add)
{
    struct Quaternion
    {
        float v[4];
    };
    Quaternion product;
    const Quaternion *from = static_cast<const Quaternion *>(value);
    if (add)
    {
        using Multiply = void *(__thiscall *)(const void *, Quaternion *, const void *);
        reinterpret_cast<Multiply>(0x5ad0f0)(to, &product, value);
        from = &product;
    }
    *static_cast<Quaternion *>(to) = *from;
}

char *CopyName(const char *name)
{
    size_t n = 0;
    while (name[n])
        ++n;
    char *copy = static_cast<char *>(kMalloc(static_cast<unsigned int>(n + 1)));
    if (copy)
        for (size_t i = 0; i <= n; ++i)
            copy[i] = name[i];
    return copy;
}
} // namespace

void __fastcall SetTrackVector(FEObject *o, void *, int index, const float *value, bool add);
void __fastcall SetTrackVector2(FEObject *o, void *, int index, const float *value, bool add);
void __fastcall SetTrackColor(FEObject *o, void *, int index, const int *value, bool add);
void __fastcall IssueScriptMessages(FEPackage *p, void *, FEngine *e, FEObject *o, FEScript *script, int from, int to);
NFS_REPLACE(0x5b7de0, SetTrackVector)
NFS_REPLACE(0x5b7ee0, SetTrackVector2)
NFS_REPLACE(0x5b7fe0, SetTrackColor)
NFS_REPLACE(0x5bc530, IssueScriptMessages)

void __fastcall SetTrackVector(FEObject *o, void *, int index, const float *value, bool add)
{
    SetValue(o, index, value, 3, add);
}

void __fastcall SetTrackVector2(FEObject *o, void *, int index, const float *value, bool add)
{
    SetValue(o, index, value, 2, add);
}

void __fastcall SetTrackColor(FEObject *o, void *, int index, const int *value, bool add)
{
    SetValue(o, index, value, 4, add);
}

unsigned int FEObject::GetDataOffset(FEKeyTrack_Indices index)
{
    const unsigned int i = static_cast<unsigned int>(index);
    return i < 11 ? kDataOffsets[i] : 0;
}

void FEObject::SetPosition(FEVector3 const &position, bool add)
{
    if (Type >= kNoValues)
        return;
    const float zero[3] = {0, 0, 0};
    const FEVector3 *against =
        add ? reinterpret_cast<const FEVector3 *>(zero) : reinterpret_cast<const FEVector3 *>(At<char *>(this, 0x2c) + 0x1c);
    if (!CloseEnoughPosition(position, *against))
        Flags |= kChanged;
    SetTrackVector(this, nullptr, kPositionTrack, reinterpret_cast<const float *>(&position), add);
}

void FEObject::SetColor(FEColor const &color, bool add)
{
    if (Type >= kNoValues)
        return;
    const int zero[4] = {0, 0, 0, 0};
    const FEColor *against =
        add ? reinterpret_cast<const FEColor *>(zero) : reinterpret_cast<const FEColor *>(At<char *>(this, 0x2c));
    if (!CloseEnoughColor(color, *against))
        Flags |= kChanged;
    SetTrackColor(this, nullptr, kColorTrack, reinterpret_cast<const int *>(&color), add);
}

void FEObject::SetRotation(FEQuaternion const &rotation, bool add)
{
    if (Type >= kNoValues)
        return;
    Flags |= kChanged;
    for (char *s = At<char *>(this, 0x4c); s; s = At<char *>(s, 4))
    {
        char *tracks = At<char *>(s, 0x20);
        for (unsigned int i = 0, n = At<unsigned int>(s, 0x1c); i < n; ++i)
        {
            char *t = tracks + i * kTrackBytes;
            if (At<signed char>(t, 7) != kRotationWord)
                continue;
            FEQuaternion *base = reinterpret_cast<FEQuaternion *>(t + 0x18);
            SetQuaternion(base, &rotation, add);
            At<unsigned char>(t, 3) &= 0x7f;
            break;
        }
    }
    FEQuaternion *data = reinterpret_cast<FEQuaternion *>(At<char *>(this, 0x2c) + 0x28);
    SetQuaternion(data, &rotation, add);
}

void FEObject::SetScript(FEScript *script, bool restart)
{
    if (!restart && script == pCurrentScript)
        return;
    pCurrentScript = script;
    if (script)
        SetupMoveToTracks();
    pCurrentScript->CurrentTime = 0;
}

void FEObject::SetCurrentScript(FEScript *script)
{
    pCurrentScript = script;
    if (script)
        SetupMoveToTracks();
}

void FEObject::SetName(char const *name)
{
    if (pName)
    {
        kFree(pName);
        pName = nullptr;
    }
    NameHash = 0xffffffff;
    if (name)
    {
        pName = CopyName(name);
        NameHash = FEHashUpper(pName);
    }
}

FEKeyTrack *FEScript::FindTrack(FEKeyTrack_Indices index) const
{
    return reinterpret_cast<FEKeyTrack *>(Track(reinterpret_cast<const char *>(this), kTrackWords[index]));
}

void FEScript::SetName(char const *name)
{
    char *&own = At<char *>(this, 0x2c);
    if (own)
    {
        kFree(own);
        own = nullptr;
    }
    At<unsigned int>(this, 0x30) = 0xffffffff;
    if (name)
    {
        own = CopyName(name);
        At<unsigned int>(this, 0x30) = FEHashUpper(own);
    }
}

bool FENode::SetName(char const *name)
{
    char *&own = At<char *>(this, 0xc);
    bool set = false;
    if (own)
    {
        kFree(own);
        own = nullptr;
    }
    if (name)
    {
        own = CopyName(name);
        set = own != nullptr;
    }
    At<unsigned int>(this, 0x10) = FEHashUpper(own);
    return set;
}

void FEPackage::SetFilename(char const *name)
{
    char *&own = At<char *>(this, 0x38);
    kFree(own);
    own = nullptr;
    if (name)
        own = CopyName(name);
}

void __fastcall IssueScriptMessages(FEPackage *p, void *, FEngine *e, FEObject *o, FEScript *script, int from, int to)
{
    const int count = At<int>(script, 0x24);
    const unsigned int *events = At<const unsigned int *>(script, 0x28);
    if (to < from)
        return;
    if (to == At<int>(script, 0xc))
        ++to;
    int i = 0;
    while (i < count && events[i * 3 + 2] < static_cast<unsigned int>(from))
        ++i;
    for (; i < count; ++i)
    {
        const unsigned int *ev = events + i * 3;
        if (ev[2] >= static_cast<unsigned int>(to))
            return;
        const unsigned int message = ev[0];
        const unsigned int target = ev[1];
        if (target == 0xfffffffc)
            e->QueueMessage(message, o, p, kToPackageAndTargets, 0);
        else if (target == 0xffffffff)
        {
            void *gi = At<void *>(e, 0x108);
            using ToGame = void(__thiscall *)(void *, unsigned int, FEObject *, unsigned int, FEPackage *);
            reinterpret_cast<ToGame>((*static_cast<void ***>(gi))[0x18 / 4])(gi, message, o, 0, p);
        }
        else if (target == 0xfffffffb)
            e->QueueMessage(message, o, p, kToGame, 0);
        else if (target == 0)
        {
            if (message == kSetButton)
                p->SetCurrentButton(p->FindObjectByGUID(0), true);
            else
                e->QueueMessage(message, o, p, nullptr, 0);
        }
        else if (target == 0xfffffffa)
            e->QueueMessage(message, o, p, kInputSwitch, 0);
        else
        {
            FEObject *targetObject = p->FindObjectByGUID(target);
            if (message == kSetButton)
                p->SetCurrentButton(p->FindObjectByGUID(target), true);
            else if (o)
                e->QueueMessage(message, o, p, targetObject, 0);
        }
    }
}
