/**
 * Texture pack and solid list chunk loaders (original: zEcstasy LoaderTexturePack, UnloaderTexturePack, LoaderSolidList
 * and their helpers): the bChunk handlers that link a loaded pack's data into the engine and take it out again.
 *
 * A texture pack chunk (0xb3300000) holds its header chunk (0xb3310000: header 0x33310001, index 0x33310002, 0x33310003,
 * the TextureInfos 0x33310004, their platform infos 0x33310005) and, for packs streamed as one, its image data
 * (0xb3320000). Image data chunks arriving on their own wait in the VRAM data list (0x91c178) until the pack with their
 * name hash loads. An animated-texture chunk (0xb0300100) makes a TextureAnimPack of the pack loaded last.
 *
 * TexturePackHeader: +0x04 name, +0x20 file name, +0x60 name hash, +0x6c set once loaded, +0x70 its TexturePack, +0x74 the
 * index table, +0x78 its second table. TexturePack (from TexturePackSlotPool, in the pack list 0x9173f8): +0x08, +0x0c the
 * shared name strings, +0x10 the name hash, +0x14 the header, +0x18 the index count, +0x1c the image data size.
 *
 * A solid list chunk (0x80134000) holds a list header chunk (0x80134001: header 0x134002, solid table 0x134003, 0x134004)
 * and the solids (0x80134010: solid 0x134011, texture slots 0x134012, light material slots 0x134013, 0x134017..0x13401b,
 * platform data 0x80134100). Each loaded solid goes into the solid list (0x916a70) and its slot in its list's table; the
 * models waiting for a solid of their name (the unattached list 0x917400) are attached to it, and moved off it again when
 * it unloads. While chunks move in memory (ChunkMovementOffset) models are parked in the moved list (0x9175d0).
 *
 * Changes: the anim chunk no longer calls InitAnims on a null pack when TexturePackSlotPool is full, and unloading
 * solids without a list header no longer writes through null; both crashed in the original. NotifySolidUnloader stops
 * at a model it cannot move instead of looping on it forever. The original's loop of unused bStringHash("DEFAULT") calls
 * per texture slot is left out. UnloaderLights returns when a light pack chunk does not start with its pack sub-chunk
 * (the original loops forever there).
 */
#include <new>
#include <string.h>

#include "game/gen/api.h"
#include "legacy/link.h"
#include "engine/resources/streamprofile.h"

NFS_REPLACE(0x502d10, DestroyTexturePack)
NFS_REPLACE(0x501760, LoadSolidListHeaderChunks)
NFS_REPLACE(0x501870, LoadStreamedSolidListHeader)
NFS_REPLACE(0x5051d0, FindSolidTextures)
NFS_REPLACE(0x5070e0, LoadSolidChunks)
NFS_REPLACE(0x507620, UnloaderSolidList)
NFS_REPLACE(0x507680, LoadStreamedSolidChunks)
NFS_REPLACE(0x6be430, SolidPlatChunksComplete)
NFS_REPLACE(0x6be460, eLoadSolidListPlatChunks)
NFS_REPLACE(0x6be180, CreateLightMaterialPlatInfo)

struct SolidPlatInfo;
int __fastcall SolidLoaderPlatChunks(SolidPlatInfo **self, void *, const unsigned int *chunk);

void __fastcall DestroyTexturePack(TexturePack *pack);
eSolidListHeader *__cdecl LoadSolidListHeaderChunks(bChunk *chunk);
void __cdecl LoadStreamedSolidListHeader(void *info);
void __fastcall FindSolidTextures(eSolid *solid);
eSolid *__cdecl LoadSolidChunks(bChunk *chunk, eSolidListHeader *header);
int __cdecl UnloaderSolidList(bChunk *chunk);
void __cdecl LoadStreamedSolidChunks(bChunk *chunk, void *entry, void *pack);
int __fastcall SolidPlatChunksComplete(eSolid *solid);
int __cdecl eLoadSolidListPlatChunks(bChunk *chunk);
void __fastcall CreateLightMaterialPlatInfo(void *material);

namespace
{
template <typename T> T &At(void *p, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(p) + offset);
}

struct Chunk
{
    unsigned int Id;
    unsigned int Size;
};

Chunk *AsChunk(bChunk *c)
{
    return reinterpret_cast<Chunk *>(c);
}

char *Data(Chunk *c)
{
    return reinterpret_cast<char *>(c + 1);
}

Chunk *End(Chunk *c)
{
    return reinterpret_cast<Chunk *>(Data(c) + c->Size);
}

/** A chunk's data aligned to 16 bytes (as the packer aligns it). */
char *Aligned16(Chunk *c)
{
    return reinterpret_cast<char *>((reinterpret_cast<unsigned int>(c) + 0x17) & ~0xfu);
}

struct Node
{
    Node *next;
    Node *prev;
};

void Unlink(Node *n)
{
    n->prev->next = n->next;
    n->next->prev = n->prev;
}

void AddTail(Node *list, Node *n)
{
    Node *tail = list->prev;
    tail->next = n;
    list->prev = n;
    n->prev = tail;
    n->next = list;
}

void InitList(Node *list)
{
    list->next = list;
    list->prev = list;
}

