/**
 * The streaming pack loader (original: zEcstasy eStreamPackLoader): packs of solids or textures whose header is read
 * in three phases (a first 32 KB read, the header chunks, then the header's resource file), and whose entries are
 * loaded and unloaded by name hash with a reference count each. Loads go out through the queued file reader in one
 * of 64 load tables (QueuedLoadingTables), which call back when their last entry is in.
 *
 * The original names every allocation with bSPrintf for its memory tracker; the PC build throws the names away, so
 * those calls are left out, as is a bLargestMalloc/bGetMemoryPoolNum whose result was never used.
 *
 * Streamed texture packs the packs serve as texture sets (kind 2: the cars', the front end's images) are made from
 * their typed textures (docs/EA_FORMATS.md phase 4, step 1.2): CreateStreamingPack builds the pack header (0x7C), the
 * key table, the entry table and the TexturePack at once, with each entry's place among the pack's images (as the
 * bridge places them) in its file offset; InternalLoadStreamingEntry reads an entry's image and palette straight from
 * its typed texture and puts its TextureInfo and platform info after them, where TextureLoadedStreamingEntryCallback
 * looks. Both complete through the delayed resource callbacks, as a queued read would, so callers see them finish
 * later as before. `-set Data.NativeTextures=0` reads the bridge's files.
 */
#include "game/gen/api.h"
#include "legacy/link.h"
#include "core/config.h"
#include "core/log.h"
#include "engine/resources/pack.h"
#include "engine/resources/packbridge.h"

#include <cstdint>
#include <cstring>

void __fastcall DestroyTexturePack(TexturePack *pack);
void AttachSolidListHeader(char *header);
void DetachSolidListHeader(char *header);

NFS_REPLACE(0x500bc0, GetStreamingEntry)

int __cdecl StreamingEntryBefore(const char *a, const char *b);

namespace
{
struct Entry
{
    unsigned int hash;
    int fileOffset;
    int size;
    int uncompressedSize;
    unsigned char unknown10;
    unsigned char flags;
    short references;
    char *data;
};
static_assert(sizeof(Entry) == 0x18, "eStreamingEntry");

enum EntryFlags : unsigned char
{
    kCompressed = 0x01,
    kLoading = 0x10,
    kUnloadWhenLoaded = 0x20,
};

struct HeaderLoading
{
    int bytesRead;
    int headerInBuffer;
    void(__cdecl *callback)(void *);
    void *argument;
};

struct Pack
{
    Pack *next;
    Pack *prev;
    const char *name;
    short pool;
    char headerPending;
    char unknown0f;
    HeaderLoading *loading;
    int headerLoaded;
    int headerChunkSize;
    char *header;
    ResourceFile *resources;
    Entry *entries;
    int entryCount;
    int loadsInFlight;
    int loadedEntries;
    int loadedBytes;
    int references;
    int unknown3c;
    int unknown40;
};
static_assert(sizeof(Pack) == 0x44, "eStreamingPack");

struct Loader
{
    int alignment;
    void(__cdecl *loaded)(void *, Entry *, Pack *);
    void(__cdecl *unloaded)(void *, Entry *, Pack *);
    void(__cdecl *phase1)(void *);
    void(__cdecl *phase2)(void *);
    void(__cdecl *deleted)(Pack *);
    Pack *head;
    Pack *tail;
    int unknown20;
    int unknown24;
    int unknown28;
    int packCount;
    int loadedEntries;
    int loadedBytes;
};

struct LoadTable
{
    signed char pool;
    char inUse;
    short count;
    void(__cdecl *callback)(void *);
    void *argument;
    Loader *loader;
};
static_assert(sizeof(LoadTable) == 0x10, "eStreamingPackLoadTable");

LoadTable *const kLoadTables = reinterpret_cast<LoadTable *>(0x915b70);
const int kLoadTableCount = 64;

/** A load list node (0x14 bytes, sorted by StreamingEntryBefore: pack, then file offset). */
struct LoadNode
{
    LoadNode *next;
    LoadNode *prev;
    Pack *pack;
    Entry *entry;
    int fileOffset;
};

int(__cdecl *const kEntryBefore)(bNode *, bNode *) = reinterpret_cast<int(__cdecl *)(bNode *, bNode *)>(&StreamingEntryBefore);

// ------------------------------------------------------------------------------------------- native texture packs

const unsigned int kStreamedTextureSet = 2;
const unsigned int kTextureHeader = 128;
const unsigned int kTextureSizes = 56;
const unsigned int kRecords = 0x7c + 0x20;
const unsigned int kPackHeader = 0x7c;
const unsigned int kImageAlign = 0x80;
const int kMaxNativePacks = 128;
const int kMaxPending = 2048;
char *const kTexturePackList = reinterpret_cast<char *>(0x9173f8);

struct NativePack
{
    Pack *pack;
    nfs::pack::SetView *view;
    /** Solid packs: the set's item of each entry (the entries are in key order). */
    unsigned int *items;
    bool solids;
};

struct Pending
{
    Pack *pack;
    Entry *entry;
    LoadTable *table;
    int failed;
};

NativePack g_nativePacks[kMaxNativePacks];
Pending g_pending[kMaxPending];
int g_pendingCount;
bool g_flushQueued;

bool NativeEnabled()
{
    static const bool on = nfs::config::Int("Data", "NativeTextures", 1) != 0;
    return on;
}

NativePack *Native(const Pack *pack)
{
    for (NativePack &n : g_nativePacks)
        if (n.pack && n.pack == pack)
            return &n;
    return nullptr;
}

template <typename T> T &Field(void *p, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(p) + offset);
}

