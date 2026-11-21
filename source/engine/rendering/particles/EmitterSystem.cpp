/**
 * Making emitter groups (original: zEcstasy EmitterSystem::CreateEmitterGroup(Collection), EmitterGroup::SetEmitters,
 * EmitterSystem::GetEmitterData / GetEmitterGroup and the two attribute caches' clean-up, EmitterDataAttribWrapper's
 * constructor).
 *
 * A group's attributes (emittergroup, layout at +8): +0 the emitters' emitterdata references (0xc each, after an 8-byte
 * array header), +0x50 their life ranges (two floats each), +0x84 the cull distance. The system caches one attribute
 * wrapper per collection key in two maps (+0x360 emitterdata, +0x36c emittergroup; std::map, head node at +4).
 *
 * The system holds at most 500 emitters in 200 groups (+0x394, +0x390) outside NIS: a group that asks for it (flag
 * 0x40000) may kill disposable groups (0x8000000, neither kept 0x40000 nor protected 0x4000000) to make room, else
 * the first group that is not kept.
 */
#include <new>
#include <string.h>

#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x50c0e0, ClearEmitterDataCache)
NFS_REPLACE(0x50c120, ClearEmitterGroupCache)

void __fastcall ClearEmitterDataCache(void *map);
void __fastcall ClearEmitterGroupCache(void *map);

namespace
{
template <typename T> T &At(void *p, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(p) + offset);
}

template <typename T> const T &At(const void *p, int offset)
{
    return *reinterpret_cast<const T *>(static_cast<const char *>(p) + offset);
}

struct Node
{
    Node *next;
    Node *prev;
};

void AddTail(Node *list, Node *n)
{
    Node *tail = list->prev;
    tail->next = n;
    list->prev = n;
    n->prev = tail;
    n->next = list;
}

const unsigned int kMaxEmitters = 500;
const unsigned int kMaxGroups = 200;
const unsigned int kNoGroupCollection = 0xeec2271a;

enum GroupFlags : unsigned int
{
    kMayKill = 0x40000,
    kKeep = 0x40000,
    kProtected = 0x4000000,
    kDisposable = 0x8000000,
};

void *FastMemAlloc(unsigned int size)
{
    return NFS_ORIGINAL(0x5d29d0, void *(__thiscall *)(void *, unsigned int, const char *))(
        reinterpret_cast<void *>(0x925b30), size, nullptr);
}

void FastMemFree(void *block, unsigned int size)
{
    NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
        reinterpret_cast<void *>(0x925b30), block, size, nullptr);
}

/** An Attrib::Gen instance (emitterdata or emittergroup, 0x14 bytes) on the stack. */
template <typename Gen> struct Attributes
{
    alignas(4) unsigned char storage[0x14];

    explicit Attributes(const Attrib::Collection *collection)
    {
        new (storage) Gen(collection, 0, nullptr);
    }
    ~Attributes()
    {
        reinterpret_cast<Attrib::Instance *>(storage)->~Instance();
    }
    Attrib::Instance &Instance()
    {
        return *reinterpret_cast<Attrib::Instance *>(storage);
    }
    const char *Layout() const
    {
        return *reinterpret_cast<const char *const *>(storage + 8);
    }
};

/** An attribute array's length (Attrib::Private: its header). */
unsigned int Length(const char *array)
{
    return reinterpret_cast<const Attrib::Private *>(array)->GetLength();
}

/** The `i`th emitterdata reference of a group layout, or the default data when past the end. */
const Attrib::Collection *EmitterCollection(const char *layout, unsigned int i)
{
    const void *spec = i < Length(layout) ? layout + 8 + i * 0xc : Attrib::DefaultDataArea(0xc);
    return static_cast<const Attrib::RefSpec *>(spec)->GetCollection();
}

void DestroyGroup(EmitterGroup *group)
{
    group->~EmitterGroup();
    bFree(EmitterGroupSlotPool, group);
}

/** std::map<unsigned int, T *> (UTL allocator) of the system: find, operator[] and the range erase, thiscall. */
struct MapCalls
{
    unsigned int find;
    unsigned int index;
    unsigned int erase;
};

const MapCalls kDataMap = {0x5081f0, 0x50bc20, 0x50b260};
const MapCalls kGroupMap = {0x508260, 0x50bce0, 0x50b3e0};

