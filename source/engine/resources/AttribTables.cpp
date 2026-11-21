/**
 * The attribute vaults laid out in memory from our tables (docs/EA_FORMATS.md phase 2 step 2; the tables:
 * tools/assetc/attribtables.py). A vault's .bin and .vlt images are made the way assetc.vaultwrite makes them: the
 * skeleton (dependencies, database, classes and their field definitions), then each collection as
 * vltadd.Vault.add_collection adds it (its record in DatN, its layout and values in the .bin, strings once each, the
 * pointer fixups), so Attrib's own loader (AddDepFile, AddVault) reads them as before. The tools build this file as a
 * DLL (ATTRIB_TABLES_TOOLS) and compare its images with vaultwrite's byte for byte.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>

#include "engine/resources/attribtables.h"

namespace
{
void *MemGrow(void *p, size_t n)
{
    return p ? HeapReAlloc(GetProcessHeap(), 0, p, n) : HeapAlloc(GetProcessHeap(), 0, n);
}

void *Cleared(size_t count, size_t size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * size ? count * size : 1);
}

void Release(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

const uint32_t kCollection = 0x8E112EB7, kClass = 0x5E970CBC, kDatabase = 0xCBBC628F;

struct Buf
{
    uint8_t *data;
    size_t size;
    size_t cap;

    bool Reserve(size_t more)
    {
        if (size + more <= cap)
            return true;
        size_t n = cap ? cap * 2 : 4096;
        while (n < size + more)
            n *= 2;
        uint8_t *p = static_cast<uint8_t *>(MemGrow(data, n));
        if (!p)
            return false;
        data = p;
        cap = n;
        return true;
    }
    bool Bytes(const void *p, size_t n)
    {
        if (!Reserve(n))
            return false;
        if (n)
            memcpy(data + size, p, n);
        size += n;
        return true;
    }
    bool Zero(size_t n)
    {
        if (!Reserve(n))
            return false;
        memset(data + size, 0, n);
        size += n;
        return true;
    }
    bool U32(uint32_t v)
    {
        return Bytes(&v, 4);
    }
    bool U16(uint16_t v)
    {
        return Bytes(&v, 2);
    }
    bool U8(uint8_t v)
    {
        return Bytes(&v, 1);
    }
    bool Align(size_t n)
    {
        return Zero((n - size % n) % n);
    }
    void Free()
    {
        Release(data);
        data = nullptr;
        size = cap = 0;
    }
};

struct Fixup
{
    uint32_t offset;
    uint16_t kind;
    uint16_t index;
    uint32_t value;
    /** A string's text (resolved to its place when the .vlt is written), or null. */
    const char *text;
    uint16_t textLength;
};

struct Fixups
{
    Fixup *items;
    size_t count;
    size_t cap;

    bool Add(const Fixup &f)
    {
        if (count == cap)
        {
            const size_t n = cap ? cap * 2 : 256;
            Fixup *p = static_cast<Fixup *>(MemGrow(items, n * sizeof(Fixup)));
            if (!p)
                return false;
            items = p;
            cap = n;
        }
        items[count++] = f;
        return true;
    }
    void Free()
    {
        Release(items);
        items = nullptr;
        count = cap = 0;
    }
};

/** The table being read. */
struct Reader
{
    const uint8_t *p;
    const uint8_t *end;
    bool ok;

    template <typename T> T Get()
    {
        T v{};
        if (p + sizeof(T) > end)
        {
            ok = false;
            return v;
        }
        memcpy(&v, p, sizeof(T));
        p += sizeof(T);
        return v;
    }
    const char *Str(uint16_t &length)
    {
        length = Get<uint16_t>();
        if (p + length > end)
        {
            ok = false;
            return "";
        }
        const char *s = reinterpret_cast<const char *>(p);
        p += length;
        return s;
    }
    void Skip(size_t n)
    {
        if (p + n > end)
            ok = false;
        else
            p += n;
    }
};

/** A string placed in the .bin, by its text. */
struct Placed
{
    const char *text;
    uint16_t length;
    uint32_t at;
};

struct Strings
{
    Placed *items;
    size_t count;
    size_t cap;
    /** An open-addressing index into items, by the text's FNV-1a. */
    uint32_t *slots;
    size_t slotCount;

