/**
 * The game's global data at boot (LoadGlobalChunks 0x664780) and a race's start of loading (RegionLoader::BeginLoading
 * 0x666de0), as the originals do them but without EA's memory files (GLOBAL\GlobalMemoryFile.bin and
 * InGameMemoryFile.bin, read whole and searched before every file): the precache (engine/resources/precache.h) reads
 * ahead instead, its boot phase let go where the global memory file was and its race phase from the race's loading
 * until it has finished (FinishedGameLoading 0x6627e0). After the global data come the rebuild's own: the realistic
 * car performance when the settings ask for it from the start, the next generation sky, the time of day's night sky
 * and the added cars' front end logos.
 */
#include <string.h>
#include <windows.h>

#include "game/gen/api.h"
#include "legacy/link.h"
#include "core/log.h"
#include "engine/rendering/x360/frame.h"
#include "engine/resources/attribtables.h"
#include "engine/resources/pack.h"
#include "engine/resources/globalload.h"
#include "engine/resources/precache.h"
#include "gameplay/vehicles/realism.h"
#include "gameplay/world/timeofday.h"

namespace
{
typedef ResourceFile *(__cdecl *CreateResourceFile)(const char *, int, int, int, int);
const CreateResourceFile kCreateResourceFile = NFS_ORIGINAL(0x65fd30, CreateResourceFile);
const auto kFree = NFS_ORIGINAL(0x4655f0, void(__cdecl *)(void *));

/** The packs LoadGlobalChunks names through pointers the game keeps (CARS\BRAKES\GEOMETRY.BIN, ...). */
const char *Named(uintptr_t pointer)
{
    return *reinterpret_cast<const char *const *>(pointer);
}
const uintptr_t kBrakesGeometry = 0x8f7ec4;
const uintptr_t kBrakesTextures = 0x8f7ec8;
const uintptr_t kDynTex = 0x8f7ed4;
const uintptr_t kCarTextures = 0x8f7ecc;
const uintptr_t kWheelTextures = 0x8f7ed0;
const uintptr_t kSolidPacks[] = {0x8f7eac, 0x8f7eb0, 0x8f7eb4, 0x8f7eb8, 0x8f7ebc, 0x8f7ec0};
const unsigned int kLoadingTexture = 0xab0e817d;

/** CarLoaderPoolSizes: KB of the car loader's pool, normally and in split screen. */
int &PoolSize(int index)
{
    return reinterpret_cast<int *>(0x8f7ef0)[index];
}
const int kTrackPool = 0x7ef4000;
const int kTrackPoolEmergency = 0x7eb4000;
const int kEmergencyCarPool = 0x40000;
const int kEmergencyRacePool = 0x100;
const int kCompressedTexturePools = 7;

int &JoylogCapturing()
{
    return *reinterpret_cast<int *>(0x9258d8);
}
int &JoylogReplaying()
{
    return *reinterpret_cast<int *>(0x9258d4);
}
JoylogBuffer *JoylogCapture()
{
    return *reinterpret_cast<JoylogBuffer **>(0x9258e0);
}
const int kJoylogSunset = 8;

/** The front end's vault, its data and the flag that has it made with the high allocator. */
void *&FrontEndVault()
{
    return *reinterpret_cast<void **>(0x9258a8);
}
unsigned char **const kFrontEndVaultData = reinterpret_cast<unsigned char **>(0x9258ac);
bool &FrontEndVaultHigh()
{
    return *reinterpret_cast<bool *>(0x9258b0);
}
void *&AttribAllocator()
{
    return *reinterpret_cast<void **>(0x8f7ea8);
}
const uintptr_t kHighAttribAllocVtable = 0x8a8998;
const unsigned int kFrontEndVaultSlots = 0x40;

void WaitForResources()
{
    for (;;)
    {
        WaitForResourceLoadingComplete();
        if (!NumResourcesBeingLoaded && !NumDelayedResourceCallbacks)
            break;
        THREAD_yield(8);
    }
}

void LoadResource(const char *path, int type)
{
    kCreateResourceFile(path, type, 0, 0, 0)->BeginLoading(nullptr, nullptr);
}
} // namespace

