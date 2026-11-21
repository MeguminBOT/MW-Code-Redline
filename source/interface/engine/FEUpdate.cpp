/**
 * The front-end engine's animation and drawing order (original: zFEng): an object's current script runs its key tracks over the object's
 * data each update (FEPackage::UpdateObject), and the visible objects go to the game's render list with their depth (FEngine::RenderGroup).
 *
 * A key track holds its keys in a list of its own, or in another track's list when it shares them (the flag at +0x2c). It lies between keys
 * as its action says: stop at the last, loop, or go back and forth. The objects' data the tracks write is laid out as FEObjData; a track's
 * offset into it (+7) counts dwords.
 */
#include <stddef.h>
#include <string.h>

#include <windows.h>

#include "game/gen/api.h"
#include "legacy/link.h"
#include "core/log.h"

NFS_REPLACE(0x5b5840, InterpNone)
NFS_REPLACE(0x5b9d10, InterpLinear)
NFS_REPLACE(0x5ba1c0, InterpScriptNone)
NFS_REPLACE(0x5bde50, InterpScriptLinear)
NFS_REPLACE(0x5bdda0, KeyInterp)
NFS_REPLACE(0x5bddd0, KeyInterpFast)
NFS_REPLACE(0x5c07b0, KeyInterpScript)

void __cdecl FELerpInteger(int from, int to, float t, int *add, int *out);
void __cdecl FELerpVector2(FEVector2 &from, FEVector2 &to, float t, FEVector2 *add, FEVector2 *out);
void __cdecl FELerpVector3(FEVector3 &from, FEVector3 &to, float t, FEVector3 *add, FEVector3 *out);
void __cdecl FELerpColor(FEColor &from, FEColor &to, float t, FEColor *add, FEColor *out);
void __cdecl FEMultMatrix(FEVector3 *out, const FEMatrix4 *matrix, const FEVector3 *in);
void __cdecl FEMultMatrix(FEMatrix4 *out, const FEMatrix4 *left, const FEMatrix4 *right);

namespace
{
/** FEKeyNode: one key of a track. */
struct Key
{
    void *vtable;
    Key *next;
    Key *previous;
    int time;
    int value[4];
};

/** The parts of FEKeyTrack the interpolation reads. */
struct Track
{
    unsigned char type;
    unsigned char size;
    unsigned char interpolation;
    unsigned char action;
    unsigned int lengthAndOffset;
    Key base;
    void *unknown_28;
    unsigned char shared;
    unsigned char unknown_2d[3];
    void *keys;
    void *tail;
};
const int kTrackSize = 0x38;
static_assert(offsetof(Track, base) == 8 && offsetof(Track, shared) == 0x2c && offsetof(Track, keys) == 0x30 &&
                  sizeof(Track) == kTrackSize,
              "FEKeyTrack layout");

enum ParamType
{
    kInteger = 1,
    kFloat = 2,
    kVector2 = 3,
    kVector3 = 4,
    kQuaternion = 5,
    kColor = 6,
};

enum InterpolationType
{
    kInterpNone = 0,
    kInterpLinear = 1,
    kInterpLinearToo = 3,
    kInterpMoveTo = 4,
};

enum Action
{
    kActionStop = 0,
    kActionLoop = 1,
    kActionBackAndForth = 2,
    kActionMask = 0x7f,
    /** The key's value is final: the track has nothing more to do. */
    kActionSettled = 0x80,
};

const float kLeastSpan = 1e-05f;

/** FEObject::Flags: what changes its data needs drawn again. */
const unsigned int kBaseDirty = 0x800000;
const unsigned int kTracksDirty = 0x1000000;
const unsigned int kForceUpdate = 0x400000;
const unsigned int kAnyDirty = 0x1c00000;
const unsigned int kDirty = 0x2000000;
/** What a group passes to its children. */
const unsigned int kInheritedDirty = 0x3c00000;

/** FEObject::Type. */
enum ObjectType
{
    kListBox = 4,
    kGroup = 5,
    kCodeListBox = 6,
    kMovie = 7,
};

/** Script flags: what happens at its end. */
const unsigned int kScriptAction = 3;
enum ScriptAction
{
    kScriptStop = 0,
    kScriptLoop = 1,
    kScriptBackAndForth = 2,
};

inline int TrackLength(const Track *track)
{
    return static_cast<int>(track->lengthAndOffset << 8) >> 8;
}

inline int TrackOffset(const FEKeyTrack *track)
{
    return reinterpret_cast<const signed char *>(track)[7];
}

inline Key *FirstKey(const void *track)
{
    const Track *t = static_cast<const Track *>(track);
    if (!t->shared)
        return static_cast<Key *>(t->keys);
    return reinterpret_cast<Key *>(static_cast<const FERefList *>(t->keys)->GetHead());
}

/** The first key at or after `time`, or the last key. */
inline Key *WalkTo(Key *key, int time)
{
    while (key->next && key->time < time)
        key = key->next;
    return key;
}

/** (done / span) with a span of nothing as the start. */
inline float Fraction(int done, int span)
{
    if (span <= 0)
        return 0.0f;
    return static_cast<float>(static_cast<double>(done) / static_cast<double>(span));
}

/** A quaternion's product (FEQuaternion::operator*). */
inline void QuaternionTimes(int *out, const int *left, const int *right)
{
    const float *a = reinterpret_cast<const float *>(left), *b = reinterpret_cast<const float *>(right);
    const float x = b[3] * a[0] + a[3] * b[0] + (a[1] * b[2] - b[1] * a[2]);
    const float y = b[1] * a[3] + b[3] * a[1] + (b[0] * a[2] - a[0] * b[2]);
    const float z = b[3] * a[2] + a[3] * b[2] + (b[1] * a[0] - a[1] * b[0]);
    const float w = b[3] * a[3] - (b[2] * a[2] + b[1] * a[1] + b[0] * a[0]);
    float *o = reinterpret_cast<float *>(out);
    o[0] = x;
    o[1] = y;
    o[2] = z;
    o[3] = w;
}
} // namespace

