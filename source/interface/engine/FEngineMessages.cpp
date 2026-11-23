/**
 * The front-end engine's messages and package commands (original: zFEng FEngine::QueueMessage 0x5b7780,
 * ProcessMessageQueue 0x5c1460, ProcessObjectMessage 0x5beaa0, ProcessResponses 0x5bbc00, QueuePackageCommand
 * 0x5b7880, ProcessPackageCommands 0x5c4f60, QueuePackageSwitch/Push/Pop, FEMessageResponse::FindConditionBranchTarget
 * 0x5b0140). docs/FRONTEND.md lists the response ids.
 *
 * FEngine: +0xe0 the loaded packages (next at +4), +0xe4 the package list's tail (previous at +8), +0x108 the game
 * interface, +0x4110 the message queue (count +0x4114, head +0x4118, tail +0x411c), +0x4120 the package commands
 * (count +0x4124, head +0x4128, tail +0x412c), +0x4144 the package markers' count (names of 0x100 from +0x4148),
 * +0x524e the message log switch (the game interface hears every message).
 * FEPackage: +0xc its name, +0x10 its name hash, +0x1c its place in the stack, +0x20 the pads it has, +0x24 the pads
 * it had, +0x28 whether it takes input, +0x3c the package it came from, +0x5c its responses, +0x6c/+0x70 its message
 * targets, +0x8c its current button.
 * A message (0x20): vtable, next, previous, the object it is from, its target, the package, the message, the pads.
 * A package command (0x20): vtable, next, previous, the package's name (+0xc), +0x14 what to do (1 pop, 2 push,
 * 4 hand the pads up, 8 hand them down), +0x18 the pads, +0x1c the package it is for.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
template <typename T> T &At(const void *p, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(p)) + offset);
}

using Malloc = void *(__cdecl *)(unsigned int);
const Malloc kMalloc = reinterpret_cast<Malloc>(0x652ac0);
const unsigned int kMessageVtable = 0x8a2c90;
const unsigned int kCommandVtable = 0x8a2c7c;
const unsigned int kDeadLink = 0xabadcafe;
const unsigned int kPadsChanged = 0x334c5493;
const unsigned int kInputOn = 0x59bed120;
const unsigned int kInputOff = 0x5d4ce32d;

enum Target : unsigned int
{
    kToAll = 0,
    kToGameAndPackage = 0xffffffff,
    kToPackages = 0xfffffffe,
    kToPackage = 0xfffffffd,
    kToPackageAndTargets = 0xfffffffc,
    kToGame = 0xfffffffb,
    kInputSwitch = 0xfffffffa,
};

enum Command : unsigned int
{
    kPop = 1,
    kPush = 2,
    kPadsUp = 4,
    kPadsDown = 8,
};

struct Node
{
    void **vtable;
    Node *next;
    Node *prev;
};

struct Message : Node
{
    FEObject *from;
    unsigned int target;
    FEPackage *package;
    unsigned int message;
    unsigned int pads;
};

struct PackageCommand : Node
{
    const char *name;
    unsigned int unused;
    unsigned int what;
    unsigned int pads;
    FEPackage *package;
};

struct ListHead
{
    void *vtable;
    int count;
    Node *head;
    Node *tail;
};

ListHead &Messages(FEngine *e)
{
    return At<ListHead>(e, 0x4110);
}

ListHead &Commands(FEngine *e)
{
    return At<ListHead>(e, 0x4120);
}

void AddTail(ListHead &list, Node *node)
{
    Node *tail = list.tail;
    if (!tail)
    {
        node->next = list.head;
        if (list.head)
            list.head->prev = node;
        node->prev = nullptr;
        list.head = node;
    }
    else
    {
        node->next = tail->next;
        if (tail->next)
            tail->next->prev = node;
        node->prev = tail;
        tail->next = node;
    }
    if (list.tail == tail)
        list.tail = node;
    ++list.count;
}

Node *PopHead(ListHead &list)
{
    Node *node = list.head;
    if (!node)
        return nullptr;
    list.head = node->next;
    if (node == list.tail)
        list.tail = node->prev;
    if (node->prev)
        node->prev->next = node->next;
    if (node->next)
        node->next->prev = node->prev;
    node->next = reinterpret_cast<Node *>(kDeadLink);
    node->prev = reinterpret_cast<Node *>(kDeadLink);
    --list.count;
    return node;
}

void Delete(Node *node)
{
    reinterpret_cast<void(__thiscall *)(Node *, int)>(node->vtable[0])(node, 1);
}

void **GameInterface(FEngine *e)
{
    return At<void **>(e, 0x108);
}

FEPackage *FirstPackage(FEngine *e)
{
    return At<FEPackage *>(e, 0xe0);
}

FEPackage *NextPackage(FEPackage *p)
{
    return At<FEPackage *>(p, 4);
}

FEMessageResponse *FindPackageResponse(FEPackage *p, unsigned int message)
{
    for (char *r = At<char *>(p, 0x5c); r; r = At<char *>(r, 4))
        if (At<unsigned int>(r, 0xc) == message)
            return reinterpret_cast<FEMessageResponse *>(r);
    return nullptr;
}

/** The package with pads (searched from the stack's top), or null. */
FEPackage *PackageWithPads(FEngine *e)
{
    for (FEPackage *p = At<FEPackage *>(e, 0xe4); p; p = At<FEPackage *>(p, 8))
        if (At<unsigned int>(p, 0x20))
            return p;
    return nullptr;
}

