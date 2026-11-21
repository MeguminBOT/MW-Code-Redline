/** The pursuit panel and the heat keys (dev/pursuitpanel.h). */
#include <math.h>
#include <windows.h>

#include "dev/dev.h"
#include "dev/pursuitpanel.h"
#include "engine/rendering/display/display.h"
#include "core/config.h"
#include "core/log.h"
#include "legacy/link.h"

namespace nfs::pursuitpanel
{
namespace
{
template <typename T> T &At(void *base, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}

template <typename R, typename... A> R Virtual(void *object, int offset, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*reinterpret_cast<void ***>(object))[offset / 4])(object, args...);
}

void *Find(void *object, unsigned int handle)
{
    return NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(At<void *>(object, 4),
                                                                       reinterpret_cast<void *>(handle));
}

const unsigned int kPerpHandle = 0x4037e0;
/** ICopMgr: its list of pursuits (IPursuit). */
void **const kCopManager = reinterpret_cast<void **>(0x92c67c);
const int kPursuitList = 0xd4;
/** AIPursuit from its IPursuit: the cops (a UTL::Vector) and the pursuit's lowest and highest heat. */
const int kPursuitFromInterface = 0x48;
const int kCops = 0x60;
const int kCopCount = 0x68;
const int kLeastHeat = 0xe4;
const int kMostHeat = 0xe8;
const int kLowestLevel = 1;
const int kHighestLevel = 5;
const float kKilometresPerHour = 3.6f;

struct State
{
    bool shown = true;
    bool panelKey = false;
    bool heatKey = false;
    DWORD pursuitSince = 0;
    bool stepped = false;
};

State g_state;

void *PlayerSimable()
{
    if (!*reinterpret_cast<int *>(0x92cd84))
        return nullptr;
    void *player = **reinterpret_cast<void ***>(0x92cd7c);
    return player ? Virtual<void *>(player, 4) : nullptr;
}

void *PlayerPursuit(void *simable)
{
    char *manager = static_cast<char *>(*kCopManager);
    if (!manager || !simable)
        return nullptr;
    char *head = At<char *>(manager, kPursuitList);
    for (char *node = At<char *>(head, 0); node != head; node = At<char *>(node, 0))
    {
        void *pursuit = At<void *>(node, 8);
        char *target = Virtual<char *>(pursuit, 8);
        if (target && At<void *>(target, 0x1c) == simable)
            return pursuit;
    }
    return nullptr;
}

float Speed(void *simable)
{
    const float *v = Virtual<const float *>(Virtual<void *>(simable, 0x54), 0x24);
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) * kKilometresPerHour;
}

const float *Position(void *simable)
{
    return Virtual<const float *>(Virtual<void *>(simable, 0x54), 0x20);
}

void StepHeat(int step)
{
    void *simable = PlayerSimable();
    if (!simable)
        return;
    void *perp = Find(simable, kPerpHandle);
    if (!perp)
        return;
    int level = static_cast<int>(Virtual<float>(perp, 4)) + step;
    level = level < kLowestLevel ? kLowestLevel : level > kHighestLevel ? kHighestLevel : level;
    const float heat = static_cast<float>(level);
    if (void *pursuit = PlayerPursuit(simable))
    {
        char *base = static_cast<char *>(pursuit) - kPursuitFromInterface;
        if (At<float>(base, kLeastHeat) > heat)
            At<float>(base, kLeastHeat) = heat;
        if (At<float>(base, kMostHeat) < heat)
            At<float>(base, kMostHeat) = heat;
        Virtual<void, float>(perp, 8, heat);
        Virtual<void>(pursuit, 0x138);
    }
    else
        Virtual<void, float>(perp, 8, heat);
    nfs::Log("pursuit panel: heat %d", level);
}

bool Edge(int key, bool &held)
{
    const bool down = nfs::display::DebugKeyDown(key);
    const bool pressed = down && !held;
    held = down;
    return pressed;
}
} // namespace

void Update()
{
    if (!nfs::dev::Enabled())
        return;
    if (Edge(VK_F2, g_state.panelKey))
        g_state.shown = !g_state.shown;
    if (Edge(VK_F3, g_state.heatKey))
        StepHeat(nfs::display::DebugKeyDown(VK_CONTROL) ? -1 : 1);
    static const int heatUpAfter = nfs::config::Int("Dev", "HeatUpAfter", 0);
    if (heatUpAfter <= 0 || g_state.stepped)
        return;
    if (!PlayerPursuit(PlayerSimable()))
    {
        g_state.pursuitSince = 0;
        return;
    }
    const DWORD now = GetTickCount();
    if (!g_state.pursuitSince)
        g_state.pursuitSince = now;
    else if (now - g_state.pursuitSince > static_cast<DWORD>(heatUpAfter) * 1000u)
    {
        g_state.stepped = true;
        StepHeat(1);
    }
}

int Text(char (*lines)[kLineLength])
{
    if (!nfs::dev::Enabled() || !g_state.shown)
        return 0;
    void *simable = PlayerSimable();
    void *pursuit = PlayerPursuit(simable);
    if (!pursuit)
        return 0;
    int count = 0;
    void *perp = Find(simable, kPerpHandle);
    const int heat100 = perp ? static_cast<int>(Virtual<float>(perp, 4) * 100.0f) : 0;
    wsprintfA(lines[count++], "PURSUIT  HEAT %d.%02d  F3 UP  CTRL+F3 DOWN", heat100 / 100, heat100 % 100);
    wsprintfA(lines[count++], "YOU %24d KM/H", static_cast<int>(Speed(simable) + 0.5f));
    const float *me = Position(simable);
    char *base = static_cast<char *>(pursuit) - kPursuitFromInterface;
    void **cops = At<void **>(base, kCops);
    const int cops_count = At<int>(base, kCopCount);
    for (int i = 0; i < cops_count && count < kMaxLines; ++i)
    {
        void *cop = cops[i];
        void *copSimable = Virtual<void *>(cop, 4);
        if (!copSimable)
            continue;
        const float *at = Position(copSimable);
        const float dx = at[0] - me[0];
        const float dy = at[1] - me[1];
        const float dz = at[2] - me[2];
        const int distance = static_cast<int>(sqrtf(dx * dx + dy * dy + dz * dz) + 0.5f);
        char name[16];
        lstrcpynA(name, Virtual<const char *>(cop, 0x4c), sizeof(name));
        CharUpperA(name);
        wsprintfA(lines[count++], "%-14s %5d KM/H %5d M", name, static_cast<int>(Speed(copSimable) + 0.5f), distance);
    }
    return count;
}
} // namespace nfs::pursuitpanel
