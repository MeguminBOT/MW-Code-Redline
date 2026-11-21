/**
 * A car's straight-line performance from its physics collections (gameplay/vehicles/performance.h). Not in the original game.
 *
 * The model is tools/perfmodel.py's (which fits its constants to the test track's runs): engine speed from road speed
 * through the gear; torque from the curve (lb-ft, points spread evenly from IDLE to MAX_RPM, Physics::Info::MaxTorque)
 * times 1 + the forced induction's boost (Physics::Info::InductionBoost), through gear, final drive and gear
 * efficiency to the driven wheels, no more than their tyres hold; drag DRAG_COEFFICIENT x v^2 (PerfStats::Fetch
 * 0x678ce0); no drive while a shift takes SHIFT_SPEED. The collections are the pvehicle's first references (the ones
 * the upgrades and junkman parts change), read as Attrib instances.
 *
 * The model gets top speeds right but not launches (the game's clutch, tyre slip and torque converter are not in it),
 * so Measured() starts from the test track's times for the car as it comes (GLOBAL\PERFORMANCE.TXT, or
 * PERFORMANCE_REAL.TXT with realistic car performance (gameplay/vehicles/realism.h), written by tools/testtrack.py: a pvehicle
 * name and its top speed in km/h, then its times to 60 mph, 100 km/h, 100 mph and 200 km/h and over the quarter
 * mile) and scales them by how the model's figures for the car as configured differ from its figures for the stock
 * car. A top speed the gearing stops at the redline (or the limiter) is that speed: the measured run only corrects the
 * model's power (a second run with no redline in top gear), so a car the model overrates stock, like the Ford GT, no
 * longer shows less when fully upgraded than stock.
 */
#include <math.h>
#include <windows.h>

#include "game/gen/api.h"
#include "core/config.h"
#include "core/log.h"
#include "gameplay/vehicles/performance.h"
#include "gameplay/vehicles/realism.h"
#include "engine/resources/pack.h"

inline void *operator new(size_t, void *where)
{
    return where;
}