void SendToTargets(FEngine *e, FEPackage *p, unsigned int message, unsigned int pads)
{
    const unsigned int lists = At<unsigned int>(p, 0x6c);
    unsigned int *list = At<unsigned int *>(p, 0x70);
    for (unsigned int i = 0; i < lists; ++i, list += 4)
        if (list[0] == message)
        {
            FEObject **targets = reinterpret_cast<FEObject **>(list[3]);
            const unsigned int count = list[2];
            for (unsigned int t = 0; t < count; ++t)
                e->ProcessObjectMessage(targets[t], p, message, pads);
            return;
        }
}

void Respond(FEngine *e, FEPackage *p, unsigned int message, unsigned int pads)
{
    if (FEMessageResponse *r = FindPackageResponse(p, message))
        e->ProcessResponses(r, nullptr, p, pads);
}
} // namespace

NFS_REPLACE(0x5c1460, ProcessMessageQueue)
NFS_REPLACE(0x5c4f60, ProcessPackageCommands)

void FEngine::QueueMessage(unsigned int message, FEObject *from, FEPackage *package, FEObject *target, unsigned int pads)
{
    Message *m = static_cast<Message *>(kMalloc(sizeof(Message)));
    m->vtable = reinterpret_cast<void **>(kMessageVtable);
    m->next = reinterpret_cast<Node *>(kDeadLink);
    m->prev = reinterpret_cast<Node *>(kDeadLink);
    m->package = package;
    m->message = message;
    m->from = from;
    m->target = reinterpret_cast<unsigned int>(target);
    m->pads = pads;
    if (At<char>(this, 0x524e))
        reinterpret_cast<void(__thiscall *)(void *, unsigned int, FEObject *, FEPackage *, FEObject *, unsigned int)>(
            (*reinterpret_cast<void ***>(GameInterface(this)))[0x54 / 4])(GameInterface(this), message, from, package,
                                                                            target, pads);
    AddTail(Messages(this), m);
}

