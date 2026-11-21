/**
 * Light contexts and view helpers (original: zEcstasy eLight*.cpp, eView): a model's light context set up from a light
 * rig or cloned from another one turned into the model's space, the light materials by name hash, the screen size of a
 * sphere or a box for the detail picks, the matrix bias the sorted transparent draws use, and the views' camera movers.
 *
 * eDynamicLightContext +0x50: three light directions stored as the columns of a 3x4 block (x at +0x50, +0x54, +0x58;
 * y at +0x60...; z at +0x70...). eView +0xc: the projection's scale (the screen height in pixels over the view's
 * tangent), +0x40: its camera (position at +0x40, direction at +0x50), +0x44: the camera movers' list.
 */
#include <math.h>
#include <string.h>

#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x5027e0, LightNothing)
NFS_REPLACE(0x5027f0, LightNothing)
NFS_REPLACE(0x4fca80, SpherePixelSize)
NFS_REPLACE(0x4fcb60, BoxPixelSize)
NFS_REPLACE(0x502860, ModelVisibleState)

namespace
{
template <typename T> T &At(void *p, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(p) + offset);
}

const int kContextWords = 0x4c;

eLightMaterial *DefaultMaterial()
{
    return *reinterpret_cast<eLightMaterial **>(0x8f23e8);
}

const char *ViewCamera(const eView *view)
{
    return *reinterpret_cast<const char *const *>(reinterpret_cast<const char *>(view) + 0x40);
}
} // namespace

void __cdecl LightNothing()
{
}

void __cdecl AdjustQuickDynamicLight(eShaperLightRig *, bVector3 *)
{
}

void __cdecl elBeginFrame()
{
}

eLightMaterial *__cdecl elGetDefaultLightMaterial()
{
    return DefaultMaterial();
}

eLightMaterial *__cdecl elGetLightMaterial(unsigned int hash)
{
    // LightMaterialList (0x91c170) links the materials by their node at +4: a material is its node - 4, its name hash at
    // +0xc. The list's own head stands for "not found", which gets the default material.
    char *const head = reinterpret_cast<char *>(0x91c170) - 4;
    char *link = *reinterpret_cast<char **>(0x91c170);
    for (char *m = link ? link - 4 : nullptr; m && m != head;)
    {
        if (At<unsigned int>(m, 0xc) == hash)
            return reinterpret_cast<eLightMaterial *>(m);
        char *next = At<char *>(m, 4);
        m = next ? next - 4 : nullptr;
    }
    return DefaultMaterial();
}

void __cdecl elRotateLightContext(eDynamicLightContext *out, eDynamicLightContext *in, bMatrix4 *world)
{
    *out = *in;
    bMatrix4 inverse;
    eInvertRotationMatrix(&inverse, world);
    char *src = reinterpret_cast<char *>(in), *dst = reinterpret_cast<char *>(out);
    for (int axis = 0; axis < 3; ++axis)
    {
        bVector4 v(At<float>(src, 0x50 + axis * 4), At<float>(src, 0x60 + axis * 4), At<float>(src, 0x70 + axis * 4), 0.0f);
        eMulVector(&v, &inverse, &v);
        At<float>(dst, 0x50 + axis * 4) = v.x;
        At<float>(dst, 0x60 + axis * 4) = v.y;
        At<float>(dst, 0x70 + axis * 4) = v.z;
    }
}

int __cdecl elSetupLightContext(eDynamicLightContext *context, eShaperLightRig *rig, bMatrix4 *world, bMatrix4 *view,
                                bVector4 *position, eView *eview)
{
    if (!context || !world || !rig)
        return 0;
    unsigned int *words = reinterpret_cast<unsigned int *>(context);
    for (int i = 0; i < kContextWords; ++i)
        words[i] = 0;
    elSetupEnvMap(context, world, view, position);
    elSetupLights(context, rig, reinterpret_cast<bVector3 *>(reinterpret_cast<char *>(world) + 0x30), world, view, eview);
    return 1;
}

int __cdecl elCloneLightContext(eDynamicLightContext *context, bMatrix4 *world, bMatrix4 *view, bVector4 *position, eView *,
                                eDynamicLightContext *from)
{
    elRotateLightContext(context, from, world);
    elSetupEnvMap(context, world, view, position);
    return 1;
}

/** eView: a sphere's size on screen in pixels (original sub_4FCA80): from inside it, twice the view's scale. */
int __fastcall SpherePixelSize(const eView *view, void *, float radius, float distance)
{
    const float scale = *reinterpret_cast<const float *>(reinterpret_cast<const char *>(view) + 0xc);
    if (!(radius * 0.5f < distance))
        return static_cast<int>(scale + scale);
    return static_cast<int>(scale / distance * radius);
}

