/**
 * Glue between code from a modern MSVC and the MSVC 7.1 C runtime that ships inside speed.exe.
 * The game's own CRT objects provide malloc, memset, __ftol2 and friends; only symbols the old CRT never had are
 * defined here.
 */
#include "core/runtime.h"

extern "C" int _fltused = 0x9875;

#pragma comment(linker, "/ALTERNATENAME:__ftol2_sse=__ftol2")

/**
 * Plain operator new: the symbol map only names the game's placement-new copies, so new code gets its own, on the
 * game's CRT heap (operator delete binds to the original, which frees from the same heap).
 */
extern "C" void *__cdecl malloc(size_t size);

void *operator new(size_t size)
{
    return malloc(size);
}

void *operator new[](size_t size)
{
    return malloc(size);
}

/** sqrt() under /fp:precise: argument and result in ST(0). The game itself always uses a bare fsqrt. */
extern "C" __declspec(naked) void _CIsqrt()
{
    __asm fsqrt
    __asm ret
}

/**
 * The helpers that /arch:SSE2 code calls (build.py SSE2_SOURCES) and the old CRT never had. 64-bit integer conversions
 * go through the x87 unit (fisttp truncates whatever the control word says; loads and stores are exact).
 */
/** sqrt() of a negative number under /fp:precise (argument and result in XMM0): NaN, without errno. */
extern "C" __declspec(naked) void _libm_sse2_sqrt_precise()
{
    __asm sqrtsd xmm0, xmm0
    __asm ret
}

/** double in XMM0 to a 64-bit integer in EDX:EAX, truncated. */
extern "C" __declspec(naked) void _dtol3()
{
    __asm
    {
        sub esp, 8
        movsd qword ptr [esp], xmm0
        fld qword ptr [esp]
        fisttp qword ptr [esp]
        mov eax, dword ptr [esp]
        mov edx, dword ptr [esp + 4]
        add esp, 8
        ret
    }
}

/** float in XMM0 to a 64-bit integer in EDX:EAX, truncated. */
extern "C" __declspec(naked) void _ftol3()
{
    __asm
    {
        sub esp, 8
        movss dword ptr [esp], xmm0
        fld dword ptr [esp]
        fisttp qword ptr [esp]
        mov eax, dword ptr [esp]
        mov edx, dword ptr [esp + 4]
        add esp, 8
        ret
    }
}

/** 64-bit integer in EDX:ECX to a double in XMM0. */
extern "C" __declspec(naked) void _ltod3()
{
    __asm
    {
        sub esp, 8
        mov dword ptr [esp], ecx
        mov dword ptr [esp + 4], edx
        fild qword ptr [esp]
        fstp qword ptr [esp]
        movsd xmm0, qword ptr [esp]
        add esp, 8
        ret
    }
}

/** 64-bit integer in EDX:ECX to a float in XMM0. */
extern "C" __declspec(naked) void _ltof3()
{
    __asm
    {
        sub esp, 8
        mov dword ptr [esp], ecx
        mov dword ptr [esp + 4], edx
        fild qword ptr [esp]
        fstp dword ptr [esp]
        movss xmm0, dword ptr [esp]
        add esp, 8
        ret
    }
}

/**
 * The original CRT only runs the static initialisers in its own `.CRT$XC*` table. build.py moves our objects'
 * `.CRT$XC*` contributions into `.srcx$XC*`; these two markers bracket them so nfs::RunStaticInitializers can walk them.
 */
typedef void(__cdecl *InitFn)();

#pragma section(".CRT$XCA", read)
#pragma section(".CRT$XCZ", read)
__declspec(allocate(".CRT$XCA")) static InitFn s_init_begin = nullptr;
__declspec(allocate(".CRT$XCZ")) static InitFn s_init_end = nullptr;

void nfs::RunStaticInitializers()
{
    static bool done;
    if (done)
        return;
    done = true;
    for (InitFn *fn = &s_init_begin + 1; fn < &s_init_end; ++fn)
        if (*fn)
            (*fn)();
}
