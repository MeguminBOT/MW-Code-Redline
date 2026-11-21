/**
 * The track streamer's memory pool and loading (TrackStreamer, zTrack): the sections of the city around the player are
 * loaded into one pool as the player drives, and thrown out ("jettisoned") when the pool is full.
 *
 * The pool is the original's size plus Streaming.ExtraPoolMegabytes (the rebuild is large address aware), so fewer
 * sections are thrown out only to be loaded again moments later, which made them pop in while on screen.
 *
 * Sections no zone wants any more are not unloaded the moment the zones switch (SwitchZones did, so their scenery and
 * shadows vanished at once, up to 140 shadow casters within 250 m of the player in 5 s): they stay, marked as current,
 * until their scenery and shadows have faded out (nfs::culling::SectionUnloading), then go as SwitchZones would have
 * let them. When memory runs short they go at once. Streaming.FadeUnloads=0 unloads at once as the original did.
 *
 * The zones only want what can be seen from them, but shadows fall from what cannot: a building behind the camera or
 * round a corner lost its shadow on the road ahead when its section was unloaded, and cast one when it was loaded
 * again. Every section whose area comes within Streaming.KeepNearMetres (150) of the camera is wanted as well, so
 * the casters around the player only change far away. The pool grows by Streaming.ExtraPoolMegabytes (512, as much of
 * it as fits below 2 GB: InitMemoryPool) for them and for the sections ahead (below): with the upscaled track textures
 * (STREAML2RA.BUN 507 MB to 773 MB) 128 MB more left the pool short at a jump across the city, hundreds of sections were
 * thrown out, and the game crashed (2026-11-14). While the streamer has had to throw sections out, nothing more is
 * wanted ahead.
 *
 * The streamer follows the camera (UpdateCameraMovers 0x47c880 gives it the player camera's position each frame), so a
 * camera on the far side of a wall from the car (looking back with the far camera, nose against a wall) can switch the
 * zones to those of the far side and unload the section whose collision holds the wall: the car drives through it,
 * out of the world. With the vanilla fixes (gameplay/vanillafixes.h) the streamer follows the player's car instead while the
 * camera follows it.
 *
 * Debugging: -streamdebug logs the sections activated (loaded and made drawable), unloaded and jettisoned every 5 seconds.
 * -set Debug.StreamAt=X,Y,Z puts the camera's streaming position there (what a camera behind a wall does to it; the
 * vanilla fix still follows the car), and -streamdebug also logs how far the camera was from the car it follows.
 */
#include <math.h>

#include "game/gen/api.h"
#include "game/platform/Platform.h"
#include "dev/clipwatch.h"
#include "core/config.h"
#include "engine/rendering/culling/culling.h"
#include "legacy/link.h"
#include "core/log.h"
#include "engine/rendering/shadows/shadowdebug.h"
#include "engine/resources/streamprofile.h"
#include "gameplay/vanillafixes.h"
#include "dev/pose.h"
#include "gameplay/vanillafixes.h"
#include "engine/rendering/vulkan/renderer.h"

namespace nfs::streamprofile
{
bool g_on;
LONGLONG g_ticks[kParts];
} // namespace nfs::streamprofile