void __cdecl FlushNative(void *);

void Queue(Pack *pack, Entry *entry, LoadTable *table, int failed)
{
    if (g_pendingCount == kMaxPending)
        FlushNative(nullptr);
    g_pending[g_pendingCount++] = {pack, entry, table, failed};
    if (!g_flushQueued)
    {
        g_flushQueued = true;
        SetDelayedResourceCallback(&FlushNative, nullptr);
    }
}

/** A native pack's header finished, as InternalLoadingHeaderPhase2Callback finishes one without resources. */
void HeaderDone(Pack *pack)
{
    HeaderLoading *loading = pack->loading;
    void(__cdecl *callback)(void *) = loading ? loading->callback : nullptr;
    void *argument = loading ? loading->argument : nullptr;
    pack->headerLoaded = 1;
    if (loading)
        bFree(loading);
    pack->loading = nullptr;
    if (callback)
        callback(argument);
}

void __cdecl FlushNative(void *)
{
    g_flushQueued = false;
    while (g_pendingCount)
    {
        Pending batch[64];
        int n = g_pendingCount < 64 ? g_pendingCount : 64;
        std::memcpy(batch, g_pending, sizeof(Pending) * n);
        std::memmove(g_pending, g_pending + n, sizeof(Pending) * (g_pendingCount - n));
        g_pendingCount -= n;
        for (int i = 0; i < n; ++i)
        {
            if (!batch[i].entry)
                HeaderDone(batch[i].pack);
            else
                eStreamPackLoader::InternalLoadedStreamingEntryCallback(batch[i].entry, batch[i].failed, batch[i].table);
        }
    }
}

/** The pack header, keys, entries and TexturePack of a streamed texture set; null when it cannot be read. */
char *MakeNativeHeader(nfs::pack::SetView *view, int pool, int &bytes, Entry *&entries, int &count)
{
    const uint8_t *set = nfs::pack::SetViewHeader(view);
    const unsigned int n = nfs::pack::SetViewCount(view);
    const bool rows = Field<unsigned int>(const_cast<uint8_t *>(set), nfs::pack::kSetLayout) == nfs::pack::kSetLayoutRows;
    bytes = static_cast<int>(kPackHeader + 8 * n + sizeof(Entry) * n);
    char *header = static_cast<char *>(bMalloc(bytes, pool && bytes < bLargestMalloc(pool) ? (0x2000 | (pool & 0xf)) : 0x2000));
    if (!header)
        return nullptr;
    std::memset(header, 0, static_cast<unsigned int>(bytes));
    std::memcpy(header, set + 8, 4);
    std::memcpy(header + 4, set + 16, 28);
    std::memcpy(header + 0x20, set + 48, 64);
    std::memcpy(header + 0x60, set + 12, 4);
    char *keys = header + kPackHeader;
    entries = reinterpret_cast<Entry *>(keys + 8 * n);
    unsigned int place = 0;
    for (unsigned int i = 0; i < n; ++i)
    {
        uint8_t h[kTextureSizes];
        unsigned int key, image, palette;
        if (rows)
        {
            const uint8_t *row = set + nfs::pack::kSetHeaderBytes + 8 * n + nfs::pack::kSetRowBytes * i;
            std::memcpy(&key, row, 4);
            std::memcpy(&image, row + 4, 4);
            std::memcpy(&palette, row + 8, 4);
        }
        else
        {
            if (!nfs::pack::SetViewRead(view, i, 0, h, kTextureSizes))
            {
                bFree(header);
                return nullptr;
            }
            std::memcpy(&key, h, 4);
            std::memcpy(&image, h + 48, 4);
            std::memcpy(&palette, h + 52, 4);
        }
        place = (place + kImageAlign - 1) & ~(kImageAlign - 1);
        const unsigned int blob = image + palette + kRecords;
        Field<unsigned int>(keys, 8 * i) = key;
        Entry &e = entries[i];
        e.hash = key;
        e.fileOffset = static_cast<int>(place);
        e.size = static_cast<int>(blob);
        e.uncompressedSize = static_cast<int>(blob);
        place += blob;
    }
    char *texturePack = static_cast<char *>(bMalloc(TexturePackSlotPool));
    if (!texturePack)
    {
        bFree(header);
        return nullptr;
    }
    Field<int>(header, 0x6c) = 1;
    Field<char *>(header, 0x74) = keys;
    Field<char *>(header, 0x78) = reinterpret_cast<char *>(entries);
    Field<const char *>(texturePack, 8) = bAllocateSharedString(header + 4);
    Field<const char *>(texturePack, 0xc) = bAllocateSharedString(header + 0x20);
    Field<char *>(texturePack, 0x14) = header;
    Field<unsigned int>(texturePack, 0x10) = bStringHash(Field<const char *>(texturePack, 8));
    Field<int>(texturePack, 0x18) = static_cast<int>(n);
    Field<int>(texturePack, 0x1c) = 0;
    char *tail = Field<char *>(kTexturePackList, 4);
    Field<char *>(tail, 0) = texturePack;
    Field<char *>(kTexturePackList, 4) = texturePack;
    Field<char *>(texturePack, 4) = tail;
    Field<char *>(texturePack, 0) = kTexturePackList;
    Field<char *>(header, 0x70) = texturePack;
    count = static_cast<int>(n);
    return header;
}

