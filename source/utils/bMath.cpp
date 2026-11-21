/**
 * bWare math (original: Speed/Indep/bWare/Src/bMath.cpp), 0x45d9a0-0x460720, plus bCalculateCrc32 and bSetRandomSeed.
 * Angles are 16-bit binary angles (bAngle); fixed-point values are 16.16 (bFix).
 *
 * Precision: the original keeps intermediates on the x87 stack (53-bit with the game's FPU control word) and rounds only
 * what it stores to memory. Here `double` stands for a value the original keeps in a register and `float` for one it
 * stores, and expression order follows the original code, so results match bit for bit where that was practical.
 */
#include <math.h>

#include "game/bWare/bMath.h"
#include "legacy/link.h"

NFS_BIND(0x8ecd78, bFixSinTable)

extern "C" void *__stdcall D3DXMatrixTranspose(void *out, const void *m);

namespace
{
const float kAngleToRadians = 9.58738e-05f;
const float kRadiansToAngle = 10430.378f;
const float kPi = 3.1415927f;
const float kHalfPi = 1.5707964f;
const float kThreeHalfPi = 4.712389f;
const float kTwoPi = 6.2831855f;
const float kFloatMax = 3.4028235e38f;

unsigned int NextRandom(unsigned int seed)
{
    unsigned int a = seed ^ 0x1D872B41;
    unsigned int b = (a >> 5) ^ a;
    return (b << 27) ^ b ^ a;
}

/** Folds an angle into [-pi/2, pi/2); returns the sign to apply to sin and cos. */
inline double ReduceAngle(bAngle angle, double &x)
{
    x = static_cast<double>(angle) * kAngleToRadians;
    if (!(x < kThreeHalfPi))
    {
        x -= kTwoPi;
    }
    else if (!(x < kHalfPi))
    {
        x -= kPi;
        return -1.0;
    }
    return 1.0;
}

bAngle ApplyQuadrant(int quad, unsigned int a)
{
    switch (quad)
    {
    case 0:
        return static_cast<bAngle>(a);
    case 3:
        return static_cast<bAngle>(0u - a);
    case 1:
        return static_cast<bAngle>(0x8000 - a);
    default:
        return static_cast<bAngle>(a + 0x8000);
    }
}
} // namespace

unsigned int bCalculateCrc32(const void *data, int size, unsigned int crc)
{
    const unsigned char *p = static_cast<const unsigned char *>(data);
    if (size > 0)
    {
        for (int i = size; i != 0; --i)
            crc = (crc << 8) ^ bCrcTable[(crc >> 24) ^ *p++];
        for (; size > 0; size >>= 8)
            crc = (crc << 8) ^ bCrcTable[((crc >> 24) ^ size) & 0xFF];
    }
    return ~crc;
}

void bSetRandomSeed(unsigned int value, unsigned int *seed)
{
    *seed = NextRandom(value);
}

unsigned int bRandom(int range, unsigned int *seed)
{
    if (range == 0)
        return 0;
    unsigned int current = *seed;
    *seed = NextRandom(current);
    return current % static_cast<unsigned int>(range);
}

float bRandom(float range, unsigned int *seed)
{
    unsigned int current = *seed;
    *seed = NextRandom(current);
    return static_cast<float>(static_cast<double>(current % 0x7FFFFFFF) * range * 4.656613e-10f);
}

unsigned int bRandom(int range)
{
    if (range == 0)
        return 0;
    unsigned int current = bDefaultSeed;
    bDefaultSeed = NextRandom(current);
    return current % static_cast<unsigned int>(range);
}

float bRandom(float range)
{
    unsigned int current = bDefaultSeed;
    bDefaultSeed = NextRandom(current);
    return static_cast<float>(static_cast<double>(current % 0x7FFFFFFF) * range * 4.656613e-10f);
}

float bFMod(float a, float b)
{
    double d = b < 0.0f ? -b : b;
    double c = a / d;
    double t = static_cast<int>(c);
    if (t > c)
        t -= 1.0;
    return static_cast<float>((c - t) * d);
}

int bSignedMod(int a, int b)
{
    if (b < 0)
        b = -b;
    int c = (a / b) * b;
    if (a < 0)
        c -= b;
    return a - c;
}

float bSin(bAngle angle)
{
    double x;
    double sign = ReduceAngle(angle, x);
    double x2 = x * x;
    double x3 = x2 * x;
    double x5 = x3 * x2;
    double x7 = x5 * x2;
    return static_cast<float>(
        ((((x - x3 * 0.16666667f) + x5 * 0.008333334f) - x7 * 0.0001984127f) + x7 * x2 * 2.7557319e-06f) * sign);
}

float bSin(float radians)
{
    return bSin(static_cast<bAngle>(static_cast<int>(radians * kRadiansToAngle)));
}

float bCos(bAngle angle)
{
    return bSin(static_cast<bAngle>(angle + 0x4000));
}