FEKeyNode *FEKeyTrack::GetKeyAt(int time)
{
    Track *track = reinterpret_cast<Track *>(this);
    if (time >= 0)
        if (Key *key = FirstKey(track))
            return reinterpret_cast<FEKeyNode *>(WalkTo(key, time));
    return reinterpret_cast<FEKeyNode *>(&track->base);
}

FEKeyNode *FEKeyTrack::GetDeltaKeyAt(int time)
{
    Key *key = FirstKey(this);
    return reinterpret_cast<FEKeyNode *>(key ? WalkTo(key, time) : nullptr);
}

void __cdecl InterpNone(FEKeyTrack *keyTrack, int time, void *out)
{
    Track *track = reinterpret_cast<Track *>(keyTrack);
    const int length = TrackLength(track);
    Key *key;
    if (length < time)
    {
        key = reinterpret_cast<Key *>(keyTrack->GetKeyAt(length - time));
        if (key->next && time > key->time)
            key = key->next;
    }
    else
    {
        key = reinterpret_cast<Key *>(keyTrack->GetKeyAt(time));
        if (key->previous && key->time > time)
            key = key->previous;
    }
    memcpy(out, key->value, track->size);
}

void __cdecl InterpLinear(FEKeyTrack *keyTrack, int time, void *outValue)
{
    Track *track = reinterpret_cast<Track *>(keyTrack);
    int *out = static_cast<int *>(outValue);
    float *outFloat = static_cast<float *>(outValue);
    int *base = track->base.value;
    const float *baseFloat = reinterpret_cast<const float *>(base);
    Key *from = nullptr, *to = nullptr;
    float t = 0.0f;
    if (FirstKey(track))
    {
        switch (track->action & kActionMask)
        {
        case kActionStop:
            to = reinterpret_cast<Key *>(keyTrack->GetDeltaKeyAt(time));
            if (to)
            {
                from = to->previous;
                if (!from || time >= to->time)
                    t = 1.0f;
                else
                    t = Fraction(time - from->time, to->time - from->time);
            }
            break;
        case kActionLoop:
        {
            Key *key = reinterpret_cast<Key *>(keyTrack->GetDeltaKeyAt(time));
            if (!key)
                break;
            if (key->time < time)
            {
                from = key;
                to = FirstKey(track);
                t = Fraction(time - from->time, TrackLength(track) + to->time - from->time);
            }
            else if (key->time == time)
            {
                from = to = key;
            }
            else
            {
                to = key;
                from = key->previous;
                if (!from)
                {
                    const int length = TrackLength(track);
                    from = reinterpret_cast<Key *>(keyTrack->GetKeyAt(length));
                    const int left = length - from->time;
                    t = Fraction(left + time, left + key->time);
                }
                else
                {
                    t = Fraction(time - from->time, key->time - from->time);
                }
            }
            break;
        }
        case kActionBackAndForth:
        {
            const int length = TrackLength(track);
            to = reinterpret_cast<Key *>(keyTrack->GetDeltaKeyAt(time > length ? length * 2 - time : time));
            // The original read through a null `from` here: no shipped track goes back and forth.
            from = to;
            break;
        }
        default:
            break;
        }
    }
    if (!to)
    {
        switch (track->type)
        {
        case kInteger:
        case kFloat:
            out[0] = base[0];
            break;
        case kVector2:
            out[0] = base[0];
            out[1] = base[1];
            break;
        case kVector3:
            out[0] = base[0];
            out[1] = base[1];
            out[2] = base[2];
            break;
        case kQuaternion:
        case kColor:
            out[0] = base[0];
            out[1] = base[1];
            out[2] = base[2];
            out[3] = base[3];
            break;
        default:
            break;
        }
        return;
    }
    if (t == 0.0f || t == 1.0f)
    {
        const Key *key = t == 0.0f ? from : to;
        const float *value = reinterpret_cast<const float *>(key->value);
        switch (track->type)
        {
        case kInteger:
            out[0] = base[0] + key->value[0];
            break;
        case kFloat:
            outFloat[0] = baseFloat[0] + value[0];
            break;
        case kVector2:
            outFloat[0] = baseFloat[0] + value[0];
            outFloat[1] = value[1] + baseFloat[1];
            break;
        case kVector3:
            outFloat[0] = baseFloat[0] + value[0];
            outFloat[1] = value[1] + baseFloat[1];
            outFloat[2] = value[2] + baseFloat[2];
            break;
        case kQuaternion:
            QuaternionTimes(out, key->value, base);
            break;
        case kColor:
            out[0] = base[0] + key->value[0];
            out[1] = base[1] + key->value[1];
            out[2] = base[2] + key->value[2];
            out[3] = base[3] + key->value[3];
            break;
        default:
            break;
        }
        return;
    }
    switch (track->type)
    {
    case kInteger:
        FELerpInteger(from->value[0], to->value[0], t, base, out);
        break;
    case kFloat:
    {
        const float a = *reinterpret_cast<const float *>(from->value), b = *reinterpret_cast<const float *>(to->value);
        outFloat[0] = (b - a) * t + a + baseFloat[0];
        break;
    }
    case kVector2:
        FELerpVector2(*reinterpret_cast<FEVector2 *>(from->value), *reinterpret_cast<FEVector2 *>(to->value), t,
                      reinterpret_cast<FEVector2 *>(base), reinterpret_cast<FEVector2 *>(out));
        break;
    case kVector3:
        FELerpVector3(*reinterpret_cast<FEVector3 *>(from->value), *reinterpret_cast<FEVector3 *>(to->value), t,
                      reinterpret_cast<FEVector3 *>(base), reinterpret_cast<FEVector3 *>(out));
        break;
    case kQuaternion:
        NFS_ORIGINAL(0x5b54d0, void(__cdecl *)(int *, int *, float, int *, int *))(from->value, to->value, t, base, out);
        break;
    case kColor:
        FELerpColor(*reinterpret_cast<FEColor *>(from->value), *reinterpret_cast<FEColor *>(to->value), t,
                    reinterpret_cast<FEColor *>(base), reinterpret_cast<FEColor *>(out));
        break;
    default:
        break;
    }
}

