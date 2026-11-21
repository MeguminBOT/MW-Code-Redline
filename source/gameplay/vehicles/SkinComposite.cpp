/** Vinyls composited into a car's skin (original: CompositeSkin32 0x73b010), with the original's colour maths. A vinyl or
 * mask of another size than the skin is scaled to it (the original read past it), and the skin keeps any number of
 * swatch pixels (the original kept 16 a colour and wrote past its table with the Xbox 360's skins, which have 30). */
#include <windows.h>

#include "game/gen/api.h"
#include "core/config.h"
#include "core/log.h"
#include "engine/rendering/vulkan/renderer.h"

namespace
{
const int kTarget = 0;
const int kBaseColour = 1;
const int kSwatchColours = 2;
const int kSwatchCount = 4;
const int kFirstLayer = 6;
const int kLayerWords = 13;
const int kLayerCount = 0x13;
const int kLayerHash = 0;
const int kLayerVinyl = 1;
const int kLayerMask = 2;
const int kLayerImage = 3;
const int kLayerMaskImage = 5;
const int kLayerRemap = 8;
const int kLayerRemapColours = 9;
const int kRemapColours = 3;
const int kWidth = 0x44;
const int kHeight = 0x46;
const int kFormat = 0x4a;
const unsigned char kFormat32 = 0x20;
const unsigned char kFormat8 = 0x08;
const unsigned char kFormat8Of16 = 0x80;
const unsigned char kFormat8Of64 = 0x81;
const TextureLockType kLockWrite = static_cast<TextureLockType>(1);
const unsigned int kSwatchMarks[kSwatchCount] = {0xBF0000FFu, 0xBF00FF00u, 0xBFFF0000u, 0xBFFF00FFu};
const float kByteWeight = 0.003921569f;
const unsigned int kFullMask = 0x80;
const int kMaxSkinSizes = 4;
const int kFraction = 256;

short Short(const void *texture, int offset)
{
    return *reinterpret_cast<const short *>(static_cast<const unsigned char *>(texture) + offset);
}

unsigned char Format(const void *texture)
{
    return static_cast<const unsigned char *>(texture)[kFormat];
}

bool ReadAs32Bit(const void *texture)
{
    const unsigned char f = Format(texture);
    return f == kFormat32 || f == kFormat8 || f == kFormat8Of16 || f == kFormat8Of64;
}

unsigned int Flip(unsigned int c)
{
    return (c & 0xFF00FF00u) | ((c & 0xFFu) << 16) | ((c >> 16) & 0xFFu);
}

struct Swatches
{
    int width;
    int height;
    int count[kSwatchCount];
    int *at[kSwatchCount];
};

Swatches g_swatches[kMaxSkinSizes];
int g_skinSizes = 0;

int SwatchOf(unsigned int pixel)
{
    for (int c = 0; c < kSwatchCount; ++c)
        if (pixel == kSwatchMarks[c])
            return c;
    return -1;
}

const Swatches *KnownSwatches(int width, int height)
{
    for (int i = 0; i < g_skinSizes; ++i)
        if (g_swatches[i].width == width && g_swatches[i].height == height)
            return &g_swatches[i];
    return nullptr;
}

/** The skin's swatch pixels, found in the first skin of its size composited (all skins of a size share them). */
const Swatches *FindSwatches(const unsigned int *pixels, int width, int height)
{
    if (const Swatches *known = KnownSwatches(width, height))
        return known;
    if (g_skinSizes == kMaxSkinSizes)
        return nullptr;
    Swatches &s = g_swatches[g_skinSizes++];
    s.width = width;
    s.height = height;
    const int count = width * height;
    int found[kSwatchCount] = {};
    for (int p = 0; p < count; ++p)
    {
        const int c = SwatchOf(pixels[p]);
        if (c >= 0)
            ++found[c];
    }
    for (int c = 0; c < kSwatchCount; ++c)
    {
        s.at[c] = static_cast<int *>(::operator new(sizeof(int) * (found[c] ? found[c] : 1)));
        s.count[c] = 0;
    }
    for (int p = 0; p < count; ++p)
    {
        const int c = SwatchOf(pixels[p]);
        if (c >= 0)
            s.at[c][s.count[c]++] = p;
    }
    nfs::Log("skin: %dx%d skins have %d, %d, %d and %d swatch pixels", width, height, s.count[0], s.count[1],
             s.count[2], s.count[3]);
    return &s;
}

struct Tap
{
    int first;
    int second;
    int weight;
};

Tap TapAt(int i, int from, int to)
{
    const float f = (static_cast<float>(i) + 0.5f) * static_cast<float>(from) / static_cast<float>(to) - 0.5f;
    if (f <= 0.0f)
        return {0, 0, 0};
    const int first = static_cast<int>(f);
    if (first >= from - 1)
        return {from - 1, from - 1, 0};
    return {first, first + 1, static_cast<int>((f - static_cast<float>(first)) * kFraction + 0.5f)};
}

unsigned int Mix(unsigned int a, unsigned int b, int weight)
{
    unsigned int out = 0;
    for (int shift = 0; shift < 32; shift += 8)
    {
        const int x = static_cast<int>((a >> shift) & 0xFF), y = static_cast<int>((b >> shift) & 0xFF);
        out |= static_cast<unsigned int>((x * (kFraction - weight) + y * weight + kFraction / 2) / kFraction) << shift;
    }
    return out;
}

/** A 32-bit image scaled to width x height (bilinear, pixel centres matched). */
unsigned int *Scaled(const unsigned int *source, int sourceWidth, int sourceHeight, int width, int height)
{
    unsigned int *out = static_cast<unsigned int *>(::operator new(sizeof(unsigned int) * width * height));
    Tap *columns = static_cast<Tap *>(::operator new(sizeof(Tap) * width));
    for (int x = 0; x < width; ++x)
        columns[x] = TapAt(x, sourceWidth, width);
    for (int y = 0; y < height; ++y)
    {
        const Tap row = TapAt(y, sourceHeight, height);
        const unsigned int *top = source + row.first * sourceWidth;
        const unsigned int *bottom = source + row.second * sourceWidth;
        unsigned int *line = out + y * width;
        for (int x = 0; x < width; ++x)
        {
            const Tap &c = columns[x];
            line[x] = Mix(Mix(top[c.first], top[c.second], c.weight), Mix(bottom[c.first], bottom[c.second], c.weight),
                          row.weight);
        }
    }
    ::operator delete(columns);
    return out;
}

/** What each remap colour adds to a pixel's channels for a vinyl channel value (GetBlendColour 0x73ae60 with the
 * channel's weight, value * 1/255 as a float; values of 1 or less add nothing). */
struct Remap
{
    unsigned char add[kRemapColours][256][4];
};

void BuildRemap(Remap &remap, const int *layer)
{
    for (int i = 0; i < kRemapColours; ++i)
    {
        const unsigned int colour = Flip(static_cast<unsigned int>(layer[kLayerRemapColours + i]));
        for (int v = 0; v < 256; ++v)
        {
            const float weight = static_cast<float>(v) * kByteWeight;
            for (int k = 0; k < 4; ++k)
                remap.add[i][v][k] =
                    weight > kByteWeight ? static_cast<unsigned char>(static_cast<int>(
                                               static_cast<double>(weight) * static_cast<double>((colour >> (8 * k)) & 0xFF)))
                                         : 0;
        }
    }
}

/** The vinyl's red, green and blue weigh the remap colours: their channels added, the alpha the most of them. */
unsigned int Remapped(const Remap &remap, unsigned int pixel)
{
    const unsigned char *a = remap.add[0][(pixel >> 16) & 0xFF];
    const unsigned char *b = remap.add[1][(pixel >> 8) & 0xFF];
    const unsigned char *c = remap.add[2][pixel & 0xFF];
    unsigned int out = 0;
    for (int k = 0; k < 3; ++k)
    {
        const unsigned int sum = static_cast<unsigned int>(a[k]) + b[k] + c[k];
        out |= (sum > 255 ? 255u : sum) << (8 * k);
    }
    unsigned int alpha = a[3];
    alpha = b[3] > alpha ? b[3] : alpha;
    alpha = c[3] > alpha ? c[3] : alpha;
    return out | alpha << 24;
}

/** A mask under 0x80 blends the vinyl over the skin by mask / 128 (GetBlendColour's sums, each term cut). */
unsigned int Blend(unsigned int vinyl, unsigned int skin, unsigned int mask)
{
    const unsigned int rest = kFullMask - mask;
    unsigned int out = 0;
    for (int shift = 0; shift < 32; shift += 8)
    {
        const unsigned int sum = ((((vinyl >> shift) & 0xFF) * mask) >> 7) + ((((skin >> shift) & 0xFF) * rest) >> 7);
        out |= (sum > 255 ? 255u : sum) << shift;
    }
    return out;
}

void CompositeLayer(unsigned int *dest, int width, int height, const int *layer, bool log)
{
    if (!layer[kLayerHash])
        return;
    const void *vinyl = reinterpret_cast<const void *>(layer[kLayerVinyl]);
    const void *mask = reinterpret_cast<const void *>(layer[kLayerMask]);
    const unsigned int *image = reinterpret_cast<const unsigned int *>(layer[kLayerImage]);
    const unsigned int *maskImage = reinterpret_cast<const unsigned int *>(layer[kLayerMaskImage]);
    if (!vinyl || !mask || !image || !maskImage)
        return;
    if (!ReadAs32Bit(vinyl) || !ReadAs32Bit(mask))
    {
        nfs::Log("skin: vinyl %08x left out, its format %02x / mask %02x is not read as 32-bit", layer[kLayerHash],
                 Format(vinyl), Format(mask));
        return;
    }
    unsigned int *scaledImage = nullptr;
    unsigned int *scaledMask = nullptr;
    if (Short(vinyl, kWidth) != width || Short(vinyl, kHeight) != height)
        image = scaledImage = Scaled(image, Short(vinyl, kWidth), Short(vinyl, kHeight), width, height);
    if (Short(mask, kWidth) != width || Short(mask, kHeight) != height)
        maskImage = scaledMask = Scaled(maskImage, Short(mask, kWidth), Short(mask, kHeight), width, height);
    const bool remap = layer[kLayerRemap] != 0;
    Remap *table = nullptr;
    if (remap)
    {
        table = static_cast<Remap *>(::operator new(sizeof(Remap)));
        BuildRemap(*table, layer);
    }
    const int pixels = width * height;
    int covered = 0;
    for (int p = 0; p < pixels; ++p)
    {
        const unsigned int m = (maskImage[p] >> 8) & 0xFF;
        if (!m)
            continue;
        ++covered;
        const unsigned int colour = remap ? Remapped(*table, image[p]) : image[p];
        dest[p] = m >= kFullMask ? colour : Blend(colour, dest[p], m);
    }
    if (log)
        nfs::Log("skin:   vinyl %08x %dx%d, mask %dx%d%s, %d of %d pixels covered, remap %d: %08x %08x %08x %08x",
                 layer[kLayerHash], Short(vinyl, kWidth), Short(vinyl, kHeight), Short(mask, kWidth),
                 Short(mask, kHeight), scaledImage || scaledMask ? " (scaled)" : "", covered, pixels,
                 layer[kLayerRemap], layer[kLayerRemapColours], layer[kLayerRemapColours + 1],
                 layer[kLayerRemapColours + 2], layer[kLayerRemapColours + 3]);
    ::operator delete(table);
    ::operator delete(scaledImage);
    ::operator delete(scaledMask);
}

/** The composited skin as a 32-bit BMP (Debug.SkinDump: skin_dump_N.bmp beside the exe). */
void Dump(const unsigned int *pixels, int width, int height)
{
    static int n = 0;
    char name[32];
    wsprintfA(name, "skin_dump_%d.bmp", n++);
    HANDLE f = CreateFileA(name, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return;
    const DWORD bytes = static_cast<DWORD>(width) * height * 4;
    unsigned char head[54] = {'B', 'M'};
    *reinterpret_cast<DWORD *>(head + 2) = 54 + bytes;
    *reinterpret_cast<DWORD *>(head + 10) = 54;
    *reinterpret_cast<DWORD *>(head + 14) = 40;
    *reinterpret_cast<LONG *>(head + 18) = width;
    *reinterpret_cast<LONG *>(head + 22) = -height;
    *reinterpret_cast<WORD *>(head + 26) = 1;
    *reinterpret_cast<WORD *>(head + 28) = 32;
    DWORD written = 0;
    WriteFile(f, head, 54, &written, nullptr);
    WriteFile(f, pixels, bytes, &written, nullptr);
    CloseHandle(f);
}
} // namespace

int __cdecl CompositeSkin32(SkinCompositeParams *params)
{
    const int *words = reinterpret_cast<const int *>(params);
    TextureInfoPlatInterface *target = reinterpret_cast<TextureInfoPlatInterface *>(words[kTarget]);
    if (!target || Format(target) != kFormat32)
        return 0;
    const int width = Short(target, kWidth), height = Short(target, kHeight);
    // Every texel is written below; the old image is read only for the swatches of a skin size not seen yet.
    unsigned int *dest = static_cast<unsigned int *>(
        KnownSwatches(width, height) && target->PlatInfo
            ? nfs::vk::g_renderer->LockLevel(static_cast<nfs::vk::Texture *>(target->PlatInfo->Texture), 0, true)
            : target->LockImage(kLockWrite));
    if (!dest)
        return 0;
    static const bool log = nfs::config::Int("Debug", "SkinLog", 0) != 0;
    static const bool dump = nfs::config::Int("Debug", "SkinDump", 0) != 0;
    const Swatches *swatches = FindSwatches(dest, width, height);
    if (log)
        nfs::Log("skin: composite into %p (%dx%d), %d layers, base %08x, swatches %08x %08x %08x %08x",
                 static_cast<void *>(target), width, height, words[kLayerCount], words[kBaseColour],
                 words[kSwatchColours], words[kSwatchColours + 1], words[kSwatchColours + 2],
                 words[kSwatchColours + 3]);
    const unsigned int base = Flip(static_cast<unsigned int>(words[kBaseColour]));
    const int pixels = width * height;
    for (int p = 0; p < pixels; ++p)
        dest[p] = base;
    for (int i = 0; i < words[kLayerCount]; ++i)
        CompositeLayer(dest, width, height, words + kFirstLayer + i * kLayerWords, log);
    if (swatches)
        for (int c = 0; c < kSwatchCount; ++c)
        {
            const unsigned int colour = Flip(static_cast<unsigned int>(words[kSwatchColours + c]));
            for (int j = 0; j < swatches->count[c]; ++j)
                dest[swatches->at[c][j]] = colour;
        }
    if (dump && words[kLayerCount] > 0)
        Dump(dest, width, height);
    target->UnlockImage(dest);
    return 1;
}