void bSinCos(float *result_sin, float *result_cos, bAngle angle)
{
    double x;
    double sign = ReduceAngle(angle, x);
    double x2 = x * x;
    double x3 = x2 * x;
    float x4 = static_cast<float>(x2 * x2);
    double x5 = x3 * x2;
    float x6 = static_cast<float>(x4 * x2);
    double x7 = x5 * x2;
    float x8 = static_cast<float>(x6 * x2);
    *result_sin = static_cast<float>(
        ((((x - x3 * 0.16666667f) + x5 * 0.008333334f) - x7 * 0.0001984127f) + x7 * x2 * 2.7557319e-06f) * sign);
    *result_cos = static_cast<float>(
        (((((1.0 - x2 * 0.5f) + x4 * 0.041666668f) - x6 * 0.0013888889f) + x8 * 2.4801588e-05f) -
         x8 * x2 * 2.755732e-07f) *
        sign);
}

bAngle bASin(float x)
{
    int negative = 0;
    if (x < 0.0f)
    {
        negative = 1;
        x = -x;
    }
    if (!(x < 1.0f))
        return negative ? 0xC000 : 0x4000;

    // The table is split into 12 sub-tables of 16 entries; each one covers half the remaining range towards 1.0.
    int fix_x = static_cast<int>(x * 65536.0f);
    int table_size = 0x8000;
    int table_top = 0x8000;
    int table_number = 0;
    if (fix_x >= table_top)
    {
        while (table_number < 11)
        {
            table_size >>= 1;
            table_top += table_size;
            ++table_number;
            if (fix_x < table_top)
                break;
        }
    }
    int table_bottom = table_top - table_size;
    int table_index = (fix_x - table_bottom) >> (11 - table_number);
    int table_x = (table_size >> 4) * table_index + table_bottom;
    const ASinTableEntry &entry = bASinTable[table_number * 16 + table_index];
    double remainder_x = x - static_cast<double>(table_x) * 1.5258789e-05f;
    int correction = static_cast<int>(remainder_x * entry.Slope * -65536.0f);
    unsigned short a = static_cast<unsigned short>(entry.Angle - correction);
    return negative ? static_cast<bAngle>(0x10000 - a) : a;
}

bAngle bFastATan(float x, float y)
{
    int quad = 0;
    if (x < 0.0f)
    {
        quad = 1;
        x = -x;
    }
    if (y < 0.0f)
    {
        quad ^= 3;
        y = -y;
    }
    unsigned int a;
    if (!(x <= y))
        a = bFastATanTable[static_cast<int>(static_cast<double>(y) / x * 256.0f)];
    else if (!(y <= x))
        a = static_cast<unsigned short>(0x4000 - bFastATanTable[static_cast<int>(static_cast<double>(x) / y * 256.0f)]);
    else
        a = y == 0.0f ? 0 : 0x2000;
    return ApplyQuadrant(quad, a);
}

bAngle bATan(float x, float y)
{
    int quad = 0;
    if (x < 0.0f)
    {
        quad = 1;
        x = -x;
    }
    if (y < 0.0f)
    {
        quad ^= 3;
        y = -y;
    }
    unsigned int a;
    if (!(x <= y))
    {
        int r = static_cast<int>(static_cast<double>(y) / x * 65536.0f);
        const unsigned short *table = &bFastATanTable[r >> 8];
        a = ((static_cast<int>(table[1] - table[0]) * (r & 0xFF)) >> 8) + table[0];
    }
    else if (!(y <= x))
    {
        int r = static_cast<int>(static_cast<double>(x) / y * 65536.0f);
        const unsigned short *table = &bFastATanTable[r >> 8];
        a = 0x4000 - ((static_cast<int>(table[1] - table[0]) * (r & 0xFF)) >> 8) - table[0];
    }
    else
    {
        a = y == 0.0f ? 0 : 0x2000;
    }
    return ApplyQuadrant(quad, a);
}

bFix bFixSin(bAngle angle)
{
    const short *entry = &bFixSinTable[angle >> 7];
    int a = entry[0] * 2;
    int b = entry[1] * 2;
    return (((b - a) * ((angle & 0x7F) << 9)) >> 16) + a;
}

bAngle bFixATan(bFix x)
{
    int negative = 0;
    if (x < 0)
    {
        negative = 1;
        x = -x;
    }
    unsigned int a, b;
    int interpolation_ratio;
    if (x < 0x200000)
    {
        if (x < 0x40000)
        {
            const unsigned short *entry = &bFixATanTableLow[x >> 11];
            a = entry[0];
            b = entry[1];
            interpolation_ratio = (x & 0x7FF) << 5;
        }
        else
        {
            const unsigned short *entry = &bFixATanTableHigh[x >> 14];
            a = entry[0];
            b = entry[1];
            interpolation_ratio = (x & 0x3FFF) << 2;
        }
    }
    else if (x < 0x1000000)
    {
        interpolation_ratio = x >> 8;
        a = 0x3EBA;
        b = 0x3FD7;
    }
    else
    {
        a = b = 0x3FFF;
        interpolation_ratio = 0;
    }
    int result = static_cast<int>((b - a) * static_cast<unsigned int>(interpolation_ratio)) >> 16;
    result += a;
    return static_cast<bAngle>(negative ? -result : result);
}