/** The cached wrapper for `key`, or nullptr (*found false) when the map has none. */
void *MapFind(const MapCalls &calls, char *map, unsigned int key, bool *found)
{
    void *node = nullptr;
    reinterpret_cast<void(__thiscall *)(void *, void **, const unsigned int *)>(calls.find)(map, &node, &key);
    *found = node != At<void *>(map, 4);
    return *found ? At<void *>(node, 0x10) : nullptr;
}

void MapStore(const MapCalls &calls, char *map, unsigned int key, void *value)
{
    *reinterpret_cast<void **(__thiscall *)(void *, const unsigned int *)>(calls.index)(map, &key) = value;
}

void MapClear(const MapCalls &calls, char *map)
{
    void *head = At<void *>(map, 4);
    void *result = nullptr;
    reinterpret_cast<void(__thiscall *)(void *, void **, void *, void *)>(calls.erase)(map, &result,
                                                                                       *static_cast<void **>(head), head);
    if (At<void *>(map, 4))
        FastMemFree(At<void *>(map, 4), 0x18);
    At<void *>(map, 4) = nullptr;
    At<int>(map, 8) = 0;
}
} // namespace

EmitterDataAttribWrapper::EmitterDataAttribWrapper(const Attrib::Collection *collection)
{
    char *self = reinterpret_cast<char *>(this);
    new (self) Attrib::Gen::emitterdata(collection, 0, nullptr);
    memset(self + 0x20, 0, 0x40);
    memset(self + 0x60, 0, 0x40);
    CalculateBases();
}

EmitterDataAttribWrapper *EmitterSystem::GetEmitterData(const Attrib::Collection *collection)
{
    char *map = reinterpret_cast<char *>(this) + 0x360;
    const unsigned int key = Attrib::GetCollectionKey(collection);
    bool found;
    if (void *cached = MapFind(kDataMap, map, key, &found))
        return static_cast<EmitterDataAttribWrapper *>(cached);
    if (found)
        return nullptr;
    EmitterDataAttribWrapper *wrapper = nullptr;
    if (void *memory = FastMemAlloc(0xa0))
        wrapper = new (memory) EmitterDataAttribWrapper(collection);
    MapStore(kDataMap, map, key, wrapper);
    return wrapper;
}

EmitterGroupAttribWrapper *EmitterSystem::GetEmitterGroup(const Attrib::Collection *collection)
{
    char *map = reinterpret_cast<char *>(this) + 0x36c;
    const unsigned int key = Attrib::GetCollectionKey(collection);
    bool found;
    if (void *cached = MapFind(kGroupMap, map, key, &found))
        return static_cast<EmitterGroupAttribWrapper *>(cached);
    if (found)
        return nullptr;
    EmitterGroupAttribWrapper *wrapper = nullptr;
    if (void *memory = FastMemAlloc(0x14))
    {
        new (memory) Attrib::Gen::emittergroup(collection, 0, nullptr);
        wrapper = static_cast<EmitterGroupAttribWrapper *>(memory);
    }
    MapStore(kGroupMap, map, key, wrapper);
    return wrapper;
}

void __fastcall ClearEmitterDataCache(void *map)
{
    MapClear(kDataMap, static_cast<char *>(map));
}

void __fastcall ClearEmitterGroupCache(void *map)
{
    MapClear(kGroupMap, static_cast<char *>(map));
}

