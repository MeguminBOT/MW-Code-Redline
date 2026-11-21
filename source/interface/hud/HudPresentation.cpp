/**
 * HUD readings between simulation steps (original: zFe GetVehicleVectors 0x579840, Tachometer::SetRpm 0x59eb30,
 * DragTachometer::SetRpm 0x5a1230).
 *
 * The tachometer needle and the minimap's player arrow and scrolling follow values that change once per simulation
 * step. Above 60 frames per second the world in between is interpolated (core/presentation.h); these readings are too,
 * so the needle and the map turn as smoothly as the car.
 */
#include "game/gen/api.h"
#include "legacy/link.h"
#include "core/presentation.h"

NFS_REPLACE(0x59eb30, TachometerSetRpm)
NFS_REPLACE(0x5a1230, DragTachometerSetRpm)

void GetVehicleVectors(bVector2 *position, bVector2 *direction, ISimable *simable)
{
    NFS_ORIGINAL(0x579840, void(__cdecl *)(bVector2 *, bVector2 *, ISimable *))(position, direction, simable);
    float values[4] = {position->x, position->y, direction->x, direction->y};
    nfs::present::Interpolate(simable, values, 4);
    position->x = values[0];
    position->y = values[1];
    direction->x = values[2];
    direction->y = values[3];
}

/** Tachometer::SetRpm, an ITachometer override: `gauge` is that interface's part of the object (the originals are
 * __thiscall; the second register argument is unused). */
void __fastcall TachometerSetRpm(void *gauge, void *, float rpm)
{
    nfs::present::Interpolate(gauge, &rpm, 1);
    NFS_ORIGINAL(0x59eb30, void(__thiscall *)(void *, float))(gauge, rpm);
}

/** DragTachometer::SetRpm, the same for the drag race tachometer. */
void __fastcall DragTachometerSetRpm(void *gauge, void *, float rpm)
{
    nfs::present::Interpolate(gauge, &rpm, 1);
    NFS_ORIGINAL(0x5a1230, void(__thiscall *)(void *, float))(gauge, rpm);
}
