/** A world point's place on screen (original: zPhysicsBehaviors eViewPlatInterface::GetScreenPosition). */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x6be940, ViewGetScreenPosition)

extern "C" void *__stdcall D3DXVec4Transform(void *out, const void *v, const void *m);

namespace
{
const int kWorldScreen = 0x80;
const int *const kScreenWidth = reinterpret_cast<const int *>(0x982be4);
const int *const kScreenHeight = reinterpret_cast<const int *>(0x982be8);
} // namespace

void __fastcall ViewGetScreenPosition(void *view, void *, bVector3 *out, const bVector3 *in)
{
    const char *info = *static_cast<char *const *>(view);
    const float point[4] = {in->x, in->y, in->z, 1.0f};
    float r[4];
    D3DXVec4Transform(r, point, info + kWorldScreen);
    const float inverse = 1.0f / r[3];
    const float x = r[0] * inverse;
    const float y = r[1] * inverse;
    out->x = (x + 1.0f) * static_cast<float>(*kScreenWidth) * 0.5f;
    out->y = (y - 1.0f) * static_cast<float>(*kScreenHeight) * -0.5f;
    out->z = inverse * r[2];
}
