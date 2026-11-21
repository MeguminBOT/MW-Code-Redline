/**
 * The front-end engine's small maths and strings (original: zFEng): its name hash, its comparison, 4x4 matrices, a quaternion's
 * matrix, and the lerps its key tracks use. FEColor, FEVector2/3, FEMatrix4 and FEQuaternion are opaque in the generated header,
 * so they are read here as the arrays they are. 
 */
#include <math.h>

#include "game/gen/api.h"

namespace
{
/** FEObject's current script and FEScript's id. */
const int kObjectScript = 0x54;
const int kScriptId = 0x30;
/** What the integer lerps add before cutting off the fraction. */
const float kRound = 0.5f;
/** A quaternion's inverse length no larger than this means it was too long to be one: it becomes the identity. */
const float kLeastInverseLength = 1e-05f;

inline char Upper(char c)
{
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 0x20) : c;
}

inline int Lerped(int from, int to, float t)
{
    return static_cast<int>(static_cast<double>(to - from) * t + kRound) + from;
}

inline float *Floats(void *object)
{
    return static_cast<float *>(object);
}

inline const float *Floats(const void *object)
{
    return static_cast<const float *>(object);
}

inline int *Ints(void *object)
{
    return static_cast<int *>(object);
}
} // namespace

unsigned int __cdecl FEHashUpper(const char *text)
{
    unsigned int hash = 0xffffffffu;
    if (text)
        for (; *text; ++text)
            hash = hash * 33 + static_cast<unsigned char>(Upper(*text));
    return hash;
}

int __cdecl FEStricmp(const char *a, const char *b)
{
    char x, y;
    do
    {
        x = Upper(*a++);
        y = Upper(*b++);
    } while (x && x == y);
    return static_cast<int>(x) - static_cast<int>(y);
}

void __cdecl FELerpInteger(int from, int to, float t, int *add, int *out)
{
    *out = Lerped(from, to, t) + *add;
}

void __cdecl FELerpVector2(FEVector2 &from, FEVector2 &to, float t, FEVector2 *add, FEVector2 *out)
{
    const float *a = Floats(&from), *b = Floats(&to), *c = Floats(add);
    float *o = Floats(out);
    for (int i = 0; i < 2; ++i)
        o[i] = (b[i] - a[i]) * t + a[i] + c[i];
}

void __cdecl FELerpVector3(FEVector3 &from, FEVector3 &to, float t, FEVector3 *add, FEVector3 *out)
{
    const float *a = Floats(&from), *b = Floats(&to), *c = Floats(add);
    float *o = Floats(out);
    for (int i = 0; i < 3; ++i)
        o[i] = (b[i] - a[i]) * t + a[i] + c[i];
}

void __cdecl FELerpColor(FEColor &from, FEColor &to, float t, FEColor *add, FEColor *out)
{
    const int *a = Ints(&from), *b = Ints(&to), *c = Ints(add);
    int *o = Ints(out);
    for (int i = 0; i < 4; ++i)
        o[i] = Lerped(a[i], b[i], t) + c[i];
}

void FEMatrix4::Identify()
{
    float *m = Floats(this);
    for (int i = 0; i < 16; ++i)
        m[i] = i % 5 == 0 ? 1.0f : 0.0f;
}

void __cdecl FEMultMatrix(FEVector3 *out, const FEMatrix4 *matrix, const FEVector3 *in)
{
    const float *m = Floats(matrix), *v = Floats(in);
    const float x = v[0], y = v[1], z = v[2];
    float *o = Floats(out);
    o[0] = m[0] * x + m[4] * y + m[8] * z + m[12];
    o[1] = m[1] * x + m[5] * y + m[9] * z + m[13];
    o[2] = m[2] * x + m[6] * y + m[10] * z + m[14];
}

void __cdecl FEMultMatrix(FEMatrix4 *out, const FEMatrix4 *left, const FEMatrix4 *right)
{
    const float *a = Floats(left), *b = Floats(right);
    float r[16];
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            r[row * 4 + column] = b[row * 4] * a[column] + b[row * 4 + 1] * a[4 + column] + b[row * 4 + 2] * a[8 + column] +
                                  b[row * 4 + 3] * a[12 + column];
    float *o = Floats(out);
    for (int i = 0; i < 16; ++i)
        o[i] = r[i];
}

void FEQuaternion::GetMatrix(FEMatrix4 *out)
{
    const float *q = Floats(this);
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float x2 = x + x, y2 = y + y, z2 = z + z;
    const float xx = x2 * x, xw = x2 * w;
    float *m = Floats(out);
    m[0] = 1.0f - (z2 * z + y2 * y);
    m[1] = z2 * w + y2 * x;
    m[2] = z2 * x - y2 * w;
    m[3] = 0.0f;
    m[4] = y2 * x - z2 * w;
    m[5] = 1.0f - (z2 * z + xx);
    m[6] = xw + z2 * y;
    m[7] = 0.0f;
    m[8] = y2 * w + z2 * x;
    m[9] = z2 * y - xw;
    m[10] = 1.0f - (y2 * y + xx);
    m[11] = 0.0f;
    m[12] = 0.0f;
    m[13] = 0.0f;
    m[14] = 0.0f;
    m[15] = 1.0f;
}

void __cdecl NormalizeQuaternion(FEQuaternion &quaternion)
{
    float *q = Floats(&quaternion);
    const float inverse = 1.0f / sqrtf(q[3] * q[3] + q[2] * q[2] + q[1] * q[1] + q[0] * q[0]);
    if (inverse > kLeastInverseLength)
    {
        for (int i = 0; i < 4; ++i)
            q[i] *= inverse;
        return;
    }
    q[0] = q[1] = q[2] = 0.0f;
    q[3] = 1.0f;
}

bool __cdecl FEngIsScriptSet(FEObject *object, unsigned int id)
{
    if (!object)
        return false;
    const char *script = *reinterpret_cast<const char *const *>(reinterpret_cast<const char *>(object) + kObjectScript);
    return script && *reinterpret_cast<const unsigned int *>(script + kScriptId) == id;
}
