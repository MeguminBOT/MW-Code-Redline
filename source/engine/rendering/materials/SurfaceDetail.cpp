/**
 * Surface detail (engine/rendering/surface.h, docs/SURFACES.md): the options, what each texture gets (GLOBAL\SURFACES.TXT) and the
 * normal maps with a height in their alpha (GLOBAL\SURFACES.BIN). The work itself is the shaders'
 * (assets/shaders/owned/surface.hlsli); this hands them a word per draw and four numbers per frame.
 */
#include <string.h>
#include <windows.h>

#include "game/gen/api.h"
#include "core/config.h"
#include "core/log.h"
#include "engine/rendering/surface.h"
#include "engine/rendering/vulkan/renderer.h"
#include "engine/resources/pack.h"

namespace nfs::surface
{
namespace
{
const char kTablePath[] = "GLOBAL\\SURFACES.TXT";
const char kImagePath[] = "GLOBAL\\SURFACES.BIN";
/** "NSF1": SURFACES.BIN starts with it, then the image count and a record for each (kRecordBytes: name hash, D3D
 * format, top level bytes, the replaced image's top level bytes, levels, offset of the levels in the file). */
const unsigned int kImageMagic = 0x3146534e;
const int kRecordBytes = 24;

const int kMaxEntries = 512;
const int kSlots = 1024;
const int kMaxImages = 256;
const int kImageSlots = 512;

/** The relief at each level: steps along the view ray, steps towards the light, and how far from the camera it
 * reaches (metres). */
const float kViewSteps[kLevels] = {0.0f, 8.0f, 16.0f, 28.0f};
const float kLightSteps[kLevels] = {0.0f, 0.0f, 4.0f, 8.0f};
const float kReach[kLevels] = {0.0f, 22.0f, 32.0f, 45.0f};
/** The deepest relief a draw's word holds, in ten-thousandths of a texture repeat. */
const int kMostDepth = 255;

const char *const kProfileNames[kProfiles] = {"none", "asphalt", "concrete", "cobble", "dirt"};

/** A line of SURFACES.TXT. */
struct Entry
{
    unsigned int diffuse;
    unsigned int normal;
    unsigned char depth;
    unsigned char profile;
};

struct State
{
    bool ready;
    int level;
    bool wear;
    Entry entries[kMaxEntries];
    int count;
    /** Entries by their diffuse texture's hash: index + 1, linear probing, 0 for an empty slot. */
    short slots[kSlots];
    Image images[kMaxImages];
    unsigned int imageHashes[kMaxImages];
    int imageCount;
    short imageSlots[kImageSlots];
    unsigned char *file;
};
State g_state;

unsigned int SlotOf(unsigned int hash, int slots)
{
    return (hash * 2654435761u) >> 16 & static_cast<unsigned int>(slots - 1);
}

/** The whole file in memory (the caller's to keep), or null. */
unsigned char *ReadWhole(const char *path, unsigned int &size)
{
    uint32_t got = 0;
    unsigned char *data = nfs::pack::ReadFile(path, got);
    size = got;
    return data;
}

unsigned int Word32(const unsigned char *p)
{
    unsigned int v;
    memcpy(&v, p, 4);
    return v;
}

void LoadImages()
{
    State &s = g_state;
    unsigned int size = 0;
    s.file = ReadWhole(kImagePath, size);
    if (!s.file)
    {
        nfs::Log("surface: no %s (tools/surfaces.py writes it), no relief", kImagePath);
        return;
    }
    if (size < 8 || Word32(s.file) != kImageMagic)
    {
        nfs::Log("surface: %s is not a surface image file", kImagePath);
        return;
    }
    const unsigned int count = Word32(s.file + 4);
    for (unsigned int i = 0; i < count && s.imageCount < kMaxImages; ++i)
    {
        const unsigned char *record = s.file + 8 + i * kRecordBytes;
        if (record + kRecordBytes > s.file + size)
            break;
        Image image;
        image.format = Word32(record + 4);
        image.baseBytes = Word32(record + 8);
        image.sourceBaseBytes = Word32(record + 12);
        image.levels = Word32(record + 16);
        const unsigned int offset = Word32(record + 20);
        // Every level a quarter of the one before: all of them are under 4/3 of the first.
        if (offset >= size || image.baseBytes > size - offset || !image.levels)
            continue;
        image.data = s.file + offset;
        const unsigned int hash = Word32(record);
        unsigned int slot = SlotOf(hash, kImageSlots);
        while (s.imageSlots[slot] && s.imageHashes[s.imageSlots[slot] - 1] != hash)
            slot = (slot + 1) & (kImageSlots - 1);
        if (s.imageSlots[slot])
            continue;
        s.imageHashes[s.imageCount] = hash;
        s.images[s.imageCount] = image;
        s.imageSlots[slot] = static_cast<short>(++s.imageCount);
    }
    nfs::Log("surface: %d normal maps with heights from %s", s.imageCount, kImagePath);
}

const Image *FindImage(unsigned int hash)
{
    const State &s = g_state;
    for (unsigned int slot = SlotOf(hash, kImageSlots);; slot = (slot + 1) & (kImageSlots - 1))
    {
        const int index = s.imageSlots[slot];
        if (!index)
            return nullptr;
        if (s.imageHashes[index - 1] == hash)
            return &s.images[index - 1];
    }
}

/** The next word of a line (up to a blank), or false at its end or at a comment. */
bool Token(const char *&p, char *out, int size)
{
    while (*p == ' ' || *p == '\t')
        ++p;
    if (!*p || *p == '#' || *p == '\r' || *p == '\n')
        return false;
    int n = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && n < size - 1)
        out[n++] = *p++;
    out[n] = 0;
    return true;
}

/** A texture as SURFACES.TXT names it: its name, or its name's hash as 0x and hex digits (the packs cut names off
 * at 23 letters; the hash is of the whole name). */
unsigned int TextureHash(const char *word)
{
    if (word[0] != '0' || (word[1] != 'x' && word[1] != 'X'))
        return bStringHash(word);
    unsigned int hash = 0;
    for (const char *c = word + 2; *c; ++c)
    {
        const int digit = *c >= '0' && *c <= '9' ? *c - '0' : (*c | 0x20) >= 'a' && (*c | 0x20) <= 'f' ? (*c | 0x20) - 'a' + 10 : -1;
        if (digit < 0)
            break;
        hash = hash << 4 | static_cast<unsigned int>(digit);
    }
    return hash;
}

/** A number of a text line: digits and a decimal point. */
float Decimal(const char *word)
{
    float value = 0.0f, place = 0.0f;
    for (const char *c = word; *c; ++c)
    {
        if (*c == '.')
        {
            place = 1.0f;
            continue;
        }
        if (*c < '0' || *c > '9')
            break;
        value = value * 10.0f + static_cast<float>(*c - '0');
        place *= 10.0f;
    }
    return place > 1.0f ? value / place : value;
}

void LoadTable()
{
    State &s = g_state;
    unsigned int size = 0;
    char *text = reinterpret_cast<char *>(ReadWhole(kTablePath, size));
    if (!text)
    {
        nfs::Log("surface: no %s, no surface detail", kTablePath);
        return;
    }
    // Debug.ReliefDepth: every depth at this many percent of its own, for tuning.
    const float scale = static_cast<float>(nfs::config::Int("Debug", "ReliefDepth", 100)) * 0.01f;
    int relief = 0;
    for (const char *line = text; *line && s.count < kMaxEntries;)
    {
        const char *p = line;
        char diffuse[64], normal[64], depth[16], profile[16];
        if (Token(p, diffuse, sizeof(diffuse)) && Token(p, normal, sizeof(normal)) && Token(p, depth, sizeof(depth)) &&
            Token(p, profile, sizeof(profile)))
        {
            Entry entry = {TextureHash(diffuse), TextureHash(normal), 0, 0};
            for (int i = 0; i < kProfiles; ++i)
                if (!lstrcmpiA(profile, kProfileNames[i]))
                    entry.profile = static_cast<unsigned char>(i);
            // Relief needs the height SURFACES.BIN gives the normal map.
            const int units = static_cast<int>(Decimal(depth) * scale * 10000.0f + 0.5f);
            if (units > 0 && FindImage(entry.normal))
            {
                entry.depth = static_cast<unsigned char>(units > kMostDepth ? kMostDepth : units);
                ++relief;
            }
            unsigned int slot = SlotOf(entry.diffuse, kSlots);
            while (s.slots[slot])
                slot = (slot + 1) & (kSlots - 1);
            s.entries[s.count] = entry;
            s.slots[slot] = static_cast<short>(++s.count);
        }
        while (*line && *line != '\n')
            ++line;
        if (*line)
            ++line;
    }
    ::operator delete(text);
    nfs::Log("surface: %d texture pairs from %s, %d with relief", s.count, kTablePath, relief);
}

/** The options and the data, read at the first use. */
State &Ready()
{
    State &s = g_state;
    if (s.ready)
        return s;
    s.ready = true;
    const int level = nfs::config::Int("Display", "SurfaceDetail", kHigh);
    s.level = level < kOff ? kOff : level >= kLevels ? kLevels - 1 : level;
    s.wear = nfs::config::Int("Display", "RoadWear", 1) != 0;
    if (nfs::vk::g_renderer)
    {
        LoadImages();
        LoadTable();
    }
    return s;
}
} // namespace

