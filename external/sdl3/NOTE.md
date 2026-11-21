# SDL 3

The game's sound and game controllers go through SDL 3 (`source/core/Sdl.cpp`), loaded at run time from `SDL3.dll`
beside the exe. `python install_deps.py` (in `rebuild/`) downloads SDL 3.4.18's VC development zip and puts its
`include/` and `lib/x86/` here, with its `LICENSE.txt` (zlib). They are not in the repository; the game folder needs
`lib/x86/SDL3.dll` beside `speed.exe`.
