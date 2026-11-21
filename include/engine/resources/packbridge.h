#pragma once

/**
 * EA's file layouts made at run time from a pack's typed resources (docs/PACKS.md "The bridge"): the texture bundle or
 * solid pack a set stands for, read by offset as EA's loaders stream it. tools/assetc/bridge.py is the same generator;
 * the tools build this file as a DLL (PACK_BRIDGE_TOOLS) and compare the two byte for byte.
 */
#include <stddef.h>
#include <stdint.h>

namespace nfs::packbridge
{
/** How the bridge reads the pack: a resource's size, and bytes of it from an offset. */
struct Source
{
    void *context;
    uint64_t (*size)(void *context, uint32_t resource);
    bool (*read)(void *context, uint32_t resource, uint64_t offset, void *out, uint32_t bytes);
};

struct File;

/** The file of a set (`set` the set resource's bytes, `items` the resources it names, in its order); null when the set
 * cannot be read. A set with a layout row per item (docs/PACKS.md) is laid out without reading its items. */
File *Open(const Source &source, const uint8_t *set, uint64_t setBytes, const uint32_t *items, uint32_t count);
uint64_t Size(const File *file);
/** Copies the file's bytes from `offset`; how many (fewer at its end). */
uint32_t Read(File *file, uint64_t offset, void *out, uint32_t bytes);
void Close(File *file);

/** A texture's TextureInfo (0x7C) and platform info (0x20) into out, from its typed header (128 bytes), its image at
 * `placement` among the pack's images. */
void TextureRecords(const uint8_t *header, uint32_t placement, uint8_t *out);
/** A typed mesh's solid chunk (0x80134010 and all it holds) as the engine loads it, written to out at a multiple of
 * 0x80 (null: only measured); its size. */
uint64_t SolidRecord(const uint8_t *mesh, uint8_t *out);

struct Section;

/** A world section of the stream file (docs/WORLD_DATA.md): resource 0 the section resource, 1.. the resources its ids
 * name, in order; `position` its place in the stream (the chunks' alignment counts from the file's start). Its bytes
 * are made as EA's streamer reads them: headers, solids and records at open, textures' images read through as asked. */
Section *OpenSection(const Source &source, uint64_t position);
uint64_t SectionSize(const Section *section);
/** Copies the section's bytes from `offset` (counted from the section's start); how many. */
uint32_t ReadSection(Section *section, uint64_t offset, void *out, uint32_t bytes);
void CloseSection(Section *section);
} // namespace nfs::packbridge
