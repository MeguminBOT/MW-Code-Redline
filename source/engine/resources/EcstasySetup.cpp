/**
 * Engine set-up and per-frame bookkeeping of zEcstasy (original: eInitEngine and the eInit* helpers it calls,
 * eSwapFrameMallocBuffers, SetCurrentSunInfo, eFixupReplacementTextureTables, FixupTextureInfoNull, the 0x3bd00 table
 * loader and eStreamPackLoader's entry-data lookup 0x504d20).
 *
 * Frame memory: two buffers (FrameMemoryBuffer 0x915128 in use, 0x91512c the other) swapped every frame; eFrameMalloc
 * hands out of CurrentBufferPos..CurrentBufferEnd. eInitEnginePlat (EngineInit.cpp) may put larger buffers in place
 * right after eInitEngine made these.
 */
#include <stdlib.h>

#include "game/gen/api.h"
#include "legacy/link.h"

extern "C" int eInitEnginePlat();
eViewPlatInfo *__cdecl ViewPlatInfo(int view);
void __cdecl BillboardCorners(const float *centre, const float *a, const float *b, float *corner0, float *corner1,
                              float *corner2, float *corner3);
extern "C" void __cdecl epInitViews();
eStreamingEntry *__fastcall GetStreamingEntry(eStreamPackLoader *loader, void *, unsigned int hash);

NFS_REPLACE(0x5042f0, LoaderKeyedTable)
NFS_REPLACE(0x504d20, GetLoadedEntryData)
NFS_REPLACE(0x4fe220, BillboardCorners)

int __cdecl LoaderKeyedTable(bChunk *chunk);
int __fastcall GetLoadedEntryData(eStreamPackLoader *loader, void *, const unsigned int *names, int count,
                                  void **data, int found);

namespace
{
template <typename T> T &At(void *p, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(p) + offset);
}

template <typename T> T &Global(unsigned int address)
{
    return *reinterpret_cast<T *>(address);
}

unsigned char *&FrontBuffer()
{
    return Global<unsigned char *>(0x915128);
}

unsigned char *&BackBuffer()
{
    return Global<unsigned char *>(0x91512c);
}

const unsigned int kFrameMemoryBytes = 0x64000;
const int kLightFlarePoolSize = 50;

/** The sun's position (0x9175a0) and the world light direction made from it (0x8f2404, its w at 0x8f2410). */
float *const kSunPosition = reinterpret_cast<float *>(0x9175a0);
float *const kWorldLightDirection = reinterpret_cast<float *>(0x8f2404);

/** Address ranges that moved when memory was defragmented (gDefragFixer): low, high, count, then (low, high, delta). */
struct MovedRange
{
    unsigned int low;
    unsigned int high;
    unsigned int delta;
};
} // namespace

void __cdecl eSwapFrameMallocBuffers()
{
    if (FrameMallocFailed && FrameMallocFailAmount > FrameMallocMaxFailAmount)
        FrameMallocMaxFailAmount = FrameMallocFailAmount;
    const int used = static_cast<int>(CurrentBufferPos - FrontBuffer());
    FrameMallocFailed = 0;
    FrameMallocFailAmount = 0;
    if (used > Eframelargest)
        Eframelargest = used;
    Eframecurrent = used;
    Global<int>(0x915124) = Global<int>(0x915120);
    Global<int>(0x915120) = used;
    unsigned char *next = BackBuffer();
    BackBuffer() = FrontBuffer();
    FrontBuffer() = next;
    CurrentBufferStart = next;
    CurrentBufferPos = next;
    CurrentBufferEnd = next + FrameMemoryBufferSize;
    FrameMallocAllocNum = 0;
}

void __cdecl eSwapDynamicFrameMemory()
{
}

void __cdecl MaybePrintUnusedTextures()
{
}

void __cdecl eInitModels()
{
    eModelSlotPool = bNewSlotPool(0x18, 0xe74, "Ecstacy:ModelSlotPool", 0);
}

void __cdecl eInitTextures()
{
    TexturePackSlotPool = bNewSlotPool(0x20, 0x80, "TexturePackSlotPool", 0);
}

void __cdecl eInitSolids()
{
    // The loaded solid stats (lists, solids, bytes, vertex bytes, extra bytes) and the queued loading tables.
    for (unsigned int address = 0x915180; address <= 0x915190; address += 4)
        Global<int>(address) = 0;
    for (int i = 0; i < 0x100; ++i)
        reinterpret_cast<int *>(0x915b70)[i] = 0;
    eStreamingPackSlotPool = bNewSlotPool(0x44, 0x40, "eStreamingPackSlotPool", 0);
}

