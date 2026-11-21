/** -set Dev.RoadDump=1: writes the loaded road network to roads.csv in the dev folder once, in the world. */
#include <windows.h>

#include "core/config.h"
#include "core/log.h"
#include "dev/dev.h"
#include "dev/roaddump.h"
#include "legacy/link.h"

namespace nfs::roaddump
{
namespace
{
const bool *const kValid = reinterpret_cast<const bool *>(0x9b3828);
const unsigned int *const kSegmentCount = reinterpret_cast<const unsigned int *>(0x9b3874);
const unsigned char *const *const kNodes = reinterpret_cast<const unsigned char *const *>(0x9b38bc);
const unsigned char *const *const kSegments = reinterpret_cast<const unsigned char *const *>(0x9b38c0);
const unsigned char *const *const kProfiles = reinterpret_cast<const unsigned char *const *>(0x9b38b8);
void *const kTrackPaths = reinterpret_cast<void *>(0x9aa540);
const float *const kLengthScale = reinterpret_cast<const float *>(0x8eb34c);
const int kZoneTypes = 15;
bool g_done;

const unsigned char *Profile(int node)
{
    const short index = *reinterpret_cast<const short *>(*kNodes + node * 0x20 + 0xe);
    return index < 0 ? nullptr : *kProfiles + index * 0x40;
}

void Lanes(const unsigned char *profile, int &traffic, char *types)
{
    traffic = 0;
    types[0] = 0;
    if (!profile)
        return;
    const int count = profile[0] < 15 ? profile[0] : 15;
    for (int i = 0; i < count; ++i)
    {
        const unsigned int lane = reinterpret_cast<const unsigned int *>(profile + 4)[i];
        traffic += (lane & 0xf) == 1;
        types[i] = "0123456789abcdef"[lane & 0xf];
    }
    types[count] = 0;
}
} // namespace

void Update()
{
    static const bool wanted = nfs::config::Int("Dev", "RoadDump", 0) != 0;
    if (!wanted || g_done || !*kValid || !*kSegments || !*kNodes || !*reinterpret_cast<int *>(0x92cd84))
        return;
    g_done = true;
    char path[MAX_PATH];
    nfs::dev::Folder(path, "");
    lstrcatA(path, "\\roads.csv");
    HANDLE out = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE)
        return;
    char line[512];
    DWORD written;
    int n = wsprintfA(line, "segment,x,y,z,length,flags_a,flags_b,raw,traffic_a,traffic_b,lanes_a,lanes_b,profile_b1,zones\n");
    WriteFile(out, line, n, &written, nullptr);
    const int count = static_cast<int>(*kSegmentCount);
    for (int s = 0; s < count; ++s)
    {
        const unsigned char *segment = *kSegments + s * 0x16;
        const unsigned short a = *reinterpret_cast<const unsigned short *>(segment);
        const unsigned short b = *reinterpret_cast<const unsigned short *>(segment + 2);
        const float *pa = reinterpret_cast<const float *>(*kNodes + a * 0x20);
        const float *pb = reinterpret_cast<const float *>(*kNodes + b * 0x20);
        const float sim[3] = {(pa[0] + pb[0]) * 0.5f, (pa[1] + pb[1]) * 0.5f, (pa[2] + pb[2]) * 0.5f};
        const float world[2] = {sim[2], -sim[0]};
        int trafficA, trafficB;
        char typesA[16], typesB[16];
        const unsigned char *profileA = Profile(a);
        Lanes(profileA, trafficA, typesA);
        Lanes(Profile(b), trafficB, typesB);
        char raw[64];
        int at = 0;
        for (int i = 6; i < 0x16; ++i)
            at += wsprintfA(raw + at, "%02x", segment[i]);
        char zones[256];
        int zat = 0;
        zones[0] = 0;
        for (int t = 0; t < kZoneTypes; ++t)
        {
            const char *zone = NFS_ORIGINAL(0x727480, const char *(__thiscall *)(void *, const float *, int, void *))(
                kTrackPaths, world, t, nullptr);
            if (zone)
                zat += wsprintfA(zones + zat, "%d:%08x ", t, *reinterpret_cast<const unsigned int *>(zone + 0x30));
        }
        n = wsprintfA(line, "%d,%d,%d,%d,%d,%u,%u,%s,%d,%d,%s,%s,%u,%s\n", s, static_cast<int>(world[0]),
                      static_cast<int>(world[1]), static_cast<int>(sim[1]),
                      static_cast<int>(*reinterpret_cast<const unsigned short *>(segment + 4) * *kLengthScale),
                      segment[0xa], segment[0xb], raw, trafficA, trafficB, typesA, typesB,
                      profileA ? profileA[1] : 255u, zones);
        WriteFile(out, line, n, &written, nullptr);
    }
    CloseHandle(out);
    nfs::Log("roaddump: %d segments to %s", count, path);
}
} // namespace nfs::roaddump