void nfs::globalload::LoadResourceFile(const char *path)
{
    if (!bFileExists(path))
    {
        nfs::Log("global data: no %s", path);
        return;
    }
    ResourceFile *file = kCreateResourceFile(path, 1, 0, 0, 0);
    if (!file)
        return;
    nfs::Log("global data: %s loading", path);
    file->BeginLoading(nullptr, nullptr);
    WaitForResources();
}

int LoadLightMaterials(const unsigned char *data, unsigned int size);
int LoadTextureAnims(const unsigned char *data, unsigned int size);

namespace
{
/** A record table of ours (tools/assetc/catalog.py RECORD_FILES) read and kept for the run: its records, their size. */
char *Records(const char *path, uint32_t &size)
{
    uint8_t *data = nfs::pack::ReadWhole(path, size);
    if (!data)
        return nullptr;
    char *kept = static_cast<char *>(bMalloc(static_cast<int>(size ? size : 1), 0x2000));
    memcpy(kept, data, size);
    ::operator delete(data);
    return kept;
}

/** GLOBALB's record tables, as their chunk loaders set them: the track infos (0x120 each), the sun infos (0x110),
 * the preset rides (0x290, linked into PresetCarList), the car slots' animation hookups and hidden slots. */
void LoadRecordTables()
{
    uint32_t size = 0;
    if (char *t = Records("GLOBAL\\TRACKINFO.REC", size))
    {
        *reinterpret_cast<char **>(0x9a3a14) = t;
        *reinterpret_cast<int *>(0x9a3a18) = static_cast<int>(size / 0x120);
    }
    if (char *t = Records("GLOBAL\\SUNINFO.REC", size))
    {
        *reinterpret_cast<char **>(0x91604c) = t;
        *reinterpret_cast<int *>(0x916050) = static_cast<int>(size / 0x110);
    }
    if (char *t = Records("GLOBAL\\PRESETRIDES.REC", size))
    {
        char **list = reinterpret_cast<char **>(0x9b2bf4);
        for (uint32_t at = 0; at + 0x290 <= size; at += 0x290)
        {
            char *preset = t + at;
            char *last = list[1];
            *reinterpret_cast<char **>(last) = preset;
            list[1] = preset;
            reinterpret_cast<char **>(preset)[1] = last;
            reinterpret_cast<char **>(preset)[0] = reinterpret_cast<char *>(list);
        }
    }
    if (char *t = Records("GLOBAL\\ANIMHOOKUP.REC", size))
        *reinterpret_cast<char **>(0x9b09e0) = t;
    if (char *t = Records("GLOBAL\\ANIMHIDE.REC", size))
    {
        *reinterpret_cast<char **>(0x9b09e4) = t;
        *reinterpret_cast<char **>(0x9b09e8) = t + size / 2;
    }
}

/** A chunk of `id` around `size` bytes of payload, its data `lead` bytes after the header (0x11 filled), in memory the
 * game keeps; the payload's place. */
char *MakeChunk(unsigned int id, uint32_t lead, uint32_t size, char *&payload)
{
    char *block = static_cast<char *>(bMalloc(static_cast<int>(16 + lead + size), 0x2000));
    char *chunk = block + 8;
    *reinterpret_cast<unsigned int *>(chunk) = id;
    *reinterpret_cast<unsigned int *>(chunk + 4) = lead + size;
    memset(chunk + 8, 0x11, lead);
    payload = chunk + 8 + lead;
    return chunk;
}

/** GLOBALB's car tables from our files (tools/assetc/catalog.py car_table_files): each made into the chunk its loader
 * reads (LoaderCarInfo 0x756a90, LoaderBounds 0x687240), in GLOBALB's order. Returns how many loaded. */
int LoadCarTables()
{
    int loaded = 0;
    uint32_t size = 0;
    if (uint8_t *types = nfs::pack::ReadWhole("GLOBAL\\CARTYPES.REC", size))
    {
        // The records from the first 16-byte place of the chunk's data: the block is 0x80-aligned, the data at +16.
        char *payload;
        bChunk *chunk = reinterpret_cast<bChunk *>(MakeChunk(0x34600, 0, size, payload));
        memcpy(payload, types, size);
        ::operator delete(types);
        loaded += LoaderCarInfo(chunk);
    }
    if (uint8_t *slots = nfs::pack::ReadWhole("GLOBAL\\SLOTTYPES.REC", size))
    {
        char *payload;
        bChunk *chunk = reinterpret_cast<bChunk *>(MakeChunk(0x34607, 0, size, payload));
        memcpy(payload, slots, size);
        ::operator delete(slots);
        loaded += LoaderCarInfo(chunk);
    }
    if (uint8_t *parts = nfs::pack::ReadWhole("GLOBAL\\CARPARTS.TBL", size))
    {
        const uint32_t count = size >= 12 && !memcmp(parts, "CPRT", 4) ? *reinterpret_cast<uint32_t *>(parts + 8) : 0;
        uint32_t total = 0, at = 12;
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t n = *reinterpret_cast<uint32_t *>(parts + at + 4);
            total += 8 + n;
            at += 8 + n;
        }
        if (count && at <= size)
        {
            char *payload;
            bChunk *chunk = reinterpret_cast<bChunk *>(MakeChunk(0x80034602, 0, total, payload));
            at = 12;
            for (uint32_t i = 0; i < count; ++i)
            {
                const uint32_t n = *reinterpret_cast<uint32_t *>(parts + at + 4);
                memcpy(payload, parts + at, 8 + n);
                payload += 8 + n;
                at += 8 + n;
            }
            loaded += LoaderCarInfo(chunk);
        }
        ::operator delete(parts);
    }
    if (uint8_t *bounds = nfs::pack::ReadWhole("GLOBAL\\CARBOUNDS.REC", size))
    {
        const uint32_t count = size >= 12 && !memcmp(bounds, "CBND", 4) ? *reinterpret_cast<uint32_t *>(bounds + 8) : 0;
        // Each record's chunk (0x3B901) has its data at the next 16-byte place after its header.
        uint32_t total = 0, at = 12;
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t n = *reinterpret_cast<uint32_t *>(bounds + at);
            const uint32_t header = 16 + total;
            const uint32_t lead = (16 - (header + 8) % 16) % 16;
            total += 8 + lead + n;
            at += 4 + n;
        }
        if (count && at <= size)
        {
            char *payload;
            char *chunk = MakeChunk(0x8003B900, 0, total, payload);
            at = 12;
            for (uint32_t i = 0; i < count; ++i)
            {
                const uint32_t n = *reinterpret_cast<uint32_t *>(bounds + at);
                const uint32_t lead = (16 - (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(payload)) + 8) % 16) % 16;
                *reinterpret_cast<unsigned int *>(payload) = 0x3B901;
                *reinterpret_cast<unsigned int *>(payload + 4) = lead + n;
                memset(payload + 8, 0x11, lead);
                memcpy(payload + 8 + lead, bounds + at + 4, n);
                payload += 8 + lead + n;
                at += 4 + n;
            }
            loaded += NFS_ORIGINAL(0x687240, int(__cdecl *)(bChunk *))(reinterpret_cast<bChunk *>(chunk));
        }
        ::operator delete(bounds);
    }
    return loaded;
}
} // namespace