int DetailLevel()
{
    return Ready().level;
}

void SetDetailLevel(int level)
{
    State &s = Ready();
    s.level = level < kOff ? kOff : level >= kLevels ? kLevels - 1 : level;
    nfs::config::WriteInt("Display", "SurfaceDetail", s.level);
}

bool RoadWear()
{
    return Ready().wear;
}

void SetRoadWear(bool on)
{
    Ready().wear = on;
    nfs::config::WriteInt("Display", "RoadWear", on ? 1 : 0);
}

bool Available()
{
    return nfs::vk::g_renderer != nullptr;
}

uint32_t DrawValue(unsigned int diffuse, unsigned int normal)
{
    const State &s = Ready();
    if (s.level == kOff || !s.count)
        return 0;
    uint32_t depth = 0, profile = 0;
    bool found = false;
    for (unsigned int slot = SlotOf(diffuse, kSlots);; slot = (slot + 1) & (kSlots - 1))
    {
        const int index = s.slots[slot];
        if (!index)
            break;
        const Entry &entry = s.entries[index - 1];
        if (entry.diffuse != diffuse)
            continue;
        // The line for this normal map, or else the texture's first.
        if (entry.normal == normal)
        {
            depth = entry.depth;
            profile = entry.profile;
            break;
        }
        if (!found)
            profile = entry.profile;
        found = true;
    }
    if (!s.wear)
        profile = 0;
    return depth | profile << 8;
}

void SceneConstants(float out[4])
{
    const State &s = Ready();
    out[0] = kViewSteps[s.level];
    out[1] = kLightSteps[s.level];
    out[2] = kReach[s.level];
    // Debug.SurfaceView: the roads in their wear profiles' colours (surface.hlsli).
    static const bool view = nfs::config::Int("Debug", "SurfaceView", 0) != 0;
    out[3] = s.level != kOff && s.wear ? view ? 2.0f : 1.0f : 0.0f;
}

const Image *Replacement(unsigned int hash)
{
    return Ready().imageCount ? FindImage(hash) : nullptr;
}
} // namespace nfs::surface
