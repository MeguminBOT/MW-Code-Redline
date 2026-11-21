/**
 * The game's strings from our tables (docs/EA_FORMATS.md phase 5): when a language file (LANGUAGES\<Language>.BIN, now
 * its fonts only) begins loading, LANGUAGES\<Language>.STRINGS (tools/assetc/localization.py strings_table) gives the
 * string lookup what LoaderLanguage (0x57e960) took from the file's 0x39000 chunk: the character table
 * (pWideCharHistogram: a count, then 3072 UTF-16 units), the records in hash order (RecordTable: hash, string) and
 * their count (NumStringRecords), the strings (PackedStringTable). The language's fonts (LANGUAGES\<Language>.FONTS
 * names them, each LANGUAGES\FONTS\<FONT>.FONT: tools/assetc/fonts.py font_table) are made into the FEngFont chunk the
 * game's LoaderFEngFont (0x591eb0) reads: the font's and texture's names, then the RealFont FNTF record (header,
 * glyphs, kerning table). The file's unloading takes them out again, as UnloaderLanguage and UnloaderFEngFont did.
 */
#include <string.h>
#include <windows.h>

#include "game/gen/api.h"
#include "legacy/link.h"
#include "core/log.h"
#include "engine/resources/pack.h"
#include "engine/resources/strings.h"

namespace
{
int &NumStrings()
{
    return *reinterpret_cast<int *>(0x91cf78);
}
char *&StringTable()
{
    return *reinterpret_cast<char **>(0x91cf7c);
}
char *&Records()
{
    return *reinterpret_cast<char **>(0x91cf80);
}
char *&Histogram()
{
    return *reinterpret_cast<char **>(0x91cf84);
}

const unsigned int kTableUnits = 3072;
const unsigned int kHistogram = 4 + 2 * kTableUnits;

ResourceFile *g_file;
char *g_block;
const int kMaxFonts = 16;
char *g_fonts[kMaxFonts];
int g_fontCount;

struct Reader
{
    const uint8_t *p, *end;
    bool ok;
    template <typename T> T Get()
    {
        T v{};
        if (p + sizeof(T) > end)
            ok = false;
        else
            memcpy(&v, p, sizeof(T)), p += sizeof(T);
        return v;
    }
    const uint8_t *Take(size_t n)
    {
        const uint8_t *at = p;
        if (p + n > end)
            ok = false;
        else
            p += n;
        return at;
    }
};

/** The FEngFont chunk (0x30201: names, then the FNTF record) of a compiled font; null when it does not read. */
char *FontChunk(const uint8_t *data, uint32_t size)
{
    Reader r{data, data + size, true};
    if (size < 8 || memcmp(data, "FONT", 4))
        return nullptr;
    r.Take(4);
    if (r.Get<uint32_t>() != 1)
        return nullptr;
    const uint16_t nameLength = r.Get<uint16_t>();
    const uint8_t *name = r.Take(nameLength);
    const uint16_t textureLength = r.Get<uint16_t>();
    const uint8_t *texture = r.Take(textureLength);
    uint32_t header[32];
    memcpy(header, r.Take(sizeof(header)), r.ok ? sizeof(header) : 0);
    const uint32_t glyphs = r.Get<uint32_t>();
    const uint8_t *glyphData = r.Take(14 * static_cast<size_t>(glyphs));
    const uint32_t pairs = r.Get<uint32_t>();
    const uint8_t *pairData = r.Take(4 * static_cast<size_t>(pairs));
    const uint32_t padding = r.Get<uint32_t>(), recordPadding = r.Get<uint32_t>(), trailerLength = r.Get<uint32_t>();
    const uint8_t *trailer = r.Take(trailerLength);
    const uint32_t namesLength = r.Get<uint32_t>();
    const uint8_t *names = r.Take(namesLength);
    if (!r.ok || nameLength >= 0x100 || textureLength >= 0x100 || (namesLength && namesLength != 0x200))
        return nullptr;
    const uint32_t glyphsAt = 0x80, kernAt = pairs ? glyphsAt + 16 * glyphs : 0;
    const uint32_t body = glyphsAt + 16 * glyphs + (pairs ? 4 + 4 * pairs : 0);
    const uint32_t recordSize = body + (trailerLength ? trailerLength : padding);
    const uint32_t payload = 0x200 + recordSize + recordPadding;
    char *chunk = static_cast<char *>(bMalloc(static_cast<int>(8 + payload), 0));
    memset(chunk, 0, 8 + payload);
    reinterpret_cast<uint32_t *>(chunk)[0] = 0x30201;
    reinterpret_cast<uint32_t *>(chunk)[1] = payload;
    char *p = chunk + 8;
    if (namesLength)
        memcpy(p, names, 0x200);
    else
    {
        memcpy(p, name, nameLength);
        memcpy(p + 0x100, texture, textureLength);
    }
    char *rec = p + 0x200;
    memcpy(&header[0], "FNTF", 4);
    header[2] = (header[2] & 0xFFFF) | (glyphs << 16);
    header[5] = glyphsAt;
    header[6] = kernAt;
    header[7] = recordSize;
    memcpy(rec, header, sizeof(header));
    // Glyph: character, width, height, u, v, advance y, offset x, offset y, kerning count, kerning index, advance x.
    uint32_t kernIndex = 0;
    for (uint32_t i = 0; i < glyphs; ++i)
    {
        const uint8_t *g = glyphData + 14 * i;
        char *out = rec + glyphsAt + 16 * i;
        memcpy(out, g, 12);
        const uint8_t count = g[11];
        const uint16_t index = count ? static_cast<uint16_t>(kernIndex) : 0;
        memcpy(out + 0xC, &index, 2);
        memcpy(out + 0xE, g + 12, 2);
        kernIndex += count;
    }
    if (pairs)
    {
        memcpy(rec + kernAt, &pairs, 4);
        memcpy(rec + kernAt + 4, pairData, 4 * pairs);
    }
    if (trailerLength)
        memcpy(rec + body, trailer, trailerLength);
    return chunk;
}

void UnloadFonts()
{
    for (int i = 0; i < g_fontCount; ++i)
    {
        UnloaderFEngFont(reinterpret_cast<bChunk *>(g_fonts[i]));
        bFree(g_fonts[i]);
        g_fonts[i] = nullptr;
    }
    g_fontCount = 0;
}

void LoadFonts(const char *path)
{
    uint32_t size = 0;
    uint8_t *list = nfs::pack::ReadWhole(path, size);
    if (!list)
        return;
    for (const char *name = reinterpret_cast<const char *>(list); name < reinterpret_cast<const char *>(list) + size &&
                                                                    *name && g_fontCount < kMaxFonts;
         name += lstrlenA(name) + 1)
    {
        char fontPath[128];
        wsprintfA(fontPath, "LANGUAGES\\FONTS\\%s.FONT", name);
        uint32_t fontSize = 0;
        uint8_t *data = nfs::pack::ReadWhole(fontPath, fontSize);
        char *chunk = data ? FontChunk(data, fontSize) : nullptr;
        ::operator delete(data);
        if (!chunk)
        {
            nfs::Log("strings: the font %s does not load", fontPath);
            continue;
        }
        LoaderFEngFont(reinterpret_cast<bChunk *>(chunk));
        g_fonts[g_fontCount++] = chunk;
    }
    ::operator delete(list);
}
} // namespace

