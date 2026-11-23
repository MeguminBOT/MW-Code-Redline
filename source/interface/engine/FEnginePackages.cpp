/**
 * The front-end engine's packages coming and going, and the mouse (original: zFEng FEngine::PushPackage 0x5c4df0,
 * UnloadPackage 0x5be6a0, SetInitialState 0x5bb720, ProcessMouseForPackage 0x5b0b20; FEPackageList::AddPackage
 * 0x5b2100; FEPackage::Startup 0x5bc4d0, InitializePackage 0x5b8510, ConnectObjectResources 0x5b87e0 with
 * PackageInitStateCB::Callback 0x5b8470 and ResourceConnector::Callback 0x5bc670; FEMouse::Update 0x5b01e0).
 *
 * FEngine: +0 runs, +1 the mouse is used, +4 the pads, +8 the mouse (FEMouse: x, y, wheel, buttons before and now,
 * how long each of three buttons has been held, whether it moved at +0x1d), +0xd0 the pad count, +0xd8 the pushed
 * packages (FEPackageList: count +4, first +8, last +0xc; a package's next at +4, previous at +8, ordered by priority
 * +0x1c, highest last), +0xe8 the idle packages (first +0xf0), +0x108 the game interface (vtable: +0 the package's
 * bytes by name, +4 its resources loaded, +8 unloaded, +0xc the package pushed, +0x10 whether to delete it on unload),
 * +0x4120 the package commands (count +0x4124, first +0x4128, last +0x412c; a command's package at +0x1c).
 * FEPackage: +0x14 runs, +0x15 kept idle when unloaded, +0x1c priority, +0x20 pads, +0x28 takes input, +0x4c objects,
 * +0x64/+0x68 resources (0x18 each: +0x10 and +0x14 what an object takes from it), +0x7c libraries it uses (each with
 * the library's hash at +0x10; a library's users at +0x1c), +0xac/+0xb0 its mouse objects' states (0x10 each).
 * The script an object starts in: 0x1744b3.
 */
#include "game/gen/api.h"
#include "legacy/link.h"
#include <string.h>

namespace
{
template <typename T> T &At(const void *p, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(p)) + offset);
}

const unsigned int kInitScript = 0x1744b3;
const unsigned int kStartFlags = 0x3c00000;
const int kListBox = 4;
const int kGroup = 5;
const int kMovie = 6;
const unsigned short kNoResource = 0xffff;
using Free = void(__cdecl *)(void *);
const Free kFree = reinterpret_cast<Free>(0x652b10);

/** A walk over a package's objects, laid out as the engine's callbacks are (vtable: destructor, callback). */
class Walker
{
  public:
    virtual ~Walker()
    {
    }
    virtual bool Callback(FEObject *o) = 0;
    FEObjectCallback &AsCallback()
    {
        return *reinterpret_cast<FEObjectCallback *>(this);
    }
};

/** Every object to its start script, from its first frame, everything about it to be worked out again. */
class InitState : public Walker
{
  public:
    bool Callback(FEObject *o) override
    {
        char *script = At<char *>(o, 0x4c);
        while (script && At<unsigned int>(script, 0x30) != kInitScript)
            script = At<char *>(script, 4);
        At<char *>(o, 0x54) = script;
        if (script)
            o->SetupMoveToTracks();
        At<unsigned int>(At<char *>(o, 0x54), 0x10) = 0;
        o->Flags |= kStartFlags;
        return true;
    }
};

/** Each object's texture from the package's resources; list boxes connect their own. */
class ResourceWalker : public Walker
{
  public:
    FEPackage *package;
    char **resources;
    bool Callback(FEObject *o) override
    {
        const int type = o->Type;
        if (type == kListBox)
            reinterpret_cast<ResourceConnector *>(this)->ConnectListBoxResources(reinterpret_cast<FEListBox *>(o));
        else if (type != kGroup && type != kMovie)
        {
            const unsigned short index = At<unsigned short>(o, 0x22);
            if (index != kNoResource)
            {
                const char *resource = *resources + index * 0x18;
                At<unsigned int>(o, 0x28) = At<unsigned int>(resource, 0x14);
                At<unsigned int>(o, 0x24) = At<unsigned int>(resource, 0x10);
            }
        }
        return true;
    }
};

void *Interface(const FEngine *e)
{
    return At<void *>(e, 0x108);
}

void **Vtable(void *o)
{
    return *static_cast<void ***>(o);
}

