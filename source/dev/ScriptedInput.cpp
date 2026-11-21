/**
 * Scripted front-end buttons (dev/fescript.h): presses the front end reads as if they came from the player's pad, so
 * menus can be driven in headless runs, where no keyboard reaches the game. Not in the original game.
 *
 * cFEngJoyInput::HandleJoy (0x559400) turns queued input actions into a table of front-end buttons (0x8f3428, one
 * entry of seven words per button: the action, the button's mask bit, its name, then whether it is down for each of
 * four pads), and the front end makes its button mask from that table every frame (0x514b90). A scripted press sets
 * the button down on every pad for kPressSeconds and lets it go.
 *
 * A step can also be one of the career map's shop zones (safehouse, carlot, customshop): it does what driving into
 * the zone does (MenuZoneTrigger::RequestDoAction 0x58c640, which reads the zone's name at +0x14), so the car lot
 * and the customization shop can be reached from free roam without a pad. A "shot" step has the Vulkan renderer
 * save the next frame (feshot_NN.bmp). A "pause" step opens the pause menu in a race, as the pause key does
 * (Game_ShowPauseMenu 0x6050f0: an EPause event).
 *
 * A scripted click (Debug.FEClicks) writes over the front end's mouse (0x987f50: +4 and +8 the cursor, +0x20 the left
 * button down, +0x21 gone down this frame, +0x22 let go this frame) right after the game read it, held down as long
 * as a scripted button press, the cursor kept there one frame more for the release.
 */
#include <windows.h>

#include "game/gen/api.h"
#include "core/config.h"
#include "dev/fescript.h"
#include "core/log.h"
#include "engine/rendering/vulkan/renderer.h"

namespace
{
const unsigned int kButtonTable = 0x8f3428;
const int kButtonWords = 7;
const int kButtonDown = 3;
const int kPads = 4;
const float kPressSeconds = 0.15f;
const int kMaxPresses = 256;

/** The table's buttons in order (FEPad_Up ... FEPad_RightTrigger). */
const char *const kButtons[] = {"up",      "down",    "left",    "right",   "accept",  "back",
                                "back2",   "start",   "button0", "button1", "button2", "button3",
                                "button4", "button5", "ltrigger", "rtrigger"};
const int kButtonCount = sizeof(kButtons) / sizeof(kButtons[0]);
/** Shop zones a step can enter, after the buttons in the step numbering. */
const char *const kZones[] = {"safehouse", "carlot", "customshop"};
const int kZoneCount = sizeof(kZones) / sizeof(kZones[0]);
const int kZoneName = 0x14;
/** The screenshot step, after the zones, and the pause step after it. */
const int kShot = kButtonCount + kZoneCount;
const int kPause = kShot + 1;

const char *StepName(int step)
{
    return step < kButtonCount ? kButtons[step] : step < kShot ? kZones[step - kButtonCount] : step == kShot ? "shot" : "pause";
}

struct Press
{
    float at;
    int button;
};

Press g_presses[kMaxPresses];
int g_count = -1;
int g_next;
int g_held = -1;
float g_release;
DWORD g_start;
int g_shots;

void SetDown(int button, unsigned int down)
{
    unsigned int *entry = reinterpret_cast<unsigned int *>(kButtonTable) + button * kButtonWords;
    for (int pad = 0; pad < kPads; ++pad)
        entry[kButtonDown + pad] = down;
}

void Load()
{
    g_count = 0;
    char text[2048];
    nfs::config::String("Debug", "FEKeys", "", text, sizeof(text));
    for (const char *p = text; *p && g_count < kMaxPresses;)
    {
        char time[16], name[16];
        int n = 0;
        while (*p && *p != ':' && n < 15)
            time[n++] = *p++;
        time[n] = 0;
        if (*p == ':')
            ++p;
        n = 0;
        while (*p && *p != ',' && n < 15)
            name[n++] = *p++;
        name[n] = 0;
        if (*p == ',')
            ++p;
        for (int b = 0; b <= kPause; ++b)
            if (!lstrcmpiA(name, StepName(b)))
            {
                g_presses[g_count].at = bStrToFloat(time);
                g_presses[g_count++].button = b;
            }
    }
    if (g_count)
        nfs::Log("fescript: %d front-end presses", g_count);
    g_start = GetTickCount();
}
struct Click
{
    float at;
    int x, y;
};
const int kMaxClicks = 32;
Click g_clicks[kMaxClicks];
int g_clickCount = -1;
int g_nextClick;
char *const kMouse = reinterpret_cast<char *>(0x987f50);

void LoadClicks()
{
    g_clickCount = 0;
    char text[1024];
    nfs::config::String("Debug", "FEClicks", "", text, sizeof(text));
    for (const char *p = text; *p && g_clickCount < kMaxClicks;)
    {
        char field[3][16] = {};
        for (int f = 0; f < 3; ++f)
        {
            int n = 0;
            while (*p && *p != ':' && *p != ',' && n < 15)
                field[f][n++] = *p++;
            if (*p == ':')
                ++p;
        }
        while (*p && *p != ',')
            ++p;
        if (*p == ',')
            ++p;
        Click &c = g_clicks[g_clickCount++];
        c.at = bStrToFloat(field[0]);
        c.x = static_cast<int>(bStrToFloat(field[1]));
        c.y = static_cast<int>(bStrToFloat(field[2]));
    }
}
} // namespace

