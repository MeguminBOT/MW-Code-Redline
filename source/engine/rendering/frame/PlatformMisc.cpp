/**
 * The platform layer's small parts (original: zPlatform): view info lookup, the immediate strip builder, texture platform
 * info, scenery instance matrices, the sky model's set-up, the front end's environment map textures, the sun's quad, the
 * disc check, the visual treatment's curve and the depth a bound sorts at.
 */
#include <string.h>
#include <windows.h>

#include "game/gen/api.h"
#include "legacy/link.h"

extern "C" char *e_current_strip;
extern "C" int e_current_strip_vert;
extern "C" int e_current_strip_uv;
extern "C" int e_current_strip_col;
extern "C" TextureInfo *e_strip_texture_info;
extern "C" float e_strip_matrix;
extern "C" SlotPool *eAnimTextureSlotPool;
extern "C" int CurrentViewMode;
extern "C" char bSkyTexturesLoaded;
extern "C" unsigned int BaseSkyHash;
extern "C" unsigned int SkyHash;
extern "C" float SkydomeLocalWorld;
extern "C" int lpData_009017f4;

NFS_REPLACE(0x6beb80, FlipBgr)
NFS_REPLACE(0x6be9e0, ViewPlatInfo)
NFS_REPLACE(0x6bf690, InitFEEnvMap)
NFS_REPLACE(0x6bf720, RemoveFEEnvMap)
NFS_REPLACE(0x6beeb0, InstanceGetMatrix)
NFS_REPLACE(0x6bef90, InstanceSetMatrix)
NFS_REPLACE(0x6bfd00, SetEnvironmentSource)
NFS_REPLACE(0x6c0160, MakeSunQuad)
NFS_REPLACE(0x6c0ab0, SkyInitModel)
NFS_REPLACE(0x6c0b60, InitSkyHash)
NFS_REPLACE(0x6c0c30, UnInitSceneryModels)
NFS_REPLACE(0x6c0c70, NotifySkyUnloader)
NFS_REPLACE(0x6c13f0, WorldDetailLevel)
NFS_REPLACE(0x6c1410, RunLoaderThread)
NFS_REPLACE(0x6c25a0, DiscInDrive)
NFS_REPLACE(0x6c2620, FindDisc)
NFS_REPLACE(0x6c2700, CheckDisc)
NFS_REPLACE(0x6c3440, TreatmentCurve)
NFS_REPLACE(0x6c39d0, CloseVisualTreatment)
NFS_REPLACE(0x6c6770, SetParticleStats)
NFS_REPLACE(0x6c6f30, BoundSortDepth)

/** The garage face callback (EnvMap.cpp), so the loads reach the port and not the original Direct3D 9 code. */
extern "C" void __cdecl EnvMapTextureLoadedCallback();

namespace
{
template <typename T> T &At(unsigned int address)
{
    return *reinterpret_cast<T *>(address);
}

const unsigned int kViewPlatInfos = 0x984130;
const int kViewPlatInfoSize = 0x1a0;
const int kStripVertexSize = 0x18;
/** A scenery instance's rotation, in 1/8192 steps. */
const float kRotationStep = 0.00012207031f;
const unsigned int kStripSlotPool = 0x982ac0;
const unsigned int kSkyModel = 0x986f50;
/** The garages' environment map faces: their names' suffixes (XPOS ... ZNEG), from 0x8f9314. */
const char *const *const kEnvMapFaces = reinterpret_cast<const char *const *>(0x8f9314);
const int kEnvMapFaceCount = 6;
const char kEnvMapFormat[] = "%s_%s";

void Identity(float *m)
{
    for (int i = 0; i < 16; ++i)
        m[i] = i % 5 == 0 ? 1.0f : 0.0f;
}

void UnInitModels(unsigned int name)
{
    if (!FindSceneryInstance(name))
        return;
    eModel **models = reinterpret_cast<eModel **>(reinterpret_cast<char *>(FindSceneryInfo(name)) + 0x28);
    for (int i = 0; i < 4; ++i)
        if (models[i])
            models[i]->UnInit();
}

/** The name of a garage's environment map face (`type`'s prefix and the face's suffix). */
unsigned int EnvMapFaceHash(int type, int face, char *buffer)
{
    const char *prefix = FEManager::Get()->GetGaragePrefixFromType(static_cast<eGarageType>(type));
    bSPrintf(buffer, kEnvMapFormat, prefix, kEnvMapFaces[face]);
    return bStringHash(buffer);
}

void UnloadIfLoaded(unsigned int hash)
{
    if (GetTextureInfo(hash, 0, 0))
        eUnloadStreamingTexture(&hash, 1);
}
} // namespace

