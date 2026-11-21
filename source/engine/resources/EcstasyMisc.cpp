/**
 * The Ecstasy engine's helpers (original: zEcstasy): views' cull set-up and pixel size, replacement texture tables,
 * smoothing normals across solids, texture data assignment as packs load, the sun table's loader, the light context's
 * environment map and the particle colour curves' bases.
 */
#include <math.h>
#include <string.h>

#include "game/gen/api.h"
#include "legacy/link.h"

extern "C" int TheGameFlowManager_CurrentGameFlowState;

NFS_REPLACE(0x4fa510, VectorTimesMatrix)
NFS_REPLACE(0x4faa90, FreeTexturePack)
NFS_REPLACE(0x4fae40, SetViewCullFlags)
NFS_REPLACE(0x4fb420, CollectModelTextures)
NFS_REPLACE(0x4fb4f0, StreamingEntryBefore)
NFS_REPLACE(0x4fbea0, SolidLoadingStreamingPackPhase1)
NFS_REPLACE(0x4fd070, InitTextureInfo)
NFS_REPLACE(0x4fd2c0, AnimationFraction)
NFS_REPLACE(0x4fdae0, FreeFastBlock)
NFS_REPLACE(0x4fdb30, FreeFastBlock)
NFS_REPLACE(0x4fdbb0, FreeEmitter)
NFS_REPLACE(0x4fdd40, FreeEmitterGroup)
NFS_REPLACE(0x4fddb0, EmitterPlatData)
NFS_REPLACE(0x4fd630, ConstructEmitterGroupAttribs)
NFS_REPLACE(0x4fd750, ConstructEmitterDataAttribs)
NFS_REPLACE(0x4fb840, SmoothModelNormals)
NFS_REPLACE(0x4fbd30, SmoothSolidNormals)
NFS_REPLACE(0x4fbb20, SolidSmoothNormals)
NFS_REPLACE(0x4fbac0, SolidPositionMarker)

void __cdecl SetEnvironmentSource(void *source, char *target);

namespace
{
template <typename T> T &At(unsigned int address)
{
    return *reinterpret_cast<T *>(address);
}

int &DisableFixUpTables()
{
    return At<int>(0x915fdc);
}

int &DirtySolids()
{
    return At<int>(0x915fe0);
}

int &DirtyTextures()
{
    return At<int>(0x915fe4);
}

int &DirtyAnimations()
{
    return At<int>(0x915fe8);
}

/** ReplacementTextureTableFixups: (table, count, references) entries up to 0x915110. */
struct Fixup
{
    void *table;
    short count;
    short references;
};
Fixup *const kFixups = reinterpret_cast<Fixup *>(0x914b60);
const int kFixupCount = (0x915110 - 0x914b60) / static_cast<int>(sizeof(Fixup));

struct ReplacementTexture
{
    unsigned int from;
    unsigned int to;
    TextureInfo *texture;
};

const unsigned int kSunChunk = 0x34202;
const int kSunInfoSize = 0x110;
const unsigned int kFastMem = 0x925b30;
const float kDegreesToAngle = 65536.0f;

float *Floats(void *p)
{
    return static_cast<float *>(p);
}
} // namespace

void __cdecl VectorTimesMatrix(const float *v, const float *m, float *out)
{
    const float x = v[0], y = v[1], z = v[2], w = v[3];
    for (int c = 0; c < 4; ++c)
        out[c] = x * m[c] + y * m[4 + c] + z * m[8 + c] + w * m[12 + c];
}

void __cdecl FreeTexturePack(void *pack)
{
    bFree(TexturePackSlotPool, pack);
}

void __cdecl eFixUpTables()
{
    if (DisableFixUpTables() || (!DirtySolids() && !DirtyTextures()))
        return;
    eFixUpTablesPlat();
    DirtySolids() = 0;
    DirtyTextures() = 0;
    DirtyAnimations() = 0;
}

/** A view's cull flags (see source/engine/rendering/culling/SceneryCulling.cpp): the player views take the player detail, mirrors and
 * reflection modes their own, the environment map faces their low detail. */