const unsigned int kSolidSet = 1;
const unsigned int kSolidListInfo = 0x90;

/** A streamed solid set's list header (eSolidListHeader, 0x90: the 0x134002 chunk's), its keys and its entries in key
 * order, as the bridge's solid pack has them; the list header linked in. Null when the set cannot be read. */
char *MakeNativeSolidHeader(nfs::pack::SetView *view, int pool, int &bytes, Entry *&entries, int &count,
                            unsigned int *&items)
{
    const uint8_t *set = nfs::pack::SetViewHeader(view);
    const unsigned int n = nfs::pack::SetViewCount(view);
    const bool rows = Field<unsigned int>(const_cast<uint8_t *>(set), nfs::pack::kSetLayout) == nfs::pack::kSetLayoutRows;
    unsigned int *keys = static_cast<unsigned int *>(bMalloc(static_cast<int>(12 * (n ? n : 1)), 0));
    unsigned int *sizes = keys + n, *order = keys + 2 * n;
    for (unsigned int i = 0; i < n; ++i)
    {
        if (rows)
        {
            const uint8_t *row = set + nfs::pack::kSetHeaderBytes + 8 * n + nfs::pack::kSetRowBytes * i;
            std::memcpy(&keys[i], row, 4);
            std::memcpy(&sizes[i], row + 4, 4);
        }
        else
        {
            const unsigned int size = static_cast<unsigned int>(nfs::pack::SetViewItemSize(view, i));
            uint8_t *mesh = static_cast<uint8_t *>(bMalloc(static_cast<int>(size), 0));
            if (!nfs::pack::SetViewRead(view, i, 0, mesh, size))
            {
                bFree(mesh);
                bFree(keys);
                return nullptr;
            }
            std::memcpy(&keys[i], mesh, 4);
            sizes[i] = static_cast<unsigned int>(nfs::packbridge::SolidRecord(mesh, nullptr));
            bFree(mesh);
        }
        order[i] = i;
    }
    for (unsigned int i = 1; i < n; ++i)
    {
        const unsigned int v = order[i];
        unsigned int j = i;
        while (j > 0 && keys[order[j - 1]] > keys[v])
        {
            order[j] = order[j - 1];
            --j;
        }
        order[j] = v;
    }
    bytes = static_cast<int>(kSolidListInfo + 8 * n + sizeof(Entry) * n);
    char *header = static_cast<char *>(bMalloc(bytes, pool && bytes < bLargestMalloc(pool) ? (0x2000 | (pool & 0xf)) : 0x2000));
    items = static_cast<unsigned int *>(bMalloc(static_cast<int>(4 * (n ? n : 1)), 0));
    if (!header || !items)
    {
        if (header)
            bFree(header);
        if (items)
            bFree(items);
        bFree(keys);
        return nullptr;
    }
    std::memset(header, 0, static_cast<unsigned int>(bytes));
    Field<unsigned int>(header, 8) = 0x1D;
    Field<unsigned int>(header, 0xc) = n;
    std::memcpy(header + 0x10, set + 48, 0x38);
    std::memcpy(header + 0x48, set + 16, 0x20);
    Field<unsigned int>(header, 0x70) = 0x80;
    char *keyTable = header + kSolidListInfo;
    entries = reinterpret_cast<Entry *>(keyTable + 8 * n);
    // Each solid's place among the pack's solids (0x80 apart), for the load order only.
    unsigned int place = 0;
    for (unsigned int i = 0; i < n; ++i)
    {
        const unsigned int item = order[i];
        place = (place + kImageAlign - 1) & ~(kImageAlign - 1);
        Field<unsigned int>(keyTable, 8 * i) = keys[item];
        Entry &e = entries[i];
        e.hash = keys[item];
        e.fileOffset = static_cast<int>(place);
        e.size = static_cast<int>(sizes[item]);
        e.uncompressedSize = static_cast<int>(sizes[item]);
        items[i] = item;
        place += sizes[item];
    }
    bFree(keys);
    Field<char *>(header, 0x74) = keyTable;
    Field<char *>(header, 0x78) = reinterpret_cast<char *>(entries);
    AttachSolidListHeader(header);
    count = static_cast<int>(n);
    return header;
}

