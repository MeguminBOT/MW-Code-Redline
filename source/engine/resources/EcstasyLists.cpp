/**
 * The emptying of zEcstasy's intrusive lists (original: the bTList<T> destructors and clear helpers the compiler made
 * for each element type, thiscall on the list head): each node is unlinked and its element destroyed and freed the way
 * that type is allocated. Some take the list at an offset in their owner (the owner's destructor).
 */
#include "game/gen/api.h"
#include "legacy/link.h"

struct EcstasyListNode
{
    EcstasyListNode *next;
    EcstasyListNode *prev;
};

NFS_REPLACE(0x504350, DeleteVirtualList)
NFS_REPLACE(0x506020, DeleteVirtualListAt44)
NFS_REPLACE(0x506300, FreeFastMemList30)
NFS_REPLACE(0x507760, FreeFastMemListAt58)
NFS_REPLACE(0x507750, InitListAt58)
NFS_REPLACE(0x506340, FreeStreamingPackList)
NFS_REPLACE(0x506920, FreeStreamingPackListAt18)
NFS_REPLACE(0x506380, DeleteListOfMembersAt4)
NFS_REPLACE(0x5063c0, FreeTexturePackList)
NFS_REPLACE(0x506400, FreeTextureAnimPackList)
NFS_REPLACE(0x506520, DeleteList)
NFS_REPLACE(0x508120, DeleteList2)
NFS_REPLACE(0x508150, DeleteList3)
NFS_REPLACE(0x508180, DeleteList4)
NFS_REPLACE(0x506630, FreePNodeList)
NFS_REPLACE(0x5080b0, FreeModelList)
NFS_REPLACE(0x5083b0, FreeModelListAt88)
NFS_REPLACE(0x508310, FreeFixupLists)
NFS_REPLACE(0x5083c0, DeleteListAt2c)
NFS_REPLACE(0x509280, DeleteListOfFastMemOwners)
NFS_REPLACE(0x509480, FreeFastMemVector)
NFS_REPLACE(0x509620, DeleteListOfFixupOwners)
NFS_REPLACE(0x509660, DeleteListOfModelOwners)
NFS_REPLACE(0x5096a0, DeleteListOfListOwners)

using Node = EcstasyListNode;

void __fastcall DeleteVirtualList(Node *list);
void __fastcall DeleteVirtualListAt44(char *owner);
void __fastcall FreeFastMemList30(Node *list);
void __fastcall FreeFastMemListAt58(char *owner);
char *__fastcall InitListAt58(char *owner);
void __fastcall FreeStreamingPackList(Node *list);
void __fastcall FreeStreamingPackListAt18(char *owner);
void __fastcall DeleteListOfMembersAt4(Node *list);
void __fastcall FreeTexturePackList(Node *list);
void __fastcall FreeTextureAnimPackList(Node *list);
void __fastcall DeleteList(Node *list);
void __fastcall DeleteList2(Node *list);
void __fastcall DeleteList3(Node *list);
void __fastcall DeleteList4(Node *list);
void __fastcall FreePNodeList(Node *list);
void __fastcall FreeModelList(Node *list);
void __fastcall FreeModelListAt88(char *owner);
void __fastcall FreeFixupLists(char *owner);
void __fastcall DeleteListAt2c(char *owner);
void __fastcall DeleteListOfFastMemOwners(Node *list);
void __fastcall FreeFastMemVector(char *vector);
void __fastcall DeleteListOfFixupOwners(Node *list);
void __fastcall DeleteListOfModelOwners(Node *list);
void __fastcall DeleteListOfListOwners(Node *list);
void __fastcall DestroyTexturePack(TexturePack *pack);

namespace
{
void Unlink(Node *n)
{
    n->prev->next = n->next;
    n->next->prev = n->prev;
}

Node *ListAt(char *owner, int offset)
{
    return reinterpret_cast<Node *>(owner + offset);
}

/** FastMem::Free on gFastMem (0x925b30). */
void FastMemFree(void *block, unsigned int size)
{
    NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(
        reinterpret_cast<void *>(0x925b30), block, size, nullptr);
}

/** Unlinks every node and hands it to `destroy`. */
template <typename F> void Drain(Node *list, F destroy)
{
    for (Node *n = list->next; n != list; n = list->next)
    {
        Unlink(n);
        destroy(n);
    }
}
} // namespace

/** Elements with a virtual destructor whose link sits at +4 (after the vtable): deleted through it. */
void __fastcall DeleteVirtualList(Node *list)
{
    Drain(list, [](Node *n) {
        char *object = reinterpret_cast<char *>(n) - 4;
        if (object)
        {
            typedef void(__thiscall * Destructor)(void *, int);
            (*reinterpret_cast<Destructor **>(object))[1](object, 1);
        }
    });
}