namespace
{
const int kMegabyte = 1 << 20;
const DWORD kReportPeriod = 5000;

struct Counts
{
    int loads;
    int unloads;
    int jettisons;
    int jettisonedBytes;
    char sections[512];
    int length;
};

Counts g_counts;
DWORD g_reported;

bool Debugging()
{
    static const bool debug = nfs::vk::HasArgument("-streamdebug");
    return debug;
}

short SectionNumber(const TrackStreamingSection *section)
{
    return *reinterpret_cast<const short *>(reinterpret_cast<const char *>(section) + 8);
}

/** A section kept after the zones stopped wanting it goes once it has faded, or after this long whatever the fade. */
const DWORD kLeaveLimitMs = 2000;
const int kMaxLeaving = 96;
/** TrackStreamingSection::CurrentZones (+0x0b) has a bit for each zone that wants the section; this one marks a section
 * kept while it fades, which SwitchZones' unload passes over like any current section. */
const unsigned char kLeavingBit = 0x80;
const int kActivated = 4;
const int kSectionStride = 0x5c;
/** Room in TrackStreamer's current section list (+0x2c4, up to the bit table's size at +0x848). */
const int kMaxCurrent = (0x848 - 0x2c4) / 4;
/** The memory pool the sections are loaded into (bCountFreeMemory). */
const int kSectionPool = 7;
/** Sections wider than this (the panoramas round the city, kilometres across) are left to the zones. */
const float kMaxNearRadius = 600.0f;
const int kMaxNear = 128;
const int kPlayerView = 1;

struct Leaving
{
    TrackStreamingSection *section;
    DWORD since;
};

Leaving g_leaving[kMaxLeaving];
int g_leavingCount;
/** While FinishLeaving unloads, UnloadSection leaves the list alone. */
bool g_finishing;

bool FadeUnloads()
{
    static const bool on = nfs::config::Int("Streaming", "FadeUnloads", 1) != 0;
    return on;
}

char *Field(const void *p, int offset)
{
    return const_cast<char *>(static_cast<const char *>(p)) + offset;
}

int Status(const TrackStreamingSection *section)
{
    return *reinterpret_cast<const int *>(Field(section, 0x0c));
}

unsigned char &CurrentZones(TrackStreamingSection *section)
{
    return *reinterpret_cast<unsigned char *>(Field(section, 0x0b));
}

/** Letters U, W, X and Y: SwitchZones never unloads those. */
bool AlwaysLoaded(short number)
{
    const int letter = number / 100;
    return letter == 21 || letter == 23 || letter == 24 || letter == 25;
}

int &CurrentCount(TrackStreamer *streamer)
{
    return *reinterpret_cast<int *>(Field(streamer, 0x2c0));
}

TrackStreamingSection **Current(TrackStreamer *streamer)
{
    return reinterpret_cast<TrackStreamingSection **>(Field(streamer, 0x2c4));
}

bool IsLeaving(const TrackStreamingSection *section)
{
    for (int i = 0; i < g_leavingCount; ++i)
        if (g_leaving[i].section == section)
            return true;
    return false;
}

/** Takes the section off the current list, so the streamer does not load it again. */
void RemoveFromCurrent(TrackStreamer *streamer, const TrackStreamingSection *section)
{
    TrackStreamingSection **current = Current(streamer);
    int &count = CurrentCount(streamer);
    for (int i = 0; i < count; ++i)
        if (current[i] == section)
        {
            for (int j = i + 1; j < count; ++j)
                current[j - 1] = current[j];
            --count;
            return;
        }
}

/** Adds the sections whose area comes within Streaming.KeepNearMetres of the camera to the first zone's (the game's own
 * AddCurrentStreamingSections): kept if loaded, loaded if not. LOD sections and panoramas stay with the zones. */
void WantNearSections(TrackStreamer *streamer)
{
    static const float keep = static_cast<float>(nfs::config::Int("Streaming", "KeepNearMetres", 150));
    if (keep <= 0.0f || !eViews[kPlayerView].pCamera)
        return;
    const bVector3 &eye = eViews[kPlayerView].pCamera->CurrentKey.Position;
    short nearby[kMaxNear];
    int count = 0;
    const int sections = reinterpret_cast<const int *>(streamer)[1];
    char *base = *reinterpret_cast<char **>(streamer);
    for (int i = 0; i < sections && count < kMaxNear; ++i)
    {
        TrackStreamingSection *s = reinterpret_cast<TrackStreamingSection *>(base + i * kSectionStride);
        const short number = SectionNumber(s);
        const float *centre = reinterpret_cast<const float *>(Field(s, 0x28));
        const float radius = *reinterpret_cast<const float *>(Field(s, 0x30));
        if (CurrentZones(s) & 3 || radius > kMaxNearRadius || AlwaysLoaded(number) ||
            IsLODScenerySectionNumber(number) || !*reinterpret_cast<const int *>(Field(s, 0x58)))
            continue;
        const float dx = centre[0] - eye.x, dy = centre[1] - eye.y;
        if (sqrtf(dx * dx + dy * dy) - radius < keep)
            nearby[count++] = number;
    }
    if (count)
        streamer->AddCurrentStreamingSections(nearby, count, 0);
    if (Debugging() && count)
        nfs::Log("streaming: %d sections within %d m of the camera kept for their shadows", count,
                 static_cast<int>(keep));
}

/**
 * Streaming ahead: the zones only switch when the car enters a new drivable section, and a section only predicts the
 * next one inside the few prediction zones the designers drew, at most 100 m ahead. At motorway speeds the next
 * section's view (its scenery, the panoramas it sees) was then loaded as the car arrived, and faded in in plain view
 * (PAN_Mountains_College at 0 m, 6 ms after it was loaded, at 460 km/h). The drivable sections the car reaches within
 * Streaming.AheadSeconds (3) along its velocity, up to kMaxAheadMetres, are wanted with everything they see, and a new
 * one switches the zones again at once (CurrentZoneNeedsRefreshing). A section stays wanted kAheadKeepMs after it was
 * last ahead, so a road along a section's edge does not switch the zones back and forth.
 */
const float kAheadStep = 40.0f;
const float kMaxAheadMetres = 520.0f;
const float kMinAheadSpeed = 12.0f;
const DWORD kAheadKeepMs = 1500;
const int kMaxAheadZones = 12;
const int kZoneNeedsRefreshing = 0x2b0;

struct AheadZone
{
    bVector2 point;
    short number;
    bool applied;
    DWORD seen;
};

AheadZone g_aheadZones[kMaxAheadZones];
int g_aheadCount;
bVector3 g_aheadFrom;
DWORD g_aheadAt;

/** When the streamer last had to throw a section out for room: nothing more is wanted ahead for a while after. */
DWORD g_lastJettison;
const DWORD kShortOfRoomMs = 10000;

bool ShortOfRoom()
{
    return g_lastJettison && GetTickCount() - g_lastJettison < kShortOfRoomMs;
}

float AheadSeconds()
{
    static const float seconds = nfs::config::Float("Streaming", "AheadSeconds", 3.0f);
    return seconds;
}

void ForgetOldAhead(DWORD now)
{
    int kept = 0;
    for (int i = 0; i < g_aheadCount; ++i)
        if (now - g_aheadZones[i].seen <= kAheadKeepMs)
            g_aheadZones[kept++] = g_aheadZones[i];
    g_aheadCount = kept;
}

/** Notes the drivable sections ahead of the streaming position; one not wanted yet switches the zones again. */
void LookAhead(TrackStreamer *streamer, const bVector3 &at, const bVector3 &velocity)
{
    const float seconds = AheadSeconds();
    const float speed = sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);
    if (seconds <= 0.0f || speed < kMinAheadSpeed || ShortOfRoom())
        return;
    const DWORD now = GetTickCount();
    const float mx = at.x - g_aheadFrom.x, my = at.y - g_aheadFrom.y;
    if (mx * mx + my * my < 100.0f && now - g_aheadAt < 250)
        return;
    g_aheadFrom = at;
    g_aheadAt = now;
    float reach = speed * seconds;
    reach = reach < kMaxAheadMetres ? reach : kMaxAheadMetres;
    const float ux = velocity.x / speed, uy = velocity.y / speed;
    bool fresh = false;
    for (float d = kAheadStep; d < reach + 0.5f * kAheadStep; d += kAheadStep)
    {
        const bVector2 point(at.x + ux * d, at.y + uy * d);
        const char *drivable = reinterpret_cast<const char *>(TheVisibleSectionManager.FindDrivableSection(&point));
        if (!drivable)
            continue;
        const short number = *reinterpret_cast<const short *>(drivable + 0x0c);
        int i = 0;
        while (i < g_aheadCount && g_aheadZones[i].number != number)
            ++i;
        if (i == g_aheadCount)
        {
            if (g_aheadCount == kMaxAheadZones)
                ForgetOldAhead(now);
            if (g_aheadCount == kMaxAheadZones)
                break;
            g_aheadZones[g_aheadCount++] = {point, number, false, now};
        }
        g_aheadZones[i].seen = now;
        fresh |= !g_aheadZones[i].applied;
    }
    if (fresh)
        *Field(streamer, kZoneNeedsRefreshing) = 1;
}

