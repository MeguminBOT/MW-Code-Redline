/**
 * Per-view projection setup (original: zPlatform 0x6cf400 and its helper 0x6be9f0), run for every active view each frame.
 *
 * Widescreen: the original takes the vertical field of view from the render target's aspect ratio, so on a screen wider
 * than 4:3 the picture loses height and looks stretched. For render targets shaped like the screen, this keeps the
 * vertical field of view the game was tuned for at 4:3 and widens the horizontal one instead (Hor+). At 4:3 the result
 * is identical to the original.
 */
#include <math.h>
#include <stddef.h>

#include "game/bWare/bMath.h"
#include "game/platform/Platform.h"
#include "engine/rendering/display/display.h"
#include "engine/rendering/view.h"
#include "engine/rendering/x360/render.h"

extern "C" void *__stdcall D3DXMatrixMultiply(void *out, const void *a, const void *b);

static_assert(offsetof(eView, pCamera) == 0x40, "eView layout");
static_assert(offsetof(eView, RenderTarget) == 0x60, "eView layout");
static_assert(offsetof(CameraParams, FieldOfView) == 0xC4, "CameraParams layout");
static_assert(offsetof(eViewPlatInfo, FrustumPlanes) == 0x140, "eViewPlatInfo layout");

namespace
{
/** Pulls the depth of the biased matrices slightly towards the camera, for decals drawn over the world. */
const float kDepthBias = -3.3333334e-05f;
/** Player views get a slightly larger vertical field of view. */
const float kPlayerViewAspectBoost = 1.15f;
/** Fixed aspect for views 8 and 9. */
const float kFixedAspect = 0.6002401f;
/** The environment map faces' views, the rear one among them, and the mirror's. */
const int kFirstEnvFace = 0x12;
const int kLastEnvFace = 0x17;
const int kRearEnvFace = 0x14;
const int kMirrorView = 3;
const float kMirrorNear = 0.75f;

void SetIdentity(bMatrix4 &m)
{
    m.v0 = bVector4(1.0f, 0.0f, 0.0f, 0.0f);
    m.v1 = bVector4(0.0f, 1.0f, 0.0f, 0.0f);
    m.v2 = bVector4(0.0f, 0.0f, 1.0f, 0.0f);
    m.v3 = bVector4(0.0f, 0.0f, 0.0f, 1.0f);
}

bool HasScreenShape(const eRenderTarget *target)
{
    return target->Width * g_WindowHeight == target->Height * g_WindowWidth;
}
} // namespace

/** Horizontal focal length in pixels for a field of view (used for level-of-detail decisions). */
float eGetFocalLength(unsigned short fov)
{
    bAngle half = static_cast<bAngle>(fov >> 1);
    float s = bSin(half);
    double t = s / static_cast<double>(bCos(half));
    if (t == 0.0)
        return 10.0f;
    return static_cast<float>((g_WindowWidth >> 1) / t);
}

/** Clip planes from a combined world-to-clip matrix (Gribb/Hartmann): left, right, bottom, top, near, far. */
void eBuildFrustumPlanes(const bMatrix4 &m, bVector4 *planes)
{
    for (int i = 0; i < 4; ++i)
    {
        const float sign = (i & 1) ? -1.0f : 1.0f;
        for (int r = 0; r < 4; ++r)
        {
            const bVector4 &row = m[r];
            float axis = (i < 2) ? row.x : row.y;
            (&planes[i].x)[r] = sign > 0 ? row.w + axis : row.w - axis;
        }
    }
    for (int r = 0; r < 4; ++r)
    {
        (&planes[4].x)[r] = m[r].z;
        (&planes[5].x)[r] = m[r].w - m[r].z;
    }
    for (int i = 0; i < 6; ++i)
    {
        bVector4 &plane = planes[i];
        double inv = 1.0 / sqrt(static_cast<double>(plane.x) * plane.x + static_cast<double>(plane.y) * plane.y +
                                static_cast<double>(plane.z) * plane.z);
        plane.x = static_cast<float>(inv * plane.x);
        plane.y = static_cast<float>(inv * plane.y);
        plane.z = static_cast<float>(inv * plane.z);
        plane.w = static_cast<float>(inv * plane.w);
    }
}

