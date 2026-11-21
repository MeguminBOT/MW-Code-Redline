/**
 * eSetTransform (original: zPlatform 0x6c8000): the object matrices of the current effect for a world matrix and a view,
 * before each draw. Skips everything when the matrix, effect and view are those of the last call. Effects without
 * shaders get the fixed-function view, projection and world transforms instead.
 *
 * Sets, where the effect has the parameter: world x view, world, world x view x projection (biased for textures that
 * ask for it, decals), the camera position and the light direction in object space, and world x the shadow view's
 * projection.
 *
 * Change from the original, X360 Stuff's (its NOP at 0x6c82ea): every effect gets the shadow map transform, not only
 * the world, reflective world, normal map, car, glossy window and particle ones; the Xbox 360 effects of the other
 * slots (trees, grass, the sea, skinned world, prelit, normal-mapped cars) receive the shadow too. While the sun
 * shadow cascades are on, no effect samples that shadow map (engine/rendering/shadows/shadow.h), so the transform is left out: a matrix
 * product and its upload for every draw.
 */
#include <emmintrin.h>

#include "game/gen/api.h"
#include "game/platform/Effect.h"
#include "game/platform/Platform.h"
#include "engine/rendering/culling/culling.h"
#include "engine/rendering/shadows/shadow.h"
#include "utils/simd.h"
#include "engine/rendering/vulkan/renderer.h"

extern "C" char *TimeOfDayPointer;
/** The diffuse texture bound last (its +0x50 asks for the biased projection). */
extern "C" TextureInfo *dword_982CB4;
/** Statistics: calls, identity matrix calls, view changes and uploads. */
extern "C" int dword_987B14;
extern "C" int dword_987B20;
extern "C" int dword_987B1C;
extern "C" int dword_987B18;
/** The effect and world matrix of the last upload, and its view. */
extern "C" eEffect *dword_988550;
extern "C" bMatrix4 *dword_988554;
extern "C" int dword_8FAE9C;

namespace
{
const int kParamWorldView = 0xfc4;
const int kParamWorld = 0xf4c;
const int kParamWorldViewProjection = 0xfec;
const int kParamLocalEye = 0x8e4;
const int kParamLocalLight = 0x90c;
const int kParamShadowMap = 0xc54;
/** The view whose projection the shadow map uses. */
const int kShadowView = 15;

void SetFixedFunctionTransform(D3DTRANSFORMSTATETYPE state, const bMatrix4 *m)
{
}

void Multiply(bMatrix4 &out, const bMatrix4 *a, const bMatrix4 *b)
{
    nfs::MultiplyMatrix(reinterpret_cast<float *>(&out), reinterpret_cast<const float *>(a),
                        reinterpret_cast<const float *>(b));
}

/** eInvertTransformationMatrix (0x6bde60) without its D3DXMatrixMultiply call: the inverse of a rigid transform, the
 * 3x3 part transposed and the translation moved back through it (rows 0 to 2 the transposed axes, row 3 minus the
 * translation times them, summed in D3DX's order; the last column 0, 0, 0, 1). As rows for Transform. */
void InvertWorld(__m128 (&out)[4], const bMatrix4 *world)
{
    const float *m = reinterpret_cast<const float *>(world);
    __m128 r0 = _mm_loadu_ps(m), r1 = _mm_loadu_ps(m + 4), r2 = _mm_loadu_ps(m + 8), r3 = _mm_setzero_ps();
    _MM_TRANSPOSE4_PS(r0, r1, r2, r3);
    // The transposed rows' last lane is the translation: rows 0 to 2 end in 0.
    const __m128 keep = _mm_castsi128_ps(_mm_set_epi32(0, -1, -1, -1));
    out[0] = _mm_and_ps(r0, keep);
    out[1] = _mm_and_ps(r1, keep);
    out[2] = _mm_and_ps(r2, keep);
    const __m128 t = _mm_add_ps(_mm_add_ps(_mm_mul_ps(_mm_set1_ps(-m[12]), out[0]), _mm_mul_ps(_mm_set1_ps(-m[13]), out[1])),
                                _mm_mul_ps(_mm_set1_ps(-m[14]), out[2]));
    out[3] = _mm_or_ps(_mm_and_ps(t, keep), _mm_set_ps(1.0f, 0.0f, 0.0f, 0.0f));
}

/** v (a point, w given) times the rows, summed in the original's order. */
void Transform(float out[4], const float v[4], const __m128 (&rows)[4])
{
    __m128 r = _mm_mul_ps(_mm_set1_ps(v[0]), rows[0]);
    r = _mm_add_ps(r, _mm_mul_ps(_mm_set1_ps(v[1]), rows[1]));
    r = _mm_add_ps(r, _mm_mul_ps(_mm_set1_ps(v[2]), rows[2]));
    r = _mm_add_ps(r, _mm_mul_ps(_mm_set1_ps(v[3]), rows[3]));
    _mm_storeu_ps(out, r);
}
} // namespace

