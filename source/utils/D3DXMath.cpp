/**
 * The functions the game took from Direct3D 9 and D3DX 9 (d3d9.dll, d3dx9_26.dll), as the rebuild's own code
 * (docs/VULKAN.md, "Vulkan only"): the matrix and vector maths the original code calls all over, the two effect
 * loaders of original functions that are no longer called, and Direct3DCreate9. build.py's finalize leaves the two DLLs
 * out of the import table and fills their import slots with these functions, so the original code's calls land here and
 * neither DLL is loaded.
 *
 * The maths gives D3DX's results bit for bit where the D3DXMath self-tests say so (they compare with d3dx9_26.dll when
 * it is installed): the same products, summed in the same order, in single precision with SSE as D3DX's SSE path does.
 */
#include <math.h>
#include <string.h>
#include <xmmintrin.h>

#include "game/platform/D3DX.h"
#include "engine/rendering/display/adapter.h"

namespace
{
inline __m128 Row(const float *m, int r)
{
    return _mm_loadu_ps(m + r * 4);
}

inline __m128 Splat(float v)
{
    return _mm_set1_ps(v);
}

/** (x * r0 + y * r1) + (z * r2 + r3): D3DXVec3Transform's sums, and D3DXVec3TransformCoord's before the divide. */
inline __m128 TransformPoint(const float *p, const float *m)
{
    const __m128 zw = _mm_add_ps(_mm_mul_ps(Row(m, 2), Splat(p[2])), Row(m, 3));
    const __m128 xy = _mm_add_ps(_mm_mul_ps(Row(m, 0), Splat(p[0])), _mm_mul_ps(Row(m, 1), Splat(p[1])));
    return _mm_add_ps(xy, zw);
}

/** 1 / w as D3DX works it out: the hardware's estimate and one Newton step, 2r - r w r. */
inline __m128 Reciprocal(__m128 w)
{
    const __m128 r = _mm_rcp_ps(w);
    return _mm_sub_ps(_mm_add_ps(r, r), _mm_mul_ps(_mm_mul_ps(r, w), r));
}

inline __m128 Shuffle(__m128 a, __m128 b, int imm)
{
    switch (imm)
    {
    case 0x88:
        return _mm_shuffle_ps(a, b, 0x88);
    case 0xDD:
        return _mm_shuffle_ps(a, b, 0xDD);
    case 0xB1:
        return _mm_shuffle_ps(a, b, 0xB1);
    case 0x4E:
        return _mm_shuffle_ps(a, b, 0x4E);
    default:
        return _mm_shuffle_ps(a, b, 0x00);
    }
}

inline __m128 LoadHalves(const float *low, const float *high)
{
    return _mm_loadh_pi(_mm_loadl_pi(_mm_setzero_ps(), reinterpret_cast<const __m64 *>(low)),
                        reinterpret_cast<const __m64 *>(high));
}
} // namespace

/** D3DX's SSE product: each row of `a` against the rows of `b`, rows 0 and 3 summed in pairs, rows 1 and 2 left to
 * right. `out` may be `a` or `b`. */
extern "C" void *__stdcall D3DXMatrixMultiply(void *out, const void *a, const void *b)
{
    const float *x = static_cast<const float *>(a), *y = static_cast<const float *>(b);
    const __m128 b0 = Row(y, 0), b1 = Row(y, 1), b2 = Row(y, 2), b3 = Row(y, 3);
    const __m128 r0 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(Splat(x[0]), b0), _mm_mul_ps(Splat(x[1]), b1)),
                                 _mm_add_ps(_mm_mul_ps(Splat(x[2]), b2), _mm_mul_ps(Splat(x[3]), b3)));
    const __m128 r1 = _mm_add_ps(_mm_add_ps(_mm_add_ps(_mm_mul_ps(Splat(x[4]), b0), _mm_mul_ps(Splat(x[5]), b1)),
                                            _mm_mul_ps(Splat(x[6]), b2)),
                                 _mm_mul_ps(Splat(x[7]), b3));
    const __m128 r2 = _mm_add_ps(_mm_add_ps(_mm_add_ps(_mm_mul_ps(Splat(x[8]), b0), _mm_mul_ps(Splat(x[9]), b1)),
                                            _mm_mul_ps(Splat(x[10]), b2)),
                                 _mm_mul_ps(Splat(x[11]), b3));
    const __m128 r3 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(b0, Splat(x[12])), _mm_mul_ps(b1, Splat(x[13]))),
                                 _mm_add_ps(_mm_mul_ps(b2, Splat(x[14])), _mm_mul_ps(b3, Splat(x[15]))));
    float *o = static_cast<float *>(out);
    _mm_storeu_ps(o, r0);
    _mm_storeu_ps(o + 4, r1);
    _mm_storeu_ps(o + 8, r2);
    _mm_storeu_ps(o + 12, r3);
    return out;
}