void __cdecl eInitLightFlarePool()
{
    int &done = Global<int>(0x91c190);
    if (done)
        return;
    done = 1;
    char *flare = reinterpret_cast<char *>(0x916a78);
    for (int i = 0; i < kLightFlarePoolSize; ++i, flare += 0x30)
    {
        flare[0x2d] = 0;
        At<int>(flare, 0x20) = 0;
        At<int>(flare, 0x24) = 0;
        At<float>(flare, 0x28) = -1.0f;
        At<float>(flare, 0x1c) = 999.0f;
    }
}

int __cdecl eInitEngine()
{
    FrontBuffer() = static_cast<unsigned char *>(malloc(kFrameMemoryBytes));
    BackBuffer() = static_cast<unsigned char *>(malloc(kFrameMemoryBytes));
    CurrentBufferPos = FrontBuffer();
    FrameMemoryBufferSize = kFrameMemoryBytes;
    CurrentBufferEnd = FrontBuffer() + kFrameMemoryBytes;
    FrameMallocAllocNum = 0;
    SlotPool *polys = bNewSlotPool(0xa0, 0x20, "ePolySlotPool", 0);
    Global<SlotPool *>(0x915f74) = polys;
    At<unsigned int>(polys, 0x14) &= ~2u;
    eInitModels();
    eInitTextures();
    eInitSolids();
    epInitViews();
    // The built-in light material the default pointer starts on.
    unsigned int *material = Global<unsigned int *>(0x8f23e8);
    for (int i = 0; i < 0x2a; ++i)
        material[i] = 0;
    elInitPlat();
    eInitLightFlarePool();
    NFS_ORIGINAL(0x6cefa0, void(__cdecl *)())();
    CAnimEngineManager::Init();
    NFS_ORIGINAL(0x73d2c0, void(__cdecl *)())();
    return eInitEnginePlat();
}

void __cdecl SetCurrentSunInfo(unsigned int name)
{
    char *found = nullptr;
    SunInfo = nullptr;
    if (name)
    {
        char *info = reinterpret_cast<char *>(SunInfoTable);
        for (int i = NumSunInfo; i > 0; --i, info += 0x110)
            if (At<unsigned int>(info, 4) == name)
                found = info;
        SunInfo = reinterpret_cast<SunChunkInfo *>(found);
    }
    if (!found)
    {
        kSunPosition[0] = kSunPosition[1] = kSunPosition[2] = 0.0f;
        return;
    }
    if (At<int>(found, 0) == 2)
        for (int i = 0; i < 3; ++i)
            kSunPosition[i] = At<float>(found, 0x20 + i * 4);
    else
    {
        SunInfo = nullptr;
        kSunPosition[0] = kSunPosition[1] = kSunPosition[2] = 0.0f;
    }
    bNormalize(reinterpret_cast<bVector3 *>(kWorldLightDirection), reinterpret_cast<const bVector3 *>(kSunPosition));
    kWorldLightDirection[3] = 0.0f;
}

void __cdecl eFixupReplacementTextureTables()
{
    const unsigned int low = Global<unsigned int>(0x9160b0);
    const unsigned int high = Global<unsigned int>(0x9160b4);
    const int rangeCount = Global<int>(0x9160b8);
    const MovedRange *ranges = reinterpret_cast<const MovedRange *>(0x9160bc);
    // The fix-up list: (table, count) pairs; each table entry (0xc bytes) has a texture pointer at +8.
    for (char *fixup = reinterpret_cast<char *>(0x914b60); fixup < reinterpret_cast<char *>(0x915110); fixup += 8)
    {
        char *table = At<char *>(fixup, 0);
        if (!table)
            continue;
        for (int i = 0; i < At<short>(fixup, 4); ++i)
        {
            unsigned int &pointer = At<unsigned int>(table, i * 0xc + 8);
            unsigned int moved = pointer;
            if (moved >= low && moved < high)
                for (int r = 0; r < rangeCount; ++r)
                    if (moved >= ranges[r].low && moved < ranges[r].high)
                    {
                        moved += ranges[r].delta;
                        break;
                    }
            pointer = moved;
        }
    }
}

TextureInfo *__cdecl FixupTextureInfoNull(TextureInfo *texture, unsigned int name, TexturePack *pack, bool loading)
{
    if (loading)
    {
        if (!texture)
            if (TextureInfo *loaded = pack->GetLoadedTexture(name))
            {
                reinterpret_cast<char *>(loaded)[0x53] = 1;
                return loaded;
            }
        return texture;
    }
    if (texture && At<TexturePack *>(texture, 0x68) == pack && !pack->GetLoadedTexture(name))
        return GetTextureInfo(name, 0, 0);
    return texture;
}

/** The 0x3bd00 chunk: a table of 0x20-byte entries (keyed at +0x14, looked up by 0x4fdf90) at 0x916058, count 0x91605c. */
int __cdecl LoaderKeyedTable(bChunk *chunkObject)
{
    char *chunk = reinterpret_cast<char *>(chunkObject);
    if (At<unsigned int>(chunk, 0) != 0x3bd00)
        return 0;
    char *data = reinterpret_cast<char *>((reinterpret_cast<unsigned int>(chunk) + 0x17) & ~0xfu);
    Global<char *>(0x916058) = data;
    Global<unsigned int>(0x91605c) = static_cast<unsigned int>(chunk + 8 + At<int>(chunk, 4) - data) >> 5;
    return 1;
}

