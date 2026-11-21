/**
 * Texture packs loaded from their typed textures (docs/EA_FORMATS.md phase 4, step 1): a resource file whose game path
 * the packs serve as a plain texture set (kind 3: NIGHTSKY.BIN, LOGOS.BIN, the HUD packs ...) is not read as a file of
 * EA's layout. ResourceFile::BeginLoading (0x6616f0) builds the engine's records from the set at once: the pack header
 * (0x7C: version, name, file name, hash; +0x6c loaded, +0x70 its TexturePack, +0x74 the key table), the key table,
 * every TextureInfo (0x7C) and platform info (0x20) from the texture's typed header, and the images at 0x80 places, in
 * one block; then the TexturePack as InternalLoadTexturePackHeaderChunks and LoaderTexturePack make it. The file is left
 * without chunks, so the resource loader finishes it as an empty file. ~ResourceFile (0x658ee0) takes the pack out again
 * as InternalUnloadTexturePackHeaderChunks does. `-set Data.NativeTextures=0` reads the bridge's files as before.
 *
 * ResourceFile: +0x10 flags, +0x1c file name, +0x28 allocation parameters, +0x3c the chunks, +0x40 their size.
 */
#include <string.h>

#include "game/gen/api.h"
#include "legacy/link.h"
#include "core/config.h"
#include "core/log.h"
#include "engine/resources/pack.h"
#include "engine/resources/packbridge.h"
#include "engine/resources/strings.h"

NFS_REPLACE(0x6616f0, ResourceFile::BeginLoading)
NFS_REPLACE(0x658ee0, ResourceFile::~ResourceFile)

void __fastcall DestroyTexturePack(TexturePack *pack);
void __fastcall ReleaseMiniMapSections(ChoppedMiniMapManager *self, void *);

namespace
{
const unsigned int kPlainTextureSet = 3;
const unsigned int kTextureHeader = 128;
const unsigned int kInfo = 0x7c;
const unsigned int kPlat = 0x20;
const unsigned int kPackHeader = 0x7c;
const unsigned int kAlign = 0x80;
const int kMaxNative = 64;
char *const kTexturePacks = reinterpret_cast<char *>(0x9173f8);

template <typename T> T &At(void *p, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(p) + offset);
}

struct Native
{
    ResourceFile *file;
    char *block;
    char *header;
    TexturePack *pack;
    int count;
    int imageBytes;
};

Native g_native[kMaxNative];

bool Enabled()
{
    static const bool on = nfs::config::Int("Data", "NativeTextures", 1) != 0;
    return on;
}

unsigned int Up(unsigned int n)
{
    return (n + kAlign - 1) & ~(kAlign - 1);
}

void AddTail(char *list, char *node)
{
    char *tail = At<char *>(list, 4);
    At<char *>(tail, 0) = node;
    At<char *>(list, 4) = node;
    At<char *>(node, 4) = tail;
    At<char *>(node, 0) = list;
}

/** Textures first .. first + n of the set as one block (allocated with `params`) and a TexturePack; false (nothing
 * made) when they cannot be read. */
