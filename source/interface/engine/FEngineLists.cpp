/**
 * The front-end engine's pads, lists and object walks (original: zFEng FEJoyPad::FEJoyPad 0x5b54c0, Reset 0x5ae8e0,
 * Update 0x5ae960, HeldFor 0x5aea10; FEngine::SetNumJoyPads 0x5b7420, SetExecution 0x5b07d0,
 * GetNumPackagesBelowPriority 0x5b0c30, FindIdlePackage 0x5b7580; FEList::FindNode 0x5b5b10 and 0x5b5b70;
 * FEPackage::ForAllObjects 0x5b1560, ForAllChildren 0x5b1510, FindObjectByHash 0x5b8550, FindObjectByGUID 0x5b85a0,
 * SetCurrentButton 0x5b8610; FEGroup::FindChildRecursive 0x5ae890; zFe2 FEPackageManager::Init 0x591d50, Loader
 * 0x5859e0).
 *
 * FEJoyPad (0x88): the buttons a frame before, the buttons now, how long each of the 32 has been held. FEngine: +0 runs,
 * +4 the pads, +0x34 a repeat time per button (19), +0xd0 the pad count, +0xe0 the first pushed package (next at +4,
 * priority at +0x1c, runs at +0x14), +0xf0 the first idle package, +0x4128 the first queued package command (what at
 * +0x14: 1 pop, 2 push). FENode: +4 next, +0xc name, +0x10 its name's hash. A group's children start at +0x64.
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

const int kGroup = 5;
const int kPadWords = 0x22;
const int kPadBytes = 0x88;
const int kRepeatTimes = 19;
const unsigned int kUnhighlight = 0x55d1e635;
const unsigned int kHighlight = 0xabc08912;
FEObject *const kToGame = reinterpret_cast<FEObject *>(0xfffffffb);
void *const kManagerVtable = reinterpret_cast<void *>(0x8a1598);
FEPackageManager **const kManager = reinterpret_cast<FEPackageManager **>(0x91d21c);
using Malloc = void *(__cdecl *)(unsigned int);
using Free = void(__cdecl *)(void *);
const Malloc kMalloc = reinterpret_cast<Malloc>(0x652ac0);
const Free kFree = reinterpret_cast<Free>(0x652b10);

/** A callback object as FEObjectCallback lays it out: vtable, then what it looks for, then what it found. */
struct Finder
{
    void *vtable;
    unsigned int key;
    FEObject *found;
};

void *const kFindByHashVtable = reinterpret_cast<void *>(0x8a2ca8);
void *const kFindByGuidVtable = reinterpret_cast<void *>(0x8a2cb0);

bool Call(FEObjectCallback &callback, FEObject *o)
{
    using Callback = bool(__thiscall *)(FEObjectCallback *, FEObject *);
    return reinterpret_cast<Callback>((*reinterpret_cast<void ***>(&callback))[1])(&callback, o);
}

FEObject *FirstChild(const void *group)
{
    return At<FEObject *>(group, 0x64);
}
} // namespace

FEJoyPad::FEJoyPad()
{
    Reset();
}

void FEJoyPad::Reset()
{
    unsigned int *w = reinterpret_cast<unsigned int *>(this);
    for (int i = 0; i < kPadWords; ++i)
        w[i] = 0;
}

void FEJoyPad::Update(unsigned int buttons, unsigned int elapsed)
{
    unsigned int *w = reinterpret_cast<unsigned int *>(this);
    w[0] = w[1];
    w[1] = buttons;
    for (int b = 0; b < 32; ++b)
    {
        const unsigned int bit = 1u << b;
        if (w[1] & bit)
            w[2 + b] = (w[0] & bit) ? w[2 + b] + elapsed : 0;
    }
}

unsigned int FEJoyPad::HeldFor(unsigned int buttons)
{
    const unsigned int *w = reinterpret_cast<const unsigned int *>(this);
    unsigned int least = 0xffffffffu;
    for (int b = 0; b < 32; ++b)
        if ((buttons & (1u << b)) && w[2 + b] <= least)
            least = w[2 + b];
    return least;
}

void FEngine::SetNumJoyPads(unsigned char count)
{
    kFree(At<void *>(this, 4));
    if (count)
    {
        char *pads = static_cast<char *>(kMalloc(count * kPadBytes));
        if (pads)
            for (unsigned int i = 0; i < count; ++i)
                reinterpret_cast<FEJoyPad *>(pads + i * kPadBytes)->Reset();
        At<char *>(this, 4) = pads;
    }
    At<unsigned int>(this, 0xd0) = count;
    for (int i = 0; i < kRepeatTimes; ++i)
        At<unsigned int>(this, 0x34 + i * 4) = 0;
}