    static uint32_t Hash(const char *s, uint16_t n)
    {
        uint32_t h = 2166136261u;
        for (uint16_t i = 0; i < n; ++i)
            h = (h ^ static_cast<uint8_t>(s[i])) * 16777619u;
        return h;
    }
    bool Grow()
    {
        const size_t n = slotCount ? slotCount * 2 : 1024;
        uint32_t *s = static_cast<uint32_t *>(Cleared(n, sizeof(uint32_t)));
        if (!s)
            return false;
        for (size_t i = 0; i < count; ++i)
        {
            size_t at = Hash(items[i].text, items[i].length) & (n - 1);
            while (s[at])
                at = (at + 1) & (n - 1);
            s[at] = static_cast<uint32_t>(i + 1);
        }
        Release(slots);
        slots = s;
        slotCount = n;
        return true;
    }
    Placed *Find(const char *text, uint16_t n)
    {
        if (!slotCount)
            return nullptr;
        size_t at = Hash(text, n) & (slotCount - 1);
        while (slots[at])
        {
            Placed &p = items[slots[at] - 1];
            if (p.length == n && !memcmp(p.text, text, n))
                return &p;
            at = (at + 1) & (slotCount - 1);
        }
        return nullptr;
    }
    bool Add(const char *text, uint16_t n, uint32_t at)
    {
        if ((count + 1) * 2 > slotCount && !Grow())
            return false;
        if (count == cap)
        {
            const size_t c = cap ? cap * 2 : 512;
            Placed *p = static_cast<Placed *>(MemGrow(items, c * sizeof(Placed)));
            if (!p)
                return false;
            items = p;
            cap = c;
        }
        items[count] = {text, n, at};
        size_t s = Hash(text, n) & (slotCount - 1);
        while (slots[s])
            s = (s + 1) & (slotCount - 1);
        slots[s] = static_cast<uint32_t>(++count);
        return true;
    }
    void Free()
    {
        Release(items);
        Release(slots);
        items = nullptr;
        slots = nullptr;
        count = cap = slotCount = 0;
    }
};

struct Vault
{
    Buf bin;
    Buf dat;
    Buf exports;
    uint32_t exportCount;
    Fixups binFixups;
    Fixups datFixups;
    Strings strings;
    uint32_t datOffset;
    Buf head;
    /** The skeleton's .bin length: vltadd aligns new .bin data relative to it. */
    size_t binBase;
};

bool Chunk(Buf &out, const char *id, const uint8_t *payload, size_t size)
{
    const size_t pad = (16 - (size + 8) % 16) % 16;
    return out.U8(static_cast<uint8_t>(id[3])) && out.U8(static_cast<uint8_t>(id[2])) &&
           out.U8(static_cast<uint8_t>(id[1])) && out.U8(static_cast<uint8_t>(id[0])) &&
           out.U32(static_cast<uint32_t>(size + pad + 8)) && out.Bytes(payload, size) && out.Zero(pad);
}

bool PlaceString(Vault &v, const char *text, uint16_t n, uint32_t &at)
{
    if (Placed *p = v.strings.Find(text, n))
    {
        at = p->at;
        return true;
    }
    at = static_cast<uint32_t>(v.bin.size);
    return v.strings.Add(text, n, at) && v.bin.Bytes(text, n) && v.bin.U8(0);
}

