#include <windows.h>
#include <stdarg.h>

#include "core/log.h"

namespace nfs
{
void Log(const char *fmt, ...)
{
    static HANDLE file = INVALID_HANDLE_VALUE;
    if (file == INVALID_HANDLE_VALUE)
    {
        char path[MAX_PATH];
        DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
        while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/')
            --n;
        lstrcpyA(path + n, "nfs.log");
        file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    char line[1024];
    va_list args;
    va_start(args, fmt);
    int len = wvsprintfA(line, fmt, args);
    va_end(args);
    line[len++] = '\r';
    line[len++] = '\n';
    line[len] = 0;
    OutputDebugStringA(line);
    if (file != INVALID_HANDLE_VALUE)
    {
        DWORD written;
        WriteFile(file, line, len, &written, nullptr);
    }
}
} // namespace nfs
