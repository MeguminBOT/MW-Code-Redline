/**
 * Cops in traffic and stuck cops, with [AI] Cops improved (gameplay/ai/aituning.h).
 *
 * - A road nav cuts its path around the cars near it (WRoadNav::UpdateOccludedPosition 0x776600, HolePunchAvoidables) as wide
 *   as its own car's half width (+0x88) needs; a cop's is widened by kTrafficClearance, so it passes traffic with room to spare
 *   or waits behind it.
 * - AIActionGetUnstuck::CanBeAttempted (0x4115c0) reverses a car (2 s) that moved under 3 m in 3 s with the gas on. A cop
 *   checks every kCopStuckWindow, and backs up longer each time it is caught again near where it last backed out.
 */
#include <math.h>

#include "legacy/link.h"
#include "core/log.h"
#include "gameplay/ai/aituning.h"

NFS_REPLACE(0x776600, WRoadNavUpdateOccludedPosition)
NFS_REPLACE(0x4115c0, AIActionGetUnstuckCanBeAttempted)

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

float Constant(unsigned int address)
{
    return *reinterpret_cast<const float *>(address);
}

void *Find(void *simable, unsigned int handle)
{
    return NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(At<void *>(simable, 4),
                                                                       reinterpret_cast<void *>(handle));
}

const unsigned int kPursuitAiHandle = 0x4038e0;
const int kNavVehicle = 0x84;
const int kNavHalfWidth = 0x88;
const int kVehicleSimable = 0x34;
const float kTrafficClearance = 0.5f;

const int kUnstuckObject = 0x30;
const int kUnstuckVehicle = 0x3c;
const int kUnstuckAi = 0x40;
const int kUnstuckFrom = 0x48;
const int kUnstuckTimer = 0x54;
const int kUnstuckInput = 0x58;
const float kCopStuckWindow = 1.5f;
const float kReverseTime = 2.0f;
const float kReverseLonger = 1.5f;
const float kReverseMost = 5.0f;
const float kSamePlace = 6.0f;

bool IsCop(void *simable)
{
    return simable && Find(simable, kPursuitAiHandle);
}

struct Unstick
{
    void *action;
    float at[3];
    float reverse;
};

const int kUnsticks = 32;
Unstick g_unsticks[kUnsticks];
int g_nextUnstick;

Unstick &UnstickOf(void *action)
{
    for (Unstick &u : g_unsticks)
        if (u.action == action)
            return u;
    Unstick &u = g_unsticks[g_nextUnstick];
    g_nextUnstick = (g_nextUnstick + 1) % kUnsticks;
    u.action = action;
    u.at[0] = u.at[1] = u.at[2] = 1e9f;
    u.reverse = kReverseTime;
    return u;
}

float ReverseFor(void *action, const float *at)
{
    Unstick &u = UnstickOf(action);
    const float dx = at[0] - u.at[0], dy = at[1] - u.at[1], dz = at[2] - u.at[2];
    if (dx * dx + dy * dy + dz * dz < kSamePlace * kSamePlace)
        u.reverse = u.reverse + kReverseLonger < kReverseMost ? u.reverse + kReverseLonger : kReverseMost;
    else
        u.reverse = kReverseTime;
    u.at[0] = at[0];
    u.at[1] = at[1];
    u.at[2] = at[2];
    return u.reverse;
}
} // namespace

void __fastcall WRoadNavUpdateOccludedPosition(char *self, void *, bool punch)
{
    char *vehicle = At<char *>(self, kNavVehicle);
    const bool widen = vehicle && nfs::aituning::CopsImproved() && IsCop(At<void *>(vehicle, kVehicleSimable));
    if (widen)
        At<float>(self, kNavHalfWidth) += kTrafficClearance;
    NFS_ORIGINAL(0x776600, void(__thiscall *)(char *, bool))(self, punch);
    if (widen)
        At<float>(self, kNavHalfWidth) -= kTrafficClearance;
}

bool __fastcall AIActionGetUnstuckCanBeAttempted(char *self, void *, float dt)
{
    void *vehicle = At<void *>(self, kUnstuckVehicle);
    void *ai = At<void *>(self, kUnstuckAi);
    void *input = At<void *>(self, kUnstuckInput);
    if (!vehicle || !ai || !input || Virtual<bool>(ai, 0x14))
        return false;
    const float *now = Virtual<const float *>(At<void *>(self, kUnstuckObject), 0x60);
    const float at[3] = {now[0], now[1], now[2]};
    float &timer = At<float>(self, kUnstuckTimer);
    const float idle = Constant(0x890634);
    if (Virtual<const float *>(input, 8)[5] < idle && Virtual<const float *>(input, 8)[2] < idle)
    {
        timer = 0.0f;
        return false;
    }
    float *from = &At<float>(self, kUnstuckFrom);
    if (Virtual<bool>(vehicle, 0x20) || !(timer > Constant(0x890968)))
    {
        from[0] = at[0];
        from[1] = at[1];
        from[2] = at[2];
        timer = dt;
        return false;
    }
    const bool cop = nfs::aituning::CopsImproved() && IsCop(Virtual<void *>(ai, 4));
    const float window = cop ? kCopStuckWindow : Constant(0x890604);
    timer += dt;
    if (timer < window)
        return false;
    const float moved = NFS_ORIGINAL(0x4100a0, float(__cdecl *)(const float *, const float *))(from, at);
    timer = 0.0f;
    if (!(moved < Constant(0x890604)))
        return false;
    const float reverse = cop ? ReverseFor(self, at) : kReverseTime;
    if (cop)
        nfs::Log("cops: a stuck cop backs up %d ms", static_cast<int>(reverse * 1000.0f));
    Virtual<void, float>(ai, 0x10, reverse);
    timer = 0.0f;
    Virtual<void, int>(ai, 0x50, 0);
    return true;
}