/** A solid entry: its typed mesh's solid chunk, as LoadStreamedSolidChunks reads it. */
bool ReadNativeSolid(NativePack &native, Pack *pack, Entry *entry, char *out)
{
    const unsigned int item = native.items[entry - pack->entries];
    const unsigned int size = static_cast<unsigned int>(nfs::pack::SetViewItemSize(native.view, item));
    uint8_t *mesh = static_cast<uint8_t *>(bMalloc(static_cast<int>(size), 0));
    bool ok = mesh && nfs::pack::SetViewRead(native.view, item, 0, mesh, size);
    ok = ok && nfs::packbridge::SolidRecord(mesh, nullptr) == static_cast<uint64_t>(entry->size);
    if (ok)
        nfs::packbridge::SolidRecord(mesh, reinterpret_cast<uint8_t *>(out));
    if (mesh)
        bFree(mesh);
    return ok;
}

/** An entry's image and palette from its typed texture, its records after them. */
bool ReadNativeEntry(NativePack &native, Pack *pack, Entry *entry, char *out)
{
    if (native.solids)
        return ReadNativeSolid(native, pack, entry, out);
    const unsigned int index = static_cast<unsigned int>(entry - pack->entries);
    uint8_t h[kTextureHeader];
    if (!nfs::pack::SetViewRead(native.view, index, 0, h, kTextureHeader))
        return false;
    unsigned int image, palette;
    std::memcpy(&image, h + 48, 4);
    std::memcpy(&palette, h + 52, 4);
    if (image + palette + kRecords != static_cast<unsigned int>(entry->size) ||
        !nfs::pack::SetViewRead(native.view, index, kTextureHeader, out, image + palette))
        return false;
    nfs::packbridge::TextureRecords(h, static_cast<unsigned int>(entry->fileOffset), reinterpret_cast<uint8_t *>(out) + image + palette);
    return true;
}

void DestroyNative(NativePack &native)
{
    Pack *pack = native.pack;
    if (native.solids)
    {
        if (char *list = reinterpret_cast<char *>(static_cast<std::intptr_t>(pack->unknown3c)))
            DetachSolidListHeader(list);
        bFree(native.items);
        nfs::pack::CloseSetView(native.view);
        native = NativePack();
        return;
    }
    char *header = reinterpret_cast<char *>(static_cast<std::intptr_t>(pack->unknown40));
    if (header)
        if (TexturePack *tp = Field<TexturePack *>(header, 0x70))
        {
            DestroyTexturePack(tp);
            bFree(TexturePackSlotPool, tp);
            Field<TexturePack *>(header, 0x70) = nullptr;
        }
    nfs::pack::CloseSetView(native.view);
    native = NativePack();
}

ResourceFile *CreateResourceFile(const char *name, int type, int flags, int offset, int size)
{
    return NFS_ORIGINAL(0x65fd30, ResourceFile *(__cdecl *)(const char *, int, int, int, int))(name, type, flags, offset, size);
}

Loader *Self(eStreamPackLoader *loader)
{
    return reinterpret_cast<Loader *>(loader);
}

Pack *AsPack(eStreamingPack *pack)
{
    return reinterpret_cast<Pack *>(pack);
}

Pack *Head(Loader *loader)
{
    return reinterpret_cast<Pack *>(&loader->head);
}

char *Align(const Loader *loader, char *p)
{
    const unsigned int mask = static_cast<unsigned int>(loader->alignment) - 1u;
    return reinterpret_cast<char *>((reinterpret_cast<unsigned int>(p) + mask) & ~mask);
}

void MoveToFront(Loader *loader, Pack *pack)
{
    pack->prev->next = pack->next;
    pack->next->prev = pack->prev;
    Pack *head = Head(loader), *first = loader->head;
    loader->head = pack;
    first->prev = pack;
    pack->prev = head;
    pack->next = first;
}

Entry *FindEntry(const Pack *pack, unsigned int hash)
{
    if (pack->headerPending || !pack->entries)
        return nullptr;
    return static_cast<Entry *>(ScanHashTableKey32(hash, pack->entries, pack->entryCount, 0, sizeof(Entry)));
}

/** A pack's memory flags for a block of `size` bytes: its own pool when that pool has room. */
int PoolFlags(int pool, int size, int base)
{
    if (pool && size < bLargestMalloc(pool))
        return base | (pool & 0xf);
    return base;
}

void ListAppend(LoadNode *head, LoadNode *&tail, LoadNode *node)
{
    tail->next = node;
    node->prev = tail;
    node->next = head;
    tail = node;
}

/** Fills `nodes` with the loaded packs' entries for `hashes`, linked in a list from `head`, then sorts it. */
void BuildLoadList(eStreamPackLoader *loader, const unsigned int *hashes, int count, LoadNode *nodes, LoadNode *head,
                   unsigned int *lowestOffset)
{
    head->next = head->prev = head;
    LoadNode *tail = head;
    for (int i = 0; i < count; ++i)
    {
        const unsigned int hash = hashes[i];
        Pack *pack = AsPack(loader->GetLoadedStreamingPack(hash));
        if (!pack)
            continue;
        Entry *entry = FindEntry(pack, hash);
        if (!entry)
            continue;
        LoadNode *node = nodes++;
        node->pack = pack;
        node->entry = entry;
        node->fileOffset = entry->fileOffset;
        if (lowestOffset && static_cast<unsigned int>(entry->fileOffset) < *lowestOffset)
            *lowestOffset = static_cast<unsigned int>(entry->fileOffset);
        ListAppend(head, tail, node);
    }
    if (tail != head)
        head->prev = tail;
    reinterpret_cast<bList *>(head)->Sort(kEntryBefore);
}
} // namespace