/** Adds the sections seen from the drivable sections ahead to the first zone's (DetermineCurrentSections). */
void WantAheadSections(TrackStreamer *streamer)
{
    ForgetOldAhead(GetTickCount());
    if (ShortOfRoom())
    {
        g_aheadCount = 0;
        return;
    }
    int added = 0;
    for (int i = 0; i < g_aheadCount; ++i)
    {
        AheadZone &z = g_aheadZones[i];
        z.applied = true;
        // Found again by its point: the track's sections move when a region is loaded again.
        const char *drivable = reinterpret_cast<const char *>(TheVisibleSectionManager.FindDrivableSection(&z.point));
        if (!drivable || *reinterpret_cast<const short *>(drivable + 0x0c) != z.number)
            continue;
        const short count = *reinterpret_cast<const short *>(drivable + 0x10);
        short sections[72];
        const int n = count < 72 ? count : 72;
        for (int k = 0; k < n; ++k)
            sections[k] = reinterpret_cast<const short *>(drivable + 0x12)[k];
        if (CurrentCount(streamer) + n > kMaxCurrent)
            continue;
        streamer->AddCurrentStreamingSections(sections, n, 0);
        added += n;
    }
    if (Debugging() && g_aheadCount)
    {
        char text[160];
        int length = 0;
        for (int i = 0; i < g_aheadCount && length < 140; ++i)
            length += wsprintfA(text + length, " %d", g_aheadZones[i].number);
        text[length] = 0;
        nfs::Log("streaming: ahead of the car:%s (%d sections seen from them)", text, added);
    }
}

