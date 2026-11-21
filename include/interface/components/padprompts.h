#pragma once

/**
 * Controller buttons in the front end's prompts (source/interface/components/PadPrompts.cpp, docs/CONTROLS.md "Button prompts"): while a
 * gamepad is in use, the keyboard key caps the footers and the HUD show are drawn as the pad's buttons.
 */
class TextureInfo;

namespace nfs::prompts
{
extern int g_count;
extern TextureInfo *g_from[];
extern TextureInfo *g_to[];

/** Once a frame before the front end draws: follows the device in use and the bindings. */
void Update();

/** The texture to draw for `texture`: a key cap's button picture while a gamepad is in use, else itself. */
inline TextureInfo *Swap(TextureInfo *texture)
{
    for (int i = 0; i < g_count; ++i)
        if (g_from[i] == texture)
            return g_to[i];
    return texture;
}
} // namespace nfs::prompts