void nfs::strings::Begin(ResourceFile *file, const char *name)
{
    const int length = lstrlenA(name);
    if (length < 14 || length > 90 || CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, name, 10, "LANGUAGES\\", 10) != 2 ||
        lstrcmpiA(name + length - 4, ".BIN"))
        return;
    char path[96];
    memcpy(path, name, length - 4);
    lstrcpyA(path + length - 4, ".STRINGS");
    uint32_t size = 0;
    uint8_t *data = nfs::pack::ReadWhole(path, size);
    if (!data)
        return;
    uint32_t head[4];
    memcpy(head, data, sizeof(head));
    const uint32_t count = head[2], units = head[3];
    const size_t recordsAt = 16 + 2 * static_cast<size_t>(units);
    const size_t stringsAt = recordsAt + 8 * static_cast<size_t>(count);
    if (memcmp(data, "STRS", 4) || head[1] != 1 || units > kTableUnits || stringsAt > size)
    {
        ::operator delete(data);
        nfs::Log("strings: %s is not a strings table", path);
        return;
    }
    End(g_file);
    const size_t text = size - stringsAt;
    char *block = static_cast<char *>(bMalloc(static_cast<int>(kHistogram + 8 * count + text), 0));
    memset(block, 0, kHistogram);
    *reinterpret_cast<int *>(block) = static_cast<int>(units);
    memcpy(block + 4, data + 16, 2 * units);
    char *records = block + kHistogram;
    char *strings = records + 8 * count;
    memcpy(strings, data + stringsAt, text);
    size_t at = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t row[2];
        memcpy(row, data + recordsAt + 8 * i, 8);
        reinterpret_cast<uint32_t *>(records)[2 * i] = row[0];
        reinterpret_cast<char **>(records)[2 * i + 1] = strings + at;
        at += row[1] + 1;
    }
    ::operator delete(data);
    Histogram() = block;
    Records() = records;
    StringTable() = strings;
    NumStrings() = static_cast<int>(count);
    g_file = file;
    g_block = block;
    lstrcpyA(path + length - 4, ".FONTS");
    LoadFonts(path);
    nfs::Log("strings: %u and %d fonts for %s", count, g_fontCount, name);
}

void nfs::strings::End(ResourceFile *file)
{
    if (!file || file != g_file)
        return;
    NumStrings() = 0;
    StringTable() = nullptr;
    Records() = nullptr;
    Histogram() = nullptr;
    bFree(g_block);
    g_block = nullptr;
    g_file = nullptr;
    UnloadFonts();
}
