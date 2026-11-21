#pragma once

/**
 * The pack file format (docs/PACKS.md); tools/assetc/pack.py writes it. Little-endian, every place an offset from the
 * file's start, no pointers.
 */
#include <stdint.h>

namespace nfs::pack
{
const uint32_t kMagic = 0x4B50464E; // "NFPK"
const uint32_t kVersion = 1;
const uint32_t kBlockSize = 256 * 1024;

/** A resource's type. */
enum Type : uint32_t
{
    /** A file of EA's layout that EA's code opens by name (its name is the resource's second name). */
    kLegacyFile = 1,
    /** Typed resources (docs/PACKS.md "Typed resources"; tools/assetc/typed.py). */
    kTexture = 2,
    kMesh = 3,
    /** The resources one EA load asks for; its second name is that EA file's (source/engine/resources/PackBridge.cpp
     * makes the file from them). */
    kSet = 4,
    /** A world section's records of one kind, a section, the world's stream file (tools/assetc/world.py). */
    kTables = 5,
    kSection = 6,
    kStream = 7,
    /** A row without bytes in a mod's pack made from a pack of data\ (mods\compiled\, tools/assetc/mods.py): its
     * legacyKey and group hold the low and high words of BaseFingerprint of that pack, the one it stands in for. */
    kBase = 8,
};

/** FNV-1a 64 of a pack's resource table as the file has it: what a mod's kBase row was made for. */
inline uint64_t BaseFingerprint(const void *table, uint32_t bytes)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint32_t i = 0; i < bytes; ++i)
        h = (h ^ static_cast<const uint8_t *>(table)[i]) * 0x100000001b3ull;
    return h;
}
/** A set's header: count, kind, then the identity of what it stands for; the ids (u64) follow at kSetHeaderBytes.
 * When the header's word at kSetLayout is kSetLayoutRows, a row of four words per item follows the ids (its key and
 * sizes: a texture's image and palette bytes, a mesh's solid chunk bytes). */
const uint32_t kSetHeaderBytes = 144;
const uint32_t kSetLayout = 136;
const uint32_t kSetLayoutRows = 1;
const uint32_t kSetRowBytes = 16;

struct Header
{
    uint32_t magic;
    uint32_t version;
    uint32_t kind;
    uint32_t flags;
    uint32_t resourceCount;
    uint32_t blockCount;
    uint32_t blockSize;
    uint32_t namesSize;
    uint64_t resourceTable;
    uint64_t blockTable;
    uint64_t names;
    uint64_t streamSize;
};
static_assert(sizeof(Header) == 64, "pack header");

struct Resource
{
    uint64_t id;
    uint32_t type;
    uint32_t legacyKey;
    uint64_t offset;
    uint64_t size;
    uint32_t name;
    uint32_t checksum;
    uint32_t group;
    uint32_t reserved;
};
static_assert(sizeof(Resource) == 48, "pack resource");

/** A block of the unpacked stream: where it is, its size there, its size unpacked (packed == unpacked: stored). */
struct Block
{
    uint64_t offset;
    uint32_t packed;
    uint32_t unpacked;
};
static_assert(sizeof(Block) == 16, "pack block");

/** FNV-1a 64 of an asset path (lower case, '/' between folders), a resource's id. */
/** A file of EA's layout a pack serves (a legacy file or a set made by the bridge), read whole with a 0 after it (the
 * caller's to free with operator delete), or null when no pack holds it (source/engine/resources/PackFiles.cpp). */
uint8_t *ReadWhole(const char *path, uint32_t &size);
/** A file the game reads whole: from the packs, or else from the disk (logged: everything should come from the packs,
 * docs/EA_FORMATS.md), with a 0 after it (the caller's to free with operator delete); null when neither has it. */
uint8_t *ReadFile(const char *path, uint32_t &size);
/** Whether the packs or the disk have the file. */
bool Exists(const char *path);

/** A set a game path names, opened for a loader of ours that builds the engine's records from its typed items
 * (docs/EA_FORMATS.md phase 4): null unless the packs serve the path as a set of that kind. */
struct SetView;
SetView *OpenSetView(const char *path, uint32_t kind);
/** The set resource's bytes (kSetHeaderBytes of header, then the ids). */
const uint8_t *SetViewHeader(const SetView *view);
uint32_t SetViewCount(const SetView *view);
uint64_t SetViewItemSize(const SetView *view, uint32_t item);
bool SetViewRead(SetView *view, uint32_t item, uint64_t offset, void *out, uint32_t bytes);
void CloseSetView(SetView *view);

inline uint64_t Id(const char *path)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (; *path; ++path)
    {
        char c = *path == '\\' ? '/' : *path;
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
        h = (h ^ static_cast<uint8_t>(c)) * 0x100000001b3ull;
    }
    return h;
}
} // namespace nfs::pack
