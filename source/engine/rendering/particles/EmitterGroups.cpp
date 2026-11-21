/**
 * Emitter groups and the emitter system's bookkeeping (original: zEcstasy): creating and destroying emitters, their
 * particle lists, the groups' enable/one-shot/world matrix, the per-frame group update, a new particle (making room
 * by killing a group when the pool is full), the distance and view-cone test, the effect libraries' sorted table and
 * the NIS start/end clean-ups.
 *
 * Layouts used here:
 *  Emitter (0x90): next, prev, EmitterControl at +8 (state, timer), +0x14 seed, +0x18 flags, +0x1c particle count,
 *   +0x1e texture slot, +0x20 local world, +0x60 inherited velocity, +0x70/+0x74 life range, +0x7c data,
 *   +0x80 particle list, +0x88 its node in a draw list, +0x8c group.
 *  EmitterGroup: next, prev, +8 emitter list, +0x18 flags, +0x1c emitter count, +0x20 local world, +0x60 deletion
 *   owner, +0x64 cull distance, +0x68 intensity, +0x6c deletion callback, +0x70 attributes, +0x78 creation tick.
 *  EmitterSystem: +0 particle count, +4 per-slot counts, +0x320 interest points, +0x340 their directions,
 *   +0x378 group list, +0x390 group count, +0x394 emitter count, +0x39c/+0x3a0 library table (key, library).
 */
#include "game/gen/api.h"
#include "legacy/link.h"

#include <cstring>

void __cdecl SetParticleStats(int a, int b, int c, int d, int e, int f, int g, int h);

NFS_REPLACE(0x506440, DeleteParticles)
NFS_REPLACE(0x506480, DeleteParticles)
NFS_REPLACE(0x5081b0, DeleteEmitterList)
NFS_REPLACE(0x50a600, DeleteGroupList)
NFS_REPLACE(0x507e60, FreePointerList)
NFS_REPLACE(0x507f90, InitList)

/** A bList node: the emitter lists' links. */
struct EmitterListNode
{
    EmitterListNode *next;
    EmitterListNode *prev;
};

namespace
{
using Node = EmitterListNode;

enum GroupFlags : unsigned int
{
    kEnabled = 0x10,
    kAlwaysClose = 0x20000,
    kKeep = 0x40000,
    kDeleteWhenEmpty = 0x80000,
    kProtected = 0x4000000,
    kDisposable = 0x8000000,
};

enum EmitterFlags : unsigned int
{
    kOneShot = 0x1,
    kParticlesDying = 0x8,
    kEmitterEnabled = 0x10,
};

const int kStateDone = 6;
const int kParticleDying = 0x1;

template <typename T> T &At(void *p, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(p) + offset);
}

char *System()
{
    return reinterpret_cast<char *>(&gEmitterSystem);
}

int &GroupCount()
{
    return At<int>(System(), 0x390);
}

int &EmitterCount()
{
    return At<int>(System(), 0x394);
}

Node *GroupList()
{
    return &At<Node>(System(), 0x378);
}

void Unlink(Node *n)
{
    n->prev->next = n->next;
    n->next->prev = n->prev;
}

void Append(Node *head, Node *n)
{
    Node *tail = head->prev;
    tail->next = n;
    head->prev = n;
    n->prev = tail;
    n->next = head;
}

Node *Emitters(void *group)
{
    return &At<Node>(group, 8);
}

Node *Particles(void *emitter)
{
    return &At<Node>(emitter, 0x80);
}

/** bList::TraversebList (private, so called by address): whether a node is in the list. */
bool InList(Node *list, void *node)
{
    return NFS_ORIGINAL(0x45d3a0, int(__thiscall *)(Node *, void *))(list, node) != 0;
}

unsigned int AttribLength(const void *layout)
{
    return NFS_ORIGINAL(0x452940, unsigned int(__thiscall *)(const void *))(layout);
}

/** Marks an emitter's particles as dying once (they fade out instead of vanishing). */
void LetParticlesDie(char *emitter)
{
    if (At<unsigned int>(emitter, 0x18) & kParticlesDying)
        return;
    Node *head = Particles(emitter);
    for (Node *p = head->next; p != head; p = p->next)
        At<unsigned int>(p, 0x5c) |= kParticleDying;
    At<unsigned int>(emitter, 0x18) |= kParticlesDying;
}

void DestroyEmitter(Emitter *emitter)
{
    emitter->~Emitter();
    bFree(EmitterSlotPool, emitter);
}

