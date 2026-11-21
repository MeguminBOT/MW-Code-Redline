/** The engine's matrix helpers over the D3DX maths (original: zPhysicsBehaviors eMulMatrix, eMulVector,
 * eCreateRotationZ, eRotateX/Y/Z, eTranslate, eCreateTranslationMatrix, eInvertRotationMatrix,
 * eInvertTransformationMatrix, eCreateAxisRotationMatrix, eMathInit). */
#include <string.h>

#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x6bdd50, eTranslateMatrix)
NFS_REPLACE(0x6bddf0, eCreateTranslationMatrixAt)
NFS_REPLACE(0x6bd830, eMathInitIdentity)
NFS_REPLACE(0x6bd980, eCreateAxisRotation)

extern "C" void *__stdcall D3DXMatrixMultiply(void *out, const void *a, const void *b);
extern "C" void *__stdcall D3DXVec4Transform(void *out, const void *v, const void *m);
extern "C" void *__stdcall D3DXVec3Transform(void *out, const void *v, const void *m);

namespace
{
struct Matrix
{
    float m[16];
};

Matrix Identity()
{
    Matrix r = {};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}
} // namespace

void eMulMatrix(bMatrix4 *out, bMatrix4 *a, bMatrix4 *b)
{
    D3DXMatrixMultiply(out, a, b);
}

void eMulVector(bVector4 *out, const bMatrix4 *m, const bVector4 *v)
{
    D3DXVec4Transform(out, v, m);
}

void eMulVector(bVector3 *out, const bMatrix4 *m, const bVector3 *v)
{
    D3DXVec3Transform(out, v, m);
}

void eCreateRotationZ(bMatrix4 *out, unsigned short angle)
{
    const float c = bCos(angle);
    const float s = bSin(angle);
    Matrix r = Identity();
    r.m[0] = c;
    r.m[1] = s;
    r.m[4] = -s;
    r.m[5] = c;
    memcpy(out, &r, sizeof(r));
}

void eRotateX(bMatrix4 *out, bMatrix4 *in, unsigned short angle)
{
    const float c = bCos(angle);
    const float s = bSin(angle);
    Matrix r = Identity();
    r.m[5] = c;
    r.m[6] = s;
    r.m[9] = -s;
    r.m[10] = c;
    D3DXMatrixMultiply(out, in, &r);
}

void eRotateY(bMatrix4 *out, bMatrix4 *in, unsigned short angle)
{
    const float c = bCos(angle);
    const float s = bSin(angle);
    Matrix r = Identity();
    r.m[0] = c;
    r.m[2] = -s;
    r.m[8] = s;
    r.m[10] = c;
    D3DXMatrixMultiply(out, in, &r);
}

void eRotateZ(bMatrix4 *out, bMatrix4 *in, unsigned short angle)
{
    bMatrix4 r;
    eCreateRotationZ(&r, angle);
    D3DXMatrixMultiply(out, in, &r);
}

void __cdecl eTranslateMatrix(bMatrix4 *out, bMatrix4 *in, const float *by)
{
    Matrix t = Identity();
    t.m[12] = by[0];
    t.m[13] = by[1];
    t.m[14] = by[2];
    D3DXMatrixMultiply(out, in, &t);
}

void __cdecl eCreateTranslationMatrixAt(bMatrix4 *out, const float *at)
{
    Matrix t = Identity();
    t.m[12] = at[0];
    t.m[13] = at[1];
    t.m[14] = at[2];
    memcpy(out, &t, sizeof(t));
}

bMatrix4 *eInvertRotationMatrix(bMatrix4 *out, bMatrix4 *in)
{
    bTransposeMatrix(out, in);
    float *m = reinterpret_cast<float *>(out);
    m[3] = m[7] = m[11] = m[15] = 0.0f;
    m[12] = m[13] = m[14] = 0.0f;
    return out;
}

bMatrix4 *eInvertTransformationMatrix(bMatrix4 *out, const bMatrix4 *in)
{
    const float *m = reinterpret_cast<const float *>(in);
    Matrix r = Identity();
    r.m[0] = m[0];
    r.m[1] = m[4];
    r.m[2] = m[8];
    r.m[4] = m[1];
    r.m[5] = m[5];
    r.m[6] = m[9];
    r.m[8] = m[2];
    r.m[9] = m[6];
    r.m[10] = m[10];
    Matrix t = Identity();
    t.m[12] = -m[12];
    t.m[13] = -m[13];
    t.m[14] = -m[14];
    D3DXMatrixMultiply(out, &t, &r);
    return out;
}

void __cdecl eCreateAxisRotation(bMatrix4 *out, const float *axis, unsigned short angle)
{
    const float c = bCos(angle);
    const float s = bSin(angle);
    const float t = 1.0f - c;
    const float x = axis[0];
    const float y = axis[1];
    const float z = axis[2];
    const float xt = x * t;
    const float yt = y * t;
    const float xty = xt * y;
    const float zs = z * s;
    const float xtz = xt * z;
    const float ys = y * s;
    const float ytz = yt * z;
    const float xs = x * s;
    float *m = reinterpret_cast<float *>(out);
    m[0] = xt * x + c;
    m[1] = xty - zs;
    m[2] = ys + xtz;
    m[3] = 0.0f;
    m[4] = zs + xty;
    m[5] = yt * y + c;
    m[6] = ytz - xs;
    m[7] = 0.0f;
    m[8] = xtz - ys;
    m[9] = xs + ytz;
    m[10] = z * z * t + c;
    m[11] = 0.0f;
    m[12] = m[13] = m[14] = 0.0f;
    m[15] = 1.0f;
}

void __cdecl eMathInitIdentity()
{
    memset(reinterpret_cast<void *>(0x9880a0), 0, sizeof(Matrix));
    Matrix identity = Identity();
    memcpy(reinterpret_cast<void *>(0x987ab0), &identity, sizeof(identity));
}