void LoadGlobalChunks()
{
    InitLocalization();
    int size;
    // From our tables the vault needs no EA image (AttribVaults.cpp).
    void *attributes = nfs::attribtables::Available() ? nullptr : bGetFile("GLOBAL\\ATTRIBUTES.BIN", &size, 0x1040);
    gDatabaseVault = InitializeSingleAttributeVault(attributes, "db", nullptr, 0);
    if (attributes)
        kFree(attributes);
    // GLOBALB's texture packs, sets of their own (tools/assetc/catalog.py drop_texture_packs): GlobalTextures last,
    // as GLOBALB's texture animations belong to the pack loaded last.
    nfs::globalload::LoadResourceFile("GLOBAL\\GLOBALTEXTURESPC.TPK");
    nfs::globalload::LoadResourceFile("GLOBAL\\PIPELINE\\PC\\EMITTER_SYSTEM_TEXTURE_PAGE.TPK");
    nfs::globalload::LoadResourceFile("GLOBAL\\GLOBALTEXTURES.TPK");
    // GLOBALB's effect tables (tools/assetc/catalog.py effect_files): the texture animations, of GlobalTextures just
    // loaded, and the emitters' texture pages (0x916058, their count 0x91605c), as their chunk loaders set them.
    {
        uint32_t size = 0;
        if (uint8_t *anims = nfs::pack::ReadWhole("GLOBAL\\TEXTUREANIMS.REC", size))
        {
            LoadTextureAnims(anims, size);
            ::operator delete(anims);
        }
        if (uint8_t *pages = nfs::pack::ReadWhole("GLOBAL\\TEXTUREPAGES.REC", size))
        {
            char *kept = static_cast<char *>(bMalloc(static_cast<int>(size ? size : 1), 0x2000));
            memcpy(kept, pages, size);
            ::operator delete(pages);
            *reinterpret_cast<char **>(0x916058) = kept;
            *reinterpret_cast<int *>(0x91605c) = static_cast<int>(size >> 5);
        }
    }
    // eLabGlobal's cube, a solid file of its own (assets/global/solidfiles/elabglobal).
    nfs::globalload::LoadResourceFile("GLOBAL\\ELABGLOBAL.BIN");
    // GLOBALB's light materials, from our table (ChunkLoaders.cpp), before the solids that name them.
    {
        uint32_t size = 0;
        if (uint8_t *materials = nfs::pack::ReadWhole("GLOBAL\\LIGHTMATERIALS.MAT", size))
        {
            nfs::Log("global data: %d light materials", LoadLightMaterials(materials, size));
            ::operator delete(materials);
        }
    }
    LoadRecordTables();
    nfs::Log("global data: %d car tables", LoadCarTables());
    ResourceFile *globalb = kCreateResourceFile("GLOBAL\\GLOBALB.LZC", 1, 9, 0, 0);
    globalb->BeginLoading(nullptr, nullptr);
    const char **name = reinterpret_cast<const char **>(reinterpret_cast<char *>(globalb) + 0x1c);
    bFreeSharedString(*name);
    *name = bAllocateSharedString("GLOBAL\\GLOBALB.BUN");
    LoadResource(Named(kBrakesGeometry), 6);
    LoadResource(Named(kBrakesTextures), 6);
    WaitForResources();
    eLoadStreamingTexturePack(Named(kDynTex), nullptr, nullptr, 0);
    eWaitForStreamingTexturePackLoading(Named(kDynTex));
    LoadCurrentLanguage();
    LoadResource(eIsWidescreen() ? "GLOBAL\\WIDESCREEN_GLOBAL.BUN" : "GLOBAL\\THINSCREEN_GLOBAL.BUN", 1);
    if (!GetTextureInfo(kLoadingTexture, 0, 0))
    {
        unsigned int key = kLoadingTexture;
        eLoadStreamingTexture(&key, 1, static_cast<void(__cdecl *)(unsigned int)>(nullptr), 0u, 0);
    }
    WaitForResources();
    IVisualTreatment *treatment = NFS_ORIGINAL(0x6dfaf0, IVisualTreatment *(__cdecl *)())();
    treatment->Reset();
    *reinterpret_cast<int *>(treatment) = 2;
    eDisplayFrame();
    eLoadStreamingTexturePack(Named(kCarTextures), nullptr, nullptr, 0);
    eLoadStreamingTexturePack(Named(kWheelTextures), nullptr, nullptr, 0);
    for (uintptr_t pack : kSolidPacks)
        eLoadStreamingSolidPack(Named(pack), nullptr, nullptr, 0);
    eWaitForStreamingTexturePackLoading(nullptr);
    eWaitForStreamingSolidPackLoading(nullptr);
    WaitForResources();
    GenerateMissingCarParts();
    int carPool = PoolSize(0) << 10;
    int trackPool = kTrackPool;
    if (EmergencySaveMemory)
    {
        carPool -= kEmergencyCarPool;
        trackPool = kTrackPoolEmergency;
    }
    TheTrackStreamer.InitMemoryPool(trackPool + carPool);
    AllowCompressedStreamingTexturesInThisPoolNum = kCompressedTexturePools;
    TheCarLoader.SetMemoryPoolSize(carPool);
    InitializeSoundLoad();
    BlockWhileQueuedFileBusy();
    nfs::precache::Release("boot");
    const bool sunset = bFileExists("TRACKS/L2RA_Sunset.BUN") != 0;
    if (JoylogCapturing())
        JoylogCapture()->AddData(sunset, kJoylogSunset, 1);
    else if (JoylogReplaying() &&
             (NFS_ORIGINAL(0x660100, int(__cdecl *)(int, int))(kJoylogSunset, 1) != 0) != sunset)
        bBreak();
    if (!FrontEndVault())
    {
        int feSize;
        void *fe = nfs::attribtables::Available() ? nullptr : bGetFile("GLOBAL\\FE_ATTRIB.BIN", &feSize, 0x1000);
        FrontEndVaultHigh() = true;
        uintptr_t highAllocator = kHighAttribAllocVtable;
        Attrib::Database::sThis->CollectGarbage();
        void *kept = AttribAllocator();
        AttribAllocator() = &highAllocator;
        FrontEndVault() = InitializeSingleAttributeVault(fe, "frontend", kFrontEndVaultData, kFrontEndVaultSlots);
        if (kept)
        {
            Attrib::Database::sThis->CollectGarbage();
            AttribAllocator() = kept;
        }
        if (fe)
            kFree(fe);
    }
    nfs::realism::Init();
    nfs::x360::LoadNextGenSky();
    if (nfs::timeofday::Enabled())
        nfs::globalload::LoadResourceFile("GLOBAL\\NIGHTSKY.BIN");
    // The added cars' car select logos (tools/assetc/frontlogos.py), which the front end's own pack lacks.
    nfs::globalload::LoadResourceFile("FRONTEND\\LOGOS.BIN");
}