/** Unloads the kept sections that have faded out (all of them with `all`), as SwitchZones unloads those no zone wants. */
void FinishLeaving(TrackStreamer *streamer, bool all)
{
    const DWORD now = GetTickCount();
    int kept = 0, unloaded = 0;
    g_finishing = true;
    for (int i = 0; i < g_leavingCount; ++i)
    {
        const Leaving l = g_leaving[i];
        const short number = SectionNumber(l.section);
        if (Status(l.section) != kActivated)
            continue;
        if (!all && !nfs::culling::SectionFadedOut(number) && now - l.since < kLeaveLimitMs)
        {
            g_leaving[kept++] = l;
            continue;
        }
        if (!unloaded)
            PostLoadFixupDisabled = true;
        if (Debugging())
            nfs::Log("streaming: %d unloaded %u ms after the zones let it go%s", number, now - l.since,
                     all ? " (memory wanted)" : "");
        RemoveFromCurrent(streamer, l.section);
        CurrentZones(l.section) = 0;
        streamer->UnloadSection(l.section);
        nfs::culling::SectionUnloading(number, false);
        ++unloaded;
    }
    g_leavingCount = kept;
    g_finishing = false;
    if (unloaded)
    {
        PostLoadFixupDisabled = false;
        PostLoadFixup();
        *Field(streamer, 0x2bc) = 1;
    }
}

void Note(const char *what, const TrackStreamingSection *section)
{
    if (g_counts.length < 480)
        g_counts.length += wsprintfA(g_counts.sections + g_counts.length, " %s%d", what, SectionNumber(section));
    if (GetTickCount() - g_reported < kReportPeriod)
        return;
    g_reported = GetTickCount();
    nfs::Log("streaming: %d activated, %d unloads, %d jettisons (%d KB):%s", g_counts.loads, g_counts.unloads,
             g_counts.jettisons, g_counts.jettisonedBytes >> 10, g_counts.sections);
    g_counts = Counts();
}
} // namespace

