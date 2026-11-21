/**
 * Car mods as text (gameplay/vehicles/carmods.h, docs/MODDING.md). Not in the original game.
 *
 * Each mods\cars\NAME.ini is read once; its values are written into the layouts of the collections the pvehicle NAME
 * references (an Attrib::Collection's layout at +0x18, read in place by every instance), after the realistic edits
 * when those are in use (gameplay/vehicles/realism.h writes its own bytes over the same fields at every switch, so the mods go on
 * again after it). The fields and their places are the live tuning's (nfs::tuning::FieldInfo).
 */
#include <string.h>
#include <windows.h>

#include "game/gen/api.h"
#include "gameplay/vehicles/carmods.h"
#include "legacy/link.h"
#include "core/log.h"
#include "gameplay/vehicles/performance.h"
#include "gameplay/vehicles/realism.h"
#include "gameplay/vehicles/tuning.h"

namespace
{
const int kCollectionLayout = 0x18;
const int kArrayCount = 2;
const int kArrayData = 8;
const int kMaxReferences = 3;
const int kMaxValues = 16;
const int kMaxEdits = 2048;
const int kInstanceSize = 0x20;

typedef const void *(__cdecl *FindCollection)(unsigned int, unsigned int);

/** One field of one collection: the values to write. */
struct Edit
{
    unsigned char *layout;
    nfs::tuning::FieldSpec field;
    float values[kMaxValues];
    int count;
    /** 0 both car performances, 1 standard only, 2 realistic only. */
    int mode;
};

struct State
{
    bool loaded;
    Edit *edits;
    int count;
};

State g_state;

unsigned char *LayoutOf(const void *collection)
{
    return collection ? *reinterpret_cast<unsigned char *const *>(static_cast<const char *>(collection) +
                                                                  kCollectionLayout)
                      : nullptr;
}

/** The layout of `cls` the pvehicle `vehicle` references: its stock collection, or with `top` its fully upgraded one
 * (the last of the list, when that is another). Null when it has none. */
unsigned char *Referenced(const void *vehicle, const char *cls, bool top)
{
    char storage[kInstanceSize] = {};
    NFS_ORIGINAL(0x452380, void(__thiscall *)(void *, const void *, unsigned int, void *))(storage, vehicle, 0, nullptr);
    const Attrib::Instance *instance = reinterpret_cast<const Attrib::Instance *>(storage);
    const unsigned int key = Attrib::StringToKey(cls);
    unsigned char *found = nullptr, *stock = nullptr;
    for (int i = 0; i < kMaxReferences; ++i)
    {
        const unsigned int *ref = static_cast<const unsigned int *>(instance->GetAttributePointer(key, i));
        if (!ref)
            break;
        unsigned char *layout = LayoutOf(NFS_ORIGINAL(0x455fd0, FindCollection)(ref[0], ref[1]));
        if (i == 0)
            stock = layout;
        if (layout)
            found = layout;
    }
    NFS_ORIGINAL(0x45a430, void(__thiscall *)(void *))(storage);
    if (!top)
        return stock;
    return found != stock ? found : nullptr;
}

/** Numbers separated by commas or spaces; returns how many (at most `max`). */
int Numbers(const char *p, float *out, int max)
{
    int n = 0;
    while (*p && n < max)
    {
        while (*p == ' ' || *p == ',' || *p == '\t')
            ++p;
        if (!*p || *p == ';')
            break;
        bool negative = *p == '-';
        if (*p == '-' || *p == '+')
            ++p;
        double value = 0.0, scale = 1.0;
        bool digits = false;
        while (*p >= '0' && *p <= '9')
        {
            value = value * 10.0 + (*p++ - '0');
            digits = true;
        }
        if (*p == '.')
            for (++p; *p >= '0' && *p <= '9'; ++p)
            {
                scale *= 0.1;
                value += (*p - '0') * scale;
                digits = true;
            }
        if (!digits)
            break;
        out[n++] = static_cast<float>(negative ? -value : value);
    }
    return n;
}

/** "engine top real" into the class, top and mode. */
bool ParseSection(const char *section, char *cls, int size, bool &top, int &mode)
{
    top = false;
    mode = 0;
    int n = 0;
    const char *p = section;
    while (*p == ' ')
        ++p;
    while (*p && *p != ' ' && n < size - 1)
        cls[n++] = *p++;
    cls[n] = 0;
    while (*p)
    {
        while (*p == ' ')
            ++p;
        char word[16];
        int w = 0;
        while (*p && *p != ' ' && w < 15)
            word[w++] = *p++;
        word[w] = 0;
        if (!lstrcmpiA(word, "top"))
            top = true;
        else if (!lstrcmpiA(word, "standard"))
            mode = 1;
        else if (!lstrcmpiA(word, "real"))
            mode = 2;
        else if (w)
            return false;
    }
    return n > 0;
}

/** Reads one car's file into edits. */
void LoadFile(const char *path, const char *car)
{
    const void *vehicle =
        NFS_ORIGINAL(0x455fd0, FindCollection)(Attrib::StringToKey("pvehicle"), Attrib::StringToKey(car));
    if (!vehicle)
    {
        nfs::Log("carmods: %s: no pvehicle %s", path, car);
        return;
    }
    static char sections[4096];
    static char lines[8192];
    const DWORD length = GetPrivateProfileSectionNamesA(sections, sizeof(sections), path);
    int values = 0, unknown = 0;
    for (const char *s = sections; s < sections + length && *s; s += lstrlenA(s) + 1)
    {
        char group[32];
        bool top;
        int mode;
        if (!ParseSection(s, group, sizeof(group), top, mode) || !nfs::tuning::GroupName(group))
        {
            nfs::Log("carmods: %s: section [%s] is not a class (engine, induction, transmission, chassis, tires, brakes, "
                     "nos, rigidbodyspecs, vehicle) with top, standard or real after it", path, s);
            continue;
        }
        const char *cls = nfs::tuning::GroupName(group);
        unsigned char *layout = !lstrcmpiA(cls, "pvehicle") ? (top ? nullptr : LayoutOf(vehicle))
                                                            : Referenced(vehicle, cls, top);
        if (!layout)
        {
            nfs::Log("carmods: %s: %s has no %s%s collection", path, car, cls, top ? " upgraded" : "");
            continue;
        }
        GetPrivateProfileSectionA(s, lines, sizeof(lines), path);
        for (const char *l = lines; *l; l += lstrlenA(l) + 1)
        {
            if (*l == ';' || *l == '#')
                continue;
            char key[40];
            int k = 0;
            const char *p = l;
            while (*p && *p != '=' && k < 39)
                key[k++] = *p++;
            while (k > 0 && key[k - 1] == ' ')
                --k;
            key[k] = 0;
            if (*p != '=' || g_state.count == kMaxEdits)
                continue;
            nfs::tuning::FieldSpec field;
            if (!nfs::tuning::FieldInfo(cls, key, field))
            {
                ++unknown;
                nfs::Log("carmods: %s: [%s] has no field %s", path, s, key);
                continue;
            }
            Edit &e = g_state.edits[g_state.count];
            e.count = Numbers(p + 1, e.values, field.count < kMaxValues ? field.count : kMaxValues);
            if (!e.count)
                continue;
            e.layout = layout;
            e.field = field;
            e.mode = mode;
            ++g_state.count;
            values += e.count;
        }
    }
    nfs::Log("carmods: %s: %d values%s", path, values, unknown ? " (some fields unknown)" : "");
}

void Load()
{
    g_state.loaded = true;
    g_state.edits = static_cast<Edit *>(::operator new(sizeof(Edit) * kMaxEdits));
    char base[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, base, MAX_PATH);
    while (n > 0 && base[n - 1] != '\\' && base[n - 1] != '/')
        --n;
    base[n] = 0;
    char pattern[MAX_PATH];
    wsprintfA(pattern, "%smods\\cars\\*.ini", base);
    WIN32_FIND_DATAA found;
    HANDLE find = FindFirstFileA(pattern, &found);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do
    {
        char car[64];
        lstrcpynA(car, found.cFileName, sizeof(car));
        for (int i = lstrlenA(car) - 1; i >= 0; --i)
            if (car[i] == '.')
            {
                car[i] = 0;
                break;
            }
        char path[MAX_PATH];
        wsprintfA(path, "%smods\\cars\\%s", base, found.cFileName);
        LoadFile(path, car);
    } while (FindNextFileA(find, &found));
    FindClose(find);
}
} // namespace

namespace nfs::carmods
{
void Apply()
{
    if (!g_state.loaded)
        Load();
    if (!g_state.count)
        return;
    const int mode = nfs::realism::Active() ? 2 : 1;
    int written = 0;
    for (int i = 0; i < g_state.count; ++i)
    {
        const Edit &e = g_state.edits[i];
        if (e.mode && e.mode != mode)
            continue;
        if (e.field.array)
        {
            unsigned short *count = reinterpret_cast<unsigned short *>(e.layout + e.field.offset + kArrayCount);
            const unsigned short room = *reinterpret_cast<unsigned short *>(e.layout + e.field.offset);
            int n = e.count;
            if (room && n > room)
                n = room;
            memcpy(e.layout + e.field.offset + kArrayData, e.values, n * sizeof(float));
            *count = static_cast<unsigned short>(n);
        }
        else
            memcpy(e.layout + e.field.offset, e.values, e.count * sizeof(float));
        ++written;
    }
    nfs::performance::Reset();
    nfs::Log("carmods: %d fields written (%s car performance)", written, mode == 2 ? "realistic" : "standard");
}
} // namespace nfs::carmods
