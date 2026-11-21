/**
 * Front-end objects changed at run time (interface/engine/feng.h).
 */
#include <new>
#include <string.h>

#include "game/gen/api.h"
#include "interface/engine/feng.h"
#include "legacy/link.h"

static_assert(sizeof(FEObject) == 0x5C, "FEObject layout");
static_assert(sizeof(FEScript) == 0x34, "FEScript layout");
static_assert(sizeof(FEKeyTrack) == 0x38, "FEKeyTrack layout");
static_assert(sizeof(FEObjData) == 0x44 && sizeof(FEImageData) == 0x54, "FEObjData layout");

/** FEScript::NodePool, the ObjectPool<FEScript, 32> that every script comes from. */
struct FEScriptPool
{
    FEScript *AllocSingle();
};
extern FEScriptPool g_FEScriptPool;
NFS_BIND(0x91dd0c, g_FEScriptPool)
NFS_BIND(0x5c1c40, FEScriptPool::AllocSingle)

namespace nfs::feng
{
namespace
{
/** InterpAction bit of a track whose keys are relative to the value the object has when the script starts. */
const unsigned char kRelativeTrack = 0x80;
/** FEObject::Flags bit a string sets when its text changes (FEString::SetString). */
const unsigned int kTextChanged = 0x400000;
/** FEObject::Clone(bool), the second virtual function of every object type. */
const int kCloneSlot = 1;

float *DataComponent(FEObject *object, Track track, int component)
{
    const unsigned int offset = object->GetDataOffset(static_cast<FEKeyTrack_Indices>(track));
    return reinterpret_cast<float *>(reinterpret_cast<char *>(object->pData) + offset) + component;
}

template <typename Change> void ChangeTrack(FEObject *object, Track track, int component, Change change)
{
    for (FEScript *script = object->FirstScript; script; script = script->Next)
    {
        FEKeyTrack *key_track = script->FindTrack(static_cast<FEKeyTrack_Indices>(track));
        if (!key_track)
            continue;
        change(key_track->BaseValue[component]);
        key_track->InterpAction &= static_cast<unsigned char>(~kRelativeTrack);
    }
    change(*DataComponent(object, track, component));
}
} // namespace

void SetTrack(FEObject *object, Track track, int component, float value)
{
    if (object)
        ChangeTrack(object, track, component, [value](float &v) { v = value; });
}

void ScaleTrack(FEObject *object, Track track, int component, float factor)
{
    if (object)
        ChangeTrack(object, track, component, [factor](float &v) { v *= factor; });
}

void StretchImageY(FEObject *image, float center_y, float height)
{
    if (!image || image->pData->Size[1] == 0.0f)
        return;
    const float factor = height / image->pData->Size[1];
    ScaleTrack(image, kUpperLeft, 1, factor);
    ScaleTrack(image, kLowerRight, 1, factor);
    ScaleTrack(image, kSize, 1, factor);
    SetTrack(image, kPosition, 1, center_y);
}

namespace
{
void ClearRenderCache(FEObject *object)
{
    object->Cached = nullptr;
    if (object->Type == 5)
        for (FEObject *o = *reinterpret_cast<FEObject **>(reinterpret_cast<char *>(object) + 0x64); o; o = o->Next)
            ClearRenderCache(o);
}
} // namespace

FEObject *Clone(FEObject *source, FEObject *group, unsigned int name_hash)
{
    if (!source || !group)
        return nullptr;
    using CloneFunction = FEObject *(__thiscall *)(FEObject *, bool);
    const CloneFunction clone = reinterpret_cast<CloneFunction>(static_cast<void **>(source->VTable)[kCloneSlot]);
    FEObject *copy = clone(source, false);
    if (!copy)
        return nullptr;
    // FEObject's copy constructor leaves the render cache unset (in a group's copied children too) and the data zeroed
    // for the scripts to fill in.
    ClearRenderCache(copy);
    memcpy(copy->pData, source->pData, source->DataSize);
    copy->NameHash = name_hash;
    reinterpret_cast<FEGroup *>(group)->AddObject(copy);
    return copy;
}

FEScript *FindOrCloneScript(FEObject *object, unsigned int id, FEScript *model)
{
    if (FEScript *script = object->FindScript(id))
        return script;
    if (!model)
        return nullptr;
    // As FEObject's copy constructor copies scripts: a pool slot with the fields the constructor frees cleared first.
    FEScript *script = g_FEScriptPool.AllocSingle();
    script->pName = nullptr;
    script->ID = 0;
    script->Length = 0;
    script->Flags = 0;
    script->pChainTo = nullptr;
    script->TrackCount = 0;
    script->pTracks = nullptr;
    new (script) FEScript(*model, false);
    script->ID = id;
    script->Next = nullptr;
    script->Prev = object->LastScript;
    if (object->LastScript)
        object->LastScript->Next = script;
    else
        object->FirstScript = script;
    object->LastScript = script;
    ++object->ScriptCount;
    return script;
}

void SetTextFormat(FEString *string, unsigned int format, unsigned int wrap_width)
{
    if (!string)
        return;
    // FEString after its FEObject: the label and its hash, the text (two words), then format, leading and width.
    struct Text
    {
        unsigned int format;
        int leading;
        unsigned int width;
    };
    Text &text = *reinterpret_cast<Text *>(reinterpret_cast<char *>(string) + 0x6C);
    if (text.format == format && text.width == wrap_width)
        return;
    text.format = format;
    text.width = wrap_width;
    reinterpret_cast<FEObject *>(string)->Flags |= kTextChanged;
}

void Reserve(FEString *string, unsigned int characters)
{
    if (string)
        reinterpret_cast<FEWideString *>(reinterpret_cast<char *>(string) + 0x64)->SetLength(characters);
}

void LimitScriptFade(FEObject *object, unsigned int script_id, int least_alpha)
{
    FEScript *script = object ? object->FindScript(script_id) : nullptr;
    FEKeyTrack *track = script ? script->FindTrack(static_cast<FEKeyTrack_Indices>(kColor)) : nullptr;
    if (!track)
        return;
    // FEKeyTrack: the base key at +8 (its value at +0x18), then whether the keys are a list shared with other objects,
    // and the keys. A key (FEKeyNode): next, previous, time, value; a colour's alpha is the fourth number, added to
    // the base's.
    struct Key
    {
        void *vtable;
        Key *next;
        Key *previous;
        int time;
        int value[4];
    };
    const char *raw = reinterpret_cast<const char *>(track);
    const int base_alpha = reinterpret_cast<const Key *>(raw + 8)->value[3];
    void *keys = *reinterpret_cast<void *const *>(raw + 0x30);
    Key *key = raw[0x2c] ? reinterpret_cast<Key *>(static_cast<FERefList *>(keys)->GetHead()) : static_cast<Key *>(keys);
    for (int guard = 0; key && guard < 64; key = key->next, ++guard)
        if (base_alpha + key->value[3] < least_alpha)
            key->value[3] = least_alpha - base_alpha;
}

void SetColour(FEObject *object, int red, int green, int blue, int alpha)
{
    if (!object)
        return;
    // Colours are kept blue first.
    const int colour[4] = {blue, green, red, alpha};
    for (FEScript *script = object->FirstScript; script; script = script->Next)
        if (FEKeyTrack *track = script->FindTrack(static_cast<FEKeyTrack_Indices>(kColor)))
        {
            // A colour track's keys are whole numbers.
            for (int i = 0; i < 4; ++i)
                reinterpret_cast<int *>(track->BaseValue)[i] = colour[i];
        }
    for (int i = 0; i < 4; ++i)
        object->pData->Col[i] = colour[i];
}

void GetColour(FEObject *object, int colour[4])
{
    // The script the object runs has the colour it shows; its data only takes it when the script has run (a package
    // just loaded still holds the file's).
    const int *stored = object->pData->Col;
    FEScript *script = object->pCurrentScript ? object->pCurrentScript : object->FirstScript;
    if (FEKeyTrack *track = script ? script->FindTrack(static_cast<FEKeyTrack_Indices>(kColor)) : nullptr)
        stored = reinterpret_cast<const int *>(track->BaseValue);
    colour[0] = stored[2];
    colour[1] = stored[1];
    colour[2] = stored[0];
    colour[3] = stored[3];
}

void SetPosition(FEObject *object, float x, float y)
{
    SetTrack(object, kPosition, 0, x);
    SetTrack(object, kPosition, 1, y);
}

FEObject *FindByGuid(const char *package, unsigned int guid)
{
    FEPackage *loaded = package ? cFEng::mInstance->FindPackage(package) : nullptr;
    return loaded ? loaded->FindObjectByGUID(guid) : nullptr;
}
} // namespace nfs::feng