void __cdecl InterpScriptNone(FEScript *script, unsigned char index, int time, void *data)
{
    FEKeyTrack *track = reinterpret_cast<FEKeyTrack *>(reinterpret_cast<char *>(script->pTracks) + index * kTrackSize);
    InterpNone(track, time, static_cast<int *>(data) + TrackOffset(track));
}

void __cdecl InterpScriptLinear(FEScript *script, unsigned char index, int time, void *data)
{
    FEKeyTrack *track = reinterpret_cast<FEKeyTrack *>(reinterpret_cast<char *>(script->pTracks) + index * kTrackSize);
    InterpLinear(track, time, static_cast<int *>(data) + TrackOffset(track));
}

void __cdecl KeyInterp(FEKeyTrack *track, int time, void *out)
{
    const unsigned char interpolation = reinterpret_cast<Track *>(track)->interpolation;
    if (interpolation == kInterpNone)
        InterpNone(track, time, out);
    else if (interpolation == kInterpLinear || interpolation == kInterpLinearToo)
        InterpLinear(track, time, out);
}

void __cdecl KeyInterpFast(FEKeyTrack *track, int time, void *out)
{
    Track *t = reinterpret_cast<Track *>(track);
    if (t->action & kActionSettled)
        return;
    KeyInterp(track, time, out);
    if (!FirstKey(t))
        t->action |= kActionSettled;
}

