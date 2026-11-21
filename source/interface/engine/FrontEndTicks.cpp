/**
 * Front-end time above 60 frames per second (original: zFEng FEngine::Update 0x5c53c0, zFe cFEng::ServiceFengOnly
 * 0x516a20 and MemoryCard::Tick 0x563d10).
 *
 * FEManager::Update hands the frame's real time to the front end in whole units: 1/1200 s ticks for FEngine::Update
 * (menu animations, key repeat) and milliseconds for MemoryCard::Tick (the save system's timeouts), each truncated. At
 * 60 frames per second a frame is exactly 20 ticks and the truncation costs at most a millisecond; at higher rates it
 * drops a growing share of every frame, so menus ran slow, and at several hundred frames per second they stopped. The
 * replacements add back what the truncations dropped.
 */
#include "game/gen/api.h"
#include "legacy/link.h"
#include "core/timing.h"

namespace
{
const float kFrontEndTicksPerSecond = 1200.0f;
const float kMillisecondsPerSecond = 1000.0f;

float g_FrontEndTickCarry;
float g_MemoryCardMsCarry;

void OriginalFEngineUpdate(FEngine *engine, int ticks, unsigned int flags)
{
    NFS_ORIGINAL(0x5c53c0, void(__thiscall *)(FEngine *, int, unsigned int))(engine, ticks, flags);
}
} // namespace

void FEngine::Update(int ticks, unsigned int flags)
{
    ticks = nfs::timing::AddDroppedUnits(ticks, RealTimeElapsed * kFrontEndTicksPerSecond, g_FrontEndTickCarry);
    OriginalFEngineUpdate(this, ticks, flags);
}

/** Runs the front end without advancing its time (while tracks unload), so it bypasses the carry above. */
void cFEng::ServiceFengOnly()
{
    OriginalFEngineUpdate(mFEngine, 0, 0);
}

void MemoryCard::Tick(int ms)
{
    ms = nfs::timing::AddDroppedUnits(ms, RealTimeElapsed * kMillisecondsPerSecond, g_MemoryCardMsCarry);
    NFS_ORIGINAL(0x563d10, void(__thiscall *)(MemoryCard *, int))(this, ms);
}