namespace
{
const uintptr_t kRaceParameters = 0x904fb8;
const uintptr_t kGameFlowState = 0x925e90;
const int kGameFlowLoadingRegion = 4;
const uintptr_t kLoadedTrackInfo = 0x9a3a20;
const uintptr_t kSkipFE = 0x926064;
const uintptr_t kSkipFESplitScreen = 0x92607c;
const uintptr_t kOnlineManager = 0x9b0fa0;
const uintptr_t kFEDatabase = 0x91cf90;
const int kSplitScreenFlag = 4;
const int kSplitScreenMode = 2;
const int kCarLoaderRegionMode = 2;
const unsigned int kRegionLoadingState = 0x9b35fe3a;
const int kRegionLoadingScreen = 1;

/** How the race is driven: 0 one player, 1 split screen, 2 online. */
int RaceKind()
{
    if (*reinterpret_cast<int *>(kSkipFE))
        return *reinterpret_cast<int *>(kSkipFESplitScreen) != 0;
    if (NFS_ORIGINAL(0x735620, bool(__thiscall *)(void *))(reinterpret_cast<void *>(kOnlineManager)))
        return 2;
    const unsigned char *fe = *reinterpret_cast<unsigned char *const *>(kFEDatabase);
    return (fe[0x12c] & kSplitScreenFlag) && fe[0] == kSplitScreenMode;
}
} // namespace