eStreamPackLoader::eStreamPackLoader(int alignment, void(__cdecl *loaded)(bChunk *, eStreamingEntry *, eStreamingPack *),
                                     void(__cdecl *unloaded)(bChunk *, eStreamingEntry *, eStreamingPack *),
                                     void(__cdecl *phase1)(eStreamingPackHeaderLoadingInfoPhase1 *),
                                     void(__cdecl *phase2)(eStreamingPackHeaderLoadingInfoPhase2 *),
                                     void(__cdecl *deleted)(eStreamingPack *))
{
    Loader *self = Self(this);
    self->head = self->tail = Head(self);
    self->unknown20 = 0;
    self->packCount = 0;
    self->loadedEntries = 0;
    self->loadedBytes = 0;
    self->alignment = alignment;
    self->loaded = reinterpret_cast<void(__cdecl *)(void *, Entry *, Pack *)>(loaded);
    self->unloaded = reinterpret_cast<void(__cdecl *)(void *, Entry *, Pack *)>(unloaded);
    self->phase1 = reinterpret_cast<void(__cdecl *)(void *)>(phase1);
    self->phase2 = reinterpret_cast<void(__cdecl *)(void *)>(phase2);
    self->deleted = reinterpret_cast<void(__cdecl *)(Pack *)>(deleted);
}

eStreamingPack *eStreamPackLoader::GetLoadedStreamingPack(const char *name)
{
    Loader *self = Self(this);
    for (Pack *p = self->head; p != Head(self); p = p->next)
    {
        if (!bStrCmp(name, p->name))
        {
            MoveToFront(self, p);
            return reinterpret_cast<eStreamingPack *>(p);
        }
    }
    return nullptr;
}

eStreamingPack *eStreamPackLoader::GetLoadedStreamingPack(unsigned int hash)
{
    Loader *self = Self(this);
    for (Pack *p = self->head; p != Head(self); p = p->next)
    {
        if (FindEntry(p, hash))
        {
            MoveToFront(self, p);
            return reinterpret_cast<eStreamingPack *>(p);
        }
    }
    return nullptr;
}

/** eStreamPackLoader::GetStreamingEntry(unsigned int): the entry for a hash in any loaded pack. */
eStreamingEntry *__fastcall GetStreamingEntry(eStreamPackLoader *loader, void *, unsigned int hash)
{
    Loader *self = Self(loader);
    for (Pack *p = self->head; p != Head(self); p = p->next)
        if (Entry *e = FindEntry(p, hash))
            return reinterpret_cast<eStreamingEntry *>(e);
    return nullptr;
}

eStreamingPack *eStreamPackLoader::CreateStreamingPack(const char *name, void(__cdecl *callback)(void *), void *argument, int pool)
{
    Loader *self = Self(this);
    if (Pack *existing = AsPack(GetLoadedStreamingPack(name)))
    {
        ++existing->references;
        return reinterpret_cast<eStreamingPack *>(existing);
    }
    const bool solidLoader = this == &StreamingSolidPackLoader;
    if (NativeEnabled() && (this == &StreamingTexturePackLoader || solidLoader))
        if (nfs::pack::SetView *view = nfs::pack::OpenSetView(name, solidLoader ? kSolidSet : kStreamedTextureSet))
        {
            NativePack *slot = nullptr;
            for (NativePack &n : g_nativePacks)
                if (!n.pack)
                {
                    slot = &n;
                    break;
                }
            int bytes = 0, count = 0;
            Entry *entries = nullptr;
            unsigned int *items = nullptr;
            char *header = !slot          ? nullptr
                           : solidLoader ? MakeNativeSolidHeader(view, pool, bytes, entries, count, items)
                                         : MakeNativeHeader(view, pool, bytes, entries, count);
            if (header)
            {
                HeaderLoading *loading = static_cast<HeaderLoading *>(bMalloc(sizeof(HeaderLoading), 0x40));
                std::memset(loading, 0, sizeof(HeaderLoading));
                loading->callback = callback;
                loading->argument = argument;
                Pack *pack = static_cast<Pack *>(bOMalloc(eStreamingPackSlotPool));
                std::memset(pack, 0, sizeof(Pack));
                pack->name = bAllocateSharedString(name);
                pack->pool = static_cast<short>(pool);
                pack->references = 1;
                pack->loading = loading;
                pack->header = header;
                pack->headerChunkSize = bytes;
                pack->entries = entries;
                pack->entryCount = count;
                if (solidLoader)
                    pack->unknown3c = static_cast<int>(reinterpret_cast<std::intptr_t>(header));
                else
                    pack->unknown40 = static_cast<int>(reinterpret_cast<std::intptr_t>(header));
                Pack *tail = self->tail;
                tail->next = pack;
                self->tail = pack;
                pack->prev = tail;
                pack->next = Head(self);
                ++self->packCount;
                *slot = {pack, view, items, solidLoader};
                Queue(pack, nullptr, nullptr, 0);
                static const bool log = nfs::config::Int("Debug", "PackLog", 0) != 0;
                if (log)
                    nfs::Log("%s: streamed %s from its %d typed items", solidLoader ? "solids" : "textures", name, count);
                return reinterpret_cast<eStreamingPack *>(pack);
            }
            nfs::pack::CloseSetView(view);
            nfs::Log("textures: streamed %s does not load from its textures; read as a file", name);
        }
    bFile *file = bOpen(name, 1, 1);
    if (!file)
        return nullptr;
    int size = bFileSize(file);
    bClose(file);
    if (size < 0x41)
        return nullptr;
    HeaderLoading *loading = static_cast<HeaderLoading *>(bMalloc(sizeof(HeaderLoading), 0x40));
    loading->bytesRead = 0;
    loading->headerInBuffer = 0;
    loading->callback = callback;
    loading->argument = argument;
    Pack *pack = static_cast<Pack *>(bOMalloc(eStreamingPackSlotPool));
    std::memset(pack, 0, sizeof(Pack));
    pack->name = bAllocateSharedString(name);
    pack->pool = static_cast<short>(pool);
    pack->headerChunkSize = 0;
    pack->references = 1;
    pack->loading = loading;
    Pack *tail = self->tail;
    tail->next = pack;
    self->tail = pack;
    pack->prev = tail;
    pack->next = Head(self);
    ++self->packCount;
    if (size >= 0x8000)
        size = 0x8000;
    pack->header = static_cast<char *>(bMalloc(size, 0x2040));
    loading->bytesRead = size;
    AddQueuedFile2(pack->header, pack->name, 0, size, &eStreamPackLoader::InternalLoadingHeaderPhase1Callback, self, pack, nullptr);
    return reinterpret_cast<eStreamingPack *>(pack);
}