extern "C" void *__stdcall D3DXMatrixTranspose(void *out, const void *m)
{
    const float *a = static_cast<const float *>(m);
    float t[16];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            t[c * 4 + r] = a[r * 4 + c];
    memcpy(out, t, sizeof(t));
    return out;
}

/** (x r0 + y r1) + (z r2 + w r3). */
extern "C" void *__stdcall D3DXVec4Transform(void *out, const void *v, const void *m)
{
    const float *p = static_cast<const float *>(v);
    const float *a = static_cast<const float *>(m);
    const __m128 zw = _mm_add_ps(_mm_mul_ps(Row(a, 2), Splat(p[2])), _mm_mul_ps(Row(a, 3), Splat(p[3])));
    const __m128 xy = _mm_add_ps(_mm_mul_ps(Row(a, 0), Splat(p[0])), _mm_mul_ps(Row(a, 1), Splat(p[1])));
    _mm_storeu_ps(static_cast<float *>(out), _mm_add_ps(xy, zw));
    return out;
}

/** The point (x, y, z, 1) through the matrix, all four components. */
extern "C" void *__stdcall D3DXVec3Transform(void *out, const void *v, const void *m)
{
    _mm_storeu_ps(static_cast<float *>(out), TransformPoint(static_cast<const float *>(v), static_cast<const float *>(m)));
    return out;
}

/** The direction (x, y, z, 0) through the matrix: (x r0 + y r1) + z r2. */
extern "C" void *__stdcall D3DXVec3TransformNormal(void *out, const void *v, const void *m)
{
    const float *p = static_cast<const float *>(v);
    const float *a = static_cast<const float *>(m);
    __m128 s = _mm_add_ps(_mm_mul_ps(Splat(p[0]), Row(a, 0)), _mm_mul_ps(Splat(p[1]), Row(a, 1)));
    s = _mm_add_ps(s, _mm_mul_ps(Splat(p[2]), Row(a, 2)));
    float r[4];
    _mm_storeu_ps(r, s);
    memcpy(out, r, 12);
    return out;
}

/**
 * Points (x, y, z, 1) through the matrix, divided by w. Like D3DX: more than ten points go four at a time, each
 * component summed ((x a + y b) + z c) + d; the rest one at a time, summed (x a + y b) + (z c + d).
 */
