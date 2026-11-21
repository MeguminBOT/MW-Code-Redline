/**
 * Movie file names (include/engine/movies/movies.h). The game names a movie MOVIES\<name>_<language>_<region>.vp6
 * (CalculateMovieFilename 0x516130): the region is PAL or NTSC by BuildRegion::IsPal 0x64a110, from the language the
 * registry's EA key gives (regeditthang 0x6cb680), and without that key it is US English, so NTSC. Every such name is
 * answered with data/movies/<name>.webm (docs/MOVIES.md), whatever the language and region: the game's VP6 files are
 * not read. FEngMovieStarter::Callback 0x559680 checks the file is there before it plays, and skips a movie without
 * one.
 */
#include <windows.h>

#include "core/config.h"
#include "core/log.h"
#include "engine/movies/movies.h"

namespace
{
bool Exists(const char *path)
{
    const DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool EndsWith(const char *s, int length, const char *tail)
{
    const int n = lstrlenA(tail);
    return length >= n && !lstrcmpiA(s + length - n, tail);
}

bool MovieLog()
{
    static const bool log = nfs::config::Int("Debug", "MovieLog", 0) != 0;
    return log;
}
} // namespace

namespace nfs::movies
{
bool FindWebm(const char *gameName, char *path, size_t size)
{
    static const bool on = nfs::config::Int("Movies", "WebM", 1) != 0;
    if (!on || !gameName || size < MAX_PATH)
        return false;
    const char *base = gameName;
    for (const char *p = gameName; *p; ++p)
        if (*p == '\\' || *p == '/')
            base = p + 1;
    char name[MAX_PATH];
    int n = 0;
    while (base[n] && base[n] != '.' && n + 1 < MAX_PATH)
    {
        name[n] = base[n] >= 'A' && base[n] <= 'Z' ? static_cast<char>(base[n] + 32) : base[n];
        ++n;
    }
    name[n] = 0;
    // <name>_<language>_<region>: two parts off the end.
    for (int cut = 0; cut < 2; ++cut)
    {
        int at = n - 1;
        while (at > 0 && name[at] != '_')
            --at;
        if (at <= 0)
            return false;
        name[at] = 0;
        n = at;
    }
    char folder[MAX_PATH];
    nfs::config::String("Data", "Folder", "data", folder, sizeof(folder));
    if (lstrlenA(folder) + n + 16 >= MAX_PATH)
        return false;
    lstrcpyA(path, folder);
    lstrcatA(path, "\\movies\\");
    lstrcatA(path, name);
    lstrcatA(path, ".webm");
    return Exists(path);
}

const char *FindFile(const char *path, char *buffer, size_t size)
{
    if (!path)
        return path;
    const int length = lstrlenA(path);
    if (!EndsWith(path, length, ".vp6"))
        return path;
    if (FindWebm(path, buffer, size))
    {
        if (MovieLog())
            nfs::Log("movies: %s is %s", path, buffer);
        return buffer;
    }
    return path;
}
} // namespace nfs::movies