bAngle bFixATan(bFix x, bFix y)
{
    int quad = 0;
    if (x < 0)
    {
        quad = 1;
        x = -x;
    }
    if (y < 0)
    {
        quad ^= 3;
        y = -y;
    }
    unsigned int a;
    if (x <= (y >> 14))
    {
        a = 0x4000;
    }
    else
    {
        int ratio;
        if (x == 0)
            ratio = y == 0 ? 0 : static_cast<int>(0x7FFFFFFFu + (y <= 0));
        else
            ratio = static_cast<int>(static_cast<unsigned int>(static_cast<int>(
                                         (static_cast<long long>(0x7FFFFFFF / x) * y) >> 16))
                                     << 1);
        a = bFixATan(ratio);
    }
    switch (quad)
    {
    case 0:
        return static_cast<bAngle>(a);
    case 1:
        return static_cast<bAngle>(0x8000 - a);
    case 2:
        return static_cast<bAngle>(a - 0x8000);
    default:
        return static_cast<bAngle>(0u - a);
    }
}

bPolar *bToPolar(bPolar *dest, bVector2 *cartesian)
{
    float x = cartesian->x;
    float y = cartesian->y;
    dest->a = bATan(x, y);
    dest->r = static_cast<float>(sqrt(static_cast<double>(y) * y + static_cast<double>(x) * x));
    return dest;
}

void bConvertToBond(bMatrix4 &dest, const bMatrix4 &m)
{
    const bMatrix4 v = m;
    dest.v0 = bVector4(v.v1.y, -v.v1.z, -v.v1.x, v.v1.w);
    dest.v1 = bVector4(-v.v2.y, v.v2.z, v.v2.x, v.v2.w);
    dest.v2 = bVector4(-v.v0.y, v.v0.z, v.v0.x, v.v0.w);
    dest.v3 = bVector4(-v.v3.y, v.v3.z, v.v3.x, v.v3.w);
}

void bConvertFromBond(bMatrix4 &dest, const bMatrix4 &m)
{
    const bMatrix4 v = m;
    dest.v0 = bVector4(v.v2.z, -v.v2.x, v.v2.y, v.v2.w);
    dest.v1 = bVector4(-v.v0.z, v.v0.x, -v.v0.y, v.v0.w);
    dest.v2 = bVector4(v.v1.z, -v.v1.x, v.v1.y, v.v1.w);
    dest.v3 = bVector4(v.v3.z, -v.v3.x, v.v3.y, v.v3.w);
}

void bMathTimingTest()
{
}

/** Inverse of an affine matrix (3x3 rotation/scale plus translation row); the last column becomes (0, 0, 0, 1). */
void bInvertMatrix(bMatrix4 *dest, const bMatrix4 *src)
{
    double m11 = src->v0.x, m12 = src->v0.y, m13 = src->v0.z;
    double m21 = src->v1.x, m22 = src->v1.y, m23 = src->v1.z;
    double m31 = src->v2.x, m32 = src->v2.y, m33 = src->v2.z;
    float m41 = src->v3.x, m42 = src->v3.y, m43 = src->v3.z;
    double c11 = m22 * m33 - m32 * m23;
    float c21 = static_cast<float>(m33 * m21 - m31 * m23);
    float c31 = static_cast<float>(m32 * m21 - m31 * m22);
    float det_inv = static_cast<float>(1.0 / ((c11 * m11 - c21 * m12) + c31 * m13));

    float d11 = static_cast<float>(c11 * det_inv);
    float d12 = static_cast<float>(-((m33 * m12 - m32 * m13) * det_inv));
    float d13 = static_cast<float>((m23 * m12 - m22 * m13) * det_inv);
    float d21 = -(c21 * det_inv);
    float d22 = static_cast<float>((m33 * m11 - m31 * m13) * det_inv);
    float d23 = static_cast<float>(-((m23 * m11 - m21 * m13) * det_inv));
    float d31 = c31 * det_inv;
    float d32 = static_cast<float>(-((m32 * m11 - m31 * m12) * det_inv));
    double d33 = (m22 * m11 - m21 * m12) * det_inv;
    dest->v0 = bVector4(d11, d12, d13, 0.0f);
    dest->v1 = bVector4(d21, d22, d23, 0.0f);
    dest->v2 = bVector4(d31, d32, static_cast<float>(d33), 0.0f);
    dest->v3.x = static_cast<float>(-((static_cast<double>(d31) * m43 + static_cast<double>(d21) * m42) +
                                      static_cast<double>(d11) * m41));
    dest->v3.y = static_cast<float>(-((static_cast<double>(d32) * m43 + static_cast<double>(d22) * m42) +
                                      static_cast<double>(d12) * m41));
    dest->v3.z = static_cast<float>(-((d33 * m43 + static_cast<double>(d23) * m42) + static_cast<double>(d13) * m41));
    dest->v3.w = 1.0f;
}