eViewPlatInfo *__cdecl ViewPlatInfo(int view)
{
    return reinterpret_cast<eViewPlatInfo *>(kViewPlatInfos + view * kViewPlatInfoSize);
}

bool __cdecl eBeginStrip(TextureInfo *texture, int, bMatrix4 *matrix)
{
    e_current_strip = static_cast<char *>(At<SlotPool *>(kStripSlotPool)->Malloc());
    e_current_strip_vert = 0;
    e_current_strip_uv = 0;
    e_current_strip_col = 0;
    e_strip_texture_info = texture;
    float *m = &e_strip_matrix;
    if (matrix)
        memcpy(m, matrix, sizeof(float) * 16);
    else
        Identity(m);
    return true;
}

unsigned int __cdecl FlipBgr(const unsigned char *c)
{
    return static_cast<unsigned int>(c[3]) << 24 | static_cast<unsigned int>(c[0]) << 16 | static_cast<unsigned int>(c[1]) << 8 | c[2];
}

void __cdecl exAddColour(unsigned int colour)
{
    // ARGB to the strip's ABGR.
    const unsigned int swapped = (colour & 0xff00ff00u) | (colour >> 16 & 0xff) | (colour & 0xff) << 16;
    *reinterpret_cast<unsigned int *>(e_current_strip + 0x10 + e_current_strip_col * kStripVertexSize) = swapped;
    ++e_current_strip_col;
}

void __cdecl exAddUV(float u, float v)
{
    float *uv = reinterpret_cast<float *>(e_current_strip + 0x14 + e_current_strip_uv * kStripVertexSize);
    uv[0] = u;
    uv[1] = v;
    ++e_current_strip_uv;
}

void __cdecl exAddVertex(const bVector3 &v)
{
    float *p = reinterpret_cast<float *>(e_current_strip + 4 + e_current_strip_vert * kStripVertexSize);
    p[0] = v.x;
    p[1] = v.y;
    p[2] = v.z;
    ++e_current_strip_vert;
}

void TextureInfoPlatInterface::SetPlatInfo(TextureInfoPlatInfo *info)
{
    *reinterpret_cast<TextureInfoPlatInfo **>(this) = info;
}

void *TextureInfoPlatInterface::LockPalette(TextureLockType)
{
    return *reinterpret_cast<void **>(reinterpret_cast<char *>(this) + 0x74);
}

void TextureInfoPlatInterface::UnlockPalette(void *)
{
}

void *TextureInfoPlatInterface::CreateAnimData()
{
    const char *info = *reinterpret_cast<const char *const *>(this);
    void **data = static_cast<void **>(bOMalloc(eAnimTextureSlotPool));
    *data = *reinterpret_cast<void *const *>(info + 0x18);
    return data;
}

void TextureInfoPlatInterface::ReleaseAnimData(void *data)
{
    bFree(eAnimTextureSlotPool, data);
}

void __fastcall InstanceGetMatrix(const char *instance, void *, bMatrix4 *out)
{
    const short *rotation = reinterpret_cast<const short *>(instance + 0x2c);
    float *m = reinterpret_cast<float *>(out);
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
            m[row * 4 + column] = static_cast<float>(rotation[row * 3 + column]) * kRotationStep;
        m[row * 4 + 3] = 0.0f;
    }
}

void __fastcall InstanceSetMatrix(const char *instance, void *, bMatrix4 *out)
{
    InstanceGetMatrix(instance, nullptr, out);
    float *m = reinterpret_cast<float *>(out);
    const float *position = reinterpret_cast<const float *>(instance + 0x20);
    m[12] = position[0];
    m[13] = position[1];
    m[14] = position[2];
    m[15] = 1.0f;
}

EVIEWMODE __cdecl eGetCurrentViewMode()
{
    return static_cast<EVIEWMODE>(CurrentViewMode);
}