/** vltadd.Vault.place: a value read from the table into the .bin at `align`; its place in `at`. */
bool PlaceValue(Vault &v, Reader &r, size_t align, uint32_t &at)
{
    const uint32_t bytes = r.Get<uint32_t>();
    const uint8_t *data = r.p;
    r.Skip(bytes);
    const uint32_t count = r.Get<uint32_t>();
    if (!r.ok)
        return false;
    // vltadd aligns the new .bin data relative to where it started, the skeleton's end.
    if (!v.bin.Zero((align - (v.bin.size - v.binBase) % align) % align))
        return false;
    at = static_cast<uint32_t>(v.bin.size);
    if (!v.bin.Bytes(data, bytes))
        return false;
    for (uint32_t i = 0; i < count && r.ok; ++i)
    {
        const uint32_t offset = r.Get<uint32_t>();
        const uint8_t kind = r.Get<uint8_t>();
        if (kind == 1)
        {
            if (!v.binFixups.Add({at + offset, 1, 0, 0, nullptr, 0}))
                return false;
        }
        else if (kind == 2)
        {
            uint16_t n;
            const char *text = r.Str(n);
            uint32_t where;
            if (!r.ok || !PlaceString(v, text, n, where) || !v.binFixups.Add({at + offset, 3, 1, where, nullptr, 0}))
                return false;
        }
        else if (kind == 3)
        {
            const uint32_t n = r.Get<uint32_t>();
            const uint8_t *blob = r.p;
            r.Skip(n);
            if (!r.ok || !v.bin.Zero((16 - (v.bin.size - v.binBase) % 16) % 16))
                return false;
            const uint32_t where = static_cast<uint32_t>(v.bin.size);
            if (!v.bin.Bytes(blob, n) || !v.binFixups.Add({at + offset, 3, 1, where, nullptr, 0}))
                return false;
        }
        else
            return false;
    }
    return r.ok;
}

/** The skeleton: DepN, StrN, the database and class records in DatN, the .bin's string chunk, types and definitions. */
bool Skeleton(Vault &v, Reader &r)
{
    const uint32_t deps = r.Get<uint32_t>();
    Buf dep{}, names{};
    bool ok = dep.U32(deps);
    const uint8_t *depStart = r.p;
    uint32_t at = 0;
    for (int pass = 0; pass < 3 && ok; ++pass)
    {
        r.p = depStart;
        at = 0;
        for (uint32_t i = 0; i < deps && r.ok; ++i)
        {
            uint16_t n;
            const char *name = r.Str(n);
            const uint32_t hash = r.Get<uint32_t>();
            if (pass == 0)
                ok = ok && dep.U32(hash);
            else if (pass == 1)
                ok = ok && dep.U32(at);
            else
                ok = ok && names.Bytes(name, n) && names.U8(0);
            at += n + 1u;
        }
    }
    ok = ok && dep.Bytes(names.data, names.size);
    names.Free();
    const uint8_t zero8[8] = {};
    ok = ok && Chunk(v.head, "DepN", dep.data, dep.size) && Chunk(v.head, "StrN", zero8, 8);
    dep.Free();
    v.datOffset = static_cast<uint32_t>(v.head.size + 8);
    const uint8_t str[16] = {'E', 'r', 't', 'S', 16, 0, 0, 0};
    ok = ok && v.bin.Bytes(str, 16);
    if (r.Get<uint8_t>())
    {
        const uint32_t exportKey = r.Get<uint32_t>(), w0 = r.Get<uint32_t>(), w1 = r.Get<uint32_t>();
        const uint32_t types = r.Get<uint32_t>();
        const uint32_t namesAt = static_cast<uint32_t>(v.bin.size);
        const uint32_t recordAt = v.datOffset + static_cast<uint32_t>(v.dat.size);
        const size_t start = v.dat.size;
        ok = ok && v.dat.U32(w0) && v.dat.U32(w1) && v.dat.U32(types) && v.dat.U32(0);
        for (uint32_t i = 0; i < types && r.ok && ok; ++i)
        {
            uint16_t n;
            const char *name = r.Str(n);
            const uint32_t size = r.Get<uint32_t>();
            ok = v.bin.Bytes(name, n) && v.bin.U8(0) && v.dat.U32(size);
        }
        ok = ok && v.exports.U32(exportKey) && v.exports.U32(kDatabase) && v.exports.U32(0) &&
             v.exports.U32(static_cast<uint32_t>(v.dat.size - start)) && v.exports.U32(recordAt);
        ++v.exportCount;
        ok = ok && v.datFixups.Add({recordAt + 12, 3, 1, namesAt, nullptr, 0});
    }
    const uint32_t classes = r.Get<uint32_t>();
    for (uint32_t c = 0; c < classes && r.ok && ok; ++c)
    {
        uint16_t n;
        r.Str(n);
        const uint32_t key = r.Get<uint32_t>(), exportKey = r.Get<uint32_t>(), reserve = r.Get<uint32_t>();
        const uint32_t layout = r.Get<uint32_t>(), tail0 = r.Get<uint32_t>(), tail1 = r.Get<uint32_t>();
        const uint32_t fields = r.Get<uint32_t>();
        ok = v.bin.Align(16);
        const uint32_t defsAt = static_cast<uint32_t>(v.bin.size);
        for (uint32_t f = 0; f < fields && r.ok && ok; ++f)
        {
            r.Str(n);
            const uint32_t fieldKey = r.Get<uint32_t>();
            r.Str(n);
            const uint32_t typeKey = r.Get<uint32_t>();
            const uint16_t offset = r.Get<uint16_t>(), size = r.Get<uint16_t>(), maxCount = r.Get<uint16_t>();
            const uint8_t flags = r.Get<uint8_t>(), align = r.Get<uint8_t>();
            ok = v.bin.U32(fieldKey) && v.bin.U32(typeKey) && v.bin.U16(offset) && v.bin.U16(size) &&
                 v.bin.U16(maxCount) && v.bin.U8(flags) && v.bin.U8(align);
        }
        ok = ok && v.dat.Align(4);
        const uint32_t recordAt = v.datOffset + static_cast<uint32_t>(v.dat.size);
        ok = ok && v.dat.U32(key) && v.dat.U32(reserve) && v.dat.U32(fields) && v.dat.U32(0) && v.dat.U32(layout) &&
             v.dat.U32(tail0) && v.dat.U32(tail1);
        ok = ok && v.exports.U32(exportKey) && v.exports.U32(kClass) && v.exports.U32(0) && v.exports.U32(28) &&
             v.exports.U32(recordAt);
        ++v.exportCount;
        ok = ok && v.datFixups.Add({recordAt + 12, 3, 1, defsAt, nullptr, 0});
    }
    // The skeleton's DatN is padded as a chunk before collections are added after it.
    if (ok)
    {
        const size_t pad = (16 - (v.dat.size + 8) % 16) % 16;
        ok = v.dat.Zero(pad);
    }
    return ok && r.ok;
}

