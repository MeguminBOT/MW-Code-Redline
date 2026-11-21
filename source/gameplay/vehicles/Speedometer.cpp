/** The speed the speedometer shows, from engine speed, gearing and tyre size (original: zPhysics
 * Physics::Info::Speedometer, zPhysicsBehaviors EngineRacer::GetSpeedometer, GetMaxSpeedometer). */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x692640, EngineRacerGetSpeedometer)
NFS_REPLACE(0x692580, EngineRacerGetMaxSpeedometer)

namespace
{
const float kMphToMetres = 0.44703f;
const float kRpmToRadians = 0.10471976f;
const float kRadiansToRpm = 9.549296f;

const int kLayout = 8;
const int kFinalGear = 0x80;
const int kRedline = 0x58;
const int kIdle = 0x5c;
const int kSpeedLimiter = 0x2c;
const int kSpeedLimiterValue = 0x34;
const int kFrontWidth = 0x28;
const int kRearWidth = 0x2c;
const int kFrontRim = 0x30;
const int kRearRim = 0x34;
const int kFrontAspect = 0x40;
const int kRearAspect = 0x44;

const int kEngineSpeed = 0xa8;
const int kGear = 0x38;
const int kEngine = 0xf8;
const int kTransmission = 0x10c;
const int kTires = 0x120;
const int kTunings = 0xcc;

template <typename T> T &At(const void *base, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(base) + offset));
}

const char *Layout(const void *attributes)
{
    return At<const char *>(attributes, kLayout);
}

unsigned int Length(const void *array)
{
    return NFS_ORIGINAL(0x452940, unsigned int(__thiscall *)(const void *))(array);
}

const float *Element(const char *array, unsigned int index)
{
    if (index < Length(array))
        return reinterpret_cast<const float *>(array + 8) + index;
    return static_cast<const float *>(NFS_ORIGINAL(0x6269b0, void *(__cdecl *)(int))(4));
}

float TyreRadius(const char *tires, int width, int aspect, int rim)
{
    return (At<float>(tires, width) * 0.01f * At<float>(tires, aspect) * 0.002f + At<float>(tires, rim) * 0.0254f) *
           0.5f;
}

float Limiter(const char *engine)
{
    const char *limiter = engine + kSpeedLimiter;
    const float *value = Length(limiter)
                             ? reinterpret_cast<const float *>(engine + kSpeedLimiterValue)
                             : static_cast<const float *>(NFS_ORIGINAL(0x6269b0, void *(__cdecl *)(int))(4));
    return *value * kMphToMetres;
}

const Physics::Tunings *Tunings(void *engine)
{
    void *owner = At<void *>(engine, -4);
    using Get = const Physics::Tunings *(__thiscall *)(void *);
    return reinterpret_cast<Get>((*reinterpret_cast<void ***>(owner))[kTunings / 4])(owner);
}

float Gauge(char *engine, float rpm, int gear)
{
    return Physics::Info::Speedometer(*reinterpret_cast<const Attrib::Gen::transmission *>(engine + kTransmission),
                                      *reinterpret_cast<const Attrib::Gen::engine *>(engine + kEngine),
                                      *reinterpret_cast<const Attrib::Gen::tires *>(engine + kTires), rpm,
                                      static_cast<GearID>(gear), Tunings(engine));
}
} // namespace

float Physics::Info::Speedometer(const Attrib::Gen::transmission &transmission, const Attrib::Gen::engine &engine,
                                 const Attrib::Gen::tires &tires, float rpm, GearID gear, const Physics::Tunings *)
{
    const char *gearing = Layout(&transmission);
    const float ratio = At<float>(gearing, kFinalGear) * *Element(gearing, static_cast<unsigned int>(gear));
    const char *motor = Layout(&engine);
    const float range = At<float>(motor, kRedline) - At<float>(motor, kIdle);
    float speed = 0.0f;
    const float absRatio = ratio < 0.0f ? -ratio : ratio;
    if (absRatio > 0.0f && range > 0.0f)
    {
        const char *tyres = Layout(&tires);
        const float radius = (TyreRadius(tyres, kRearWidth, kRearAspect, kRearRim) +
                              TyreRadius(tyres, kFrontWidth, kFrontAspect, kFrontRim)) *
                             0.5f;
        speed =
            (rpm - At<float>(motor, kIdle)) / absRatio / range * At<float>(motor, kRedline) * radius * kRpmToRadians;
    }
    const float limit = Limiter(motor);
    if (limit > 0.0f && speed >= limit)
        return limit;
    return speed;
}

float __fastcall EngineRacerGetSpeedometer(void *engine)
{
    char *self = static_cast<char *>(engine);
    return Gauge(self, At<float>(self, kEngineSpeed) * kRadiansToRpm, At<int>(self, kGear));
}

float __fastcall EngineRacerGetMaxSpeedometer(void *engine)
{
    char *self = static_cast<char *>(engine);
    const unsigned int gears = Length(Layout(self + kTransmission));
    if (!gears)
        return 0.0f;
    const char *motor = Layout(self + kEngine);
    const float limit = Limiter(motor);
    const float speed = Gauge(self, At<float>(motor, kRedline), static_cast<int>(gears - 1));
    if (limit > 0.0f && speed >= limit)
        return limit;
    return speed;
}
