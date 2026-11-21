#pragma once

/**
 * SDL 3 (external/sdl3, SDL3.dll beside the exe) for the window, keyboard, mouse, game controllers and sound
 * (source/core/Sdl.cpp, Input.cpp, Sound.cpp; source/engine/rendering/display/Display.cpp, MessagePump.cpp; docs/VULKAN.md, "No
 * DirectX"). Include this rather than <SDL3/SDL.h>: SDL's functions are then imports, which build.py resolves from
 * SDL3.dll at the first call like any API the original exe did not import.
 */
#include <windows.h>

#define SDL_DECLSPEC __declspec(dllimport)
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

namespace nfs::sdl
{
/** SDL with these subsystems (SDL_INIT_VIDEO, SDL_INIT_AUDIO, SDL_INIT_GAMEPAD), set up once with the rebuild's hints:
 * no DirectInput, XInput or DirectSound behind it. False when SDL3.dll is missing or the subsystem cannot start
 * (logged). */
bool Start(SDL_InitFlags subsystems);
/** Hands the game's window (made by the original code, with its own window procedure, which SDL passes every message
 * on to) to SDL, able to take a Vulkan surface. Null when SDL cannot take it (logged). */
SDL_Window *AdoptWindow(HWND window);
/** The game's window as SDL's, null before AdoptWindow. */
SDL_Window *Window();
/** SDL's Vulkan side (the instance extensions a surface needs, presentation support), loaded once. */
bool VulkanReady();
} // namespace nfs::sdl