void __cdecl KeyInterpScript(FEScript *script, unsigned char index, int time, FEObject *object)
{
    FEKeyTrack *track = reinterpret_cast<FEKeyTrack *>(reinterpret_cast<char *>(script->pTracks) + index * kTrackSize);
    KeyInterp(track, time, reinterpret_cast<int *>(object->pData) + TrackOffset(track));
}

namespace
{
/** FEPackage: whether its scripts run (+0x14), its engine and how many ticks a script moves an update. */
inline bool Executing(const FEPackage *package)
{
    return reinterpret_cast<const char *>(package)[0x14] != 0;
}

inline FEngine *Engine(const FEPackage *package)
{
    return *reinterpret_cast<FEngine *const *>(reinterpret_cast<const char *>(package) + 0x30);
}

inline int TickIncrement(const FEPackage *package)
{
    return *reinterpret_cast<const int *>(reinterpret_cast<const char *>(package) + 0x34);
}

/** FEGroup's first child. */
inline FEObject *FirstChild(const FEObject *group)
{
    return *reinterpret_cast<FEObject *const *>(reinterpret_cast<const char *>(group) + 0x64);
}
} // namespace

void FEObject::SetupMoveToTracks()
{
    const int count = static_cast<int>(pCurrentScript->TrackCount);
    Track *track = reinterpret_cast<Track *>(pCurrentScript->pTracks);
    for (int i = 0; i < count; ++i, ++track)
    {
        track->action &= kActionMask;
        if (track->interpolation != kInterpLinearToo && track->interpolation != kInterpMoveTo)
            continue;
        Key *key = FirstKey(track);
        if (!key)
            continue;
        const int *data = reinterpret_cast<const int *>(pData) + reinterpret_cast<const signed char *>(track)[7];
        const float *now = reinterpret_cast<const float *>(data), *base = reinterpret_cast<const float *>(track->base.value);
        float *value = reinterpret_cast<float *>(key->value);
        switch (track->type)
        {
        case kInteger:
            key->value[0] = data[0] - track->base.value[0];
            break;
        case kFloat:
            value[0] = now[0] - base[0];
            break;
        case kVector2:
            value[0] = now[0] - base[0];
            value[1] = now[1] - base[1];
            break;
        case kVector3:
            value[0] = now[0] - base[0];
            value[1] = now[1] - base[1];
            value[2] = now[2] - base[2];
            break;
        case kQuaternion:
        {
            // The rotation from the base to where the object is now.
            const float inverse[4] = {-base[0], -base[1], -base[2], base[3]};
            QuaternionTimes(key->value, data, reinterpret_cast<const int *>(inverse));
            break;
        }
        case kColor:
            for (int c = 0; c < 4; ++c)
                key->value[c] = data[c] - track->base.value[c];
            break;
        default:
            break;
        }
    }
}

void FEPackage::UpdateObjectTracks(FEObject *object, FEScript *script)
{
    const int time = script->CurrentTime;
    FEKeyTrack *track = script->pTracks;
    int *data = reinterpret_cast<int *>(object->pData);
    const int trackCount = static_cast<unsigned char>(script->TrackCount);
    if (!Executing(this))
    {
        // Paused: every track again, as what changes them is not the clock.
        if (track && !(reinterpret_cast<Track *>(track)->lengthAndOffset & 0xff000000u))
            KeyInterp(track, time, data);
        // The first track is usually the colour: it may just have made the object visible or hidden.
        if (object->pData->Col[3])
            for (int i = 0; i < trackCount; ++i)
            {
                KeyInterp(track, time, data + TrackOffset(track));
                track = reinterpret_cast<FEKeyTrack *>(reinterpret_cast<char *>(track) + kTrackSize);
            }
    }
    else
    {
        const unsigned int hold = FEPackage::uHoldDirtyFlags;
        if (!track || (reinterpret_cast<Track *>(track)->lengthAndOffset & 0xff000000u))
        {
            object->Flags &= hold | ~kBaseDirty;
        }
        else if (reinterpret_cast<Track *>(track)->action & kActionSettled)
        {
            object->Flags &= hold | ~kBaseDirty;
            KeyInterpFast(track, time, data);
        }
        else
        {
            object->Flags |= kBaseDirty;
            KeyInterpFast(track, time, data);
        }
        bool moving = false;
        if (object->pData->Col[3] && trackCount)
        {
            unsigned char settled = kActionSettled;
            for (int i = 0; i < trackCount; ++i)
            {
                settled &= reinterpret_cast<Track *>(track)->action;
                KeyInterpFast(track, time, data + TrackOffset(track));
                track = reinterpret_cast<FEKeyTrack *>(reinterpret_cast<char *>(track) + kTrackSize);
            }
            moving = settled == 0;
        }
        if (moving)
            object->Flags |= kTracksDirty;
        else
            object->Flags &= hold | ~kTracksDirty;
    }
    if (object->Flags & kAnyDirty)
        object->Flags |= kDirty;
}