Node *const kTexturePacks = reinterpret_cast<Node *>(0x9173f8);
Node *const kVRAMData = reinterpret_cast<Node *>(0x91c178);
Node *const kSolidListHeaders = reinterpret_cast<Node *>(0x91a0d0);
Node *const kSolids = reinterpret_cast<Node *>(0x916a70);
/** Solids whose platform data failed to load. */
Node *const kFailedSolids = reinterpret_cast<Node *>(0x91c168);
Node *const kUnattachedModels = reinterpret_cast<Node *>(0x917400);
Node *const kMovedModels = reinterpret_cast<Node *>(0x9175d0);
/** eLightMaterial list: its nodes are at a material's +4, so its head belongs to a pseudo material at 0x91c16c. */
char *const kLightMaterials = reinterpret_cast<char *>(0x91c16c);
Node *const kLightFlarePacks = reinterpret_cast<Node *>(0x91757c);
Node *const kLightPacks = reinterpret_cast<Node *>(0x917574);
/** The light material used while no "DEFAULT" one is loaded. */
void *const kBuiltInLightMaterial = reinterpret_cast<void *>(0x917408);
const unsigned int kDefaultLightMaterialName = 0x2c420d64;

const unsigned int kTexturePackChunk = 0xb3300000;
const unsigned int kTexturePackHeaderChunk = 0xb3310000;
const unsigned int kTextureVRAMDataChunk = 0xb3320000;
const unsigned int kTextureAnimChunk = 0xb0300100;
const unsigned int kSolidListChunk = 0x80134000;
const unsigned int kSolidListHeaderChunk = 0x80134001;
const unsigned int kSolidListPlatChunk = 0x80134008;
const unsigned int kSolidChunk = 0x80134010;
const unsigned int kSolidPlatChunk = 0x80134100;

int &SolidListCount()
{
    return *reinterpret_cast<int *>(0x915180);
}

int &SolidCount()
{
    return *reinterpret_cast<int *>(0x915184);
}

int &SolidBytes()
{
    return *reinterpret_cast<int *>(0x915188);
}

int &SolidVertexBytes()
{
    return *reinterpret_cast<int *>(0x91518c);
}

int &SolidExtraBytes()
{
    return *reinterpret_cast<int *>(0x915190);
}

int &SolidLoadedTotal()
{
    return *reinterpret_cast<int *>(0x9175d8);
}

signed char &SolidLoadedByName(unsigned int hash)
{
    return reinterpret_cast<signed char *>(0x9175dc)[hash & 0x1fff];
}

float &MaxNotifyTimeUnload()
{
    return *reinterpret_cast<float *>(0x915fc0);
}

int &NumNotifyUnloads()
{
    return *reinterpret_cast<int *>(0x915fd4);
}

void *&DefaultLightMaterialPointer()
{
    return *reinterpret_cast<void **>(0x8f23e8);
}

/** A model waiting for, or attached to, a solid: in the unattached list or its solid's model list (+0x88). */
struct ModelNode
{
    Node link;
    unsigned int nameHash;
    char *solid;
};

Node *ModelList(char *solid)
{
    return &At<Node>(solid, 0x88);
}

/** Moves a model to `solid`'s model list, or to the unattached list without one. */
void Reattach(ModelNode *model, char *solid)
{
    Unlink(&model->link);
    AddTail(solid ? ModelList(solid) : kUnattachedModels, &model->link);
    model->solid = solid;
}

/** The light material named `hash`, or the default one (the list is searched from its start for every slot). */
void *FindLightMaterial(unsigned int hash)
{
    for (char *link = At<char *>(kLightMaterials, 4);;)
    {
        char *material = link ? link - 4 : nullptr;
        if (material == kLightMaterials)
            return DefaultLightMaterialPointer();
        if (At<unsigned int>(material, 0xc) == hash)
            return material;
        link = At<char *>(material, 4);
    }
}
} // namespace

TextureVRAMDataHeader *__cdecl InternalLoadVRAMDataChunks(bChunk *chunkObject)
{
    char *chunk = reinterpret_cast<char *>(chunkObject);
    // The node sits in the chunk's own data; its image data chunk follows the padding (+0xc).
    char *node = chunk + 0x10;
    char *data = chunk + At<int>(chunk, 0xc) + 0x10;
    if (!At<int>(node, 0x10))
        At<int>(node, 0x10) = 1;
    At<char *>(node, 0x14) = data + At<int>(data, 4) + 8;
    AddTail(kVRAMData, reinterpret_cast<Node *>(node));
    return reinterpret_cast<TextureVRAMDataHeader *>(node);
}

void __fastcall DestroyTexturePack(TexturePack *pack)
{
    Unlink(reinterpret_cast<Node *>(pack));
    bFreeSharedString(At<const char *>(pack, 8));
    bFreeSharedString(At<const char *>(pack, 0xc));
}

