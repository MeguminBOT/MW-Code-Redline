/**
 * EA's file layouts made from typed resources (include/engine/resources/packbridge.h, docs/PACKS.md "The bridge"); the
 * same generator as tools/assetc/bridge.py (texture_bundle, and mwsolid.write_pack for solids).
 */
#include <windows.h>

#include <string.h>

#include "engine/resources/pack.h"
#include "engine/resources/packbridge.h"

namespace nfs::packbridge
{
namespace
{
const uint32_t kAlign = 0x80;
const uint32_t kDataLead = 0x78;
const uint32_t kTextureHeader = 128;
const uint32_t kMeshHeader = 192;
const uint32_t kRun = 64;
const uint32_t kSetHeader = nfs::pack::kSetHeaderBytes;
/** A texture's header up to its image and palette sizes (at 48 and 52). */
const uint32_t kTextureSizes = 56;
const uint32_t kInfoBytes = 0x7C;
const uint32_t kFormatBytes = 0x20;
const uint32_t kSetSolids = 1;
const uint32_t kSetTextures = 2;
/** A plain texture pack (every texture's record in the header, the images after: tpk.build_pack), which the front end
 * and the global data load whole; its items come sorted by key from the compiler. */
const uint32_t kSetTexturePack = 3;
/** The minimap's chops: a plain pack per texture, each stored (RAWW) in a 0x3A100 chunk, in the set's order. */
const uint32_t kSetTextureChops = 4;
const uint32_t kChop = 0x3A100;
const uint32_t kChopLead = 8 + 16;

void *Allocate(size_t size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void Release(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

template <typename T> T Get(const uint8_t *p, uint32_t at)
{
    T v;
    memcpy(&v, p + at, sizeof(T));
    return v;
}

template <typename T> void Put(uint8_t *p, uint32_t at, T v)
{
    memcpy(p + at, &v, sizeof(T));
}

uint32_t Pad16(uint32_t n)
{
    return (n + 15) & ~15u;
}

/** Bytes written to a buffer, or only counted (out null): one code path for a layout's sizes and its bytes. */
struct Out
{
    uint8_t *buffer;
    uint64_t at;

    void Bytes(const void *data, uint32_t n)
    {
        if (buffer && data)
            memcpy(buffer + at, data, n);
        at += n;
    }
    void Zero(uint32_t n)
    {
        if (buffer)
            memset(buffer + at, 0, n);
        at += n;
    }
    void Fill(uint8_t value, uint32_t n)
    {
        if (buffer)
            memset(buffer + at, value, n);
        at += n;
    }
    void U32(uint32_t v)
    {
        Bytes(&v, 4);
    }
    void F32(float v)
    {
        Bytes(&v, 4);
    }
    /** A chunk header whose size is patched by End. */
    uint64_t Begin(uint32_t id)
    {
        const uint64_t start = at;
        U32(id);
        U32(0);
        return start;
    }
    void End(uint64_t start)
    {
        if (buffer)
            Put<uint32_t>(buffer, static_cast<uint32_t>(start + 4), static_cast<uint32_t>(at - start - 8));
    }
    /** mwsolid._padding / bridge.header_at: a padding chunk after which the next chunk's header is aligned. */
    void PaddingTo(uint32_t align)
    {
        if (at % align == 0)
            return;
        const uint32_t gap = static_cast<uint32_t>((align - (at + 8) % align) % align);
        U32(0);
        U32(gap);
        Zero(gap);
    }
    /** mwsolid._lead: 0x11 words bringing the next byte to a multiple of align. */
    void Lead(uint32_t align)
    {
        Fill(0x11, static_cast<uint32_t>((align - at % align) % align) & ~3u);
    }
};

struct Item
{
    uint64_t start;
    uint64_t size;
};

} // namespace

struct File
{
    Source source;
    uint32_t kind;
    uint32_t count;
    uint32_t *items;
    Item *placed;
    uint8_t *head;
    uint64_t headSize;
    uint64_t size;
    /** The item last made, its bytes. */
    int64_t cached;
    uint8_t *cache;
    uint64_t cacheSize;
    /** Texture bundles: the image region's start; solid packs: the pack's own end (slack follows). */
    uint64_t imagesAt;
    /** Chops: the set, each chop's pack laid out from it. */
    uint8_t *set;
};

namespace
{
bool ReadAll(const Source &s, uint32_t resource, uint8_t *&out, uint64_t &size)
{
    size = s.size(s.context, resource);
    out = static_cast<uint8_t *>(Allocate(static_cast<size_t>(size)));
    return out && s.read(s.context, resource, 0, out, static_cast<uint32_t>(size));
}

// ------------------------------------------------------------------------------------------------------- textures

uint32_t LevelBytes(uint8_t format, uint32_t width, uint32_t height)
{
    const uint32_t bw = (width + 3) / 4 ? (width + 3) / 4 : 1, bh = (height + 3) / 4 ? (height + 3) / 4 : 1;
    switch (format)
    {
    case 1:
        return bw * bh * 8;
    case 2:
    case 3:
        return bw * bh * 16;
    case 4:
        return width * height * 4;
    default:
        return width * height;
    }
}

uint32_t D3DFormat(uint8_t format)
{
    switch (format)
    {
    case 1:
        return 0x31545844;
    case 2:
        return 0x33545844;
    case 3:
        return 0x35545844;
    case 4:
        return 21;
    default:
        return 41;
    }
}

/** bridge.texture_info and format_record: the texture's EA records after its image and palette. */
void MakeTextureRecords(const uint8_t *h, uint32_t placement, uint8_t *out)
{
    memset(out, 0, kInfoBytes + kFormatBytes);
    const uint32_t image = Get<uint32_t>(h, 48), palette = Get<uint32_t>(h, 52);
    memcpy(out + 0x0C, h + 56, 23);
    Put<uint32_t>(out, 0x24, Get<uint32_t>(h, 0));
    Put<uint32_t>(out, 0x28, Get<uint32_t>(h, 4));
    Put<uint32_t>(out, 0x2C, Get<uint32_t>(h, 8));
    Put<int32_t>(out, 0x30, static_cast<int32_t>(placement));
    Put<int32_t>(out, 0x34, palette ? static_cast<int32_t>(placement + image) : -1);
    Put<uint32_t>(out, 0x38, image);
    Put<uint32_t>(out, 0x3C, palette);
    Put<uint32_t>(out, 0x40, LevelBytes(h[16], Get<uint16_t>(h, 12), Get<uint16_t>(h, 14)));
    Put<uint16_t>(out, 0x44, Get<uint16_t>(h, 12));
    Put<uint16_t>(out, 0x46, Get<uint16_t>(h, 14));
    out[0x48] = h[80];
    out[0x49] = h[81];
    out[0x4A] = h[18];
    out[0x4B] = h[19];
    Put<uint16_t>(out, 0x4C, Get<uint16_t>(h, 44));
    out[0x4E] = h[17];
    // Tileable .. flags: the header's 20..28 in the TextureInfo's order.
    memcpy(out + 0x4F, h + 20, 9);
    memcpy(out + 0x58, h + 30, 14);
    uint8_t *format = out + kInfoBytes;
    Put<uint32_t>(format, 0x08, static_cast<uint8_t>(h[29]));
    Put<uint32_t>(format, 0x0C, 5);
    Put<uint32_t>(format, 0x10, h[82]);
    Put<uint32_t>(format, 0x14, D3DFormat(h[16]));
}

/** The textures' keys and entry sizes, from the set's rows or else from each texture's header. */
bool TextureSizes(File *f, const uint8_t *rows, uint32_t *keys, uint32_t *blobs)
{
    for (uint32_t i = 0; i < f->count; ++i)
    {
        uint32_t key, image, palette;
        if (rows)
        {
            const uint8_t *row = rows + nfs::pack::kSetRowBytes * i;
            key = Get<uint32_t>(row, 0);
            image = Get<uint32_t>(row, 4);
            palette = Get<uint32_t>(row, 8);
        }
        else
        {
            uint8_t h[kTextureSizes];
            if (!f->source.read(f->source.context, f->items[i], 0, h, kTextureSizes))
                return false;
            key = Get<uint32_t>(h, 0);
            image = Get<uint32_t>(h, 48);
            palette = Get<uint32_t>(h, 52);
        }
        keys[i] = key;
        blobs[i] = image + palette + kInfoBytes + kFormatBytes;
    }
    return true;
}

bool LayoutTextures(File *f, const uint8_t *set, const uint8_t *rows)
{
    const uint32_t n = f->count;
    uint32_t *keys = static_cast<uint32_t *>(Allocate(sizeof(uint32_t) * 2 * n));
    uint32_t *blobs = keys + n;
    if (!TextureSizes(f, rows, keys, blobs))
    {
        Release(keys);
        return false;
    }
    // The head (bridge.texture_bundle up to the images), measured then written.
    for (int pass = 0; pass < 2; ++pass)
    {
        Out o = {pass ? f->head : nullptr, 0};
        const uint64_t pack = o.Begin(0xB3300000);
        o.U32(0);
        o.U32(0x30);
        o.Zero(0x30);
        const uint64_t info = o.Begin(0xB3310000);
        const uint64_t h1 = o.Begin(0x33310001);
        o.Bytes(set + 8, 4);
        o.Bytes(set + 16, 28);
        o.Bytes(set + 48, 64);
        o.Bytes(set + 12, 4);
        o.Zero(24);
        o.End(h1);
        const uint64_t keyChunk = o.Begin(0x33310002);
        for (uint32_t i = 0; i < n; ++i)
        {
            o.U32(keys[i]);
            o.U32(0);
        }
        o.End(keyChunk);
        const uint64_t entries = o.Begin(0x33310003);
        const uint64_t entriesAt = o.at;
        o.Zero(24 * n);
        o.End(entries);
        o.End(info);
        o.PaddingTo(kAlign);
        const uint64_t data = o.Begin(0xB3320000);
        const uint64_t dh = o.Begin(0x33320001);
        o.Bytes(set + 112, 24);
        o.End(dh);
        o.PaddingTo(kAlign);
        const uint64_t images = o.Begin(0x33320002);
        o.Fill(0x11, kDataLead);
        f->imagesAt = o.at;
        uint64_t length = 0;
        for (uint32_t i = 0; i < n; ++i)
        {
            length = (length + kAlign - 1) & ~static_cast<uint64_t>(kAlign - 1);
            const uint64_t blob = blobs[i];
            f->placed[i].start = f->imagesAt + length;
            f->placed[i].size = blob;
            if (o.buffer)
            {
                uint8_t *e = o.buffer + entriesAt + 24 * i;
                Put<uint32_t>(e, 0, keys[i]);
                Put<uint32_t>(e, 4, static_cast<uint32_t>(f->placed[i].start));
                Put<uint32_t>(e, 8, static_cast<uint32_t>(blob));
                Put<uint32_t>(e, 12, static_cast<uint32_t>(blob));
            }
            length += blob;
        }
        length = (length + kAlign - 1) & ~static_cast<uint64_t>(kAlign - 1);
        f->size = f->imagesAt + length;
        if (o.buffer)
        {
            Put<uint32_t>(o.buffer, static_cast<uint32_t>(images + 4), static_cast<uint32_t>(f->size - images - 8));
            Put<uint32_t>(o.buffer, static_cast<uint32_t>(data + 4), static_cast<uint32_t>(f->size - data - 8));
            Put<uint32_t>(o.buffer, static_cast<uint32_t>(pack + 4), static_cast<uint32_t>(f->size - pack - 8));
        }
        if (!pass)
        {
            f->headSize = o.at;
            f->head = static_cast<uint8_t *>(Allocate(static_cast<size_t>(o.at)));
        }
    }
    Release(keys);
    return true;
}

bool MakeTexture(File *f, uint32_t i, uint8_t *out)
{
    uint8_t h[kTextureHeader];
    if (!f->source.read(f->source.context, f->items[i], 0, h, kTextureHeader))
        return false;
    const uint32_t image = Get<uint32_t>(h, 48), palette = Get<uint32_t>(h, 52);
    if (image + palette + kInfoBytes + kFormatBytes != f->placed[i].size ||
        !f->source.read(f->source.context, f->items[i], kTextureHeader, out, image + palette))
        return false;
    MakeTextureRecords(h, static_cast<uint32_t>(f->placed[i].start - f->imagesAt), out + image + palette);
    return true;
}

/** A plain texture pack's head: the pack, its info (header, keys, every texture's record and format) and the data
 * chunk up to the first image; each texture's image and palette follow at kAlign places (MakePlainTexture). */
bool LayoutPlainTextures(File *f, const uint8_t *set)
{
    const uint32_t n = f->count;
    uint8_t *headers = static_cast<uint8_t *>(Allocate(static_cast<size_t>(kTextureHeader) * (n ? n : 1)));
    uint8_t *records = static_cast<uint8_t *>(Allocate(kInfoBytes + kFormatBytes));
    for (uint32_t i = 0; i < n; ++i)
        if (!f->source.read(f->source.context, f->items[i], 0, headers + kTextureHeader * i, kTextureHeader))
        {
            Release(headers);
            Release(records);
            return false;
        }
    for (int pass = 0; pass < 2; ++pass)
    {
        Out o = {pass ? f->head : nullptr, 0};
        const uint64_t pack = o.Begin(0xB3300000);
        o.U32(0);
        o.U32(0x30);
        o.Zero(0x30);
        const uint64_t info = o.Begin(0xB3310000);
        const uint64_t h1 = o.Begin(0x33310001);
        o.Bytes(set + 8, 4);
        o.Bytes(set + 16, 28);
        o.Bytes(set + 48, 64);
        o.Bytes(set + 12, 4);
        o.Zero(24);
        o.End(h1);
        const uint64_t keyChunk = o.Begin(0x33310002);
        for (uint32_t i = 0; i < n; ++i)
        {
            o.U32(Get<uint32_t>(headers + kTextureHeader * i, 0));
            o.U32(0);
        }
        o.End(keyChunk);
        // Each texture's place among the images, the same in both passes.
        uint64_t length = 0;
        const uint64_t infos = o.Begin(0x33310004);
        for (uint32_t i = 0; i < n; ++i)
        {
            const uint8_t *h = headers + kTextureHeader * i;
            length = (length + kAlign - 1) & ~static_cast<uint64_t>(kAlign - 1);
            MakeTextureRecords(h, static_cast<uint32_t>(length), records);
            o.Bytes(records, kInfoBytes);
            length += Get<uint32_t>(h, 48) + Get<uint32_t>(h, 52);
        }
        o.End(infos);
        const uint64_t formats = o.Begin(0x33310005);
        for (uint32_t i = 0; i < n; ++i)
        {
            MakeTextureRecords(headers + kTextureHeader * i, 0, records);
            o.Bytes(records + kInfoBytes, kFormatBytes);
        }
        o.End(formats);
        o.End(info);
        o.PaddingTo(kAlign);
        const uint64_t data = o.Begin(0xB3320000);
        const uint64_t dh = o.Begin(0x33320001);
        o.Bytes(set + 112, 24);
        o.End(dh);
        o.PaddingTo(kAlign);
        const uint64_t images = o.Begin(0x33320002);
        o.Fill(0x11, kDataLead);
        f->imagesAt = o.at;
        length = 0;
        for (uint32_t i = 0; i < n; ++i)
        {
            const uint8_t *h = headers + kTextureHeader * i;
            length = (length + kAlign - 1) & ~static_cast<uint64_t>(kAlign - 1);
            f->placed[i].start = f->imagesAt + length;
            f->placed[i].size = Get<uint32_t>(h, 48) + Get<uint32_t>(h, 52);
            length += f->placed[i].size;
        }
        length = (length + kAlign - 1) & ~static_cast<uint64_t>(kAlign - 1);
        f->size = f->imagesAt + length;
        if (o.buffer)
        {
            Put<uint32_t>(o.buffer, static_cast<uint32_t>(images + 4), static_cast<uint32_t>(f->size - images - 8));
            Put<uint32_t>(o.buffer, static_cast<uint32_t>(data + 4), static_cast<uint32_t>(f->size - data - 8));
            Put<uint32_t>(o.buffer, static_cast<uint32_t>(pack + 4), static_cast<uint32_t>(f->size - pack - 8));
        }
        if (!pass)
        {
            f->headSize = o.at;
            f->head = static_cast<uint8_t *>(Allocate(static_cast<size_t>(o.at)));
        }
    }
    Release(headers);
    Release(records);
    return true;
}

/** A plain pack's texture: its image and palette (the records are in the head). */
bool MakePlainTexture(File *f, uint32_t i, uint8_t *out)
{
    return f->source.read(f->source.context, f->items[i], kTextureHeader, out,
                          static_cast<uint32_t>(f->placed[i].size));
}

// --------------------------------------------------------------------------------------------------------- solids

/** mwsolid Solid.payload of a typed mesh, as the chunk 0x80134010 at `o` (its header included). A version 2 mesh (a
 * world solid) has a vertex buffer per stream record, each its own 0x00134B01 chunk. */
void SolidChunk(Out &o, const uint8_t *m)
{
    const bool world = Get<uint16_t>(m, 4) == 2;
    const uint32_t verts = Get<uint32_t>(m, 8), indexCount = Get<uint32_t>(m, 12);
    const uint16_t runs = Get<uint16_t>(m, 16), textures = Get<uint16_t>(m, 18), lights = Get<uint16_t>(m, 20),
                   markers = Get<uint16_t>(m, 22), nameLength = Get<uint16_t>(m, 24), extras = Get<uint16_t>(m, 26);
    const uint32_t streams = world ? Get<uint16_t>(m, 28) : 0;
    const uint32_t presentCount = Get<uint32_t>(m, 168);
    uint32_t p = kMeshHeader;
    const uint8_t *name = m + p;
    p += Pad16(nameLength);
    const uint8_t *textureKeys = m + p;
    p += Pad16(4u * textures);
    const uint8_t *lightKeys = m + p;
    p += Pad16(4u * lights);
    const uint8_t *markerData = m + p;
    p += 80u * markers;
    const uint8_t *runData = m + p;
    p += kRun * runs;
    const uint8_t *runNames = m + p;
    p += 64u * runs;
    const uint8_t *streamTable = m + p;
    p += 16u * streams;
    const uint8_t *vertices = m + p;
    if (world)
        for (uint32_t s = 0; s < streams; ++s)
            p += Pad16(Get<uint32_t>(streamTable, 16 * s) * Get<uint32_t>(streamTable, 16 * s + 4));
    else
        p += Pad16(36u * verts);
    const uint8_t *indices = m + p;
    p += Pad16(2u * indexCount);
    const uint8_t *extraData = m + p;
    uint32_t e = p;
    for (uint32_t i = 0; i < extras; ++i)
        e += 16 + Pad16(Get<uint32_t>(m, e + 4));
    const uint8_t *present = m + e;
    auto has = [&](uint32_t id) {
        if (presentCount == 0xFFFFFFFFu)
            return true;
        for (uint32_t i = 0; i < presentCount; ++i)
            if (Get<uint32_t>(present, 4 * i) == id)
                return true;
        return false;
    };

    const uint64_t solid = o.Begin(0x80134010);
    const uint64_t head = o.Begin(0x00134011);
    o.Lead(16);
    o.Zero(12);
    o.Bytes(m + 120, 4);
    o.Bytes(m, 4);
    o.Bytes(m + 152, 4);
    const uint8_t counts[4] = {0, static_cast<uint8_t>(textures), static_cast<uint8_t>(lights), 0};
    o.Bytes(counts, 4);
    o.Zero(4);
    o.Bytes(m + 32, 12);
    o.Zero(4);
    o.Bytes(m + 44, 12);
    o.Zero(4);
    o.Bytes(m + 56, 64);
    o.Bytes(m + 124, 20);
    o.Bytes(m + 160, 4);
    o.Bytes(m + 144, 8);
    uint32_t nameBytes = 0;
    while (nameBytes < nameLength && name[nameBytes])
        ++nameBytes;
    o.Bytes(name, nameBytes);
    o.Zero(1);
    o.Zero((4 - (nameBytes + 1) % 4) % 4);
    o.End(head);
    if (has(0x00134012))
    {
        const uint64_t c = o.Begin(0x00134012);
        for (uint32_t i = 0; i < textures; ++i)
        {
            o.Bytes(textureKeys + 4 * i, 4);
            o.U32(0);
        }
        o.End(c);
    }
    if (has(0x00134013))
    {
        const uint64_t c = o.Begin(0x00134013);
        for (uint32_t i = 0; i < lights; ++i)
        {
            o.Bytes(lightKeys + 4 * i, 4);
            o.U32(0);
        }
        o.End(c);
    }
    if (markers)
    {
        const uint64_t c = o.Begin(0x0013401A);
        o.Lead(16);
        for (uint32_t i = 0; i < markers; ++i)
        {
            o.Bytes(markerData + 80 * i, 80);
        }
        o.End(c);
    }
    const uint64_t mesh = o.Begin(0x80134100);
    uint32_t flags = Get<uint32_t>(m, 156);
    if (flags == 0xFFFFFFFFu)
    {
        flags = 0;
        for (uint32_t i = 0; i < runs; ++i)
            flags |= Get<uint32_t>(runData, kRun * i + 20);
    }
    {
        const uint64_t c = o.Begin(0x00134900);
        o.Lead(16);
        const uint32_t desc[12] = {0, 0, 0x12, flags, runs, 0, world ? streams : has(0x00134B01) ? 1u : 0u,
                                   0, 0, 0, 0, indexCount};
        o.Bytes(desc, sizeof(desc));
        o.End(c);
    }
    {
        const uint64_t c = o.Begin(0x00134B02);
        o.Lead(16);
        for (uint32_t i = 0; i < runs; ++i)
        {
            const uint8_t *r = runData + kRun * i;
            uint8_t rec[0x68] = {};
            memcpy(rec, r + 32, 24);
            memcpy(rec + 0x18, r + 24, 6);
            Put<uint32_t>(rec, 0x30, Get<uint32_t>(r, 16));
            Put<uint32_t>(rec, 0x38, Get<uint32_t>(r, 20));
            Put<uint32_t>(rec, 0x3C, Get<uint32_t>(r, 12));
            Put<uint32_t>(rec, 0x40, Get<uint32_t>(r, 8));
            Put<uint32_t>(rec, 0x44, Get<uint32_t>(r, 0));
            Put<uint32_t>(rec, 0x5C, Get<uint32_t>(r, 4));
            o.Bytes(rec, sizeof(rec));
        }
        o.End(c);
    }
    {
        const uint64_t c = o.Begin(0x00134B03);
        o.Lead(16);
        o.Bytes(indices, 2 * indexCount);
        o.Zero((2 * indexCount) % 4);
        o.End(c);
    }
    if (world)
    {
        const uint8_t *v = vertices;
        for (uint32_t s = 0; s < streams; ++s)
        {
            const uint32_t bytes = Get<uint32_t>(streamTable, 16 * s) * Get<uint32_t>(streamTable, 16 * s + 4);
            const uint64_t c = o.Begin(0x00134B01);
            o.Lead(128);
            o.Bytes(v, bytes);
            o.End(c);
            v += Pad16(bytes);
        }
    }
    else if (has(0x00134B01))
    {
        const uint64_t c = o.Begin(0x00134B01);
        o.Lead(128);
        o.Bytes(vertices, 36 * verts);
        o.End(c);
    }
    for (uint32_t i = 0; i < runs; ++i)
    {
        const uint64_t c = o.Begin(0x00134C02);
        const uint8_t *rn = runNames + 64 * i;
        const uint32_t len = Get<uint16_t>(runData + kRun * i, 30);
        o.Bytes(rn, len);
        o.Zero(1);
        o.Zero((4 - (len + 1) % 4) % 4);
        o.End(c);
    }
    o.End(mesh);
    uint32_t q = static_cast<uint32_t>(extraData - m);
    for (uint32_t i = 0; i < extras; ++i)
    {
        const uint32_t id = Get<uint32_t>(m, q), size = Get<uint32_t>(m, q + 4);
        o.U32(id);
        o.U32(size);
        o.Bytes(m + q + 16, size);
        q += 16 + Pad16(size);
    }
    o.End(solid);
}

bool LayoutSolids(File *f, const uint8_t *set, const uint8_t *rows)
{
    const uint32_t n = f->count;
    const uint64_t headerSize = 8 + (8 + 0x90) + (8 + 8ull * n) + (8 + 24ull * n) + 8;
    const uint64_t start = 8 + 8 + headerSize;
    uint32_t *keys = static_cast<uint32_t *>(Allocate(4 * n));
    // The solids' places: each starts at a multiple of kAlign, so its chunk's size is the set's row's, or measured.
    uint64_t at = start;
    for (uint32_t i = 0; i < n; ++i)
    {
        Out pad = {nullptr, at};
        pad.PaddingTo(kAlign);
        at = pad.at;
        uint64_t size;
        if (rows)
        {
            keys[i] = Get<uint32_t>(rows + nfs::pack::kSetRowBytes * i, 0);
            size = Get<uint32_t>(rows + nfs::pack::kSetRowBytes * i, 4);
        }
        else
        {
            uint8_t *mesh;
            uint64_t bytes;
            if (!ReadAll(f->source, f->items[i], mesh, bytes))
            {
                Release(mesh);
                Release(keys);
                return false;
            }
            keys[i] = Get<uint32_t>(mesh, 0);
            Out o = {nullptr, at};
            SolidChunk(o, mesh);
            size = o.at - at;
            Release(mesh);
        }
        f->placed[i].start = at;
        f->placed[i].size = size;
        at += size;
    }
    const uint64_t packSize = at;
    const uint64_t slack = packSize / 64 + 0x120;
    const uint64_t tail = (0x40 - (packSize + 8 + slack + 8) % 0x40) % 0x40;
    f->size = packSize + 8 + slack + 8 + tail;
    f->imagesAt = packSize;
    // The head: the pack's chunk, its header (info, keys, index in key order), the padding before the first solid.
    f->headSize = start;
    f->head = static_cast<uint8_t *>(Allocate(static_cast<size_t>(start)));
    uint32_t *order = static_cast<uint32_t *>(Allocate(4 * n));
    for (uint32_t i = 0; i < n; ++i)
        order[i] = i;
    for (uint32_t i = 1; i < n; ++i)
    {
        const uint32_t v = order[i];
        uint32_t j = i;
        while (j > 0 && (keys[order[j - 1]] > keys[v] ||
                         (keys[order[j - 1]] == keys[v] && f->placed[order[j - 1]].start > f->placed[v].start)))
        {
            order[j] = order[j - 1];
            --j;
        }
        order[j] = v;
    }
    Out o = {f->head, 0};
    o.U32(0x80134000);
    o.U32(static_cast<uint32_t>(packSize - 8));
    o.U32(0);
    o.U32(0);
    const uint64_t header = o.Begin(0x80134001);
    const uint64_t info = o.Begin(0x00134002);
    o.U32(0);
    o.U32(0);
    o.U32(0x1D);
    o.U32(n);
    o.Bytes(set + 48, 0x38);
    o.Bytes(set + 16, 0x20);
    o.U32(static_cast<uint32_t>(f->size));
    o.U32(0);
    o.U32(0x80);
    o.Zero(0x90 - 0x74);
    o.End(info);
    const uint64_t k = o.Begin(0x00134003);
    for (uint32_t i = 0; i < n; ++i)
    {
        o.U32(keys[order[i]]);
        o.U32(0);
    }
    o.End(k);
    const uint64_t index = o.Begin(0x00134004);
    for (uint32_t i = 0; i < n; ++i)
    {
        const Item &it = f->placed[order[i]];
        o.U32(keys[order[i]]);
        o.U32(static_cast<uint32_t>(it.start));
        o.U32(static_cast<uint32_t>(it.size));
        o.U32(static_cast<uint32_t>(it.size));
        o.U32(0);
        o.U32(0);
    }
    o.End(index);
    o.Begin(0x80134008);
    o.End(header);
    Release(order);
    Release(keys);
    return o.at == start;
}

bool MakeSolid(File *f, uint32_t i, uint8_t *out)
{
    uint8_t *mesh;
    uint64_t size;
    if (!ReadAll(f->source, f->items[i], mesh, size))
    {
        Release(mesh);
        return false;
    }
    Out o = {out - f->placed[i].start, f->placed[i].start};
    Out measure = {nullptr, f->placed[i].start};
    SolidChunk(measure, mesh);
    const bool fits = measure.at == f->placed[i].start + f->placed[i].size;
    if (fits)
        SolidChunk(o, mesh);
    Release(mesh);
    return fits;
}

/** One chop's plain pack laid out (its head and size) as a file of its own. */
bool ChopPack(File *f, uint32_t i, File &one, Item &placed)
{
    memset(&one, 0, sizeof(one));
    one.source = f->source;
    one.kind = kSetTexturePack;
    one.count = 1;
    one.items = f->items + i;
    one.placed = &placed;
    if (LayoutPlainTextures(&one, f->set))
        return true;
    Release(one.head);
    return false;
}

bool LayoutChops(File *f, const uint8_t *set, uint64_t setBytes)
{
    f->set = static_cast<uint8_t *>(Allocate(static_cast<size_t>(setBytes)));
    memcpy(f->set, set, static_cast<size_t>(setBytes));
    uint64_t at = 0;
    for (uint32_t i = 0; i < f->count; ++i)
    {
        File one;
        Item placed;
        if (!ChopPack(f, i, one, placed))
            return false;
        Release(one.head);
        f->placed[i].start = at;
        f->placed[i].size = kChopLead + one.size;
        at += f->placed[i].size;
    }
    f->size = at;
    return true;
}

bool MakeChop(File *f, uint32_t i, uint8_t *out)
{
    File one;
    Item placed;
    if (!ChopPack(f, i, one, placed))
        return false;
    const uint32_t size = static_cast<uint32_t>(one.size);
    Put<uint32_t>(out, 0, kChop);
    Put<uint32_t>(out, 4, size + 16);
    memcpy(out + 8, "RAWW", 4);
    Put<uint32_t>(out, 12, 0x1001);
    Put<uint32_t>(out, 16, size);
    Put<uint32_t>(out, 20, size + 16);
    uint8_t *pack = out + kChopLead;
    memcpy(pack, one.head, static_cast<size_t>(one.headSize));
    memset(pack + one.headSize, 0, static_cast<size_t>(one.size - one.headSize));
    const bool ok = MakePlainTexture(&one, 0, pack + placed.start);
    Release(one.head);
    return ok;
}

bool Make(File *f, uint32_t i, uint8_t *out)
{
    return f->kind == kSetTextures       ? MakeTexture(f, i, out)
           : f->kind == kSetTexturePack  ? MakePlainTexture(f, i, out)
           : f->kind == kSetTextureChops ? MakeChop(f, i, out)
                                         : MakeSolid(f, i, out);
}
} // namespace

File *Open(const Source &source, const uint8_t *set, uint64_t setBytes, const uint32_t *items, uint32_t count)
{
    if (!set || setBytes < kSetHeader)
        return nullptr;
    const uint64_t rowsAt = kSetHeader + 8ull * count;
    const uint8_t *rows = Get<uint32_t>(set, nfs::pack::kSetLayout) == nfs::pack::kSetLayoutRows &&
                                  Get<uint32_t>(set, 0) == count &&
                                  setBytes >= rowsAt + static_cast<uint64_t>(nfs::pack::kSetRowBytes) * count
                              ? set + rowsAt
                              : nullptr;
    File *f = static_cast<File *>(Allocate(sizeof(File)));
    f->source = source;
    f->kind = Get<uint32_t>(set, 4);
    f->count = count;
    f->items = static_cast<uint32_t *>(Allocate(4 * (count ? count : 1)));
    memcpy(f->items, items, 4 * count);
    f->placed = static_cast<Item *>(Allocate(sizeof(Item) * (count ? count : 1)));
    f->cached = -1;
    const bool ok = f->kind == kSetTextures      ? LayoutTextures(f, set, rows)
                    : f->kind == kSetTexturePack ? LayoutPlainTextures(f, set)
                    : f->kind == kSetTextureChops ? LayoutChops(f, set, setBytes)
                    : f->kind == kSetSolids      ? LayoutSolids(f, set, rows)
                                                 : false;
    if (!ok)
    {
        Close(f);
        return nullptr;
    }
    return f;
}

uint64_t SolidRecord(const uint8_t *mesh, uint8_t *out)
{
    Out o = {out, 0};
    SolidChunk(o, mesh);
    return o.at;
}

void TextureRecords(const uint8_t *header, uint32_t placement, uint8_t *out)
{
    MakeTextureRecords(header, placement, out);
}

uint64_t Size(const File *file)
{
    return file->size;
}

uint32_t Read(File *f, uint64_t offset, void *buffer, uint32_t bytes)
{
    uint8_t *out = static_cast<uint8_t *>(buffer);
    uint32_t done = 0;
    while (done < bytes && offset < f->size)
    {
        uint64_t take = bytes - done;
        if (offset < f->headSize)
        {
            if (take > f->headSize - offset)
                take = f->headSize - offset;
            memcpy(out + done, f->head + offset, static_cast<size_t>(take));
        }
        else
        {
            // The item holding this offset, or the gap before the next one.
            uint32_t lo = 0, hi = f->count;
            while (lo < hi)
            {
                const uint32_t mid = (lo + hi) / 2;
                if (f->placed[mid].start + f->placed[mid].size <= offset)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            if (lo < f->count && offset >= f->placed[lo].start)
            {
                const Item &it = f->placed[lo];
                const uint64_t within = offset - it.start;
                if (take > it.size - within)
                    take = it.size - within;
                if (f->cached != static_cast<int64_t>(lo) && !within && take == it.size)
                {
                    // The whole item asked for: made straight into the reader's buffer.
                    if (!Make(f, lo, out + done))
                        break;
                }
                else
                {
                    if (f->cached != static_cast<int64_t>(lo))
                    {
                        if (f->cacheSize < it.size)
                        {
                            Release(f->cache);
                            f->cache = static_cast<uint8_t *>(Allocate(static_cast<size_t>(it.size)));
                            f->cacheSize = it.size;
                        }
                        f->cached = -1;
                        if (!Make(f, lo, f->cache))
                            break;
                        f->cached = lo;
                    }
                    memcpy(out + done, f->cache + within, static_cast<size_t>(take));
                }
            }
            else
            {
                // Padding between items and after the last: zero chunks' bytes, written as the generator writes them.
                const uint64_t next = lo < f->count ? f->placed[lo].start : f->size;
                if (take > next - offset)
                    take = next - offset;
                if (f->kind == kSetSolids)
                {
                    // A solid pack's gaps are padding chunks (id 0, size) and its slack after the pack.
                    Out o = {nullptr, 0};
                    memset(out + done, 0, static_cast<size_t>(take));
                    const uint64_t gapStart = lo ? f->placed[lo - 1].start + f->placed[lo - 1].size
                                                 : f->headSize;
                    (void)o;
                    // Chunk headers at the gap's start: [0][size], then zeros; the slack has two such chunks.
                    auto header = [&](uint64_t at, uint32_t size) {
                        for (uint32_t b = 0; b < 8; ++b)
                        {
                            const uint64_t pos = at + b;
                            if (pos >= offset && pos < offset + take)
                            {
                                const uint8_t v = b < 4 ? 0 : static_cast<uint8_t>(size >> (8 * (b - 4)));
                                out[done + (pos - offset)] = v;
                            }
                        }
                    };
                    if (lo < f->count)
                    {
                        if (next > gapStart)
                            header(gapStart, static_cast<uint32_t>(next - gapStart - 8));
                    }
                    else
                    {
                        const uint64_t slack = f->imagesAt / 64 + 0x120;
                        header(f->imagesAt, static_cast<uint32_t>(slack));
                        header(f->imagesAt + 8 + slack, static_cast<uint32_t>(f->size - (f->imagesAt + 8 + slack) - 8));
                    }
                }
                else
                    memset(out + done, 0, static_cast<size_t>(take));
            }
        }
        done += static_cast<uint32_t>(take);
        offset += take;
    }
    return done;
}

void Close(File *f)
{
    if (!f)
        return;
    Release(f->items);
    Release(f->placed);
    Release(f->head);
    Release(f->cache);
    Release(f->set);
    Release(f);
}
// ----------------------------------------------------------------------------------------------------------- world

namespace
{
const uint32_t kPiecePadding = 1, kPieceTextures = 2, kPieceSolids = 3;
const uint32_t kSectionHeader = 64, kPiece = 64;
const uint32_t kScenery = 0x80034100, kTriggers = 0x80036000, kTextureAnims = 0xB0300100, kHeli = 0x00034159,
               kSmokeables = 0x00034027, kEmitters = 0x0003BC00, kCollision = 0x0003B801;

/** What a part of a section's bytes is: zeros, the section's own made bytes (at `from` in its arena) or bytes of one
 * of its resources (from `from`). */
struct Span
{
    uint64_t start;
    uint64_t size;
    uint64_t from;
    uint32_t item;
    uint8_t kind;
};
const uint8_t kSpanZero = 0, kSpanArena = 1, kSpanItem = 2;

/** A tables resource (assetc/world.py): table `i`'s rows. */
struct Tables
{
    const uint8_t *data;
    uint32_t count;

    const uint8_t *Rows(uint32_t i, uint32_t &rows, uint32_t &size) const
    {
        if (i >= count)
        {
            rows = size = 0;
            return data;
        }
        size = Get<uint32_t>(data, 16 + 16 * i);
        rows = Get<uint32_t>(data, 16 + 16 * i + 4);
        return data + Get<uint32_t>(data, 16 + 16 * i + 8);
    }
    uint32_t Bytes(uint32_t i) const
    {
        uint32_t rows, size;
        Rows(i, rows, size);
        return rows * size;
    }
    void Put(Out &o, uint32_t i) const
    {
        uint32_t rows, size;
        const uint8_t *p = Rows(i, rows, size);
        o.Bytes(p, rows * size);
    }
};
} // namespace

struct Section
{
    Source source;
    uint8_t *arena;
    uint64_t arenaSize;
    uint64_t arenaCapacity;
    Span *spans;
    uint32_t spanCount;
    uint32_t spanCapacity;
    uint64_t position;
    uint64_t size;
};

namespace
{
bool Grow(Section *s, uint64_t more)
{
    if (s->arenaSize + more <= s->arenaCapacity)
        return true;
    uint64_t capacity = s->arenaCapacity ? s->arenaCapacity : 1 << 16;
    while (capacity < s->arenaSize + more)
        capacity *= 2;
    void *p = s->arena ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, s->arena, static_cast<size_t>(capacity))
                       : Allocate(static_cast<size_t>(capacity));
    if (!p)
        return false;
    s->arena = static_cast<uint8_t *>(p);
    s->arenaCapacity = capacity;
    return true;
}

void AddSpan(Section *s, uint8_t kind, uint64_t size, uint64_t from, uint32_t item)
{
    if (!size)
        return;
    if (s->spanCount)
    {
        Span &last = s->spans[s->spanCount - 1];
        if (last.kind == kind && (kind == kSpanZero || (last.from + last.size == from && last.item == item)))
        {
            last.size += size;
            s->size += size;
            return;
        }
    }
    if (s->spanCount == s->spanCapacity)
    {
        const uint32_t capacity = s->spanCapacity ? s->spanCapacity * 2 : 64;
        Span *spans = static_cast<Span *>(Allocate(sizeof(Span) * capacity));
        if (s->spans)
            memcpy(spans, s->spans, sizeof(Span) * s->spanCount);
        Release(s->spans);
        s->spans = spans;
        s->spanCapacity = capacity;
    }
    s->spans[s->spanCount++] = {s->position + s->size, size, from, item, kind};
    s->size += size;
}

/** Made bytes: `make(o)` measured where they land, then written into the arena. */
template <typename F> bool Made(Section *s, F make)
{
    const uint64_t at = s->position + s->size;
    Out measure = {nullptr, at};
    if (!make(measure))
        return false;
    const uint64_t n = measure.at - at;
    if (!Grow(s, n))
        return false;
    Out o = {s->arena + s->arenaSize - at, at};
    if (!make(o) || o.at != measure.at)
        return false;
    AddSpan(s, kSpanArena, n, s->arenaSize, 0);
    s->arenaSize += n;
    return true;
}

/** mwsolid.write_list: the solid list's header, then its solids one after another. */
bool SolidsPiece(Section *s, const uint8_t *piece, const uint32_t *ids, const char *strings)
{
    const uint32_t first = Get<uint32_t>(piece, 4), count = Get<uint32_t>(piece, 8);
    const char *filename = strings + Get<uint32_t>(piece, 12), *section = strings + Get<uint32_t>(piece, 16);
    const uint32_t sizeField = Get<uint32_t>(piece, 20);
    uint32_t *keys = static_cast<uint32_t *>(Allocate(4 * (count ? count : 1)));
    for (uint32_t i = 0; i < count; ++i)
        if (!s->source.read(s->source.context, first + i + 1, 0, keys + i, 4))
        {
            Release(keys);
            return false;
        }
    for (uint32_t i = 1; i < count; ++i)
        for (uint32_t j = i; j > 0 && keys[j - 1] > keys[j]; --j)
        {
            const uint32_t k = keys[j];
            keys[j] = keys[j - 1];
            keys[j - 1] = k;
        }
    (void)ids;
    // The pack's chunk header (its size patched at the end), the padding to the header, the header.
    const uint64_t packAt = s->position + s->size;
    const uint64_t arenaAt = s->arenaSize;
    bool ok = Made(s, [&](Out &o) {
        o.U32(0x80134000);
        o.U32(0);
        const uint32_t lead = static_cast<uint32_t>((16 - (packAt + 16) % 16) % 16);
        o.U32(0);
        o.U32(lead);
        o.Zero(lead);
        const uint64_t header = o.Begin(0x80134001);
        const uint64_t info = o.Begin(0x00134002);
        o.U32(0);
        o.U32(0);
        o.U32(0x1D);
        o.U32(count);
        uint8_t name[0x58] = {};
        strncpy(reinterpret_cast<char *>(name), filename, 0x38);
        strncpy(reinterpret_cast<char *>(name) + 0x38, section, 0x20);
        o.Bytes(name, 0x58);
        o.U32(sizeField);
        o.U32(0);
        o.U32(0x80);
        o.Zero(0x90 - 0x74);
        o.End(info);
        const uint64_t k = o.Begin(0x00134003);
        for (uint32_t i = 0; i < count; ++i)
        {
            o.U32(keys[i]);
            o.U32(0);
        }
        o.End(k);
        o.End(o.Begin(0x80134008));
        o.End(header);
        return true;
    });
    Release(keys);
    for (uint32_t i = 0; ok && i < count; ++i)
    {
        uint8_t *mesh;
        uint64_t size;
        ok = ReadAll(s->source, first + i + 1, mesh, size);
        if (ok)
            ok = Made(s, [&](Out &o) {
                SolidChunk(o, mesh);
                return true;
            });
        Release(mesh);
    }
    if (ok)
        Put<uint32_t>(s->arena, static_cast<uint32_t>(arenaAt + 4), static_cast<uint32_t>(s->position + s->size - packAt - 8));
    return ok;
}

/** tpk.build_pack: the pack's header part made, its palettes and images read from the textures as they are. */
bool TexturesPiece(Section *s, const uint8_t *piece, const char *strings, const uint8_t *copies)
{
    const uint32_t first = Get<uint32_t>(piece, 4), n = Get<uint32_t>(piece, 8);
    const char *name = strings + Get<uint32_t>(piece, 12), *filename = strings + Get<uint32_t>(piece, 16);
    const uint32_t version = Get<uint32_t>(piece, 20), hash = Get<uint32_t>(piece, 24), endMod = Get<uint32_t>(piece, 28);
    uint8_t(*headers)[kTextureHeader] = static_cast<uint8_t(*)[kTextureHeader]>(Allocate(kTextureHeader * (n ? n : 1)));
    uint32_t *order = static_cast<uint32_t *>(Allocate(4 * (n ? n : 1)));
    uint64_t *placed = static_cast<uint64_t *>(Allocate(8 * (n ? n : 1)));
    bool ok = true;
    for (uint32_t i = 0; ok && i < n; ++i)
    {
        if (copies)
            memcpy(headers[i], copies + kTextureHeader * i, kTextureHeader);
        else
            ok = s->source.read(s->source.context, first + i + 1, 0, headers[i], kTextureHeader);
        order[i] = i;
    }
    for (uint32_t i = 1; ok && i < n; ++i)
        for (uint32_t j = i; j > 0 && Get<uint32_t>(headers[order[j - 1]], 0) > Get<uint32_t>(headers[order[j]], 0); --j)
        {
            const uint32_t v = order[j];
            order[j] = order[j - 1];
            order[j - 1] = v;
        }
    // The images' region: the palettes one after another, then each image at a multiple of 0x80.
    uint64_t palettes = 0;
    for (uint32_t i = 0; i < n; ++i)
        palettes += Get<uint32_t>(headers[order[i]], 52);
    uint64_t length = palettes;
    for (uint32_t i = 0; i < n; ++i)
    {
        length = (length + kAlign - 1) & ~static_cast<uint64_t>(kAlign - 1);
        placed[i] = length;
        length += Get<uint32_t>(headers[order[i]], 48);
    }
    const uint64_t packAt = s->position + s->size;
    uint64_t imagesAt = 0;
    if (ok)
        ok = Made(s, [&](Out &o) {
            const uint64_t pack = o.Begin(0xB3300000);
            o.U32(0);
            o.U32(48);
            o.Zero(48);
            const uint64_t info = o.Begin(0xB3310000);
            const uint64_t h1 = o.Begin(0x33310001);
            o.U32(version);
            uint8_t names[28 + 64] = {};
            strncpy(reinterpret_cast<char *>(names), name, 28);
            strncpy(reinterpret_cast<char *>(names) + 28, filename, 64);
            o.Bytes(names, sizeof(names));
            o.U32(hash);
            o.Zero(24);
            o.End(h1);
            const uint64_t keys = o.Begin(0x33310002);
            for (uint32_t i = 0; i < n; ++i)
            {
                o.Bytes(headers[order[i]], 4);
                o.U32(0);
            }
            o.End(keys);
            uint8_t records[kInfoBytes + kFormatBytes];
            const uint64_t infos = o.Begin(0x33310004);
            uint64_t palette = 0;
            for (uint32_t i = 0; i < n; ++i)
            {
                const uint8_t *h = headers[order[i]];
                MakeTextureRecords(h, static_cast<uint32_t>(placed[i]), records);
                Put<uint32_t>(records, 0x34, static_cast<uint32_t>(palette));
                palette += Get<uint32_t>(h, 52);
                o.Bytes(records, kInfoBytes);
            }
            o.End(infos);
            const uint64_t formats = o.Begin(0x33310005);
            for (uint32_t i = 0; i < n; ++i)
            {
                MakeTextureRecords(headers[order[i]], 0, records);
                o.Bytes(records + kInfoBytes, kFormatBytes);
            }
            o.End(formats);
            o.End(info);
            const uint32_t gap = static_cast<uint32_t>((kAlign - (o.at - pack + 8) % kAlign) % kAlign);
            o.U32(0);
            o.U32(gap);
            o.Zero(gap);
            const uint64_t data = o.Begin(0xB3320000);
            const uint64_t dh = o.Begin(0x33320001);
            for (uint32_t i = 0; i < 6; ++i)
                o.U32(Get<uint32_t>(piece, 32 + 4 * i));
            o.End(dh);
            o.U32(0);
            o.U32(80);
            o.Zero(80);
            const uint64_t images = o.Begin(0x33320002);
            o.Fill(0x11, kDataLead);
            imagesAt = o.at;
            const uint64_t end = imagesAt - pack + length;
            const uint64_t tail = (kAlign + endMod - end % kAlign) % kAlign;
            const uint64_t total = imagesAt + length + tail;
            if (o.buffer)
            {
                Put<uint32_t>(o.buffer, static_cast<uint32_t>(images + 4), static_cast<uint32_t>(total - images - 8));
                Put<uint32_t>(o.buffer, static_cast<uint32_t>(data + 4), static_cast<uint32_t>(total - data - 8));
                Put<uint32_t>(o.buffer, static_cast<uint32_t>(pack + 4), static_cast<uint32_t>(total - pack - 8));
            }
            return true;
        });
    (void)packAt;
    if (ok)
    {
        uint64_t at = 0;
        for (uint32_t i = 0; i < n; ++i)
        {
            const uint32_t p = Get<uint32_t>(headers[order[i]], 52);
            AddSpan(s, kSpanItem, p, kTextureHeader + Get<uint32_t>(headers[order[i]], 48), first + order[i] + 1);
            at += p;
        }
        for (uint32_t i = 0; i < n; ++i)
        {
            AddSpan(s, kSpanZero, placed[i] - at, 0, 0);
            const uint32_t image = Get<uint32_t>(headers[order[i]], 48);
            AddSpan(s, kSpanItem, image, kTextureHeader, first + order[i] + 1);
            at = placed[i] + image;
        }
        const uint64_t end = imagesAt - packAt + length;
        AddSpan(s, kSpanZero, (kAlign + endMod - end % kAlign) % kAlign, 0, 0);
    }
    Release(headers);
    Release(order);
    Release(placed);
    return ok;
}

/** mwworld.write_collision: the carp header, then the CARP tree (root, articles' group, data records, data). */
bool CollisionBytes(Out &o, const Tables &t)
{
    uint32_t rows, size;
    const uint8_t *header = t.Rows(0, rows, size);
    const uint8_t *params = t.Rows(1, rows, size);
    const uint32_t spare = Get<uint32_t>(params, 0), nameSize = Get<uint32_t>(params, 4);
    uint32_t nameLength;
    const uint8_t *name = t.Rows(2, nameLength, size);
    uint32_t instanceCount;
    const uint8_t *instances = t.Rows(3, instanceCount, size);
    uint32_t articleCount;
    const uint8_t *articles = t.Rows(4, articleCount, size);
    const uint8_t *spheres = t.Rows(5, rows, size);
    const uint8_t *verts = t.Rows(6, rows, size);
    const uint8_t *edges = t.Rows(7, rows, size);
    const uint8_t *surfaces = t.Rows(8, rows, size);
    const uint8_t *leftovers = t.Rows(9, rows, size);
    o.Lead(16);
    o.Bytes(header, 16);
    const uint32_t total = Get<uint32_t>(header, 0);
    const uint64_t carp = o.at;
    const uint32_t blobs = articleCount + 2;
    const uint32_t groups[8] = {0x43415250, 0x2A, 0, 1, 0x41727469, 0x12, blobs, 1};
    o.Bytes(groups, sizeof(groups));
    const uint64_t records = o.at;
    o.Zero(16 * blobs);
    o.Fill(0xAA, 16 * spare);
    auto align = [&]() { o.Fill(0xAA, static_cast<uint32_t>((16 - (o.at - carp) % 16) % 16)); };
    auto record = [&](uint32_t i, uint32_t tag, uint32_t bits, uint32_t count, uint32_t bytes) {
        if (!o.buffer)
            return;
        uint8_t *r = o.buffer + records + 16 * i;
        Put<uint32_t>(r, 0, tag);
        Put<uint32_t>(r, 4, bits | bytes << 8);
        Put<uint32_t>(r, 8, count);
        Put<uint32_t>(r, 12, static_cast<uint32_t>(o.at - (records + 16 * i)));
    };
    align();
    record(0, 0x4E616D65, 2, 0, nameSize);
    o.Bytes(name, nameLength);
    o.Zero(nameSize - nameLength);
    uint32_t sp = 0, vt = 0, ed = 0, sf = 0, lf = 0;
    for (uint32_t i = 0; i < articleCount; ++i)
    {
        // Our article row (assetc/world.py ARTICLE_T): size, flags, resolved, pad, intermediate, the counts.
        const uint8_t *a = articles + 24 * i;
        const uint32_t bytes = Get<uint32_t>(a, 0), nv = Get<uint32_t>(a, 12), nl = Get<uint32_t>(a, 20);
        const uint16_t ns = Get<uint16_t>(a, 10), ne = Get<uint16_t>(a, 16), nf = Get<uint16_t>(a, 18);
        align();
        record(i + 1, 0x63610000 | i, 3, 1, bytes);
        const uint64_t at = o.at;
        uint8_t head[16];
        Put<uint16_t>(head, 0, ns);
        Put<uint16_t>(head, 2, static_cast<uint16_t>(16 * ns + 8 * nv));
        Put<uint16_t>(head, 4, ne);
        Put<uint16_t>(head, 6, static_cast<uint16_t>(0x20 * ne));
        head[8] = a[6];
        head[9] = static_cast<uint8_t>(nf);
        Put<uint16_t>(head, 10, static_cast<uint16_t>(4 * nf));
        Put<uint16_t>(head, 12, Get<uint16_t>(a, 8));
        Put<uint16_t>(head, 14, Get<uint16_t>(a, 4));
        o.Bytes(head, 16);
        o.Bytes(spheres + 16 * sp, 16 * ns);
        o.Bytes(verts + 8 * vt, 8 * nv);
        o.Bytes(edges + 0x20 * ed, 0x20 * ne);
        o.Bytes(surfaces + 4 * sf, 4 * nf);
        sp += ns;
        vt += nv;
        ed += ne;
        sf += nf;
        if (nl)
            o.Bytes(leftovers + lf, nl);
        else
            o.Zero(static_cast<uint32_t>(bytes - (o.at - at)));
        lf += nl;
    }
    align();
    record(articleCount + 1, 0x63690000, 3, instanceCount, 0x40 * instanceCount);
    o.Bytes(instances, 0x40 * instanceCount);
    o.Fill(0xAA, static_cast<uint32_t>(total - (o.at - carp)));
    return o.at - carp == total;
}

/** mwworld's writers: one kind's chunk from its tables. */
bool RecordsBytes(Out &o, uint32_t kind, const Tables &t)
{
    const uint64_t outer = o.Begin(kind);
    uint32_t orderCount = 0, size;
    const uint8_t *order = t.Rows(0, orderCount, size);
    switch (kind)
    {
    case kScenery:
        for (uint32_t i = 0; i < orderCount; ++i)
        {
            const uint32_t id = Get<uint32_t>(order, 4 * i);
            const uint64_t c = o.Begin(id);
            switch (id)
            {
            case 0x34101:
                o.Lead(16);
                t.Put(o, 1);
                t.Put(o, 7);
                break;
            case 0x34103:
                o.Lead(16);
                t.Put(o, 2);
                t.Put(o, 8);
                break;
            case 0x34102:
                t.Put(o, 3);
                t.Put(o, 9);
                break;
            case 0x34105:
                t.Put(o, 4);
                t.Put(o, 10);
                break;
            case 0x34106:
                t.Put(o, 5);
                t.Put(o, 11);
                break;
            default:
                t.Put(o, 6);
                t.Put(o, 12);
                break;
            }
            o.End(c);
        }
        break;
    case kTriggers:
        for (uint32_t i = 0; i < orderCount; ++i)
        {
            const uint32_t id = Get<uint32_t>(order, 4 * i);
            const uint64_t c = o.Begin(id);
            if (id == 0x36001)
            {
                t.Put(o, 1);
                t.Put(o, 5);
            }
            else if (id == 0x36002)
            {
                o.Lead(16);
                t.Put(o, 2);
                t.Put(o, 3);
                t.Put(o, 6);
            }
            else
            {
                o.Lead(16);
                t.Put(o, 4);
                t.Put(o, 7);
            }
            o.End(c);
        }
        break;
    case kTextureAnims:
        for (uint32_t i = 0; i < orderCount; ++i)
        {
            const uint32_t id = Get<uint32_t>(order, 4 * i);
            const uint64_t c = o.Begin(id);
            const uint32_t table = id == 0x30300101 ? 1 : id == 0x30300102 ? 2 : 3;
            t.Put(o, table);
            t.Put(o, table + 3);
            o.End(c);
        }
        break;
    case kHeli:
    case kSmokeables:
        o.Lead(16);
        t.Put(o, 0);
        t.Put(o, 1);
        t.Put(o, 2);
        break;
    case kEmitters:
    {
        o.Lead(16);
        t.Put(o, 0);
        uint32_t libraries, triggers, lsize, tsize;
        const uint8_t *lib = t.Rows(1, libraries, lsize);
        const uint8_t *trig = t.Rows(2, triggers, tsize);
        for (uint32_t i = 0; i < libraries; ++i)
        {
            const uint16_t n = Get<uint16_t>(lib, 10);
            o.Bytes(lib, 0x50);
            o.Bytes(trig, 0x30 * n);
            lib += 0x50;
            trig += 0x30 * n;
        }
        t.Put(o, 3);
        break;
    }
    case kCollision:
        if (!CollisionBytes(o, t))
            return false;
        break;
    default:
        return false;
    }
    o.End(outer);
    return true;
}

bool RecordsPiece(Section *s, const uint8_t *piece)
{
    const uint32_t first = Get<uint32_t>(piece, 4);
    uint8_t *data;
    uint64_t size;
    bool ok = ReadAll(s->source, first + 1, data, size);
    if (ok)
    {
        const Tables t = {data, Get<uint32_t>(data, 4)};
        const uint32_t kind = Get<uint32_t>(data, 0);
        ok = kind == Get<uint32_t>(piece, 0) && Made(s, [&](Out &o) { return RecordsBytes(o, kind, t); });
    }
    Release(data);
    return ok;
}
} // namespace

Section *OpenSection(const Source &source, uint64_t position)
{
    uint8_t *data;
    uint64_t size;
    if (!ReadAll(source, 0, data, size))
    {
        Release(data);
        return nullptr;
    }
    Section *s = static_cast<Section *>(Allocate(sizeof(Section)));
    s->source = source;
    s->position = position;
    const uint32_t pieces = Get<uint32_t>(data, 4), ids = Get<uint32_t>(data, 8);
    const char *strings = reinterpret_cast<const char *>(data + kSectionHeader + kPiece * pieces + 8ull * ids);
    const uint32_t copiesAt = Get<uint32_t>(data, 32), copyCount = Get<uint32_t>(data, 36);
    uint32_t copied = 0;
    bool ok = true;
    for (uint32_t i = 0; ok && i < pieces; ++i)
    {
        const uint8_t *piece = data + kSectionHeader + kPiece * i;
        switch (Get<uint32_t>(piece, 0))
        {
        case kPiecePadding:
        {
            const uint32_t gap = Get<uint32_t>(piece, 20);
            ok = Made(s, [&](Out &o) {
                o.U32(0);
                o.U32(gap);
                return true;
            });
            AddSpan(s, kSpanZero, gap, 0, 0);
            break;
        }
        case kPieceTextures:
        {
            const uint32_t n = Get<uint32_t>(piece, 8);
            const bool have = copiesAt && copied + n <= copyCount;
            ok = TexturesPiece(s, piece, strings, have ? data + copiesAt + kTextureHeader * copied : nullptr);
            copied += n;
            break;
        }
        case kPieceSolids:
            ok = SolidsPiece(s, piece, nullptr, strings);
            break;
        default:
            ok = RecordsPiece(s, piece);
            break;
        }
    }
    Release(data);
    if (!ok)
    {
        CloseSection(s);
        return nullptr;
    }
    return s;
}

uint64_t SectionSize(const Section *s)
{
    return s->size;
}

uint32_t ReadSection(Section *s, uint64_t offset, void *buffer, uint32_t bytes)
{
    uint8_t *out = static_cast<uint8_t *>(buffer);
    uint32_t done = 0;
    uint32_t lo = 0, hi = s->spanCount;
    while (lo < hi)
    {
        const uint32_t mid = (lo + hi) / 2;
        if (s->spans[mid].start + s->spans[mid].size - s->position <= offset)
            lo = mid + 1;
        else
            hi = mid;
    }
    for (uint32_t i = lo; i < s->spanCount && done < bytes; ++i)
    {
        const Span &sp = s->spans[i];
        const uint64_t within = offset + done - (sp.start - s->position);
        uint64_t take = sp.size - within;
        if (take > bytes - done)
            take = bytes - done;
        if (sp.kind == kSpanZero)
            memset(out + done, 0, static_cast<size_t>(take));
        else if (sp.kind == kSpanArena)
            memcpy(out + done, s->arena + sp.from + within, static_cast<size_t>(take));
        else if (!s->source.read(s->source.context, sp.item, sp.from + within, out + done, static_cast<uint32_t>(take)))
            break;
        done += static_cast<uint32_t>(take);
    }
    return done;
}

void CloseSection(Section *s)
{
    if (!s)
        return;
    Release(s->arena);
    Release(s->spans);
    Release(s);
}

} // namespace nfs::packbridge

#ifdef PACK_BRIDGE_TOOLS
namespace
{
struct Memory
{
    const uint8_t *const *items;
    const size_t *sizes;
};

uint64_t MemorySize(void *context, uint32_t resource)
{
    return static_cast<Memory *>(context)->sizes[resource];
}

bool MemoryRead(void *context, uint32_t resource, uint64_t offset, void *out, uint32_t bytes)
{
    Memory *m = static_cast<Memory *>(context);
    if (offset + bytes > m->sizes[resource])
        return false;
    memcpy(out, m->items[resource] + offset, bytes);
    return true;
}
} // namespace

/** The tools' entry: resource 0 the set, 1..count its items. Writes the file (out null: its size only). */
extern "C" __declspec(dllexport) uint64_t pack_bridge_build(const uint8_t *const *resources, const size_t *sizes,
                                                            uint32_t count, uint8_t *out, uint64_t capacity,
                                                            uint64_t offset, uint32_t chunk)
{
    Memory memory = {resources, sizes};
    nfs::packbridge::Source source = {&memory, &MemorySize, &MemoryRead};
    uint32_t *items = static_cast<uint32_t *>(HeapAlloc(GetProcessHeap(), 0, 4 * (count ? count : 1)));
    for (uint32_t i = 0; i < count; ++i)
        items[i] = i + 1;
    nfs::packbridge::File *f = nfs::packbridge::Open(source, resources[0], sizes[0], items, count);
    HeapFree(GetProcessHeap(), 0, items);
    if (!f)
        return 0;
    const uint64_t size = nfs::packbridge::Size(f);
    if (out)
    {
        // Read in pieces of `chunk` bytes from `offset`, as a streaming loader would.
        uint64_t at = 0;
        while (at < capacity && at < size)
        {
            const uint32_t n = static_cast<uint32_t>(capacity - at < chunk ? capacity - at : chunk);
            const uint32_t got = nfs::packbridge::Read(f, offset + at, out + at, n);
            if (!got)
                break;
            at += got;
        }
    }
    nfs::packbridge::Close(f);
    return size;
}

/** The tools' world entry: resource 0 a section, 1..count its ids' resources in order, placed at `position` in the
 * stream. Writes the section (out null: its size only), read in pieces of `chunk` bytes. */
extern "C" __declspec(dllexport) uint64_t pack_world_section(const uint8_t *const *resources, const size_t *sizes,
                                                             uint64_t position, uint8_t *out, uint64_t capacity,
                                                             uint32_t chunk)
{
    Memory memory = {resources, sizes};
    nfs::packbridge::Source source = {&memory, &MemorySize, &MemoryRead};
    nfs::packbridge::Section *s = nfs::packbridge::OpenSection(source, position);
    if (!s)
        return 0;
    const uint64_t size = nfs::packbridge::SectionSize(s);
    if (out)
    {
        uint64_t at = 0;
        while (at < capacity && at < size)
        {
            const uint32_t n = static_cast<uint32_t>(capacity - at < chunk ? capacity - at : chunk);
            const uint32_t got = nfs::packbridge::ReadSection(s, at, out + at, n);
            if (!got)
                break;
            at += got;
        }
    }
    nfs::packbridge::CloseSection(s);
    return size;
}
#endif
