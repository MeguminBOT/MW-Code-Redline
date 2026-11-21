/** HUD elements' objects (original: zFe2 HudElement::Toggle 0x5793f0, RegisterGroup 0x58bbd0, destructor). */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x59c930, HudElementDestroy)
NFS_REPLACE(0x59c960, HudElementUpdate)
NFS_REPLACE(0x59c970, HudElementDelete)

namespace
{
const int kObjects = 8;
const int kPackage = 0x10;
const int kShownLow = 0x18;
const int kShownHigh = 0x1c;
const int kMaskLow = 0x20;
const int kMaskHigh = 0x24;
const int kGroupFirstChild = 0x64;
const int kObjectNext = 4;
const int kObjectHash = 0x10;
const int kObjectType = 0x18;
const int kTypeGroup = 5;
void *const kHudElementVtable = reinterpret_cast<void *>(0x8a1e80);

struct Node
{
    Node *next;
    Node *prev;
    FEObject *object;
};

template <typename T> T &At(void *base, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}
} // namespace

void HudElement::Toggle(unsigned __int64 mask)
{
    const unsigned int low = static_cast<unsigned int>(mask);
    const unsigned int high = static_cast<unsigned int>(mask >> 32);
    At<unsigned int>(this, kMaskLow) = low;
    At<unsigned int>(this, kMaskHigh) = high;
    const bool visible = ((At<unsigned int>(this, kShownLow) & low) | (At<unsigned int>(this, kShownHigh) & high)) != 0;
    Node *head = &At<Node>(this, kObjects);
    for (Node *n = head->next; n != head; n = n->next)
    {
        if (visible)
            FEngSetVisible(n->object);
        else
            FEngSetInvisible(n->object);
    }
}

FEGroup *HudElement::RegisterGroup(unsigned int hash)
{
    FEGroup *group = FEngFindGroup(At<const char *>(this, kPackage), hash);
    if (!group)
        return nullptr;
    for (FEObject *child = At<FEObject *>(group, kGroupFirstChild); child; child = At<FEObject *>(child, kObjectNext))
    {
        if (At<int>(child, kObjectType) == kTypeGroup)
            RegisterGroup(At<unsigned int>(child, kObjectHash));
        else
            NFS_ORIGINAL(0x58af10, void *(__thiscall *)(void *, FEObject *))(&At<Node>(this, kObjects), child);
    }
    return group;
}

void __fastcall HudElementDestroy(HudElement *element)
{
    *reinterpret_cast<void **>(element) = kHudElementVtable;
    Node *head = &At<Node>(element, kObjects);
    while (head->next != head)
    {
        Node *node = head->next;
        node->prev->next = node->next;
        node->next->prev = node->prev;
        bPNode::Free(node);
    }
}

void __fastcall HudElementUpdate(HudElement *, void *, IPlayer *)
{
}

HudElement *__fastcall HudElementDelete(HudElement *element, void *, unsigned int flags)
{
    HudElementDestroy(element);
    if (flags & 1)
        NFS_ORIGINAL(0x652b00, void(__cdecl *)(void *))(element);
    return element;
}