void FEPackage::UpdateGroup(FEGroup *groupObject, int ticks)
{
    FEObject *group = reinterpret_cast<FEObject *>(groupObject);
    for (FEObject *child = FirstChild(group); child; child = child->Next)
    {
        UpdateObject(child, ticks);
        child->Flags |= group->Flags & kInheritedDirty;
    }
}

void FEPackage::UpdateObject(FEObject *object, int ticks)
{
    if (object->Flags & kAnyDirty)
        object->Flags |= kDirty;
    else
        object->Flags &= FEPackage::uHoldDirtyFlags | ~kDirty;
    const bool executing = Executing(this);
    FEngine *engine = Engine(this);
    FEScript *script = object->pCurrentScript;
    const int previous = script->CurrentTime;
    const int length = script->Length;
    script->CurrentTime = previous + TickIncrement(this);
    if (script->CurrentTime < 0)
        script->CurrentTime = 0;
    const int now = script->CurrentTime;
    if (now < length)
    {
        if (executing && script->EventCount)
        {
            const unsigned int action = script->Flags & kScriptAction;
            if (action == kScriptStop)
            {
                IssueScriptMessages(engine, object, script, previous, now);
            }
            else if (action == kScriptLoop)
            {
                if (now < previous)
                {
                    IssueScriptMessages(engine, object, script, previous, length);
                    IssueScriptMessages(engine, object, script, 0, script->CurrentTime);
                }
                else
                {
                    IssueScriptMessages(engine, object, script, previous, now);
                }
            }
            else if (action == kScriptBackAndForth)
            {
                if (previous < length)
                {
                    int from = previous, to = now;
                    if (length <= now)
                    {
                        IssueScriptMessages(engine, object, script, previous, length);
                        to = script->CurrentTime - length;
                        from = length;
                    }
                    IssueScriptMessages(engine, object, script, from, to);
                }
                else if (now < length)
                {
                    IssueScriptMessages(engine, object, script, previous - length, 0);
                    IssueScriptMessages(engine, object, script, 0, script->CurrentTime);
                }
                else
                {
                    IssueScriptMessages(engine, object, script, previous - length, now - length);
                }
            }
        }
    }
    else if (executing)
    {
        if (!script->pChainTo)
        {
            const unsigned int action = script->Flags & kScriptAction;
            if (action == kScriptStop)
            {
                if (script->EventCount)
                    IssueScriptMessages(engine, object, script, previous, length);
                script->CurrentTime = script->Length + 1;
            }
            else if (action == kScriptLoop)
            {
                if (length > 0)
                {
                    if (script->EventCount)
                        IssueScriptMessages(engine, object, script, previous, length);
                    script->CurrentTime %= script->Length;
                    if (script->EventCount)
                        IssueScriptMessages(engine, object, script, 0, script->CurrentTime);
                    object->SetupMoveToTracks();
                }
                else
                {
                    script->CurrentTime = 0;
                }
            }
            else if (action == kScriptBackAndForth)
            {
                script->CurrentTime = length > 0 ? now % (length * 2) : 0;
            }
        }
        else
        {
            UpdateObjectTracks(object, script);
            const int over = script->CurrentTime - length;
            script->CurrentTime = 0;
            if (script->EventCount)
                IssueScriptMessages(engine, object, script, previous, length);
            script = script->pChainTo;
            object->pCurrentScript = script;
            if (script)
                object->SetupMoveToTracks();
            script->CurrentTime = over;
            if (script->EventCount)
                IssueScriptMessages(engine, object, script, 0, over);
        }
    }
    const bool stopped = executing && previous == script->CurrentTime && previous == script->Length + 1 &&
                         !(object->Flags & kForceUpdate);
    if (!stopped)
        UpdateObjectTracks(object, script);
    switch (object->Type)
    {
    case kListBox:
        reinterpret_cast<FEListBox *>(object)->Update(static_cast<float>(ticks));
        break;
    case kGroup:
        UpdateGroup(reinterpret_cast<FEGroup *>(object), ticks);
        break;
    case kCodeListBox:
        NFS_ORIGINAL(0x5adf20, void(__thiscall *)(FEObject *, float))(object, static_cast<float>(ticks));
        break;
    case kMovie:
        if (executing)
            *reinterpret_cast<int *>(reinterpret_cast<char *>(object) + 0x5c) += ticks;
        break;
    default:
        break;
    }
    if (reinterpret_cast<const char *>(this)[0x14] == 1 && previous == script->CurrentTime &&
        previous == script->Length + 1 && !(object->Flags & kForceUpdate))
        object->Flags &= FEPackage::uHoldDirtyFlags | ~(kBaseDirty | kTracksDirty);
    object->Flags &= FEPackage::uHoldDirtyFlags | ~kForceUpdate;
    if (object->Flags & kAnyDirty)
        object->Flags |= kDirty;
}

