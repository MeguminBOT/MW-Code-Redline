#pragma once

#include <windows.h>

/**
 * Where a section's activation spends its time (-streamdebug, source/gameplay/world/TrackStreaming.cpp): the parts add their
 * ticks while an activation is being timed.
 */
namespace nfs::streamprofile
{
enum Part
{
    kTexturePacks,
    kTextureImages,
    kSolidLists,
    kSolidPlat,
    kSolidNotify,
    kSolidMaterials,
    kSolidUploads,
    kArenaAllocate,
    kStagingCopy,
    kParts,
};
extern bool g_on;
extern LONGLONG g_ticks[kParts];

struct Scope
{
    int part;
    LONGLONG start;
    explicit Scope(int p) : part(p), start(0)
    {
        if (g_on)
        {
            LARGE_INTEGER t;
            QueryPerformanceCounter(&t);
            start = t.QuadPart;
        }
    }
    ~Scope()
    {
        if (g_on && start)
        {
            LARGE_INTEGER t;
            QueryPerformanceCounter(&t);
            g_ticks[part] += t.QuadPart - start;
        }
    }
};
} // namespace nfs::streamprofile
