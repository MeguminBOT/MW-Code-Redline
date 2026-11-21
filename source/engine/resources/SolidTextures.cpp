/**
 * Solids following their textures (original: zEcstasy eSolid, eSolidListHeader): when a texture pack loads, unloads or
 * moves in memory, each solid's texture slots (eSolid +0x2c: (name hash, TextureInfo) pairs, +0x19 their count) are
 * pointed at the textures now loaded, the default texture standing in for one that is not.
 *
 * A solid list (eSolidListHeader) keeps its fix-up info to know which lists a pack touches: +0x80 the packs its solids'
 * textures come from, +0x88 the slots now on the default texture, with their counts at +0x7c and +0x7e (-1: to be
 * rebuilt); +0xc its solid count, +0x74 its (name hash, solid) table.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x504380, AddPointerHead)
NFS_REPLACE(0x5043c0, AddPointerHead)
NFS_REPLACE(0x506d00, RebuildSolidTextureFixupInfo)

namespace
{
template <typename T> T &At(void *p, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(p) + offset);
}

struct Node
{
    Node *next;
    Node *prev;
    void *data;
};

Node *const kSolidLists = reinterpret_cast<Node *>(0x91a0d0);

void Unlink(Node *n)
{
    n->prev->next = n->next;
    n->next->prev = n->prev;
}

char *Slot(char *solid, int i)
{
    return At<char *>(solid, 0x2c) + i * 8;
}

int SlotCount(const char *solid)
{
    return static_cast<signed char>(solid[0x19]);
}

char *ListSolid(char *list, int i)
{
    return At<char *>(At<char *>(list, 0x74) + i * 8, 4);
}

bool HasPack(char *list, TexturePack *pack)
{
    Node *head = &At<Node>(list, 0x80);
    for (Node *n = head->next; n != head; n = n->next)
        if (n->data == pack)
            return true;
    return false;
}
} // namespace

/** bPList::AddHead (two copies in the original, thiscall): a pointer node at the list's head. */
void *__fastcall AddPointerHead(void *listObject, void *, void *data)
{
    Node *list = static_cast<Node *>(listObject);
    Node *n = static_cast<Node *>(bPNode::Malloc());
    if (n)
        n->data = data;
    Node *first = list->next;
    list->next = n;
    first->prev = n;
    n->prev = list;
    n->next = first;
    return n;
}

bool eSolid::NotifyTextureLoading(TexturePack *pack)
{
    char *self = reinterpret_cast<char *>(this);
    bool changed = false;
    for (int i = 0; i < SlotCount(self); ++i)
    {
        char *slot = Slot(self, i);
        char *texture = At<char *>(slot, 4);
        if (texture == reinterpret_cast<char *>(DefaultTextureInfo))
            if (char *loaded = reinterpret_cast<char *>(pack->GetLoadedTexture(At<unsigned int>(slot, 0))))
            {
                loaded[0x53] = 1;
                texture = loaded;
            }
        if (texture != At<char *>(slot, 4))
        {
            At<char *>(slot, 4) = texture;
            changed = true;
        }
    }
    return changed;
}

bool eSolid::NotifyTextureUnloading(TexturePack *pack)
{
    char *self = reinterpret_cast<char *>(this);
    bool changed = false;
    for (int i = 0; i < SlotCount(self); ++i)
    {
        char *slot = Slot(self, i);
        char *texture = At<char *>(slot, 4);
        const unsigned int hash = At<unsigned int>(slot, 0);
        if (At<TexturePack *>(texture, 0x68) == pack && !pack->GetLoadedTexture(hash))
        {
            // Another pack's copy, or the default texture.
            if (char *other = reinterpret_cast<char *>(GetTextureInfo(hash, 0, 0)))
                texture = other;
            else
            {
                texture = reinterpret_cast<char *>(DefaultTextureInfo);
                if (!texture)
                    bBreak();
            }
        }
        if (texture != At<char *>(slot, 4))
        {
            At<char *>(slot, 4) = texture;
            changed = true;
        }
    }
    return changed;
}

void __cdecl EmptySolidTextureFixupInfo(eSolidListHeader *header)
{
    char *list = reinterpret_cast<char *>(header);
    const int lists[2] = {0x80, 0x88};
    for (int offset : lists)
    {
        Node *head = &At<Node>(list, offset);
        while (head->next != head)
        {
            Node *n = head->next;
            Unlink(n);
            bPNode::Free(n);
        }
    }
    At<short>(list, 0x7c) = 0;
    At<short>(list, 0x7e) = 0;
}