void __cdecl SetViewCullFlags(eView *view, char *cull, unsigned int flags)
{
    const int mode = eGetCurrentViewMode();
    const int id = view->ID;
    flags |= id == 1 || id == 2 ? 0x12 : 0x10;
    if (mode == 4 || mode == 3)
        flags |= 1;
    if (id > 0x11 && id < 0x18)
        flags |= 0x100;
    const char *race = reinterpret_cast<const char *>(GRaceStatus::fObj);
    if (race && *reinterpret_cast<const int *>(race + 0x1960) == 1)
        flags |= 4;
    if (flags & 0x1800)
        flags |= 0x8000040;
    if (flags & 0x1000)
        flags |= 2;
    if (id == 3)
        flags |= 0x20;
    SetEnvironmentSource(view, cull);
    *reinterpret_cast<unsigned int *>(cull + 0x84) = flags;
}

/** A model's textures into `out` from `count` on: its replacement table's and its solid's (thiscall). */
int __fastcall CollectModelTextures(const char *self, void *, void **out, int count)
{
    if (void *table = *reinterpret_cast<void *const *>(self + 0x1c))
        out[count++] = table;
    if (const char *solid = *reinterpret_cast<const char *const *>(self + 0x20))
        out[count++] = *reinterpret_cast<void *const *>(solid + 0x3c);
    return count;
}

/** The load list's order (bList::Sort, which tests all of eax): by pack (+8), then by the entry's file offset (+0x10). */
int __cdecl StreamingEntryBefore(const char *a, const char *b)
{
    const unsigned int ao = *reinterpret_cast<const unsigned int *>(a + 8), bo = *reinterpret_cast<const unsigned int *>(b + 8);
    if (ao > bo)
        return false;
    return bo != ao || *reinterpret_cast<const unsigned int *>(a + 0x10) <= *reinterpret_cast<const unsigned int *>(b + 0x10);
}

void eStreamPackLoader::InternalUnloadStreamingEntry(eStreamingPack *packObject, eStreamingEntry *entryObject)
{
    int *loader = reinterpret_cast<int *>(this);
    char *pack = reinterpret_cast<char *>(packObject), *entry = reinterpret_cast<char *>(entryObject);
    char *&data = *reinterpret_cast<char **>(entry + 0x14);
    if (!(static_cast<unsigned char>(entry[0x11]) & 0x20))
    {
        const unsigned int align = static_cast<unsigned int>(loader[0]) - 1u;
        reinterpret_cast<void(__cdecl *)(void *, void *, void *)>(loader[2])(
            reinterpret_cast<void *>((reinterpret_cast<unsigned int>(data) + align) & ~align), entry, pack);
    }
    bFree(data);
    data = nullptr;
    const int size = *reinterpret_cast<const int *>(entry + 8);
    --*reinterpret_cast<int *>(pack + 0x30);
    *reinterpret_cast<int *>(pack + 0x34) -= size;
    --loader[0xc];
    loader[0xd] -= size;
}

void __cdecl eStreamPackLoader::InternalLoadingHeaderPhase3Callback(void *infoObject)
{
    char *info = static_cast<char *>(infoObject);
    char *phase = *reinterpret_cast<char **>(info + 0x10);
    void *argument = *reinterpret_cast<void **>(phase + 0xc);
    void(__cdecl * callback)(void *) = *reinterpret_cast<void(__cdecl **)(void *)>(phase + 8);
    *reinterpret_cast<int *>(info + 0x14) = 1;
    bFree(phase);
    *reinterpret_cast<char **>(info + 0x10) = nullptr;
    if (callback)
        callback(argument);
}

void __cdecl AddReplacementTextureTableFixup(eReplacementTextureTable *table, int count)
{
    Fixup *free = nullptr;
    for (int i = 0; i < kFixupCount; ++i)
    {
        Fixup &f = kFixups[i];
        if (f.table == table)
        {
            ++f.references;
            return;
        }
        if (!free && !f.table)
            free = &f;
    }
    ++NumReplacementTextureTableFixups;
    free->table = table;
    free->count = static_cast<short>(count);
    free->references = 1;
}