void __fastcall DeleteVirtualListAt44(char *owner)
{
    DeleteVirtualList(ListAt(owner, 0x44));
}

void __fastcall FreeFastMemList30(Node *list)
{
    Drain(list, [](Node *n) { FastMemFree(n, 0x30); });
}

void __fastcall FreeFastMemListAt58(char *owner)
{
    FreeFastMemList30(ListAt(owner, 0x58));
}

char *__fastcall InitListAt58(char *owner)
{
    Node *list = ListAt(owner, 0x58);
    list->next = list->prev = list;
    return owner;
}

void __fastcall FreeStreamingPackList(Node *list)
{
    Drain(list, [](Node *n) { bFree(eStreamingPackSlotPool, n); });
}

void __fastcall FreeStreamingPackListAt18(char *owner)
{
    FreeStreamingPackList(ListAt(owner, 0x18));
}

/** Elements whose link sits at +4: deleted. */
void __fastcall DeleteListOfMembersAt4(Node *list)
{
    Drain(list, [](Node *n) {
        char *object = reinterpret_cast<char *>(n) - 4;
        if (object)
            ::operator delete(object);
    });
}

void __fastcall FreeTexturePackList(Node *list)
{
    Drain(list, [](Node *n) {
        DestroyTexturePack(reinterpret_cast<TexturePack *>(n));
        bFree(TexturePackSlotPool, n);
    });
}

void __fastcall FreeTextureAnimPackList(Node *list)
{
    Drain(list, [](Node *n) {
        reinterpret_cast<TextureAnimPack *>(n)->~TextureAnimPack();
        bFree(TexturePackSlotPool, n);
    });
}

void __fastcall DeleteList(Node *list)
{
    Drain(list, [](Node *n) { ::operator delete(n); });
}

void __fastcall DeleteList2(Node *list)
{
    DeleteList(list);
}

void __fastcall DeleteList3(Node *list)
{
    DeleteList(list);
}

void __fastcall DeleteList4(Node *list)
{
    DeleteList(list);
}

void __fastcall FreePNodeList(Node *list)
{
    Drain(list, [](Node *n) { bPNode::Free(n); });
}

void __fastcall FreeModelList(Node *list)
{
    Drain(list, [](Node *n) {
        reinterpret_cast<eModel *>(n)->~eModel();
        bFree(eModelSlotPool, n);
    });
}

void __fastcall FreeModelListAt88(char *owner)
{
    FreeModelList(ListAt(owner, 0x88));
}

/** eSolidListHeader-style owner's texture fix-up lists (+0x88, then +0x80; see SolidTextures.cpp). */
void __fastcall FreeFixupLists(char *owner)
{
    FreePNodeList(ListAt(owner, 0x88));
    FreePNodeList(ListAt(owner, 0x80));
}

void __fastcall DeleteListAt2c(char *owner)
{
    DeleteList(ListAt(owner, 0x2c));
}

void __fastcall DeleteListOfFastMemOwners(Node *list)
{
    Drain(list, [](Node *n) {
        FreeFastMemListAt58(reinterpret_cast<char *>(n));
        ::operator delete(n);
    });
}

/** A vector of 8-byte elements on gFastMem (+4 begin, +8 end, +0xc capacity end): freed and emptied. */
void __fastcall FreeFastMemVector(char *vector)
{
    char *&begin = *reinterpret_cast<char **>(vector + 4);
    if (begin)
        FastMemFree(begin, static_cast<unsigned int>((*reinterpret_cast<char **>(vector + 0xc) - begin) >> 3 << 3));
    begin = nullptr;
    *reinterpret_cast<char **>(vector + 8) = nullptr;
    *reinterpret_cast<char **>(vector + 0xc) = nullptr;
}

void __fastcall DeleteListOfFixupOwners(Node *list)
{
    Drain(list, [](Node *n) {
        FreeFixupLists(reinterpret_cast<char *>(n));
        ::operator delete(n);
    });
}

/** Elements whose link sits at +4, each owning a model list at +0x88. */
void __fastcall DeleteListOfModelOwners(Node *list)
{
    Drain(list, [](Node *n) {
        char *object = reinterpret_cast<char *>(n) - 4;
        if (object)
        {
            FreeModelListAt88(object);
            ::operator delete(object);
        }
    });
}

void __fastcall DeleteListOfListOwners(Node *list)
{
    Drain(list, [](Node *n) {
        DeleteListAt2c(reinterpret_cast<char *>(n));
        ::operator delete(n);
    });
}