bool Load(ResourceFile *file, int params, nfs::pack::SetView *view, unsigned int first, unsigned int n, Native &out)
{
    const uint8_t *set = nfs::pack::SetViewHeader(view);
    uint8_t *headers = static_cast<uint8_t *>(bMalloc(static_cast<int>(kTextureHeader * (n ? n : 1)), 0));
    if (!headers)
        return false;
    unsigned int images = 0;
    bool ok = true;
    for (unsigned int i = 0; ok && i < n; ++i)
    {
        uint8_t *h = headers + kTextureHeader * i;
        ok = nfs::pack::SetViewRead(view, first + i, 0, h, kTextureHeader);
        images = Up(images) + At<unsigned int>(h, 48) + At<unsigned int>(h, 52);
    }
    images = Up(images);
    const unsigned int tables = Up(kPackHeader + 8 * n + (kInfo + kPlat) * n);
    char *block = ok ? static_cast<char *>(bMalloc(static_cast<int>(tables + images), params)) : nullptr;
    if (!block)
    {
        bFree(headers);
        return false;
    }
    memset(block, 0, tables);
    char *header = block;
    char *keys = header + kPackHeader;
    char *infos = keys + 8 * n;
    char *plats = infos + kInfo * n;
    char *data = block + tables;
    memcpy(header, set + 8, 4);
    memcpy(header + 4, set + 16, 28);
    memcpy(header + 0x20, set + 48, 64);
    memcpy(header + 0x60, set + 12, 4);
    uint8_t records[kInfo + kPlat];
    unsigned int at = 0;
    for (unsigned int i = 0; ok && i < n; ++i)
    {
        const uint8_t *h = headers + kTextureHeader * i;
        const unsigned int bytes = At<unsigned int>(const_cast<uint8_t *>(h), 48) +
                                   At<unsigned int>(const_cast<uint8_t *>(h), 52);
        at = Up(at);
        memcpy(keys + 8 * i, h, 4);
        nfs::packbridge::TextureRecords(h, at, records);
        memcpy(infos + kInfo * i, records, kInfo);
        memcpy(plats + kPlat * i, records + kInfo, kPlat);
        ok = nfs::pack::SetViewRead(view, first + i, kTextureHeader, data + at, bytes);
        at += bytes;
    }
    bFree(headers);
    if (ok && at < images)
        memset(data + at, 0, images - at);
    char *pack = ok ? static_cast<char *>(bMalloc(TexturePackSlotPool)) : nullptr;
    if (!pack)
    {
        bFree(block);
        return false;
    }
    At<char *>(header, 0x74) = keys;
    At<int>(header, 0x6c) = 1;
    At<const char *>(pack, 8) = bAllocateSharedString(header + 4);
    At<const char *>(pack, 0xc) = bAllocateSharedString(header + 0x20);
    At<char *>(pack, 0x14) = header;
    At<unsigned int>(pack, 0x10) = bStringHash(At<const char *>(pack, 8));
    At<int>(pack, 0x18) = static_cast<int>(n);
    At<int>(pack, 0x1c) = 0;
    AddTail(kTexturePacks, pack);
    At<char *>(header, 0x70) = pack;
    TexturePack *tp = reinterpret_cast<TexturePack *>(pack);
    if (n)
        tp->AttachTextureTable(reinterpret_cast<TextureInfo *>(infos), reinterpret_cast<TextureInfoPlatInfo *>(plats),
                               static_cast<int>(n));
    tp->AssignTextureData(data, 0, static_cast<int>(images));
    At<int>(pack, 0x1c) = static_cast<int>(images);
    // As LoaderTexturePack: a texture animation chunk loaded next belongs to this pack.
    PrevLoadedTexturePack = tp;
    out = {file, block, header, tp, static_cast<int>(n), static_cast<int>(images)};
    return true;
}

void Unload(Native &m)
{
    char *infos = m.header + kPackHeader + 8 * m.count;
    char *plats = infos + kInfo * m.count;
    m.pack->UnAssignTextureData(0, m.imageBytes);
    if (m.count)
        m.pack->UnattachTextureTable(reinterpret_cast<TextureInfo *>(infos),
                                     reinterpret_cast<TextureInfoPlatInfo *>(plats), m.count);
    if (PrevLoadedTexturePack == m.pack)
        PrevLoadedTexturePack = nullptr;
    DestroyTexturePack(m.pack);
    bFree(TexturePackSlotPool, m.pack);
    At<TexturePack *>(m.header, 0x70) = nullptr;
    bFree(m.block);
    m = Native();
}
// ------------------------------------------------------------------------------------------------- minimap chops

/**
 * The minimap's files (set kind 4: TRACKS\L2RA\MINI_MAP*.BIN), one texture per chop. ChoppedMiniMapManager (0x1b4
 * bytes, gChoppedMiniMapManager): +0 the chops loaded, +4 the sections it keeps unpacked (9), +0x48 a pointer per chop
 * (the chunk's LZ data in the original; a ChopRef here), +0x148 the sections {chop, chunks, size}. UncompressMaps
 * (0x5672f0) and the release of every section (0x567420) are ported: a native chop's section holds its Native record
 * in place of the chunks it unpacked, and its texture is made from the typed texture instead of unpacking the chop.
 */
