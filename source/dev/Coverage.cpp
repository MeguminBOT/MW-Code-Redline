/**
 * Which original functions run (dev/coverage.h, tools/porting/fe_inventory.py). `-set Debug.Coverage=<list>` names a
 * file of function addresses (hex, one a line). Each listed function's first byte becomes a breakpoint; the first time
 * it runs, the handler puts the byte back, notes the address and lets it go on. A thread writes the addresses hit so
 * far to nfs_coverage.txt next to the exe every two seconds.
 */
#include <windows.h>

#include "core/config.h"
#include "core/log.h"
#include "dev/coverage.h"

namespace nfs::coverage
{
namespace
{
const int kMaxFunctions = 16384;
const DWORD kFlushInterval = 2000;

DWORD s_address[kMaxFunctions];
unsigned char s_original[kMaxFunctions];
volatile LONG s_hit[kMaxFunctions];
int s_count;
volatile LONG s_hits;

int Find(DWORD address)
{
    int lo = 0, hi = s_count - 1;
    while (lo <= hi)
    {
        const int mid = (lo + hi) / 2;
        if (s_address[mid] == address)
            return mid;
        if (s_address[mid] < address)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

LONG CALLBACK OnBreakpoint(EXCEPTION_POINTERS *e)
{
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT)
        return EXCEPTION_CONTINUE_SEARCH;
    const DWORD at = static_cast<DWORD>(reinterpret_cast<UINT_PTR>(e->ExceptionRecord->ExceptionAddress));
    const int i = Find(at);
    if (i < 0)
        return EXCEPTION_CONTINUE_SEARCH;
    unsigned char *code = reinterpret_cast<unsigned char *>(at);
    if (*code == 0xCC)
    {
        DWORD old;
        VirtualProtect(code, 1, PAGE_EXECUTE_READWRITE, &old);
        *code = s_original[i];
        VirtualProtect(code, 1, old, &old);
        FlushInstructionCache(GetCurrentProcess(), code, 1);
    }
    if (!InterlockedExchange(&s_hit[i], 1))
        InterlockedIncrement(&s_hits);
    e->ContextRecord->Eip = at;
    return EXCEPTION_CONTINUE_EXECUTION;
}

void Write()
{
    char path[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    char *slash = path + n;
    while (slash > path && slash[-1] != '\\' && slash[-1] != '/')
        --slash;
    lstrcpyA(slash, "nfs_coverage.txt");
    HANDLE file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    char line[16];
    for (int i = 0; i < s_count; ++i)
        if (s_hit[i])
        {
            const int len = wsprintfA(line, "%08x\r\n", s_address[i]);
            DWORD written;
            WriteFile(file, line, static_cast<DWORD>(len), &written, nullptr);
        }
    CloseHandle(file);
}

DWORD WINAPI Flusher(void *)
{
    LONG written = -1;
    for (;;)
    {
        Sleep(kFlushInterval);
        if (s_hits != written)
        {
            written = s_hits;
            Write();
        }
    }
}
} // namespace

void Start()
{
    char list[MAX_PATH];
    nfs::config::String("Debug", "Coverage", "", list, sizeof(list));
    if (!list[0] || s_count)
        return;
    HANDLE file = CreateFileA(list, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        nfs::Log("coverage: no list %s", list);
        return;
    }
    static char text[1 << 20];
    DWORD size = 0;
    ReadFile(file, text, sizeof(text) - 1, &size, nullptr);
    CloseHandle(file);
    text[size] = 0;
    DWORD value = 0;
    bool digits = false;
    for (DWORD k = 0; k <= size; ++k)
    {
        const char c = text[k];
        int d = -1;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else if ((c == 'x' || c == 'X') && digits && value == 0)
            continue;
        if (d >= 0)
        {
            value = value * 16 + static_cast<DWORD>(d);
            digits = true;
        }
        else if (digits)
        {
            if (s_count < kMaxFunctions && (s_count == 0 || value > s_address[s_count - 1]))
                s_address[s_count++] = value;
            value = 0;
            digits = false;
        }
    }
    AddVectoredExceptionHandler(1, OnBreakpoint);
    for (int i = 0; i < s_count; ++i)
    {
        unsigned char *code = reinterpret_cast<unsigned char *>(s_address[i]);
        DWORD old;
        if (!VirtualProtect(code, 1, PAGE_EXECUTE_READWRITE, &old))
            continue;
        s_original[i] = *code;
        *code = 0xCC;
        VirtualProtect(code, 1, old, &old);
    }
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    CreateThread(nullptr, 0, Flusher, nullptr, 0, nullptr);
    nfs::Log("coverage: %d functions watched (%s)", s_count, list);
}
} // namespace nfs::coverage
