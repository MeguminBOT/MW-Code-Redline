/**
 * The front end's cursor position (original: zPlatform __UpdateCursorPos 0x6c9b60), on the mouse struct at 0x987f50:
 * +4 and +8 the cursor on the front end's 640 by 480 screen, +0xc and +0x10 last frame's, +0x14 and +0x18 the move.
 *
 * The original measured the cursor from the window's outer corner, took 24 pixels off for a title bar when windowed,
 * and spread the 640 across the whole width. The front end is drawn on the client area, narrowed to 4:3 and centred on
 * a wider screen (nfs::display::FrontEndScaleX, interface/engine/eFrontEndPass.cpp), so the cursor is measured from
 * the client area and put through the same narrowing: the game's pointer (drawn with its tip at the position) sits
 * under the system's.
 */
#include <windows.h>
#include <math.h>

#include "game/gen/api.h"
#include "game/platform/Platform.h"
#include "engine/rendering/display/display.h"
#include "legacy/link.h"
#include "core/config.h"
#include "core/log.h"

NFS_REPLACE(0x6c9b60, UpdateCursorPos)

void __fastcall UpdateCursorPos(int *self, void *);

namespace
{
const float kWidth = 640.0f;
const float kHeight = 480.0f;

/** -set Debug.CursorLog=1: the cursor's way from the screen to the front end, logged when it moves (twice a second at
 * most). */
void Log(POINT screen, POINT client, int width, int height, const int *self)
{
    static DWORD last;
    static POINT logged = {-1, -1};
    const DWORD now = GetTickCount();
    if ((screen.x == logged.x && screen.y == logged.y) || now - last < 500)
        return;
    last = now;
    logged = screen;
    RECT window = {};
    GetWindowRect(g_hWnd, &window);
    POINT origin = {};
    ClientToScreen(g_hWnd, &origin);
    nfs::Log("cursor: screen %d %d, window %d %d %d %d, client at %d %d size %dx%d, cursor in client %d %d, front end "
             "scale %d/1000 -> %d %d",
             screen.x, screen.y, window.left, window.top, window.right, window.bottom, origin.x, origin.y, width, height,
             client.x, client.y, static_cast<int>(nfs::display::FrontEndScaleX() * 1000.0f), self[1], self[2]);
}
} // namespace

void __fastcall UpdateCursorPos(int *self, void *)
{
    self[3] = self[1];
    self[4] = self[2];
    POINT cursor = {};
    RECT client = {};
    GetCursorPos(&cursor);
    const POINT screen = cursor;
    ScreenToClient(g_hWnd, &cursor);
    GetClientRect(g_hWnd, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width > 0 && height > 0)
    {
        const float across = (static_cast<float>(cursor.x) + 0.5f) / static_cast<float>(width) * 2.0f - 1.0f;
        const float x = (across / nfs::display::FrontEndScaleX() + 1.0f) * (kWidth * 0.5f);
        const float y = (static_cast<float>(cursor.y) + 0.5f) / static_cast<float>(height) * kHeight;
        self[1] = static_cast<int>(floorf(x));
        self[2] = static_cast<int>(floorf(y));
    }
    else
    {
        self[1] = 0;
        self[2] = 0;
    }
    static const bool log = nfs::config::Int("Debug", "CursorLog", 0) != 0;
    if (log)
        Log(screen, cursor, width, height, self);
    self[5] = self[1] - self[3];
    self[6] = self[2] - self[4];
}