void RegionLoader::BeginLoading()
{
    *reinterpret_cast<int *>(kGameFlowState) = kGameFlowLoadingRegion;
    *reinterpret_cast<TrackInfo **>(kLoadedTrackInfo) =
        TrackInfo::GetTrackInfo(*reinterpret_cast<int *>(kRaceParameters));
    LeakDetectorFreeMemory = NFS_ORIGINAL(0x464050, int(__cdecl *)(int))(0);
    LeakDetectorAllocationNumber = NFS_ORIGINAL(0x460b80, int(__cdecl *)())();
    LeakDetectorLargestAlloc = NFS_ORIGINAL(0x464080, int(__cdecl *)(int))(0);
    nfs::precache::Begin("race");
    WWorld::Init();
    const bool splitScreen = RaceKind() == 1;
    int pool = splitScreen ? PoolSize(1) : PoolSize(0);
    if (EmergencySaveMemory)
        pool -= kEmergencyRacePool;
    TheCarLoader.SetMemoryPoolSize(pool << 10);
    TheCarLoader.SetLoadingMode(static_cast<CarLoader::eLoadingMode>(kCarLoaderRegionMode), splitScreen);
    if (void *event = NFS_ORIGINAL(0x627400, void *(__cdecl *)(size_t))(0xc))
        NFS_ORIGINAL(0x62d570, void(__thiscall *)(void *, int))(event, kRegionLoadingScreen);
    reinterpret_cast<unsigned int *>(this)[1] = kRegionLoadingState;
    NFS_ORIGINAL(0x666aa0, void(__thiscall *)(void *))(this);
}

void FinishedGameLoading()
{
    NFS_ORIGINAL(0x6627e0, void(__cdecl *)())();
    nfs::precache::Release("race");
}
