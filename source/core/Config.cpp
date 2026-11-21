#include <windows.h>

#include "game/gen/api.h"
#include "core/config.h"

namespace nfs::config
{
namespace
{
char g_path[MAX_PATH];
char g_profilesPath[MAX_PATH];

/** A file next to the exe. */
void BesideExe(char *path, const char *name)
{
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/')
        --n;
    lstrcpyA(path + n, name);
}

const char *Path()
{
    if (!g_path[0])
        BesideExe(g_path, "speed.ini");
    return g_path;
}

bool IsProfileSection(const char *name)
{
    return !lstrcmpiA(name, "Profiles") || !lstrcmpiA(name, "UnlockAll") ||
           (CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, name, 8, "Profile ", 8) == CSTR_EQUAL);
}

/** profiles.ini; the first call moves speed.ini's profile sections into it (keys it already has stay its own). */
const char *ProfilesPath()
{
    if (g_profilesPath[0])
        return g_profilesPath;
    BesideExe(g_profilesPath, "profiles.ini");
    static char names[8192];
    const DWORD length = GetPrivateProfileSectionNamesA(names, sizeof(names), Path());
    for (const char *name = names; name < names + length && *name; name += lstrlenA(name) + 1)
    {
        if (!IsProfileSection(name))
            continue;
        static char pairs[16384];
        const DWORD size = GetPrivateProfileSectionA(name, pairs, sizeof(pairs), Path());
        for (char *pair = pairs; pair < pairs + size && *pair; pair += lstrlenA(pair) + 1)
        {
            char *equals = pair;
            while (*equals && *equals != '=')
                ++equals;
            if (!*equals)
                continue;
            *equals = 0;
            char existing[4];
            if (!GetPrivateProfileStringA(name, pair, "", existing, sizeof(existing), g_profilesPath))
                WritePrivateProfileStringA(name, pair, equals + 1, g_profilesPath);
            *equals = '=';
        }
        WritePrivateProfileStringA(name, nullptr, nullptr, Path());
    }
    return g_profilesPath;
}

/** `-set Section.Key=Value` on the command line overrides that key for the run; speed.ini is left alone. Finds the
 * value of section.key there and copies it to `out`; false when the command line has none. */
bool Override(const char *section, const char *key, char *out, int size)
{
    const char *line = GetCommandLineA();
    for (const char *p = line; *p; ++p)
    {
        if (p[0] != '-' || p[1] != 's' || p[2] != 'e' || p[3] != 't' || p[4] != ' ' || (p != line && p[-1] != ' '))
            continue;
        const char *q = p + 5;
        while (*q == ' ')
            ++q;
        const char *s = section;
        while (*s && (*q | 0x20) == (*s | 0x20))
            ++q, ++s;
        if (*s || *q != '.')
            continue;
        ++q;
        const char *k = key;
        while (*k && (*q | 0x20) == (*k | 0x20))
            ++q, ++k;
        if (*k || *q != '=')
            continue;
        ++q;
        int n = 0;
        while (q[n] && q[n] != ' ' && n < size - 1)
        {
            out[n] = q[n];
            ++n;
        }
        out[n] = 0;
        return true;
    }
    return false;
}
} // namespace

int Int(const char *section, const char *key, int fallback)
{
    char text[32];
    if (Override(section, key, text, sizeof(text)))
        return static_cast<int>(bStrToLong(text));
    return static_cast<int>(GetPrivateProfileIntA(section, key, fallback, Path()));
}

float Float(const char *section, const char *key, float fallback)
{
    char text[64];
    if (!Override(section, key, text, sizeof(text)))
        GetPrivateProfileStringA(section, key, "", text, sizeof(text), Path());
    return text[0] ? bStrToFloat(text) : fallback;
}

void String(const char *section, const char *key, const char *fallback, char *out, int size)
{
    if (!Override(section, key, out, size))
        GetPrivateProfileStringA(section, key, fallback, out, size, Path());
    for (char *p = out; *p; ++p)
        if (*p >= 'A' && *p <= 'Z')
            *p = static_cast<char>(*p + ('a' - 'A'));
}

void WriteString(const char *section, const char *key, const char *value)
{
    WritePrivateProfileStringA(section, key, value, Path());
}

void WriteInt(const char *section, const char *key, int value)
{
    char text[16];
    wsprintfA(text, "%d", value);
    WriteString(section, key, text);
}

void WriteFloat(const char *section, const char *key, float value)
{
    const bool negative = value < 0.0f;
    const int thousandths = static_cast<int>((negative ? -value : value) * 1000.0f + 0.5f);
    char text[32];
    wsprintfA(text, "%s%d.%03d", negative ? "-" : "", thousandths / 1000, thousandths % 1000);
    WriteString(section, key, text);
}

bool Has(const char *section, const char *key)
{
    char text[8];
    if (Override(section, key, text, sizeof(text)))
        return true;
    return GetPrivateProfileStringA(section, key, "", text, sizeof(text), Path()) > 0;
}

void MoveProfileSections()
{
    ProfilesPath();
}

int ProfileInt(const char *section, const char *key, int fallback)
{
    char text[32];
    if (Override(section, key, text, sizeof(text)))
        return static_cast<int>(bStrToLong(text));
    return static_cast<int>(GetPrivateProfileIntA(section, key, fallback, ProfilesPath()));
}

void ProfileString(const char *section, const char *key, const char *fallback, char *out, int size)
{
    if (!Override(section, key, out, size))
        GetPrivateProfileStringA(section, key, fallback, out, size, ProfilesPath());
    for (char *p = out; *p; ++p)
        if (*p >= 'A' && *p <= 'Z')
            *p = static_cast<char>(*p + ('a' - 'A'));
}

void ProfileWriteString(const char *section, const char *key, const char *value)
{
    WritePrivateProfileStringA(section, key, value, ProfilesPath());
}
} // namespace nfs::config
