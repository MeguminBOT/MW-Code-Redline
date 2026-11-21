/**
 * Textures joined to their packs (original: zEcstasy TexturePack, TextureAnimPack): a texture's entry in its pack's index
 * (the pack's +0x14 header, its table at +0x74 of (name hash, texture) pairs and its count at the pack's +0x18), the
 * loaded-texture counts (TextureLoadedTable: a byte per name hash's low 13 bits, and their total at 0x91a0d8), and the
 * animated textures' frame tables.
 *
 * TextureInfo (0x7c): +0x24 its name hash, +0x2c its palette's (or 0), +0x3c the palette's size, +0x68 its pack, +0x6c its
 * palette texture (itself without one), +0x70 its image data, +0x74 its palette data.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
template <typename T> T &At(void *p, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(p) + offset);
}

const unsigned int kDefaultTextureName = 0x66994f4a;
const int kTextureInfoSize = 0x7c;
const int kPlatInfoSize = 0x20;

int &LoadedTotal()
{
    return *reinterpret_cast<int *>(0x91a0d8);
}

signed char &LoadedByName(unsigned int hash)
{
    return reinterpret_cast<signed char *>(&TextureLoadedTable)[hash & 0x1fff];
}

/** The pack's index entry (8 bytes: name hash, texture) for a name, or nullptr. */
char *IndexEntry(TexturePack *pack, unsigned int hash)
{
    char *index = At<char *>(pack, 0x14);
    void *table = index ? At<void *>(index, 0x74) : nullptr;
    if (!table)
        return nullptr;
    return static_cast<char *>(ScanHashTableKey32(hash, table, At<int>(pack, 0x18), 0, 8));
}

struct Node
{
    Node *next;
    Node *prev;
};

Node *const kAnimPacks = reinterpret_cast<Node *>(0x91c160);
} // namespace

void TexturePack::AttachTextureInfo(TextureInfo *textureObject, TextureInfoPlatInfo *plat, TextureIndexEntry *entryObject)
{
    char *texture = reinterpret_cast<char *>(textureObject);
    char *entry = reinterpret_cast<char *>(entryObject);
    if (!entry)
        entry = IndexEntry(this, At<unsigned int>(texture, 0x24));
    if (entry)
    {
        At<char *>(entry, 4) = texture;
        ++LoadedTotal();
        ++LoadedByName(At<unsigned int>(texture, 0x24));
    }
    if (!At<unsigned int>(texture, 0x2c))
        At<char *>(texture, 0x6c) = texture;
    else
    {
        char *palette = IndexEntry(this, At<unsigned int>(texture, 0x2c));
        At<char *>(texture, 0x6c) = palette ? At<char *>(palette, 4) : nullptr;
    }
    At<TexturePack *>(texture, 0x68) = this;
    At<void *>(texture, 0x70) = nullptr;
    At<void *>(texture, 0x74) = nullptr;
    reinterpret_cast<TextureInfoPlatInterface *>(texture)->SetPlatInfo(plat);
    eDirtyTextures = 1;
    if (At<unsigned int>(texture, 0x24) == kDefaultTextureName)
        DefaultTextureInfo = textureObject;
}

void TexturePack::AttachTextureTable(TextureInfo *textures, TextureInfoPlatInfo *plats, int count)
{
    char *table = At<char *>(At<char *>(this, 0x14), 0x74);
    char *texture = reinterpret_cast<char *>(textures), *plat = reinterpret_cast<char *>(plats);
    for (int i = 0; i < count; ++i, texture += kTextureInfoSize, plat += kPlatInfoSize)
        AttachTextureInfo(reinterpret_cast<TextureInfo *>(texture), reinterpret_cast<TextureInfoPlatInfo *>(plat),
                          reinterpret_cast<TextureIndexEntry *>(table + i * 8));
}