void eSetTransform(bMatrix4 *world, int viewId)
{
    TextureInfo *texture = dword_982CB4;
    const bool biased = texture && reinterpret_cast<const char *>(texture)[0x50];
    ++dword_987B14;
    if (world == &eMathIdentityMatrix)
        ++dword_987B20;
    eEffect *effect = g_CurrentEffect;
    if (nfs::vk::Renderer *r = nfs::vk::g_renderer)
    {
        r->SetWorldMatrix(reinterpret_cast<const float *>(world));
        r->SetDrawFade(nfs::culling::DrawFade(world));
    }
    if (world == dword_988554 && effect == dword_988550 && viewId == dword_8FAE9C)
        return;
    eView &view = eViews[viewId];
    eViewPlatInfo *plat = view.PlatInfo;
    const bMatrix4 *viewScreen = biased ? &plat->ViewScreenBiasedMatrix : &plat->ViewScreenMatrix;
    const bMatrix4 *worldScreen = biased ? &plat->WorldScreenBiasedMatrix : &plat->WorldScreenMatrix;
    const bool shaders = effect->HasShaders;
    if (viewId != dword_8FAE9C)
    {
        if (!shaders)
        {
            SetFixedFunctionTransform(D3DTS_VIEW, &plat->WorldViewMatrix);
            SetFixedFunctionTransform(D3DTS_PROJECTION, viewScreen);
        }
        ++dword_987B1C;
        dword_8FAE9C = viewId;
    }
    else if (effect != dword_988550 && !shaders)
    {
        SetFixedFunctionTransform(D3DTS_VIEW, &plat->WorldViewMatrix);
        SetFixedFunctionTransform(D3DTS_PROJECTION, viewScreen);
    }
    ++dword_987B18;
    dword_988550 = effect;
    dword_988554 = world;
    ID3DXEffect *e = effect->Effect;
    bMatrix4 m;
    if (D3DXHANDLE h = effect->Parameter(kParamWorldView))
    {
        Multiply(m, world, &plat->WorldViewMatrix);
        e->SetMatrix(h, &m);
    }
    if (D3DXHANDLE h = effect->Parameter(kParamWorld))
        e->SetMatrix(h, world);
    if (!shaders)
    {
        SetFixedFunctionTransform(D3DTS_WORLD, world);
    }
    else
    {
        if (D3DXHANDLE h = effect->Parameter(kParamWorldViewProjection))
        {
            Multiply(m, world, worldScreen);
            e->SetMatrix(h, &m);
        }
    }
    const D3DXHANDLE eye = effect->Parameter(kParamLocalEye), light = effect->Parameter(kParamLocalLight);
    if (eye || light)
    {
        __m128 inverse[4];
        InvertWorld(inverse, world);
        if (eye)
        {
            const bVector3 &position = view.pCamera->CurrentKey.Position;
            const float camera[4] = {position.x, position.y, position.z, 1.0f};
            float local[4];
            Transform(local, camera, inverse);
            e->SetVector(eye, local);
        }
        if (light)
        {
            float local[4];
            Transform(local, reinterpret_cast<const float *>(TimeOfDayPointer + 0x180), inverse);
            bNormalize(reinterpret_cast<bVector3 *>(local), reinterpret_cast<const bVector3 *>(local));
            e->SetVector(light, local);
        }
    }
    if (nfs::shadow::Active())
        return;
    if (D3DXHANDLE h = effect->Parameter(kParamShadowMap))
    {
        Multiply(m, world, &eViews[kShadowView].PlatInfo->ViewScreenMatrix);
        e->SetMatrix(h, &m);
    }
}
