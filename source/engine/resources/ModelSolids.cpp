/**
 * Models and their solids (original: zEcstasy eModel, eFindSolid): a model names its solid by hash and is connected to it
 * while the solid is loaded (the solid's model list at +0x88), or waits on the unattached list (UnattachedModelList,
 * 0x917400) until it is; the solid lookup goes through every loaded solid list (moving the list it found it in to the
 * front) or one named list.
 *
 * eModel: list node (next, prev), +8 the solid's name hash, +0xc the solid, +0x10 a replacement texture table, +0x14.
 * The original timed every lookup into TotalFindSolidTime (two ticker reads a call; nothing reads the total): left out.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x5016d0, ModelPositionMarker)
NFS_REPLACE(0x5019d0, ClearSixWords)

void *__fastcall SolidPositionMarker(const char *self, void *, unsigned int name);

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
};

Node *const kUnattached = reinterpret_cast<Node *>(0x917400);
Node *const kSolidLists = reinterpret_cast<Node *>(0x91a0d0);
const unsigned char *const kSolidLoaded = reinterpret_cast<const unsigned char *>(0x9175dc);

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

void *Lookup(unsigned int hash, char *list)
{
    char *entry = static_cast<char *>(ScanHashTableKey32(hash, At<void *>(list, 0x74), At<int>(list, 0xc), 0, 8));
    return entry ? At<void *>(entry, 4) : nullptr;
}
} // namespace

eSolid *__cdecl eFindSolid(unsigned int hash, eSolidListHeader *list)
{
    if (!kSolidLoaded[hash & 0x1fff])
        return nullptr;
    if (list)
        return static_cast<eSolid *>(Lookup(hash, reinterpret_cast<char *>(list)));
    for (Node *n = kSolidLists->next; n != kSolidLists; n = n->next)
    {
        if (!n)
            continue;
        if (void *solid = Lookup(hash, reinterpret_cast<char *>(n)))
        {
            // The list it was in goes to the front: the next lookup likely wants the same one.
            Unlink(n);
            Node *first = kSolidLists->next;
            kSolidLists->next = n;
            first->prev = n;
            n->prev = kSolidLists;
            n->next = first;
            return static_cast<eSolid *>(solid);
        }
    }
    return nullptr;
}

void eModel::UnInit()
{
    char *self = reinterpret_cast<char *>(this);
    if (At<int>(self, 8) || At<void *>(self, 0xc))
        Unlink(reinterpret_cast<Node *>(self));
    At<void *>(self, 0xc) = nullptr;
    At<int>(self, 8) = 0;
    if (eReplacementTextureTable *table = At<eReplacementTextureTable *>(self, 0x10))
        RemoveReplacementTextureTableFixup(table);
    At<void *>(self, 0x10) = nullptr;
    At<int>(self, 0x14) = 0;
}

void eModel::ConnectSolid(eSolid *solid)
{
    char *self = reinterpret_cast<char *>(this);
    if (!At<int>(self, 8) || solid == At<eSolid *>(self, 0xc))
        return;
    Unlink(reinterpret_cast<Node *>(self));
    if (solid)
    {
        Append(&At<Node>(solid, 0x88), reinterpret_cast<Node *>(self));
        At<eSolid *>(self, 0xc) = solid;
        return;
    }
    Append(kUnattached, reinterpret_cast<Node *>(self));
    At<eSolid *>(self, 0xc) = nullptr;
}

void eModel::Init(unsigned int hash)
{
    char *self = reinterpret_cast<char *>(this);
    At<unsigned int>(self, 8) = hash;
    At<void *>(self, 0x10) = nullptr;
    At<int>(self, 0x14) = 0;
    if (hash)
        Append(kUnattached, reinterpret_cast<Node *>(self));
    if (At<unsigned int>(self, 8))
        ConnectSolid(eFindSolid(At<unsigned int>(self, 8), nullptr));
}

int eModel::GetBoundingBox(bVector3 *low, bVector3 *high)
{
    const char *solid = At<char *>(this, 0xc);
    if (!solid)
    {
        low->x = low->y = low->z = 0.0f;
        high->x = high->y = high->z = 0.0f;
        return 0;
    }
    const float *b = reinterpret_cast<const float *>(solid + 0x20);
    low->x = b[0];
    low->y = b[1];
    low->z = b[2];
    high->x = b[4];
    high->y = b[5];
    high->z = b[6];
    return 1;
}

ePositionMarker *eModel::GetPostionMarker(ePositionMarker *after)
{
    const char *solid = At<char *>(this, 0xc);
    if (!solid)
        return nullptr;
    const unsigned int first = *reinterpret_cast<const unsigned int *>(solid + 0x80);
    const int count = static_cast<signed char>(solid[0x1b]);
    if (!first || !count)
        return nullptr;
    const unsigned int at = reinterpret_cast<unsigned int>(after);
    if (!after)
        return reinterpret_cast<ePositionMarker *>(first);
    // The markers are 0x50 each; the next after `after`, while there is one.
    if (first <= at && at < first + (count * 5 - 5) * 0x10)
        return reinterpret_cast<ePositionMarker *>(at + 0x50);
    return nullptr;
}

/** eModel::GetPositionMarker(name) (0x5016d0): its solid's marker of that name. */
void *__fastcall ModelPositionMarker(const char *model, void *, unsigned int name)
{
    const char *solid = *reinterpret_cast<const char *const *>(model + 0xc);
    return solid ? SolidPositionMarker(solid, nullptr, name) : nullptr;
}

/** Zeroes six words from +8 (original sub_5019D0, thiscall). */
void __fastcall ClearSixWords(char *self, void *)
{
    for (int i = 0; i < 6; ++i)
        At<int>(self, 8 + i * 4) = 0;
}