void TexturePack::UnattachTextureTable(TextureInfo *textures, TextureInfoPlatInfo *, int count)
{
    char *texture = reinterpret_cast<char *>(textures);
    for (int i = 0; i < count; ++i, texture += kTextureInfoSize)
    {
        // The index entry at the texture's place in the table (the table lists them in order); by name only when the
        // table is missing, as the original has it.
        char *table = At<char *>(At<char *>(this, 0x14), 0x74);
        char *entry = table + i * 8;
        const unsigned int hash = At<unsigned int>(texture, 0x24);
        if (!entry)
            entry = IndexEntry(this, hash);
        if (entry)
        {
            At<void *>(entry, 4) = nullptr;
            --LoadedTotal();
            --LoadedByName(hash);
        }
        if (reinterpret_cast<TextureInfo *>(texture) == DefaultTextureInfo)
            DefaultTextureInfo = nullptr;
    }
}

TextureAnimPack::TextureAnimPack(TexturePack *pack, TextureAnim *anims, TextureAnimEntry *entries, int count, int unknown)
{
    char *self = reinterpret_cast<char *>(this);
    At<TexturePack *>(self, 8) = pack;
    At<TextureAnim *>(self, 0xc) = anims;
    At<TextureAnimEntry *>(self, 0x10) = entries;
    At<int>(self, 0x14) = count;
    At<int>(self, 0x18) = unknown;
    // Each animation's frames (0x10 each: name hash, texture, its animation data) follow the last's.
    char *frame = reinterpret_cast<char *>(entries);
    char *anim = reinterpret_cast<char *>(anims);
    for (int i = 0; i < count; ++i, anim += 0x34)
    {
        At<char *>(anim, 0x28) = frame;
        frame += At<int>(anim, 0x1c) * 0x10;
    }
    Node *n = reinterpret_cast<Node *>(self);
    Node *tail = kAnimPacks->prev;
    tail->next = n;
    kAnimPacks->prev = n;
    n->prev = tail;
    n->next = kAnimPacks;
}

TextureAnimPack::~TextureAnimPack()
{
    char *self = reinterpret_cast<char *>(this);
    char *anim = At<char *>(self, 0xc);
    for (int i = 0; i < At<int>(self, 0x14); ++i, anim += 0x34)
    {
        char *frame = At<char *>(anim, 0x28);
        for (int k = At<int>(anim, 0x1c); k > 0; --k, frame += 0x10)
            if (TextureInfoPlatInterface *texture = At<TextureInfoPlatInterface *>(frame, 4))
                texture->ReleaseAnimData(At<void *>(frame, 8));
    }
    Node *n = reinterpret_cast<Node *>(self);
    n->prev->next = n->next;
    n->next->prev = n->prev;
}

void TextureAnimPack::InitAnims()
{
    char *self = reinterpret_cast<char *>(this);
    TexturePack *pack = At<TexturePack *>(self, 8);
    char *anim = At<char *>(self, 0xc);
    for (int i = At<int>(self, 0x14); i > 0; --i, anim += 0x34)
    {
        int complete = 1;
        char *frame = At<char *>(anim, 0x28);
        for (int k = At<int>(anim, 0x1c); k > 0; --k, frame += 0x10)
        {
            const unsigned int hash = At<unsigned int>(frame, 0);
            char *texture = nullptr;
            if (LoadedByName(hash))
                if (char *entry = IndexEntry(pack, hash))
                    texture = At<char *>(entry, 4);
            // A frame needs its image, and its palette when it has one.
            if (!texture || !At<void *>(texture, 0x70) || (At<int>(texture, 0x3c) && !At<void *>(texture, 0x74)))
            {
                At<void *>(frame, 4) = nullptr;
                At<void *>(frame, 8) = nullptr;
                complete = 0;
            }
            else
            {
                At<char *>(frame, 4) = texture;
                At<void *>(frame, 8) = reinterpret_cast<TextureInfoPlatInterface *>(texture)->CreateAnimData();
            }
        }
        At<int>(anim, 0x2c) = complete;
        At<int>(anim, 0x30) = 0;
    }
}
