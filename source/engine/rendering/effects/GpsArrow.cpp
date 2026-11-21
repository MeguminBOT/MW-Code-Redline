/** The GPS arrow drawn ahead of the camera (original: zAI Gps::Render, RenderGpsArrows). */
#include <math.h>

#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
const float kTwoPi = 6.2831855f;
const float kPi = 3.1415927f;
const float kPulseSpeed = 1.5f;
const float kArrowSize = 0.2f;

inline float Sin(float angle)
{
    float result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}

inline float Cos(float angle)
{
    float result;
    __asm {
        fld angle
        fcos
        fstp result
    }
    return result;
}

template <typename T> T &At(const void *base, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(base) + offset));
}

const int kModel = 0x64;
const int kOrigin = 0x58;
const int kDirection = 0x68;
const int kState = 0x74;
const int kHeadingSet = 0x364;
const int kHeading = 0x368;
const int kPulse = 0x36c;
const int kPulseRate = 0x374;
const int kStateActive = 1;

const int kViewActive = 4;
const int kViewMovers = 0x44;
const int kCameraFov = 0xc4;
const int kCameraPosition = 0x200;
const int kProfileWideArrow = 0x39;

bVector3 Vec(float x, float y, float z)
{
    bVector3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

void Scale(bVector3 &v, float s)
{
    v.x *= s;
    v.y *= s;
    v.z *= s;
}

void SetRow(bVector4 &row, const bVector3 &v, float w)
{
    row.x = v.x;
    row.y = v.y;
    row.z = v.z;
    row.w = w;
}

void *ActiveCamera(eView *view)
{
    char *head = reinterpret_cast<char *>(view) + kViewMovers;
    char *node = *reinterpret_cast<char **>(head);
    if (node == head || !node)
        return nullptr;
    char *mover = node - 4;
    if (!mover || At<int>(mover, 0xc) != 1)
        return nullptr;
    return At<void *>(mover, 0x1c);
}

void AdvancePulse(char *gps, int state)
{
    float &pulse = At<float>(gps, kPulse);
    if (state == kStateActive)
    {
        pulse = (At<float>(gps, kPulseRate) * 2.0f + kPulseSpeed) * RealTimeElapsed + pulse;
        pulse = UMath::Mod(pulse, 1.0f);
    }
    else if (pulse > 0.0f)
        pulse = UMath::Max(pulse - RealTimeElapsed * kPulseSpeed, 0.0f);
}

void TurnTowards(char *gps, float target)
{
    float &heading = At<float>(gps, kHeading);
    if (!At<bool>(gps, kHeadingSet))
    {
        heading = target;
        return;
    }
    const float apart = UMath::Abs(heading - target);
    const float shortest = UMath::Min(apart, UMath::Abs(apart - kTwoPi));
    const float step = (UMath::Ramp(shortest, 0.0f, kPi) * 0.875f + 0.125f) * RealTimeElapsed * kTwoPi;
    float signedStep = step;
    if (heading > target)
        signedStep = -signedStep;
    if (apart > kPi)
        signedStep = -signedStep;
    if (shortest > step)
        heading += signedStep;
    else
        heading = target;
    if (heading > kPi)
        heading -= kTwoPi;
    else if (heading < -kPi)
        heading += kTwoPi;
}
} // namespace

void Gps::Render(eView *view)
{
    char *gps = reinterpret_cast<char *>(this);
    if (At<int>(view, kViewActive) != 1 || *reinterpret_cast<void *const *>(0x9885c8) || FEManager::mPauseRequest > 0)
        return;
    const int state = At<int>(gps, kState);
    if (!state)
        return;
    void *camera = ActiveCamera(view);
    if (!camera)
        return;

    UMath::Vector3 origin = At<UMath::Vector3>(gps, kOrigin);
    const float distance = bLength(reinterpret_cast<const bVector3 *>(static_cast<char *>(camera) + kCameraPosition));
    AdvancePulse(gps, state);
    const float pulse = Sin(At<float>(gps, kPulse) * kTwoPi) * 0.1f + 1.0f;
    UMath::ScaleAdd(At<UMath::Vector3>(gps, kDirection), distance, origin, origin);
    const bVector3 target = Vec(origin.z, -origin.x, origin.y);

    bMatrix4 inverse;
    eInvertTransformationMatrix(&inverse, static_cast<const bMatrix4 *>(camera));
    unsigned short halfFov = static_cast<unsigned short>(At<unsigned short>(camera, kCameraFov) >> 1);
    if (Sim::GetUserMode() == 1)
        halfFov = static_cast<unsigned short>(halfFov >> 1);
    float wide = 1.0f;
    if (eGetCurrentViewMode() == 2 && At<char>(FEDatabase->CurrentUserProfile, kProfileWideArrow))
        wide = 1.5f;
    const float spread = bCos(halfFov) * 2.0f;
    float sine;
    float cosine;
    bSinCos(&sine, &cosine,
            static_cast<unsigned short>(static_cast<int>(static_cast<float>(static_cast<short>(halfFov)) * -0.5f)));
    bVector3 position = Vec(0.0f, sine * spread, cosine * spread * wide);
    bMulMatrix(&position, &inverse, &position);

    bVector3 toward = Vec(position.x - target.x, position.y - target.y, 0.0f);
    bNormalize(&toward, &toward);
    TurnTowards(gps, UMath::Atan2r(toward.y, toward.x));

    const float heading = At<float>(gps, kHeading) * 1.0f;
    bVector3 forward = Vec(Cos(heading) * kArrowSize, Sin(heading) * kArrowSize, 0.0f);
    bVector3 up = Vec(0.0f, 0.0f, 1.0f);
    bVector3 side;
    bCross(&side, &up, &forward);
    bNormalize(&side, &side, kArrowSize);
    bCross(&up, &forward, &side);
    bNormalize(&up, &up, kArrowSize);

    bMatrix4 *matrix = eFrameMallocMatrix(1);
    if (!matrix)
        return;
    Scale(side, pulse);
    Scale(up, pulse);
    Scale(forward, pulse);
    SetRow(matrix->v0, side, 0.0f);
    SetRow(matrix->v1, up, 0.0f);
    SetRow(matrix->v2, forward, 0.0f);
    SetRow(matrix->v3, position, 1.0f);
    reinterpret_cast<eViewPlatInterface *>(view)->Render(At<eModel *>(gps, kModel), matrix, nullptr, 0, nullptr);
    At<bool>(gps, kHeadingSet) = true;
}

void RenderGpsArrows(eView *view)
{
    if (Gps *gps = *reinterpret_cast<Gps *const *>(0x90d8e4))
        gps->Render(view);
}