void DestroyGroup(EmitterGroup *group)
{
    group->~EmitterGroup();
    bFree(EmitterGroupSlotPool, group);
}

/** A group's emitter, removed from it and freed. */
void DropEmitter(char *group, Node *emitter)
{
    Unlink(emitter);
    --At<short>(group, 0x1c);
    DestroyEmitter(reinterpret_cast<Emitter *>(emitter));
    --EmitterCount();
}

struct LibEntry
{
    unsigned int key;
    EmitterLibrary *library;
};

LibEntry *LowerBound(LibEntry *first, LibEntry *last, unsigned int key)
{
    int count = static_cast<int>(last - first);
    while (count > 0)
    {
        const int half = count / 2;
        if (first[half].key < key)
        {
            first += half + 1;
            count -= half + 1;
        }
        else
            count = half;
    }
    return first;
}
} // namespace

void __fastcall DeleteParticles(Node *list, void *)
{
    for (Node *p = list->next; p != list; p = list->next)
    {
        Unlink(p);
        bFree(ParticleSlotPool, p);
    }
}

void __fastcall DeleteEmitterList(Node *list, void *)
{
    for (Node *e = list->next; e != list; e = list->next)
    {
        Unlink(e);
        DestroyEmitter(reinterpret_cast<Emitter *>(e));
    }
}

void __fastcall DeleteGroupList(Node *list, void *)
{
    for (Node *g = list->next; g != list; g = list->next)
    {
        Unlink(g);
        DestroyGroup(reinterpret_cast<EmitterGroup *>(g));
    }
}

void __fastcall FreePointerList(Node *list, void *)
{
    for (Node *n = list->next; n != list; n = list->next)
    {
        Unlink(n);
        bPNode::Free(n);
    }
}

Node *__fastcall InitList(Node *list, void *)
{
    list->next = list->prev = list;
    return list;
}

Emitter::Emitter(const Attrib::Collection *collection, EmitterGroup *group)
{
    char *self = reinterpret_cast<char *>(this);
    At<int>(self, 8) = 1;
    At<int>(self, 0xc) = 0;
    InitList(Particles(self), nullptr);
    At<EmitterDataAttribWrapper *>(self, 0x7c) = gEmitterSystem.GetEmitterData(collection);
    At<int>(self, 0x10) = 0;
    At<int>(self, 0x14) = 0;
    float *world = &At<float>(self, 0x20);
    std::memset(world, 0, 16 * sizeof(float));
    world[0] = world[5] = world[10] = world[15] = 1.0f;
    At<int>(self, 0x14) = static_cast<int>(bRandom(2147483648.0f, reinterpret_cast<unsigned int *>(0x8f29e8)));
    At<unsigned int>(self, 0x18) = At<unsigned int>(group, 0x18) & 0xffff0000u;
    At<float>(self, 0x70) = 0.0f;
    At<float>(self, 0x74) = 1.0f;
    At<EmitterGroup *>(self, 0x8c) = group;
    At<void *>(self, 0x88) = nullptr;
    At<short>(self, 0x1c) = 0;
    const char *data = *reinterpret_cast<const char *const *>(At<char *>(self, 0x7c) + 8);
    At<int>(self, 0x78) = 0;
    At<short>(self, 0x1e) = static_cast<short>(*reinterpret_cast<const short *>(data + 0x9c) + 2);
}

Emitter::~Emitter()
{
    char *self = reinterpret_cast<char *>(this);
    DeleteParticles(Particles(self), nullptr);
    if (Node *drawNode = At<Node *>(self, 0x88))
    {
        Unlink(drawNode);
        bPNode::Free(drawNode);
        At<Node *>(self, 0x88) = nullptr;
    }
    DeleteParticles(Particles(self), nullptr);
}

EmitterGroup::EmitterGroup(const Attrib::Collection *collection, unsigned int flags)
{
    char *self = reinterpret_cast<char *>(this);
    InitList(Emitters(self), nullptr);
    At<EmitterGroupAttribWrapper *>(self, 0x70) = gEmitterSystem.GetEmitterGroup(collection);
    At<short>(self, 0x1c) = 0;
    At<float>(self, 0x68) = 1.0f;
    At<unsigned int>(self, 0x18) = flags | kEnabled;
    float *world = &At<float>(self, 0x20);
    std::memset(world, 0, 16 * sizeof(float));
    world[0] = world[5] = world[10] = world[15] = 1.0f;
    At<void *>(self, 0x60) = nullptr;
    At<void *>(self, 0x6c) = nullptr;
    At<int>(self, 0x74) = 0;
    At<unsigned int>(self, 0x78) = bGetTicker();
    At<int>(self, 0x7c) = 0;
    if (collection)
    {
        if (SetEmitters(flags))
            At<unsigned int>(self, 0x18) |= 0x8;
        else
            At<unsigned int>(self, 0x18) &= ~0x8u;
    }
}