const unsigned int kChopSet = 4;
const int kMaxChopFiles = 4;
const int kSections = 0x148;
const int kChops = 0x48;

struct ChopRef
{
    nfs::pack::SetView *view;
    unsigned int item;
};

struct ChopFile
{
    ResourceFile *file;
    nfs::pack::SetView *view;
    ChopRef *refs;
    int count;
};

ChopFile g_chopFiles[kMaxChopFiles];
Native g_chopTextures[16];

char *Manager()
{
    return reinterpret_cast<char *>(gChoppedMiniMapManager);
}

const ChopRef *AsChopRef(const void *p)
{
    for (const ChopFile &f : g_chopFiles)
        if (f.refs && p >= f.refs && p < f.refs + f.count)
            return static_cast<const ChopRef *>(p);
    return nullptr;
}

Native *AsChopTexture(const void *p)
{
    for (Native &m : g_chopTextures)
        if (p == &m)
            return &m;
    return nullptr;
}

/** A section's chunks taken out: a native chop's texture, or the original's unpacked chunks. */
void ReleaseSection(char *section)
{
    void *chunks = At<void *>(section, 4);
    if (Native *m = AsChopTexture(chunks))
        Unload(*m);
    else
    {
        UnloadChunks(static_cast<bChunk *>(chunks), At<int>(section, 8), "MiniMap Chop");
        bFree(chunks);
    }
    At<void *>(section, 4) = nullptr;
    At<int>(section, 8) = 0;
}

bool BeginChops(ResourceFile *file)
{
    char *mgr = Manager();
    nfs::pack::SetView *view = mgr ? nfs::pack::OpenSetView(At<const char *>(file, 0x1c), kChopSet) : nullptr;
    if (!view)
        return false;
    ChopFile *slot = nullptr;
    for (ChopFile &f : g_chopFiles)
        if (!f.file)
        {
            slot = &f;
            break;
        }
    const int n = static_cast<int>(nfs::pack::SetViewCount(view));
    ChopRef *refs = slot && At<int>(mgr, 0) + n <= 64 ? static_cast<ChopRef *>(bMalloc(static_cast<int>(sizeof(ChopRef)) * (n ? n : 1), 0)) : nullptr;
    if (!refs)
    {
        nfs::pack::CloseSetView(view);
        return false;
    }
    for (int i = 0; i < n; ++i)
    {
        refs[i] = {view, static_cast<unsigned int>(i)};
        At<ChopRef *>(mgr, kChops + 4 * At<int>(mgr, 0)) = &refs[i];
        ++At<int>(mgr, 0);
    }
    *slot = {file, view, refs, n};
    return true;
}

void EndChops(ChopFile &f)
{
    char *mgr = Manager();
    for (int i = 0; i < f.count && mgr && At<int>(mgr, 0) > 0; ++i)
    {
        --At<int>(mgr, 0);
        At<void *>(mgr, kChops + 4 * At<int>(mgr, 0)) = nullptr;
        if (!At<int>(mgr, 0))
            reinterpret_cast<ChoppedMiniMapManager *>(mgr)->UncompressMaps(nullptr, 0);
    }
    bFree(f.refs);
    nfs::pack::CloseSetView(f.view);
    f = ChopFile();
}
} // namespace

NFS_REPLACE(0x5672f0, ChoppedMiniMapManager::UncompressMaps)
NFS_REPLACE(0x567420, ReleaseMiniMapSections)

void __fastcall ReleaseMiniMapSections(ChoppedMiniMapManager *self, void *)
{
    char *mgr = reinterpret_cast<char *>(self);
    for (int i = 0; i < At<int>(mgr, 4); ++i)
    {
        char *section = mgr + kSections + 12 * i;
        if (At<void *>(section, 4))
            ReleaseSection(section);
    }
}