void __cdecl RemoveReplacementTextureTableFixup(eReplacementTextureTable *table)
{
    for (int i = 0; i < kFixupCount; ++i)
    {
        Fixup &f = kFixups[i];
        if (f.table != table)
            continue;
        if (--f.references == 0)
        {
            f.table = nullptr;
            --NumReplacementTextureTableFixups;
        }
        return;
    }
}

void __cdecl eFixupReplacementTexturesAfterUnloading(TextureInfo *texture)
{
    for (int i = 0; i < kFixupCount; ++i)
    {
        Fixup &f = kFixups[i];
        if (!f.table)
            continue;
        ReplacementTexture *r = static_cast<ReplacementTexture *>(f.table);
        for (int k = 0; k < f.count; ++k)
            if (r[k].texture == texture)
                r[k].texture = reinterpret_cast<TextureInfo *>(-1);
    }
}

void eModel::AttachReplacementTextureTable(eReplacementTextureTable *table, int count, int)
{
    if (pReplacementTextureTable)
        RemoveReplacementTextureTableFixup(pReplacementTextureTable);
    pReplacementTextureTable = table;
    NumReplacementTextures = count;
    AddReplacementTextureTableFixup(table, count);
}

bVector4 *eModel::GetPivotPosition()
{
    return Solid ? reinterpret_cast<bVector4 *>(reinterpret_cast<char *>(Solid) + 0x70) : nullptr;
}

/** Smoothing normals across solids (eSmoorthNormals, eSmoothNormals, eSolid::SmoothNormals) ends in
 * eSolidPlatInterface::SetSmoothVertex, which does nothing on the PC: they only check their arguments. */
int __cdecl SmoothModelNormals(eModel **models, int count)
{
    if (!models || count >= 0x100 || count < 0)
        return 0;
    return 1;
}

int __cdecl SmoothSolidNormals(eSolid **solids, int count)
{
    return solids && count >= 0 ? 1 : 0;
}

void __fastcall SolidSmoothNormals(eSolid *, void *, void *, int)
{
}

void *__fastcall SolidPositionMarker(const char *self, void *, unsigned int name)
{
    char *markers = *reinterpret_cast<char *const *>(self + 0x80);
    const int count = static_cast<unsigned char>(self[0x1b]);
    if (!markers)
        return nullptr;
    for (int i = 0; i < count; ++i)
    {
        char *marker = markers + i * 0x50;
        if (*reinterpret_cast<const unsigned int *>(marker) == name)
            return marker;
    }
    return nullptr;
}

/** The first phase of a solid pack's header: its chunk headers swapped, and where its data begins and how long it is. */
void __cdecl SolidLoadingStreamingPackPhase1(char *info)
{
    char *header = *reinterpret_cast<char **>(info + 4);
    EndianSwapChunkHeader(reinterpret_cast<bChunk *>(header));
    EndianSwapChunkHeader(reinterpret_cast<bChunk *>(header + 8));
    char *data = header + *reinterpret_cast<int *>(header + 0xc) + 0x10;
    EndianSwapChunkHeader(reinterpret_cast<bChunk *>(data));
    *reinterpret_cast<int *>(info + 8) = static_cast<int>(data - header);
    *reinterpret_cast<int *>(info + 0xc) = *reinterpret_cast<int *>(data + 4) + 0x10;
}

void __cdecl TextureLoadingStreamingPackPhase1(eStreamingPackHeaderLoadingInfoPhase1 *info)
{
    SolidLoadingStreamingPackPhase1(reinterpret_cast<char *>(info));
}

void __cdecl SetSelectCarLighting(int, float, int)
{
}

void __cdecl SphericalToCartesian(bVector3 *out, float azimuth, float elevation, float radius)
{
    float cosA, sinA, cosE, sinE;
    bSinCos(&sinA, &cosA, static_cast<unsigned short>(static_cast<int>(azimuth * kDegreesToAngle) / 360));
    bSinCos(&sinE, &cosE, static_cast<unsigned short>(static_cast<int>(elevation * kDegreesToAngle) / 360));
    out->x = sinE * cosA * radius;
    out->y = sinE * sinA * radius;
    out->z = cosE * radius;
}

