/**
 * The emitter system's world effects and life (original: zEcstasy EmitterSystem): its per-frame update, the libraries'
 * world effects (steam, fountains, sparks placed in the track) that start and stop with the camera's distance, the
 * triggered effects the player's cars run into, the libraries' loader and unloader, and the system's setup.
 *
 * A library (in its chunk): its key, a type (+8; 0 none, 0xa28 a group that always plays), its trigger count (+0xa), its
 * group while one plays (+0xc), its world matrix (+0x10, the position at +0x40), then its triggers, 0x30 each: a list
 * node, the library (+8), the radius (+0xc), the centre (+0x10), the seconds before it can fire again (+0x20), the chance
 * it fires (+0x24, of 0xffff; 0 never, 0xffff always), its state (+0x26: 1 armed, 2 fired, 3 waiting to rearm), its flags
 * (+0x27: 1 the car is inside, 2 the last roll fired) and when it fired (+0x28).
 */
#include "game/gen/api.h"
#include "legacy/link.h"

#include <cstring>
#include <new>

struct EmitterListNode;
void __fastcall DeleteGroupList(EmitterListNode *list, void *);
void __fastcall FreePointerList(EmitterListNode *list, void *);
void __fastcall UpdateInterestPoints(EmitterSystem *system, void *);

NFS_REPLACE(0x504210, TriggersAt)
NFS_REPLACE(0x5041a0, RearmTriggers)
NFS_REPLACE(0x50ce20, ServiceTriggers)

namespace
{
struct Node
{
    Node *next;
    Node *prev;
};

struct Trigger
{
    Node node;
    char *library;
    float radius;
    float centre[3];
    int unknown1c;
    float rearmSeconds;
    unsigned short chance;
    unsigned char state;
    unsigned char flags;
    float firedAt;
};
static_assert(sizeof(Trigger) == 0x2c, "WorldFXTrigger");

enum TriggerState : unsigned char
{
    kArmed = 1,
    kFired = 2,
    kRearming = 3,
};

const unsigned int kEmitterGroupClass = 0xaba86e60;
const unsigned short kAlwaysPlays = 0xa28;
const unsigned int kWorldEffectFlags = 0x8000000;
const unsigned int kAlwaysFlags = 0x8020000;
const unsigned int kTriggeredFlags = 0xc000000;
const int kMostTriggered = 67;
/** World effects start this much (m) further out than their groups' cull distance, and need the view's cone within. */
const float kStartMargin = 20.0f;
const float kStartCosine = 0.55f;

template <typename T> T &At(void *p, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(p) + offset);
}

char *System()
{
    return reinterpret_cast<char *>(&gEmitterSystem);
}

Node *TriggerList()
{
    return &At<Node>(System(), 0x380);
}

int &TriggerCount()
{
    return At<int>(System(), 0x388);
}

struct LibEntry
{
    unsigned int key;
    char *library;
};

/** The libraries' first entry (from +0x10 of a library chunk's header), and the next: 0x50 and its triggers. */
char *NextLibrary(char *library)
{
    return library + 0x50 + At<unsigned short>(library, 0xa) * 0x30;
}

void DestroyGroup(EmitterGroup *group)
{
    group->~EmitterGroup();
    bFree(EmitterGroupSlotPool, group);
}

/** A group for a library's effect, placed at the library's matrix and owned by it (NotifyLibOfDeletion clears it). */
EmitterGroup *StartLibraryEffect(char *library, unsigned int flags)
{
    if (!EnableParticleSystem)
        return nullptr;
    const Attrib::Collection *collection = Attrib::FindCollection(kEmitterGroupClass, At<unsigned int>(library, 0));
    if (!collection)
        return nullptr;
    EmitterGroup *group = gEmitterSystem.CreateEmitterGroup(collection, flags);
    if (!group)
        return nullptr;
    At<void *>(group, 0x60) = library;
    At<void(__cdecl *)(void *, EmitterGroup *)>(group, 0x6c) = &NotifyLibOfDeletion;
    At<EmitterGroup *>(library, 0xc) = group;
    group->SetLocalWorld(&At<bMatrix4>(library, 0x10));
    At<unsigned int>(group, 0x18) |= 1;
    return group;
}

