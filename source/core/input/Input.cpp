/**
 * DirectInput 8's entry point, which the original exe imports (build.py points the import slot here). The game's input
 * is read from SDL (source/core/input/GameInput.cpp). Only the wheel manager still asks for a DirectInput object (zPlatform
 * 0x6d80c0, 0x6e2640, 0x6e2690), to look for force feedback devices; it takes the refusal as no such device, and its
 * device search (0x6dc590) is replaced. The DirectInput stand-in this replaced is in removed/runtime.
 */
#include <windows.h>
#include <unknwn.h>

#include "core/log.h"

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE, DWORD, REFIID, LPVOID *out, IUnknown *)
{
    static bool s_logged;
    if (!s_logged)
    {
        s_logged = true;
        nfs::Log("input: no DirectInput (force feedback is not available)");
    }
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}