void __cdecl CartesianToSpherical(bVector3 *out, float x, float y, float z)
{
    const float radius = sqrtf(z * z + y * y + x * x);
    const unsigned short azimuth = bATan(x, y);
    const short elevation = static_cast<short>(bASin(z / radius));
    out->z = radius;
    out->x = static_cast<float>(azimuth);
    out->y = static_cast<float>(static_cast<unsigned short>(0x4000 - elevation));
}

eDynamicLightContext &eDynamicLightContext::operator=(const eDynamicLightContext &from)
{
    unsigned int *to = reinterpret_cast<unsigned int *>(this);
    const unsigned int *source = reinterpret_cast<const unsigned int *>(&from);
    to[0] = source[0];
    memcpy(to + 4, source + 4, (0x4c - 4) * 4);
    return *this;
}

int __cdecl elSetupEnvMap(eDynamicLightContext *contextObject, bMatrix4 *world, bMatrix4 *view, bVector4 *eye)
{
    char *context = reinterpret_cast<char *>(contextObject);
    bMatrix4 inverse;
    eInvertRotationMatrix(&inverse, world);
    if (eye)
    {
        const float *w = reinterpret_cast<const float *>(world);
        bVector4 d = {eye->x - w[12], eye->y - w[13], eye->z - w[14], 1.0f};
        eMulVector(&d, &inverse, &d);
        float most = TheGameFlowManager_CurrentGameFlowState == 3 ? 8.0f : 2.78f;
        const float length = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z + d.w * d.w);
        float distance;
        if (length < 2.78f)
            distance = 2.78f;
        else
            distance = length <= most ? length : most;
        bNormalize(reinterpret_cast<bVector3 *>(&d), reinterpret_cast<bVector3 *>(&d));
        float *e = reinterpret_cast<float *>(context + 0xd0);
        e[3] = 1.0f;
        e[2] = distance * d.z;
        e[0] = distance * d.x;
        e[1] = distance * d.y;
    }
    if (view && eye)
    {
        eMulMatrix(reinterpret_cast<bMatrix4 *>(context + 0xf0), world, view);
        float *m = reinterpret_cast<float *>(context + 0xf0);
        m[3] = m[7] = m[11] = 0.0f;
        memcpy(m + 12, context + 0xd0, 12);
        m[15] = 0.0f;
    }
    return 1;
}

void __cdecl elResetLightContext(eDynamicLightContext *context)
{
    memset(context, 0, 0x4c * 4);
}

int eView::GetPixelSize(const bVector3 *position, float radius)
{
    const char *camera = reinterpret_cast<const char *>(pCamera);
    const float *eye = reinterpret_cast<const float *>(camera + 0x40), *forward = reinterpret_cast<const float *>(camera + 0x50);
    const float dx = position->x - eye[0], dy = position->y - eye[1], dz = position->z - eye[2];
    // Wholly behind the camera.
    if (dz * forward[2] + dy * forward[1] + dx * forward[0] < -radius)
        return 0;
    const float distance =
        static_cast<float>(sqrt(static_cast<double>(dz) * dz + static_cast<double>(dy) * dy + static_cast<double>(dx) * dx) - radius);
    const float focal = *reinterpret_cast<const float *>(reinterpret_cast<const char *>(this) + 0xc);
    if (radius * 0.5f < distance)
        return static_cast<int>(focal / distance * radius);
    return static_cast<int>(focal + focal);
}

namespace
{
/** A pack's texture table: entries (hash, TextureInfo *) at its data +0x74, count at +0x18. */
TextureInfo *PackTexture(const TexturePack *pack, int i)
{
    const char *data = *reinterpret_cast<const char *const *>(reinterpret_cast<const char *>(pack) + 0x14);
    const char *table = *reinterpret_cast<const char *const *>(data + 0x74);
    return *reinterpret_cast<TextureInfo *const *>(table + i * 8 + 4);
}

int PackCount(const TexturePack *pack)
{
    return *reinterpret_cast<const int *>(reinterpret_cast<const char *>(pack) + 0x18);
}

template <typename T> T &Field(TextureInfo *t, int offset)
{
    return *reinterpret_cast<T *>(reinterpret_cast<char *>(t) + offset);
}
} // namespace