float fDeterminant(bMatrix4 *matrix)
{
    const float *m = &matrix->v0.x;
    return m[0] * (m[5] * (m[10] * m[15] - m[11] * m[14]) - m[6] * (m[9] * m[15] - m[11] * m[13]) +
                   m[7] * (m[9] * m[14] - m[10] * m[13])) -
           m[1] * (m[4] * (m[10] * m[15] - m[11] * m[14]) - m[6] * (m[8] * m[15] - m[11] * m[12]) +
                   m[7] * (m[8] * m[14] - m[10] * m[12])) +
           m[2] * (m[4] * (m[9] * m[15] - m[11] * m[13]) - m[5] * (m[8] * m[15] - m[11] * m[12]) +
                   m[7] * (m[8] * m[13] - m[9] * m[12])) -
           m[3] * (m[4] * (m[9] * m[14] - m[10] * m[13]) - m[5] * (m[8] * m[14] - m[10] * m[12]) +
                  m[6] * (m[8] * m[13] - m[9] * m[12]));
}

/** General 4x4 inverse by cofactors (no singularity check). */
void fInvertMatrix(bMatrix4 *d, bMatrix4 *s)
{
    float scale = 1.0f / fDeterminant(s);
    const float *m = &s->v0.x;
    float *o = &d->v0.x;
    o[0] = (m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
            m[13] * m[6] * m[11] - m[13] * m[7] * m[10]) * scale;
    o[1] = (-m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
            m[13] * m[2] * m[11] + m[13] * m[3] * m[10]) * scale;
    o[2] = (m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
            m[13] * m[2] * m[7] - m[13] * m[3] * m[6]) * scale;
    o[3] = (-m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
            m[9] * m[2] * m[7] + m[9] * m[3] * m[6]) * scale;
    o[4] = (-m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
            m[12] * m[6] * m[11] + m[12] * m[7] * m[10]) * scale;
    o[5] = (m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
            m[12] * m[2] * m[11] - m[12] * m[3] * m[10]) * scale;
    o[6] = (-m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
            m[12] * m[2] * m[7] + m[12] * m[3] * m[6]) * scale;
    o[7] = (m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
            m[8] * m[2] * m[7] - m[8] * m[3] * m[6]) * scale;
    o[8] = (m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
            m[12] * m[5] * m[11] - m[12] * m[7] * m[9]) * scale;
    o[9] = (-m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
            m[12] * m[1] * m[11] + m[12] * m[3] * m[9]) * scale;
    o[10] = (m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
             m[12] * m[1] * m[7] - m[12] * m[3] * m[5]) * scale;
    o[11] = (-m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
             m[8] * m[1] * m[7] + m[8] * m[3] * m[5]) * scale;
    o[12] = (-m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
             m[12] * m[5] * m[10] + m[12] * m[6] * m[9]) * scale;
    o[13] = (m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
             m[12] * m[1] * m[10] - m[12] * m[2] * m[9]) * scale;
    o[14] = (-m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
             m[12] * m[1] * m[6] + m[12] * m[2] * m[5]) * scale;
    o[15] = (m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
             m[8] * m[1] * m[6] - m[8] * m[2] * m[5]) * scale;
}

/** Hermite basis for the curve through parameters u1..u4: b = H * (D * inverse(U)) * p. */
void hermite_basis(bMatrix4 *b, bMatrix4 *p, float u1, float u2, float u3, float u4)
{
    __declspec(align(16)) bMatrix4 u;
    u.v0 = bVector4(u1 * u1 * u1, u1 * u1, u1, 1.0f);
    u.v1 = bVector4(u2 * u2 * u2, u2 * u2, u2, 1.0f);
    u.v2 = bVector4(u3 * u3 * u3, u3 * u3, u3, 1.0f);
    u.v3 = bVector4(u4 * u4 * u4, u4 * u4, u4, 1.0f);
    __declspec(align(16)) bMatrix4 h;
    h.v0 = bVector4(2.0f, -2.0f, 1.0f, 1.0f);
    h.v1 = bVector4(-3.0f, 3.0f, -2.0f, -1.0f);
    h.v2 = bVector4(0.0f, 0.0f, 1.0f, 0.0f);
    h.v3 = bVector4(1.0f, 0.0f, 0.0f, 0.0f);
    __declspec(align(16)) bMatrix4 d;
    d.v0 = bVector4(0.0f, 0.0f, 0.0f, 1.0f);
    d.v1 = bVector4(1.0f, 1.0f, 1.0f, 1.0f);
    d.v2 = bVector4(0.0f, 0.0f, 1.0f, 0.0f);
    d.v3 = bVector4(3.0f, 2.0f, 1.0f, 0.0f);
    __declspec(align(16)) bMatrix4 inverse;
    __declspec(align(16)) bMatrix4 t;
    fInvertMatrix(&inverse, &u);
    eMulMatrix(&u, &d, &inverse);
    eMulMatrix(&t, &h, &u);
    eMulMatrix(b, &t, p);
}