EmitterGroup::~EmitterGroup()
{
    char *self = reinterpret_cast<char *>(this);
    UnloadEmitters(true);
    if (InList(GroupList(), self))
    {
        --GroupCount();
        Unlink(reinterpret_cast<Node *>(self));
    }
    void *owner = At<void *>(self, 0x60);
    void(__cdecl *callback)(void *, EmitterGroup *) = At<void(__cdecl *)(void *, EmitterGroup *)>(self, 0x6c);
    if (owner && callback)
        callback(owner, this);
    DeleteEmitterList(Emitters(self), nullptr);
}

void EmitterGroup::Enable()
{
    Node *head = Emitters(this);
    for (Node *e = head->next; e != head; e = e->next)
        At<unsigned int>(e, 0x18) |= kEmitterEnabled;
    At<unsigned int>(this, 0x18) |= kEnabled;
}

void EmitterGroup::Disable()
{
    Node *head = Emitters(this);
    for (Node *e = head->next; e != head; e = e->next)
        At<unsigned int>(e, 0x18) &= ~static_cast<unsigned int>(kEmitterEnabled);
    At<unsigned int>(this, 0x18) &= ~static_cast<unsigned int>(kEnabled);
}

unsigned int EmitterGroup::GetNumParticles()
{
    unsigned int count = 0;
    if (!At<short>(this, 0x1c))
        return 0;
    Node *head = Emitters(this);
    for (Node *e = head->next; e != head; e = e->next)
        count += At<unsigned short>(e, 0x1c);
    return count;
}

void EmitterGroup::SetInheritVelocity(const bVector3 *velocity)
{
    Node *head = Emitters(this);
    for (Node *e = head->next; e != head; e = e->next)
    {
        float *v = &At<float>(e, 0x60);
        v[0] = velocity->x;
        v[1] = velocity->y;
        v[2] = velocity->z;
    }
}

void EmitterGroup::SetLocalWorld(const bMatrix4 *world)
{
    std::memcpy(&At<char>(this, 0x20), world, 0x40);
    Node *head = Emitters(this);
    for (Node *e = head->next; e != head; e = e->next)
        std::memcpy(&At<char>(e, 0x20), world, 0x40);
}

bool EmitterGroup::MakeOneShot(bool force)
{
    bool any = false;
    Node *head = Emitters(this);
    for (Node *e = head->next; e != head; e = e->next)
    {
        if (!force)
        {
            const char *data = At<char *>(e, 0x7c);
            if (!*reinterpret_cast<void *const *>(data + 4) || !(*reinterpret_cast<const char *const *>(data + 8))[0x112])
                continue;
            any = true;
        }
        At<unsigned int>(e, 0x18) |= kOneShot;
    }
    return force || any;
}

void EmitterGroup::DeleteEmitters()
{
    char *self = reinterpret_cast<char *>(this);
    Node *head = Emitters(self);
    while (head->next != head)
        DropEmitter(self, head->prev);
}

void EmitterGroup::UnloadEmitters(bool killParticles)
{
    char *self = reinterpret_cast<char *>(this);
    Node *head = Emitters(self);
    while (head->next != head)
    {
        Node *e = head->next;
        Unlink(e);
        if (killParticles)
            gEmitterSystem.KillParticlesFromThisEmitter(reinterpret_cast<Emitter *>(e));
        else
            LetParticlesDie(reinterpret_cast<char *>(e));
        DestroyEmitter(reinterpret_cast<Emitter *>(e));
        --At<short>(self, 0x1c);
        --EmitterCount();
    }
}