void TexturePack::AssignTextureData(char *data, int start, int size)
{
    int changed = 0;
    TextureInfo *last = nullptr;
    for (int i = 0; i < PackCount(this); ++i)
    {
        TextureInfo *t = PackTexture(this, i);
        if (!t)
            continue;
        bool assigned = false;
        if (Field<int>(t, 0x38) && Field<int>(t, 0x30) >= start && Field<int>(t, 0x30) < size + start)
        {
            Field<char *>(t, 0x70) = data + (Field<int>(t, 0x30) - start);
            assigned = true;
        }
        if (Field<int>(t, 0x3c) && Field<int>(t, 0x34) >= start && Field<int>(t, 0x34) < size + start)
        {
            Field<char *>(t, 0x74) = data + (Field<int>(t, 0x34) - start);
            assigned = true;
        }
        if (assigned && Field<void *>(t, 0x70))
        {
            reinterpret_cast<TextureInfoPlatInterface *>(t)->Init();
            ++changed;
            last = t;
        }
    }
    // The solids and fonts hear of the one texture that changed, or of the whole pack.
    eSolidNotifyTextureLoading(this, changed == 1 ? last : nullptr, true);
    FEngFontNotifyTextureLoading(this, true);
    eDirtyTextures = 1;
}

void TexturePack::UnAssignTextureData(int start, int size)
{
    int changed = 0;
    TextureInfo *last = nullptr;
    for (int i = 0; i < PackCount(this); ++i)
    {
        TextureInfo *t = PackTexture(this, i);
        if (!t)
            continue;
        bool removed = false;
        if (Field<int>(t, 0x38) && start <= Field<int>(t, 0x30) && Field<int>(t, 0x30) < size + start)
        {
            removed = true;
            Field<void *>(t, 0x70) = nullptr;
        }
        if (Field<int>(t, 0x3c) && start <= Field<int>(t, 0x34) && Field<int>(t, 0x34) < size + start)
        {
            removed = true;
            Field<void *>(t, 0x74) = nullptr;
        }
        if (Field<void *>(t, 0x70) || !removed)
            continue;
        reinterpret_cast<TextureInfoPlatInterface *>(t)->Close();
        const unsigned int hash = t->NameHash;
        reinterpret_cast<TextureInfo **>(&TextureInfoCache)[hash & 0xff] = nullptr;
        if (hash == 0x530b82b0 || hash == 0x530b82b1 || hash == 0x530b82b2 || hash == 0x530b82b3)
            FlushFromSkinCompositeCache(hash);
        ++changed;
        last = t;
    }
    eSolidNotifyTextureLoading(this, changed == 1 ? last : nullptr, false);
    FEngFontNotifyTextureLoading(this, false);
    eDirtyTextures = 1;
}

/** TextureInfo's set-up from a pack's header entry (thiscall): its name, size as powers of two, format and data. Ten
 * arguments, the last unused (ret 0x28): with nine the movie texture's set-up (PlatSetFirstMovieFrame 0x6e1880, the one
 * caller) returned to the wrong place and every movie crashed at its first frame. */