/** vltadd.Vault.add_collection, from the table. */
bool Collection(Vault &v, Reader &r)
{
    const uint32_t exportKey = r.Get<uint32_t>(), cls = r.Get<uint32_t>(), coll = r.Get<uint32_t>();
    const uint32_t parent = r.Get<uint32_t>();
    uint16_t n;
    r.Str(n);
    r.Str(n);
    r.Str(n);
    const uint8_t hasLayout = r.Get<uint8_t>();
    if (!r.ok)
        return false;
    // The entries' type keys come after the header; collect them first (the table is read twice).
    const uint8_t *afterHeader = r.p;
    if (hasLayout)
    {
        const uint32_t bytes = r.Get<uint32_t>();
        r.Skip(bytes);
        const uint32_t count = r.Get<uint32_t>();
        for (uint32_t i = 0; i < count && r.ok; ++i)
        {
            r.Get<uint32_t>();
            const uint8_t kind = r.Get<uint8_t>();
            if (kind == 2)
                r.Str(n);
            else if (kind == 3)
                r.Skip(r.Get<uint32_t>());
        }
    }
    const uint32_t entries = r.Get<uint32_t>();
    uint32_t *types = static_cast<uint32_t *>(MemGrow(nullptr, sizeof(uint32_t) * (entries ? entries : 1)));
    uint32_t typeCount = 0;
    const uint8_t *entriesAt = r.p;
    for (uint32_t i = 0; i < entries && r.ok; ++i)
    {
        r.Get<uint32_t>();
        const uint32_t t = r.Get<uint32_t>();
        r.Get<uint8_t>();
        uint32_t k = 0;
        while (k < typeCount && types[k] != t)
            ++k;
        if (k == typeCount)
            types[typeCount++] = t;
        const uint32_t bytes = r.Get<uint32_t>();
        r.Skip(bytes);
        const uint32_t count = r.Get<uint32_t>();
        for (uint32_t j = 0; j < count && r.ok; ++j)
        {
            r.Get<uint32_t>();
            const uint8_t kind = r.Get<uint8_t>();
            if (kind == 2)
                r.Str(n);
            else if (kind == 3)
                r.Skip(r.Get<uint32_t>());
        }
    }
    const uint8_t *end = r.p;
    bool ok = r.ok;
    Buf rec{};
    ok = ok && rec.U32(coll) && rec.U32(cls) && rec.U32(parent) && rec.U32(entries) && rec.U32(0) &&
         rec.U32(entries) && rec.U32(typeCount) && rec.U32(0);
    for (uint32_t k = 0; k < typeCount && ok; ++k)
        ok = rec.U32(types[k]);
    ok = ok && v.dat.Align(4);
    const uint32_t at = v.datOffset + static_cast<uint32_t>(v.dat.size);
    Fixups local{};
    if (ok && hasLayout)
    {
        r.p = afterHeader;
        uint32_t where;
        ok = PlaceValue(v, r, 16, where) && local.Add({at + 0x1C, 3, 1, where, nullptr, 0});
    }
    r.p = entriesAt;
    for (uint32_t i = 0; i < entries && ok && r.ok; ++i)
    {
        const uint32_t fk = r.Get<uint32_t>(), t = r.Get<uint32_t>();
        const uint8_t array = r.Get<uint8_t>();
        uint32_t k = 0;
        while (types[k] != t)
            ++k;
        const uint32_t e = static_cast<uint32_t>(rec.size);
        uint8_t flags = array ? 2 : 0;
        const uint32_t bytes = r.Get<uint32_t>();
        const uint8_t *data = r.p;
        r.Skip(bytes);
        const uint32_t count = r.Get<uint32_t>();
        if (!array && bytes <= 4 && !count)
        {
            uint8_t value[4] = {};
            memcpy(value, data, bytes);
            ok = rec.U32(fk) && rec.Bytes(value, 4);
            flags |= 0x20;
        }
        else
        {
            r.p = data - 4;
            uint32_t where;
            ok = rec.U32(fk) && rec.U32(0) && PlaceValue(v, r, array ? 16 : 4, where) &&
                 local.Add({at + e + 4, 3, 1, where, nullptr, 0});
        }
        ok = ok && rec.U16(static_cast<uint16_t>(k)) && rec.U8(flags) && rec.U8(0);
    }
    r.p = end;
    Release(types);
    ok = ok && v.dat.Bytes(rec.data, rec.size);
    for (size_t i = 0; ok && i < local.count; ++i)
        ok = v.datFixups.Add(local.items[i]);
    ok = ok && v.exports.U32(exportKey) && v.exports.U32(kCollection) && v.exports.U32(0) &&
         v.exports.U32(static_cast<uint32_t>(rec.size)) && v.exports.U32(at);
    ++v.exportCount;
    rec.Free();
    local.Free();
    return ok;
}