/** eStreamPackLoader (thiscall): appends to `data` the loaded data (+0x14) of each named entry that has some. */
int __fastcall GetLoadedEntryData(eStreamPackLoader *loader, void *, const unsigned int *names, int count,
                                  void **data, int found)
{
    for (int i = 0; i < count; ++i)
    {
        if (!names[i])
            continue;
        char *entry = reinterpret_cast<char *>(GetStreamingEntry(loader, nullptr, names[i]));
        if (entry && At<void *>(entry, 0x14))
            data[found++] = At<void *>(entry, 0x14);
    }
    return found;
}

eView::eView()
{
    char *self = reinterpret_cast<char *>(this);
    for (int i = 0; i < 0x1c; ++i)
        reinterpret_cast<int *>(self)[i] = 0;
    char **list = reinterpret_cast<char **>(self + 0x44);
    list[0] = list[1] = reinterpret_cast<char *>(list);
    const int index = static_cast<int>(self - reinterpret_cast<char *>(0x9195e0)) / 0x70;
    At<float>(self, 0x18) = 1.0f;
    At<int>(self, 4) = index;
    *reinterpret_cast<eViewPlatInfo **>(self) = ViewPlatInfo(index);
    At<int>(self, 0x64) = 0;
    At<int>(self, 0x68) = 0;
    At<int>(self, 0x6c) = 0;
    At<int>(self, 0x24) = 4;
}

bool __cdecl GetConstraintBasis(EffectParticleConstraint constraint, bVector4 &right, bVector4 &up, unsigned short &angle,
                                bMatrix4 *orientation)
{
    float *r = &right.x, *u = &up.x;
    switch (static_cast<int>(constraint))
    {
    case 3:
        r[0] = 0.0f;
        r[1] = 1.0f;
        r[2] = 0.0f;
        r[3] = 0.0f;
        u[0] = 0.0f;
        u[1] = 0.0f;
        u[2] = 1.0f;
        u[3] = 0.0f;
        if (angle)
        {
            const float c = bCos(angle), s = bSin(angle);
            r[1] = c;
            r[2] = s;
            u[1] = -s;
            u[2] = c;
        }
        return true;
    case 5:
        r[0] = 1.0f;
        r[1] = 0.0f;
        r[2] = 0.0f;
        r[3] = 0.0f;
        u[0] = 0.0f;
        u[1] = 0.0f;
        u[2] = 1.0f;
        u[3] = 0.0f;
        if (angle)
        {
            const float c = bCos(angle), s = bSin(angle);
            r[0] = c;
            r[2] = -s;
            u[0] = s;
            u[2] = c;
        }
        return true;
    case 6:
        r[0] = 1.0f;
        r[1] = 0.0f;
        r[2] = 0.0f;
        r[3] = 0.0f;
        u[0] = 0.0f;
        u[1] = 1.0f;
        u[2] = 0.0f;
        u[3] = 0.0f;
        if (angle)
        {
            const float c = bCos(angle), s = bSin(angle);
            r[0] = c;
            r[1] = s;
            u[0] = -s;
            u[1] = c;
        }
        return true;
    case 8: {
        // The first two columns of the given orientation, then turned about its z axis.
        if (!orientation)
            return false;
        const float *m = reinterpret_cast<const float *>(orientation);
        r[0] = m[0];
        r[1] = m[4];
        r[2] = m[8];
        r[3] = 0.0f;
        u[0] = m[1];
        u[1] = m[5];
        u[2] = m[9];
        u[3] = 0.0f;
        if (angle)
        {
            bMatrix4 turn;
            eCreateRotationZ(&turn, angle);
            eMulVector(&right, &turn, &right);
            eMulVector(&up, &turn, &up);
        }
        return true;
    }
    default:
        return false;
    }
}

/** The four corners of a billboard around `centre` from its two half axes `a` and `b` (original sub_4FE220). */
void __cdecl BillboardCorners(const float *centre, const float *a, const float *b, float *corner0, float *corner1,
                              float *corner2, float *corner3)
{
    float difference[4], sum[4];
    for (int i = 0; i < 4; ++i)
    {
        difference[i] = a[i] - b[i];
        sum[i] = a[i] + b[i];
    }
    for (int i = 0; i < 4; ++i)
        corner0[i] = centre[i] - difference[i];
    for (int i = 0; i < 4; ++i)
        corner1[i] = centre[i] - sum[i];
    for (int i = 0; i < 4; ++i)
        corner2[i] = centre[i] + sum[i];
    for (int i = 0; i < 4; ++i)
        corner3[i] = centre[i] + difference[i];
}