namespace nfs::fescript
{
void Mouse()
{
    if (g_clickCount < 0)
        LoadClicks();
    if (g_count < 0)
        return;
    static const Click *held;
    static float release;
    const float now = static_cast<float>(GetTickCount() - g_start) * 0.001f;
    if (held)
    {
        *reinterpret_cast<int *>(kMouse + 4) = held->x;
        *reinterpret_cast<int *>(kMouse + 8) = held->y;
        if (now < release)
        {
            kMouse[0x20] = 1;
            kMouse[0x22] = 0;
            return;
        }
        held = nullptr;
        return;
    }
    if (g_nextClick >= g_clickCount || now < g_clicks[g_nextClick].at)
        return;
    held = &g_clicks[g_nextClick++];
    release = now + kPressSeconds;
    *reinterpret_cast<int *>(kMouse + 4) = held->x;
    *reinterpret_cast<int *>(kMouse + 8) = held->y;
    kMouse[0x20] = 1;
    kMouse[0x21] = 1;
    kMouse[0x22] = 0;
    nfs::Log("fescript: click %d %d", held->x, held->y);
}

bool Press(const char *button)
{
    if (g_count < 0)
        Load();
    for (int b = 0; b < kButtonCount; ++b)
    {
        if (lstrcmpiA(button, kButtons[b]))
            continue;
        if (g_held >= 0)
            SetDown(g_held, 0);
        g_held = b;
        g_release = static_cast<float>(GetTickCount() - g_start) * 0.001f + kPressSeconds;
        SetDown(b, 1);
        nfs::Log("fescript: %s (test channel)", kButtons[b]);
        return true;
    }
    return false;
}

bool Zone(const char *name)
{
    for (const char *zone : kZones)
    {
        if (lstrcmpiA(name, zone))
            continue;
        typedef void(__thiscall * RequestDoAction)(void *);
        unsigned char trigger[kZoneName + sizeof(const char *)] = {};
        *reinterpret_cast<const char **>(trigger + kZoneName) = zone;
        nfs::Log("fescript: zone %s (test channel)", zone);
        reinterpret_cast<RequestDoAction>(0x58c640)(trigger);
        return true;
    }
    return false;
}

bool Shot(const char *name)
{
    if (!nfs::vk::g_renderer || !name || !*name)
        return false;
    char file[64];
    wsprintfA(file, "%.48s.bmp", name);
    nfs::vk::g_renderer->RequestCapture(file);
    return true;
}

void Update()
{
    if (g_count < 0)
        Load();
    const float now = static_cast<float>(GetTickCount() - g_start) * 0.001f;
    if (g_held >= 0 && now >= g_release)
    {
        SetDown(g_held, 0);
        g_held = -1;
    }
    if (!g_count)
        return;
    if (g_held < 0 && g_next < g_count && now >= g_presses[g_next].at && g_presses[g_next].button == kShot)
    {
        char name[32];
        wsprintfA(name, "feshot_%02d.bmp", g_shots++);
        if (nfs::vk::g_renderer)
            nfs::vk::g_renderer->RequestCapture(name);
        nfs::Log("fescript: shot %s", name);
        ++g_next;
        return;
    }
    if (g_held < 0 && g_next < g_count && now >= g_presses[g_next].at && g_presses[g_next].button == kPause)
    {
        nfs::Log("fescript: pause");
        reinterpret_cast<void(__cdecl *)()>(0x6050f0)();
        ++g_next;
        return;
    }
    if (g_held < 0 && g_next < g_count && now >= g_presses[g_next].at && g_presses[g_next].button >= kButtonCount)
    {
        typedef void(__thiscall * RequestDoAction)(void *);
        const char *zone = kZones[g_presses[g_next].button - kButtonCount];
        unsigned char trigger[kZoneName + sizeof(const char *)] = {};
        *reinterpret_cast<const char **>(trigger + kZoneName) = zone;
        nfs::Log("fescript: zone %s", zone);
        reinterpret_cast<RequestDoAction>(0x58c640)(trigger);
        ++g_next;
        return;
    }
    if (g_held < 0 && g_next < g_count && now >= g_presses[g_next].at)
    {
        g_held = g_presses[g_next].button;
        g_release = now + kPressSeconds;
        SetDown(g_held, 1);
        nfs::Log("fescript: %s", kButtons[g_held]);
        ++g_next;
    }
}
} // namespace nfs::fescript