void hermite_parameter(bVector4 *result, const bMatrix4 *basis, float t)
{
    __declspec(align(16)) bVector4 powers(t * t * t, t * t, t, 1.0f);
    eMulVector(result, basis, &powers);
}

void bMulMatrix(bMatrix4 *dest, const bMatrix4 *a, const bMatrix4 *b)
{
    eMulMatrix(dest, const_cast<bMatrix4 *>(b), const_cast<bMatrix4 *>(a));
}

bMatrix4 *bTransposeMatrix(bMatrix4 *dest, const bMatrix4 *m)
{
    D3DXMatrixTranspose(dest, m);
    return dest;
}

void bMatrixToQuaternion(bQuaternion &q, const bMatrix4 &matrix)
{
    const float *m = &matrix.v0.x;
    double trace = static_cast<double>(m[5]) + m[0] + m[10];
    if (trace > 0.0f)
    {
        double s = sqrt(trace + 1.0);
        q.w = static_cast<float>(s * 0.5f);
        s = 0.5f / s;
        q.x = static_cast<float>((static_cast<double>(m[6]) - m[9]) * s);
        q.y = static_cast<float>((static_cast<double>(m[8]) - m[2]) * s);
        q.z = static_cast<float>((static_cast<double>(m[1]) - m[4]) * s);
        return;
    }
    unsigned int i = m[0] < m[5] ? 1 : 0;
    if (m[i * 5] < m[10])
        i = 2;
    unsigned int j = (i + 1) % 3;
    unsigned int k = (j + 1) % 3;
    double s = sqrt((static_cast<double>(m[i * 5]) - m[j * 5]) - m[k * 5] + 1.0);
    float r[4];
    r[i] = static_cast<float>(s * 0.5f);
    if (s != 0.0f)
        s = 0.5f / s;
    r[3] = static_cast<float>((static_cast<double>(m[j * 4 + k]) - m[k * 4 + j]) * s);
    r[j] = static_cast<float>((static_cast<double>(m[j * 4 + i]) + m[i * 4 + j]) * s);
    r[k] = static_cast<float>((static_cast<double>(m[k * 4 + i]) + m[i * 4 + k]) * s);
    q.x = r[0];
    q.y = r[1];
    q.z = r[2];
    q.w = r[3];
}

float bDistBetween(const bVector3 *a, const bVector3 *b)
{
    double dx = static_cast<double>(a->x) - b->x, dy = static_cast<double>(a->y) - b->y,
           dz = static_cast<double>(a->z) - b->z;
    return static_cast<float>(sqrt(dz * dz + dy * dy + dx * dx));
}

float bDistBetween(const bVector4 *a, const bVector4 *b)
{
    double dx = static_cast<double>(a->x) - b->x, dy = static_cast<double>(a->y) - b->y,
           dz = static_cast<double>(a->z) - b->z, dw = static_cast<double>(a->w) - b->w;
    return static_cast<float>(sqrt(dw * dw + dz * dz + dy * dy + dx * dx));
}

bVector2 *bNormalize(bVector2 *dest, const bVector2 *v)
{
    float x = v->x, y = v->y;
    double len = sqrt(static_cast<double>(y) * y + static_cast<double>(x) * x);
    if (len != 0.0)
    {
        double inv = 1.0 / len;
        dest->x = static_cast<float>(inv * x);
        dest->y = static_cast<float>(inv * y);
    }
    else
    {
        dest->x = 1.0f;
        dest->y = 0.0f;
    }
    return dest;
}

bVector2 *bNormalize(bVector2 *dest, const bVector2 *v, float length)
{
    float x = v->x, y = v->y;
    float len = static_cast<float>(sqrt(static_cast<double>(y) * y + static_cast<double>(x) * x) / length);
    if (len != 0.0f)
    {
        double inv = 1.0 / len;
        dest->x = static_cast<float>(inv * x);
        dest->y = static_cast<float>(inv * y);
    }
    else
    {
        dest->x = 1.0f;
        dest->y = 0.0f;
    }
    return dest;
}

bVector3 *bNormalize(bVector3 *dest, const bVector3 *v)
{
    float x = v->x, y = v->y, z = v->z;
    double len = sqrt(static_cast<double>(x) * x + static_cast<double>(y) * y + static_cast<double>(z) * z);
    if (len != 0.0)
    {
        double inv = 1.0 / len;
        dest->x = static_cast<float>(inv * x);
        dest->y = static_cast<float>(y * inv);
        dest->z = static_cast<float>(z * inv);
    }
    else
    {
        dest->x = 1.0f;
        dest->y = 0.0f;
        dest->z = 0.0f;
    }
    return dest;
}