/** eView: a box's size on screen in pixels by its bounding sphere (original sub_4FCB60); 0 behind the camera. */
int __fastcall BoxPixelSize(const eView *view, void *, const float *low, const float *high)
{
    const char *camera = ViewCamera(view);
    const float dx = low[0] - high[0], dy = low[1] - high[1], dz = low[2] - high[2];
    const float radius = static_cast<float>(sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy +
                                                 static_cast<double>(dz) * dz) * 0.5);
    const float cx = (low[0] + high[0]) * 0.5f - At<float>(const_cast<char *>(camera), 0x40);
    const float cy = (low[1] + high[1]) * 0.5f - At<float>(const_cast<char *>(camera), 0x44);
    const float cz = (low[2] + high[2]) * 0.5f - At<float>(const_cast<char *>(camera), 0x48);
    const float ahead = cz * At<float>(const_cast<char *>(camera), 0x58) + cy * At<float>(const_cast<char *>(camera), 0x54) +
                        cx * At<float>(const_cast<char *>(camera), 0x50);
    if (-radius > ahead)
        return 0;
    const float distance = static_cast<float>(sqrt(static_cast<double>(cz) * cz + static_cast<double>(cy) * cy +
                                                   static_cast<double>(cx) * cx)) - radius;
    const float scale = *reinterpret_cast<const float *>(reinterpret_cast<const char *>(view) + 0xc);
    if (!(distance > radius))
        return static_cast<int>(scale);
    return static_cast<int>(scale / distance * radius);
}

/** eView: whether a model's bounding box, placed by a matrix, is in view (original eView::GetVisibleState 0x502860). */
eVisibleState __fastcall ModelVisibleState(eView *view, void *, eModel *model, bMatrix4 *matrix)
{
    bVector3 low, high;
    model->GetBoundingBox(&low, &high);
    return reinterpret_cast<eViewPlatInterface *>(view)->GetVisibleStateSB(&low, &high, matrix);
}

void eView::BiasMatrixForZSorting(bMatrix4 *matrix, float scale)
{
    const char *camera = ViewCamera(this);
    if (!camera)
        return;
    float *m = reinterpret_cast<float *>(matrix);
    const float *eye = reinterpret_cast<const float *>(camera + 0x40);
    // Scaled about the camera: the draw sorts as if it were `scale` times as far, and looks the same.
    bMatrix4 bias;
    float *b = reinterpret_cast<float *>(&bias);
    for (int i = 0; i < 16; ++i)
        b[i] = 0.0f;
    b[0] = b[5] = b[10] = scale;
    for (int i = 0; i < 3; ++i)
        b[12 + i] = (m[12 + i] - eye[i]) * scale + eye[i];
    b[15] = 1.0f;
    m[12] = m[13] = m[14] = 0.0f;
    m[15] = 1.0f;
    eMulMatrix(matrix, matrix, &bias);
}

void eView::AttachCameraMover(CameraMover *mover)
{
    char *self = reinterpret_cast<char *>(this);
    char **node = mover ? reinterpret_cast<char **>(reinterpret_cast<char *>(mover) + 4) : nullptr;
    char *first = At<char *>(self, 0x44);
    At<char **>(self, 0x44) = node;
    At<char **>(first, 4) = node;
    node[1] = self + 0x44;
    node[0] = first;
    eUpdateViewMode();
}

void eView::UnattachCameraMover(CameraMover *mover)
{
    if (mover)
    {
        char *next = At<char *>(mover, 4), *prev = At<char *>(mover, 8);
        At<char *>(prev, 0) = next;
        At<char *>(next, 4) = prev;
    }
    eUpdateViewMode();
}

namespace
{
/** A light of a rig (eShaperLightRig +0x20, 0x1c each): how its direction is given, two angles (degrees), its colour and
 * its intensity. */
struct RigLight
{
    int kind;
    float azimuth;
    float elevation;
    float colour[3];
    float intensity;
};

enum RigLightKind
{
    kWorldAngles = 0,
    kViewAngles = 1,
    kFromSun = 2,
    kAgainstSun = 3,
    kFromPoint = 4,
};

const int kRigLights = 3;
/** CartesianToSpherical's angles are in 0x10000 a turn; the rig's offsets are in degrees. */
const float kDegreesPerAngle = 0.005493164f;

/** An angle (0x10000 a turn, as a float) wrapped to a turn, in degrees. */
float Wrapped(float angle)
{
    return static_cast<float>(static_cast<unsigned short>(static_cast<int>(angle))) * kDegreesPerAngle;
}
} // namespace