int eStreamPackLoader::DeleteStreamingPack(const char *name)
{
    Loader *self = Self(this);
    Pack *pack = AsPack(GetLoadedStreamingPack(name));
    if (!pack)
        return 0;
    if (--pack->references == 0)
    {
        NativePack *native = Native(pack);
        if (native)
            FlushNative(nullptr);
        pack->headerPending = 0;
        UnloadAllStreamingEntries(name);
        if (native)
            DestroyNative(*native);
        else
            self->deleted(pack);
        pack->prev->next = pack->next;
        pack->next->prev = pack->prev;
        if (pack->header)
            bFree(pack->header);
        if (pack->resources)
            UnloadResourceFile(pack->resources);
        bFreeSharedString(pack->name);
        bFree(eStreamingPackSlotPool, pack);
        --self->packCount;
    }
    return 1;
}

void __cdecl eStreamPackLoader::InternalLoadingHeaderPhase1Callback(void *loaderObject, int, void *packObject)
{
    Loader *loader = static_cast<Loader *>(loaderObject);
    Pack *pack = static_cast<Pack *>(packObject);
    struct
    {
        const char *name;
        char *header;
        int offset;
        int size;
    } info = {pack->name, pack->header, 0, 0};
    HeaderLoading *loading = pack->loading;
    loader->phase1(&info);
    const int offset = info.offset, size = info.size;
    pack->headerChunkSize = size;
    int flags = 0x2000;
    if (pack->pool && size < bLargestMalloc(pack->pool))
        flags = (static_cast<unsigned char>(pack->pool) & 0xf) | 0x2040;
    char *chunks = static_cast<char *>(bMalloc(size, flags));
    char *buffer = pack->header;
    if (offset + size <= loading->bytesRead)
    {
        std::memcpy(chunks, buffer + offset, static_cast<unsigned int>(size));
        loading->headerInBuffer = 1;
    }
    bFree(buffer);
    pack->header = chunks;
    if (!loading->headerInBuffer)
        AddQueuedFile2(chunks, pack->name, offset, size, &eStreamPackLoader::InternalLoadingHeaderPhase2Callback, loader, pack, nullptr);
    if (loading->headerInBuffer)
        InternalLoadingHeaderPhase2Callback(loader, 0, pack);
}

void __cdecl eStreamPackLoader::InternalLoadingHeaderPhase2Callback(void *loaderObject, int, void *packObject)
{
    Loader *loader = static_cast<Loader *>(loaderObject);
    Pack *pack = static_cast<Pack *>(packObject);
    struct
    {
        const char *name;
        char *header;
        int headerInBuffer;
        Pack *pack;
        Entry *entries;
        int entryCount;
        int unknown3c;
        int unknown40;
        int resourceOffset;
        int resourceSize;
    } info = {pack->name, pack->header, pack->loading->headerInBuffer, pack, nullptr, 0, 0, 0, 0, 0};
    loader->phase2(&info);
    pack->unknown3c = info.unknown3c;
    pack->entries = info.entries;
    pack->entryCount = info.entryCount;
    pack->unknown40 = info.unknown40;
    if (info.resourceSize)
    {
        pack->resources = CreateResourceFile(pack->name, 6, 0, info.resourceOffset, info.resourceSize);
        int flags = 0x2000;
        if (pack->pool && info.resourceSize < bLargestMalloc(pack->pool))
            flags = (static_cast<unsigned char>(pack->pool) & 0xf) | 0x2040;
        char name[1024];
        bSPrintf(name, "%s: StrmHdrRes", pack->name);
        pack->resources->SetAllocationParams(flags, name);
        pack->resources->BeginLoading(&eStreamPackLoader::InternalLoadingHeaderPhase3Callback, pack);
    }
    if (!pack->resources)
    {
        HeaderLoading *loading = pack->loading;
        void(__cdecl *callback)(void *) = loading->callback;
        void *argument = loading->argument;
        pack->headerLoaded = 1;
        bFree(loading);
        pack->loading = nullptr;
        if (callback)
            callback(argument);
    }
}