bVector3 *bNormalize(bVector3 *dest, const bVector3 *v, float length)
{
    float x = v->x, y = v->y, z = v->z;
    double len = sqrt(static_cast<double>(x) * x + static_cast<double>(y) * y + static_cast<double>(z) * z) / length;
    if (len != 0.0)
    {
        double inv = 1.0 / len;
        dest->x = static_cast<float>(inv * x);
        dest->y = static_cast<float>(y * inv);
        dest->z = static_cast<float>(z * inv);
    }
    else
    {
        dest->x = length;
        dest->y = 0.0f;
        dest->z = 0.0f;
    }
    return dest;
}

/** Scales x, y and z by the inverse 4D length; w is left as it is. */
bVector4 *bNormalize(bVector4 *dest, const bVector4 *v)
{
    float x = v->x, y = v->y, z = v->z, w = v->w;
    double len = sqrt(static_cast<double>(w) * w + static_cast<double>(z) * z + static_cast<double>(y) * y +
                      static_cast<double>(x) * x);
    if (len != 0.0)
    {
        double inv = 1.0 / len;
        dest->x = static_cast<float>(inv * x);
        dest->y = static_cast<float>(y * inv);
        dest->z = static_cast<float>(z * inv);
    }
    else
    {
        dest->x = 1.0f;
        dest->y = 0.0f;
        dest->z = 0.0f;
    }
    return dest;
}

bVector2 *bScaleAdd(bVector2 *dest, const bVector2 *a, const bVector2 *b, float scale)
{
    float bx = b->x, by = b->y, ax = a->x, ay = a->y;
    dest->x = scale * bx + ax;
    dest->y = scale * by + ay;
    return dest;
}

bVector3 *bScaleAdd(bVector3 *dest, const bVector3 *a, const bVector3 *b, float scale)
{
    float bx = b->x, by = b->y, bz = b->z, ax = a->x, ay = a->y, az = a->z;
    dest->x = scale * bx + ax;
    dest->y = scale * by + ay;
    dest->z = scale * bz + az;
    return dest;
}

bVector4 *bScaleAdd(bVector4 *dest, const bVector4 *a, const bVector4 *b, float scale)
{
    float bx = b->x, by = b->y, bz = b->z, bw = b->w, ax = a->x, ay = a->y, az = a->z, aw = a->w;
    dest->w = scale * bw + aw;
    dest->x = scale * bx + ax;
    dest->y = scale * by + ay;
    dest->z = scale * bz + az;
    return dest;
}

int bEqual(const bVector2 *a, const bVector2 *b, float epsilon)
{
    return fabs(static_cast<double>(a->x) - b->x) <= epsilon && fabs(static_cast<double>(a->y) - b->y) <= epsilon;
}

int bEqual(const bVector3 *a, const bVector3 *b, float epsilon)
{
    return fabs(static_cast<double>(a->x) - b->x) <= epsilon && fabs(static_cast<double>(a->y) - b->y) <= epsilon && fabs(static_cast<double>(a->z) - b->z) <= epsilon;
}

int bEqual(const bVector4 *a, const bVector4 *b, float epsilon)
{
    return fabs(static_cast<double>(a->x) - b->x) <= epsilon && fabs(static_cast<double>(a->y) - b->y) <= epsilon && fabs(static_cast<double>(a->z) - b->z) <= epsilon &&
           fabs(static_cast<double>(a->w) - b->w) <= epsilon;
}

bVector3 *bCross(bVector3 *dest, const bVector3 *a, const bVector3 *b)
{
    float y = b->x * a->z - b->z * a->x;
    float z = b->y * a->x - a->y * b->x;
    float x = b->z * a->y - b->y * a->z;
    dest->x = x;
    dest->y = y;
    dest->z = z;
    return dest;
}

void bInitializeBoundingBox(bVector2 *bbox_min, bVector2 *bbox_max)
{
    bbox_min->x = kFloatMax;
    bbox_min->y = kFloatMax;
    bbox_max->x = -kFloatMax;
    bbox_max->y = -kFloatMax;
}

void bInitializeBoundingBox(bVector2 *bbox_min, bVector2 *bbox_max, const bVector2 *point)
{
    float x = point->x, y = point->y;
    bbox_min->x = x;
    bbox_min->y = y;
    bbox_max->x = x;
    bbox_max->y = y;
}

void bExpandBoundingBox(bVector2 *bbox_min, bVector2 *bbox_max, const bVector2 *point)
{
    float min_x = bbox_min->x < point->x ? bbox_min->x : point->x;
    float min_y = bbox_min->y < point->y ? bbox_min->y : point->y;
    float max_x = point->x < bbox_max->x ? bbox_max->x : point->x;
    float max_y = point->y < bbox_max->y ? bbox_max->y : point->y;
    bbox_min->x = min_x;
    bbox_min->y = min_y;
    bbox_max->x = max_x;
    bbox_max->y = max_y;
}