int __cdecl elSetupLights(eDynamicLightContext *context, eShaperLightRig *rigObject, bVector3 *position, bMatrix4 *world,
                          bMatrix4 *view, eView *eview)
{
    if (!context)
        return -1;
    if (eview && At<bMatrix4 *>(eview, 0x40))
        view = At<bMatrix4 *>(eview, 0x40);
    bMatrix4 viewInverse;
    eInvertRotationMatrix(&viewInverse, view);
    const RigLight *lights = reinterpret_cast<const RigLight *>(reinterpret_cast<char *>(rigObject) + 0x20);
    bVector3 direction[kRigLights];
    bVector4 colour[kRigLights];
    for (int i = 0; i < kRigLights; ++i)
    {
        const RigLight &l = lights[i];
        bVector3 &d = direction[i];
        switch (l.kind)
        {
        case kWorldAngles:
            SphericalToCartesian(&d, l.azimuth + 180.0f, 90.0f - l.elevation, 1.0f);
            break;
        case kViewAngles:
        {
            // Given in the view's space (its axes x right, y down, z ahead as the angles have them): into the world's.
            SphericalToCartesian(&d, l.azimuth + 180.0f, 90.0f - l.elevation, 1.0f);
            bVector4 v(-d.y, -d.z, d.x, 0.0f);
            eMulVector(&v, &viewInverse, &v);
            d.x = v.x;
            d.y = v.y;
            d.z = v.z;
            break;
        }
        case kFromSun:
        case kAgainstSun:
        {
            const float *sun = reinterpret_cast<const float *>(*reinterpret_cast<const char *const *>(0x9b392c) + 0x180);
            const float sign = l.kind == kFromSun ? 1.0f : -1.0f;
            bVector3 angles;
            CartesianToSpherical(&angles, sun[0] * sign, sun[1] * sign, sun[2] * sign);
            const float azimuth = Wrapped(angles.x) + l.azimuth;
            const float elevation = Wrapped(angles.y) + 90.0f + l.elevation;
            SphericalToCartesian(&d, azimuth + 180.0f, 90.0f - elevation, 1.0f);
            break;
        }
        case kFromPoint:
        {
            const float *at = reinterpret_cast<const float *>(reinterpret_cast<char *>(rigObject) + 0x80);
            d.x = at[0] - position->x;
            d.y = at[1] - position->y;
            d.z = at[2] - position->z;
            break;
        }
        default:
            break;
        }
        colour[i] = bVector4(l.intensity * l.colour[0] * 255.0f, l.intensity * l.colour[1] * 255.0f,
                             l.intensity * l.colour[2] * 255.0f, 0.0f);
    }
    if (world)
    {
        bMatrix4 worldInverse;
        eInvertRotationMatrix(&worldInverse, world);
        for (bVector3 &d : direction)
            eMulVector(&d, &worldInverse, &d);
    }
    char *out = reinterpret_cast<char *>(context);
    for (int i = 0; i < kRigLights; ++i)
    {
        bVector3 n;
        bNormalize(&n, &direction[i]);
        // The colours went through 0..255 and back, as the original does (its rounding included).
        const float k = 0.003921569f;
        At<float>(out, 0x50 + i * 4) = n.x;
        At<float>(out, 0x60 + i * 4) = n.y;
        At<float>(out, 0x70 + i * 4) = n.z;
        At<float>(out, 0x10 + i * 0x10) = colour[i].x * k;
        At<float>(out, 0x14 + i * 0x10) = colour[i].y * k;
        At<float>(out, 0x18 + i * 0x10) = colour[i].z * k;
    }
    return 1;
}

NFS_REPLACE(0x5029f0, SetupWorldLightContext)

/** eView::SetupWorldLightContext (0x5029f0): the view's light context for the world, from frame memory (eView +0x5c):
 * the default light rig at a point 10 m behind the camera and 4 m up, and at +0x90 the sun's view of the world centre. */
void __fastcall SetupWorldLightContext(eView *view, void *)
{
    char *self = reinterpret_cast<char *>(view);
    const char *camera = ViewCamera(view);
    At<void *>(self, 0x5c) = nullptr;
    if (!camera)
        return;
    unsigned int &position = *reinterpret_cast<unsigned int *>(0x915f84);
    const unsigned int end = *reinterpret_cast<const unsigned int *>(0x915f88);
    const unsigned int size = 0x130;
    char *context = nullptr;
    if (position + size < end)
    {
        context = reinterpret_cast<char *>(position);
        position += size;
    }
    else
    {
        *reinterpret_cast<unsigned int *>(0x915f98) += size;
        *reinterpret_cast<int *>(0x915f94) = 1;
    }
    At<char *>(self, 0x5c) = context;
    if (!context)
        return;
    const float *c = reinterpret_cast<const float *>(camera);
    bVector4 at(c[0x40 / 4] - c[0x8 / 4] * 10.0f, c[0x44 / 4] - c[0x18 / 4] * 10.0f, c[0x48 / 4] - c[0x28 / 4] * 10.0f + 4.0f,
                1.0f);
    elSetupLightContext(reinterpret_cast<eDynamicLightContext *>(context), reinterpret_cast<eShaperLightRig *>(0x8f2ab0),
                        &eMathIdentityMatrix, reinterpret_cast<bMatrix4 *>(0x987fc0), &at, view);
    bMatrix4 sunView;
    const bVector3 origin(0.0f, 0.0f, 0.0f), up(0.0f, 0.0f, 1.0f);
    NFS_ORIGINAL(0x6cf0a0, void(__cdecl *)(bMatrix4 *, const bVector3 *, const bVector3 *, const bVector3 *))(
        &sunView, reinterpret_cast<const bVector3 *>(0x9175a0), &origin, &up);
    memcpy(context + 0x90, &sunView, sizeof(sunView));
}