namespace
{
/** FEngine: whether it draws what is flagged hidden too, its count of groups drawn, the game's interface and the objects to draw. */
struct RenderListEntry
{
    FEObject *object;
    float depth;
};

inline bool DrawsHidden(const FEngine *engine)
{
    return *reinterpret_cast<const char *>(engine) != 0;
}

inline unsigned short &RenderCount(FEngine *engine)
{
    return *reinterpret_cast<unsigned short *>(reinterpret_cast<char *>(engine) + 0xd4);
}

inline void *GameInterface(FEngine *engine)
{
    return *reinterpret_cast<void **>(reinterpret_cast<char *>(engine) + 0x108);
}

inline int &RenderListCount(FEngine *engine)
{
    return *reinterpret_cast<int *>(reinterpret_cast<char *>(engine) + 0x10c);
}

inline RenderListEntry *RenderList(FEngine *engine)
{
    return reinterpret_cast<RenderListEntry *>(reinterpret_cast<char *>(engine) + 0x110);
}

const int kInterfaceGroupRendered = 10;
const int kInterfacePackageBegin = 8;
const int kInterfacePackageEnd = 9;
const int kInterfaceObjectList = 12;
const int kInterfaceViewMatrix = 15;
/** FEngine: its list of active packages, and whether it drew what is flagged hidden. */
const int kPackageList = 0xd8;
const int kDrewHidden = 0x524d;
const int kMaxPackages = 64;
/** FEPackage: its priority and first object. */
const int kPackagePriority = 0x1c;
const int kPackageFirstObject = 0x4c;
const unsigned int kObjectHidden = 0x80000000u;
} // namespace

void FEngine::RenderObject(FEObject *object, FEMatrix4 &matrix, unsigned short context)
{
    const FEObjData *data = object->pData;
    if (!data->Col[3])
        return;
    const float at[3] = {data->Pivot[0] + data->Pos[0], data->Pivot[1] + data->Pos[1], data->Pivot[2] + data->Pos[2]};
    float out[3];
    FEMultMatrix(reinterpret_cast<FEVector3 *>(out), &matrix, reinterpret_cast<const FEVector3 *>(at));
    object->RenderContext = context;
    if (out[2] <= 0.0f)
        return;
    int &count = RenderListCount(this);
    RenderListEntry &entry = RenderList(this)[count];
    entry.object = object;
    entry.depth = out[2];
    ++count;
}