void bExpandBoundingBox(bVector2 *bbox_min, bVector2 *bbox_max, const bVector2 *point, float extra)
{
    float lo_x = point->x - extra, lo_y = point->y - extra;
    float hi_x = extra + point->x, hi_y = extra + point->y;
    float min_x = bbox_min->x < lo_x ? bbox_min->x : lo_x;
    float min_y = bbox_min->y < lo_y ? bbox_min->y : lo_y;
    float max_x = hi_x < bbox_max->x ? bbox_max->x : hi_x;
    float max_y = hi_y < bbox_max->y ? bbox_max->y : hi_y;
    bbox_min->x = min_x;
    bbox_min->y = min_y;
    bbox_max->x = max_x;
    bbox_max->y = max_y;
}

void bExpandBoundingBox(bVector2 *bbox_min, bVector2 *bbox_max, const bVector2 *other_min, const bVector2 *other_max)
{
    float min_x = bbox_min->x < other_min->x ? bbox_min->x : other_min->x;
    float min_y = bbox_min->y < other_min->y ? bbox_min->y : other_min->y;
    float max_x = other_max->x < bbox_max->x ? bbox_max->x : other_max->x;
    float max_y = other_max->y < bbox_max->y ? bbox_max->y : other_max->y;
    bbox_min->x = min_x;
    bbox_min->y = min_y;
    bbox_max->x = max_x;
    bbox_max->y = max_y;
}

int bBoundingBoxIsInside(const bVector2 *bbox_min, const bVector2 *bbox_max, const bVector2 *point, float extra)
{
    return bbox_min->x - extra <= point->x && point->x <= extra + bbox_max->x && bbox_min->y - extra <= point->y &&
           point->y <= extra + bbox_max->y;
}

int bBoundingBoxIsInside(const bVector2 *bbox_min, const bVector2 *bbox_max, const bVector2 *inner_min,
                         const bVector2 *inner_max)
{
    return bbox_min->x <= inner_min->x && inner_max->x <= bbox_max->x && bbox_min->y <= inner_min->y &&
           inner_max->y <= bbox_max->y;
}

int bBoundingBoxOverlapping(const bVector2 *min_a, const bVector2 *max_a, const bVector2 *min_b, const bVector2 *max_b)
{
    return min_b->x < max_a->x && min_a->x < max_b->x && min_b->y < max_a->y && min_a->y < max_b->y;
}

float bBoundingBoxDistOutside(const bVector2 *bbox_min, const bVector2 *bbox_max, const bVector2 *point)
{
    float dist = bbox_min->x - point->x;
    float d = bbox_min->y - point->y;
    if (dist <= d)
        dist = d;
    d = point->x - bbox_max->x;
    if (dist <= d)
        dist = d;
    d = point->y - bbox_max->y;
    if (dist <= d)
        dist = d;
    return dist;
}

void bInitializeBoundingBox(bVector3 *bbox_min, bVector3 *bbox_max)
{
    bbox_min->x = kFloatMax;
    bbox_min->y = kFloatMax;
    bbox_min->z = kFloatMax;
    bbox_max->x = -kFloatMax;
    bbox_max->y = -kFloatMax;
    bbox_max->z = -kFloatMax;
}

void bInitializeBoundingBox(bVector3 *bbox_min, bVector3 *bbox_max, const bVector3 *point)
{
    float x = point->x, y = point->y, z = point->z;
    bbox_min->x = x;
    bbox_min->y = y;
    bbox_min->z = z;
    bbox_max->x = x;
    bbox_max->y = y;
    bbox_max->z = z;
}

void bExpandBoundingBox(bVector3 *bbox_min, bVector3 *bbox_max, const bVector3 *point)
{
    float x = point->x, y = point->y, z = point->z;
    if (x < bbox_min->x)
        bbox_min->x = x;
    if (y < bbox_min->y)
        bbox_min->y = y;
    if (z < bbox_min->z)
        bbox_min->z = z;
    if (bbox_max->x < x)
        bbox_max->x = x;
    if (bbox_max->y < y)
        bbox_max->y = y;
    if (bbox_max->z < z)
        bbox_max->z = z;
}

void bExpandBoundingBox(bVector3 *bbox_min, bVector3 *bbox_max, const bVector3 *point, float extra)
{
    float x = point->x, y = point->y, z = point->z;
    if (x - extra < bbox_min->x)
        bbox_min->x = x - extra;
    if (y - extra < bbox_min->y)
        bbox_min->y = y - extra;
    if (z - extra < bbox_min->z)
        bbox_min->z = z - extra;
    if (bbox_max->x < extra + x)
        bbox_max->x = extra + x;
    if (bbox_max->y < extra + y)
        bbox_max->y = extra + y;
    if (bbox_max->z < extra + z)
        bbox_max->z = extra + z;
}