bool WritePtr(Buf &ptr, const Fixup &f)
{
    return ptr.U32(f.offset) && ptr.U16(f.kind) && ptr.U16(f.index) && ptr.U32(f.value);
}

void FreeVault(Vault &v)
{
    v.bin.Free();
    v.dat.Free();
    v.exports.Free();
    v.binFixups.Free();
    v.datFixups.Free();
    v.strings.Free();
    v.head.Free();
}

/** Skips a vault's record in the table without laying it out. */
bool SkipVault(Reader &r)
{
    uint16_t n;
    const uint32_t deps = r.Get<uint32_t>();
    for (uint32_t i = 0; i < deps && r.ok; ++i)
    {
        r.Str(n);
        r.Get<uint32_t>();
    }
    if (r.Get<uint8_t>())
    {
        r.Skip(12);
        const uint32_t types = r.Get<uint32_t>();
        for (uint32_t i = 0; i < types && r.ok; ++i)
        {
            r.Str(n);
            r.Get<uint32_t>();
        }
    }
    const uint32_t classes = r.Get<uint32_t>();
    for (uint32_t c = 0; c < classes && r.ok; ++c)
    {
        r.Str(n);
        r.Skip(24);
        const uint32_t fields = r.Get<uint32_t>();
        for (uint32_t f = 0; f < fields && r.ok; ++f)
        {
            r.Str(n);
            r.Get<uint32_t>();
            r.Str(n);
            r.Skip(4 + 2 + 2 + 2 + 1 + 1);
        }
    }
    const uint32_t collections = r.Get<uint32_t>();
    for (uint32_t c = 0; c < collections && r.ok; ++c)
    {
        r.Skip(16);
        r.Str(n);
        r.Str(n);
        r.Str(n);
        auto value = [&]() {
            r.Skip(r.Get<uint32_t>());
            const uint32_t count = r.Get<uint32_t>();
            for (uint32_t j = 0; j < count && r.ok; ++j)
            {
                r.Get<uint32_t>();
                const uint8_t kind = r.Get<uint8_t>();
                if (kind == 2)
                    r.Str(n);
                else if (kind == 3)
                    r.Skip(r.Get<uint32_t>());
            }
        };
        if (r.Get<uint8_t>())
            value();
        const uint32_t entries = r.Get<uint32_t>();
        for (uint32_t i = 0; i < entries && r.ok; ++i)
        {
            r.Skip(9);
            value();
        }
    }
    return r.ok;
}
} // namespace