/**
 * The pool (original 0x72d9c0: bMalloc and a TSMemoryPool over it; the memory at +0x838, its size at +0x83c, the
 * TSMemoryPool at +0x844): the original's room and Streaming.ExtraPoolMegabytes more. The streamer compares addresses
 * as signed numbers (whether memory is the pool's own, IsUserMemory and FindSectionByAddress), so a pool that runs
 * across 2 GB is taken for two: the game's allocator placed 687 MB from just under 2 GB in a window (the system's
 * overlays take room lower down), sections above the line were freed as if they were not the pool's, and the game
 * crashed at the first section of every start (0064a9f1 SplitPermTempChunks, 2026-11-14). The pool is now the
 * system's memory at the top of the address space, all of it above 2 GB (all on one side of the line); where the
 * system cannot give that, the game's allocator as before, with as much as fits below 2 GB. It is never freed.
 */
void TrackStreamer::InitMemoryPool(int bytes)
{
    const int wanted = nfs::config::Int("Streaming", "ExtraPoolMegabytes", 512);
    const SIZE_T total = static_cast<SIZE_T>(bytes) + static_cast<SIZE_T>(wanted) * kMegabyte;
    void *memory = VirtualAlloc(nullptr, total, MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN, PAGE_READWRITE);
    if (memory && reinterpret_cast<uintptr_t>(memory) < 0x80000000u)
    {
        VirtualFree(memory, 0, MEM_RELEASE);
        memory = nullptr;
    }
    if (memory)
    {
        char *self = reinterpret_cast<char *>(this);
        *reinterpret_cast<void **>(self + 0x838) = memory;
        *reinterpret_cast<int *>(self + 0x83c) = static_cast<int>(total);
        void *pool = NFS_ORIGINAL(0x652ad0, void *(__cdecl *)(unsigned int))(0x2750);
        if (pool)
            NFS_ORIGINAL(0x72d670, void(__thiscall *)(void *, void *, int, const char *, int))(
                pool, memory, static_cast<int>(total), reinterpret_cast<const char *>(0x8b2b00), 7);
        *reinterpret_cast<void **>(self + 0x844) = pool;
        nfs::Log("streaming: pool %d MB + %d MB at %08x (above 2 GB)", bytes / kMegabyte, wanted,
                 reinterpret_cast<unsigned int>(memory));
        return;
    }
    int extra = wanted;
    for (; extra > 0; extra -= 32)
    {
        const SIZE_T size = static_cast<SIZE_T>(bytes) + static_cast<SIZE_T>(extra) * kMegabyte + 0x10000;
        void *probe = VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
        const bool low = probe && reinterpret_cast<uintptr_t>(probe) + size <= 0x80000000u;
        if (probe)
            VirtualFree(probe, 0, MEM_RELEASE);
        if (low)
            break;
    }
    extra = extra > 0 ? extra : 0;
    NFS_ORIGINAL(0x72d9c0, void(__thiscall *)(TrackStreamer *, int))(this, bytes + extra * kMegabyte);
    nfs::Log("streaming: pool %d MB + %d MB (of %d MB wanted) below 2 GB at %08x", bytes / kMegabyte, extra, wanted,
             *reinterpret_cast<const unsigned int *>(reinterpret_cast<const char *>(this) + 0x838));
}

