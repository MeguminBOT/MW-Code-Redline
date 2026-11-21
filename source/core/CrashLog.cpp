/**
 * Logs access violations and the other faults that end the game (a breakpoint, an illegal or privileged instruction,
 * a division by zero, a stack overflow) to nfs.log: the faulting address, registers and the return addresses
 * found on the stack (tools/symbolize.py turns them into names). The exception then goes on to whatever handles it.
 */
#include <windows.h>

#include "core/log.h"
#include "core/runtime.h"

namespace
{
/** The executable's code sections: the game's .text and the rebuild's own code linked in after it. */
const int kMaxRanges = 4;
DWORD g_codeStart[kMaxRanges], g_codeEnd[kMaxRanges];
int g_codeRanges;

void FindCode()
{
    const BYTE *base = reinterpret_cast<const BYTE *>(GetModuleHandleA(nullptr));
    const IMAGE_NT_HEADERS *nt =
        reinterpret_cast<const IMAGE_NT_HEADERS *>(base + reinterpret_cast<const IMAGE_DOS_HEADER *>(base)->e_lfanew);
    const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections && g_codeRanges < kMaxRanges; ++i, ++section)
        if (section->Characteristics & IMAGE_SCN_MEM_EXECUTE)
        {
            g_codeStart[g_codeRanges] = reinterpret_cast<DWORD>(base) + section->VirtualAddress;
            g_codeEnd[g_codeRanges++] = reinterpret_cast<DWORD>(base) + section->VirtualAddress + section->Misc.VirtualSize;
        }
}

bool InGameCode(DWORD address)
{
    for (int i = 0; i < g_codeRanges; ++i)
        if (address >= g_codeStart[i] && address < g_codeEnd[i])
            return true;
    return false;
}

LONG CALLBACK OnException(EXCEPTION_POINTERS *info)
{
    const EXCEPTION_RECORD *e = info->ExceptionRecord;
    const CONTEXT *c = info->ContextRecord;
    const char *what;
    switch (e->ExceptionCode)
    {
    case EXCEPTION_ACCESS_VIOLATION:
        what = "access violation";
        break;
    case EXCEPTION_BREAKPOINT:
        what = "breakpoint";
        break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        what = "illegal instruction";
        break;
    case EXCEPTION_PRIV_INSTRUCTION:
        what = "privileged instruction";
        break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        what = "division by zero";
        break;
    case EXCEPTION_STACK_OVERFLOW:
        what = "stack overflow";
        break;
    default:
        return EXCEPTION_CONTINUE_SEARCH;
    }
    static int logged;
    if (++logged > 4)
        return EXCEPTION_CONTINUE_SEARCH;
    char module[MAX_PATH] = "speed";
    HMODULE owner = nullptr;
    if (!InGameCode(c->Eip) &&
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(c->Eip), &owner))
        GetModuleFileNameA(owner, module, MAX_PATH);
    if (e->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
        nfs::Log("CRASH access violation at %08x in %s (%s %08x)", c->Eip, module,
                 e->ExceptionInformation[0] ? "writing" : "reading", static_cast<DWORD>(e->ExceptionInformation[1]));
    else
        nfs::Log("CRASH %s at %08x in %s", what, c->Eip, module);
    nfs::Log("  eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x ebp=%08x esp=%08x", c->Eax, c->Ebx, c->Ecx,
             c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    const DWORD *stack = reinterpret_cast<const DWORD *>(c->Esp);
    const DWORD *stackBase = reinterpret_cast<const DWORD *>(__readfsdword(4));
    char line[512];
    int n = wsprintfA(line, "  stack:");
    int found = 0;
    for (int i = 0; i < 512 && found < 24 && stack + i < stackBase; ++i)
    {
        DWORD value = stack[i];
        if (InGameCode(value))
        {
            n += wsprintfA(line + n, " %08x", value);
            if (++found % 8 == 0)
            {
                nfs::Log("%s", line);
                n = wsprintfA(line, "        ");
            }
        }
    }
    if (found % 8)
        nfs::Log("%s", line);
    // The callers by frame pointer, as far as the frames chain up the stack (the rebuild's code keeps them).
    n = wsprintfA(line, "  frames:");
    const DWORD *frame = reinterpret_cast<const DWORD *>(c->Ebp);
    for (int i = 0; i < 16 && frame > stack && frame + 1 < stackBase; ++i)
    {
        if (InGameCode(frame[1]))
            n += wsprintfA(line + n, " %08x", frame[1]);
        const DWORD *next = reinterpret_cast<const DWORD *>(frame[0]);
        if (next <= frame)
            break;
        frame = next;
    }
    nfs::Log("%s", line);
    return EXCEPTION_CONTINUE_SEARCH;
}
} // namespace

void nfs::InstallCrashLog()
{
    FindCode();
    AddVectoredExceptionHandler(1, OnException);
}