TexturePackHeader *__cdecl InternalLoadTexturePackHeaderChunks(bChunk *chunkObject)
{
    Chunk *chunk = AsChunk(chunkObject);
    char *header = nullptr;
    char *textures = nullptr, *plats = nullptr;
    int indexCount = 0, textureCount = 0, platCount = 0;
    for (Chunk *c = reinterpret_cast<Chunk *>(Data(chunk)); c != End(chunk); c = End(c))
    {
        switch (c->Id)
        {
        case 0x33310001:
            header = Data(c);
            break;
        case 0x33310002:
            At<char *>(header, 0x74) = Data(c);
            indexCount = c->Size >> 3;
            break;
        case 0x33310003:
            At<char *>(header, 0x78) = Data(c);
            break;
        case 0x33310004:
            textures = Data(c);
            textureCount = c->Size / 0x7c;
            break;
        case 0x33310005:
            plats = Data(c);
            platCount = c->Size >> 5;
            break;
        }
    }
    At<int>(header, 0x6c) = 1;
    if (textureCount != platCount)
        return nullptr;

    char *pack = static_cast<char *>(bMalloc(TexturePackSlotPool));
    if (pack)
    {
        At<const char *>(pack, 8) = bAllocateSharedString(header + 4);
        At<const char *>(pack, 0xc) = bAllocateSharedString(header + 0x20);
        At<char *>(pack, 0x14) = header;
        At<unsigned int>(pack, 0x10) = bStringHash(At<const char *>(pack, 8));
        At<int>(pack, 0x18) = indexCount;
        At<int>(pack, 0x1c) = 0;
        AddTail(kTexturePacks, reinterpret_cast<Node *>(pack));
    }
    At<char *>(header, 0x70) = pack;
    if (textures)
        reinterpret_cast<TexturePack *>(pack)->AttachTextureTable(reinterpret_cast<TextureInfo *>(textures),
                                                                  reinterpret_cast<TextureInfoPlatInfo *>(plats),
                                                                  textureCount);
    return reinterpret_cast<TexturePackHeader *>(header);
}

void __cdecl InternalUnloadTexturePackHeaderChunks(bChunk *chunkObject)
{
    Chunk *chunk = AsChunk(chunkObject);
    char *header = nullptr;
    char *textures = nullptr, *plats = nullptr;
    int textureCount = 0;
    for (Chunk *c = reinterpret_cast<Chunk *>(Data(chunk)); c != End(chunk); c = End(c))
    {
        if (c->Id == 0x33310001)
            header = Data(c);
        else if (c->Id == 0x33310004)
        {
            textureCount = c->Size / 0x7c;
            textures = Data(c);
        }
        else if (c->Id == 0x33310005)
            plats = Data(c);
    }
    TexturePack *pack = At<TexturePack *>(header, 0x70);
    pack->UnAssignTextureData(0, At<int>(pack, 0x1c));
    if (textures)
        pack->UnattachTextureTable(reinterpret_cast<TextureInfo *>(textures),
                                   reinterpret_cast<TextureInfoPlatInfo *>(plats), textureCount);
    if (TexturePack *loaded = At<TexturePack *>(header, 0x70))
    {
        DestroyTexturePack(loaded);
        bFree(TexturePackSlotPool, loaded);
    }
    At<TexturePack *>(header, 0x70) = nullptr;
}

int __cdecl LoaderTexturePack(bChunk *chunkObject)
{
    nfs::streamprofile::Scope profile(nfs::streamprofile::kTexturePacks);
    Chunk *chunk = AsChunk(chunkObject);
    if (chunk->Id == kTexturePackChunk)
    {
        char *header = nullptr;
        for (Chunk *c = reinterpret_cast<Chunk *>(Data(chunk)); c != End(chunk); c = End(c))
        {
            if (c->Id == kTexturePackHeaderChunk)
            {
                header = reinterpret_cast<char *>(InternalLoadTexturePackHeaderChunks(reinterpret_cast<bChunk *>(c)));
                PrevLoadedTexturePack = At<TexturePack *>(header, 0x70);
            }
            else if (c->Id == kTextureVRAMDataChunk)
                InternalLoadVRAMDataChunks(reinterpret_cast<bChunk *>(c));
        }
        // The pack's image data, streamed with it or loaded before it.
        for (Node *n = kVRAMData->next; n != kVRAMData; n = n->next)
        {
            if (At<unsigned int>(n, 0xc) != At<unsigned int>(header, 0x60))
                continue;
            TexturePack *pack = At<TexturePack *>(header, 0x70);
            char *data = At<char *>(n, 0x14);
            char *image = reinterpret_cast<char *>((reinterpret_cast<unsigned int>(data) + 0x87) & ~0x7fu);
            const int size = static_cast<int>(data + At<int>(data, 4) + 8 - image);
            pack->AssignTextureData(image, 0, size);
            At<int>(pack, 0x1c) = size;
            Unlink(n);
            break;
        }
        return 1;
    }
    if (chunk->Id == kTextureVRAMDataChunk)
    {
        InternalLoadVRAMDataChunks(chunkObject);
        return 1;
    }
    if (chunk->Id != kTextureAnimChunk)
        return 0;

    char *self = reinterpret_cast<char *>(chunk);
    if (At<int>(self, 0x10) == 1)
    {
        // Two sub-chunks after the padding (+0xc): the animations (0x34 each), then their frames (0x10 each).
        Chunk *anims = reinterpret_cast<Chunk *>(self + At<int>(self, 0xc) + 0x10);
        Chunk *frames = End(anims);
        TextureAnimPack *animPack = nullptr;
        if (void *memory = bMalloc(TexturePackSlotPool))
            animPack = new (memory) TextureAnimPack(PrevLoadedTexturePack, reinterpret_cast<TextureAnim *>(Data(anims)),
                                                    reinterpret_cast<TextureAnimEntry *>(Data(frames)),
                                                    anims->Size / 0x34, frames->Size >> 4);
        At<TextureAnimPack *>(self, 0x14) = animPack;
        if (animPack)
            animPack->InitAnims();
    }
    else
        At<TextureAnimPack *>(self, 0x14) = nullptr;
    At<int>(self, 0x18) = 1;
    return 1;
}

