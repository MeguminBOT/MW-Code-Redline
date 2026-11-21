/**
 * -mute: silences the game for test runs (tools/rungame.py passes it) by muting the process's audio session through
 * Windows Core Audio, so the game's sound settings and the user's profile stay as they are. Windows keeps an
 * application's mute from one run to the next (the volume mixer's, by executable), so without -mute the session is
 * unmuted again: a test run's mute does not carry over to playing. Not in the original game.
 */
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>

#include "core/log.h"
#include "core/runtime.h"
#include "engine/rendering/vulkan/renderer.h"

namespace
{
const CLSID kDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const IID kIDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const IID kISessionManager = {0xBFA971F1, 0x4D5E, 0x40BB, {0x93, 0x5E, 0x96, 0x70, 0x39, 0xBF, 0xBE, 0xE4}};
const DWORD kWaitMs = 2000;

template <typename T> void Release(T *&object)
{
    if (object)
        object->Release();
    object = nullptr;
}

/** On its own thread, so its COM apartment cannot clash with the one the game's main thread sets up later. */
DWORD WINAPI Mute(void *parameter)
{
    const BOOL mute = parameter != nullptr;
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
        return 0;
    IMMDeviceEnumerator *devices = nullptr;
    IMMDevice *device = nullptr;
    IAudioSessionManager *sessions = nullptr;
    ISimpleAudioVolume *volume = nullptr;
    int step = 0;
    HRESULT result = CoCreateInstance(kDeviceEnumerator, nullptr, CLSCTX_ALL, kIDeviceEnumerator,
                                      reinterpret_cast<void **>(&devices));
    if (SUCCEEDED(result) && (++step, SUCCEEDED(result = devices->GetDefaultAudioEndpoint(eRender, eConsole, &device))) &&
        (++step, SUCCEEDED(result = device->Activate(kISessionManager, CLSCTX_ALL, nullptr,
                                                     reinterpret_cast<void **>(&sessions)))) &&
        (++step, SUCCEEDED(result = sessions->GetSimpleAudioVolume(nullptr, FALSE, &volume))))
    {
        BOOL was = FALSE;
        volume->GetMute(&was);
        if (was != mute && SUCCEEDED(result = volume->SetMute(mute, nullptr)))
            nfs::Log(mute ? "Audio muted (-mute)" : "Audio unmuted (a test run had muted it)");
        else if (mute && was)
            nfs::Log("Audio muted (-mute)");
    }
    else
    {
        nfs::Log("Audio: could not reach the audio session (step %d, %08x)", step, static_cast<unsigned>(result));
    }
    Release(volume);
    Release(sessions);
    Release(device);
    Release(devices);
    CoUninitialize();
    return 0;
}
} // namespace

namespace nfs
{
void MuteAudioIfAsked()
{
    void *mute = nfs::vk::HasArgument("-mute") ? reinterpret_cast<void *>(1) : nullptr;
    if (HANDLE thread = CreateThread(nullptr, 0, Mute, mute, 0, nullptr))
    {
        WaitForSingleObject(thread, kWaitMs);
        CloseHandle(thread);
    }
}
} // namespace nfs