namespace
{
/** One vault from the reader (its name and file read already). */
bool LayoutVault(Reader &r, uint8_t *&bin, size_t &binSize, uint8_t *&vlt, size_t &vltSize)
{
    Vault v{};
    bool ok = Skeleton(v, r);
    v.binBase = v.bin.size;
    const uint32_t collections = r.Get<uint32_t>();
    for (uint32_t c = 0; c < collections && ok && r.ok; ++c)
        ok = Collection(v, r);
    Buf out{}, ptr{};
    ok = ok && r.ok;
    // PtrN: the .bin's pointers, then the .vlt's.
    ok = ok && WritePtr(ptr, {0, 2, 1, 0, nullptr, 0});
    for (size_t k = 0; ok && k < v.binFixups.count; ++k)
        ok = WritePtr(ptr, v.binFixups.items[k]);
    ok = ok && WritePtr(ptr, {0, 2, 0, 0, nullptr, 0});
    for (size_t k = 0; ok && k < v.datFixups.count; ++k)
        ok = WritePtr(ptr, v.datFixups.items[k]);
    ok = ok && WritePtr(ptr, {0, 0, 0, 0, nullptr, 0});
    Buf exp{};
    ok = ok && exp.U32(v.exportCount) && exp.Bytes(v.exports.data, v.exports.size);
    ok = ok && out.Bytes(v.head.data, v.head.size) && Chunk(out, "DatN", v.dat.data, v.dat.size) &&
         Chunk(out, "ExpN", exp.data, exp.size) && Chunk(out, "PtrN", ptr.data, ptr.size);
    exp.Free();
    ptr.Free();
    if (!ok)
    {
        out.Free();
        FreeVault(v);
        return false;
    }
    bin = v.bin.data;
    binSize = v.bin.size;
    v.bin.data = nullptr;
    vlt = out.data;
    vltSize = out.size;
    FreeVault(v);
    return true;
}

bool Open(Reader &r, const uint8_t *table, size_t size, uint32_t &vaults)
{
    r = Reader{table, table + size, true};
    if (size < 12 || memcmp(table, "ATBL", 4))
        return false;
    r.p += 4;
    if (r.Get<uint32_t>() != 1)
        return false;
    vaults = r.Get<uint32_t>();
    return r.ok;
}

bool Same(const char *a, uint16_t n, const char *b)
{
    return strlen(b) == n && !memcmp(a, b, n);
}
} // namespace

bool nfs::attribtables::Layout(const uint8_t *table, size_t size, const char *vaultName, uint8_t *&bin, size_t &binSize,
                               uint8_t *&vlt, size_t &vltSize)
{
    Reader r{};
    uint32_t vaults = 0;
    if (!Open(r, table, size, vaults))
        return false;
    for (uint32_t i = 0; i < vaults && r.ok; ++i)
    {
        uint16_t n, fn;
        const char *name = r.Str(n);
        r.Str(fn);
        if (!Same(name, n, vaultName))
        {
            if (!SkipVault(r))
                return false;
            continue;
        }
        return LayoutVault(r, bin, binSize, vlt, vltSize);
    }
    return false;
}

