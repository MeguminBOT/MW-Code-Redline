/**
 * Rotation helpers of the particles (original: zEcstasy, zCamera): a matrix of a rotation about an axis, and a particle's two corners turned
 * by its angle about the view's axis and scaled by its size.
 */
#include <math.h>

#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x4868b0, VU0_MATRIX4_vect3mult)

namespace
{
const float kTurnsPerDegree = 0.0027777778f;
const float kTwoPi = 6.2831855f;
/** A particle's angle (0x10000 a turn) in degrees. */
const float kDegreesPerAngle = 0.005493164f;
const int kParticleSize = 0xc;
const int kParticleAngle = 0x6a;

/** The original's fsincos: this build has no C runtime's. */
inline void SinCos(double angle, double &s, double &c)
{
    __asm {
        fld angle
        fsincos
        mov eax, c
        fstp qword ptr [eax]
        mov eax, s
        fstp qword ptr [eax]
    }
}
} // namespace

void __cdecl BuildRotate(UMath::Matrix4 &out, float degrees, float x, float y, float z)
{
    float *m = reinterpret_cast<float *>(&out);
    const double turns = static_cast<double>(degrees) * kTurnsPerDegree;
    for (int i = 0; i < 16; ++i)
        m[i] = 0.0f;
    m[15] = 1.0f;
    if (turns == 0.0)
    {
        m[0] = m[5] = m[10] = 1.0f;
        return;
    }
    const double angle = turns * kTwoPi;
    double sine, c;
    SinCos(angle, sine, c);
    const float s = static_cast<float>(sine);
    const double length = sqrt(static_cast<double>(z) * z + static_cast<double>(y) * y + static_cast<double>(x) * x);
    const double inverse = length == 0.0 ? 0.0 : 1.0 / length;
    const double ax = x * inverse;
    const float ay = static_cast<float>(inverse * y), az = static_cast<float>(inverse * z);
    const double ic = 1.0 - c;
    const double icx = ic * ax;
    const float icy = static_cast<float>(ic * ay), icz = static_cast<float>(ic * az);
    m[0] = static_cast<float>(icx * ax + c);
    m[1] = static_cast<float>(ay * icx + static_cast<double>(s * az));
    m[2] = static_cast<float>(icx * az - static_cast<double>(s * ay));
    m[4] = static_cast<float>(icy * ax - static_cast<double>(s * az));
    m[5] = static_cast<float>(static_cast<double>(icy) * ay + c);
    m[6] = icy * az + static_cast<float>(s * ax);
    m[8] = static_cast<float>(icz * ax + static_cast<double>(s * ay));
    m[9] = icz * ay - static_cast<float>(s * ax);
    m[10] = static_cast<float>(static_cast<double>(icz) * az + c);
}

void __cdecl VU0_MATRIX4_vect3mult(const UMath::Vector3 &in, const UMath::Matrix4 &matrix, UMath::Vector3 &out)
{
    const float *v = reinterpret_cast<const float *>(&in), *m = reinterpret_cast<const float *>(&matrix);
    const float x = v[0], y = v[1], z = v[2];
    float *o = reinterpret_cast<float *>(&out);
    o[0] = x * m[0] + y * m[4] + z * m[8] + m[12];
    o[1] = x * m[1] + y * m[5] + z * m[9] + m[13];
    o[2] = x * m[2] + y * m[6] + z * m[10] + m[14];
}

void __cdecl PlatRotateScaleParticle(EmitterParticle *particle, UMath::Vector3 &first, UMath::Vector3 &second, UMath::Vector3 &axis,
                                     UMath::Vector3 &secondOut, UMath::Vector3 &firstOut)
{
    const char *p = reinterpret_cast<const char *>(particle);
    const float *a = reinterpret_cast<const float *>(&axis);
    alignas(16) float rotation[16];
    UMath::Matrix4 &matrix = *reinterpret_cast<UMath::Matrix4 *>(rotation);
    BuildRotate(matrix, static_cast<float>(*reinterpret_cast<const unsigned short *>(p + kParticleAngle)) * kDegreesPerAngle, a[0], a[1], a[2]);
    VU0_MATRIX4_vect3mult(second, matrix, secondOut);
    VU0_MATRIX4_vect3mult(first, matrix, firstOut);
    const float size = *reinterpret_cast<const float *>(p + kParticleSize);
    float *f = reinterpret_cast<float *>(&firstOut), *s = reinterpret_cast<float *>(&secondOut);
    for (int i = 0; i < 3; ++i)
    {
        f[i] *= size;
        s[i] *= size;
    }
}
