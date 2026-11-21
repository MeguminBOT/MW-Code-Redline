/**
 * Finding textures by name hash (original: zEcstasy): the loaded packs' tables, sorted by hash and searched by halves, a small cache in front,
 * and the render targets and particle textures behind them. A pack found goes to the front of the list of packs.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
/** TexturePackList: a list head of its own (next, previous). */
struct PackNode
{
    PackNode *next;
    PackNode *previous;
};
inline PackNode &Packs()
{
    return *reinterpret_cast<PackNode *>(0x9173f8);
}

const unsigned int kLoadedTableMask = 0x1fff;
const unsigned int kCacheMask = 0xff;

/** TexturePack: the pack's data, with its table of (hash, texture) at +0x74, and how many entries that has. */
inline const char *PackData(const void *pack)
{
    return *reinterpret_cast<const char *const *>(static_cast<const char *>(pack) + 0x14);
}

inline int PackTextures(const void *pack)
{
    return *reinterpret_cast<const int *>(static_cast<const char *>(pack) + 0x18);
}

struct TableEntry
{
    unsigned int hash;
    TextureInfo *texture;
};

inline TableEntry *FindInPack(const void *pack, unsigned int hash)
{
    const char *data = PackData(pack);
    if (!data)
        return nullptr;
    void *table = *reinterpret_cast<void *const *>(data + 0x74);
    if (!table)
        return nullptr;
    return static_cast<TableEntry *>(ScanHashTableKey32(hash, table, PackTextures(pack), 0, sizeof(TableEntry)));
}

inline const unsigned char *LoadedFlags()
{
    return reinterpret_cast<const unsigned char *>(&TextureLoadedTable);
}
} // namespace

void *__cdecl ScanHashTableKey32(unsigned int key, void *table, int count, int keyOffset, int stride)
{
    if (!table || count <= 0 || static_cast<unsigned int>(keyOffset) + 4u > static_cast<unsigned int>(stride))
        return nullptr;
    char *base = static_cast<char *>(table);
    int low = 0, high = count - 1;
    while (low <= high)
    {
        const int middle = (high + low) / 2;
        char *entry = base + middle * stride;
        const unsigned int at = *reinterpret_cast<const unsigned int *>(entry + keyOffset);
        if (key < at)
            high = middle - 1;
        else if (key > at)
            low = middle + 1;
        else
        {
            // The first of those with this key.
            for (int i = middle; i > 0 && *reinterpret_cast<const unsigned int *>(entry - stride + keyOffset) == key; --i)
                entry -= stride;
            return entry;
        }
    }
    return nullptr;
}

void eModel::ReplaceLightMaterial(unsigned int name, eLightMaterial *material)
{
    struct Entry
    {
        unsigned int name;
        eLightMaterial *material;
    };
    const char *solid = *reinterpret_cast<const char *const *>(reinterpret_cast<const char *>(this) + 0xc);
    if (!solid || !material)
        return;
    Entry *entries = *reinterpret_cast<Entry *const *>(solid + 0x3c);
    const int count = *reinterpret_cast<const signed char *>(solid + 0x1a);
    for (int i = 0; i < count; ++i)
        if (entries[i].name == name)
            entries[i].material = material;
}

TextureInfo *TexturePack::GetLoadedTexture(unsigned int hash)
{
    if (!LoadedFlags()[hash & kLoadedTableMask])
        return nullptr;
    const TableEntry *entry = FindInPack(this, hash);
    TextureInfo *texture = entry ? entry->texture : nullptr;
    if (!texture)
        return nullptr;
    const char *t = reinterpret_cast<const char *>(texture);
    if (!*reinterpret_cast<void *const *>(t + 0x70))
        return nullptr;
    if (*reinterpret_cast<void *const *>(t + 0x3c) && !*reinterpret_cast<void *const *>(t + 0x74))
        return nullptr;
    return texture;
}

TextureInfo *__cdecl GetTextureInfo(unsigned int hash, int defaultIfMissing, int includeUnloaded)
{
    if (hash)
    {
        TextureInfo **cache = reinterpret_cast<TextureInfo **>(&TextureInfoCache);
        TextureInfo *cached = cache[hash & kCacheMask];
        if (cached && cached->NameHash == hash)
            return cached;
        if (LoadedFlags()[hash & kLoadedTableMask])
        {
            PackNode &packs = Packs();
            for (PackNode *node = packs.next; node != &packs; node = node->next)
            {
                TextureInfo *found;
                if (!includeUnloaded)
                {
                    found = reinterpret_cast<TexturePack *>(node)->GetLoadedTexture(hash);
                }
                else
                {
                    const TableEntry *entry = FindInPack(node, hash);
                    found = entry ? entry->texture : nullptr;
                    if (!entry)
                        continue;
                }
                if (!found)
                    continue;
                // The pack goes first in the list; the texture into the cache.
                node->previous->next = node->next;
                node->next->previous = node->previous;
                node->next = packs.next;
                node->previous = &packs;
                packs.next->previous = node;
                packs.next = node;
                const unsigned int slot = found->NameHash & kCacheMask;
                reinterpret_cast<char *>(found)[0x53] = 1;
                cache[slot] = found;
                reinterpret_cast<unsigned int *>(&TextureInfoCacheSafety)[slot] = found->NameHash;
                return found;
            }
        }
        if (TextureInfo *target = eGetRenderTargetTextureInfo(static_cast<int>(hash)))
            return target;
        const unsigned int *names = reinterpret_cast<const unsigned int *>(&OtherEcstacyTextures_name_hash);
        TextureInfo *const *others = reinterpret_cast<TextureInfo *const *>(&OtherEcstacyTextures);
        for (unsigned int i = 0; i < numOtherTex; ++i)
            if (names[i] == hash)
            {
                if (others[i])
                    return others[i];
                break;
            }
    }
    return defaultIfMissing ? DefaultTextureInfo : nullptr;
}