int __cdecl UnloaderTexturePack(bChunk *chunkObject)
{
    Chunk *chunk = AsChunk(chunkObject);
    if (chunk->Id == kTexturePackChunk)
    {
        for (Chunk *c = reinterpret_cast<Chunk *>(Data(chunk)); c != End(chunk); c = End(c))
            if (c->Id == kTexturePackHeaderChunk)
            {
                InternalUnloadTexturePackHeaderChunks(reinterpret_cast<bChunk *>(c));
                PrevLoadedTexturePack = nullptr;
            }
        return 1;
    }
    if (chunk->Id == kTextureVRAMDataChunk)
        return 1;
    if (chunk->Id != kTextureAnimChunk)
        return 0;
    char *self = reinterpret_cast<char *>(chunk);
    if (At<int>(self, 0x10) == 1)
        if (TextureAnimPack *animPack = At<TextureAnimPack *>(self, 0x14))
        {
            animPack->~TextureAnimPack();
            bFree(TexturePackSlotPool, animPack);
        }
    return 1;
}

/** eLoadSolidListPlatChunks: the PC keeps nothing of a solid list's platform chunks. */
int __cdecl eLoadSolidListPlatChunks(bChunk *)
{
    return 1;
}

/** The platform check after a solid's chunks load: nothing can be missing on the PC. */
int __fastcall SolidPlatChunksComplete(eSolid *)
{
    return 1;
}

eSolidListHeader *__cdecl LoadSolidListHeaderChunks(bChunk *chunkObject)
{
    Chunk *chunk = AsChunk(chunkObject);
    char *header = nullptr;
    for (Chunk *c = reinterpret_cast<Chunk *>(Data(chunk)); c < End(chunk); c = End(c))
    {
        if (c->Id == 0x134002)
        {
            header = Data(c);
            AddTail(kSolidListHeaders, reinterpret_cast<Node *>(header));
            ++SolidListCount();
        }
        else if (c->Id == 0x134003)
            At<char *>(header, 0x74) = Data(c);
        else if (c->Id == 0x134004)
            At<char *>(header, 0x78) = Data(c);
        else if (c->Id == kSolidListPlatChunk)
            eLoadSolidListPlatChunks(reinterpret_cast<bChunk *>(c));
    }
    if (header)
        At<short>(header, 0x72) = 1;
    // The texture fix-up lists (see SolidTextures.cpp).
    InitList(&At<Node>(header, 0x80));
    InitList(&At<Node>(header, 0x88));
    return reinterpret_cast<eSolidListHeader *>(header);
}

/**
 * A streamed solid pack's header (its loading info: +4 the chunk, +8 set when already byte-swapped; filled in: +0x10
 * the second table, +0x14 the solid count, +0x18 the header, +0x20/+0x24 the extra data when there is some).
 */
void __cdecl LoadStreamedSolidListHeader(void *infoObject)
{
    char *info = static_cast<char *>(infoObject);
    Chunk *chunk = At<Chunk *>(info, 4);
    if (!At<int>(info, 8))
        EndianSwapChunkHeader(reinterpret_cast<bChunk *>(chunk));
    EndianSwapChunkHeadersRecursive(reinterpret_cast<bChunk *>(Data(chunk)), reinterpret_cast<int>(End(chunk)));
    char *header = reinterpret_cast<char *>(LoadSolidListHeaderChunks(reinterpret_cast<bChunk *>(chunk)));
    At<int>(info, 0x10) = At<int>(header, 0x78);
    At<int>(info, 0x14) = At<int>(header, 0xc);
    At<char *>(info, 0x18) = header;
    if (At<int>(header, 0x6c))
    {
        At<int>(info, 0x20) = At<int>(header, 0x68);
        At<int>(info, 0x24) = At<int>(header, 0x6c);
    }
}

/** A loaded solid's texture slots (+0x2c, +0x19 their count) pointed at their textures (unless chunks are moving). */
void __fastcall FindSolidTextures(eSolid *solidObject)
{
    if (ChunkMovementOffset)
        return;
    char *solid = reinterpret_cast<char *>(solidObject);
    for (int i = 0; i < static_cast<signed char>(solid[0x19]); ++i)
    {
        char *slot = At<char *>(solid, 0x2c) + i * 8;
        At<TextureInfo *>(slot, 4) = GetTextureInfo(At<unsigned int>(slot, 0), 1, 0);
    }
}