void __fastcall InitTextureInfo(char *t, void *, const unsigned int *name, void *data, unsigned int width, int height,
                                int format, void *palette, unsigned short flags, unsigned char alphaBits, unsigned char shift,
                                int)
{
    unsigned char widthShift = 0, heightShift = 0;
    while (static_cast<int>(width) > 1 && (1 << widthShift) < static_cast<int>(width))
        ++widthShift;
    while (height > 1 && (1 << heightShift) < height)
        ++heightShift;
    int bytes = 0;
    switch (format)
    {
    case 4:
    case 0x21:
    case 0x22:
    case 0x24:
        bytes = static_cast<int>((width - 1) & 1) * height;
        break;
    case 8:
    case 0x80:
    case 0x81:
        bytes = static_cast<int>(width) * height;
        break;
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
        bytes = static_cast<int>(width) * height * 2;
        break;
    case 0x18:
        bytes = static_cast<int>(width) * height * 3;
        break;
    case 0x20:
        bytes = static_cast<int>(width) * height * 4;
        break;
    default:
        break;
    }
    memcpy(t + 0xc, name, 24);
    t[0x23] = 0;
    *reinterpret_cast<unsigned int *>(t + 0x24) = bStringHash(reinterpret_cast<const char *>(name));
    t[0x4a] = static_cast<char>(format);
    *reinterpret_cast<short *>(t + 0x44) = static_cast<short>(width);
    *reinterpret_cast<unsigned short *>(t + 0x4c) = flags;
    t[0x49] = static_cast<char>(heightShift);
    t[0x48] = static_cast<char>(widthShift);
    t[0x55] = static_cast<char>(alphaBits);
    *reinterpret_cast<short *>(t + 0x62) = 0x100;
    *reinterpret_cast<short *>(t + 0x64) = 0x100;
    *reinterpret_cast<int *>(t + 0x40) = bytes;
    *reinterpret_cast<int *>(t + 0x38) = bytes;
    *reinterpret_cast<short *>(t + 0x46) = static_cast<short>(height);
    *reinterpret_cast<int *>(t + 0x30) = 0;
    *reinterpret_cast<int *>(t + 0x34) = 0;
    *reinterpret_cast<int *>(t + 0x3c) = 0;
    t[0x4b] = 0;
    t[0x54] = 0;
    t[0x51] = 0;
    t[0x4f] = 0;
    t[0x57] = 0;
    t[0x52] = 0;
    *reinterpret_cast<short *>(t + 0x5e) = 0;
    *reinterpret_cast<short *>(t + 0x60) = 0;
    *reinterpret_cast<int *>(t + 0x68) = 0;
    t[0x4e] = 1;
    t[0x56] = static_cast<char>(shift);
    *reinterpret_cast<char **>(t + 0x6c) = t;
    *reinterpret_cast<void **>(t + 0x70) = data;
    *reinterpret_cast<void **>(t + 0x74) = palette;
    *reinterpret_cast<int *>(t + 0x78) = 1;
}

/** A texture animation's fraction (stdcall): mode 0 none, else the fraction of `time * rate`, in whole steps of `step`
 * first for mode 2. */
float __stdcall AnimationFraction(float time, float rate, int mode, float step)
{
    if (mode == 0)
        return 0.0f;
    double t = time;
    if (mode == 2)
        t = static_cast<double>(static_cast<int>(static_cast<double>(time) / step));
    t *= rate;
    return static_cast<float>(t - static_cast<int>(t));
}

int __cdecl LoaderSun(bChunk *chunkObject)
{
    const unsigned int *chunk = reinterpret_cast<const unsigned int *>(chunkObject);
    if (chunk[0] != kSunChunk)
        return 0;
    const unsigned int start = (reinterpret_cast<unsigned int>(chunk) + 0x17) & ~0xfu;
    SunInfoTable = reinterpret_cast<SunChunkInfo *>(start);
    NumSunInfo = static_cast<int>((chunk[1] - start + 8 + reinterpret_cast<unsigned int>(chunk)) / kSunInfoSize);
    return 1;
}

int __cdecl UnloaderSun(bChunk *chunkObject)
{
    if (*reinterpret_cast<const unsigned int *>(chunkObject) != kSunChunk)
        return 0;
    SunInfoTable = nullptr;
    NumSunInfo = 0;
    return 1;
}

void __cdecl SunTrackUnloader()
{
    for (int i = 0; i < 5; ++i)
        At<void *>(0x915a18 + i * 4) = nullptr;
}

void *DefragFixer::Fix(void *pointer)
{
    const unsigned int *self = reinterpret_cast<const unsigned int *>(this);
    const unsigned int p = reinterpret_cast<unsigned int>(pointer);
    if (p < self[0] || self[1] <= p)
        return pointer;
    const int count = static_cast<int>(self[2]);
    for (int i = 0; i < count; ++i)
    {
        const unsigned int *range = self + i * 3;
        if (range[3] <= p && p < range[4])
            return reinterpret_cast<void *>(range[5] + p);
    }
    return pointer;
}

namespace
{
void *ConstructAttribs(void *self, const Attrib::Collection *collection, unsigned int key, UTL::COM::IUnknown *owner, unsigned int size)
{
    NFS_ORIGINAL(0x452380, void *(__thiscall *)(void *, const Attrib::Collection *, unsigned int, UTL::COM::IUnknown *))(self, collection, key, owner);
    const void *&data = *reinterpret_cast<const void **>(static_cast<char *>(self) + 8);
    if (!data)
        data = Attrib::DefaultDataArea(size);
    return self;
}
} // namespace