void eViewPlatInfo::SetupProjection(eView *view, float near_override, float far_override, int unused, bool build_frustum)
{
    Camera *camera = view->pCamera;
    if (!camera)
    {
        SetIdentity(WorldViewMatrix);
        SetIdentity(ViewScreenMatrix);
        return;
    }
    const CameraParams &key = camera->CurrentKey;
    WorldViewMatrix = key.Matrix;
    float near_z = key.NearZ;
    float far_z = 0.0f;
    if (view->ID != 1)
        far_z = key.FarZ;
    if (near_override != 0.0f)
        near_z = near_override;
    if (far_override != 0.0f)
        far_z = far_override;
    // The Xbox 360's reflection depths (X360 Stuff's CalculateViewMatricies): the environment map faces show what is
    // within 100 m, the mirror view and, while the mirror shows it, the rear face within 400 m. Their frustums cull the
    // scenery, so the faces draw a fraction of what the full depth would.
    const int view_id = view->ID;
    if (view_id == kMirrorView || (view_id == kRearEnvFace && eViews[kMirrorView].Active))
    {
        near_z = kMirrorNear;
        far_z = nfs::x360::options::kMirrorFarPlane;
    }
    else if (view_id >= kFirstEnvFace && view_id <= kLastEnvFace)
    {
        far_z = nfs::x360::options::kEnvMapFarPlane;
    }

    const eRenderTarget *target = view->RenderTarget;
    float aspect = static_cast<float>(static_cast<double>(target->Height) / target->Width);
    float horizontal_scale = 1.0f;
    // The road reflection (view 4) is read at each pixel's place on the screen, so it looks through the player's own lens.
    if (nfs::display::IsHorPlus() && (HasScreenShape(target) || view->ID == 4))
    {
        if (view->ID == 4)
            aspect = static_cast<float>(static_cast<double>(g_WindowHeight) / g_WindowWidth);
        horizontal_scale = aspect / 0.75f;
        aspect = 0.75f;
    }
    int id = view->ID;
    if (id == 1 || id == 4 || id == 2 || id == 5)
        aspect = aspect * kPlayerViewAspectBoost;
    if (id == 8 || id == 9)
        aspect = kFixedAspect;

    bAngle fov = key.FieldOfView;
    bAngle half_x = static_cast<bAngle>(static_cast<int>(fov * 0.5f));
    float sin_x = bSin(half_x);
    float x_scale = static_cast<float>(1.0 / (sin_x / static_cast<double>(bCos(half_x))));
    bAngle half_y = static_cast<bAngle>(static_cast<int>(static_cast<double>(fov) * aspect * 0.5f));
    float sin_y = bSin(half_y);
    double y_scale = 1.0 / (sin_y / static_cast<double>(bCos(half_y)));

    bMatrix4 &p = ViewScreenMatrix;
    SetIdentity(p);
    p.v0.x = x_scale * horizontal_scale;
    p.v2.w = 1.0f;
    p.v3.w = 0.0f;
    double q = far_z / (static_cast<double>(far_z) - near_z);
    p.v1.y = static_cast<float>(-y_scale);
    p.v2.z = static_cast<float>(q);
    p.v3.z = static_cast<float>(-(q * near_z));
    view->H = eGetFocalLength(fov);

    WorldViewMatrix.v0.w = 0.0f;
    WorldViewMatrix.v1.w = 0.0f;
    WorldViewMatrix.v2.w = 0.0f;
    D3DXMatrixMultiply(&WorldScreenMatrix, &WorldViewMatrix, &ViewScreenMatrix);
    if (!build_frustum)
        return;

    eBuildFrustumPlanes(WorldScreenMatrix, FrustumPlanes);

    ViewScreenBiasedMatrix = ViewScreenMatrix;
    ViewScreenBiasedMatrix.v2.z = kDepthBias + ViewScreenBiasedMatrix.v2.z;
    D3DXMatrixMultiply(&WorldScreenBiasedMatrix, &WorldViewMatrix, &ViewScreenBiasedMatrix);
}