unsigned int CountParticles(char *group)
{
    unsigned int n = 0;
    Node *head = &At<Node>(group, 8);
    for (Node *e = head->next; e != head; e = e->next)
        n += At<unsigned short>(e, 0x1c);
    return n;
}

/** Whether a group has emitters with live particles (the count is only looked at when it has emitters). */
bool HasParticles(char *group)
{
    return At<short>(group, 0x1c) && CountParticles(group);
}

/** An emitterdata/emittergroup attribute instance on the stack (Attrib::Gen::emittergroup, 0x14 bytes). */
struct GroupAttributes
{
    alignas(4) unsigned char storage[0x14];

    explicit GroupAttributes(const Attrib::Collection *collection)
    {
        new (storage) Attrib::Gen::emittergroup(collection, 0, nullptr);
    }
    ~GroupAttributes()
    {
        reinterpret_cast<Attrib::Instance *>(storage)->~Instance();
    }
    const char *Layout() const
    {
        return *reinterpret_cast<const char *const *>(storage + 8);
    }
};
} // namespace

/** The triggers a point is inside of that fire now (original sub_504210): they go out in `fired` (at most 67). */
int __cdecl TriggersAt(const float *point, char **fired)
{
    int count = 0;
    Node *head = TriggerList();
    for (Node *n = head->next; n != head; n = n->next)
    {
        Trigger &t = *reinterpret_cast<Trigger *>(n);
        if (t.state != kArmed)
            continue;
        const float dx = point[0] - t.centre[0], dy = point[1] - t.centre[1], dz = point[2] - t.centre[2];
        if (!(t.radius * t.radius > dx * dx + dy * dy + dz * dz))
        {
            t.flags &= 0xfc;
            continue;
        }
        if (t.flags & 1)
            continue;
        bool fire = false;
        if (t.chance)
        {
            if (t.chance == 0xffff)
                fire = true;
            else if (bRandom(0xffff, reinterpret_cast<unsigned int *>(0x8f3218)) > t.chance)
            {
                // As the original has it: a roll above the chance fires.
                t.flags |= 2;
                fire = true;
            }
            else
                t.flags &= 0xfd;
            if (fire)
                t.state = kFired;
        }
        t.flags |= 1;
        if (fire)
            fired[count++] = t.library;
    }
    return count;
}

/** Triggers whose effect has ended wait their rearm time, then are armed again (original sub_5041a0). */
void __cdecl RearmTriggers()
{
    const float now = static_cast<float>(*reinterpret_cast<const int *>(0x925ae8)) * 0.00025f;
    Node *head = TriggerList();
    for (Node *n = head->next; n != head; n = n->next)
    {
        Trigger &t = *reinterpret_cast<Trigger *>(n);
        if (t.state == kFired)
        {
            if (!At<void *>(t.library, 0xc))
            {
                t.state = kRearming;
                t.firedAt = now;
            }
        }
        else if (t.state == kRearming && now - t.firedAt > t.rearmSeconds)
            t.state = kArmed;
    }
}

/** The player cars' triggered effects (original sub_50ce20): where each car will be in 0.15 s, the libraries whose
 * triggers that point enters start their effect, one-shot where the effect's emitters are. */
void __cdecl ServiceTriggers()
{
    void **players = *reinterpret_cast<void ***>(0x92d87c);
    const int count = *reinterpret_cast<const int *>(0x92d884);
    typedef void *(__thiscall * Getter)(void *);
    for (int i = 0; i < count; ++i)
    {
        void *player = players[i];
        void *simable = (*reinterpret_cast<Getter **>(player))[1](player);
        if (!simable)
            continue;
        void *body = (*reinterpret_cast<Getter **>(simable))[0x54 / 4](simable);
        if (!body)
            continue;
        const float *position = static_cast<const float *>((*reinterpret_cast<Getter **>(body))[0x20 / 4](body));
        const float *velocity = static_cast<const float *>((*reinterpret_cast<Getter **>(body))[0x24 / 4](body));
        // The simulation's axes (x right, y up, z forward) as the world's.
        const float ahead[3] = {velocity[2] * 0.15f + position[2], -(velocity[0] * 0.15f + position[0]),
                                velocity[1] * 0.15f + position[1]};
        char *fired[kMostTriggered];
        const int n = TriggersAt(ahead, fired);
        for (int k = 0; k < n; ++k)
        {
            char *library = fired[k];
            if (At<void *>(library, 0xc))
                continue;
            EmitterGroup *group = StartLibraryEffect(library, kTriggeredFlags);
            if (!group)
                continue;
            Node *head = &At<Node>(group, 8);
            for (Node *e = head->next; e != head; e = e->next)
            {
                const char *data = At<char *>(e, 0x7c);
                if (*reinterpret_cast<void *const *>(data + 4) && (*reinterpret_cast<const char *const *>(data + 8))[0x112])
                    At<unsigned int>(e, 0x18) |= 1;
            }
        }
    }
    RearmTriggers();
}