void TrackStreamer::ActivateSection(TrackStreamingSection *section)
{
    nfs::shadow::debug::NoteStreaming(SectionNumber(section), true);
    if (Debugging())
    {
        ++g_counts.loads;
        Note("+", section);
    }
    LARGE_INTEGER start, end, frequency;
    if (Debugging())
    {
        const char *f = reinterpret_cast<const char *>(section);
        if (*reinterpret_cast<const int *>(f + 0x18) > 0)
            nfs::Log("streaming: activating %d at %08x: size %d KB, loaded %d KB, perm %d KB", SectionNumber(section),
                     *reinterpret_cast<const unsigned int *>(f + 0x4c), *reinterpret_cast<const int *>(f + 0x18) >> 10,
                     *reinterpret_cast<const int *>(f + 0x54) >> 10, *reinterpret_cast<const int *>(f + 0x20) >> 10);
        nfs::streamprofile::g_on = true;
        for (LONGLONG &t : nfs::streamprofile::g_ticks)
            t = 0;
        QueryPerformanceCounter(&start);
    }
    NFS_ORIGINAL(0x727de0, void(__thiscall *)(TrackStreamer *, TrackStreamingSection *))(this, section);
    if (Debugging())
    {
        QueryPerformanceCounter(&end);
        QueryPerformanceFrequency(&frequency);
        const int us = static_cast<int>(static_cast<double>(end.QuadPart - start.QuadPart) * 1e6 /
                                        static_cast<double>(frequency.QuadPart));
        nfs::streamprofile::g_on = false;
        const double ms = 1000.0 / static_cast<double>(frequency.QuadPart);
        const int packs = static_cast<int>(nfs::streamprofile::g_ticks[nfs::streamprofile::kTexturePacks] * ms * 10.0);
        const int images = static_cast<int>(nfs::streamprofile::g_ticks[nfs::streamprofile::kTextureImages] * ms * 10.0);
        const int solids = static_cast<int>(nfs::streamprofile::g_ticks[nfs::streamprofile::kSolidLists] * ms * 10.0);
        const int plat = static_cast<int>(nfs::streamprofile::g_ticks[nfs::streamprofile::kSolidPlat] * ms * 10.0);
        const int notify = static_cast<int>(nfs::streamprofile::g_ticks[nfs::streamprofile::kSolidNotify] * ms * 10.0);
        const int materials = static_cast<int>(nfs::streamprofile::g_ticks[nfs::streamprofile::kSolidMaterials] * ms * 10.0);
        const int uploads = static_cast<int>(nfs::streamprofile::g_ticks[nfs::streamprofile::kSolidUploads] * ms * 10.0);
        const int arenas = static_cast<int>(nfs::streamprofile::g_ticks[nfs::streamprofile::kArenaAllocate] * ms * 10.0);
        const int copies = static_cast<int>(nfs::streamprofile::g_ticks[nfs::streamprofile::kStagingCopy] * ms * 10.0);
        if (us >= 1000)
            nfs::Log("streaming: %d activated in %d.%d ms (%d KB): texture packs %d.%d ms (images %d.%d), solids %d.%d ms "
                     "(platform data %d.%d: materials %d.%d, buffers %d.%d [arena %d.%d, staging copy %d.%d]; models "
                     "attached %d.%d)",
                     SectionNumber(section), us / 1000, us / 100 % 10,
                     *reinterpret_cast<const int *>(reinterpret_cast<const char *>(section) + 0x18) >> 10, packs / 10,
                     packs % 10, images / 10, images % 10, solids / 10, solids % 10, plat / 10, plat % 10,
                     materials / 10, materials % 10, uploads / 10, uploads % 10, arenas / 10, arenas % 10, copies / 10,
                     copies % 10, notify / 10, notify % 10);
    }
    nfs::vanillafixes::SectionActivated(SectionNumber(section));
    nfs::clipwatch::Event("section activated", SectionNumber(section));
}

void TrackStreamer::UnloadSection(TrackStreamingSection *section)
{
    if (!g_finishing && IsLeaving(section))
    {
        // Unloaded by the streamer itself (short of memory, a restart): no longer ours to keep.
        int kept = 0;
        for (int i = 0; i < g_leavingCount; ++i)
            if (g_leaving[i].section != section)
                g_leaving[kept++] = g_leaving[i];
        g_leavingCount = kept;
        CurrentZones(section) &= ~kLeavingBit;
        nfs::culling::SectionUnloading(SectionNumber(section), false);
    }
    nfs::shadow::debug::NoteStreaming(SectionNumber(section), false);
    nfs::clipwatch::Event("section UNLOADED", SectionNumber(section));
    if (Debugging())
    {
        ++g_counts.unloads;
        Note("-", section);
    }
    NFS_ORIGINAL(0x727e60, void(__thiscall *)(TrackStreamer *, TrackStreamingSection *))(this, section);
}

void TrackStreamer::JettisonSection(TrackStreamingSection *section)
{
    g_lastJettison = GetTickCount();
    nfs::clipwatch::Event("section thrown out", SectionNumber(section));
    if (Debugging())
    {
        ++g_counts.jettisons;
        g_counts.jettisonedBytes += *reinterpret_cast<const int *>(reinterpret_cast<const char *>(section) + 0x18);
        Note("x", section);
    }
    NFS_ORIGINAL(0x72c370, void(__thiscall *)(TrackStreamer *, TrackStreamingSection *))(this, section);
}

