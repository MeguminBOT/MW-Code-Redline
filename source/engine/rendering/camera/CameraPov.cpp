/** Whether a camera mover draws the player's car (original: zCamera CameraMover::Render, RenderCarPOV,
 * CubicCameraMover::RenderCarPOV, TrackCopCameraMover::RenderCarPOV). */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x4687f0, CameraMoverRender)
NFS_REPLACE(0x4709d0, CameraMoverRenderCarPov)
NFS_REPLACE(0x46afd0, CubicCameraMoverRenderCarPov)
NFS_REPLACE(0x475160, TrackCopCameraMoverRenderCarPov)

namespace
{
const int kCubicMode = 0x9c;
const int kCubicInside = 0xa1;
const int kTrackCopShowsCar = 0x285;
} // namespace

void __fastcall CameraMoverRender(void *, void *, eView *)
{
}

bool __fastcall CameraMoverRenderCarPov(void *)
{
    return true;
}

int __fastcall CubicCameraMoverRenderCarPov(void *mover)
{
    const char *self = static_cast<const char *>(mover);
    switch (*reinterpret_cast<const int *>(self + kCubicMode))
    {
    case 1:
        return self[kCubicInside] == 0 ? 1 : 0;
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
        return 1;
    default:
        return 0;
    }
}

bool __fastcall TrackCopCameraMoverRenderCarPov(void *mover)
{
    return static_cast<const char *>(mover)[kTrackCopShowsCar] != 0;
}