EmitterSystem::EmitterSystem()
{
    char *self = reinterpret_cast<char *>(this);
    for (int i = 0; i < 0x42; ++i)
    {
        Node &list = At<Node>(self, 0x10c + i * 8);
        list.next = list.prev = &list;
    }
    // The emitter data and group maps (std::map): a head node from the map's allocator, linked to itself.
    const struct
    {
        int offset;
        unsigned int buyNode;
    } maps[2] = {{0x360, 0x5047f0}, {0x36c, 0x5048f0}};
    for (const auto &m : maps)
    {
        char *map = self + m.offset;
        char *head = static_cast<char *>(NFS_ORIGINAL(m.buyNode, void *(__thiscall *)(void *))(map));
        At<char *>(map, 4) = head;
        At<unsigned char>(head, 0x15) = 1;
        At<char *>(head, 4) = head;
        At<char *>(head, 0) = head;
        At<char *>(head, 8) = head;
        At<int>(map, 8) = 0;
    }
    const int lists[2] = {0x378, 0x380};
    for (int offset : lists)
    {
        Node &list = At<Node>(self, offset);
        list.next = list.prev = &list;
    }
    At<int>(self, 0x39c) = At<int>(self, 0x3a0) = At<int>(self, 0x3a4) = 0;
    At<int>(self, 0x390) = At<int>(self, 0x394) = At<int>(self, 0x38c) = 0;
    NFS_ORIGINAL(0x50b720, void(__thiscall *)(void *, unsigned int))(self + 0x398, 400);
    std::memset(self, 0, 0x43 * 4);
}

EmitterSystem::~EmitterSystem()
{
    char *self = reinterpret_cast<char *>(this);
    void *const fastMem = reinterpret_cast<void *>(0x925b30);
    typedef void(__thiscall * FastFree)(void *, void *, unsigned int, const char *);
    if (char *first = At<char *>(self, 0x39c))
        NFS_ORIGINAL(0x5d0370, FastFree)(fastMem, first, static_cast<unsigned int>((At<char *>(self, 0x3a4) - first) & ~7), nullptr);
    At<int>(self, 0x39c) = At<int>(self, 0x3a0) = At<int>(self, 0x3a4) = 0;
    Node *triggers = &At<Node>(self, 0x380);
    for (Node *n = triggers->next; n != triggers; n = triggers->next)
    {
        n->prev->next = n->next;
        n->next->prev = n->prev;
        NFS_ORIGINAL(0x652b00, void(__cdecl *)(void *))(n);
    }
    DeleteGroupList(reinterpret_cast<EmitterListNode *>(self + 0x378), nullptr);
    const struct
    {
        int offset;
        unsigned int erase;
    } maps[2] = {{0x36c, 0x50b3e0}, {0x360, 0x50b260}};
    for (const auto &m : maps)
    {
        char *map = self + m.offset;
        char *head = At<char *>(map, 4);
        void *result;
        NFS_ORIGINAL(m.erase, void(__thiscall *)(void *, void **, void *, void *))(map, &result, At<void *>(head, 0), head);
        if (At<char *>(map, 4))
            NFS_ORIGINAL(0x5d0370, FastFree)(fastMem, At<char *>(map, 4), 0x18, nullptr);
        At<int>(map, 4) = At<int>(map, 8) = 0;
    }
    for (int i = 0; i < 0x42; ++i)
        FreePointerList(reinterpret_cast<EmitterListNode *>(self + 0x10c + i * 8), nullptr);
}