extern "C" void *__stdcall D3DXVec3TransformCoordArray(void *out, UINT outStride, const void *v, UINT vStride,
                                                        const void *m, UINT count)
{
    const float *a = static_cast<const float *>(m);
    char *o = static_cast<char *>(out);
    const char *in = static_cast<const char *>(v);
    const UINT grouped = count > 10 ? count & ~3u : 0;
    for (UINT i = 0; i < grouped; i += 4, o += 4 * outStride, in += 4 * vStride)
    {
        const float *p[4];
        for (int k = 0; k < 4; ++k)
            p[k] = reinterpret_cast<const float *>(in + k * vStride);
        const __m128 x = _mm_setr_ps(p[0][0], p[1][0], p[2][0], p[3][0]);
        const __m128 y = _mm_setr_ps(p[0][1], p[1][1], p[2][1], p[3][1]);
        const __m128 z = _mm_setr_ps(p[0][2], p[1][2], p[2][2], p[3][2]);
        __m128 c[4];
        for (int k = 0; k < 4; ++k)
            c[k] = _mm_add_ps(
                _mm_add_ps(_mm_add_ps(_mm_mul_ps(x, Splat(a[k])), _mm_mul_ps(y, Splat(a[4 + k]))),
                           _mm_mul_ps(z, Splat(a[8 + k]))),
                Splat(a[12 + k]));
        const __m128 r = Reciprocal(c[3]);
        float rx[4], ry[4], rz[4];
        _mm_storeu_ps(rx, _mm_mul_ps(c[0], r));
        _mm_storeu_ps(ry, _mm_mul_ps(c[1], r));
        _mm_storeu_ps(rz, _mm_mul_ps(c[2], r));
        for (int k = 0; k < 4; ++k)
        {
            float *d = reinterpret_cast<float *>(o + k * outStride);
            d[0] = rx[k];
            d[1] = ry[k];
            d[2] = rz[k];
        }
    }
    for (UINT i = grouped; i < count; ++i, o += outStride, in += vStride)
    {
        const __m128 t = TransformPoint(reinterpret_cast<const float *>(in), a);
        const __m128 r = Reciprocal(t);
        float s[4];
        _mm_storeu_ps(s, _mm_mul_ps(t, _mm_shuffle_ps(r, r, 0xFF)));
        memcpy(o, s, 12);
    }
    return out;
}

/** D3DX's: the hardware's reciprocal square root and one Newton step; zero when the length is below 1.2e-7. */
extern "C" void *__stdcall D3DXVec3Normalize(void *out, const void *v)
{
    const float *p = static_cast<const float *>(v);
    const __m128 x = _mm_set_ss(p[0]), y = _mm_set_ss(p[1]), z = _mm_set_ss(p[2]);
    const __m128 squared = _mm_add_ss(_mm_add_ss(_mm_mul_ss(x, x), _mm_mul_ss(y, y)), _mm_mul_ss(z, z));
    float *o = static_cast<float *>(out);
    // 0x28800000: 2^-46.
    if (!(_mm_cvtss_f32(squared) >= 1.42108547e-14f))
    {
        o[0] = o[1] = o[2] = 0.0f;
        return out;
    }
    const __m128 r = _mm_rsqrt_ss(squared);
    const __m128 rr = _mm_mul_ss(_mm_mul_ss(r, squared), r);
    const __m128 scale = _mm_mul_ss(_mm_sub_ss(_mm_set_ss(3.0f), rr), _mm_mul_ss(r, _mm_set_ss(0.5f)));
    o[0] = _mm_cvtss_f32(_mm_mul_ss(x, scale));
    o[1] = _mm_cvtss_f32(_mm_mul_ss(y, scale));
    o[2] = _mm_cvtss_f32(_mm_mul_ss(z, scale));
    return out;
}

/**
 * D3DX's SSE inverse (Intel's "Streaming SIMD Extensions - Inverse of 4x4 Matrix", AP-928: cofactors from 2x2
 * products, the determinant's reciprocal by the hardware's estimate and one Newton step), step for step, so its
 * results are D3DX's to the bit. Null, and the determinant not stored, when it is 0 (or not a number).
 */