eSolid *__cdecl LoadSolidChunks(bChunk *chunkObject, eSolidListHeader *headerObject)
{
    Chunk *chunk = AsChunk(chunkObject);
    if (chunk->Id != kSolidChunk)
        return nullptr;
    char *header = reinterpret_cast<char *>(headerObject);
    char *solid = nullptr;
    bool loaded = true;
    if (Data(chunk) < reinterpret_cast<char *>(End(chunk)))
    {
        for (Chunk *c = reinterpret_cast<Chunk *>(Data(chunk)); c < End(chunk); c = End(c))
        {
            switch (c->Id)
            {
            case 0x134011:
                solid = Aligned16(c);
                InitList(ModelList(solid));
                break;
            case 0x134012:
                At<char *>(solid, 0x2c) = Data(c);
                break;
            case 0x134013:
                At<char *>(solid, 0x3c) = Data(c);
                break;
            case 0x134017:
                At<char *>(solid, 0x84) = Data(c);
                SolidVertexBytes() += c->Size;
                break;
            case 0x134018:
                At<char *>(At<char *>(solid, 0x84), 0) = Data(c);
                At<short>(At<char *>(solid, 0x84), 8) = static_cast<short>(c->Size >> 3);
                SolidVertexBytes() += c->Size;
                break;
            case 0x134019:
                At<char *>(At<char *>(solid, 0x84), 4) = Data(c);
                At<short>(At<char *>(solid, 0x84), 0xa) = static_cast<short>(c->Size / 0xc);
                SolidVertexBytes() += c->Size;
                break;
            case 0x13401b:
                At<char *>(solid, 0x90) = Data(c);
                SolidExtraBytes() += c->Size;
                break;
            case 0x13401a: {
                char *data = Aligned16(c);
                At<char *>(solid, 0x80) = data;
                solid[0x1b] = static_cast<char>(static_cast<unsigned int>(Data(c) + c->Size - data) / 0x50);
                break;
            }
            case kSolidPlatChunk:
                if (!SolidLoaderPlatChunks(reinterpret_cast<SolidPlatInfo **>(solid), nullptr,
                                           reinterpret_cast<const unsigned int *>(c)))
                    loaded = false;
                break;
            }
        }
        if (solid)
            solid[0xd] = 1;
    }
    At<short>(header, 0x7c) = -1;
    FindSolidTextures(reinterpret_cast<eSolid *>(solid));

    char *slot = At<char *>(solid, 0x3c);
    for (int i = static_cast<signed char>(solid[0x1a]); i > 0; --i, slot += 8)
        At<void *>(slot, 4) = FindLightMaterial(At<unsigned int>(slot, 0));

    // A second solid of the same name: both are marked (and, unless duplicates are allowed, as replaced).
    if (!ChunkMovementOffset && !(solid[0xe] & 0x10))
        if (char *other = reinterpret_cast<char *>(eFindSolid(At<unsigned int>(solid, 0x10), nullptr)))
        {
            At<unsigned short>(solid, 0xe) |= 0x100;
            At<unsigned short>(other, 0xe) |= 0x100;
            if (!AllowDuplicateSolids)
            {
                At<unsigned short>(solid, 0xe) |= 0x200;
                At<unsigned short>(other, 0xe) |= 0x200;
            }
        }
    if (!SolidPlatChunksComplete(reinterpret_cast<eSolid *>(solid)))
        loaded = false;

    const unsigned int name = At<unsigned int>(solid, 0x10);
    char *entry = static_cast<char *>(ScanHashTableKey32(name, At<void *>(header, 0x74), At<int>(header, 0xc), 0, 8));
    Node *link = &At<Node>(solid, 4);
    if (loaded)
    {
        if (entry)
            At<char *>(entry, 4) = solid;
        AddTail(kSolids, link);
        ++SolidLoadedTotal();
        ++SolidLoadedByName(name);
    }
    else
    {
        if (entry)
            At<char *>(entry, 4) = nullptr;
        AddTail(kFailedSolids, link);
    }
    ++SolidCount();
    eDirtySolids = 1;
    SolidBytes() += chunk->Size;
    return reinterpret_cast<eSolid *>(solid);
}

void __cdecl NotifySolidLoader(eSolidListHeader *header)
{
    nfs::streamprofile::Scope profile(nfs::streamprofile::kSolidNotify);
    if (ChunkMovementOffset)
    {
        // Back onto their (moved) solids.
        while (kMovedModels->next != kMovedModels)
        {
            ModelNode *model = reinterpret_cast<ModelNode *>(kMovedModels->next);
            Unlink(&model->link);
            AddTail(ModelList(model->solid), &model->link);
        }
        return;
    }
    const unsigned int start = bGetTicker();
    for (Node *n = kUnattachedModels->next; n != kUnattachedModels;)
    {
        Node *next = n->next;
        ModelNode *model = reinterpret_cast<ModelNode *>(n);
        if (model->nameHash)
        {
            char *solid = reinterpret_cast<char *>(eFindSolid(model->nameHash, header));
            if (model->nameHash && solid != model->solid)
                Reattach(model, solid);
        }
        n = next;
    }
    const float time = bGetTickerDifference(start, bGetTicker());
    if (!(MaxNotifyTimeLoad > time))
        MaxNotifyTimeLoad = time;
    ++NumNotifyLoads;
    TotalNotifyTimeLoad += time;
    TotalNotifyTime = TotalNotifyTimeUnload + TotalNotifyTimeLoad;
}

void __cdecl NotifySolidUnloader(eSolid *solidObject)
{
    char *solid = reinterpret_cast<char *>(solidObject);
    Node *models = ModelList(solid);
    if (ChunkMovementOffset)
    {
        // Parked with where their solid will be once moved.
        while (models->next != models)
        {
            ModelNode *model = reinterpret_cast<ModelNode *>(models->next);
            Unlink(&model->link);
            AddTail(kMovedModels, &model->link);
            model->solid = ChunkMovementOffset + solid;
        }
        return;
    }
    const unsigned int start = bGetTicker();
    if (models->next != models)
    {
        // To the solid's duplicate when it has one, else back to waiting.
        char *other = nullptr;
        if (solid[0xf] & 1)
            other = reinterpret_cast<char *>(eFindSolid(At<unsigned int>(solid, 0x10), nullptr));
        while (models->next != models)
        {
            ModelNode *model = reinterpret_cast<ModelNode *>(models->next);
            if (!model->nameHash || other == model->solid)
                break;
            Reattach(model, other);
        }
    }
    const float time = bGetTickerDifference(start, bGetTicker());
    if (!(MaxNotifyTimeUnload() > time))
        MaxNotifyTimeUnload() = time;
    ++NumNotifyUnloads();
    TotalNotifyTimeUnload += time;
    TotalNotifyTime = TotalNotifyTimeLoad + TotalNotifyTimeUnload;
}