void __cdecl EmitterSystem::Init()
{
    struct
    {
        SlotPool **pool;
        int size, count;
        const char *name;
    } const pools[3] = {{&ParticleSlotPool, 0x70, 0x400, "ParticleSlotPool"},
                        {&EmitterSlotPool, 0x90, 500, "EmitterSlotPool"},
                        {&EmitterGroupSlotPool, 0x80, 200, "EmitterGroupSlotPool"}};
    for (const auto &p : pools)
    {
        *p.pool = bNewSlotPool(p.size, p.count, p.name, 0);
        At<unsigned int>(*p.pool, 0x14) &= ~3u;
    }
}

void EmitterSystem::Update(float dt)
{
    char *self = reinterpret_cast<char *>(this);
    if (!EnableParticleSystem)
        return;
    ServiceWorldEffects();
    ServiceTriggers();
    ::UpdateInterestPoints(this, nullptr);
    Node *list = &At<Node>(self, 0x378);
    for (Node *g = list->next, *next; g != list; g = next)
    {
        next = g->next;
        char *group = reinterpret_cast<char *>(g);
        const unsigned int flags = At<unsigned int>(group, 0x18);
        if (flags & 1)
            reinterpret_cast<EmitterGroup *>(group)->Update(dt);
        else if ((flags & 0x40000000) && !(flags & 0x80000) && !HasParticles(group) && ++At<unsigned int>(group, 0x74) > 10)
            reinterpret_cast<EmitterGroup *>(group)->DeleteEmitters();
        if (!At<short>(group, 0x1c) && !(At<unsigned char>(group, 0x18) & 2))
        {
            DestroyGroup(reinterpret_cast<EmitterGroup *>(group));
            continue;
        }
        if ((At<unsigned int>(group, 0x18) & 0x80000) && !HasParticles(group))
            DestroyGroup(reinterpret_cast<EmitterGroup *>(group));
    }
    UpdateParticles(dt);
}

void EmitterSystem::ServiceWorldEffects()
{
    char *self = reinterpret_cast<char *>(this);
    LibEntry *first = At<LibEntry *>(self, 0x39c);
    for (LibEntry *it = first; it != At<LibEntry *>(self, 0x3a0); ++it)
    {
        char *library = it->library;
        const unsigned short type = At<unsigned short>(library, 8);
        if (!type || type == kAlwaysPlays)
            continue;
        GroupAttributes attributes(Attrib::FindCollection(kEmitterGroupClass, At<unsigned int>(library, 0)));
        const float *at = &At<float>(library, 0x40);
        const bVector3 position = {at[0], at[1], at[2]};
        const float reach = *reinterpret_cast<const float *>(attributes.Layout() + 0x84) + kStartMargin;
        if (IsCloseEnough(&position, reach, 1, kStartCosine))
        {
            if (!At<void *>(library, 0xc))
                StartLibraryEffect(library, kWorldEffectFlags);
        }
        else if (EmitterGroup *group = At<EmitterGroup *>(library, 0xc))
            DestroyGroup(group);
    }
}

void EmitterSystem::RefreshWorldEffects()
{
    char *self = reinterpret_cast<char *>(this);
    Node *triggers = &At<Node>(self, 0x380);
    for (Node *n = triggers->next; n != triggers; n = n->next)
    {
        reinterpret_cast<Trigger *>(n)->state = kArmed;
        reinterpret_cast<Trigger *>(n)->firedAt = 0.0f;
    }
    for (LibEntry *it = At<LibEntry *>(self, 0x39c); it != At<LibEntry *>(self, 0x3a0); ++it)
    {
        char *library = it->library;
        if (!At<void *>(library, 0xc) && At<unsigned short>(library, 8))
            StartLibraryEffect(library, kWorldEffectFlags);
    }
    NFS_ORIGINAL(0x5ebd90, void(__thiscall *)(void *))(*reinterpret_cast<void **>(0x91e00c));
}