extern "C" void *__stdcall D3DXMatrixInverse(void *out, float *determinant, const void *m)
{
    const float *a = static_cast<const float *>(m);
    __m128 x1, x2, x3, x4, x5, x6, x7;
    __m128 sF30, sF40, sF50, sF60, sF70, sF20, s80, s70, s60, s50, s40, s30, s20;
    x2 = LoadHalves(a + 2, a + 6);
    x4 = LoadHalves(a + 10, a + 14);
    x3 = LoadHalves(a + 8, a + 12);
    x1 = LoadHalves(a + 0, a + 4);
    x5 = Shuffle(x2, x4, 0x88);
    x4 = Shuffle(x4, x2, 0xDD);
    x2 = _mm_mul_ps(x4, x5);
    x2 = Shuffle(x2, x2, 0xB1);
    x6 = Shuffle(x2, x2, 0x4E);
    x7 = x3;
    x3 = Shuffle(x3, x1, 0xDD);
    x1 = Shuffle(x1, x7, 0x88);
    x7 = x3;
    x3 = _mm_mul_ps(x3, x6);
    x6 = _mm_mul_ps(x6, x1);
    sF30 = x6;
    x6 = x7;
    x7 = _mm_mul_ps(x7, x2);
    x2 = _mm_mul_ps(x2, x1);
    x3 = _mm_sub_ps(x3, x7);
    x7 = _mm_mul_ps(x6, x5);
    x5 = Shuffle(x5, x5, 0x4E);
    x7 = Shuffle(x7, x7, 0xB1);
    sF40 = x2;
    x2 = _mm_mul_ps(x4, x7);
    x2 = _mm_add_ps(x2, x3);
    x3 = x7;
    x7 = Shuffle(x7, x7, 0x4E);
    x3 = _mm_mul_ps(x3, x1);
    sF50 = x3;
    x3 = _mm_mul_ps(x4, x7);
    x7 = _mm_mul_ps(x7, x1);
    x2 = _mm_sub_ps(x2, x3);
    x3 = Shuffle(x6, x6, 0x4E);
    x3 = _mm_mul_ps(x3, x4);
    x3 = Shuffle(x3, x3, 0xB1);
    sF60 = x7;
    x7 = x5;
    x5 = _mm_mul_ps(x5, x3);
    x5 = _mm_add_ps(x5, x2);
    x2 = x3;
    x3 = Shuffle(x3, x3, 0x4E);
    x2 = _mm_mul_ps(x2, x1);
    sF70 = x4;
    x4 = x7;
    x7 = _mm_mul_ps(x7, x3);
    x3 = _mm_mul_ps(x3, x1);
    x5 = _mm_sub_ps(x5, x7);
    x3 = _mm_sub_ps(x3, x2);
    x2 = x1;
    x1 = _mm_mul_ps(x1, x5);
    x3 = Shuffle(x3, x3, 0x4E);
    x7 = x1;
    x1 = Shuffle(x1, x1, 0x4E);
    sF20 = x5;
    x1 = _mm_add_ps(x1, x7);
    x5 = x1;
    x1 = Shuffle(x1, x1, 0xB1);
    x1 = _mm_add_ss(x1, x5);
    x5 = _mm_mul_ps(x6, x2);
    x5 = Shuffle(x5, x5, 0xB1);
    x7 = x5;
    x5 = Shuffle(x5, x5, 0x4E);
    s80 = x4;
    x4 = sF70;
    s70 = x6;
    x6 = _mm_mul_ps(x4, x7);
    x6 = _mm_add_ps(x6, x3);
    x3 = _mm_mul_ps(x4, x5);
    x3 = _mm_sub_ps(x3, x6);
    x6 = _mm_mul_ps(x4, x2);
    x6 = Shuffle(x6, x6, 0xB1);
    s60 = x5;
    x5 = s70;
    s50 = x7;
    x7 = _mm_mul_ps(x6, x5);
    x7 = _mm_add_ps(x7, x3);
    x3 = Shuffle(x6, x6, 0x4E);
    s40 = x4;
    x4 = x5;
    x5 = _mm_mul_ps(x5, x3);
    s30 = x4;
    x4 = x6;
    x6 = _mm_sub_ps(x7, x5);
    x5 = _mm_sub_ps(sF30, sF40);
    x5 = Shuffle(x5, x5, 0x4E);
    x7 = s80;
    x4 = _mm_mul_ps(x4, x7);
    x3 = _mm_mul_ps(x3, x7);
    x5 = _mm_sub_ps(x5, x4);
    x2 = _mm_mul_ps(x2, x7);
    x3 = _mm_add_ps(x3, x5);
    x2 = Shuffle(x2, x2, 0xB1);
    x4 = Shuffle(x2, x2, 0x4E);
    x5 = s40;
    s20 = x6;
    x6 = _mm_mul_ps(x5, x4);
    x5 = _mm_mul_ps(x5, x2);
    x5 = _mm_add_ps(x5, x3);
    x3 = x4;
    x4 = _mm_sub_ps(x5, x6);
    x5 = _mm_sub_ps(sF60, sF50);
    x5 = Shuffle(x5, x5, 0x4E);
    x6 = _mm_mul_ps(s50, x7);
    x6 = _mm_sub_ps(x6, x5);
    x5 = s60;
    x7 = _mm_mul_ps(x7, x5);
    x6 = _mm_sub_ps(x6, x7);
    x5 = s30;
    x2 = _mm_mul_ps(x2, x5);
    x5 = _mm_mul_ps(x5, x3);
    x6 = _mm_sub_ps(x6, x2);
    x2 = _mm_add_ps(x5, x6);
    const float det = _mm_cvtss_f32(x1);
    if (det == 0.0f || det != det)
        return nullptr;
    if (determinant)
        *determinant = det;
    x6 = s20;
    __m128 r = _mm_rcp_ss(x1);
    const __m128 rr = _mm_mul_ss(x1, _mm_mul_ss(r, r));
    r = _mm_sub_ss(_mm_add_ss(r, r), rr);
    r = _mm_shuffle_ps(r, r, 0);
    float *o = static_cast<float *>(out);
    _mm_storeu_ps(o, _mm_mul_ps(sF20, r));
    _mm_storeu_ps(o + 4, _mm_mul_ps(x4, r));
    _mm_storeu_ps(o + 8, _mm_mul_ps(x6, r));
    _mm_storeu_ps(o + 12, _mm_mul_ps(r, x2));
    return out;
}