namespace
{
/** pvehicle attributes (Attrib::StringToKey of the names) and layout offsets. */
const unsigned int kEngine = 0xF1F5FBC7;
const unsigned int kInduction = 0xC92A0142;
const unsigned int kTransmission = 0x07A7A3E5;
const unsigned int kChassis = 0xAFA210F0;
const unsigned int kTires = 0xBD38D1CA;
const int kMass = 0x38;
/** engine: TORQUE (array), SPEED_LIMITER (array, mph), MAX_RPM, RED_LINE, IDLE. */
const int kTorque = 0x00;
const int kSpeedLimiter = 0x2C;
const int kMaxRpm = 0x54;
const int kRedLine = 0x58;
const int kIdle = 0x5C;
/** induction: LOW_BOOST, SPOOL_TIME_DOWN, VACUUM, SPOOL, SPOOL_TIME_UP, HIGH_BOOST. */
const int kLowBoost = 0x00;
const int kSpoolTimeDown = 0x04;
const int kVacuum = 0x08;
const int kSpool = 0x0C;
const int kSpoolTimeUp = 0x10;
const int kHighBoost = 0x18;
/** transmission: GEAR_RATIO (array: reverse, neutral, forward gears), GEAR_EFFICIENCY (array, same order),
 * TORQUE_SPLIT, SHIFT_SPEED, FINAL_GEAR. */
const int kGearRatio = 0x00;
const int kGearEfficiency = 0x40;
const int kTorqueSplit = 0x70;
const int kShiftSpeed = 0x7C;
const int kFinalGear = 0x80;
/** chassis: FRONT_WEIGHT_BIAS (percent), DRAG_COEFFICIENT. */
const int kFrontWeightBias = 0x6C;
const int kDragCoefficient = 0x70;
/** tires (front, rear pairs): GRIP_SCALE, ASPECT_RATIO, RIM_SIZE, STATIC_GRIP, SECTION_WIDTH. */
const int kYawControl = 0x00;
const int kGripScale = 0x18;
const int kDynamicGrip = 0x20;
const int kAspectRatio = 0x28;
const int kRimSize = 0x30;
const int kStaticGrip = 0x38;
const int kSectionWidth = 0x40;
/** An Attrib array: u16 room, u16 count, u16 element size, u16 flags, then the elements. */
const int kArrayCount = 2;
const int kArrayData = 8;
const int kInstanceLayout = 8;
const int kInstanceSize = 0x14;
const int kMaxGears = 8;
const int kMaxPoints = 16;
const unsigned int kPvehicleClass = 0x4A97EC8F;
/** Attrib::Instance flags: bit 0, a modified copy whose GetParent is the collection it was made from. */
const int kInstanceFlags = 0x10;
const int kMaxMeasured = 256;

const float kNmPerLbFt = 1.3558f;
const float kGravity = 9.81f;
const float kMph = 0.44704f;
const float kKmh = 1.0f / 3.6f;
const float kQuarterMile = 402.336f;
const float kStep = 1.0f / 120.0f;
const float kMaxTime = 150.0f;
/** The run's top speed: the speed stops rising by 0.5 km/h for this long (as the test track decides). */
const float kPlateauTime = 10.0f;
/** A run within this share of the gearing's cap is taken as stopped by it. */
const float kCapShare = 0.995f;

/** Fitted to the test track (tools/perfmodel.py). */
const float kGrip = 1.3026f;
const float kDriveline = 1.0202f;
const float kDrag = 0.8586f;
const float kRolling = 0.0003f;
const float kShiftScale = 1.0100f;
const float kShiftFraction = 1.0f;
const float kLaunchRpmFraction = 0.2186f;
const float kTransfer = 0.0f;
const float kSpoolScale = 0.0f;
/** The grip index (Figures::grip): the weight of each part, and the ranges Most Wanted's and Carbon's cars span. */
const float kYawWeight = 0.35f;
const float kBalanceWeight = 0.2f;
const float kSlidingWeight = 0.15f;
const float kPowerWeight = 0.3f;
const float kYawControlMax = 1.0f;
const float kBalanceMin = 1.0f;
const float kBalanceMax = 1.22f;
const float kSlidingMin = 0.81f;
const float kSlidingMax = 0.92f;
const float kOverpowerMax = 4.0f;

/** A pvehicle physics reference read as an Attrib instance (constructed in place, destroyed with it). */
class Collection
{
  public:
    Collection(const Attrib::Instance &vehicle, unsigned int key) : valid_(false)
    {
        const void *ref = vehicle.GetAttributePointer(key, 0);
        if (!ref)
            return;
        new (storage_) Attrib::Instance(*static_cast<const Attrib::RefSpec *>(ref), 0, nullptr);
        valid_ = true;
    }
    ~Collection()
    {
        if (valid_)
            reinterpret_cast<Attrib::Instance *>(storage_)->~Instance();
    }
    const unsigned char *Layout() const
    {
        return valid_ ? *reinterpret_cast<const unsigned char *const *>(storage_ + kInstanceLayout) : nullptr;
    }

  private:
    alignas(4) unsigned char storage_[kInstanceSize];
    bool valid_;
};

float Float(const unsigned char *layout, int offset)
{
    return *reinterpret_cast<const float *>(layout + offset);
}

/** The rear (1) or front (0) value of an axle pair. */
float Rear(const unsigned char *layout, int offset)
{
    return Float(layout, offset + 4);
}

float Front(const unsigned char *layout, int offset)
{
    return Float(layout, offset);
}

int ArrayCount(const unsigned char *layout, int offset)
{
    return *reinterpret_cast<const unsigned short *>(layout + offset + kArrayCount);
}

float ArrayItem(const unsigned char *layout, int offset, int index)
{
    return Float(layout, offset + kArrayData + index * 4);
}

float Ramp(float x, float a, float b)
{
    if (b <= a)
        return 0.0f;
    const float f = (x - a) / (b - a);
    return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}

struct Model
{
    float torque[kMaxPoints];
    int points;
    float idle, maxRpm, redline, limiter;
    float lowBoost, highBoost, spoolRpm, vacuum, spoolUp, spoolDown;
    float gears[kMaxGears];
    float efficiency[kMaxGears];
    int gearCount;
    float finalGear, shiftTime, frontDrive;
    float radius, drag, mass, frontLoad, frontGrip, rearGrip;
    /** No redline in top gear (the torque curve's last point past it): where power alone stops gaining on drag. */
    bool openTop;

