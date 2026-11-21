/**
 * -logpatches: logs every range of the game's code that differs in memory from the exe on disk, i.e. what loaded mods
 * (ASI plugins) patched. Debug aid for the Vulkan port, whose replaced functions do not see such patches.
 */
#include <windows.h>

#include "core/log.h"

namespace nfs
{
void LogCodePatches()
{
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    const unsigned char *base = reinterpret_cast<const unsigned char *>(GetModuleHandleA(nullptr));
    const IMAGE_NT_HEADERS *nt =
        reinterpret_cast<const IMAGE_NT_HEADERS *>(base + reinterpret_cast<const IMAGE_DOS_HEADER *>(base)->e_lfanew);
    const IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++s)
    {
        if (!(s->Characteristics & IMAGE_SCN_MEM_EXECUTE) || s->VirtualAddress != 0x1000)
            continue;
        const DWORD size = s->SizeOfRawData < s->Misc.VirtualSize ? s->SizeOfRawData : s->Misc.VirtualSize;
        unsigned char *disk = static_cast<unsigned char *>(VirtualAlloc(nullptr, size, MEM_COMMIT, PAGE_READWRITE));
        DWORD read = 0;
        SetFilePointer(file, s->PointerToRawData, nullptr, FILE_BEGIN);
        ReadFile(file, disk, size, &read, nullptr);
        const unsigned char *mem = base + s->VirtualAddress;
        int ranges = 0;
        for (DWORD o = 0; o < read && ranges < 400;)
        {
            if (mem[o] == disk[o])
            {
                ++o;
                continue;
            }
            DWORD end = o;
            while (end < read && (mem[end] != disk[end] || (end + 4 < read && mem[end + 4] != disk[end + 4])))
                ++end;
            char hex[3 * 16 + 1];
            int n = 0;
            for (DWORD k = o; k < end && k < o + 16; ++k)
                n += wsprintfA(hex + n, "%02x ", mem[k]);
            hex[n] = 0;
            Log("patch %08x +%u: %s", static_cast<DWORD>(reinterpret_cast<DWORD>(mem) + o), end - o, hex);
            ++ranges;
            o = end;
        }
        VirtualFree(disk, 0, MEM_RELEASE);
    }
    CloseHandle(file);
}
} // namespace nfs