void __cdecl eFixUpTablesPlat()
{
    At<TextureInfo *>(0x982b48) = GetTextureInfo(bStringHash("RVM"), 0, 0);
    At<TextureInfo *>(0x982b4c) = GetTextureInfo(bStringHash("RVM_MASK"), 0, 0);
    At<TextureInfo *>(0x982b50) = GetTextureInfo(bStringHash("WHITE16X16_NOALPHA"), 0, 0);
}

void __cdecl InitFEEnvMap()
{
    if (!At<char>(0x8f945b))
        return;
    At<char>(0x93e86c) = 0;
    for (int face = 0; face < kEnvMapFaceCount; ++face)
    {
        char name[64];
        unsigned int hash = EnvMapFaceHash(FEManager::Get()->GetGarageType(), face, name);
        eLoadStreamingTexture(&hash, 1, reinterpret_cast<void(__cdecl *)(unsigned int)>(&EnvMapTextureLoadedCallback), 0, 0);
    }
}

void __cdecl RemoveFEEnvMap()
{
    for (int face = 0; face < kEnvMapFaceCount; ++face)
    {
        char name[64];
        const int previous = *reinterpret_cast<const int *>(reinterpret_cast<const char *>(FEManager::Get()) + 0x1c);
        if (previous)
            UnloadIfLoaded(EnvMapFaceHash(previous, face, name));
        if (FEManager::Get()->GetGarageType())
            UnloadIfLoaded(EnvMapFaceHash(FEManager::Get()->GetGarageType(), face, name));
    }
}

/** The original turned a matrix by the source's heading and threw it away: only the source is kept. */
void __cdecl SetEnvironmentSource(void *source, char *target)
{
    *reinterpret_cast<void **>(target + 0x80) = source;
}

/** One sun layer's quad (`layer` as in source/engine/rendering/sky/Sun.cpp): turned as the sun crosses the screen, its colour and alpha
 * (the cloud cover dims it), at the far plane. */
void __fastcall MakeSunQuad(const char *sun, void *, ePoly *poly, const char *layer, float largest, float x, float y, float cloud)
{
    const float intensity = *reinterpret_cast<const float *>(layer + 8);
    const float size = *reinterpret_cast<const float *>(layer + 0xc);
    const float alpha = (1.0f - cloud) * intensity;
    if (*reinterpret_cast<const int *>(layer) == 0 && size > largest)
        largest = size;
    const double sweep = (static_cast<double>(largest) + x) / (static_cast<double>(largest) + largest + 640.0) *
                         *reinterpret_cast<const float *>(layer + 0x20) * -65536.0;
    const unsigned short angle = static_cast<unsigned short>(*reinterpret_cast<const unsigned short *>(layer + 0x1c) -
                                                             static_cast<unsigned short>(static_cast<int>(sweep)));
    const float half = size * 0.5f;
    const float s = bSin(angle);
    const double c = bCos(angle);
    const double ch = c * half, sh = static_cast<double>(s) * half;
    const float a = static_cast<float>(ch + sh);
    const double b = ch - sh;
    const double cx = static_cast<double>(x) + *reinterpret_cast<const float *>(layer + 0x10);
    const float cy = y + *reinterpret_cast<const float *>(layer + 0x14);
    const double scaled = intensity < 0.0f ? alpha * -256.0 : static_cast<double>(alpha) * *reinterpret_cast<const float *>(sun + 0x18) * 256.0;
    const unsigned char alphaByte = static_cast<unsigned char>(static_cast<int>(scaled));
    float *v = reinterpret_cast<float *>(poly);
    v[0] = static_cast<float>(cx - a);
    v[1] = static_cast<float>(cy - b);
    v[4] = static_cast<float>(cx + b);
    v[5] = cy - a;
    v[8] = static_cast<float>(a + cx);
    v[9] = static_cast<float>(cy + b);
    v[12] = static_cast<float>(cx - b);
    v[13] = cy + a;
    v[2] = v[6] = v[10] = v[14] = 0.999f;
    unsigned char *colours = reinterpret_cast<unsigned char *>(poly) + 0x80;
    for (int i = 0; i < 4; ++i)
    {
        colours[i * 4 + 0] = static_cast<unsigned char>(layer[0x18]);
        colours[i * 4 + 1] = static_cast<unsigned char>(layer[0x19]);
        colours[i * 4 + 2] = static_cast<unsigned char>(layer[0x1a]);
        colours[i * 4 + 3] = alphaByte;
    }
}

