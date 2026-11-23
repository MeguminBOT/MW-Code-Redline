/**
 * Files of EA's layout served from the packs (docs/PACKS.md): the PC file device's Open, Close, Read, Seek and Getsize
 * (original: RealFile's Win32 driver 0x7eff71, 0x7f0084, 0x7f008e, 0x7f00cc, 0x7f00f7). A name a pack under data\
 * holds, as a legacy file or as a set of typed resources (CARS\GTI\GEOMETRY.BIN), opens as a handle of ours: a legacy
 * file's reads unpack the pack's blocks, a set's file is made from its resources by the bridge (packbridge.h) as it is
 * read. The world's stream (TRACKS\STREAML2RA.BUN) is a stream resource: each section the streamer reads is made by
 * the bridge from the section's resources, whatever packs hold them (docs/WORLD_DATA.md). Every other name goes to the
 * disk as before. -set Data.Packs=0 turns it off.
 *
 * Mods: the packs under mods\ (beside data\; tools/convertcar.py and convertmod.py build them there, docs/MODS.md)
 * are mounted after data\'s, and a file or resource both serve comes from the mod's. Nothing of a mod is in data\,
 * so deleting mods\ takes every mod out. -set Data.Mods=0 leaves them unmounted, and so does a pack of mods\compiled\
 * made from another pack of data\ than the one there (ModsMatchData).
 *
 * -set Data.WorldFolder=PATH mounts the world's packs from PATH (laid out as data\world: rockport\*.pack,
 * minimap.pack) instead of data\world, to try world packs without installing them (tools/gametest/world_check.py).
 */
#include <windows.h>

#include <stdlib.h>
#include <string.h>

#include "game/gen/api.h"
#include "legacy/link.h"
#include "core/config.h"
#include "core/log.h"
#include "engine/movies/movies.h"
#include "engine/resources/attribtables.h"
#include "engine/resources/pack.h"
#include "engine/resources/packbridge.h"
#include "engine/resources/packcodec.h"
#include "engine/resources/precache.h"

NFS_REPLACE(0x7eff71, FileDeviceOpen)
NFS_REPLACE(0x7f0084, FileDeviceClose)
NFS_REPLACE(0x7f008e, FileDeviceRead)
NFS_REPLACE(0x7f00cc, FileDeviceSeek)
NFS_REPLACE(0x7f00f7, FileDeviceGetsize)

HANDLE __fastcall FileDeviceOpen(void *self, void *, const char *path, unsigned int mode, int unused);
BOOL __stdcall FileDeviceClose(HANDLE file);
DWORD __fastcall FileDeviceRead(void *self, void *, HANDLE file, void *buffer, DWORD size, int, int);
DWORD __fastcall FileDeviceSeek(void *self, void *, HANDLE file, LONG low, LONG high, int method, int, int);
DWORD __fastcall FileDeviceGetsize(void *self, void *, HANDLE file);