int __cdecl EmitterSystem::Loader(bChunk *chunk)
{
    const unsigned int id = *reinterpret_cast<const unsigned int *>(chunk);
    if (id == 0x3bb00)
        return 1;
    if (id != 0x3bc00)
        return 0;
    char *header = reinterpret_cast<char *>((reinterpret_cast<unsigned int>(chunk) + 0x17) & ~0xfu);
    reinterpret_cast<EmitterLibraryHeader *>(header)->EndianSwap();
    char *self = System();
    char *library = header + 0x10;
    for (int i = 0; i < At<int>(header, 8); ++i, library = NextLibrary(library))
    {
        const unsigned short triggers = At<unsigned short>(library, 0xa);
        char *trigger = reinterpret_cast<char *>(reinterpret_cast<EmitterLibraryHeader *>(header)->GetLibraryTriggers(i));
        // Into the sorted table after any with the same key (std::upper_bound), the vector growing as it needs.
        LibEntry entry = {At<unsigned int>(library, 0), library};
        LibEntry *first = At<LibEntry *>(self, 0x39c), *last = At<LibEntry *>(self, 0x3a0);
        while (first != last)
        {
            LibEntry *mid = first + (last - first) / 2;
            if (entry.key < mid->key)
                last = mid;
            else
                first = mid + 1;
        }
        NFS_ORIGINAL(0x50b4a0, void(__thiscall *)(void *, LibEntry *, unsigned int, const LibEntry *))(self + 0x398, first, 1,
                                                                                                      &entry);
        const unsigned short type = At<unsigned short>(library, 8);
        if (!type)
            continue;
        if (!triggers)
        {
            if (type == kAlwaysPlays)
            {
                const unsigned int key = At<unsigned int>(library, 0);
                if (EmitterGroup *group = gEmitterSystem.CreateEmitterGroup(key, kAlwaysFlags))
                {
                    At<void *>(group, 0x60) = library;
                    At<void(__cdecl *)(void *, EmitterGroup *)>(group, 0x6c) = &NotifyLibOfDeletion;
                    At<EmitterGroup *>(library, 0xc) = group;
                    group->SetLocalWorld(&At<bMatrix4>(library, 0x10));
                    At<unsigned int>(group, 0x18) |= 1;
                }
            }
            continue;
        }
        for (int k = 0; k < triggers; ++k, trigger += 0x30)
        {
            Trigger &t = *reinterpret_cast<Trigger *>(trigger);
            t.state = kArmed;
            t.library = library;
            Node *list = TriggerList();
            Node *tail = list->prev;
            tail->next = &t.node;
            list->prev = &t.node;
            t.node.prev = tail;
            t.node.next = list;
            ++TriggerCount();
        }
    }
    return 1;
}

int __cdecl EmitterSystem::Unloader(bChunk *chunk)
{
    const unsigned int id = *reinterpret_cast<const unsigned int *>(chunk);
    char *header = reinterpret_cast<char *>((reinterpret_cast<unsigned int>(chunk) + 0x17) & ~0xfu);
    if (id == 0x3bb00)
    {
        // Groups built into the chunk: out of the system's list (their memory goes with the chunk).
        char *group = header + 0x10;
        for (int i = At<int>(header, 0xc); i > 0; --i, group += 0x80)
        {
            if (NFS_ORIGINAL(0x45d3a0, int(__thiscall *)(void *, void *))(System() + 0x378, group))
            {
                --At<int>(System(), 0x390);
                Node *n = reinterpret_cast<Node *>(group);
                n->prev->next = n->next;
                n->next->prev = n->prev;
            }
        }
        return 1;
    }
    if (id != 0x3bc00)
        return 0;
    char *library = header + 0x10;
    for (int i = 0; i < At<int>(header, 8); ++i, library = NextLibrary(library))
    {
        if (EmitterGroup *group = At<EmitterGroup *>(library, 0xc))
            DestroyGroup(group);
        Node *list = TriggerList();
        for (Node *n = list->next, *next; n != list; n = next)
        {
            next = n->next;
            if (reinterpret_cast<Trigger *>(n)->library == library)
            {
                n->prev->next = n->next;
                n->next->prev = n->prev;
                --TriggerCount();
            }
        }
        gEmitterSystem.RemoveLibrary(reinterpret_cast<EmitterLibrary *>(library));
    }
    return 1;
}