namespace
{
/** StreamingPositionEntry (0x64 bytes from +0xc0, one for each player): position, velocity and direction (x, y),
 * whether it is set and whether a camera following a car gave it. */
const int kStreamingPositions = 0xc0;
const int kStreamingPositionSize = 0x64;
const int kStreamingPositionSet = 0x1c;
const int kStreamingPositionFollowing = 0x1d;
/** The player car's IRigidBody (UTL::COM::Object::_IList::Find handle) and its LinearVelocity. */
const unsigned int kRigidBodyHandle = 0x4039f0;
const int kBodyLinearVelocity = 0x24;
/** A following camera farther than this from the player's car follows some other car: the fix leaves it be. */
const float kMaxFollowDistance = 50.0f;

template <typename R, typename... A> R Virtual(void *object, int offset, A... args)
{
    typedef R(__thiscall * Function)(void *, A...);
    return (*reinterpret_cast<Function *>(*reinterpret_cast<char **>(object) + offset))(object, args...);
}

/** Debug.StreamAt=X,Y,Z, or false. */
bool StreamAt(bVector3 &at)
{
    static int state;
    static bVector3 point;
    if (!state)
    {
        char text[64];
        nfs::config::String("Debug", "StreamAt", "", text, sizeof(text));
        float v[3] = {};
        int n = 0;
        for (const char *p = text; *p && n < 3;)
        {
            char number[24];
            int k = 0;
            while (*p && *p != ',' && k < 23)
                number[k++] = *p++;
            number[k] = 0;
            v[n++] = bStrToFloat(number);
            if (*p == ',')
                ++p;
        }
        state = n == 3 ? 1 : 2;
        point = bVector3(v[0], v[1], v[2]);
    }
    at = point;
    return state == 1;
}

/** The player car's place and velocity (world axes), for the streamer; false without one. */
bool PlayerCarStream(bVector3 &position, bVector3 &velocity)
{
    float car[4];
    if (!nfs::pose::PlayerCar(car))
        return false;
    position = bVector3(car[0], car[1], car[2]);
    velocity = bVector3(0.0f, 0.0f, 0.0f);
    if (void *body = nfs::pose::PlayerInterface(kRigidBodyHandle))
    {
        const float *v = Virtual<const float *>(body, kBodyLinearVelocity);
        velocity = bVector3(v[2], -v[0], v[1]);
    }
    return true;
}
} // namespace

/**
 * Where the streamer loads around for a player (original 0x7238d0: the position, the velocity and direction's x and y,
 * and whether a camera following a car gave them). The vanilla fix puts the player's car there in place of the camera
 * that follows it; Debug.StreamAt puts the camera somewhere else.
 */
void TrackStreamer::PredictStreamingPosition(int number, const bVector3 *position, const bVector3 *velocity,
                                             const bVector3 *direction, bool following)
{
    bVector3 at = *position, speed = *velocity;
    bVector3 held;
    if (StreamAt(held))
    {
        at = held;
        speed = bVector3(0.0f, 0.0f, 0.0f);
    }
    bVector3 car, carSpeed;
    if (number == 0 && following && nfs::vanillafixes::On() && PlayerCarStream(car, carSpeed))
    {
        const float dx = at.x - car.x, dy = at.y - car.y, dz = at.z - car.z;
        const float distance = sqrtf(dx * dx + dy * dy + dz * dz);
        if (distance < kMaxFollowDistance)
        {
            if (Debugging())
            {
                static float farthest;
                static DWORD reported;
                farthest = distance > farthest ? distance : farthest;
                if (GetTickCount() - reported >= kReportPeriod)
                {
                    reported = GetTickCount();
                    nfs::Log("streaming: following the player's car (the camera up to %d cm from it)",
                             static_cast<int>(farthest * 100.0f));
                    farthest = 0.0f;
                }
            }
            at = car;
            speed = carSpeed;
        }
    }
    float *entry = reinterpret_cast<float *>(Field(this, kStreamingPositions + number * kStreamingPositionSize));
    entry[0] = at.x;
    entry[1] = at.y;
    entry[2] = at.z;
    entry[3] = speed.x;
    entry[4] = speed.y;
    entry[5] = direction->x;
    entry[6] = direction->y;
    *Field(entry, kStreamingPositionSet) = 1;
    *Field(entry, kStreamingPositionFollowing) = following;
    if (number == 0)
        LookAhead(this, at, speed);
}