/** A solid list header made by a loader of ours (StreamPackLoader.cpp): linked in as LoadSolidListHeaderChunks links one. */
void AttachSolidListHeader(char *header)
{
    AddTail(kSolidListHeaders, reinterpret_cast<Node *>(header));
    ++SolidListCount();
    At<short>(header, 0x72) = 1;
    InitList(&At<Node>(header, 0x80));
    InitList(&At<Node>(header, 0x88));
}

/** ... and taken out as InternalUnloaderSolidHeaderChunks takes one out. */
void DetachSolidListHeader(char *header)
{
    Unlink(reinterpret_cast<Node *>(header));
    EmptySolidTextureFixupInfo(reinterpret_cast<eSolidListHeader *>(header));
    --SolidListCount();
}

eSolidListHeader *__cdecl InternalUnloaderSolidHeaderChunks(bChunk *chunkObject)
{
    Chunk *chunk = AsChunk(chunkObject);
    if (chunk->Id != kSolidListHeaderChunk)
        return nullptr;
    char *header = nullptr;
    for (Chunk *c = reinterpret_cast<Chunk *>(Data(chunk)); c < End(chunk); c = End(c))
    {
        if (c->Id == 0x134002)
        {
            header = Data(c);
            Unlink(reinterpret_cast<Node *>(header));
            EmptySolidTextureFixupInfo(reinterpret_cast<eSolidListHeader *>(header));
            --SolidListCount();
        }
        else if (c->Id == kSolidListPlatChunk)
            eUnloadSolidListPlatChunks(reinterpret_cast<bChunk *>(c));
    }
    return reinterpret_cast<eSolidListHeader *>(header);
}

void __cdecl InternalUnloaderSolidChunks(bChunk *chunkObject, eSolidListHeader *headerObject)
{
    Chunk *chunk = AsChunk(chunkObject);
    if (chunk->Id != kSolidChunk)
        return;
    char *header = reinterpret_cast<char *>(headerObject);
    char *solid = nullptr;
    for (Chunk *c = reinterpret_cast<Chunk *>(Data(chunk)); c < End(chunk); c = End(c))
    {
        switch (c->Id)
        {
        case 0x134011:
            solid = Aligned16(c);
            break;
        case 0x134017:
        case 0x134018:
        case 0x134019:
            SolidVertexBytes() -= c->Size;
            break;
        case 0x13401b:
            SolidExtraBytes() -= c->Size;
            break;
        case kSolidPlatChunk:
            reinterpret_cast<eSolidPlatInterface *>(solid)->UnloaderPlatChunks(reinterpret_cast<bChunk *>(c));
            break;
        }
    }
    const unsigned int name = At<unsigned int>(solid, 0x10);
    if (header)
    {
        char *entry =
            static_cast<char *>(ScanHashTableKey32(name, At<void *>(header, 0x74), At<int>(header, 0xc), 0, 8));
        if (entry)
        {
            At<char *>(entry, 4) = nullptr;
            --SolidLoadedTotal();
            --SolidLoadedByName(name);
        }
        At<short>(header, 0x7c) = -1;
    }
    NotifySolidUnloader(reinterpret_cast<eSolid *>(solid));
    Unlink(&At<Node>(solid, 4));
    eDirtySolids = 1;
    --SolidCount();
    SolidBytes() -= chunk->Size;
}

int __cdecl LoaderSolidList(bChunk *chunkObject)
{
    nfs::streamprofile::Scope profile(nfs::streamprofile::kSolidLists);
    Chunk *chunk = AsChunk(chunkObject);
    if (chunk->Id != kSolidListChunk)
        return 0;
    eSolidListHeader *header = nullptr;
    for (Chunk *c = reinterpret_cast<Chunk *>(Data(chunk)); c < End(chunk); c = End(c))
    {
        if (c->Id == kSolidListHeaderChunk)
        {
            if (header)
                NotifySolidLoader(header);
            header = LoadSolidListHeaderChunks(reinterpret_cast<bChunk *>(c));
        }
        else if (c->Id == kSolidChunk)
            LoadSolidChunks(reinterpret_cast<bChunk *>(c), header);
    }
    if (header)
        NotifySolidLoader(header);
    return 1;
}

int __cdecl UnloaderSolidList(bChunk *chunkObject)
{
    Chunk *chunk = AsChunk(chunkObject);
    if (chunk->Id != kSolidListChunk)
        return 0;
    eSolidListHeader *header = nullptr;
    for (Chunk *c = reinterpret_cast<Chunk *>(Data(chunk)); c < End(chunk); c = End(c))
    {
        if (c->Id == kSolidListHeaderChunk)
            header = InternalUnloaderSolidHeaderChunks(reinterpret_cast<bChunk *>(c));
        else if (c->Id == kSolidChunk)
            InternalUnloaderSolidChunks(reinterpret_cast<bChunk *>(c), header);
    }
    return 1;
}