void FEngine::SetExecution(bool run)
{
    At<bool>(this, 0) = run;
    for (char *p = At<char *>(this, 0xe0); p; p = At<char *>(p, 4))
        At<bool>(p, 0x14) = run;
}

int FEngine::GetNumPackagesBelowPriority(unsigned char priority)
{
    int count = 0;
    for (const char *p = At<const char *>(this, 0xe0); p; p = At<const char *>(p, 4))
        if (At<int>(p, 0x1c) < static_cast<int>(priority))
            ++count;
    for (const char *c = At<const char *>(this, 0x4128); c; c = At<const char *>(c, 4))
    {
        const unsigned int what = At<unsigned int>(c, 0x14);
        if (count == 0 && (what & 3))
            count = 1;
        else if (what & 2)
            ++count;
        else if (what & 1)
            --count;
    }
    return count;
}

FEPackage *FEngine::FindIdlePackage(char const *name) const
{
    return reinterpret_cast<FEPackage *>(
        reinterpret_cast<const FEList *>(this)->FindNode(name, At<FENode *>(this, 0xf0)));
}

FENode *FEList::FindNode(char const *name, FENode *from) const
{
    const unsigned int hash = FEHashUpper(name);
    for (FENode *n = from; n; n = At<FENode *>(n, 4))
    {
        const char *nodeName = At<const char *>(n, 0xc);
        if (!nodeName)
        {
            if (!name)
                return n;
        }
        else if (At<unsigned int>(n, 0x10) == hash && FEStricmp(nodeName, name) == 0)
            return n;
    }
    return nullptr;
}

FENode *FEList::FindNode(char const *name) const
{
    return FindNode(name, At<FENode *>(this, 8));
}

bool FEPackage::ForAllChildren(FEGroup *group, FEObjectCallback &callback)
{
    for (FEObject *o = FirstChild(group); o; o = o->Next)
        if (!Call(callback, o) || (o->Type == kGroup && !ForAllChildren(reinterpret_cast<FEGroup *>(o), callback)))
            return false;
    return true;
}

bool FEPackage::ForAllObjects(FEObjectCallback &callback)
{
    for (FEObject *o = At<FEObject *>(this, 0x4c); o; o = o->Next)
        if (!Call(callback, o) || (o->Type == kGroup && !ForAllChildren(reinterpret_cast<FEGroup *>(o), callback)))
            return false;
    return true;
}

FEObject *FEPackage::FindObjectByHash(unsigned int hash)
{
    Finder f = {kFindByHashVtable, hash, nullptr};
    ForAllObjects(*reinterpret_cast<FEObjectCallback *>(&f));
    return f.found;
}

FEObject *FEPackage::FindObjectByGUID(unsigned int guid)
{
    Finder f = {kFindByGuidVtable, guid, nullptr};
    ForAllObjects(*reinterpret_cast<FEObjectCallback *>(&f));
    return f.found;
}

void FEPackage::SetCurrentButton(FEObject *button, bool notify)
{
    FEObject *&current = At<FEObject *>(this, 0x8c);
    if (notify)
    {
        FEngine *e = At<FEngine *>(this, 0x30);
        if (current)
        {
            e->QueueMessage(kUnhighlight, nullptr, this, current, 0);
            e->QueueMessage(kUnhighlight, current, this, kToGame, 0);
        }
        if (button)
        {
            e->QueueMessage(kHighlight, nullptr, this, button, 0);
            e->QueueMessage(kHighlight, button, this, kToGame, 0);
        }
    }
    current = button;
}

FEObject *FEGroup::FindChildRecursive(unsigned int hash) const
{
    for (FEObject *o = FirstChild(this); o; o = o->Next)
    {
        if (o->NameHash == hash)
            return o;
        if (o->Type == kGroup)
            if (FEObject *found = reinterpret_cast<const FEGroup *>(o)->FindChildRecursive(hash))
                return found;
    }
    return nullptr;
}

void __cdecl FEPackageManager::Init()
{
    if (*kManager)
        return;
    void **m = static_cast<void **>(kMalloc(0xc));
    if (m)
    {
        m[0] = kManagerVtable;
        m[1] = &m[1];
        m[2] = &m[1];
    }
    *kManager = reinterpret_cast<FEPackageManager *>(m);
}

void FEPackageManager::Loader(bChunk *chunk, bool)
{
    void *memory = kMalloc(0x38);
    char *node = nullptr;
    if (memory)
        node = reinterpret_cast<char *>(new (memory) FEPackageData(chunk)) + 4;
    char *m = reinterpret_cast<char *>(*kManager);
    char **tail = At<char **>(m, 8);
    *tail = node;
    At<char *>(m, 8) = node;
    reinterpret_cast<char **>(node)[0] = m + 4;
    reinterpret_cast<char **>(node)[1] = reinterpret_cast<char *>(tail);
}
