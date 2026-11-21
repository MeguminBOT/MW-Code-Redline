/**
 * Controller buttons in the front end's prompts (interface/components/padprompts.h, docs/CONTROLS.md "Button prompts"). Not in the
 * original game, whose PC footers show keyboard key caps whatever is in use.
 *
 * The key caps are GLOBALB textures (PC_KEY_ENTER, PC_KEY_ESC ...). Each stands for the key of a front-end action;
 * while the gamepad is the device last used, FERenderObject::Render draws in their place the picture of the button
 * bound to that action (nfs::input::PromptPicture: Xbox or PlayStation pictures by the pad's type and face labels,
 * tools/padglyphs.py). Packages and their cached render objects are left alone: only the texture handed to the
 * renderer changes, so a switch shows on the next frame everywhere, the HUD's prompts included.
 */
#include <windows.h>

#include "game/gen/api.h"
#include "core/config.h"
#include "core/input.h"
#include "core/log.h"
#include "interface/components/padprompts.h"

namespace
{
struct KeyCap
{
    const char *texture;
    int key;
};

/** The key caps and their keys (DirectInput codes). */
const KeyCap kKeyCaps[] = {
    {"PC_KEY_ENTER", 0x1c}, {"PC_KEY_ESC", 0x01}, {"PC_KEY_ESC_FRENCH", 0x01}, {"PC_KEY_1", 0x02},
    {"PC_KEY_2", 0x03},     {"PC_KEY_3", 0x04},   {"PC_KEY_4", 0x05},          {"PC_KEY_M", 0x32},
    {"PC_KEY_T", 0x14},
};
const int kKeyCapCount = sizeof(kKeyCaps) / sizeof(kKeyCaps[0]);
/** The bindings can change on the controls page: looked at again this often while the pad is in use. */
const DWORD kRefreshMs = 1000;

bool g_padShown;
DWORD g_builtAt;
} // namespace

namespace nfs::prompts
{
int g_count;
TextureInfo *g_from[kKeyCapCount];
TextureInfo *g_to[kKeyCapCount];

void Update()
{
    const bool pad = nfs::input::PadInUse();
    const DWORD now = GetTickCount();
    if (pad == g_padShown && (!pad || now - g_builtAt < kRefreshMs))
        return;
    g_padShown = pad;
    g_builtAt = now;
    static int logged;
    const bool logging = pad && logged++ < 2 && nfs::config::Int("Debug", "PromptLog", 0);
    int count = 0;
    for (int i = 0; pad && i < kKeyCapCount; ++i)
    {
        const char *picture = nfs::input::PromptPicture(kKeyCaps[i].key);
        if (!picture)
            continue;
        TextureInfo *from = GetTextureInfo(bStringHash(kKeyCaps[i].texture), 0, 0);
        TextureInfo *to = GetTextureInfo(bStringHash(picture), 0, 0);
        if (logging)
            nfs::Log("prompts: %s as %s (%p -> %p)", kKeyCaps[i].texture, picture, from, to);
        if (!from || !to)
            continue;
        g_from[count] = from;
        g_to[count] = to;
        ++count;
    }
    g_count = count;
}
} // namespace nfs::prompts