void EmitterGroup::Update(float dt)
{
    char *self = reinterpret_cast<char *>(this);
    if (!EnableParticleSystem)
        return;
    const unsigned int flags = At<unsigned int>(self, 0x18);
    if (!(flags & kEnabled))
        return;
    bool close = true;
    if (!(flags & kAlwaysClose))
        close = gEmitterSystem.IsCloseEnough(&At<bVector3>(self, 0x50), At<float>(self, 0x64), ~(flags >> 18) & 1, 0.7f);
    Node *head = Emitters(self);
    for (Node *e = head->next, *next; e != head; e = next)
    {
        next = e->next;
        float overshoot = 0.0f;
        bool on = false;
        if (EnableParticleSystem && (At<unsigned int>(e, 0x18) & kEmitterEnabled))
            on = reinterpret_cast<EmitterControl *>(&At<char>(e, 8))->Update(dt, reinterpret_cast<Emitter *>(e), overshoot);
        const int state = At<int>(e, 8);
        if (on && close)
        {
            const float time = overshoot + dt;
            if (time > 0.0f)
                reinterpret_cast<Emitter *>(e)->SpawnParticles(time, At<float>(self, 0x68));
        }
        if (state == kStateDone)
        {
            LetParticlesDie(reinterpret_cast<char *>(e));
            if (!At<short>(e, 0x1c))
            {
                DropEmitter(self, e);
                continue;
            }
        }
        if ((At<unsigned int>(self, 0x18) & kDeleteWhenEmpty) && !At<short>(e, 0x1c))
            DropEmitter(self, e);
    }
}

void EmitterSystem::AddEmitterGroup(EmitterGroup *group)
{
    char *self = reinterpret_cast<char *>(this);
    Node *list = &At<Node>(self, 0x378);
    if (InList(list, group))
        return;
    Append(list, reinterpret_cast<Node *>(group));
    ++At<int>(self, 0x390);
    const char *attributes = At<char *>(group, 0x70);
    At<int>(self, 0x394) += static_cast<int>(AttribLength(*reinterpret_cast<const void *const *>(attributes + 8)));
}

void EmitterSystem::KillEffectsMatchingFlag(unsigned int flags)
{
    Node *list = &At<Node>(this, 0x378);
    for (Node *g = list->next, *next; g != list; g = next)
    {
        next = g->next;
        if (At<unsigned int>(g, 0x18) & flags)
        {
            reinterpret_cast<EmitterGroup *>(g)->UnloadEmitters(true);
            DestroyGroup(reinterpret_cast<EmitterGroup *>(g));
        }
    }
}

void EmitterSystem::KillParticlesFromThisEmitter(Emitter *emitter)
{
    int *self = reinterpret_cast<int *>(this);
    Node *head = Particles(emitter);
    const unsigned short slot = At<unsigned short>(emitter, 0x1e);
    for (Node *p = head->next, *next; p != head; p = next)
    {
        next = p->next;
        --self[slot + 1];
        --self[0];
        --At<short>(emitter, 0x1c);
        Unlink(p);
        bFree(ParticleSlotPool, p);
    }
}

void EmitterSystem::KillEverything()
{
    char *self = reinterpret_cast<char *>(this);
    Node *list = &At<Node>(self, 0x378);
    // Groups flagged 2 survive (the original looped forever on one; it moves on here).
    for (Node *g = list->next, *next; g != list; g = next)
    {
        next = g->next;
        if (!(At<unsigned char>(g, 0x18) & 2))
            DestroyGroup(reinterpret_cast<EmitterGroup *>(g));
    }
    SetParticleStats(0, 0x400, 0, 0x2e, At<int>(self, 0x394), 500,
                                                                                    At<int>(self, 0x390), 200);
}

EmitterParticle *EmitterSystem::GetNewParticle(Emitter *emitter)
{
    int *self = reinterpret_cast<int *>(this);
    Node *list = &At<Node>(this, 0x378);
    if (self[0] == 0x400 && (At<unsigned int>(At<char *>(emitter, 0x8c), 0x18) & kKeep))
    {
        // The pool is full and this effect matters: free a disposable group with live particles, else any
        // group that is not kept.
        bool freed = false;
        for (Node *g = list->next, *next; g != list && !freed; g = next)
        {
            next = g->next;
            if (!At<short>(g, 0x1c))
                continue;
            unsigned int particles = 0;
            Node *head = Emitters(g);
            for (Node *e = head->next; e != head; e = e->next)
                particles += At<unsigned short>(e, 0x1c);
            const unsigned int flags = At<unsigned int>(g, 0x18);
            if (particles && (flags & kDisposable) && !(flags & (kProtected | kKeep)))
            {
                freed = true;
                DestroyGroup(reinterpret_cast<EmitterGroup *>(g));
            }
        }
        for (Node *g = list->next, *next; g != list && !freed; g = next)
        {
            next = g->next;
            if (!(At<unsigned int>(g, 0x18) & kKeep))
            {
                freed = true;
                DestroyGroup(reinterpret_cast<EmitterGroup *>(g));
            }
        }
    }
    char *p = static_cast<char *>(bOMalloc(ParticleSlotPool));
    if (!p)
        return nullptr;
    std::memset(p, 0, 0x70);
    At<unsigned int>(p, 0x5c) = At<unsigned int>(emitter, 0x18) & 0xffff0000u;
    Append(Particles(emitter), reinterpret_cast<Node *>(p));
    ++self[0];
    ++self[At<unsigned short>(emitter, 0x1e) + 1];
    return reinterpret_cast<EmitterParticle *>(p);
}

