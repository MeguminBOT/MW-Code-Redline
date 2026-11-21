/**
 * The front end's cars (original: zWorld RenderFrontEndCars 0x750980): each car of FrontEndRenderingCarList with its
 * render info drawn at its place; for the reflection pass (the second argument set) mirrored and moved by the garage's
 * floor offset (feposoff and the two floats after it) and by the car's ride height along its up axis, except in the
 * garage of type 5, which draws no reflection.
 *
 * A list node: +0x318 its CarRenderInfo, +0x320 position, +0x330 matrix, +0x370 and +0x470 the wheel matrix sets,
 * +0x574 shown. CarRenderInfo: +0x1628 the LOD in use, +0x17b4 its ride (+0xf4 the height the mirror moves it by).
 */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
template <typename T> T &At(void *base, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}

char *const kCarList = reinterpret_cast<char *>(0x9b27ec);
const float *const kMirrorOffset = reinterpret_cast<const float *>(0x9053c0);
const int kNodeInfo = 0x318;
const int kNodePosition = 0x320;
const int kNodeMatrix = 0x330;
const int kNodeWheels = 0x370;
const int kNodeWheelsAlso = 0x470;
const int kNodeShown = 0x574;
const int kInfoLod = 0x1628;
const int kInfoRide = 0x17b4;
const int kRideMirrorHeight = 0xf4;
const int kGarageWithoutReflection = 5;

struct alignas(16) Matrix
{
    float m[16];
};
} // namespace

void __cdecl RenderFrontEndCars(eView *view, int reflection)
{
    if (!DrawCars)
        return;
    if (reflection && static_cast<int>(FEManager::Get()->GetGarageType()) == kGarageWithoutReflection)
        return;
    eGetCurrentViewMode();
    for (char *node = *reinterpret_cast<char **>(kCarList); node != kCarList; node = *reinterpret_cast<char **>(node))
    {
        char *info = At<char *>(node, kNodeInfo);
        if (!info || !At<int>(node, kNodeShown))
            continue;
        const int lod = At<int>(info, kInfoLod);
        Matrix matrix;
        NFS_ORIGINAL(0x444fe0, void(__cdecl *)(void *, const void *))(&matrix, node + kNodeMatrix);
        float position[3] = {At<float>(node, kNodePosition), At<float>(node, kNodePosition + 4),
                             At<float>(node, kNodePosition + 8)};
        if (reflection)
        {
            float *up = &matrix.m[8];
            up[0] = -up[0];
            up[1] = -up[1];
            up[2] = -up[2];
            const float height = At<float>(At<char *>(info, kInfoRide), kRideMirrorHeight);
            position[0] = position[0] + kMirrorOffset[0] + up[0] * height;
            position[1] = position[1] + kMirrorOffset[1] + height * up[1];
            position[2] = kMirrorOffset[2] + position[2] + height * up[2];
        }
        NFS_ORIGINAL(0x74e160, void(__thiscall *)(void *, eView *, const float *, const void *, const void *, const void *,
                                                  const void *, int, int, int, float, int, int))(
            info, view, position, &matrix, node + kNodeWheels, node + kNodeWheelsAlso, node + kNodeWheels,
            reflection != 0, 0, reflection, 1.0f, lod, lod);
    }
}
