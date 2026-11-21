#pragma once
/**
 * Small copies and compares of 16-byte rows (shader constant registers, matrices, vectors) with SSE moves. The rebuild
 * links the game's MSVC 7.1 CRT, whose memcpy and memcmp walk bytes and dwords; these stay inline.
 */
#include <emmintrin.h>
#include <stdint.h>
#include <string.h>
#include <xmmintrin.h>

namespace nfs
{
/** Copies `rows` 16-byte rows; neither side needs to be aligned. */
inline void CopyRows(void *to, const void *from, uint32_t rows)
{
    __m128i *d = static_cast<__m128i *>(to);
    const __m128i *s = static_cast<const __m128i *>(from);
    for (uint32_t i = 0; i < rows; ++i)
        _mm_storeu_si128(d + i, _mm_loadu_si128(s + i));
}

/** CopyRows into write-combined memory the GPU reads (the frame's constant ring): `to` must be 16-byte aligned, and
 * the stores bypass the caches. */
inline void StreamRows(void *to, const void *from, uint32_t rows)
{
    __m128i *d = static_cast<__m128i *>(to);
    const __m128i *s = static_cast<const __m128i *>(from);
    for (uint32_t i = 0; i < rows; ++i)
        _mm_stream_si128(d + i, _mm_loadu_si128(s + i));
}

/** Copies `bytes` into write-combined memory the GPU reads (vertices into the frame's ring); `to` must be 16-byte
 * aligned. */
inline void StreamBytes(void *to, const void *from, uint32_t bytes)
{
    const uint32_t rows = bytes >> 4;
    StreamRows(to, from, rows);
    if (bytes & 15)
        memcpy(static_cast<char *>(to) + rows * 16, static_cast<const char *>(from) + rows * 16, bytes & 15);
}

/** Copies `bytes` from `from` to `to` when they differ, bit for bit; true when that changed `to`. */
inline bool StoreIfChanged(void *to, const void *from, uint32_t bytes)
{
    if (bytes & 15)
    {
        if (bytes & 3)
        {
            if (memcmp(to, from, bytes) == 0)
                return false;
            memcpy(to, from, bytes);
            return true;
        }
        uint32_t *d = static_cast<uint32_t *>(to);
        const uint32_t *s = static_cast<const uint32_t *>(from);
        const uint32_t words = bytes >> 2;
        uint32_t differ = 0;
        for (uint32_t i = 0; i < words; ++i)
            differ |= d[i] ^ s[i];
        if (!differ)
            return false;
        for (uint32_t i = 0; i < words; ++i)
            d[i] = s[i];
        return true;
    }
    __m128i *d = static_cast<__m128i *>(to);
    const __m128i *s = static_cast<const __m128i *>(from);
    const uint32_t rows = bytes >> 4;
    uint32_t i = 0;
    while (i < rows && _mm_movemask_epi8(_mm_cmpeq_epi32(_mm_loadu_si128(s + i), _mm_loadu_si128(d + i))) == 0xFFFF)
        ++i;
    if (i == rows)
        return false;
    for (; i < rows; ++i)
        _mm_storeu_si128(d + i, _mm_loadu_si128(s + i));
    return true;
}
/** out = a x b for row-major 4x4 float matrices (D3DXMatrixMultiply's product, summed in the same order); `out` must
 * not be either of them. */
inline void MultiplyMatrix(float *out, const float *a, const float *b)
{
    const __m128 b0 = _mm_loadu_ps(b), b1 = _mm_loadu_ps(b + 4), b2 = _mm_loadu_ps(b + 8), b3 = _mm_loadu_ps(b + 12);
    for (int r = 0; r < 4; ++r)
    {
        const float *row = a + r * 4;
        __m128 v = _mm_mul_ps(_mm_set1_ps(row[0]), b0);
        v = _mm_add_ps(v, _mm_mul_ps(_mm_set1_ps(row[1]), b1));
        v = _mm_add_ps(v, _mm_mul_ps(_mm_set1_ps(row[2]), b2));
        v = _mm_add_ps(v, _mm_mul_ps(_mm_set1_ps(row[3]), b3));
        _mm_storeu_ps(out + r * 4, v);
    }
}
} // namespace nfs
