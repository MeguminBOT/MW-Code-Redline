/**
 * A -headless run keeps its profiles beside its exe instead of in the player's Documents: the memory card's setup
 * (0x6cbf00) asks SHGetFolderPathA for Documents, through the import table. tools/rungame.py starts such a run from a
 * folder of its own while the player may be playing, so it never sees the player's profiles. -testsaves does the same
 * for a run on the screen (tools/rungame.py --visible --test-saves). Not in the original game.
 *
 * A -dev run keeps them in the dev folder's saves (nfs::dev::Folder), and starts with the Developer profile there: the
 * first time, it is written from the copy the packs hold (DEV\DEVELOPER.SAV: tools/devprofile.py makes it from the
 * test profile; source/dev/DevProfile.cpp loads it at the start, gameplay unlocks everything for it).
 */
#include <windows.h>

#include "engine/rendering/display/display.h"
#include "engine/rendering/vulkan/renderer.h"
#include "engine/resources/pack.h"
#include "core/log.h"
#include "dev/dev.h"

namespace
{
typedef HRESULT(__stdcall *FolderPath)(HWND, int, HANDLE, DWORD, char *);
FolderPath *const kImportedFolderPath = reinterpret_cast<FolderPath *>(0x8902ec);
const int kFolderMask = 0xff;
/** CSIDL_PERSONAL. */
const int kDocuments = 0x05;
FolderPath g_original;

const char kDeveloperSeed[] = "DEV\\DEVELOPER.SAV";
const char kDeveloper[] = "Developer";

/** The Developer profile in the dev saves, from the packs' copy when it is not there yet. */
void SeedDeveloper(const char *saves)
{
    char folder[MAX_PATH], file[MAX_PATH];
    wsprintfA(folder, "%s\\NFS Most Wanted", saves);
    CreateDirectoryA(folder, nullptr);
    wsprintfA(folder, "%s\\NFS Most Wanted\\%s", saves, kDeveloper);
    wsprintfA(file, "%s\\%s", folder, kDeveloper);
    if (GetFileAttributesA(file) != INVALID_FILE_ATTRIBUTES)
        return;
    uint32_t size = 0;
    uint8_t *data = nfs::pack::ReadWhole(kDeveloperSeed, size);
    if (!data)
    {
        nfs::Log("dev: no %s in the packs, no Developer profile", kDeveloperSeed);
        return;
    }
    CreateDirectoryA(folder, nullptr);
    HANDLE out = CreateFileA(file, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(out, data, size, &written, nullptr);
        CloseHandle(out);
        nfs::Log("dev: the Developer profile written to %s", file);
    }
    ::operator delete(data);
}

HRESULT __stdcall FolderPathForRun(HWND window, int folder, HANDLE token, DWORD flags, char *path)
{
    if ((folder & kFolderMask) != kDocuments)
        return g_original(window, folder, token, flags, path);
    if (nfs::dev::Enabled() && !nfs::vk::HasArgument("-testsaves"))
    {
        nfs::dev::Folder(path, "saves");
        SeedDeveloper(path);
        nfs::Log("dev: profiles under %s", path);
        return S_OK;
    }
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/')
        --n;
    path[n > 0 ? n - 1 : 0] = 0;
    nfs::Log("headless run: profiles under %s", path);
    return S_OK;
}
} // namespace

namespace nfs::display
{
void RedirectHeadlessSaves()
{
    if (!Headless() && !nfs::vk::HasArgument("-testsaves") && !nfs::dev::Enabled())
        return;
    DWORD protection;
    if (!VirtualProtect(kImportedFolderPath, sizeof(*kImportedFolderPath), PAGE_READWRITE, &protection))
        return;
    g_original = *kImportedFolderPath;
    *kImportedFolderPath = FolderPathForRun;
    VirtualProtect(kImportedFolderPath, sizeof(*kImportedFolderPath), protection, &protection);
}
} // namespace nfs::display
