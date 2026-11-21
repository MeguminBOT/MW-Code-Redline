/**
 * The window message pump at the end of each main loop pass (original zPlatform 0x6c21a0, called from MainLoop through
 * 0x6df8e0). The original handled one message a pass, sent a zero mouse movement every pass while the game was active
 * to keep the screen saver away, and otherwise waited in GetMessage for the next message, which held the whole loop to
 * the rate messages arrived at. Here every waiting message is handled each pass, the screen stays on through
 * SetThreadExecutionState, and the game waits only while it cannot be seen: minimised, or in the background in
 * exclusive fullscreen. SDL pumps the messages (core/sdl.h).
 */
#include <windows.h>

#include "game/gen/api.h"
#include "game/platform/Platform.h"
#include "engine/rendering/display/display.h"
#include "core/input.h"
#include "legacy/link.h"
#include "core/sdl.h"

NFS_REPLACE(0x6c21a0, sub_6C21A0)

/** Nonzero while the game is the active application and not minimised (WM_ACTIVATEAPP and WM_SIZE in 0x6db6c0). */
extern "C" int dword_982BEC;

namespace
{
/** cFrontendDatabase::FEGameMode bits of the online modes, which keep running in the background. */
const unsigned int kOnlineGameModes = 0x48;

int s_keptAwake = -1;

void KeepAwake(bool active)
{
    if (s_keptAwake == static_cast<int>(active))
        return;
    s_keptAwake = active;
    SetThreadExecutionState(active ? ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED : ES_CONTINUOUS);
}

bool s_quit;
WPARAM s_quitCode;

/** WM_QUIT, which no window procedure sees, as SDL takes it from the queue. */
bool SDLCALL WatchForQuit(void *, MSG *message)
{
    if (message->message == WM_QUIT)
    {
        s_quit = true;
        s_quitCode = message->wParam;
    }
    return true;
}
} // namespace

extern "C" WPARAM sub_6C21A0()
{
    const bool active = dword_982BEC != 0;
    KeepAwake(active);
    if (!active && !(FEDatabase->FEGameMode & kOnlineGameModes) &&
        (!nfs::display::IsWindowed() || IsIconic(g_hWnd)))
        WaitMessage();
    // SDL dispatches the messages (to the game's window procedure, after its own) and keeps the keyboard and mouse
    // state; of its events only the wheel is read (source/core/input/Input.cpp).
    static bool s_watching;
    if (!s_watching)
        s_watching = true, SDL_SetWindowsMessageHook(WatchForQuit, nullptr);

    SDL_PumpEvents();
    SDL_Event events[32];
    int count;
    while ((count = SDL_PeepEvents(events, 32, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST)) > 0)
        for (int i = 0; i < count; ++i)
            if (events[i].type == SDL_EVENT_MOUSE_WHEEL)
                nfs::input::OnMouseWheel(events[i].wheel.y);

    if (s_quit)
    {
        s_quit = false;
        if (!ExitTheGameFlag)
            ExitTheGameFlag = 1;
        ShowCursor(TRUE);
        return s_quitCode;
    }
    return 0;
}