namespace
{
using namespace nfs::pack;

/** Where the packs are, beside the exe (-set Data.Folder=PATH for another, as tests do). */
const char kDefaultFolder[] = "data";
const char kModsFolder[] = "mods";
const int kMaxOpen = 256;
const uintptr_t kHandleBase = 0xF0000000u;
const unsigned int kModeRead = 1;

struct Pack
{
    char path[MAX_PATH];
    HANDLE file;
    Header header;
    Block *blocks;
    /** Its resources, by id (a set names its resources by id). */
    Resource *table;
    CRITICAL_SECTION lock;
};

struct Entry
{
    char name[96];
    Pack *pack;
    Resource resource;
    uint32_t index;
};

/** Every resource of every pack, by id: a world section names resources of several packs. */
struct IdRef
{
    uint64_t id;
    Pack *pack;
    uint32_t index;
};

/** What the bridge reads a world section's resources through: resource 0 the section, k + 1 its k-th id. */
struct SectionSource
{
    IdRef *items;
    uint32_t count;
};

/** A world section made, kept while the streamer may come back to it (it reads the shared sections in turns). */
struct MadeSection
{
    int32_t index;
    uint32_t used;
    nfs::packbridge::Section *section;
    SectionSource source;
};
const int kMadeSections = 3;

/** A pack's unpacked block, cached for one reader, and the room its packed bytes are read into. */
struct BlockCache
{
    Pack *pack;
    uint32_t cached;
    uint8_t *block;
    uint8_t *packed;
};

struct Open
{
    bool used;
    const Entry *entry;
    uint64_t position;
    BlockCache blocks;
    /** A set's file, made by the bridge; its own reads of the pack go through `bridgeBlocks`. */
    nfs::packbridge::File *bridge;
    BlockCache bridgeBlocks;
    uint64_t size;
    /** The world's stream: its sections (id, place, size) and the ones made last. */
    uint64_t *stream;
    uint32_t streamCount;
    uint32_t clock;
    MadeSection made[kMadeSections];
    /** -set Debug.PackVerify=1: the same file on the disk, every read compared with it. */
    HANDLE verify;
    uint64_t verified;
    uint32_t mismatches;
    /** A precached file's bytes (the whole file, or its first `memorySize` bytes) and its item + 1 (0: none). */
    const uint8_t *memory;
    uint64_t memorySize;
    int precached;
};

Pack *g_packs;
int g_packCount;
IdRef *g_ids;
uint32_t g_idCount;
Entry *g_entries;
int g_entryCount;
Open g_open[kMaxOpen];
CRITICAL_SECTION g_lock;
bool g_mounted;

void *Allocate(size_t size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void *Grow(void *block, size_t size)
{
    return block ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, block, size) : Allocate(size);
}

void Release(void *block)
{
    if (block)
        HeapFree(GetProcessHeap(), 0, block);
}

/** Room whose bytes are all written before they are read. */
void *AllocateRaw(size_t size)
{
    return HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
}

void ReleaseCache(BlockCache &c)
{
    Release(c.block);
    Release(c.packed);
    c.block = c.packed = nullptr;
    c.cached = 0xFFFFFFFFu;
}
bool g_enabled;
long g_served;

/** -set Debug.PackTime=1: reads and opens slower than kSlowMicroseconds logged with their file and thread, and every
 * 5 s the time spent in the packs' reads by thread (the main thread's is what makes frames late). */
const unsigned int kSlowMicroseconds = 2000;
DWORD g_mainThread;
long long g_mainTicks, g_otherTicks;
long g_mainReads, g_otherReads;
DWORD g_timeReported;
/** Blocks unpacked since the start, and those of them unpacked by the decode pool. */
volatile long g_unpacked, g_batched;

bool PackTime()
{
    static const bool on = nfs::config::Int("Debug", "PackTime", 0) != 0;
    return on;
}

long long Ticks()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

unsigned int Microseconds(long long ticks)
{
    static double scale = 0.0;
    if (scale == 0.0)
    {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        scale = 1e6 / static_cast<double>(f.QuadPart);
    }
    return static_cast<unsigned int>(static_cast<double>(ticks) * scale);
}

void NoteTime(const char *what, const char *name, uint32_t bytes, long long ticks, long unpacked, long batched)
{
    const bool main = GetCurrentThreadId() == g_mainThread;
    InterlockedExchangeAdd64(main ? &g_mainTicks : &g_otherTicks, ticks);
    InterlockedIncrement(main ? &g_mainReads : &g_otherReads);
    const unsigned int us = Microseconds(ticks);
    if (us >= kSlowMicroseconds)
        nfs::Log("packs: %s %u bytes of %s in %u us on the %s thread, %ld blocks unpacked (%ld by the pool)", what, bytes,
                 name, us, main ? "main" : "loading", unpacked, batched);
    if (GetTickCount() - g_timeReported > 5000)
    {
        g_timeReported = GetTickCount();
        nfs::Log("packs: in the last 5 s the main thread spent %u ms in %ld reads, the others %u ms in %ld reads",
                 Microseconds(InterlockedExchange64(&g_mainTicks, 0)) / 1000, InterlockedExchange(&g_mainReads, 0),
                 Microseconds(InterlockedExchange64(&g_otherTicks, 0)) / 1000, InterlockedExchange(&g_otherReads, 0));
    }
}

void Normalize(const char *in, char *out, size_t size)
{
    while (*in == '.' && (in[1] == '\\' || in[1] == '/'))
        in += 2;
    while (*in == '\\' || *in == '/')
        ++in;
    size_t n = 0;
    for (; *in && n + 1 < size; ++in)
    {
        char c = *in == '/' ? '\\' : *in;
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
        out[n++] = c;
    }
    out[n] = 0;
}

bool ReadAt(HANDLE file, uint64_t offset, void *out, DWORD size)
{
    OVERLAPPED at = {};
    at.Offset = static_cast<DWORD>(offset);
    at.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD read = 0;
    return ReadFile(file, out, size, &read, &at) && read == size;
}

void Collect(const char *folder, char (*found)[MAX_PATH], int &count, int capacity)
{
    char pattern[MAX_PATH];
    wsprintfA(pattern, "%s\\*", folder);
    WIN32_FIND_DATAA fd;
    HANDLE find = FindFirstFileA(pattern, &fd);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do
    {
        if (fd.cFileName[0] == '.')
            continue;
        char path[MAX_PATH];
        wsprintfA(path, "%s\\%s", folder, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            Collect(path, found, count, capacity);
        else if (count < capacity && lstrlenA(fd.cFileName) > 5 &&
                 !lstrcmpiA(fd.cFileName + lstrlenA(fd.cFileName) - 5, ".pack"))
            lstrcpynA(found[count++], path, MAX_PATH);
    } while (FindNextFileA(find, &fd));
    FindClose(find);
}

/** By name, then by the order the packs were mounted (a mod's pack after data\'s). */
int CompareEntries(const void *a, const void *b)
{
    const Entry *x = static_cast<const Entry *>(a), *y = static_cast<const Entry *>(b);
    const int c = lstrcmpA(x->name, y->name);
    return c ? c : x->pack < y->pack ? -1 : x->pack > y->pack ? 1 : 0;
}

int CompareIds(const void *a, const void *b)
{
    const IdRef *p = static_cast<const IdRef *>(a), *q = static_cast<const IdRef *>(b);
    if (p->id != q->id)
        return p->id < q->id ? -1 : 1;
    return p->pack < q->pack ? -1 : p->pack > q->pack ? 1 : 0;
}

const IdRef *FindId(uint64_t id)
{
    uint32_t lo = 0, hi = g_idCount;
    while (lo < hi)
    {
        const uint32_t mid = (lo + hi) >> 1;
        if (g_ids[mid].id < id)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < g_idCount && g_ids[lo].id == id ? &g_ids[lo] : nullptr;
}

/** A pack's header, resource table and names read from its file (false when it is no pack of this version). */
bool ReadIndex(const char *path, Header &header, Resource *&table, char *&names)
{
    table = nullptr;
    names = nullptr;
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    bool ok = ReadAt(file, 0, &header, sizeof(Header)) && header.magic == kMagic && header.version == kVersion;
    if (ok)
    {
        table = static_cast<Resource *>(Allocate(sizeof(Resource) * (header.resourceCount ? header.resourceCount : 1)));
        names = static_cast<char *>(Allocate(header.namesSize + 2));
        ok = ReadAt(file, header.resourceTable, table, sizeof(Resource) * header.resourceCount) &&
             ReadAt(file, header.names, names, header.namesSize);
    }
    CloseHandle(file);
    if (!ok)
    {
        Release(table);
        Release(names);
        table = nullptr;
        names = nullptr;
    }
    return ok;
}

/** Whether each mod pack made from a pack of data\ (mods\compiled\, a kBase row naming it) was made from the one data\
 * has now. One that was not would hide a newer catalog, vaults or strings behind older copies: the mods are then left
 * out and the game is its own, as without mods\. */
bool ModsMatchData(const char *dataRoot, char (*found)[MAX_PATH], int from, int count)
{
    for (int i = from; i < count; ++i)
    {
        Header header;
        Resource *table;
        char *names;
        if (!ReadIndex(found[i], header, table, names))
            continue;
        bool match = true;
        for (uint32_t r = 0; r < header.resourceCount && match; ++r)
        {
            if (table[r].type != kBase || table[r].name >= header.namesSize)
                continue;
            const char *asset = names + table[r].name;
            const char *baseName = asset + lstrlenA(asset) + 1;
            char basePath[MAX_PATH];
            wsprintfA(basePath, "%s\\%s", dataRoot, baseName);
            const uint64_t want = static_cast<uint64_t>(table[r].group) << 32 | table[r].legacyKey;
            Header baseHeader;
            Resource *baseTable;
            char *baseNames;
            match = ReadIndex(basePath, baseHeader, baseTable, baseNames) &&
                    BaseFingerprint(baseTable, sizeof(Resource) * baseHeader.resourceCount) == want;
            Release(baseTable);
            Release(baseNames);
            if (!match)
                nfs::Log("packs: %s was made from another %s than the one there now (python -m assetc.mods makes it "
                         "anew): the mods are left out",
                         found[i], basePath);
        }
        Release(table);
        Release(names);
        if (!match)
            return false;
    }
    return true;
}

void StartPool();

void Mount()
{
    InitializeCriticalSection(&g_lock);
    g_mounted = true;
    g_enabled = nfs::config::Int("Data", "Packs", 1) != 0;
    if (!g_enabled)
        return;
    const int kMaxPacks = 1024;
    char(*found)[MAX_PATH] = static_cast<char(*)[MAX_PATH]>(Allocate(sizeof(char[MAX_PATH]) * kMaxPacks));
    int count = 0;
    char root[MAX_PATH], folder[MAX_PATH];
    nfs::config::String("Data", "Folder", kDefaultFolder, root, sizeof(root));
    lstrcpynA(folder, root, MAX_PATH);
    Collect(folder, found, count, kMaxPacks);
    char world[MAX_PATH];
    nfs::config::String("Data", "WorldFolder", "", world, sizeof(world));
    if (world[0])
    {
        char prefix[MAX_PATH];
        wsprintfA(prefix, "%s\\world\\", folder);
        const int length = lstrlenA(prefix);
        int kept = 0;
        for (int i = 0; i < count; ++i)
            if (_strnicmp(found[i], prefix, length))
            {
                if (kept != i)
                    lstrcpynA(found[kept], found[i], MAX_PATH);
                ++kept;
            }
        count = kept;
        Collect(world, found, count, kMaxPacks);
        nfs::Log("packs: the world's packs from %s (%d) instead of %s\\world", world, count - kept, folder);
    }
    const int base = count;
    // mods\ beside the data folder.
    char mods[MAX_PATH];
    lstrcpynA(mods, root, MAX_PATH);
    int cut = lstrlenA(mods);
    while (cut > 0 && mods[cut - 1] != '\\' && mods[cut - 1] != '/')
        --cut;
    mods[cut] = 0;
    lstrcatA(mods, kModsFolder);
    if (nfs::config::Int("Data", "Mods", 1))
    {
        Collect(mods, found, count, kMaxPacks);
        if (count > base && !ModsMatchData(folder, found, base, count))
            count = base;
    }
    g_packs = static_cast<Pack *>(Allocate(sizeof(Pack) * (count ? count : 1)));
    int capacity = 0;
    for (int i = 0; i < count; ++i)
    {
        Pack &p = g_packs[g_packCount];
        lstrcpynA(p.path, found[i], MAX_PATH);
        p.file = CreateFileA(p.path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS,
                             nullptr);
        if (p.file == INVALID_HANDLE_VALUE)
            continue;
        if (!ReadAt(p.file, 0, &p.header, sizeof(Header)) || p.header.magic != kMagic || p.header.version != kVersion ||
            p.header.blockSize != kBlockSize)
        {
            nfs::Log("packs: %s is no pack of version %u", p.path, kVersion);
            CloseHandle(p.file);
            continue;
        }
        p.blocks = static_cast<Block *>(Allocate(sizeof(Block) * (p.header.blockCount ? p.header.blockCount : 1)));
        Resource *table = static_cast<Resource *>(Allocate(sizeof(Resource) * (p.header.resourceCount ? p.header.resourceCount : 1)));
        char *names = static_cast<char *>(Allocate(p.header.namesSize + 1));
        const bool read = ReadAt(p.file, p.header.blockTable, p.blocks, sizeof(Block) * p.header.blockCount) &&
                          ReadAt(p.file, p.header.resourceTable, table, sizeof(Resource) * p.header.resourceCount) &&
                          ReadAt(p.file, p.header.names, names, p.header.namesSize);
        names[p.header.namesSize] = 0;
        if (read)
        {
            InitializeCriticalSection(&p.lock);
            for (uint32_t r = 0; r < p.header.resourceCount; ++r)
            {
                if ((table[r].type != kLegacyFile && table[r].type != kSet && table[r].type != kStream) ||
                    table[r].name >= p.header.namesSize)
                    continue;
                if (g_entryCount == capacity)
                {
                    capacity = capacity ? capacity * 2 : 1024;
                    g_entries = static_cast<Entry *>(Grow(g_entries, sizeof(Entry) * capacity));
                }
                const char *asset = names + table[r].name;
                Entry &e = g_entries[g_entryCount++];
                Normalize(asset + lstrlenA(asset) + 1, e.name, sizeof(e.name));
                e.pack = &p;
                e.resource = table[r];
                e.index = r;
            }
            p.table = table;
            table = nullptr;
            ++g_packCount;
        }
        else
        {
            CloseHandle(p.file);
            Release(p.blocks);
        }
        Release(table);
        Release(names);
    }
    Release(found);
    qsort(g_entries, g_entryCount, sizeof(Entry), CompareEntries);
    // A name two packs serve: the last mounted one's (a mod's over data\'s).
    int kept = 0;
    for (int i = 0; i < g_entryCount; ++i)
    {
        if (kept && !lstrcmpA(g_entries[kept - 1].name, g_entries[i].name))
            g_entries[kept - 1] = g_entries[i];
        else
            g_entries[kept++] = g_entries[i];
    }
    const int overridden = g_entryCount - kept;
    g_entryCount = kept;
    StartPool();
    for (int i = 0; i < g_packCount; ++i)
        g_idCount += g_packs[i].header.resourceCount;
    g_ids = static_cast<IdRef *>(Allocate(sizeof(IdRef) * (g_idCount ? g_idCount : 1)));
    uint32_t n = 0;
    for (int i = 0; i < g_packCount; ++i)
        for (uint32_t r = 0; r < g_packs[i].header.resourceCount; ++r)
            g_ids[n++] = {g_packs[i].table[r].id, &g_packs[i], r};
    qsort(g_ids, g_idCount, sizeof(IdRef), CompareIds);
    uint32_t ids = 0;
    for (uint32_t i = 0; i < g_idCount; ++i)
    {
        if (ids && g_ids[ids - 1].id == g_ids[i].id)
            g_ids[ids - 1] = g_ids[i];
        else
            g_ids[ids++] = g_ids[i];
    }
    g_idCount = ids;
    nfs::Log("packs: %d packs (%d under %s), %d files of EA's layout served from them", g_packCount, base, folder,
             g_entryCount);
    if (count > base)
        nfs::Log("packs: %d packs under %s (mods), %d files of data's served from them instead", count - base, mods,
                 overridden);
}

/** The game folder as RealFile puts it before a name (its search path: the working folder). */
char g_base[MAX_PATH];
int g_baseLength;

/** A path as the packs name their files: upper case, '\\' between folders, from the game folder. */
void RelativeName(const char *path, char *name, size_t size)
{
    char full[MAX_PATH];
    Normalize(path, full, sizeof(full));
    const char *rest = full;
    if (g_baseLength && !memcmp(full, g_base, g_baseLength) && full[g_baseLength] == '\\')
        rest = full + g_baseLength + 1;
    Normalize(rest, name, size);
}

const Entry *Find(const char *path)
{
    char name[96];
    RelativeName(path, name, sizeof(name));
    int lo = 0, hi = g_entryCount - 1;
    while (lo <= hi)
    {
        const int mid = (lo + hi) / 2;
        const int c = lstrcmpA(g_entries[mid].name, name);
        if (!c)
            return &g_entries[mid];
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return nullptr;
}

Open *Opened(HANDLE file)
{
    const uintptr_t h = reinterpret_cast<uintptr_t>(file);
    if ((h & 0xFFFF0000u) != kHandleBase)
        return nullptr;
    const uintptr_t slot = (h - kHandleBase) >> 2;
    return slot < kMaxOpen && g_open[slot].used ? &g_open[slot] : nullptr;
}

/** The block unpacked into `to` (room for its unpacked bytes), its packed bytes read into the cache's room for them. */
bool DecodeBlock(BlockCache &c, uint32_t index, uint8_t *to)
{
    InterlockedIncrement(&g_unpacked);
    Pack &p = *c.pack;
    const Block &b = p.blocks[index];
    const bool stored = b.packed == b.unpacked;
    if (b.unpacked > kBlockSize || b.packed > b.unpacked)
        return false;
    if (!stored && !c.packed && !(c.packed = static_cast<uint8_t *>(AllocateRaw(kBlockSize))))
        return false;
    uint8_t *const in = stored ? to : c.packed;
    EnterCriticalSection(&p.lock);
    const bool read = ReadAt(p.file, b.offset, in, b.packed);
    LeaveCriticalSection(&p.lock);
    if (read && (stored || pack_decode(in, b.packed, to, b.unpacked)))
        return true;
    nfs::Log("packs: block %u of %s does not read", index, p.path);
    return false;
}

/** The decode pool: a read that takes several whole blocks has them unpacked at once by the pool's threads and the
 * reader (the front end's platform, a car's textures, the world's always-loaded textures of 600 MB). */
const uint32_t kBatchBlocks = 16;
const int kMaxDecoders = 6;

/** A batch's blocks: up to kBatchBlocks a read takes whole, and the one it ends in. */
const uint32_t kBatchRoom = kBatchBlocks + 1;

struct Batch
{
    const uint8_t *in[kBatchRoom];
    uint32_t packed[kBatchRoom];
    uint8_t *out[kBatchRoom];
    uint32_t unpacked[kBatchRoom];
    long count;
    volatile long next;
    volatile long left;
    volatile long failed;
};

struct DecodePool
{
    /** One batch at a time; its packed bytes are read into `scratch` in one go. */
    CRITICAL_SECTION lock;
    HANDLE wake;
    HANDLE done;
    int workers;
    /** Set while a batch is being unpacked; `active` counts the threads that may be looking at it. */
    volatile long open;
    volatile long active;
    uint8_t *scratch;
    Batch batch;
};
DecodePool g_pool;

void RunBatch(Batch &b)
{
    for (;;)
    {
        const long k = InterlockedIncrement(&b.next) - 1;
        if (k >= b.count)
            return;
        if (b.packed[k] == b.unpacked[k])
            memcpy(b.out[k], b.in[k], b.unpacked[k]);
        else if (!pack_decode(b.in[k], b.packed[k], b.out[k], b.unpacked[k]))
            InterlockedExchange(&b.failed, 1);
        if (!InterlockedDecrement(&b.left))
            SetEvent(g_pool.done);
    }
}

DWORD WINAPI Decoder(void *)
{
    for (;;)
    {
        WaitForSingleObject(g_pool.wake, INFINITE);
        InterlockedIncrement(&g_pool.active);
        if (g_pool.open)
            RunBatch(g_pool.batch);
        InterlockedDecrement(&g_pool.active);
    }
}

void StartPool()
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    int workers = static_cast<int>(info.dwNumberOfProcessors) - 2;
    workers = workers > kMaxDecoders ? kMaxDecoders : workers;
    if (workers < 1)
        return;
    g_pool.wake = CreateSemaphoreA(nullptr, 0, 0x7FFFFFFF, nullptr);
    g_pool.done = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    g_pool.scratch = static_cast<uint8_t *>(AllocateRaw(kBatchRoom * kBlockSize));
    if (!g_pool.wake || !g_pool.done || !g_pool.scratch)
        return;
    InitializeCriticalSection(&g_pool.lock);
    for (int i = 0; i < workers; ++i)
        if (HANDLE thread = CreateThread(nullptr, 0, Decoder, nullptr, 0, nullptr))
        {
            SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);
            CloseHandle(thread);
            ++g_pool.workers;
        }
}

/** `whole` blocks from `first` unpacked one after the other into `to`, and the block after them into `tail` (unless
 * null), by the pool and this thread; their packed bytes at most kBatchRoom blocks long (Batched). */
bool DecodeBlocks(Pack &p, uint32_t first, uint32_t whole, uint8_t *to, uint8_t *tail)
{
    const uint32_t count = whole + (tail ? 1 : 0);
    InterlockedExchangeAdd(&g_unpacked, static_cast<long>(count));
    InterlockedExchangeAdd(&g_batched, static_cast<long>(count));
    const Block &a = p.blocks[first], &z = p.blocks[first + count - 1];
    EnterCriticalSection(&g_pool.lock);
    EnterCriticalSection(&p.lock);
    bool ok = ReadAt(p.file, a.offset, g_pool.scratch, static_cast<DWORD>(z.offset + z.packed - a.offset));
    LeaveCriticalSection(&p.lock);
    if (ok)
    {
        Batch &b = g_pool.batch;
        for (uint32_t k = 0; k < count; ++k)
        {
            const Block &block = p.blocks[first + k];
            b.in[k] = g_pool.scratch + (block.offset - a.offset);
            b.packed[k] = block.packed;
            b.out[k] = k < whole ? to : tail;
            b.unpacked[k] = block.unpacked;
            to += k < whole ? block.unpacked : 0;
        }
        b.count = static_cast<long>(count);
        b.next = 0;
        b.left = static_cast<long>(count);
        b.failed = 0;
        InterlockedExchange(&g_pool.open, 1);
        ReleaseSemaphore(g_pool.wake, static_cast<long>(count) - 1 < g_pool.workers ? static_cast<long>(count) - 1
                                                                                    : g_pool.workers,
                         nullptr);
        RunBatch(b);
        WaitForSingleObject(g_pool.done, INFINITE);
        InterlockedExchange(&g_pool.open, 0);
        while (g_pool.active)
            SwitchToThread();
        ok = !b.failed;
    }
    LeaveCriticalSection(&g_pool.lock);
    if (!ok)
        nfs::Log("packs: blocks %u to %u of %s do not read", first, first + count - 1, p.path);
    return ok;
}

/** The blocks from `index` the pool unpacks at once for a read that starts there with `room` bytes to go: `whole`
 * blocks it takes whole, and (true) the block after them, which the read ends inside. */
bool Batched(const Pack &p, uint32_t index, uint64_t room, uint32_t &whole)
{
    whole = 1;
    if (!g_pool.workers)
        return false;
    const Block &a = p.blocks[index];
    uint64_t span = a.unpacked;
    for (; whole < kBatchBlocks && index + whole < p.header.blockCount; ++whole)
    {
        const Block &next = p.blocks[index + whole];
        if (next.unpacked > kBlockSize || next.packed > next.unpacked || next.offset < a.offset ||
            next.offset + next.packed - a.offset > kBatchRoom * kBlockSize)
            return false;
        if (span + next.unpacked > room)
            return span < room;
        span += next.unpacked;
    }
    return false;
}

/** The block's bytes, unpacked into the cache's buffer; null when the pack cannot be read. */
const uint8_t *LoadBlock(BlockCache &c, uint32_t index)
{
    if (c.block && c.cached == index)
        return c.block;
    if (!c.block && !(c.block = static_cast<uint8_t *>(AllocateRaw(kBlockSize))))
        return nullptr;
    if (!DecodeBlock(c, index, c.block))
    {
        c.cached = 0xFFFFFFFFu;
        return nullptr;
    }
    c.cached = index;
    return c.block;
}

/** Bytes of a resource from an offset: how many (fewer at its end). A whole block asked for and not in the cache is
 * unpacked straight into the buffer. */
uint32_t ReadBlocks(BlockCache &c, const Resource &r, uint64_t offset, void *buffer, uint32_t size)
{
    uint8_t *out = static_cast<uint8_t *>(buffer);
    uint32_t done = 0;
    while (done < size && offset < r.size)
    {
        const uint64_t at = r.offset + offset;
        const uint32_t index = static_cast<uint32_t>(at / kBlockSize);
        if (index >= c.pack->header.blockCount)
            break;
        const uint32_t start = static_cast<uint32_t>(at - static_cast<uint64_t>(index) * kBlockSize);
        const uint32_t unpacked = c.pack->blocks[index].unpacked;
        if (start >= unpacked)
            break;
        uint64_t take = unpacked - start;
        if (take > size - done)
            take = size - done;
        if (take > r.size - offset)
            take = r.size - offset;
        if (!start && take == unpacked && !(c.block && c.cached == index))
        {
            // Whole blocks: unpacked straight into the buffer, several at once by the decode pool, with the block the
            // read ends in (into the cache, where the next read starts).
            uint64_t room = size - done;
            room = room < r.size - offset ? room : r.size - offset;
            uint32_t whole = 1;
            bool tail = unpacked <= kBlockSize && c.pack->blocks[index].packed <= unpacked &&
                        Batched(*c.pack, index, room, whole) && !(c.block && c.cached == index + whole);
            if (tail && !c.block && !(c.block = static_cast<uint8_t *>(AllocateRaw(kBlockSize))))
                tail = false;
            if (whole > 1 || tail)
            {
                if (tail)
                    c.cached = 0xFFFFFFFFu;
                if (!DecodeBlocks(*c.pack, index, whole, out + done, tail ? c.block : nullptr))
                    break;
                if (tail)
                    c.cached = index + whole;
                take = 0;
                for (uint32_t k = 0; k < whole; ++k)
                    take += c.pack->blocks[index + k].unpacked;
            }
            else if (!DecodeBlock(c, index, out + done))
                break;
        }
        else
        {
            const uint8_t *block = LoadBlock(c, index);
            if (!block)
                break;
            memcpy(out + done, block + start, static_cast<size_t>(take));
        }
        done += static_cast<uint32_t>(take);
        offset += take;
    }
    return done;
}

/** -set Debug.PackCheck=1: every read of a resource read again block by block through a cache of its own and the two
 * compared, the decode pool's batches and the blocks unpacked straight into the reader's buffer against the plain way. */
bool PackCheck()
{
    static const bool on = nfs::config::Int("Debug", "PackCheck", 0) != 0;
    return on;
}

volatile long g_checked, g_checkFailed;
DWORD g_checkReported;

void CheckRead(Pack &p, const Resource &r, uint64_t offset, const uint8_t *got, uint32_t size)
{
    BlockCache c = {&p, 0xFFFFFFFFu, nullptr, nullptr};
    uint8_t *want = static_cast<uint8_t *>(AllocateRaw(size));
    uint32_t done = 0;
    while (want && done < size)
    {
        const uint64_t at = r.offset + offset + done;
        const uint32_t index = static_cast<uint32_t>(at / kBlockSize);
        const uint8_t *block = index < p.header.blockCount ? LoadBlock(c, index) : nullptr;
        if (!block)
            break;
        const uint32_t start = static_cast<uint32_t>(at - static_cast<uint64_t>(index) * kBlockSize);
        uint32_t take = p.blocks[index].unpacked - start;
        take = take < size - done ? take : size - done;
        memcpy(want + done, block + start, take);
        done += take;
    }
    const bool same = want && done == size && !memcmp(want, got, size);
    InterlockedIncrement(&g_checked);
    if (!same && InterlockedIncrement(&g_checkFailed) <= 16)
        nfs::Log("packs: check: %u bytes at %u of a resource of %s differ from the block by block read", size,
                 static_cast<unsigned>(offset), p.path);
    if (GetTickCount() - g_checkReported > 5000)
    {
        g_checkReported = GetTickCount();
        nfs::Log("packs: check: %ld reads compared with the block by block read, %ld differ", g_checked, g_checkFailed);
    }
    ReleaseCache(c);
    Release(want);
}

uint32_t ReadResource(BlockCache &c, const Resource &r, uint64_t offset, void *buffer, uint32_t size)
{
    const uint32_t done = ReadBlocks(c, r, offset, buffer, size);
    if (PackCheck() && done)
        CheckRead(*c.pack, r, offset, static_cast<const uint8_t *>(buffer), done);
    return done;
}

uint64_t BridgeSize(void *context, uint32_t resource)
{
    return static_cast<BlockCache *>(context)->pack->table[resource].size;
}

bool BridgeRead(void *context, uint32_t resource, uint64_t offset, void *out, uint32_t bytes)
{
    BlockCache &c = *static_cast<BlockCache *>(context);
    return ReadResource(c, c.pack->table[resource], offset, out, bytes) == bytes;
}

/** A resource's place in its pack's table, by id (the table is sorted by id); -1 when it has none. */
int64_t IndexOf(const Pack &p, uint64_t id)
{
    int32_t lo = 0, hi = static_cast<int32_t>(p.header.resourceCount) - 1;
    while (lo <= hi)
    {
        const int32_t mid = static_cast<int32_t>(static_cast<uint32_t>(lo + hi) >> 1);
        if (p.table[mid].id == id)
            return mid;
        if (p.table[mid].id < id)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

/** The blocks a world section's resources are read through, one per pack (the least recently used goes): its
 * resources lie in the packs of their kinds and are read in turns, and with one block for all of them a block was
 * unpacked again at each turn (601 blocks for Rockport's section 0 of 18 MB). */
const int kSectionCaches = 16;
struct SectionCaches
{
    BlockCache slot[kSectionCaches];
    uint32_t used[kSectionCaches];
    uint32_t clock;
};

SectionCaches &Caches(SectionSource &s)
{
    return *reinterpret_cast<SectionCaches *>(s.items + s.count);
}

void ReleaseCaches(SectionCaches &c)
{
    for (BlockCache &b : c.slot)
    {
        ReleaseCache(b);
        b.pack = nullptr;
    }
}

uint64_t SectionResourceSize(void *context, uint32_t resource)
{
    const SectionSource &s = *static_cast<SectionSource *>(context);
    return resource < s.count ? s.items[resource].pack->table[s.items[resource].index].size : 0;
}

bool SectionResourceRead(void *context, uint32_t resource, uint64_t offset, void *out, uint32_t bytes)
{
    SectionSource &s = *static_cast<SectionSource *>(context);
    if (resource >= s.count)
        return false;
    const IdRef &ref = s.items[resource];
    SectionCaches &c = Caches(s);
    int pick = 0;
    for (int i = 0; i < kSectionCaches; ++i)
    {
        if (c.slot[i].pack == ref.pack)
        {
            pick = i;
            break;
        }
        if (c.used[i] < c.used[pick])
            pick = i;
    }
    BlockCache &b = c.slot[pick];
    if (b.pack != ref.pack)
    {
        b.pack = ref.pack;
        b.cached = 0xFFFFFFFFu;
    }
    c.used[pick] = ++c.clock;
    return ReadResource(b, ref.pack->table[ref.index], offset, out, bytes) == bytes;
}

void CloseStreamSection(MadeSection &m)
{
    nfs::packbridge::CloseSection(m.section);
    if (m.source.items)
        ReleaseCaches(Caches(m.source));
    Release(m.source.items);
    m = MadeSection();
    m.index = -1;
}

/** The stream's section k made by the bridge into the least recently used slot: its resource and the resources its ids
 * name found in the packs. */
MadeSection *OpenStreamSection(Open &o, uint32_t k)
{
    MadeSection *slot = &o.made[0];
    for (int i = 1; i < kMadeSections; ++i)
        if (o.made[i].used < slot->used)
            slot = &o.made[i];
    CloseStreamSection(*slot);
    MadeSection &m = *slot;
    const uint64_t *row = o.stream + 3 * k;
    const IdRef *section = FindId(row[0]);
    if (!section)
        return false;
    BlockCache cache = {section->pack, 0xFFFFFFFFu, nullptr, nullptr};
    const Resource &res = section->pack->table[section->index];
    uint8_t head[64];
    bool ok = ReadResource(cache, res, 0, head, 64) == 64;
    uint32_t pieces = 0, count = 0;
    memcpy(&pieces, head + 4, 4);
    memcpy(&count, head + 8, 4);
    uint64_t *ids = static_cast<uint64_t *>(Allocate(8 * (count ? count : 1)));
    ok = ok && ReadResource(cache, res, 64 + 64ull * pieces, ids, 8 * count) == 8 * count;
    ReleaseCache(cache);
    // The items, then the block cache the bridge's reads share.
    IdRef *items = static_cast<IdRef *>(Allocate(sizeof(IdRef) * (count + 1) + sizeof(SectionCaches)));
    items[0] = *section;
    for (uint32_t i = 0; ok && i < count; ++i)
    {
        const IdRef *ref = FindId(ids[i]);
        ok = ref != nullptr;
        if (ok)
            items[i + 1] = *ref;
        else
            nfs::Log("packs: a section of the world names %08x%08x, which no pack holds",
                     static_cast<unsigned>(ids[i] >> 32), static_cast<unsigned>(ids[i]));
    }
    Release(ids);
    m.source = {items, count + 1};
    if (ok)
    {
        LARGE_INTEGER t0, t1, f;
        const long unpacked = g_unpacked;
        QueryPerformanceCounter(&t0);
        const nfs::packbridge::Source source = {&m.source, &SectionResourceSize, &SectionResourceRead};
        m.section = nfs::packbridge::OpenSection(source, row[1]);
        // What the section reads later are its textures' images, through one pack or two.
        ReleaseCaches(Caches(m.source));
        QueryPerformanceCounter(&t1);
        QueryPerformanceFrequency(&f);
        ok = m.section && nfs::packbridge::SectionSize(m.section) == row[2];
        static const bool log = nfs::config::Int("Debug", "PackLog", 0) != 0;
        if (log && m.section)
            nfs::Log("packs: world section %u made (%u bytes, %u resources) in %u us, %ld blocks unpacked", k,
                     static_cast<unsigned>(row[2]), count,
                     static_cast<unsigned>(1e6 * static_cast<double>(t1.QuadPart - t0.QuadPart) /
                                           static_cast<double>(f.QuadPart)),
                     g_unpacked - unpacked);
    }
    if (!ok)
    {
        nfs::Log("packs: world section %u (at %u) is not made", k, static_cast<unsigned>(row[1]));
        CloseStreamSection(m);
        return nullptr;
    }
    m.index = static_cast<int32_t>(k);
    return &m;
}

MadeSection *StreamSection(Open &o, uint32_t k)
{
    MadeSection *m = nullptr;
    for (int i = 0; i < kMadeSections; ++i)
        if (o.made[i].index == static_cast<int32_t>(k))
            m = &o.made[i];
    if (!m)
        m = OpenStreamSection(o, k);
    if (m)
        m->used = ++o.clock;
    return m;
}

bool OpenStream(Open &o)
{
    BlockCache cache = {o.entry->pack, 0xFFFFFFFFu, nullptr, nullptr};
    uint8_t head[16];
    bool ok = ReadResource(cache, o.entry->resource, 0, head, 16) == 16;
    uint32_t count = 0;
    memcpy(&count, head, 4);
    memcpy(&o.size, head + 8, 8);
    o.stream = static_cast<uint64_t *>(Allocate(24 * (count ? count : 1)));
    ok = ok && ReadResource(cache, o.entry->resource, 16, o.stream, 24 * count) == 24 * count;
    o.streamCount = count;
    for (int i = 0; i < kMadeSections; ++i)
        o.made[i].index = -1;
    ReleaseCache(cache);
    return ok;
}

/** The stream's bytes: the sections made by the bridge, zeros between them. */
uint32_t ReadStream(Open &o, uint64_t offset, uint8_t *out, uint32_t size)
{
    uint32_t done = 0;
    while (done < size && offset < o.size)
    {
        uint32_t lo = 0, hi = o.streamCount;
        while (lo < hi)
        {
            const uint32_t mid = (lo + hi) >> 1;
            if (o.stream[3 * mid + 1] + o.stream[3 * mid + 2] <= offset)
                lo = mid + 1;
            else
                hi = mid;
        }
        uint64_t take = size - done;
        if (lo < o.streamCount && offset >= o.stream[3 * lo + 1])
        {
            MadeSection *m = StreamSection(o, lo);
            if (!m)
                break;
            const uint64_t within = offset - o.stream[3 * lo + 1];
            if (take > o.stream[3 * lo + 2] - within)
                take = o.stream[3 * lo + 2] - within;
            if (nfs::packbridge::ReadSection(m->section, within, out + done, static_cast<uint32_t>(take)) != take)
                break;
        }
        else
        {
            const uint64_t next = lo < o.streamCount ? o.stream[3 * lo + 1] : o.size;
            if (take > next - offset)
                take = next - offset;
            memset(out + done, 0, static_cast<size_t>(take));
        }
        done += static_cast<uint32_t>(take);
        offset += take;
    }
    return done;
}

/** Set resources read lately, by entry: a set's file is opened two or three times in a row (the main thread asks its
 * size, the loading thread reads it), and its resource shares a block with others. */
struct CachedSet
{
    const Entry *entry;
    uint8_t *bytes;
    uint32_t used;
};
const int kCachedSets = 32;
CachedSet g_sets[kCachedSets];
uint32_t g_setClock;

/** The set resource's bytes, the caller's to release; null when they do not read. */
uint8_t *SetBytes(Open &o)
{
    const Entry *e = o.entry;
    const uint32_t size = static_cast<uint32_t>(e->resource.size);
    uint8_t *bytes = static_cast<uint8_t *>(AllocateRaw(size));
    if (!bytes)
        return nullptr;
    EnterCriticalSection(&g_lock);
    for (CachedSet &c : g_sets)
        if (c.entry == e)
        {
            memcpy(bytes, c.bytes, size);
            c.used = ++g_setClock;
            LeaveCriticalSection(&g_lock);
            return bytes;
        }
    LeaveCriticalSection(&g_lock);
    if (ReadResource(o.bridgeBlocks, e->resource, 0, bytes, size) != size)
    {
        Release(bytes);
        return nullptr;
    }
    if (uint8_t *kept = static_cast<uint8_t *>(AllocateRaw(size)))
    {
        memcpy(kept, bytes, size);
        EnterCriticalSection(&g_lock);
        CachedSet *slot = &g_sets[0];
        for (CachedSet &c : g_sets)
            if (c.used < slot->used)
                slot = &c;
        Release(slot->bytes);
        *slot = {e, kept, ++g_setClock};
        LeaveCriticalSection(&g_lock);
    }
    return bytes;
}

/** The set's file: its ids found in the pack, handed to the bridge with the set's bytes. */
bool OpenSet(Open &o)
{
    Pack &p = *o.entry->pack;
    o.bridgeBlocks = {&p, 0xFFFFFFFFu, nullptr, nullptr};
    const uint64_t size = o.entry->resource.size;
    uint8_t *set = SetBytes(o);
    uint32_t count = 0;
    if (set && size >= kSetHeaderBytes)
        memcpy(&count, set, 4);
    bool ok = set && size >= kSetHeaderBytes && kSetHeaderBytes + 8ull * count <= size;
    uint32_t *items = ok ? static_cast<uint32_t *>(AllocateRaw(4 * (count ? count : 1))) : nullptr;
    ok = ok && items;
    for (uint32_t i = 0; ok && i < count; ++i)
    {
        uint64_t id;
        memcpy(&id, set + kSetHeaderBytes + 8 * i, 8);
        const int64_t at = IndexOf(p, id);
        ok = at >= 0;
        items[i] = static_cast<uint32_t>(at);
    }
    if (ok)
    {
        const nfs::packbridge::Source source = {&o.bridgeBlocks, &BridgeSize, &BridgeRead};
        o.bridge = nfs::packbridge::Open(source, set, size, items, count);
        ok = o.bridge != nullptr;
        if (ok)
            o.size = nfs::packbridge::Size(o.bridge);
    }
    Release(set);
    Release(items);
    if (!ok)
        nfs::Log("packs: the set %s of %s does not make its file", o.entry->name, p.path);
    return ok;
}
} // namespace


namespace
{
// The precache (engine/resources/precache.h).

enum PrecacheState : LONG
{
    kIdle,
    kQueued,
    kLoading,
    kReady,
    kSkipped,
};

struct PrecacheItem
{
    char name[96];
    char phase[8];
    /** Bytes to hold: 0 for the whole file. */
    uint32_t want;
    uint8_t *data;
    uint32_t size;
    uint32_t total;
    volatile LONG state;
    long users;
    bool releasing;
    uint32_t served;
    HANDLE ready;
};

const char kPrecacheList[] = "GLOBAL\\PRECACHE.TXT";
const int kMaxPrecache = 256;
PrecacheItem *g_precache;
int g_precacheCount;
CRITICAL_SECTION g_precacheLock;
HANDLE g_precacheWake;
HANDLE g_precacheThread;
DWORD g_precacheThreadId;

void FreeItem(PrecacheItem &item)
{
    Release(item.data);
    item.data = nullptr;
    item.size = item.total = 0;
    item.releasing = false;
    item.state = kIdle;
}

bool KeepRead(PrecacheItem &item, uint8_t *data, uint32_t want, uint32_t got, uint32_t total)
{
    if (got != want)
    {
        Release(data);
        return false;
    }
    item.data = data;
    item.size = want;
    item.total = total;
    return true;
}

/** The item's bytes from the packs (through the same opens as the game's), or from the disk. */
bool ReadItem(PrecacheItem &item)
{
    if (Find(item.name))
    {
        HANDLE file = FileDeviceOpen(nullptr, nullptr, item.name, kModeRead, 0);
        Open *o = Opened(file);
        if (!o)
            return false;
        const uint32_t total = static_cast<uint32_t>(o->size);
        const uint32_t want = item.want && item.want < total ? item.want : total;
        uint8_t *data = static_cast<uint8_t *>(AllocateRaw(want));
        uint32_t got = 0;
        while (got < want)
        {
            const DWORD n = FileDeviceRead(nullptr, nullptr, file, data + got, want - got, 0, 0);
            if (!n)
                break;
            got += n;
        }
        FileDeviceClose(file);
        return KeepRead(item, data, want, got, total);
    }
    if (item.want)
        return false;
    HANDLE file = CreateFileA(item.name, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    const DWORD total = GetFileSize(file, nullptr);
    uint8_t *data = static_cast<uint8_t *>(AllocateRaw(total));
    DWORD got = 0;
    if (!ReadFile(file, data, total, &got, nullptr))
        got = 0;
    CloseHandle(file);
    return KeepRead(item, data, total, got, total);
}

DWORD WINAPI PrecacheWorker(void *)
{
    for (;;)
    {
        WaitForSingleObject(g_precacheWake, INFINITE);
        for (int i = 0; i < g_precacheCount; ++i)
        {
            PrecacheItem &item = g_precache[i];
            if (InterlockedCompareExchange(&item.state, kLoading, kQueued) != kQueued)
                continue;
            const bool read = ReadItem(item);
            EnterCriticalSection(&g_precacheLock);
            if (!read)
                item.state = kSkipped;
            else if (item.releasing)
                FreeItem(item);
            else
                item.state = kReady;
            LeaveCriticalSection(&g_precacheLock);
            SetEvent(item.ready);
        }
    }
}

PrecacheItem *FindPrecache(const char *name)
{
    for (int i = 0; i < g_precacheCount; ++i)
        if (!lstrcmpA(g_precache[i].name, name))
            return &g_precache[i];
    return nullptr;
}

/** A precached file's item + 1 for an open of it (waiting for one being read), or 0 when it is read as usual: not
 * listed, not read yet (the worker then leaves it), let go, or asked for by the worker itself. */
int Acquire(const char *path)
{
    if (!g_precacheCount || GetCurrentThreadId() == g_precacheThreadId)
        return 0;
    char name[96];
    RelativeName(path, name, sizeof(name));
    PrecacheItem *item = FindPrecache(name);
    if (!item)
        return 0;
    const LONG state = InterlockedCompareExchange(&item->state, kSkipped, kQueued);
    if (state == kQueued)
        return 0;
    if (state == kLoading)
        WaitForSingleObject(item->ready, INFINITE);
    int at = 0;
    EnterCriticalSection(&g_precacheLock);
    if (item->state == kReady && !item->releasing)
    {
        ++item->users;
        ++item->served;
        at = static_cast<int>(item - g_precache) + 1;
    }
    LeaveCriticalSection(&g_precacheLock);
    return at;
}

void Unacquire(int at)
{
    if (!at)
        return;
    EnterCriticalSection(&g_precacheLock);
    PrecacheItem &item = g_precache[at - 1];
    if (--item.users == 0 && item.releasing)
        FreeItem(item);
    LeaveCriticalSection(&g_precacheLock);
}

/** The retail game's checks for files no install has (docs/EA_FORMATS.md phase 0), answered as the disk would without
 * asking it: the disc check's BIN.DAT at every drive's root, the Disculator's NFS\ZDIR.BIN, FOOBAR and the sunset
 * track Joylog notes. */
bool IsRetailProbe(const char *path)
{
    static const char *const kProbes[] = {"NFS\\ZDIR.BIN", "FOOBAR", "TRACKS\\L2RA_SUNSET.BUN"};
    char name[MAX_PATH];
    Normalize(path, name, sizeof(name));
    if (name[0] && name[1] == ':' && name[2] == '\\' && !lstrcmpA(name + 3, "BIN.DAT"))
        return true;
    char relative[96];
    RelativeName(path, relative, sizeof(relative));
    for (const char *probe : kProbes)
        if (!lstrcmpA(relative, probe))
            return true;
    return false;
}

/** GLOBAL\PRECACHE.TXT: a line per item, "PHASE BYTES NAME" (BYTES 0: the whole file). */
void LoadPrecacheList()
{
    uint32_t size = 0;
    uint8_t *text = nfs::pack::ReadWhole(kPrecacheList, size);
    if (!text)
        return;
    InitializeCriticalSection(&g_precacheLock);
    g_precache = static_cast<PrecacheItem *>(Allocate(sizeof(PrecacheItem) * kMaxPrecache));
    for (char *line = reinterpret_cast<char *>(text); *line && g_precacheCount < kMaxPrecache;)
    {
        char *end = line;
        while (*end && *end != '\n')
            ++end;
        const char next = *end;
        *end = 0;
        if (*line && *line != '#')
        {
            PrecacheItem &item = g_precache[g_precacheCount];
            char *p = line;
            int n = 0;
            while (*p && *p != ' ' && n < 7)
                item.phase[n++] = *p++;
            while (*p == ' ')
                ++p;
            while (*p >= '0' && *p <= '9')
                item.want = item.want * 10 + static_cast<uint32_t>(*p++ - '0');
            while (*p == ' ')
                ++p;
            char *e = p;
            while (*e && *e != '\r')
                ++e;
            *e = 0;
            Normalize(p, item.name, sizeof(item.name));
            item.ready = CreateEventA(nullptr, TRUE, FALSE, nullptr);
            ++g_precacheCount;
        }
        line = next ? end + 1 : end;
    }
    ::operator delete(text);
    g_precacheWake = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    nfs::Log("precache: %d files listed in %s", g_precacheCount, kPrecacheList);
}
} // namespace

void nfs::precache::Begin(const char *phase)
{
    if (!g_precacheCount)
        return;
    int queued = 0;
    EnterCriticalSection(&g_precacheLock);
    for (int i = 0; i < g_precacheCount; ++i)
    {
        PrecacheItem &item = g_precache[i];
        if (lstrcmpA(item.phase, phase) || item.users || (item.state != kIdle && item.state != kSkipped))
            continue;
        item.releasing = false;
        item.served = 0;
        ResetEvent(item.ready);
        item.state = kQueued;
        ++queued;
    }
    LeaveCriticalSection(&g_precacheLock);
    if (!g_precacheThread)
        g_precacheThread = CreateThread(nullptr, 0, PrecacheWorker, nullptr, 0, &g_precacheThreadId);
    SetEvent(g_precacheWake);
    nfs::Log("precache: %s, %d files", phase, queued);
}

void nfs::precache::Release(const char *phase)
{
    if (!g_precacheCount)
        return;
    int held = 0, skipped = 0;
    uint32_t bytes = 0, served = 0;
    EnterCriticalSection(&g_precacheLock);
    for (int i = 0; i < g_precacheCount; ++i)
    {
        PrecacheItem &item = g_precache[i];
        if (lstrcmpA(item.phase, phase))
            continue;
        if (item.state == kReady)
        {
            ++held;
            bytes += item.size;
            served += item.served;
            item.releasing = true;
            if (!item.users)
                FreeItem(item);
        }
        else if (item.state == kLoading)
            item.releasing = true;
        else
        {
            if (item.state == kSkipped)
                ++skipped;
            InterlockedCompareExchange(&item.state, kIdle, kQueued);
            InterlockedCompareExchange(&item.state, kIdle, kSkipped);
        }
    }
    LeaveCriticalSection(&g_precacheLock);
    nfs::Log("precache: %s let go: %d files held (%u KB, %u opens served), %d read as usual", phase, held, bytes >> 10,
             served, skipped);
}

namespace
{
/**
 * The attribute vault files (GLOBAL\\ATTRIBUTES.BIN, FE_ATTRIB.BIN, GAMEPLAY.BIN and its RAWW-stored GAMEPLAY.LZC),
 * made in memory from GLOBAL\\ATTRIBUTES.TABLES (nfs::attribtables::LayoutPack) the first time they are opened and
 * kept: the packs hold only the tables (docs/EA_FORMATS.md phase 2 step 2). GManager reads GAMEPLAY.LZC whole and
 * GAMEPLAY.BIN by its vaults' offsets; the front end's reload reads FE_ATTRIB.BIN and then lays its vault out from the
 * tables itself.
 */
struct MadeFile
{
    const char *name;
    const char *vaults;
    bool stored;
    uint8_t *image;
    uint32_t size;
};

MadeFile g_madeFiles[] = {{"GLOBAL\\ATTRIBUTES.BIN", "ATTRIBUTES.BIN", false, nullptr, 0},
                          {"GLOBAL\\FE_ATTRIB.BIN", "FE_ATTRIB.BIN", false, nullptr, 0},
                          {"GLOBAL\\GAMEPLAY.BIN", "GAMEPLAY.BIN", false, nullptr, 0},
                          {"GLOBAL\\GAMEPLAY.LZC", "GAMEPLAY.BIN", true, nullptr, 0}};
const uint32_t kStoredHeader = 16;

/** A made file's bytes, or null when the path is none of them or the tables cannot make it. */
const uint8_t *Made(const char *path, uint32_t &size)
{
    char name[96];
    RelativeName(path, name, sizeof(name));
    for (MadeFile &m : g_madeFiles)
    {
        if (lstrcmpiA(m.name, name))
            continue;
        if (!m.image && nfs::attribtables::Available())
        {
            // GAMEPLAY.BIN and GAMEPLAY.LZC share one image: the stored header, then the file.
            {
                uint32_t tableSize = 0;
                uint8_t *table = ReadWhole("GLOBAL\\ATTRIBUTES.TABLES", tableSize);
                uint8_t *pack = nullptr;
                size_t packSize = 0;
                if (table && nfs::attribtables::LayoutPack(table, tableSize, m.vaults, kStoredHeader, pack, packSize))
                {
                    const uint32_t body = static_cast<uint32_t>(packSize - kStoredHeader);
                    memcpy(pack, "RAWW", 4);
                    pack[4] = 1;
                    pack[5] = 0x10;
                    pack[6] = pack[7] = 0;
                    const uint32_t words[2] = {body, body + kStoredHeader};
                    memcpy(pack + 8, words, 8);
                    m.image = pack;
                    nfs::Log("packs: %s made from the attribute tables (%u bytes)", m.vaults, body);
                }
                ::operator delete(table);
            }
            if (m.image)
                for (MadeFile &other : g_madeFiles)
                    if (!lstrcmpA(other.vaults, m.vaults))
                    {
                        other.image = m.image;
                        const uint32_t body = *reinterpret_cast<const uint32_t *>(m.image + 8);
                        other.size = other.stored ? body + kStoredHeader : body;
                    }
        }
        if (!m.image)
            return nullptr;
        size = m.size;
        return m.stored ? m.image : m.image + kStoredHeader;
    }
    return nullptr;
}
} // namespace

HANDLE __fastcall FileDeviceOpen(void *self, void *, const char *path, unsigned int mode, int unused)
{
    if (!g_mounted)
    {
        g_mainThread = GetCurrentThreadId();
        Mount();
        char cwd[MAX_PATH];
        if (GetCurrentDirectoryA(MAX_PATH, cwd))
        {
            Normalize(cwd, g_base, sizeof(g_base));
            g_baseLength = lstrlenA(g_base);
        }
        if (g_enabled)
        {
            LoadPrecacheList();
            nfs::precache::Begin("boot");
            nfs::precache::Begin("keep");
        }
    }
    uint32_t madeSize = 0;
    if (const uint8_t *made = g_enabled && path && (mode & kModeRead) ? Made(path, madeSize) : nullptr)
    {
        EnterCriticalSection(&g_lock);
        for (int i = 0; i < kMaxOpen; ++i)
            if (!g_open[i].used)
            {
                g_open[i] = Open();
                g_open[i].used = true;
                g_open[i].size = madeSize;
                g_open[i].memory = made;
                g_open[i].memorySize = madeSize;
                LeaveCriticalSection(&g_lock);
                return reinterpret_cast<HANDLE>(kHandleBase + (static_cast<uintptr_t>(i) << 2));
            }
        LeaveCriticalSection(&g_lock);
    }
    const int cached = g_enabled && path && (mode & kModeRead) ? Acquire(path) : 0;
    if (cached && g_precache[cached - 1].size == g_precache[cached - 1].total)
    {
        // A whole file held: every read comes from memory, the pack or the disk file not opened at all.
        const PrecacheItem &item = g_precache[cached - 1];
        EnterCriticalSection(&g_lock);
        for (int i = 0; i < kMaxOpen; ++i)
            if (!g_open[i].used)
            {
                g_open[i] = Open();
                g_open[i].used = true;
                g_open[i].entry = Find(path);
                g_open[i].size = item.total;
                g_open[i].memory = item.data;
                g_open[i].memorySize = item.size;
                g_open[i].precached = cached;
                LeaveCriticalSection(&g_lock);
                return reinterpret_cast<HANDLE>(kHandleBase + (static_cast<uintptr_t>(i) << 2));
            }
        LeaveCriticalSection(&g_lock);
    }
    if (g_enabled && path && (mode & kModeRead))
        if (const Entry *e = Find(path))
        {
            EnterCriticalSection(&g_lock);
            for (int i = 0; i < kMaxOpen; ++i)
                if (!g_open[i].used)
                {
                    g_open[i] = Open();
                    g_open[i].used = true;
                    g_open[i].entry = e;
                    g_open[i].blocks = {e->pack, 0xFFFFFFFFu, nullptr, nullptr};
                    g_open[i].size = e->resource.size;
                    LeaveCriticalSection(&g_lock);
                    const long long opened = PackTime() ? Ticks() : 0;
                    const long unpacked = g_unpacked, batched = g_batched;
                    if (e->resource.type == kSet && !OpenSet(g_open[i]))
                    {
                        ReleaseCache(g_open[i].bridgeBlocks);
                        g_open[i] = Open();
                        break;
                    }
                    if (e->resource.type == kStream && !OpenStream(g_open[i]))
                    {
                        nfs::Log("packs: the stream %s does not read", e->name);
                        Release(g_open[i].stream);
                        g_open[i] = Open();
                        break;
                    }
                    static const bool verify = nfs::config::Int("Debug", "PackVerify", 0) != 0;
                    if (verify)
                    {
                        g_open[i].verify = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0,
                                                       nullptr);
                        if (g_open[i].verify == INVALID_HANDLE_VALUE)
                            g_open[i].verify = nullptr;
                    }
                    InterlockedIncrement(&g_served);
                    if (PackTime())
                        NoteTime("opened, header of", e->name, 0, Ticks() - opened, g_unpacked - unpacked,
                                 g_batched - batched);
                    static const bool log = nfs::config::Int("Debug", "PackLog", 0) != 0;
                    if (log)
                        nfs::Log("packs: %s from %s", e->name, e->pack->path);
                    if (cached)
                    {
                        // A streamed pack's first bytes held: reads within them come from memory.
                        g_open[i].memory = g_precache[cached - 1].data;
                        g_open[i].memorySize = g_precache[cached - 1].size;
                        g_open[i].precached = cached;
                    }
                    return reinterpret_cast<HANDLE>(kHandleBase + (static_cast<uintptr_t>(i) << 2));
                }
            LeaveCriticalSection(&g_lock);
            nfs::Log("packs: more than %d files open at once; %s from the disk", kMaxOpen, path);
        }
    Unacquire(cached);
    if (path && IsRetailProbe(path))
        return INVALID_HANDLE_VALUE;
    char movie[MAX_PATH];
    if (mode & kModeRead)
        path = nfs::movies::FindFile(path, movie, sizeof(movie));
    // -set Debug.DiskLog=1: every file opened from the disk (not served by the packs), for the EA formats plan.
    static const bool diskLog = nfs::config::Int("Debug", "DiskLog", 0) != 0;
    if (diskLog && path)
        nfs::Log("disk: %s (mode %u)", path, mode);
    return NFS_ORIGINAL(0x7eff71, HANDLE(__thiscall *)(void *, const char *, unsigned int, int))(self, path, mode, unused);
}

BOOL __stdcall FileDeviceClose(HANDLE file)
{
    if (Open *o = Opened(file))
    {
        Unacquire(o->precached);
        EnterCriticalSection(&g_lock);
        ReleaseCache(o->blocks);
        ReleaseCache(o->bridgeBlocks);
        nfs::packbridge::Close(o->bridge);
        for (int i = 0; i < kMadeSections; ++i)
            CloseStreamSection(o->made[i]);
        if (o->verify)
        {
            nfs::Log("packs: %s checked against the disk: %u KB read, %u reads differ", o->entry->name,
                     static_cast<unsigned>(o->verified >> 10), o->mismatches);
            CloseHandle(o->verify);
        }
        Release(o->stream);
        *o = Open();
        LeaveCriticalSection(&g_lock);
        return TRUE;
    }
    return CloseHandle(file);
}

DWORD __fastcall FileDeviceRead(void *self, void *, HANDLE file, void *buffer, DWORD size, int a, int b)
{
    Open *o = Opened(file);
    if (!o)
        return NFS_ORIGINAL(0x7f008e, DWORD(__thiscall *)(void *, HANDLE, void *, DWORD, int, int))(self, file, buffer, size,
                                                                                                    a, b);
    if (o->memory && (o->memorySize == o->size || o->position + size <= o->memorySize))
    {
        const uint64_t left = o->size > o->position ? o->size - o->position : 0;
        const DWORD n = size < left ? size : static_cast<DWORD>(left);
        memcpy(buffer, o->memory + o->position, n);
        o->position += n;
        return n;
    }
    const long long started = PackTime() ? Ticks() : 0;
    const long unpacked = g_unpacked, batched = g_batched;
    const uint32_t done = o->stream   ? ReadStream(*o, o->position, static_cast<uint8_t *>(buffer), size)
                          : o->bridge ? nfs::packbridge::Read(o->bridge, o->position, buffer, size)
                                      : ReadResource(o->blocks, o->entry->resource, o->position, buffer, size);
    if (PackTime())
        NoteTime("read", o->entry->name, done, Ticks() - started, g_unpacked - unpacked, g_batched - batched);
    if (o->verify && done)
    {
        uint8_t *disk = static_cast<uint8_t *>(Allocate(done));
        if (ReadAt(o->verify, o->position, disk, done))
        {
            o->verified += done;
            if (memcmp(disk, buffer, done))
            {
                uint32_t at = 0;
                while (disk[at] == static_cast<uint8_t *>(buffer)[at])
                    ++at;
                if (o->mismatches++ < 8)
                    nfs::Log("packs: %s differs from the disk's at %u", o->entry->name,
                             static_cast<unsigned>(o->position + at));
            }
        }
        Release(disk);
    }
    o->position += done;
    return done;
}

DWORD __fastcall FileDeviceSeek(void *self, void *, HANDLE file, LONG low, LONG high, int method, int a, int b)
{
    Open *o = Opened(file);
    if (!o)
        return NFS_ORIGINAL(0x7f00cc, DWORD(__thiscall *)(void *, HANDLE, LONG, LONG, int, int, int))(self, file, low,
                                                                                                    high, method, a, b);
    const int64_t size = static_cast<int64_t>(o->size);
    int64_t base = method == 1 ? static_cast<int64_t>(o->position) : method == 2 ? size : 0;
    int64_t to = base + static_cast<int64_t>(low);
    if (to < 0)
        to = 0;
    o->position = static_cast<uint64_t>(to);
    return static_cast<DWORD>(o->position);
}

DWORD __fastcall FileDeviceGetsize(void *self, void *, HANDLE file)
{
    if (Open *o = Opened(file))
        return static_cast<DWORD>(o->size);
    return NFS_ORIGINAL(0x7f00f7, DWORD(__thiscall *)(void *, HANDLE))(self, file);
}

uint8_t *nfs::pack::ReadFile(const char *path, uint32_t &size)
{
    if (uint8_t *data = ReadWhole(path, size))
        return data;
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return nullptr;
    const DWORD total = GetFileSize(file, nullptr);
    uint8_t *data = static_cast<uint8_t *>(::operator new(total + 1));
    DWORD read = 0;
    if (!::ReadFile(file, data, total, &read, nullptr))
        read = 0;
    CloseHandle(file);
    data[read] = 0;
    size = read;
    nfs::Log("packs: %s from the disk, not the packs", path);
    return data;
}

uint8_t *nfs::pack::ReadWhole(const char *path, uint32_t &size)
{
    size = 0;
    if (!g_mounted || !g_enabled || !Find(path))
        return nullptr;
    HANDLE file = FileDeviceOpen(nullptr, nullptr, path, kModeRead, 0);
    if (!Opened(file))
        return nullptr;
    const uint32_t total = static_cast<uint32_t>(FileDeviceGetsize(nullptr, nullptr, file));
    uint8_t *data = static_cast<uint8_t *>(::operator new(total + 1));
    uint32_t got = 0;
    while (got < total)
    {
        const DWORD n = FileDeviceRead(nullptr, nullptr, file, data + got, total - got, 0, 0);
        if (!n)
            break;
        got += n;
    }
    FileDeviceClose(file);
    data[got] = 0;
    size = got;
    return data;
}

bool nfs::pack::Exists(const char *path)
{
    if (g_mounted && g_enabled && Find(path))
        return true;
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

struct nfs::pack::SetView
{
    const Entry *entry;
    BlockCache blocks;
    uint8_t *set;
    uint64_t setSize;
    uint32_t count;
    uint32_t *items;
};

nfs::pack::SetView *nfs::pack::OpenSetView(const char *path, uint32_t kind)
{
    if (!g_mounted || !g_enabled)
        return nullptr;
    const Entry *e = Find(path);
    if (!e || e->resource.type != kSet || e->resource.size < kSetHeaderBytes)
        return nullptr;
    SetView *v = static_cast<SetView *>(AllocateRaw(sizeof(SetView)));
    if (!v)
        return nullptr;
    memset(v, 0, sizeof(*v));
    v->entry = e;
    v->blocks = {e->pack, 0xFFFFFFFFu, nullptr, nullptr};
    v->setSize = e->resource.size;
    v->set = static_cast<uint8_t *>(AllocateRaw(static_cast<size_t>(v->setSize)));
    bool ok = v->set && ReadResource(v->blocks, e->resource, 0, v->set, static_cast<uint32_t>(v->setSize)) == v->setSize;
    uint32_t setKind = 0;
    if (ok)
    {
        memcpy(&v->count, v->set, 4);
        memcpy(&setKind, v->set + 4, 4);
        ok = setKind == kind && kSetHeaderBytes + 8ull * v->count <= v->setSize;
    }
    if (ok)
    {
        v->items = static_cast<uint32_t *>(AllocateRaw(4 * (v->count ? v->count : 1)));
        ok = v->items != nullptr;
        for (uint32_t i = 0; ok && i < v->count; ++i)
        {
            uint64_t id;
            memcpy(&id, v->set + kSetHeaderBytes + 8 * i, 8);
            const int64_t at = IndexOf(*e->pack, id);
            ok = at >= 0;
            v->items[i] = static_cast<uint32_t>(at);
        }
    }
    if (!ok)
    {
        CloseSetView(v);
        return nullptr;
    }
    return v;
}

const uint8_t *nfs::pack::SetViewHeader(const SetView *v)
{
    return v->set;
}

uint32_t nfs::pack::SetViewCount(const SetView *v)
{
    return v->count;
}

uint64_t nfs::pack::SetViewItemSize(const SetView *v, uint32_t i)
{
    return v->entry->pack->table[v->items[i]].size;
}

bool nfs::pack::SetViewRead(SetView *v, uint32_t i, uint64_t offset, void *out, uint32_t bytes)
{
    return ReadResource(v->blocks, v->entry->pack->table[v->items[i]], offset, out, bytes) == bytes;
}

void nfs::pack::CloseSetView(SetView *v)
{
    if (!v)
        return;
    ReleaseCache(v->blocks);
    Release(v->set);
    Release(v->items);
    Release(v);
}