void eStreamPackLoader::InternalLoadStreamingEntry(eStreamingPackLoadTable *tableObject, eStreamingPack *packObject, eStreamingEntry *entryObject)
{
    Loader *self = Self(this);
    LoadTable *table = reinterpret_cast<LoadTable *>(tableObject);
    Pack *pack = AsPack(packObject);
    Entry *entry = reinterpret_cast<Entry *>(entryObject);
    if (!table || !pack || !entry)
        return;
    if (entry->references)
    {
        ++entry->references;
        return;
    }
    if (entry->flags & kLoading)
    {
        // Unloaded while its read was still out: it is wanted again, so it stays.
        entry->flags &= static_cast<unsigned char>(~kUnloadWhenLoaded);
        entry->references = 1;
    }
    else
    {
        const int pool = table->pool;
        const int size = entry->size + self->alignment;
        int flags = 0x2000;
        if (!(entry->flags & kCompressed) || (flags = 0x2040, pool == AllowCompressedStreamingTexturesInThisPoolNum))
            if (pool)
                flags = PoolFlags(pool, size, flags);
        entry->data = static_cast<char *>(bMalloc(size, flags));
        ++entry->references;
        ++pack->loadsInFlight;
        ++table->count;
        entry->flags |= kLoading;
        if (NativePack *native = Native(pack))
            Queue(pack, entry, table, ReadNativeEntry(*native, pack, entry, Align(self, entry->data)) ? 0 : 1);
        else
            AddQueuedFile2(Align(self, entry->data), pack->name, entry->fileOffset, entry->size,
                           &eStreamPackLoader::InternalLoadedStreamingEntryCallback, entry, table, nullptr);
    }
    ++pack->loadedEntries;
    pack->loadedBytes += entry->size;
}

void __cdecl eStreamPackLoader::InternalLoadedStreamingEntryCallback(void *entryObject, int failed, void *tableObject)
{
    Entry *entry = static_cast<Entry *>(entryObject);
    LoadTable *table = static_cast<LoadTable *>(tableObject);
    Loader *loader = table->loader;
    Pack *pack = AsPack(reinterpret_cast<eStreamPackLoader *>(loader)->GetLoadedStreamingPack(entry->hash));
    entry->flags &= static_cast<unsigned char>(~kLoading);
    if (!failed)
    {
        ++loader->loadedEntries;
        loader->loadedBytes += entry->size;
        if (entry->flags & kUnloadWhenLoaded)
        {
            reinterpret_cast<eStreamPackLoader *>(loader)->InternalUnloadStreamingEntry(
                reinterpret_cast<eStreamingPack *>(pack), reinterpret_cast<eStreamingEntry *>(entry));
            entry->flags &= static_cast<unsigned char>(~kUnloadWhenLoaded);
        }
        else
        {
            char *chunk = Align(loader, entry->data);
            if (entry->flags & kCompressed)
            {
                const int pool = table->pool;
                const int size = entry->uncompressedSize + loader->alignment;
                int flags = 0x2000;
                if (pool && size < bLargestMalloc(pool))
                    flags = (pool & 0xf) | 0x2000;
                char *compressed = entry->data;
                entry->data = static_cast<char *>(bMalloc(size, flags));
                char *packed = chunk;
                chunk = Align(loader, entry->data);
                LZDecompress(reinterpret_cast<unsigned char *>(packed), reinterpret_cast<unsigned char *>(chunk));
                bFree(compressed);
            }
            loader->loaded(chunk, entry, pack);
        }
    }
    if (pack)
        --pack->loadsInFlight;
    if (--table->count == 0)
    {
        if (table->callback)
            table->callback(table->argument);
        table->loader = nullptr;
        table->inUse = 0;
        table->count = 0;
        table->callback = nullptr;
        table->argument = nullptr;
    }
}

void eStreamPackLoader::UnloadStreamingEntry(unsigned int hash, eStreamingPack *packObject)
{
    if (!hash)
        return;
    Pack *pack = packObject ? AsPack(packObject) : AsPack(GetLoadedStreamingPack(hash));
    if (!pack)
        return;
    Entry *entry = FindEntry(pack, hash);
    if (!entry || --entry->references != 0)
        return;
    eWaitUntilRenderingDone();
    if (entry->flags & kLoading)
    {
        entry->flags |= kUnloadWhenLoaded;
        return;
    }
    InternalUnloadStreamingEntry(reinterpret_cast<eStreamingPack *>(pack), reinterpret_cast<eStreamingEntry *>(entry));
}