void bExpandBoundingBox(bVector3 *bbox_min, bVector3 *bbox_max, const bVector3 *other_min, const bVector3 *other_max)
{
    float lx = other_min->x, ly = other_min->y, lz = other_min->z;
    float hx = other_max->x, hy = other_max->y, hz = other_max->z;
    if (lx < bbox_min->x)
        bbox_min->x = lx;
    if (ly < bbox_min->y)
        bbox_min->y = ly;
    if (lz < bbox_min->z)
        bbox_min->z = lz;
    if (bbox_max->x < hx)
        bbox_max->x = hx;
    if (bbox_max->y < hy)
        bbox_max->y = hy;
    if (bbox_max->z < hz)
        bbox_max->z = hz;
}

int bBoundingBoxIsInside(const bVector3 *bbox_min, const bVector3 *bbox_max, const bVector3 *point, float extra)
{
    return bbox_min->x - extra <= point->x && point->x <= extra + bbox_max->x && bbox_min->y - extra <= point->y &&
           point->y <= extra + bbox_max->y && bbox_min->z - extra <= point->z && point->z <= extra + bbox_max->z;
}

int bBoundingBoxIsInside(const bVector3 *bbox_min, const bVector3 *bbox_max, const bVector3 *inner_min,
                         const bVector3 *inner_max)
{
    return bbox_min->x <= inner_min->x && inner_max->x <= bbox_max->x && bbox_min->y <= inner_min->y &&
           inner_max->y <= bbox_max->y && bbox_min->z <= inner_min->z && inner_max->z <= bbox_max->z;
}

int bBoundingBoxOverlapping(const bVector3 *min_a, const bVector3 *max_a, const bVector3 *min_b, const bVector3 *max_b)
{
    return min_b->x < max_a->x && min_a->x < max_b->x && min_b->y < max_a->y && min_a->y < max_b->y &&
           min_b->z < max_a->z && min_a->z < max_b->z;
}

float bBoundingBoxDistOutside(const bVector3 *bbox_min, const bVector3 *bbox_max, const bVector3 *point)
{
    float dist = bbox_min->x - point->x;
    float d = bbox_min->y - point->y;
    if (dist <= d)
        dist = d;
    d = bbox_min->z - point->z;
    if (dist <= d)
        dist = d;
    d = point->x - bbox_max->x;
    if (dist <= d)
        dist = d;
    d = point->y - bbox_max->y;
    if (dist <= d)
        dist = d;
    d = point->z - bbox_max->z;
    if (dist <= d)
        dist = d;
    return dist;
}

/** Distance from `point` to the segment start..end. */
float bDistToLine(const bVector2 *point, const bVector2 *start, const bVector2 *end)
{
    bVector2 d(point->x - start->x, point->y - start->y);
    bVector2 dir(end->x - start->x, end->y - start->y);
    float length = static_cast<float>(sqrt(static_cast<double>(dir.y) * dir.y + static_cast<double>(dir.x) * dir.x));
    bNormalize(&dir, &dir);
    float along = static_cast<float>(static_cast<double>(d.y) * dir.y + static_cast<double>(d.x) * dir.x);
    double across = -static_cast<double>(dir.x) * d.y + static_cast<double>(d.x) * dir.y;
    if (along < 0.0f)
        return static_cast<float>(sqrt(across * across + static_cast<double>(along) * along));
    if (length < along)
    {
        double beyond = static_cast<double>(along) - length;
        return static_cast<float>(sqrt(beyond * beyond + across * across));
    }
    return static_cast<float>(fabs(across));
}

/** Signed area (counter-clockwise positive). */
float bGetPolyArea(const bVector2 *points, int num_points)
{
    float area = 0.0f;
    for (int i = 0; i < num_points; ++i)
    {
        const bVector2 &a = points[i];
        const bVector2 &b = points[(i + 1) % num_points];
        area += a.x * b.y - a.y * b.x;
    }
    return area * 0.5f;
}

/** Even-odd rule test of `point` against the polygon's x/y outline. */
template <typename Vertex> static bool IsPointInPoly(const bVector2 *point, const Vertex *points, int num_points)
{
    float px = point->x, py = point->y;
    bool inside = false;
    for (int i = 0, j = num_points - 1; i < num_points; j = i++)
    {
        const Vertex &a = points[i];
        const Vertex &b = points[j];
        if (((a.y <= py && py < b.y) || (b.y <= py && py < a.y)) && px < (b.x - a.x) * (py - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
    }
    return inside;
}

bool bIsPointInPoly(const bVector2 *point, const bVector2 *points, int num_points)
{
    return IsPointInPoly(point, points, num_points);
}

bool bIsPointInPoly(const bVector2 *point, const bVector3 *points, int num_points)
{
    return IsPointInPoly(point, points, num_points);
}