void TrackStreamer::SwitchZones(short *zones)
{
    nfs::clipwatch::Event("zones switched to", zones[0], zones[1]);
    NFS_ORIGINAL(0x730460, void(__thiscall *)(TrackStreamer *, short *))(this, zones);
}

void TrackStreamer::ServiceGameState()
{
    NFS_ORIGINAL(0x7310b0, void(__thiscall *)(TrackStreamer *))(this);
    if (g_leavingCount)
        FinishLeaving(this, false);
}

NFS_REPLACE(0x72dd70, DetermineCurrentSections)
NFS_REPLACE(0x730bf0, MakeRoomInPool)

/** SwitchZones' choice of the sections its zones want (sub_72DD70, between RemoveCurrentStreamingSections and the
 * unload of every activated section no zone wants). Those about to be unloaded, and those still fading from an earlier
 * switch that the new zones do not want back, are kept: marked as current, so the unload passes over them. */
void __fastcall DetermineCurrentSections(TrackStreamer *self, void *)
{
    NFS_ORIGINAL(0x72dd70, void(__thiscall *)(TrackStreamer *))(self);
    WantNearSections(self);
    WantAheadSections(self);
    if (!FadeUnloads())
        return;
    int kept = 0;
    for (int i = 0; i < g_leavingCount; ++i)
    {
        const Leaving l = g_leaving[i];
        if (Status(l.section) != kActivated)
            continue;
        if (CurrentZones(l.section))
        {
            nfs::culling::SectionUnloading(SectionNumber(l.section), false);
            continue;
        }
        g_leaving[kept++] = l;
    }
    g_leavingCount = kept;
    const int count = reinterpret_cast<const int *>(self)[1];
    char *sections = *reinterpret_cast<char **>(self);
    for (int i = 0; i < count && g_leavingCount < kMaxLeaving; ++i)
    {
        TrackStreamingSection *s = reinterpret_cast<TrackStreamingSection *>(sections + i * kSectionStride);
        if (Status(s) != kActivated || CurrentZones(s) || AlwaysLoaded(SectionNumber(s)) || IsLeaving(s))
            continue;
        g_leaving[g_leavingCount++] = {s, GetTickCount()};
        nfs::culling::SectionUnloading(SectionNumber(s), true);
        if (Debugging())
            nfs::Log("streaming: %d kept while it fades out", SectionNumber(s));
    }
    // RemoveCurrentStreamingSections takes the mark off again at the next switch.
    kept = 0;
    for (int i = 0; i < g_leavingCount; ++i)
    {
        TrackStreamingSection *s = g_leaving[i].section;
        if (CurrentCount(self) >= kMaxCurrent)
        {
            // No room to keep it: SwitchZones unloads it now.
            nfs::culling::SectionUnloading(SectionNumber(s), false);
            continue;
        }
        CurrentZones(s) = kLeavingBit;
        Current(self)[CurrentCount(self)++] = s;
        g_leaving[kept++] = g_leaving[i];
    }
    g_leavingCount = kept;
}

/** Room for a section to load (TrackStreamer::MakeSpaceInPool(int, bool)): when the pool is short, the kept sections go
 * first, as SwitchZones had unloaded them already in the original. */
bool __fastcall MakeRoomInPool(TrackStreamer *self, void *, int bytes, bool jettison)
{
    if (g_leavingCount && bCountFreeMemory(kSectionPool) < bytes)
        FinishLeaving(self, true);
    return NFS_ORIGINAL(0x730bf0, bool(__thiscall *)(TrackStreamer *, int, bool))(self, bytes, jettison);
}