/** FEngine::ProcessMessageQueue (private, 0x5c1460): every message queued, those its responses queue too. */
void __fastcall ProcessMessageQueue(FEngine *e, void *)
{
    while (Message *m = static_cast<Message *>(PopHead(Messages(e))))
    {
        if (At<char>(e, 0x524e))
            reinterpret_cast<void(__thiscall *)(void *, unsigned int, unsigned int, FEObject *, FEPackage *, unsigned int)>(
                (*reinterpret_cast<void ***>(GameInterface(e)))[0x58 / 4])(GameInterface(e), m->message, m->target,
                                                                         m->from, m->package, m->pads);
        switch (m->target)
        {
        case kToPackageAndTargets:
            for (FEPackage *p = FirstPackage(e); p; p = NextPackage(p))
                if (p == m->package)
                {
                    Respond(e, p, m->message, m->pads);
                    SendToTargets(e, p, m->message, m->pads);
                    break;
                }
            break;
        case kToAll:
            for (FEPackage *p = FirstPackage(e); p; p = NextPackage(p))
            {
                Respond(e, p, m->message, m->pads);
                SendToTargets(e, p, m->message, m->pads);
            }
            break;
        case kInputSwitch:
            if (m->message == kInputOn)
            {
                if (m->package)
                    At<bool>(m->package, 0x28) = true;
            }
            else if (m->message == kInputOff && m->package)
                At<bool>(m->package, 0x28) = false;
            break;
        case kToGame:
            reinterpret_cast<void(__thiscall *)(void *, unsigned int, FEObject *, unsigned int, FEPackage *)>(
                (*reinterpret_cast<void ***>(GameInterface(e)))[0x1c / 4])(GameInterface(e), m->message, m->from, m->pads,
                                                                         m->package);
            break;
        case kToPackage:
            Respond(e, m->package, m->message, m->pads);
            break;
        case kToPackages:
            for (FEPackage *p = FirstPackage(e); p; p = NextPackage(p))
                Respond(e, p, m->message, m->pads);
            break;
        case kToGameAndPackage:
            reinterpret_cast<void(__thiscall *)(void *, unsigned int, FEObject *, unsigned int, FEPackage *)>(
                (*reinterpret_cast<void ***>(GameInterface(e)))[0x18 / 4])(GameInterface(e), m->message, m->from, m->pads,
                                                                         m->package);
            break;
        default:
            e->ProcessObjectMessage(reinterpret_cast<FEObject *>(m->target), m->package, m->message, m->pads);
            break;
        }
        Delete(m);
    }
}

void FEngine::ProcessObjectMessage(FEObject *object, FEPackage *package, unsigned int message, unsigned int pads)
{
    if (object->Type == 4 && ProcessListBoxResponses(object, package, message))
        return;
    if (object->Type == 6 && ProcessCodeListBoxResponses(object, package, message))
        return;
    for (FEMessageResponse *r = object->FirstResponse; r; r = At<FEMessageResponse *>(r, 4))
        if (At<unsigned int>(r, 0xc) == message)
        {
            ProcessResponses(r, object, package, pads);
            return;
        }
}

unsigned int FEMessageResponse::FindConditionBranchTarget(unsigned int index) const
{
    const unsigned int count = At<unsigned int>(this, 0x10);
    if (index == count - 1)
        return count;
    const unsigned int *entry = At<unsigned int *>(this, 0x14) + index * 3;
    int depth = 1;
    do
    {
        entry += 3;
        const unsigned int id = entry[0];
        ++index;
        if (id == 0x500)
        {
            if (depth == 1)
                depth = 0;
        }
        else if (id == 0x300 || id == 0x301)
            ++depth;
        else if (id == 0x501)
            --depth;
    } while (index < count && depth != 0);
    return index;
}