bool EmitterGroup::SetEmitters(unsigned int)
{
    char *self = reinterpret_cast<char *>(this);
    char *wrapper = At<char *>(self, 0x70);
    if (!At<void *>(wrapper, 4))
        return false;
    const int count = static_cast<int>(Length(At<char *>(wrapper, 8)));
    const bool hasRanges = Length(At<char *>(wrapper, 8) + 0x50) == static_cast<unsigned int>(count);
    if (!count)
        return false;
    for (int i = 0; i < count; ++i)
    {
        const Attrib::Collection *collection = EmitterCollection(At<char *>(wrapper, 8), i);
        if (!collection)
            continue;
        Attributes<Attrib::Gen::emitterdata> data(collection);
        Emitter *emitter = nullptr;
        if (void *memory = bOMalloc(EmitterSlotPool))
            emitter = new (memory) Emitter(collection, this);
        if (!emitter)
        {
            *reinterpret_cast<bool *>(0x91c1a0) = true;
            return false;
        }
        char *e = reinterpret_cast<char *>(emitter);
        memcpy(e + 0x20, self + 0x20, 16 * sizeof(float));
        if (hasRanges)
        {
            const char *layout = At<char *>(wrapper, 8);
            const float *range = static_cast<const float *>(
                static_cast<unsigned int>(i) < Length(layout + 0x50) ? layout + 0x58 + i * 8 : Attrib::DefaultDataArea(8));
            float low = range[0], high = range[1];
            if (low == high || high < low)
            {
                low = 0.0f;
                high = 1.0f;
            }
            At<float>(e, 0x70) = low;
            At<float>(e, 0x74) = high;
        }
        else
        {
            At<float>(e, 0x70) = 0.0f;
            At<float>(e, 0x74) = 999999.0f;
        }
        AddTail(&At<Node>(self, 8), reinterpret_cast<Node *>(e));
        ++At<short>(self, 0x1c);
    }
    At<int>(self, 0x64) = At<int>(At<char *>(wrapper, 8), 0x84);
    return true;
}

EmitterGroup *EmitterSystem::CreateEmitterGroup(const Attrib::Collection *collection, unsigned int flags)
{
    char *self = reinterpret_cast<char *>(this);
    if (!EnableParticleSystem)
        return nullptr;
    const bool inNis = IsInNIS;
    if (!collection)
        return nullptr;
    Attributes<Attrib::Gen::emittergroup> attributes(collection);
    const unsigned int key = attributes.Instance().GetCollection();
    if (key == kNoGroupCollection || !key)
        return nullptr;

    // Every emitter it names must exist.
    const char *layout = attributes.Layout();
    const unsigned int emitters = Length(layout);
    for (unsigned int i = 0; i < emitters; ++i)
        if (!EmitterCollection(layout, i))
            return nullptr;

    unsigned int &groupCount = At<unsigned int>(self, 0x390);
    unsigned int &emitterCount = At<unsigned int>(self, 0x394);
    Node *groups = &At<Node>(self, 0x378);
    const bool full = !(emitterCount + emitters <= kMaxEmitters && groupCount < kMaxGroups);
    if (full && (flags & kMayKill))
    {
        int needed = static_cast<int>(emitterCount + emitters - kMaxEmitters);
        int groupsNeeded = 1;
        bool done = false;
        for (Node *g = groups->next; g != groups && !done;)
        {
            Node *next = g->next;
            const unsigned int groupFlags = At<unsigned int>(g, 0x18);
            if ((groupFlags & kDisposable) && !(groupFlags & kKeep) && !(groupFlags & kProtected))
            {
                needed -= At<unsigned short>(g, 0x1c);
                --groupsNeeded;
                DestroyGroup(reinterpret_cast<EmitterGroup *>(g));
                if (needed <= 0 && groupsNeeded <= 0)
                    done = true;
            }
            g = next;
        }
        if (!done)
            for (Node *g = groups->next; g != groups;)
            {
                Node *next = g->next;
                if (!(At<unsigned int>(g, 0x18) & kKeep))
                {
                    DestroyGroup(reinterpret_cast<EmitterGroup *>(g));
                    break;
                }
                g = next;
            }
    }

    EmitterGroup *group = nullptr;
    if (inNis || (emitterCount + emitters <= kMaxEmitters && groupCount < kMaxGroups))
        if (void *memory = bOMalloc(EmitterGroupSlotPool))
            group = new (memory) EmitterGroup(collection, flags);
    if (!group)
    {
        *reinterpret_cast<bool *>(0x91c1a1) = true;
        return nullptr;
    }
    group->Enable();
    bVector3 still;
    still.x = still.y = still.z = 0.0f;
    group->SetInheritVelocity(&still);
    if (char *library = reinterpret_cast<char *>(FindLibrary(attributes.Instance().GetCollection())))
        group->SetLocalWorld(reinterpret_cast<const bMatrix4 *>(library + 0x10));
    ++groupCount;
    emitterCount += emitters;
    AddTail(groups, reinterpret_cast<Node *>(group));
    return group;
}
