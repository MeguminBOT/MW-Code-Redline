/**
 * Lookup for Win32 APIs that the original exe never imported (see dynamic_imports() in build.py). New code calls them
 * like any other API; the first call lands here, later calls go straight to the export.
 */
#include <windows.h>

namespace
{
const char *const kModules[] = {"kernel32.dll", "user32.dll", "gdi32.dll", "advapi32.dll", "winmm.dll",
                                "shell32.dll",  "ole32.dll",  "dwmapi.dll", "shcore.dll",  "version.dll",
                                "SDL3.dll"};
}

extern "C" void *__stdcall nfs_resolve_import(const char *name, void **slot)
{
    for (const char *module : kModules)
    {
        HMODULE handle = LoadLibraryA(module);
        if (!handle)
            continue;
        if (FARPROC proc = GetProcAddress(handle, name))
        {
            *slot = reinterpret_cast<void *>(proc);
            return *slot;
        }
    }
    char message[256];
    wsprintfA(message, "Windows API %s is not available on this system.", name);
    MessageBoxA(nullptr, message, "Need for Speed Most Wanted", MB_ICONERROR | MB_OK);
    ExitProcess(1);
}