void FEngine::ProcessResponses(FEMessageResponse *response, FEObject *object, FEPackage *package, unsigned int pads)
{
    const unsigned int count = At<unsigned int>(response, 0x10);
    for (unsigned int i = 0; i < count; ++i)
    {
        const unsigned int *entry = At<unsigned int *>(response, 0x14) + i * 3;
        const unsigned int id = entry[0];
        const unsigned int param = entry[1];
        switch (id)
        {
        case 0x000:
            if (object)
                if (FEScript *script = object->FindScript(param))
                {
                    object->SetCurrentScript(script);
                    script->CurrentTime = 0;
                }
            break;
        case 0x001:
        {
            unsigned int target = entry[2];
            if (target != kToPackageAndTargets && target != kToGameAndPackage)
                target = reinterpret_cast<unsigned int>(package->FindObjectByGUID(target));
            QueueMessage(param, object, package, reinterpret_cast<FEObject *>(target), pads);
            break;
        }
        case 0x002:
            QueueMessage(param, object, package, reinterpret_cast<FEObject *>(kToGameAndPackage), pads);
            break;
        case 0x003:
            QueueMessage(param, object, package, reinterpret_cast<FEObject *>(kToGame), pads);
            break;
        case 0x100:
            if (!param)
                package->SetCurrentButton(nullptr, false);
            else if (FEObject *button = package->FindObjectByGUID(param))
                package->SetCurrentButton(button, true);
            break;
        case 0x101:
            if (package)
                At<bool>(package, 0x28) = param == 1;
            break;
        case 0x102:
        {
            char *current = At<char *>(package, 0x8c);
            RecordLastPackageButton(At<unsigned int>(package, 0x10), current ? At<unsigned int>(current, 0xc) : 0);
            break;
        }
        case 0x103:
        {
            FEObject *button = nullptr;
            if (const unsigned int recalled = RecallLastPackageButton(At<unsigned int>(package, 0x10)))
                button = package->FindObjectByGUID(recalled);
            if (!button && param)
                button = package->FindObjectByGUID(param);
            if (button)
                package->SetCurrentButton(button, true);
            else if (!param)
                package->SetCurrentButton(nullptr, false);
            break;
        }
        case 0x105:
            QueuePackageUserTransfer(package, true, pads);
            break;
        case 0x106:
            QueuePackageUserTransfer(package, true, 0xff);
            break;
        case 0x107:
            QueuePackageUserTransfer(package, false, pads);
            break;
        case 0x108:
            QueuePackageUserTransfer(package, false, 0xff);
            break;
        case 0x200:
            QueuePackageCommand(3, At<unsigned int>(package, 0x20), reinterpret_cast<const char *>(param));
            break;
        case 0x201:
            QueuePackageCommand(2, At<unsigned int>(package, 0x20), reinterpret_cast<const char *>(param));
            break;
        case 0x202:
            for (unsigned int bit = 0; bit < 8; ++bit)
                if (pads & (1u << bit))
                    QueuePackageCommand(2, 1u << bit, reinterpret_cast<const char *>(param));
            break;
        case 0x203:
            QueuePackageCommand(1, 0, nullptr);
            break;
        case 0x204:
            QueuePackageCommand(2, 0, reinterpret_cast<const char *>(param));
            break;
        case 0x2c0:
            RecordPackageMarker(At<const char *>(package, 0xc));
            break;
        case 0x2c1:
        {
            int &markers = At<int>(this, 0x4144);
            if (markers)
            {
                --markers;
                QueuePackageCommand(3, At<unsigned int>(package, 0x20), reinterpret_cast<const char *>(this) + 0x4148 +
                                                                            markers * 0x100);
            }
            break;
        }
        case 0x2c2:
            ClearPackageMarkers();
            break;
        case 0x300:
            if (At<unsigned int>(At<char *>(object, 0x54), 0x30) != param)
                i = response->FindConditionBranchTarget(i);
            break;
        case 0x301:
            if (At<unsigned int>(At<char *>(object, 0x54), 0x30) == param)
                i = response->FindConditionBranchTarget(i);
            break;
        default:
            if (id == 0x500)
                i = response->FindConditionBranchTarget(i);
            break;
        }
    }
}

void FEngine::QueuePackageCommand(int what, unsigned int pads, const char *name)
{
    FEPackage *with = PackageWithPads(this);
    PackageCommand *c = static_cast<PackageCommand *>(kMalloc(sizeof(PackageCommand)));
    c->vtable = reinterpret_cast<void **>(kCommandVtable);
    c->next = reinterpret_cast<Node *>(kDeadLink);
    c->prev = reinterpret_cast<Node *>(kDeadLink);
    c->name = nullptr;
    c->unused = 0;
    c->what = 0;
    c->pads = 0;
    c->package = with;
    if (!with)
    {
        c->pads = pads ? pads : 0xff;
        for (Node *n = Commands(this).tail; n; n = n->prev)
            if (static_cast<PackageCommand *>(n)->what & kPush)
            {
                c->pads = pads ? pads : static_cast<PackageCommand *>(n)->pads;
                break;
            }
    }
    else
    {
        c->pads = pads ? pads : At<unsigned int>(with, 0x20);
        At<unsigned int>(with, 0x24) = At<unsigned int>(with, 0x20);
        At<unsigned int>(with, 0x20) = 0;
    }
    c->what = static_cast<unsigned int>(what);
    reinterpret_cast<FENode *>(c)->SetName(name);
    AddTail(Commands(this), c);
}