    float Boost(float rpm) const
    {
        if (lowBoost <= 0.0f && highBoost <= 0.0f)
            return 0.0f;
        if (rpm < spoolRpm)
            return vacuum < 0.0f ? Ramp(rpm, idle, spoolRpm) * vacuum : 0.0f;
        const float f = Ramp(rpm, spoolRpm, redline);
        return f * highBoost + (1.0f - f) * lowBoost;
    }

    /** N m at the crank, full throttle, the boost times how far the turbo has spooled up (0 to 1). */
    float EngineTorque(float rpm, float spooled) const
    {
        float x = (rpm - idle) / (maxRpm - idle) * static_cast<float>(points - 1);
        if (x < 0.0f)
            x = 0.0f;
        if (x > static_cast<float>(points - 1))
            x = static_cast<float>(points - 1);
        int i = static_cast<int>(x);
        if (i > points - 2)
            i = points - 2;
        const float f = x - static_cast<float>(i);
        const float boost = Boost(rpm);
        const float factor = 1.0f + (boost > 0.0f ? boost * spooled : boost);
        return (torque[i] * (1.0f - f) + torque[i + 1] * f) * kNmPerLbFt * factor;
    }

    float Rpm(float speed, int gear) const
    {
        return speed / radius * gears[gear] * finalGear * 60.0f / (2.0f * 3.14159265f);
    }

    /** The force the engine puts on the road in a gear (before the tyres' limit); 0 past the redline. */
    float WheelForce(float speed, int gear, float spooled, float &rpm) const
    {
        rpm = Rpm(speed, gear);
        const float floor = gear == 0 ? kLaunchRpmFraction * redline : idle;
        if (rpm < floor)
            rpm = floor;
        if (rpm > redline && !(openTop && gear == gearCount - 1))
            return 0.0f;
        return EngineTorque(rpm, spooled) * gears[gear] * finalGear * efficiency[gear] * kDriveline / radius;
    }