extern "C" void *__stdcall D3DXMatrixTranslation(void *out, float x, float y, float z)
{
    float *o = static_cast<float *>(out);
    memset(o, 0, 64);
    o[0] = o[5] = o[10] = o[15] = 1.0f;
    o[12] = x;
    o[13] = y;
    o[14] = z;
    return out;
}

extern "C" void *__stdcall D3DXMatrixOrthoLH(void *out, float width, float height, float zn, float zf)
{
    float *o = static_cast<float *>(out);
    memset(o, 0, 64);
    o[0] = 2.0f / width;
    o[5] = 2.0f / height;
    o[10] = 1.0f / (zf - zn);
    o[14] = zn / (zn - zf);
    o[15] = 1.0f;
    return out;
}

extern "C" void *__stdcall D3DXMatrixPerspectiveLH(void *out, float width, float height, float zn, float zf)
{
    float *o = static_cast<float *>(out);
    memset(o, 0, 64);
    o[0] = 2.0f * zn / width;
    o[5] = 2.0f * zn / height;
    o[10] = zf / (zf - zn);
    o[11] = 1.0f;
    o[14] = zn * zf / (zn - zf);
    return out;
}

/** The PC effects in the exe's resources: the Vulkan renderer makes its effects itself (nfs::vk::CreateEffect), and
 * the original functions that called this are no longer used. */
extern "C" HRESULT __stdcall D3DXCreateEffectFromResourceA(IDirect3DDevice9 *, HMODULE, const char *, const void *,
                                                          void *, DWORD, ID3DXEffectPool *, ID3DXEffect **effect,
                                                          void **errors)
{
    if (effect)
        *effect = nullptr;
    if (errors)
        *errors = nullptr;
    return E_NOTIMPL;
}

extern "C" HRESULT __stdcall D3DXCreateEffectPool(ID3DXEffectPool **pool)
{
    if (pool)
        *pool = nullptr;
    return E_NOTIMPL;
}

/** The adapter's questions are answered by source/engine/rendering/display/Adapter.cpp. */
extern "C" IDirect3D9 *__stdcall Direct3DCreate9(UINT)
{
    return nfs::adapter::Create();
}