ePoly::ePoly()
{
    unsigned int *p = reinterpret_cast<unsigned int *>(this);
    const unsigned int one = 0x3f800000, sixtyFour = 0x42800000, grey = 0x80808080;
    p[0] = p[1] = p[2] = 0;
    p[4] = sixtyFour;
    p[5] = p[6] = 0;
    p[8] = p[9] = sixtyFour;
    p[10] = 0;
    p[12] = 0;
    p[13] = sixtyFour;
    p[14] = 0;
    p[16] = p[17] = 0;
    p[18] = one;
    p[19] = 0;
    p[20] = p[21] = one;
    p[22] = 0;
    p[23] = one;
    p[32] = p[33] = p[34] = p[35] = grey;
    reinterpret_cast<unsigned char *>(this)[0x90] = 0;
}

void __fastcall InstanceSetMatrix(const char *instance, void *, bMatrix4 *out);

int __cdecl SkyInitModel(eModel *model, bMatrix4 *matrix, unsigned int name)
{
    if (*reinterpret_cast<void **>(reinterpret_cast<char *>(model) + 8))
        return 1;
    if (!NFS_ORIGINAL(0x505340, void *(__cdecl *)(unsigned int))(name))
        return 0;
    model->Init(name);
    Identity(reinterpret_cast<float *>(matrix));
    if (SceneryInstance *instance = FindSceneryInstance(name))
    {
        InstanceSetMatrix(reinterpret_cast<const char *>(instance), nullptr, matrix);
        eModel **models = reinterpret_cast<eModel **>(reinterpret_cast<char *>(FindSceneryInfo(name)) + 0x28);
        for (int i = 0; i < 4; ++i)
            if (models[i])
                models[i]->UnInit();
    }
    return 1;
}

void __cdecl InitSkyHash()
{
    if (bSkyTexturesLoaded)
        return;
    bSkyTexturesLoaded = 1;
    BaseSkyHash = bStringHash("SKY_MIDDAY_A_CLOUDS_A");
    At<unsigned int>(0x93e868) = bStringHash("SKY_MIDDAY_A_CAP_A");
    SkyHash = bStringHash("SKY_MIDDAY_A_GRADIENT_A");
    static const char *const kLayers[] = {"SKY_MIDDAY_A_CAP_A",    "SKY_MIDDAY_A_CLOUDS_A",       "SKY_MIDDAY_A_CAP_A",
                                          "SKY_MIDDAY_A_CLOUDS_B", "SKY_MIDDAY_A_CAP_B",          "SKY_MIDDAY_A_ENVIRONMENT_A",
                                          "SKY_MIDDAY_A_ENVIRONMENT_CAP_A", "SKY_MIDDAY_A_REFLECTION", "SKY_MIDDAY_A_REFLECTION"};
    for (int i = 0; i < 9; ++i)
        At<unsigned int>(0x98287c + i * 4) = bStringHash(kLayers[i]);
}

void __cdecl UnInitSceneryModels(unsigned int name)
{
    UnInitModels(name);
}

void __cdecl NotifySkyUnloader()
{
    reinterpret_cast<eModel *>(kSkyModel)->UnInit();
    Identity(&SkydomeLocalWorld);
    At<int>(0x9829f0) = 0;
    At<int>(0x9829f4) = 0;
    At<int>(0x9829f8) = 0;
    At<char>(0x982bcc) = 0;
}

/** The world detail the culling uses: 2 (full) from the video option's two highest levels, 1 at its second, else 0. */
int __cdecl WorldDetailLevel()
{
    if (lpData_009017f4 == 3 || lpData_009017f4 == 2)
        return 2;
    return lpData_009017f4 == 1;
}

void __cdecl RunLoaderThread(int run)
{
    HANDLE thread = At<HANDLE>(0x9bb168);
    if (run)
        ResumeThread(thread);
    else
        SuspendThread(thread);
}

namespace
{
void *FastAlloc(int bytes)
{
    return NFS_ORIGINAL(0x652ad0, void *(__cdecl *)(int))(bytes);
}

void FastFree(void *p)
{
    NFS_ORIGINAL(0x652b00, void(__cdecl *)(void *))(p);
}

/** The drive letter the game's disc was last found in (0: none), and whether to accept any drive type. */
char &DiscDrive()
{
    return At<char>(0x8f945c);
}

const unsigned int kAnyDriveType = 0x8a9260;
} // namespace