/** The name without its folders (the original keeps the length in a signed byte). */
const char *BaseName(const char *name)
{
    signed char left = static_cast<signed char>(static_cast<int>(strlen(name)));
    const char *p = name + left - 1;
    char c = *p;
    while (c != '/' && c != '\\' && left > 0)
    {
        c = p[-1];
        --p;
        --left;
    }
    return left ? p + 1 : p;
}
} // namespace

FEPackage *FEngine::PushPackage(char const *name, unsigned char priority, unsigned int pads)
{
    FEList *idle = reinterpret_cast<FEList *>(reinterpret_cast<char *>(this) + 0xe8);
    FEPackage *p = reinterpret_cast<FEPackage *>(idle->FindNode(name, At<FENode *>(this, 0xf0)));
    if (!p)
        p = reinterpret_cast<FEPackage *>(idle->FindNode(BaseName(name), At<FENode *>(this, 0xf0)));
    if (p)
    {
        InitState init;
        At<bool>(p, 0x15) = true;
        p->ForAllObjects(init.AsCallback());
        idle->RemNode(reinterpret_cast<FEMinNode *>(p));
    }
    else
    {
        void *bytes = nullptr;
        bool owned = false;
        using Fetch = void *(__thiscall *)(void *, const char *, void **, bool *);
        void *data = reinterpret_cast<Fetch>(Vtable(Interface(this))[0])(Interface(this), name, &bytes, &owned);
        if (!data)
            return nullptr;
        p = LoadPackage(data, false);
        if (owned)
            kFree(bytes);
        if (!p)
            return nullptr;
    }
    At<unsigned int>(p, 0x20) = pads;
    At<unsigned int>(p, 0x1c) = priority;
    At<bool>(p, 0x14) = At<bool>(this, 0);
    if (void *gi = Interface(this))
        reinterpret_cast<void(__thiscall *)(void *, FEPackage *)>(Vtable(gi)[3])(gi, p);
    reinterpret_cast<FEPackageList *>(reinterpret_cast<char *>(this) + 0xd8)->AddPackage(p);
    return p;
}

bool FEngine::UnloadPackage(FEPackage *p)
{
    char *found = At<char *>(this, 0xe0);
    while (found && found != reinterpret_cast<char *>(p))
        found = At<char *>(found, 4);
    if (!found)
        return false;
    bool remove = true;
    if (void *gi = Interface(this))
        remove = reinterpret_cast<bool(__thiscall *)(void *, FEPackage *)>(Vtable(gi)[4])(gi, p);
    for (char *q = At<char *>(this, 0xe0); q; q = At<char *>(q, 4))
        if (q == reinterpret_cast<char *>(p))
        {
            reinterpret_cast<FEList *>(reinterpret_cast<char *>(this) + 0xd8)->RemNode(reinterpret_cast<FEMinNode *>(p));
            break;
        }
    for (char *c = At<char *>(this, 0x4128); c;)
    {
        char *next = At<char *>(c, 4);
        if (At<FEPackage *>(c, 0x1c) == p)
        {
            if (c == At<char *>(this, 0x4128))
                At<char *>(this, 0x4128) = next;
            if (c == At<char *>(this, 0x412c))
                At<char *>(this, 0x412c) = At<char *>(c, 8);
            if (char *prev = At<char *>(c, 8))
                At<char *>(prev, 4) = At<char *>(c, 4);
            if (char *after = At<char *>(c, 4))
                At<char *>(after, 8) = At<char *>(c, 8);
            At<unsigned int>(c, 4) = 0xabadcafe;
            At<unsigned int>(c, 8) = 0xabadcafe;
            --At<int>(this, 0x4124);
            reinterpret_cast<void(__thiscall *)(void *, int)>(Vtable(c)[0])(c, 1);
        }
        c = next;
    }
    if (At<bool>(p, 0x15))
    {
        AddToIdleList(p);
        return true;
    }
    for (char *lib = At<char *>(p, 0x7c); lib; lib = At<char *>(lib, 4))
        if (FEPackage *library = FindLibraryPackage(At<unsigned int>(lib, 0x10)))
        {
            const int users = At<int>(library, 0x1c) - 1;
            if (users < 1)
                UnloadLibraryPackage(library);
            else
                At<int>(library, 0x1c) = users;
        }
    if (void *gi = Interface(this))
        reinterpret_cast<void(__thiscall *)(void *, FEPackage *, unsigned int, unsigned int)>(Vtable(gi)[2])(
            gi, p, At<unsigned int>(p, 0x64), At<unsigned int>(p, 0x68));
    if (remove)
        reinterpret_cast<void(__thiscall *)(void *, int)>(Vtable(p)[0])(p, 1);
    return true;
}