/** eStreamPackLoader::UnloadStreamingEntry(unsigned int *, int): each hash of a list, found in its loaded pack. */
void __fastcall UnloadStreamingEntries(eStreamPackLoader *loader, void *, const unsigned int *hashes, int count)
{
    for (int i = 0; i < count; ++i)
    {
        const unsigned int hash = hashes[i];
        if (!hash)
            continue;
        Pack *pack = AsPack(loader->GetLoadedStreamingPack(hash));
        if (!pack)
            continue;
        Entry *entry = FindEntry(pack, hash);
        if (!entry || --entry->references != 0)
            continue;
        eWaitUntilRenderingDone();
        if (entry->flags & kLoading)
            entry->flags |= kUnloadWhenLoaded;
        else
            loader->InternalUnloadStreamingEntry(reinterpret_cast<eStreamingPack *>(pack), reinterpret_cast<eStreamingEntry *>(entry));
    }
}
NFS_REPLACE(0x500fc0, UnloadStreamingEntries)

void eStreamPackLoader::UnloadAllStreamingEntries(const char *name)
{
    Pack *pack = AsPack(GetLoadedStreamingPack(name));
    if (!pack)
        return;
    Entry *entry = pack->entries;
    for (int i = pack->entryCount; i > 0; --i, ++entry)
    {
        if (entry->data)
        {
            entry->references = 1;
            UnloadStreamingEntry(entry->hash, reinterpret_cast<eStreamingPack *>(pack));
        }
    }
}

int eStreamPackLoader::IsLoading(const char *name)
{
    Loader *self = Self(this);
    for (Pack *p = self->head; p != Head(self); p = p->next)
        if ((!name || !bStrCmp(name, p->name)) && (!p->headerLoaded || p->loadsInFlight > 0))
            return 1;
    return 0;
}

void eStreamPackLoader::LoadStreamingEntry(unsigned int *hashes, int count, void(__cdecl *callback)(void *), void *argument, int pool)
{
    int index = 0;
    while (index < kLoadTableCount && kLoadTables[index].inUse)
        ++index;
    if (index == kLoadTableCount)
        return;
    LoadTable *table = &kLoadTables[index];
    table->loader = Self(this);
    table->inUse = 1;
    table->pool = static_cast<signed char>(pool);
    table->callback = callback;
    table->argument = argument;
    LoadNode *nodes = static_cast<LoadNode *>(bMalloc(count * static_cast<int>(sizeof(LoadNode)), 0x40));
    LoadNode head;
    unsigned int lowest = 0x7fffffff;
    BuildLoadList(this, hashes, count, nodes, &head, &lowest);
    for (LoadNode *n = head.next; n != &head; n = n->next)
        InternalLoadStreamingEntry(reinterpret_cast<eStreamingPackLoadTable *>(table), reinterpret_cast<eStreamingPack *>(n->pack),
                                   reinterpret_cast<eStreamingEntry *>(n->entry));
    bFree(nodes);
    // With nothing to read the callback goes out now; with no callback the table stays taken, as in the original.
    if (!table->count && callback)
    {
        table->inUse = 0;
        SetDelayedResourceCallback(callback, argument);
    }
}

int eStreamPackLoader::TestLoadStreamingEntry(unsigned int *hashes, int count, int pool, bool breakIfShort)
{
    Loader *self = Self(this);
    LoadNode *nodes = static_cast<LoadNode *>(bMalloc(count * static_cast<int>(sizeof(LoadNode)), 0x40));
    LoadNode head;
    BuildLoadList(this, hashes, count, nodes, &head, nullptr);
    int shortBy = 0, shortInMain = 0;
    const int poolFlags = pool ? (pool & 0xf) | 0x2000 : 0x2000;
    // Three passes: allocate what would be read, then what compressed entries unpack to, then free it all again.
    for (int pass = 0; pass < 3; ++pass)
    {
        unsigned int previous = 0;
        for (LoadNode *n = head.next; n != &head; n = n->next)
        {
            Entry *e = n->entry;
            if (e->references || (e->flags & kLoading) || e->hash == previous)
                continue;
            previous = e->hash;
            if (pass == 0)
            {
                const int size = e->size + self->alignment;
                const int flags = (e->flags & kCompressed) ? 0x2040 : poolFlags;
                if (size < bLargestMalloc(flags))
                    e->data = static_cast<char *>(bMalloc(size, flags));
                else
                {
                    if (!(flags & 0xf))
                        shortInMain += size;
                    shortBy += size;
                }
            }
            else if (pass == 1)
            {
                if (!(e->flags & kCompressed))
                    continue;
                const int size = e->uncompressedSize + self->alignment;
                char *compressed = e->data;
                e->data = nullptr;
                if (size < bLargestMalloc(poolFlags))
                    e->data = static_cast<char *>(bMalloc(size, poolFlags));
                else
                {
                    shortBy += size;
                    if (!(poolFlags & 0xf))
                        shortInMain += size;
                }
                if (compressed)
                    bFree(compressed);
            }
            else if (e->data)
            {
                bFree(e->data);
                e->data = nullptr;
            }
        }
    }
    bFree(nodes);
    if (breakIfShort && shortInMain)
        bBreak();
    return shortBy;
}