bool EmitterSystem::IsCloseEnough(const bVector3 *position, float range, int useViewCone, float minimumCosine) const
{
    const char *self = reinterpret_cast<const char *>(this);
    const int views = (eGetCurrentViewMode() == 3) + 1;
    for (int i = 0; i < views; ++i)
    {
        const bVector3 *point = reinterpret_cast<const bVector3 *>(self + 0x320 + i * 0x10);
        const float distance = bDistBetween(position, point);
        if (!useViewCone)
        {
            if (distance < range)
                return true;
            continue;
        }
        if (!(distance < range))
            continue;
        const float *direction = reinterpret_cast<const float *>(self + 0x340 + i * 0x10);
        const float inverse = 1.0f / distance;
        const float dx = (position->x - point->x) * inverse;
        const float dz = position->z - point->z;
        const float facing = dx * direction[0] + inverse * (position->y - point->y) * direction[1] + dz * inverse * direction[2];
        if (facing > minimumCosine)
            return true;
    }
    return false;
}

EmitterGroup *EmitterSystem::CreateEmitterGroup(const unsigned int &key, unsigned int flags)
{
    if (!EnableParticleSystem)
        return nullptr;
    const Attrib::Collection *collection = Attrib::FindCollection(0xaba86e60u, key);
    return collection ? CreateEmitterGroup(collection, flags) : nullptr;
}

EmitterGroup *EmitterSystem::CreateEmitterGroup(const Attrib::StringKey &key, unsigned int flags)
{
    if (!EnableParticleSystem)
        return nullptr;
    const Attrib::Collection *collection = Attrib::FindCollection(0xaba86e60u, *reinterpret_cast<const unsigned int *>(
                                                                                    reinterpret_cast<const char *>(&key) + 8));
    return collection ? CreateEmitterGroup(collection, flags) : nullptr;
}

EmitterLibrary *EmitterSystem::FindLibrary(unsigned int key)
{
    LibEntry *first = At<LibEntry *>(this, 0x39c), *last = At<LibEntry *>(this, 0x3a0);
    LibEntry *it = LowerBound(first, last, key);
    return it != last && it->key == key ? it->library : nullptr;
}

void EmitterSystem::RemoveLibrary(EmitterLibrary *library)
{
    const unsigned int key = *reinterpret_cast<const unsigned int *>(library);
    LibEntry *&last = At<LibEntry *>(this, 0x3a0);
    for (LibEntry *it = LowerBound(At<LibEntry *>(this, 0x39c), last, key); it != last && it->key == key; ++it)
    {
        if (it->library == library)
        {
            std::memmove(it, it + 1, static_cast<unsigned int>(last - (it + 1)) * sizeof(LibEntry));
            --last;
            return;
        }
    }
}

void __cdecl CleanParticlesOnRaceRestart()
{
    gEmitterSystem.KillEffectsMatchingFlag(0xf7000000u);
}

void __cdecl EmitterSystem_OnStartNIS()
{
    if (IsInNIS)
        return;
    gEmitterSystem.KillEverything();
    SlotPool *const pools[3] = {ParticleSlotPool, EmitterSlotPool, EmitterGroupSlotPool};
    for (SlotPool *pool : pools)
        At<unsigned int>(pool, 0x14) |= 1;
    for (SlotPool *pool : pools)
        At<unsigned int>(pool, 0x14) &= ~4u;
    gEmitterSystem.RefreshWorldEffects();
    IsInNIS = true;
}

void __cdecl EmitterSystem_OnEndNis()
{
    if (!IsInNIS)
        return;
    gEmitterSystem.KillEverything();
    CollapseFXSlotPools();
    gEmitterSystem.RefreshWorldEffects();
    IsInNIS = false;
}