void FEngine::SetInitialState()
{
    InitState init;
    for (FEPackage *p = At<FEPackage *>(this, 0xe0); p && p->ForAllObjects(init.AsCallback());
         p = At<FEPackage *>(p, 4))
    {
    }
    for (unsigned char i = 0; i < At<unsigned int>(this, 0xd0); ++i)
        reinterpret_cast<FEJoyPad *>(At<char *>(this, 4) + i * 0x88)->Reset();
    if (At<bool>(this, 1))
    {
        At<unsigned int>(this, 0x10) = 0;
        At<unsigned int>(this, 0xc) = 0;
        At<unsigned int>(this, 8) = 0;
        At<unsigned short>(this, 0x16) = 0;
        At<unsigned short>(this, 0x14) = 0;
        At<unsigned int>(this, 0x20) = 0;
        At<unsigned int>(this, 0x1c) = 0;
        At<unsigned int>(this, 0x18) = 0;
        At<unsigned char>(this, 0x24) = 0;
        At<unsigned char>(this, 0x25) = 0;
    }
}

void FEngine::ProcessMouseForPackage(FEPackage *p)
{
    const unsigned int pads = At<unsigned int>(p, 0x20);
    if (!pads || !(pads & 1) || !At<bool>(p, 0x28))
        return;
    const float x = static_cast<float>(At<int>(this, 8));
    const float y = static_cast<float>(At<int>(this, 0xc));
    char *state = At<char *>(p, 0xac);
    for (int n = At<int>(p, 0xb0); n > 0; --n, state += 0x10)
        UpdateMouseState(p, reinterpret_cast<FEObjectMouseState *>(state), x, y);
}

void FEPackageList::AddPackage(FEPackage *p)
{
    char *self = reinterpret_cast<char *>(this);
    char *at = At<char *>(self, 0xc);
    while (at && At<int>(at, 0x1c) > At<int>(p, 0x1c))
        at = At<char *>(at, 8);
    if (!p)
        return;
    char *node = reinterpret_cast<char *>(p);
    if (!at)
    {
        char *first = At<char *>(self, 8);
        At<char *>(node, 4) = first;
        if (first)
            At<char *>(first, 8) = node;
        At<char *>(node, 8) = nullptr;
        At<char *>(self, 8) = node;
    }
    else
    {
        char *next = At<char *>(at, 4);
        At<char *>(node, 4) = next;
        if (next)
            At<char *>(next, 8) = node;
        At<char *>(node, 8) = at;
        At<char *>(at, 4) = node;
    }
    if (At<char *>(self, 0xc) == at)
        At<char *>(self, 0xc) = node;
    ++At<int>(self, 4);
}

bool FEPackage::Startup(FEGameInterface *gi)
{
    using Load = bool(__thiscall *)(FEGameInterface *, FEPackage *, unsigned int, unsigned int);
    const bool loaded = reinterpret_cast<Load>(Vtable(gi)[1])(gi, this, At<unsigned int>(this, 0x64),
                                                             At<unsigned int>(this, 0x68));
    ConnectObjectResources();
    BuildMouseObjectStateList();
    return loaded;
}

bool FEPackage::InitializePackage()
{
    InitState init;
    return ForAllObjects(init.AsCallback());
}

void FEPackage::ConnectObjectResources()
{
    ResourceWalker walker;
    walker.package = this;
    walker.resources = &At<char *>(this, 0x68);
    ForAllObjects(walker.AsCallback());
}

void FEMouse::Update(FEMouseInfo &info, unsigned int elapsed)
{
    const short *in = reinterpret_cast<const short *>(&info);
    char *m = reinterpret_cast<char *>(this);
    At<unsigned char>(m, 0x1d) = (in[0] != At<int>(m, 0) || in[1] != At<int>(m, 4)) ? 1 : 0;
    At<int>(m, 0) = in[0];
    At<int>(m, 4) = in[1];
    At<int>(m, 8) = in[2];
    At<unsigned short>(m, 0xc) = At<unsigned short>(m, 0xe);
    const unsigned short now = static_cast<unsigned short>(in[3]);
    At<unsigned short>(m, 0xe) = now;
    const unsigned short before = At<unsigned short>(m, 0xc);
    for (int b = 0; b < 3; ++b)
        if (now & (1 << b))
            At<unsigned int>(m, 0x10 + b * 4) = (before & (1 << b)) ? At<unsigned int>(m, 0x10 + b * 4) + elapsed : 0;
}
