/**
 * Sampling profiler (-sample; dev/sampler.h). A thread stops the game's main thread about 4000 times a second while a
 * race runs (with -samplefe also in the front end and while loading), records where it was (EIP) and the return addresses of its EBP chain (up to kDepth; code built without
 * frame pointers ends the chain early), and appends them to nfs_samples.bin next to the exe: a word with the number of
 * addresses (bit 31 set: the next two words are the sample's QueryPerformanceCounter, low then high), then the
 * addresses. The renderer's spike log (-vkstats) writes the slow frames' counter windows to nfs_spikes.txt, so
 * tools/samples.py --spikes can keep only the samples taken in them. nfs_samples_modules.txt lists the loaded modules (base, size, path) for addresses
 * outside the exe. tools/samples.py turns both into per-function times.
 */
#include <windows.h>

#include "dev/sampler.h"
#include "engine/rendering/vulkan/renderer.h"

extern "C" int TheGameFlowManager_CurrentGameFlowState;

namespace nfs::sampler
{
namespace
{
const int kDepth = 28;
const int kGameFlowRacing = 6;
/** Samples a second, about. */
const LONGLONG kRate = 4000;
const DWORD kFlushInterval = 1000;

HANDLE s_mainThread;
/** The top of the main thread's stack (its TIB's StackBase), which bounds the EBP chain. */
DWORD s_stackBase;
HANDLE s_file;
DWORD s_buffer[65536];
int s_used;

void ExePath(char *out, const char *name)
{
    DWORD n = GetModuleFileNameA(nullptr, out, MAX_PATH);
    while (n > 0 && out[n - 1] != '\\' && out[n - 1] != '/')
        --n;
    lstrcpyA(out + n, name);
}

void Flush()
{
    if (!s_used)
        return;
    DWORD written;
    WriteFile(s_file, s_buffer, static_cast<DWORD>(s_used * sizeof(DWORD)), &written, nullptr);
    s_used = 0;
}

void WriteModules()
{
    typedef BOOL(WINAPI * EnumModules)(HANDLE, HMODULE *, DWORD, LPDWORD);
    typedef BOOL(WINAPI * ModuleInfo)(HANDLE, HMODULE, void *, DWORD);
    HMODULE kernel = GetModuleHandleA("kernel32.dll");
    EnumModules enumModules = reinterpret_cast<EnumModules>(GetProcAddress(kernel, "K32EnumProcessModules"));
    ModuleInfo moduleInfo = reinterpret_cast<ModuleInfo>(GetProcAddress(kernel, "K32GetModuleInformation"));
    if (!enumModules || !moduleInfo)
        return;
    HMODULE modules[512];
    DWORD bytes = 0;
    if (!enumModules(GetCurrentProcess(), modules, sizeof(modules), &bytes))
        return;
    char path[MAX_PATH];
    ExePath(path, "nfs_samples_modules.txt");
    HANDLE file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    for (DWORD i = 0; i < bytes / sizeof(HMODULE); ++i)
    {
        struct
        {
            void *base;
            DWORD size;
            void *entry;
        } info;
        char name[MAX_PATH], line[MAX_PATH + 40];
        if (!moduleInfo(GetCurrentProcess(), modules[i], &info, sizeof(info)) ||
            !GetModuleFileNameA(modules[i], name, MAX_PATH))
            continue;
        const int n = wsprintfA(line, "%08x %08x %s\r\n", reinterpret_cast<DWORD>(info.base), info.size, name);
        DWORD written;
        WriteFile(file, line, static_cast<DWORD>(n), &written, nullptr);
    }
    CloseHandle(file);
}

void Sample()
{
    if (SuspendThread(s_mainThread) == static_cast<DWORD>(-1))
        return;
    CONTEXT context;
    context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (GetThreadContext(s_mainThread, &context))
    {
        DWORD addresses[kDepth + 1];
        int count = 0;
        addresses[count++] = context.Eip;
        const DWORD low = context.Esp, high = s_stackBase;
        DWORD frame = context.Ebp;
        while (count <= kDepth && frame >= low && frame <= high - 8 && !(frame & 3))
        {
            const DWORD *slots = reinterpret_cast<const DWORD *>(frame);
            const DWORD next = slots[0], ret = slots[1];
            if (ret < 0x400000)
                break;
            addresses[count++] = ret;
            if (next <= frame)
                break;
            frame = next;
        }
        if (s_used + count + 3 > static_cast<int>(sizeof(s_buffer) / sizeof(s_buffer[0])))
            Flush();
        LARGE_INTEGER at;
        QueryPerformanceCounter(&at);
        s_buffer[s_used++] = static_cast<DWORD>(count) | 0x80000000u;
        s_buffer[s_used++] = at.LowPart;
        s_buffer[s_used++] = static_cast<DWORD>(at.HighPart);
        for (int i = 0; i < count; ++i)
            s_buffer[s_used++] = addresses[i];
    }
    ResumeThread(s_mainThread);
}

DWORD WINAPI Run(void *)
{
    LARGE_INTEGER frequency, next, now;
    QueryPerformanceFrequency(&frequency);
    // 32-bit division: the counter runs at a few MHz.
    const LONGLONG interval = static_cast<DWORD>(frequency.QuadPart) / static_cast<DWORD>(kRate);
    QueryPerformanceCounter(&next);
    DWORD lastFlush = GetTickCount();
    bool modulesWritten = false;
    for (;;)
    {
        next.QuadPart += interval;
        do
        {
            SwitchToThread();
            QueryPerformanceCounter(&now);
        } while (now.QuadPart < next.QuadPart);
        if (now.QuadPart - next.QuadPart > interval * 16)
            next = now;
        static const bool always = nfs::vk::HasArgument("-samplefe");
        if (always || TheGameFlowManager_CurrentGameFlowState == kGameFlowRacing)
        {
            Sample();
            if (!modulesWritten)
            {
                WriteModules();
                modulesWritten = true;
            }
        }
        if (GetTickCount() - lastFlush >= kFlushInterval)
        {
            Flush();
            lastFlush = GetTickCount();
        }
    }
}
} // namespace

void Start()
{
    if (s_mainThread || !nfs::vk::HasArgument("-sample"))
        return;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &s_mainThread,
                    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
    s_stackBase = reinterpret_cast<DWORD>(reinterpret_cast<NT_TIB *>(NtCurrentTeb())->StackBase);
    char path[MAX_PATH];
    ExePath(path, "nfs_samples.bin");
    s_file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (s_file == INVALID_HANDLE_VALUE)
        return;
    HANDLE thread = CreateThread(nullptr, 0, Run, nullptr, 0, nullptr);
    if (thread)
    {
        SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL);
        CloseHandle(thread);
    }
}
} // namespace nfs::sampler