void __cdecl RebuildSolidTextureFixupInfo(eSolidListHeader *header)
{
    char *list = reinterpret_cast<char *>(header);
    EmptySolidTextureFixupInfo(header);
    for (int s = 0; s < At<int>(list, 0xc); ++s)
    {
        char *solid = ListSolid(list, s);
        if (!solid)
            continue;
        for (int i = 0; i < SlotCount(solid); ++i)
        {
            char *slot = Slot(solid, i);
            char *texture = At<char *>(slot, 4);
            if (texture == reinterpret_cast<char *>(DefaultTextureInfo))
            {
                // Waiting slots, once per name.
                Node *head = &At<Node>(list, 0x88);
                bool known = false;
                for (Node *n = head->next; n != head && !known; n = n->next)
                    known = *static_cast<unsigned int *>(n->data) == At<unsigned int>(texture, 0x24);
                if (!known)
                {
                    AddPointerHead(head, nullptr, slot);
                    ++At<short>(list, 0x7e);
                }
            }
            else if (!HasPack(list, At<TexturePack *>(texture, 0x68)))
            {
                AddPointerHead(&At<Node>(list, 0x80), nullptr, At<TexturePack *>(texture, 0x68));
                ++At<short>(list, 0x7c);
            }
        }
    }
}

void __cdecl eSolidNotifyTextureMoving(TexturePack *pack, TextureInfo *moved)
{
    const int offset = ChunkMovementOffset;
    for (Node *l = kSolidLists->next; l != kSolidLists; l = l->next)
    {
        char *list = reinterpret_cast<char *>(l);
        if (At<short>(list, 0x7c) == -1)
            RebuildSolidTextureFixupInfo(reinterpret_cast<eSolidListHeader *>(list));
        if (!HasPack(list, pack))
            continue;
        for (int s = 0; s < At<int>(list, 0xc); ++s)
        {
            char *solid = ListSolid(list, s);
            if (!solid)
                continue;
            for (int i = 0; i < SlotCount(solid); ++i)
            {
                char *&texture = At<char *>(Slot(solid, i), 4);
                if (moved ? texture == reinterpret_cast<char *>(moved) : At<TexturePack *>(texture, 0x68) == pack)
                    texture += offset;
            }
        }
    }
}

void __cdecl eSolidNotifyTextureLoading(TexturePack *pack, TextureInfo *texture, bool loading)
{
    if (!loading)
    {
        if (ChunkMovementOffset)
        {
            eSolidNotifyTextureMoving(pack, texture);
            return;
        }
        for (Node *l = kSolidLists->next; l != kSolidLists; l = l->next)
        {
            char *list = reinterpret_cast<char *>(l);
            if (At<short>(list, 0x7c) == -1)
                RebuildSolidTextureFixupInfo(reinterpret_cast<eSolidListHeader *>(list));
            if (!HasPack(list, pack))
                continue;
            bool changed = false;
            for (int s = 0; s < At<int>(list, 0xc); ++s)
                if (char *solid = ListSolid(list, s))
                    changed |= reinterpret_cast<eSolid *>(solid)->NotifyTextureUnloading(pack);
            if (changed)
                RebuildSolidTextureFixupInfo(reinterpret_cast<eSolidListHeader *>(list));
        }
        return;
    }
    if (ChunkMovementOffset)
        return;
    for (Node *l = kSolidLists->next; l != kSolidLists; l = l->next)
    {
        char *list = reinterpret_cast<char *>(l);
        if (At<short>(list, 0x7c) == -1)
            RebuildSolidTextureFixupInfo(reinterpret_cast<eSolidListHeader *>(list));
        // Waiting slots the pack now has: off the waiting list.
        Node *head = &At<Node>(list, 0x88);
        bool changed = false;
        for (Node *n = head->next, *next; n != head; n = next)
        {
            next = n->next;
            char *slot = static_cast<char *>(n->data);
            char *now = At<char *>(slot, 4);
            if (now == reinterpret_cast<char *>(DefaultTextureInfo))
                if (char *loaded = reinterpret_cast<char *>(pack->GetLoadedTexture(At<unsigned int>(slot, 0))))
                {
                    loaded[0x53] = 1;
                    now = loaded;
                }
            if (now != At<char *>(slot, 4))
            {
                Unlink(n);
                bPNode::Free(n);
                --At<short>(list, 0x7e);
                changed = true;
            }
        }
        if (!changed)
            continue;
        if (!HasPack(list, pack))
        {
            AddPointerHead(&At<Node>(list, 0x80), nullptr, pack);
            ++At<short>(list, 0x7c);
        }
        for (int s = 0; s < At<int>(list, 0xc); ++s)
            if (char *solid = ListSolid(list, s))
                reinterpret_cast<eSolid *>(solid)->NotifyTextureLoading(pack);
    }
}