/** A streamed solid's chunk (byte-swapping its headers unless done) into its pack's list header (pack +0x3c). */
void __cdecl LoadStreamedSolidChunks(bChunk *chunkObject, void *, void *pack)
{
    Chunk *chunk = AsChunk(chunkObject);
    if (chunk->Id != kSolidChunk)
    {
        EndianSwapChunkHeader(chunkObject);
        EndianSwapChunkHeadersRecursive(reinterpret_cast<bChunk *>(Data(chunk)), reinterpret_cast<int>(End(chunk)));
    }
    LoadSolidChunks(chunkObject, At<eSolidListHeader *>(pack, 0x3c));
    NotifySolidLoader(At<eSolidListHeader *>(pack, 0x3c));
}

void __cdecl eLightUpdateTextures()
{
    const unsigned int *names = reinterpret_cast<const unsigned int *>(0x8f2434);
    TextureInfo **textures = reinterpret_cast<TextureInfo **>(0x915114);
    for (int i = 0; i < 3; ++i)
        textures[i] = GetTextureInfo(names[i], 0, 0);
}

/**
 * eLightMaterialPlatInterface::CreatePlatInfo (thiscall): a zeroed 0x80-byte platform info from its pool (0x982a8c),
 * or none when the pool is full.
 */
void __fastcall CreateLightMaterialPlatInfo(void *material)
{
    unsigned int *info = static_cast<unsigned int *>(bOMalloc(*reinterpret_cast<SlotPool **>(0x982a8c)));
    if (info)
        for (int i = 0; i < 0x20; ++i)
            info[i] = 0;
    *static_cast<unsigned int **>(material) = info;
}

/**
 * Light chunks: light materials (0x135200), light flare packs (0x80135100: pack 0x135101, its flares 0x135102, 0x30 each)
 * and light packs (0x80135000: pack 0x135001, lights 0x135002, 0x135003) of a section, which the visible section
 * manager's user info for the section points at (+8 flares, +0xc lights).
 */
/**
 * GLOBALB's light materials from our table (GLOBAL\LIGHTMATERIALS.MAT, tools/assetc/catalog.py light_material_table):
 * each eLightMaterial (0xA8: its words 1, 3, 3, its name's bStringHash, version 3, the name, 30 values) made in one
 * block the game keeps, then linked in as LoaderLights links a 0x135200 chunk's. Returns how many.
 */
/**
 * GLOBALB's texture animations from our table (GLOBAL\TEXTUREANIMS.REC, tools/assetc/catalog.py effect_files): the
 * TextureAnimPack of the texture pack loaded last (GlobalTextures), made as LoaderTexturePack makes it for a 0xB0300100
 * chunk, its animations (0x34 each) and frames (0x10 each) in memory the game keeps. Returns the animations made.
 */
int LoadTextureAnims(const unsigned char *data, unsigned int size)
{
    if (size < 16 || data[0] != 'T' || data[1] != 'A' || data[2] != 'N' || data[3] != 'M')
        return 0;
    const unsigned int animBytes = *reinterpret_cast<const unsigned int *>(data + 8);
    if (16 + animBytes > size)
        return 0;
    const unsigned int frameBytes = *reinterpret_cast<const unsigned int *>(data + 12 + animBytes);
    if (16 + animBytes + frameBytes > size || !PrevLoadedTexturePack)
        return 0;
    char *block = static_cast<char *>(bMalloc(static_cast<int>(animBytes + frameBytes + 16), 0));
    memcpy(block, data + 12, animBytes);
    char *frames = block + ((animBytes + 15) & ~15u);
    memcpy(frames, data + 16 + animBytes, frameBytes);
    void *memory = bMalloc(TexturePackSlotPool);
    if (!memory)
        return 0;
    TextureAnimPack *pack = new (memory) TextureAnimPack(PrevLoadedTexturePack, reinterpret_cast<TextureAnim *>(block),
                                                         reinterpret_cast<TextureAnimEntry *>(frames),
                                                         static_cast<int>(animBytes / 0x34), static_cast<int>(frameBytes >> 4));
    pack->InitAnims();
    return static_cast<int>(animBytes / 0x34);
}

int LoadLightMaterials(const unsigned char *data, unsigned int size)
{
    const unsigned int kRecord = 28 + 30 * 4;
    if (size < 12 || data[0] != 'L' || data[1] != 'M' || data[2] != 'A' || data[3] != 'T')
        return 0;
    const unsigned int count = *reinterpret_cast<const unsigned int *>(data + 8);
    if (*reinterpret_cast<const unsigned int *>(data + 4) != 1 || 12 + count * kRecord > size)
        return 0;
    char *block = static_cast<char *>(bMalloc(static_cast<int>(count * 0xA8), 0));
    for (unsigned int i = 0; i < count; ++i)
    {
        const unsigned char *r = data + 12 + i * kRecord;
        char *material = block + i * 0xA8;
        memset(material, 0, 0xA8);
        At<unsigned int>(material, 0) = 1;
        At<unsigned int>(material, 4) = 3;
        At<unsigned int>(material, 8) = 3;
        char name[29];
        memcpy(name, r, 28);
        name[28] = 0;
        At<unsigned int>(material, 0xc) = bStringHash(name);
        At<int>(material, 0x10) = 3;
        memcpy(material + 0x14, r, 28);
        memcpy(material + 0x30, r + 28, 30 * 4);
        CreateLightMaterialPlatInfo(material);
        reinterpret_cast<eLightMaterialPlatInterface *>(material)->UpdatePlatInfo();
        AddTail(reinterpret_cast<Node *>(kLightMaterials + 4), &At<Node>(material, 4));
        if (At<unsigned int>(material, 0xc) == kDefaultLightMaterialName)
            DefaultLightMaterialPointer() = material;
    }
    return static_cast<int>(count);
}