void ChoppedMiniMapManager::UncompressMaps(short *chops, int count)
{
    char *mgr = reinterpret_cast<char *>(this);
    const int sections = At<int>(mgr, 4);
    for (int s = 0; s < sections; ++s)
    {
        char *section = mgr + kSections + 12 * s;
        if (!At<void *>(section, 4))
            continue;
        bool wanted = false;
        for (int i = 0; i < count && !wanted; ++i)
            wanted = chops[i] == At<int>(section, 0);
        if (!wanted)
        {
            ReleaseSection(section);
            At<int>(section, 0) = 0;
        }
    }
    for (int i = 0; i < count; ++i)
    {
        const int chop = chops[i];
        char *free = nullptr;
        int s = 0;
        for (; s < sections; ++s)
        {
            char *section = mgr + kSections + 12 * s;
            if (!At<void *>(section, 4))
                free = section;
            else if (At<int>(section, 0) == chop)
                break;
        }
        if (s != sections || chop < 0 || !free)
            continue;
        void *packed = At<void *>(mgr, kChops + 4 * chop);
        if (!packed)
            continue;
        if (const ChopRef *ref = AsChopRef(packed))
        {
            Native *slot = nullptr;
            for (Native &m : g_chopTextures)
                if (!m.block)
                {
                    slot = &m;
                    break;
                }
            if (slot && Load(nullptr, 0x2000, ref->view, ref->item, 1, *slot))
            {
                At<int>(free, 0) = chop;
                At<Native *>(free, 4) = slot;
                At<int>(free, 8) = 0;
            }
            continue;
        }
        const int size = At<int>(packed, 8);
        At<int>(free, 0) = chop;
        At<int>(free, 8) = size;
        char *chunks = static_cast<char *>(bMalloc(size, 0x2000));
        At<char *>(free, 4) = chunks;
        LZDecompress(static_cast<unsigned char *>(packed), reinterpret_cast<unsigned char *>(chunks));
        LoadChunks(reinterpret_cast<bChunk *>(chunks), size, "MiniMap Chop embedded");
    }
}

void ResourceFile::BeginLoading(void (*callback)(void *), void *param)
{
    nfs::strings::Begin(this, At<const char *>(this, 0x1c));
    if (Enabled() && !At<char *>(this, 0x3c) && BeginChops(this))
    {
        At<int>(this, 0x40) = 0;
        static const bool log = nfs::config::Int("Debug", "PackLog", 0) != 0;
        if (log)
            nfs::Log("textures: minimap %s from its typed chops", At<const char *>(this, 0x1c));
    }
    else if (Enabled() && !At<char *>(this, 0x3c))
        if (nfs::pack::SetView *view = nfs::pack::OpenSetView(At<const char *>(this, 0x1c), kPlainTextureSet))
        {
            for (Native &m : g_native)
                if (!m.file)
                {
                    // -set Debug.NativeTexturesCycle=1: each pack is made, taken out and made again (tests the unload).
                    static const bool cycle = nfs::config::Int("Debug", "NativeTexturesCycle", 0) != 0;
                    // The file's own allocation (0x80-aligned by default, high memory when its flags say so).
                    const int params = At<int>(this, 0x28) | ((At<int>(this, 0x10) & 2) ? 0x40 : 0);
                    const unsigned int n = nfs::pack::SetViewCount(view);
                    if (cycle && Load(this, params, view, 0, n, m))
                        Unload(m);
                    if (Load(this, params, view, 0, n, m))
                    {
                        At<int>(this, 0x40) = 0;
                        static const bool log = nfs::config::Int("Debug", "PackLog", 0) != 0;
                        if (log)
                            nfs::Log("textures: %s from its %d textures", At<const char *>(this, 0x1c), m.count);
                    }
                    else
                        nfs::Log("textures: %s does not load from its textures; read as a file",
                                 At<const char *>(this, 0x1c));
                    break;
                }
            nfs::pack::CloseSetView(view);
        }
    NFS_ORIGINAL(0x6616f0, void(__thiscall *)(ResourceFile *, void (*)(void *), void *))(this, callback, param);
}

ResourceFile::~ResourceFile()
{
    nfs::strings::End(this);
    for (Native &m : g_native)
        if (m.file == this)
            Unload(m);
    for (ChopFile &f : g_chopFiles)
        if (f.file == this)
            EndChops(f);
    NFS_ORIGINAL(0x658ee0, void(__thiscall *)(ResourceFile *))(this);
}