    /** The most the driven wheels can push: their axle's load (static, plus the share acceleration moves to the
     * rear) times grip. */
    float Traction(float accel) const
    {
        const float moved = kTransfer * mass * accel;
        float front = mass * kGravity * frontLoad - moved;
        if (front < 0.0f)
            front = 0.0f;
        const float rear = mass * kGravity * (1.0f - frontLoad) + moved;
        if (frontDrive <= 0.0f)
            return rear * rearGrip * kGrip;
        if (frontDrive >= 1.0f)
            return front * frontGrip * kGrip;
        return (front * frontGrip + rear * rearGrip) * kGrip;
    }
};

/**
 * How a car corners, 0 (it slides: drifting) to 1 (it grips): the tyres' YAW_CONTROL (the yaw damping the game adds,
 * low on the muscle cars), the rear tyres' grip against the front's (more at the rear keeps the tail in line), the
 * share of its grip the rear keeps once it slides (DYNAMIC_GRIP of STATIC_GRIP), and how far the engine overpowers
 * the rear tyres in first gear (none for front-wheel drive).
 */
float GripIndex(const Model &m, const unsigned char *tires, float peakTorque)
{
    const int count = ArrayCount(tires, kYawControl);
    float yaw = 0.0f;
    for (int i = 0; i < count; ++i)
        yaw += ArrayItem(tires, kYawControl, i);
    if (count)
        yaw /= static_cast<float>(count);
    const float balance = m.frontGrip > 0.0f ? m.rearGrip / m.frontGrip : 1.0f;
    const float staticRear = Rear(tires, kStaticGrip);
    const float sliding = staticRear > 0.0f ? Rear(tires, kDynamicGrip) / staticRear : 1.0f;
    const float push = peakTorque * m.gears[0] * m.finalGear / m.radius * (1.0f - m.frontDrive);
    const float hold = m.mass * kGravity * (1.0f - m.frontLoad) * m.rearGrip;
    const float overpower = hold > 0.0f ? push / hold : 0.0f;
    return kYawWeight * Ramp(yaw, 0.0f, kYawControlMax) + kBalanceWeight * Ramp(balance, kBalanceMin, kBalanceMax) +
           kSlidingWeight * Ramp(sliding, kSlidingMin, kSlidingMax) +
           kPowerWeight * (1.0f - Ramp(overpower, 0.0f, kOverpowerMax));
}

bool Build(const Attrib::Instance &vehicle, Model &m, nfs::performance::Figures &out)
{
    Collection engine(vehicle, kEngine), induction(vehicle, kInduction), transmission(vehicle, kTransmission),
        chassis(vehicle, kChassis), tires(vehicle, kTires);
    const unsigned char *e = engine.Layout();
    const unsigned char *t = transmission.Layout();
    const unsigned char *c = chassis.Layout();
    const unsigned char *w = tires.Layout();
    const unsigned char *v = *reinterpret_cast<const unsigned char *const *>(
        reinterpret_cast<const unsigned char *>(&vehicle) + kInstanceLayout);
    if (!e || !t || !c || !w || !v)
        return false;
    m.points = ArrayCount(e, kTorque);
    if (m.points < 2 || m.points > kMaxPoints)
        return false;
    for (int i = 0; i < m.points; ++i)
        m.torque[i] = ArrayItem(e, kTorque, i);
    m.idle = Float(e, kIdle);
    m.maxRpm = Float(e, kMaxRpm);
    m.redline = Float(e, kRedLine);
    // EngineRacer::DoECU limits only with both the speed and the ramp over which it takes the throttle off.
    m.limiter = ArrayCount(e, kSpeedLimiter) >= 2 && ArrayItem(e, kSpeedLimiter, 1) > 0.0f
                    ? ArrayItem(e, kSpeedLimiter, 0) * kMph
                    : 0.0f;
    if (m.maxRpm <= m.idle || m.redline <= m.idle)
        return false;
    const unsigned char *in = induction.Layout();
    m.lowBoost = in ? Float(in, kLowBoost) : 0.0f;
    m.highBoost = in ? Float(in, kHighBoost) : 0.0f;
    m.vacuum = in ? Float(in, kVacuum) : 0.0f;
    m.spoolRpm = m.idle + (m.redline - m.idle) * (in ? Float(in, kSpool) : 0.0f);
    m.spoolUp = in ? Float(in, kSpoolTimeUp) * kSpoolScale : 0.0f;
    m.spoolDown = in ? Float(in, kSpoolTimeDown) * kSpoolScale : 0.0f;

    const int ratios = ArrayCount(t, kGearRatio);
    const int efficiencies = ArrayCount(t, kGearEfficiency);
    m.gearCount = 0;
    for (int i = 2; i < ratios && m.gearCount < kMaxGears; ++i)
    {
        const float r = ArrayItem(t, kGearRatio, i);
        if (r <= 0.0f)
            break;
        m.gears[m.gearCount] = r;
        m.efficiency[m.gearCount] = i < efficiencies ? ArrayItem(t, kGearEfficiency, i) : 1.0f;
        ++m.gearCount;
    }
    if (!m.gearCount)
        return false;
    m.finalGear = Float(t, kFinalGear);
    m.shiftTime = Float(t, kShiftSpeed);
    m.frontDrive = Float(t, kTorqueSplit);
    m.radius = (Rear(w, kRimSize) * 0.0254f + Rear(w, kSectionWidth) * 0.01f * Rear(w, kAspectRatio) * 0.002f) * 0.5f;
    m.drag = Float(c, kDragCoefficient) * kDrag;
    m.mass = Float(v, kMass);
    if (m.radius <= 0.0f || m.mass <= 0.0f || m.finalGear <= 0.0f)
        return false;
    m.frontLoad = Float(c, kFrontWeightBias) * 0.01f;
    m.frontGrip = Front(w, kStaticGrip) * Front(w, kGripScale);
    m.rearGrip = Rear(w, kStaticGrip) * Rear(w, kGripScale);

    out.mass = m.mass;
    out.gears = m.gearCount;
    out.frontDrive = m.frontDrive;
    out.forcedInduction = m.lowBoost > 0.0f || m.highBoost > 0.0f;
    out.peakPower = out.peakTorque = 0.0f;
    for (float rpm = m.idle; rpm <= m.redline; rpm += 50.0f)
    {
        const float tq = m.EngineTorque(rpm, 1.0f);
        if (tq > out.peakTorque)
        {
            out.peakTorque = tq;
            out.peakTorqueRpm = rpm;
        }
        const float p = tq * rpm * (2.0f * 3.14159265f / 60.0f);
        if (p > out.peakPower)
        {
            out.peakPower = p;
            out.peakPowerRpm = rpm;
        }
    }
    out.grip = GripIndex(m, w, out.peakTorque);
    out.capSpeed = m.redline / 60.0f * 2.0f * 3.14159265f * m.radius / (m.gears[m.gearCount - 1] * m.finalGear);
    if (m.limiter > 0.0f && m.limiter < out.capSpeed)
        out.capSpeed = m.limiter;
    m.openTop = false;
    return true;
}

void Mark(float &field, float t, float v, float next, float mark)
{
    if (field < 0.0f && v < mark && next >= mark)
        field = t + kStep * (mark - v) / (next - v);
}
struct MeasuredCar
{
    unsigned int key;
    float top, zeroTo60, zeroTo100Kmh, zeroTo100Mph, zeroTo200, quarter;
};

MeasuredCar g_measured[kMaxMeasured];
int g_measuredCount = -1;

/** "12.34" to a float; bStrToFloat needs the game's locale tables set up, these files are plain. */
float ParseNumber(const char *&p)
{
    while (*p == ' ' || *p == '\t')
        ++p;
    float value = 0.0f, scale = 0.0f;
    bool negative = *p == '-';
    if (negative)
        ++p;
    for (; (*p >= '0' && *p <= '9') || *p == '.'; ++p)
    {
        if (*p == '.')
            scale = 1.0f;
        else
        {
            value = value * 10.0f + static_cast<float>(*p - '0');
            if (scale > 0.0f)
                scale *= 10.0f;
        }
    }
    if (scale > 0.0f)
        value /= scale;
    return negative ? -value : value;
}

void LoadMeasured()
{
    g_measuredCount = 0;
    const char *path = nfs::realism::Active() ? "GLOBAL\\PERFORMANCE_REAL.TXT" : "GLOBAL\\PERFORMANCE.TXT";
    uint32_t read = 0;
    char *text = reinterpret_cast<char *>(nfs::pack::ReadFile(path, read));
    if (!text)
        return;
    for (char *line = text; *line && g_measuredCount < kMaxMeasured;)
    {
        char *end = line;
        while (*end && *end != '\n')
            ++end;
        const char saved = *end;
        *end = 0;
        char name[64];
        int n = 0;
        const char *p = line;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && n < 63)
            name[n++] = *p++;
        name[n] = 0;
        if (n && name[0] != '#')
        {
            MeasuredCar &car = g_measured[g_measuredCount++];
            car.key = Attrib::StringToKey(name);
            car.top = ParseNumber(p) / 3.6f;
            car.zeroTo60 = ParseNumber(p);
            car.zeroTo100Kmh = ParseNumber(p);
            car.zeroTo100Mph = ParseNumber(p);
            car.zeroTo200 = ParseNumber(p);
            car.quarter = ParseNumber(p);
        }
        *end = saved;
        line = *end ? end + 1 : end;
    }
    ::operator delete(text);
    nfs::Log("performance: %d measured cars from %s", g_measuredCount, path);
}