int __cdecl LoaderLights(bChunk *chunkObject)
{
    Chunk *chunk = AsChunk(chunkObject);
    if (chunk->Id == 0x135200)
    {
        char *material = Data(chunk);
        if (At<int>(material, 0x10) == 3)
        {
            CreateLightMaterialPlatInfo(material);
            reinterpret_cast<eLightMaterialPlatInterface *>(material)->UpdatePlatInfo();
            AddTail(reinterpret_cast<Node *>(kLightMaterials + 4), &At<Node>(material, 4));
            if (At<unsigned int>(material, 0xc) == kDefaultLightMaterialName)
                DefaultLightMaterialPointer() = material;
        }
        return 1;
    }
    if (chunk->Id == 0x80135100)
    {
        char *pack = nullptr;
        for (Chunk *c = reinterpret_cast<Chunk *>(Data(chunk)); c != End(chunk); c = End(c))
        {
            if (c->Id == 0x135101)
            {
                pack = Aligned16(c);
                if (!pack[0x52])
                    pack[0x52] = 1;
                if (At<int>(pack, 8) == 3)
                    InitList(&At<Node>(pack, 0x58));
                else
                    pack = nullptr;
            }
            else if (c->Id == 0x135102 && pack)
            {
                char *flare = Aligned16(c);
                for (int i = At<unsigned short>(pack, 0x50); i > 0; --i, flare += 0x30)
                    AddTail(&At<Node>(pack, 0x58), reinterpret_cast<Node *>(flare));
            }
        }
        if (pack)
        {
            AddTail(kLightFlarePacks, reinterpret_cast<Node *>(pack));
            char *info = reinterpret_cast<char *>(TheVisibleSectionManager.AllocateUserInfo(At<int>(pack, 0x54)));
            At<char *>(info, 8) = pack;
        }
        return 1;
    }
    if (chunk->Id != 0x80135000)
        return 0;
    char *pack = nullptr;
    for (Chunk *c = reinterpret_cast<Chunk *>(Data(chunk)); c != End(chunk); c = End(c))
    {
        if (c->Id == 0x135001)
        {
            pack = Aligned16(c);
            if (!pack[0xa])
                pack[0xa] = 1;
            if (At<short>(pack, 8) != 4)
                return 1;
        }
        else if (c->Id == 0x135002)
        {
            // The light table: its header, then the lights.
            char *lights = Aligned16(c);
            At<char *>(pack, 0x10) = lights;
            At<char *>(lights, 0) = lights + 0x10;
        }
        else if (c->Id == 0x135003)
            At<char *>(pack, 0x18) = Aligned16(c);
    }
    if (pack)
    {
        AddTail(kLightPacks, reinterpret_cast<Node *>(pack));
        char *info = reinterpret_cast<char *>(TheVisibleSectionManager.AllocateUserInfo(At<int>(pack, 0xc)));
        At<char *>(info, 0xc) = pack;
    }
    return 1;
}

int __cdecl UnloaderLights(bChunk *chunkObject)
{
    Chunk *chunk = AsChunk(chunkObject);
    if (chunk->Id == 0x135200)
    {
        char *material = Data(chunk);
        if (At<int>(material, 0x10) == 3)
        {
            Unlink(&At<Node>(material, 4));
            if (At<unsigned int>(material, 0xc) == kDefaultLightMaterialName)
                DefaultLightMaterialPointer() = kBuiltInLightMaterial;
        }
        return 1;
    }
    char **userInfos = reinterpret_cast<char **>(reinterpret_cast<char *>(&TheVisibleSectionManager) + 0x60);
    int section;
    if (chunk->Id == 0x80135100)
    {
        Chunk *c = reinterpret_cast<Chunk *>(Data(chunk));
        while (c != End(chunk) && c->Id != 0x135101)
            c = End(c);
        if (c == End(chunk))
            return 1;
        char *pack = Aligned16(c);
        if (At<int>(pack, 8) != 3)
            return 1;
        Unlink(reinterpret_cast<Node *>(pack));
        section = At<int>(pack, 0x54);
        At<char *>(userInfos[section], 8) = nullptr;
    }
    else if (chunk->Id == 0x80135000)
    {
        // Only the first sub-chunk is looked at (the original loops forever when it is not the pack).
        Chunk *c = reinterpret_cast<Chunk *>(Data(chunk));
        if (c == End(chunk) || c->Id != 0x135001)
            return 1;
        char *pack = Aligned16(c);
        if (At<short>(pack, 8) != 4)
            return 1;
        Unlink(reinterpret_cast<Node *>(pack));
        section = At<int>(pack, 0xc);
        At<char *>(userInfos[section], 0xc) = nullptr;
    }
    else
        return 0;
    TheVisibleSectionManager.UnallocateUserInfo(section);
    return 1;
}