void *__fastcall ConstructEmitterGroupAttribs(void *self, void *, const Attrib::Collection *collection, unsigned int key, UTL::COM::IUnknown *owner)
{
    return ConstructAttribs(self, collection, key, owner, 0x88);
}

void *__fastcall ConstructEmitterDataAttribs(void *self, void *, const Attrib::Collection *collection, unsigned int key, UTL::COM::IUnknown *owner)
{
    return ConstructAttribs(self, collection, key, owner, 0x118);
}

void __cdecl FreeFastBlock(void *block, unsigned int size)
{
    if (block)
        NFS_ORIGINAL(0x5d0370, void(__thiscall *)(void *, void *, unsigned int, const char *))(reinterpret_cast<void *>(kFastMem), block, size, nullptr);
}

void __cdecl FreeEmitter(void *emitter)
{
    bFree(EmitterSlotPool, emitter);
}

void __cdecl FreeEmitterGroup(void *group)
{
    bFree(EmitterGroupSlotPool, group);
}

void *__fastcall EmitterPlatData(char *self, void *)
{
    return self + 0x50;
}

void __cdecl NotifyLibOfDeletion(void *library, EmitterGroup *)
{
    *reinterpret_cast<void **>(static_cast<char *>(library) + 0xc) = nullptr;
}

void __cdecl CollapseFXSlotPools()
{
    SlotPool *pools[3] = {ParticleSlotPool, EmitterSlotPool, EmitterGroupSlotPool};
    for (SlotPool *p : pools)
        *reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(p) + 0x14) &= ~1u;
    for (SlotPool *p : pools)
        *reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(p) + 0x14) |= 4u;
    SlotPool *order[3] = {EmitterGroupSlotPool, EmitterSlotPool, ParticleSlotPool};
    for (SlotPool *p : order)
    {
        const char *s = reinterpret_cast<const char *>(p);
        if (*reinterpret_cast<const int *>(s + 0x2c) != *reinterpret_cast<const int *>(s + 0x24))
            p->CleanupExpandedSlotPools();
    }
}

void __cdecl GetFloatColor(unsigned int colour, bVector4 &out)
{
    const float k = 0.003921569f;
    out.x = static_cast<float>(colour >> 24) * k;
    out.y = static_cast<float>(colour >> 16 & 0xff) * k;
    out.z = static_cast<float>(colour >> 8 & 0xff) * k;
    out.w = static_cast<float>(colour & 0xff) * k;
}

void EmitterDataAttribWrapper::CalculateBases()
{
    char *self = reinterpret_cast<char *>(this);
    if (!*reinterpret_cast<void **>(self + 4))
        return;
    const char *a = *reinterpret_cast<const char *const *>(self + 8);
    auto f = [a](int offset) { return *reinterpret_cast<const float *>(a + offset); };
    auto u = [a](int offset) { return *reinterpret_cast<const unsigned int *>(a + offset); };
    bVector4 colours[4];
    GetFloatColor(u(0xac), colours[0]);
    GetFloatColor(u(0xe4), colours[1]);
    GetFloatColor(u(0xb0), colours[2]);
    GetFloatColor(u(0xb4), colours[3]);
    float sizes[16] = {};
    const float k0 = f(0x50) * 0.33333334f, k1 = f(0x54) * 0.33333334f, k2 = f(0x58) * 0.33333334f, k3 = f(0x5c) * 0.33333334f;
    for (int i = 0; i < 4; ++i)
    {
        sizes[i * 4] = f(0x40 + i * 4) * 0.5f;
        sizes[i * 4 + 1] = f(0x20 + i * 4) * 182.04445f;
    }
    hermite_basis(reinterpret_cast<bMatrix4 *>(self + 0x20), reinterpret_cast<bMatrix4 *>(colours), k0, k1, k2, k3);
    hermite_basis(reinterpret_cast<bMatrix4 *>(self + 0x60), reinterpret_cast<bMatrix4 *>(sizes), k0, k1, k2, k3);
}