bool nfs::attribtables::LayoutPack(const uint8_t *table, size_t size, const char *fileName, size_t lead, uint8_t *&pack,
                                   size_t &packSize)
{
    Reader r{};
    uint32_t vaults = 0;
    if (!Open(r, table, size, vaults))
        return false;
    struct Laid
    {
        const char *name;
        uint16_t length;
        uint8_t *bin, *vlt;
        size_t binSize, vltSize;
    };
    Laid *laid = static_cast<Laid *>(Cleared(vaults ? vaults : 1, sizeof(Laid)));
    uint32_t count = 0;
    bool ok = laid != nullptr;
    for (uint32_t i = 0; i < vaults && r.ok && ok; ++i)
    {
        uint16_t n, fn;
        const char *name = r.Str(n);
        const char *file = r.Str(fn);
        if (!Same(file, fn, fileName))
        {
            ok = SkipVault(r);
            continue;
        }
        Laid &l = laid[count++];
        l.name = name;
        l.length = n;
        ok = LayoutVault(r, l.bin, l.binSize, l.vlt, l.vltSize);
    }
    // vltadd.VPak.bytes: header, a row per vault, the names at the next 64, each .bin and .vlt at 0x80, the end at 0x40.
    Buf out{};
    ok = ok && count && out.Zero(lead);
    const size_t base = out.size;
    size_t namesSize = 0;
    for (uint32_t i = 0; i < count; ++i)
        namesSize += laid[i].length + 1u;
    const size_t namesAt = (16 + 20 * count + 63) / 64 * 64;
    ok = ok && out.Bytes("VPAK", 4) && out.U32(count) && out.U32(static_cast<uint32_t>(namesAt)) &&
         out.U32(static_cast<uint32_t>(namesSize)) && out.Zero(20 * count) && out.Zero(namesAt - (out.size - base));
    for (uint32_t i = 0; ok && i < count; ++i)
        ok = out.Bytes(laid[i].name, laid[i].length) && out.U8(0);
    uint32_t nameAt = 0;
    for (uint32_t i = 0; ok && i < count; ++i)
    {
        ok = out.Zero((0x80 - (out.size - base) % 0x80) % 0x80);
        const uint32_t binAt = static_cast<uint32_t>(out.size - base);
        ok = ok && out.Bytes(laid[i].bin, laid[i].binSize) && out.Zero((0x80 - (out.size - base) % 0x80) % 0x80);
        const uint32_t vltAt = static_cast<uint32_t>(out.size - base);
        ok = ok && out.Bytes(laid[i].vlt, laid[i].vltSize);
        if (ok)
        {
            uint32_t row[5] = {nameAt, static_cast<uint32_t>(laid[i].binSize), static_cast<uint32_t>(laid[i].vltSize),
                               binAt, vltAt};
            memcpy(out.data + base + 16 + 20 * i, row, sizeof(row));
        }
        nameAt += laid[i].length + 1u;
    }
    ok = ok && out.Zero((0x40 - (out.size - base) % 0x40) % 0x40);
    for (uint32_t i = 0; i < count; ++i)
    {
        Release(laid[i].bin);
        Release(laid[i].vlt);
    }
    Release(laid);
    if (!ok)
    {
        out.Free();
        return false;
    }
    pack = out.data;
    packSize = out.size;
    return true;
}

void nfs::attribtables::FreeLayout(uint8_t *p)
{
    Release(p);
}

#ifdef ATTRIB_TABLES_TOOLS
extern "C" __declspec(dllexport) int AttribTablesPack(const uint8_t *table, size_t size, const char *file, uint8_t *out,
                                                      size_t cap, size_t *outSize)
{
    uint8_t *p = nullptr;
    size_t n = 0;
    if (!nfs::attribtables::LayoutPack(table, size, file, 0, p, n) || n > cap)
        return 0;
    memcpy(out, p, n);
    *outSize = n;
    Release(p);
    return 1;
}

extern "C" __declspec(dllexport) int AttribTablesLayout(const uint8_t *table, size_t size, const char *name, uint8_t *bin,
                                                        size_t binCap, size_t *binSize, uint8_t *vlt, size_t vltCap,
                                                        size_t *vltSize)
{
    uint8_t *b = nullptr, *v = nullptr;
    size_t nb = 0, nv = 0;
    if (!nfs::attribtables::Layout(table, size, name, b, nb, v, nv) || nb > binCap || nv > vltCap)
        return 0;
    memcpy(bin, b, nb);
    memcpy(vlt, v, nv);
    *binSize = nb;
    *vltSize = nv;
    Release(b);
    Release(v);
    return 1;
}
#endif