void FEngine::RenderGroup(FEGroup *groupObject, FEMatrix4 &parent, FEMatrix4 &view, unsigned short context)
{
    FEObject *group = reinterpret_cast<FEObject *>(groupObject);
    FEObjData *data = group->pData;
    if (!data->Col[3] || (!DrawsHidden(this) && (group->Flags & kObjectHidden)))
        return;
    alignas(16) float own[16], world[16];
    reinterpret_cast<FEQuaternion *>(data->Rot)->GetMatrix(reinterpret_cast<FEMatrix4 *>(own));
    const float pivot[3] = {-data->Pivot[0], -data->Pivot[1], -data->Pivot[2]};
    float turned[3];
    FEMultMatrix(reinterpret_cast<FEVector3 *>(turned), reinterpret_cast<const FEMatrix4 *>(own), reinterpret_cast<const FEVector3 *>(pivot));
    own[12] = turned[0] + data->Pos[0] + data->Pivot[0];
    own[13] = data->Pos[1] + data->Pivot[1] + turned[1];
    own[14] = data->Pos[2] + data->Pivot[2] + turned[2];
    FEMultMatrix(reinterpret_cast<FEMatrix4 *>(world), reinterpret_cast<const FEMatrix4 *>(own), &parent);
    FEMultMatrix(reinterpret_cast<FEMatrix4 *>(own), reinterpret_cast<const FEMatrix4 *>(world), &view);
    const unsigned short rendered = ++RenderCount(this);
    group->RenderContext = context;
    void *game = GameInterface(this);
    typedef void(__thiscall * GroupRendered)(void *, unsigned int, FEObject *);
    (*static_cast<GroupRendered **>(game))[kInterfaceGroupRendered](game, rendered, group);
    for (FEObject *child = FirstChild(group); child; child = child->Next)
    {
        if (child->Type == kGroup)
            RenderGroup(reinterpret_cast<FEGroup *>(child), *reinterpret_cast<FEMatrix4 *>(world), view, rendered);
        else
            RenderObject(child, *reinterpret_cast<FEMatrix4 *>(own), rendered);
    }
}

void FEngine::Render()
{
    char *engine = reinterpret_cast<char *>(this);
    void *game = GameInterface(this);
    typedef void(__thiscall * WithPackage)(void *, void *);
    typedef void(__thiscall * ObjectList)(void *, void *, int);
    void **const table = *static_cast<void ***>(game);
    alignas(16) float identity[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    alignas(16) float view[16];
    reinterpret_cast<WithPackage>(table[kInterfaceViewMatrix])(game, view);
    // The packages in order of priority, those of the same priority as they were.
    FEMinList &packages = *reinterpret_cast<FEMinList *>(engine + kPackageList);
    FEMinNode *const head = *reinterpret_cast<FEMinNode **>(engine + kPackageList + 8);
    FEMinNode *order[kMaxPackages];
    int count = 0;
    for (FEMinNode *node = head; node; node = *reinterpret_cast<FEMinNode **>(reinterpret_cast<char *>(node) + 4))
        order[count++] = node;
    for (int i = 0; i < count; ++i)
        packages.RemNode(order[i]);
    for (int i = 0; i < count; ++i)
    {
        const int priority = *reinterpret_cast<const int *>(reinterpret_cast<const char *>(order[i]) + kPackagePriority);
        FEMinNode *after = *reinterpret_cast<FEMinNode **>(engine + kPackageList + 0xc);
        while (after && *reinterpret_cast<const int *>(reinterpret_cast<const char *>(after) + kPackagePriority) > priority)
            after = *reinterpret_cast<FEMinNode **>(reinterpret_cast<char *>(after) + 8);
        packages.AddNode(after, order[i]);
    }
    RenderCount(this) = 0;
    for (char *package = *reinterpret_cast<char **>(engine + kPackageList + 8); package; package = *reinterpret_cast<char **>(package + 4))
    {
        reinterpret_cast<WithPackage>(table[kInterfacePackageBegin])(game, package);
        RenderListCount(this) = 0;
        for (FEObject *object = *reinterpret_cast<FEObject **>(package + kPackageFirstObject); object; object = object->Next)
        {
            if (object->Type == kGroup)
                RenderGroup(reinterpret_cast<FEGroup *>(object), *reinterpret_cast<FEMatrix4 *>(identity), *reinterpret_cast<FEMatrix4 *>(view), 0);
            else
                RenderObject(object, *reinterpret_cast<FEMatrix4 *>(view), 0);
        }
        NFS_ORIGINAL(0x5b3850, void(__thiscall *)(int *))(&RenderListCount(this));
        reinterpret_cast<ObjectList>(table[kInterfaceObjectList])(game, RenderList(this), RenderListCount(this));
        reinterpret_cast<WithPackage>(table[kInterfacePackageEnd])(game, package);
    }
    engine[kDrewHidden] = engine[0];
}