/** FEngine::ProcessPackageCommands (private, 0x5c4f60): the queued pushes, pops and switches, in order. */
void __fastcall ProcessPackageCommands(FEngine *e, void *)
{
    FEPackage *replaced = nullptr;
    FEPackage *replacedParent = nullptr;
    while (PackageCommand *c = static_cast<PackageCommand *>(PopHead(Commands(e))))
    {
        int place;
        if (!c->package)
        {
            c->package = PackageWithPads(e);
            if (!c->package)
                place = -1;
            else
            {
                place = At<int>(c->package, 0x1c);
                At<unsigned int>(c->package, 0x24) = At<unsigned int>(c->package, 0x20);
                At<unsigned int>(c->package, 0x20) = 0;
            }
        }
        else
            place = At<int>(c->package, 0x1c);
        if ((c->what & kPop) && place >= 0)
        {
            if (!(c->what & kPush))
            {
                FEPackage *parent = At<FEPackage *>(c->package, 0x3c);
                for (FEPackage *p = FirstPackage(e); p; p = NextPackage(p))
                    if (At<FEPackage *>(p, 0x3c) == c->package)
                        At<FEPackage *>(p, 0x3c) = parent;
            }
            else
            {
                replaced = c->package;
                replacedParent = At<FEPackage *>(replaced, 0x3c);
            }
            if (FEPackage *parent = At<FEPackage *>(c->package, 0x3c))
                At<unsigned int>(parent, 0x20) = At<unsigned int>(parent, 0x24);
            e->UnloadPackage(c->package);
            --place;
        }
        if (c->what & kPush)
        {
            FEPackage *pushed = e->PushPackage(c->name, static_cast<unsigned char>(place + 1), c->pads);
            bool relink = false;
            if (!pushed)
                relink = (c->what & kPop) != 0;
            else if (!(c->what & kPop))
            {
                if (place >= 0)
                    At<FEPackage *>(pushed, 0x3c) = c->package;
                else
                    relink = false;
            }
            else
                relink = true;
            if (relink && pushed)
            {
                At<FEPackage *>(pushed, 0x3c) = replacedParent;
                for (FEPackage *p = FirstPackage(e); p; p = NextPackage(p))
                    if (At<FEPackage *>(p, 0x3c) == replaced)
                        At<FEPackage *>(p, 0x3c) = pushed;
            }
        }
        if (c->what & kPadsUp)
        {
            FEPackage *from = c->package;
            if (FEPackage *parent = At<FEPackage *>(from, 0x3c))
            {
                const unsigned int moved = At<unsigned int>(from, 0x20) & c->pads;
                At<unsigned int>(from, 0x20) &= ~moved;
                At<unsigned int>(parent, 0x20) |= moved;
                e->QueueMessage(kPadsChanged, nullptr, parent, reinterpret_cast<FEObject *>(kToPackageAndTargets), c->pads);
            }
        }
        if (c->what & kPadsDown)
            for (FEPackage *p = FirstPackage(e); p; p = NextPackage(p))
                if (At<FEPackage *>(p, 0x3c) == c->package)
                {
                    const unsigned int moved = At<unsigned int>(c->package, 0x20) & c->pads;
                    At<unsigned int>(c->package, 0x20) &= ~moved;
                    At<unsigned int>(p, 0x20) |= moved;
                    e->QueueMessage(kPadsChanged, nullptr, p, reinterpret_cast<FEObject *>(kToPackageAndTargets), c->pads);
                    break;
                }
        Delete(c);
    }
}

void FEngine::QueuePackageSwitch(char const *name, unsigned int pads)
{
    QueuePackageCommand(3, pads, name);
}

void FEngine::QueuePackagePush(char const *name, unsigned int pads)
{
    QueuePackageCommand(2, pads, name);
}

void FEngine::QueuePackagePop()
{
    QueuePackageCommand(1, 0, nullptr);
}
