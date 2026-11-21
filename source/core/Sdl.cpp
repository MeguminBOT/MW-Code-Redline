/**
 * SDL 3's set-up (core/sdl.h). Its backends are chosen so that nothing of DirectX is loaded: sound through WASAPI, game
 * controllers through Raw Input and SDL's own HID drivers (not DirectInput, XInput or GameInput). The game's window is
 * made by the original code and handed to SDL, which then places it (engine/rendering/display/display.h), keeps the keyboard and mouse
 * state, and makes its Vulkan surface. The keyboard comes from Raw Input on SDL's own thread, so a key's state is
 * current whenever the game reads it, not only after the message pump.
 */
#include <windows.h>

#include "core/config.h"
#include "core/log.h"
#include "core/sdl.h"

namespace nfs::sdl
{
namespace
{
SDL_Window *g_window;
} // namespace

bool Start(SDL_InitFlags subsystems)
{
    static bool checked, present;
    if (!checked)
    {
        checked = true;
        // SDL's functions resolve at their first call; without the DLL that call would end the game.
        present = LoadLibraryA("SDL3.dll") != nullptr;
        if (!present)
        {
            nfs::Log("sdl: SDL3.dll is missing next to the game");
            return false;
        }
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "wasapi");
        SDL_SetHint(SDL_HINT_JOYSTICK_DIRECTINPUT, "0");
        SDL_SetHint(SDL_HINT_XINPUT_ENABLED, "0");
        SDL_SetHint(SDL_HINT_JOYSTICK_GAMEINPUT, "0");
        // Windows.Gaming.Input (not DirectX) only when asked for: it is what lets SDL rumble Xbox pads, whose RAWINPUT
        // reports carry no motors ([Controls] XboxRumble, source/core/input/Rumble.cpp).
        SDL_SetHint(SDL_HINT_JOYSTICK_WGI, nfs::config::Int("Controls", "XboxRumble", 0) ? "1" : "0");
        SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "1");
        // Raw Input reports an Xbox pad's triggers as one axis; SDL would ask XInput for them separately. The game reads
        // them as that one axis anyway (DirectInput's Z).
        SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT_CORRELATE_XINPUT, "0");
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        SDL_SetHint(SDL_HINT_WINDOWS_RAW_KEYBOARD, "1");
        // The game's sizes are pixels of the whole desktop (as SetProcessDPIAware made them).
        SDL_SetHint("SDL_WINDOWS_DPI_AWARENESS", "system");
        // The message pump keeps the screen on while the game is in front (source/core/MessagePump.cpp).
        SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
        int version = SDL_GetVersion();
        nfs::Log("sdl: SDL %d.%d.%d", SDL_VERSIONNUM_MAJOR(version), SDL_VERSIONNUM_MINOR(version),
                 SDL_VERSIONNUM_MICRO(version));
    }
    if (!present)
        return false;
    if ((SDL_WasInit(subsystems) & subsystems) == subsystems)
        return true;
    if (!SDL_InitSubSystem(subsystems))
    {
        nfs::Log("sdl: subsystems %x did not start: %s", static_cast<unsigned>(subsystems), SDL_GetError());
        return false;
    }
    if (subsystems & SDL_INIT_JOYSTICK)
    {
        // The game reads states; SDL's event queue would only fill up.
        SDL_SetJoystickEventsEnabled(false);
        SDL_SetGamepadEventsEnabled(false);
    }
    if (subsystems & SDL_INIT_VIDEO)
    {
        // The keyboard and mouse are read as states (source/core/input/Input.cpp); only the wheel comes as events.
        static const Uint32 kStates[] = {SDL_EVENT_KEY_DOWN,          SDL_EVENT_KEY_UP,         SDL_EVENT_TEXT_INPUT,
                                         SDL_EVENT_MOUSE_MOTION,      SDL_EVENT_MOUSE_BUTTON_DOWN,
                                         SDL_EVENT_MOUSE_BUTTON_UP};
        for (Uint32 type : kStates)
            SDL_SetEventEnabled(type, false);
    }
    return true;
}

SDL_Window *AdoptWindow(HWND window)
{
    if (g_window)
        return g_window;
    if (!Start(SDL_INIT_VIDEO))
        return nullptr;
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, window);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, true);
    g_window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!g_window)
        nfs::Log("sdl: the game window could not be handed to SDL: %s", SDL_GetError());
    return g_window;
}

SDL_Window *Window()
{
    return g_window;
}

bool VulkanReady()
{
    static int s_ready = -1;
    if (s_ready < 0)
    {
        s_ready = Start(SDL_INIT_VIDEO) && SDL_Vulkan_LoadLibrary(nullptr);
        if (!s_ready)
            nfs::Log("sdl: no Vulkan through SDL: %s", SDL_GetError());
    }
    return s_ready != 0;
}
} // namespace nfs::sdl