const MeasuredCar *FindMeasured(unsigned int key)
{
    if (g_measuredCount < 0)
        LoadMeasured();
    for (int i = 0; i < g_measuredCount; ++i)
        if (g_measured[i].key == key)
            return &g_measured[i];
    return nullptr;
}

/** The measured time scaled by the model's change; the model's own when either is missing. */
float Scaled(float measured, float model, float modelStock)
{
    if (measured <= 0.0f || model <= 0.0f || modelStock <= 0.0f)
        return model;
    return measured * model / modelStock;
}
} // namespace

namespace nfs::performance
{
/** A full-throttle run from a standstill: its top speed, and the times into marks when given. */
float Run(const Model &m, Figures *marks)
{
    Figures unused;
    Figures &out = marks ? *marks : unused;
    out.zeroTo60Mph = out.zeroTo100Kmh = out.zeroTo100Mph = out.zeroTo200Kmh = out.quarterMile = -1.0f;
    float v = 0.0f, x = 0.0f, t = 0.0f, shifting = 0.0f, top = 0.0f, mark = 0.0f, topAt = 0.0f, accel = 0.0f;
    float spooled = m.spoolUp > 0.0f ? 0.0f : 1.0f;
    int gear = 0;
    while (t < kMaxTime)
    {
        float force = 0.0f;
        if (shifting > 0.0f)
        {
            shifting -= kStep;
            if (m.spoolDown > 0.0f)
                spooled = spooled > kStep / m.spoolDown ? spooled - kStep / m.spoolDown : 0.0f;
        }
        else
        {
            spooled = m.spoolUp > 0.0f ? (spooled + kStep / m.spoolUp < 1.0f ? spooled + kStep / m.spoolUp : 1.0f)
                                       : 1.0f;
            float rpm;
            force = m.WheelForce(v, gear, spooled, rpm);
            const float traction = m.Traction(accel);
            if (force > traction)
                force = traction;
            if (gear + 1 < m.gearCount)
            {
                float nextRpm;
                float next = m.WheelForce(v, gear + 1, spooled, nextRpm);
                if (next > traction)
                    next = traction;
                if (rpm >= m.redline * kShiftFraction || next > force)
                {
                    ++gear;
                    shifting = m.shiftTime * kShiftScale;
                }
            }
        }
        accel = (force - m.drag * v * v - kRolling * m.mass * kGravity) / m.mass;
        float next = v + accel * kStep;
        if (next < 0.0f)
            next = 0.0f;
        if (m.limiter > 0.0f && next > m.limiter)
            next = m.limiter;
        Mark(out.zeroTo60Mph, t, v, next, 60.0f * kMph);
        Mark(out.zeroTo100Kmh, t, v, next, 100.0f * kKmh);
        Mark(out.zeroTo100Mph, t, v, next, 100.0f * kMph);
        Mark(out.zeroTo200Kmh, t, v, next, 200.0f * kKmh);
        const float nextX = x + (v + next) * 0.5f * kStep;
        if (out.quarterMile < 0.0f && x < kQuarterMile && nextX >= kQuarterMile)
            out.quarterMile = t + kStep * (kQuarterMile - x) / (nextX - x);
        v = next;
        x = nextX;
        t += kStep;
        if (v > top)
            top = v;
        if (v > mark + 0.5f * kKmh)
        {
            mark = v;
            topAt = t;
        }
        if (t - topAt > kPlateauTime && t > 20.0f)
            break;
    }
    return top;
}

bool Estimate(const Attrib::Instance &vehicle, Figures &out)
{
    Model m;
    if (!Build(vehicle, m, out))
        return false;
    out.topSpeed = Run(m, &out);
    m.openTop = true;
    out.powerSpeed = out.topSpeed < out.capSpeed * kCapShare ? out.topSpeed : Run(m, nullptr);
    return true;
}

bool Measured(const Attrib::Instance &vehicle, Figures &out)
{
    if (!Estimate(vehicle, out))
        return false;
    const unsigned char *self = reinterpret_cast<const unsigned char *>(&vehicle);
    const unsigned int key = (*reinterpret_cast<const unsigned int *>(self + kInstanceFlags) & 1) ? vehicle.GetParent()
                                                                                                  : vehicle.GetCollection();
    const MeasuredCar *car = FindMeasured(key);
    if (!car)
        return true;
    alignas(4) unsigned char storage[kInstanceSize];
    new (storage) Attrib::Gen::pvehicle(key, 0, nullptr);
    Figures stock;
    const bool have = Estimate(*reinterpret_cast<const Attrib::Instance *>(storage), stock);
    reinterpret_cast<Attrib::Instance *>(storage)->~Instance();
    if (!have)
        return true;
    // A stock car the model's power stops short of its redline says by its measured top speed how far the model's
    // power is off for it; the gearing's cap is exact, so a configuration geared to stop at its redline shows that
    // speed, or what the corrected power reaches below it.
    const bool stockCapped = stock.topSpeed >= stock.capSpeed * kCapShare;
    const bool capped = out.topSpeed >= out.capSpeed * kCapShare;
    const bool same = out.topSpeed == stock.topSpeed && out.capSpeed == stock.capSpeed;
    if (car->top > 0.0f && stock.topSpeed > 0.0f && capped && !same)
    {
        const float top = stockCapped ? out.powerSpeed : out.powerSpeed * car->top / stock.topSpeed;
        out.topSpeed = top < out.capSpeed ? top : out.capSpeed;
    }
    else
        out.topSpeed = Scaled(car->top, out.topSpeed, stock.topSpeed);
    out.zeroTo60Mph = Scaled(car->zeroTo60, out.zeroTo60Mph, stock.zeroTo60Mph);
    out.zeroTo100Kmh = Scaled(car->zeroTo100Kmh, out.zeroTo100Kmh, stock.zeroTo100Kmh);
    out.zeroTo100Mph = Scaled(car->zeroTo100Mph, out.zeroTo100Mph, stock.zeroTo100Mph);
    out.zeroTo200Kmh = Scaled(car->zeroTo200, out.zeroTo200Kmh, stock.zeroTo200Kmh);
    out.quarterMile = Scaled(car->quarter, out.quarterMile, stock.quarterMile);
    return true;
}

void Reset()
{
    g_measuredCount = -1;
    ResetScale();
}

void LogAll()
{
    Attrib::Class *cls = Attrib::Database::sThis->GetClass(kPvehicleClass);
    if (!cls)
        return;
    int count = 0;
    for (unsigned int key = cls->GetFirstCollection(); key; key = cls->GetNextCollection(key))
    {
        alignas(4) unsigned char storage[kInstanceSize];
        new (storage) Attrib::Gen::pvehicle(key, 0, nullptr);
        const Attrib::Instance &vehicle = *reinterpret_cast<const Attrib::Instance *>(storage);
        Figures f;
        if (Estimate(vehicle, f))
        {
            const int values[] = {static_cast<int>(f.topSpeed * 360.0f), static_cast<int>(f.zeroTo60Mph * 100.0f),
                                  static_cast<int>(f.zeroTo100Kmh * 100.0f), static_cast<int>(f.zeroTo200Kmh * 100.0f),
                                  static_cast<int>(f.quarterMile * 100.0f), static_cast<int>(f.peakPower / 745.7f)};
            nfs::Log("perf: %08x top %d 060 %d 0100 %d 0200 %d qm %d hp %d (x100)", key, values[0], values[1],
                     values[2], values[3], values[4], values[5] * 100);
            ++count;
        }
        reinterpret_cast<Attrib::Instance *>(storage)->~Instance();
    }
    nfs::Log("perf: %d cars", count);
}
} // namespace nfs::performance