bool __cdecl DiscInDrive(char drive)
{
    char *path = static_cast<char *>(FastAlloc(0x400));
    if (!path)
        return false;
    const char saved = DiscDrive();
    DiscDrive() = 0;
    path[0] = drive;
    path[1] = ':';
    path[2] = '\\';
    path[3] = 0;
    memcpy(path + 3, "bin.dat", 8);
    const bool found = bFileExists(path) != 0;
    DiscDrive() = saved;
    FastFree(path);
    return found;
}

char __cdecl FindDisc()
{
    const DWORD drives = GetLogicalDrives();
    char *root = static_cast<char *>(FastAlloc(0x400));
    DiscDrive() = 0;
    if (!root)
        return 0;
    char drive = 'A';
    bool found = false;
    for (int i = 0; i < 32; ++i, ++drive)
    {
        if (!(drives & (1u << i)))
            continue;
        wsprintfA(root, "%c:\\", drive);
        const UINT type = GetDriveTypeA(root);
        if ((type == DRIVE_CDROM || At<int>(kAnyDriveType)) && type != DRIVE_REMOTE && DiscInDrive(drive))
        {
            found = true;
            break;
        }
    }
    FastFree(root);
    if (!found)
    {
        DiscDrive() = 0;
        return 0;
    }
    DiscDrive() = drive;
    NFS_ORIGINAL(0x6cba30, void(__cdecl *)(char))(drive);
    NFS_ORIGINAL(0x6cb9d0, void(__cdecl *)())();
    return drive;
}

bool __cdecl CheckDisc()
{
    int &failed = At<int>(0x9018bc);
    const bool found = failed ? FindDisc() != 0 : DiscInDrive(DiscDrive());
    failed = found ? 0 : 1;
    return found || At<int>(kAnyDriveType) != 0;
}

/** The visual treatment's tone curve: x through a knee at the global split point (at least 0.1), `slope` its steepness. */
float __cdecl TreatmentCurve(float x, float slope)
{
    if (x < 0.0f || x == 0.0f)
        return 0.0f;
    if (x > 1.0f)
        return 1.0f;
    float &split = At<float>(0x8f9b0c);
    if (split < 0.1f)
        split = 0.1f;
    const float k = static_cast<float>((1.0 / slope - 2.0) * (1.0 - 1.0 / split * x));
    if (x < split)
        return static_cast<float>(x / (static_cast<double>(k) + 1.0));
    return static_cast<float>((static_cast<double>(k) - x) / (static_cast<double>(k) - 1.0));
}

void __cdecl CloseVisualTreatment()
{
}

/** DrawXenonEmitters on the PC: keeps the particle statistics it was handed (eight words). */
void __cdecl SetParticleStats(int a, int b, int c, int d, int e, int f, int g, int h)
{
    int *stats = &At<int>(0x987b34);
    stats[0] = a;
    stats[1] = b;
    stats[2] = c;
    stats[3] = d;
    stats[4] = e;
    stats[5] = f;
    stats[6] = g;
    stats[7] = h;
}

/** How deep a bound's centre lies in the current view, for sorting (0 unless `skip` is 0). */
int __cdecl BoundSortDepth(int skip, const float *bound, const bMatrix4 *matrix)
{
    if (skip)
        return 0;
    const int view = At<int *>(0x982a20)[1];
    const char *v = reinterpret_cast<const char *>(0x9195e0 + view * 0x70);
    bVector3 low = {bound[0], bound[1], bound[2]}, high = {bound[3], bound[4], bound[5]};
    NFS_ORIGINAL(0x6be800, void(__cdecl *)(const bMatrix4 *, bVector3 *, bVector3 *))(matrix, &low, &high);
    const bVector3 centre = {(high.x + low.x) * 0.5f, (high.y + low.y) * 0.5f, (high.z + low.z) * 0.5f};
    bVector3 inView;
    const char *plat = *reinterpret_cast<const char *const *>(v);
    bMulMatrix(&inView, reinterpret_cast<const bMatrix4 *>(plat + 0x80), &centre);
    const char *camera = *reinterpret_cast<const char *const *>(v + 0x40);
    return static_cast<int>(*reinterpret_cast<const float *>(camera + 0xc0) - inView.z);
}
