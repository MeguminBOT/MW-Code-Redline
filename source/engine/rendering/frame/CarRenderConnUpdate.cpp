/**
 * Per-frame pieces of a car's render connection (original: zWorld CarRenderConn::UpdateParts 0x745bc0, Hide 0x747510,
 * UpdateSteering 0x7459f0, BuildRenderMatrix 0x739250, AddRoadNoise 0x7390b0, UpdateRoadNoise 0x745c70,
 * UpdateRenderMatrix 0x747050, UpdateBodyAnimation 0x746440, UpdateEngineAnimation 0x745f20, Update 0x7564c0,
 * UpdateEffects 0x7561c0, UpdateTires 0x746750; VehicleRenderConn::Effect::Fire 0x744980, Update 0x744a50; HeliRenderConn::Update 0x739420).
 *
 * CarRenderConn: +0x14 its attribute instance, +0x34 the car's matrix, +0x38 its position, +0x44 the CarRenderInfo,
 * +0x50 the render offset, +0x60 skids to restart, +0x74 the four tyre states (SkidMaker at +0x5c), +0x330 last frame's
 * matrix, +0x3a0 wheel spin, +0x3bc the hidden-part bits (3 words), +0x3c8/+0x3cc/+0x3d0 the steering-animation limits,
 * +0x3dc/+0x3e0 the drawn front wheel angles, +0x3e4/+0x3ec the two effect lists, +0x3f8 flags (bit 0 hidden),
 * +0x390/+0x394 the road noise's roll and pitch, +0x398 the body's lift from it (by +0x90/+0xc0 and +0x94/+0xa4).
 * +0x130/+0x230 the wheels' two matrix sets (4 each, car space), +0x370..+0x3b0 the body's animated angles (pitch:
 * +0x370 +0x378 +0x384 +0x3a8 +0x3ac, roll: +0x374 +0x37c +0x388 +0x3a4 +0x3b0) and lifts (+0x380 +0x38c +0x398), +0xd0
 * the wheels' radii; +0x1c the car's ride (+0xfc its ride height offset, inches; +0xa0..+0xa8 the body roll's angle,
 * reach in g and rate, +0xac/+0xb8 the pitch's under braking and under power), +0x3c its acceleration.
 * Engine animation: +0x39c the throttle, +0x3a4 the rev shake, +0x3a8 the engine's torque twist, +0x3ac its idle rock,
 * +0x3b0 the packet's engine speed, +0x3d8 the rev shake's timer (positive and negative halves).
 * Pkt_Car_Service: +0x44/+0x48 the front wheels' steering angles, +0x4c the wheels on the ground, +0x54 the hidden-part
 * bits, +0x8c the blown tyres. A tyre state's +0x90 its surface, whose road noise record is at +0xcc.
 */
#include <math.h>

#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x745bc0, CarRenderConnUpdateParts)
NFS_REPLACE(0x747510, CarRenderConnHide)
NFS_REPLACE(0x7459f0, CarRenderConnUpdateSteering)
NFS_REPLACE(0x739250, CarRenderConnBuildRenderMatrix)
NFS_REPLACE(0x7390b0, CarRenderConnAddRoadNoise)
NFS_REPLACE(0x745c70, CarRenderConnUpdateRoadNoise)
NFS_REPLACE(0x747050, CarRenderConnUpdateRenderMatrix)
NFS_REPLACE(0x746440, CarRenderConnUpdateBodyAnimation)
NFS_REPLACE(0x745f20, CarRenderConnUpdateEngineAnimation)
NFS_REPLACE(0x7564c0, CarRenderConnUpdate)
NFS_REPLACE(0x7561c0, CarRenderConnUpdateEffects)
NFS_REPLACE(0x744980, VehicleEffectFire)
NFS_REPLACE(0x744a50, VehicleEffectUpdate)
NFS_REPLACE(0x739420, HeliRenderConnUpdate)
NFS_REPLACE(0x746750, CarRenderConnUpdateTires)

void __fastcall CarRenderConnUpdateParts(char *self, void *, float dt, const char *packet);
void __fastcall CarRenderConnHide(char *self, void *, bool hide);
void __fastcall CarRenderConnUpdateSteering(char *self, void *, float dt, const char *packet);
void __fastcall CarRenderConnBuildRenderMatrix(char *self, void *, float dt);
void __fastcall CarRenderConnAddRoadNoise(char *self, void *, float speed, unsigned int wheels, const float *record);
void __fastcall CarRenderConnUpdateRoadNoise(char *self, void *, float dt, float speed, const char *packet);
void __fastcall CarRenderConnUpdateRenderMatrix(char *self, void *, float dt);
void __fastcall CarRenderConnUpdateBodyAnimation(char *self, void *, float dt, const char *packet);
void __fastcall CarRenderConnUpdateEngineAnimation(char *self, void *, float dt, const char *packet);
void __fastcall CarRenderConnUpdate(char *self, void *, const char *packet, float dt);
void __fastcall CarRenderConnUpdateEffects(char *self, void *, const char *packet, float dt);
void __fastcall VehicleEffectFire(char *effect, void *, const bMatrix4 *matrix, unsigned int key, float intensity,
                                  const bVector3 *velocity);
void __fastcall VehicleEffectUpdate(char *effect, void *, const bMatrix4 *matrix, unsigned int key, float dt,
                                    float intensity, const bVector3 *velocity);
void __cdecl HandleEmitterGroupDelete(void *owner, EmitterGroup *);
void __fastcall HeliRenderConnUpdate(char *self, void *, const char *packet, float dt);
void __fastcall CarRenderConnUpdateTires(char *self, void *, float dt, float speed, const char *packet);
bool __fastcall VehicleCheckForRain(VehicleRenderConn *self, void *, eView *view);

namespace
{
template <typename T> T &At(void *base, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}

template <typename T> const T &At(const void *base, int offset)
{
    return *reinterpret_cast<const T *>(static_cast<const char *>(base) + offset);
}

const int kAttributes = 0x14;
const int kMatrix = 0x34;
const int kPosition = 0x38;
const int kInfo = 0x44;
const int kRenderOffset = 0x50;
const int kRestartSkids = 0x60;
const int kTyres = 0x74;
const int kTyreSkidMaker = 0x5c;
const int kLastMatrix = 0x330;
const int kLastTranslation = 0x360;
const int kWheelSpin = 0x3a0;
const int kHiddenParts = 0x3bc;
const int kSteerCount = 0x3c8;
const int kSteerFrame = 0x3cc;
const int kSteerDistance = 0x3d0;
const int kSteerLeft = 0x3dc;
const int kSteerRight = 0x3e0;
const int kEffectsA = 0x3e4;
const int kEffectsB = 0x3ec;
const int kFlags = 0x3f8;
const int kEffectEmitters = 0x50;

const int kPacketSteerLeft = 0x44;
const int kPacketSteerRight = 0x48;
const int kPacketHiddenParts = 0x54;
const int kPacketOnGround = 0x4c;
const int kPacketBlownTyres = 0x8c;

const int kRoll = 0x390;
const int kPitch = 0x394;
const int kLift = 0x398;
const int kLiftFront = 0x90;
const int kLiftFrontAlso = 0xc0;
const int kLiftRear = 0x94;
const int kLiftRearAlso = 0xa4;
const int kTyreSurface = 0x90;
const int kSurfaceNoise = 0xcc;
const unsigned int kRoadNoiseAngle = 0x13eb7da2;
const float *const kBlowOutNoise = reinterpret_cast<const float *>(0x9053d0);
const int *const kDisableRoadNoise = reinterpret_cast<const int *>(0x9b09b0);

const int kWheelsA = 0x130;
const int kWheelsB = 0x230;
const int kMatrixUp = 0x350;
const int kWheelRadii = 0xd0;
const int kRide = 0x1c;
const int kRideHeight = 0xfc;
const int kPitchAngles[5] = {0x3a8, 0x384, 0x378, 0x370, 0x394};
const int kRollAngles[5] = {0x3a4, 0x388, 0x37c, 0x374, 0x394};
const int kLifts[3] = {0x398, 0x38c, 0x380};
const unsigned int kBodyRoll = 0xe95257c2;
const unsigned int kWheelTravel = 0x24cb15ea;
const float kInches = 0.0254f;
const float kAngleUnits = 10430.378f;

const int kLoadState = 0x28;
const unsigned int kFlagFire = 2;
const unsigned int kFlagShakeEffect = 0x10;
const int kPacketEffectLevel = 0x90;
const int kPacketEffectOn = 0x72;
/** The effects' attribute records (collection references: the key at +4): the second list's at a packet level of
 * at most 0 and 0.5 and with the packet's +0x72 set, the first list's when firing (flag 2) and while shifting or the
 * shake timer runs (flag 0x10). */
const unsigned int kEffectAtZero = 0xf7b59fc7;
const unsigned int kEffectAtHalf = 0x30b2997b;
const unsigned int kEffectOn = 0xd9cca9a3;
const unsigned int kEffectFire = 0xb699b7be;
const unsigned int kEffectShifting = 0x60cec115;

unsigned int EffectKey(char *self, unsigned int key)
{
    const void *p = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
        self + kAttributes, key, 0);
    if (!p)
        p = NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(0xc);
    return static_cast<const unsigned int *>(p)[1];
}

struct alignas(16) Matrix
{
    float m[16];
};

void MulMatrix(void *out, const void *a, const void *b)
{
    NFS_ORIGINAL(0x6bd900, void(__cdecl *)(void *, const void *, const void *))(out, a, b);
}

void EffectUpdate(char *self, char *effect, unsigned int key, float dt)
{
    VehicleEffectUpdate(effect, 0, reinterpret_cast<const bMatrix4 *>(self + kLastMatrix), key, dt, 1.0f,
                        At<const bVector3 *>(self, kPosition));
}

/** VehicleRenderConn::Effect: +0x10 its matrix on the car, +0x50 its emitter group, +0x54 the group's attribute key. */
const int kEffectMatrix = 0x10;
const int kEffectKey = 0x54;
const int kGroupIntensity = 0x68;
const int kGroupFlags = 0x18;
const unsigned int kOneShotGroup = 0x10c00000;
const unsigned int kLoopingGroup = 0x10800000;
void *const kEmitterSystem = reinterpret_cast<void *>(0x9166c0);

/** An Attrib::Gen::emittergroup instance of the key (its collection at +4), destroyed with the scope. */
struct EmitterGroupAttributes
{
    alignas(16) char instance[0x20];

    explicit EmitterGroupAttributes(unsigned int key)
    {
        NFS_ORIGINAL(0x744890, void(__thiscall *)(void *, unsigned int, unsigned int, unsigned int))(instance, key, 0, 0);
    }
    ~EmitterGroupAttributes()
    {
        NFS_ORIGINAL(0x45a430, void(__thiscall *)(void *))(instance);
    }
    const void *Collection() const
    {
        return *reinterpret_cast<const void *const *>(instance + 4);
    }
};

EmitterGroup *CreateGroup(const void *collection, unsigned int flags)
{
    return NFS_ORIGINAL(0x50c7b0, EmitterGroup *(__thiscall *)(void *, const void *, unsigned int))(kEmitterSystem,
                                                                                                   collection, flags);
}

void PlaceGroup(EmitterGroup *group, char *effect, const bMatrix4 *matrix)
{
    const bMatrix4 *local = reinterpret_cast<const bMatrix4 *>(effect + kEffectMatrix);
    if (matrix)
    {
        Matrix world;
        MulMatrix(&world, local, matrix);
        group->SetLocalWorld(reinterpret_cast<const bMatrix4 *>(&world));
    }
    else
        group->SetLocalWorld(local);
}
const unsigned int kFlagRain = 8;
const int kPacketDamage = 0x50;
const int kPacketLights = 0x60;
const int kPacketBrokenLights = 0x64;
const int kPacketFlashing = 0x70;
const int kPacketShifting = 0x71;
/** CarRenderInfo: +0 the wheels' spin, +0x117c shifting, +0x1180/+0x1182 the front wheels' angles (16-bit),
 * +0x1620/+0x1624 lights on and broken, +0x17a4 the frame's time, +0x17c0 flashing. */
const int kInfoShifting = 0x117c;
const int kInfoSteerLeft = 0x1180;
const int kInfoSteerRight = 0x1182;
const int kInfoLights = 0x1620;
const int kInfoBrokenLights = 0x1624;
const int kInfoDt = 0x17a4;
const int kInfoFlashing = 0x17c0;

const int kAcceleration = 0x3c;
const int kBodyRollAngle = 0x370;
const int kBodyPitchAngle = 0x374;
const int kPacketRollScale = 0x84;
const int kPacketPitchScale = 0x88;
const int kRideRoll = 0xa0;
const int kRidePitchBraking = 0xac;
const int kRidePitchPower = 0xb8;
const float kPerG = 0.10204081f;

/** Toward the target by at most rate, never past it. */
float Approach(float now, float target, float rate)
{
    if (target > now)
    {
        const float v = rate + now;
        return v < target ? v : target;
    }
    if (target < now)
    {
        const float v = now - rate;
        return v > target ? v : target;
    }
    return now;
}

const int kThrottle = 0x39c;
const int kRevShake = 0x3a4;
const int kTwist = 0x3a8;
const int kRock = 0x3ac;
const int kEngineSpeed = 0x3b0;
const int kRevTimer = 0x3d8;
const int kPacketGear = 0x78;
const int kPacketThrottle = 0x7c;
const int kPacketLoad = 0x80;
const int kPacketEngineSpeed = 0x94;
const int kPacketTwist = 0x98;
const int kPacketRock = 0x9c;
const unsigned int kRevShakeRate = 0xda27cd35;
const unsigned int kRevShakeAngle = 0x24e25513;
const unsigned int kTwistAngle = 0xe2c63383;
const unsigned int kTwistRate = 0xfaad3e61;
const unsigned int kTwistResponse = 0x564773be;
const unsigned int kRockLoadAngle = 0xf947fe58;
const unsigned int kRockAngle = 0xe91f59bd;
const unsigned int kRockFrequency = 0x80e657ff;

float Max0(float a)
{
    return NFS_ORIGINAL(0x401dc0, float(__cdecl *)(float, float))(a, 0.0f);
}

float Min0(float a)
{
    return NFS_ORIGINAL(0x401da0, float(__cdecl *)(float, float))(a, 0.0f);
}

float Abs(float a)
{
    return NFS_ORIGINAL(0x401d10, float(__cdecl *)(float))(a);
}


/** CarRenderInfo: angular and linear velocity (for motion blur and the wheels) and last position. */
const int kInfoPosition = 0x10;
const int kInfoSpin = 0x20;
const int kInfoVelocity = 0x30;

const int kHideableParts = 0x4c;
const unsigned int kMaxSteerAngle = 0xa9633fde;
const unsigned int kSteerRate = 0x79356463;
const float kDegrees = 0.017453f;
const float kTwoPi = 6.2831855f;

void *const *const kNis = reinterpret_cast<void *const *>(0x9885c8);
const float *const kRenderModifier = reinterpret_cast<const float *>(0x8f2260);

const float *Attribute(char *self, unsigned int key)
{
    const void *p = NFS_ORIGINAL(0x454810, const void *(__thiscall *)(void *, unsigned int, unsigned int))(
        self + kAttributes, key, 0);
    if (!p)
        p = NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(4);
    return static_cast<const float *>(p);
}

/** The drawn angle eased toward the wanted one by at most rate, then held within +-limit. */
float Ease(float drawn, float wanted, float rate, float limit)
{
    if (wanted - rate > drawn)
        drawn = rate + drawn;
    else if (wanted + rate < drawn)
        drawn = drawn - rate;
    else
        drawn = wanted;
    if (!(drawn < limit))
        drawn = limit;
    if (drawn < -limit)
        drawn = -limit;
    return drawn;
}

float Sin(float angle)
{
    float result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}

/** The view and distance gate of the animations: the NIS and the camera's own car always animate, the others only
 * when in view (frame counts) and within reach. */
bool Animates(char *self, float reach)
{
    if (*kNis || reinterpret_cast<VehicleRenderConn *>(self)->IsViewAnchor())
        return true;
    const unsigned int frame = At<unsigned int>(self, kSteerFrame);
    return !(frame < At<unsigned int>(self, kSteerCount) || frame == 0 || At<float>(self, kSteerDistance) > reach);
}

void DisableEmitters(char *self, int list)
{
    char *head = self + list;
    for (char *node = At<char *>(self, list); node != head; node = *reinterpret_cast<char **>(node))
        if (EmitterGroup *group = At<EmitterGroup *>(node, kEffectEmitters))
            group->Disable();
}
} // namespace

void __fastcall CarRenderConnUpdateParts(char *self, void *, float, const char *packet)
{
    unsigned int *drawn = &At<unsigned int>(self, kHiddenParts);
    const unsigned int *wanted = &At<unsigned int>(packet, kPacketHiddenParts);
    if (drawn[0] == wanted[0] && drawn[1] == wanted[1] && drawn[2] == wanted[2])
        return;
    for (unsigned int part = 0; part < kHideableParts; ++part)
    {
        const unsigned int bit = 1u << (part & 31);
        const bool hide = (wanted[part >> 5] & bit) != 0;
        if (hide == ((drawn[part >> 5] & bit) != 0))
            continue;
        if (CarRenderInfo *info = At<CarRenderInfo *>(self, kInfo))
            info->HideCarPart(static_cast<int>(part), hide);
    }
    drawn[0] = wanted[0];
    drawn[1] = wanted[1];
    drawn[2] = wanted[2];
}

void __fastcall CarRenderConnHide(char *self, void *, bool hide)
{
    unsigned int &flags = At<unsigned int>(self, kFlags);
    if (static_cast<bool>(flags & 1) == hide)
        return;
    if (!hide)
    {
        flags &= ~1u;
        return;
    }
    flags |= 1;
    At<int>(self, kWheelSpin) = 0;
    for (int tyre = 0; tyre < 4; ++tyre)
        reinterpret_cast<SkidMaker *>(At<char *>(self, kTyres + tyre * 4) + kTyreSkidMaker)->MakeNoSkid();
    At<char>(self, kRestartSkids) = 1;
    DisableEmitters(self, kEffectsB);
    DisableEmitters(self, kEffectsA);
}

void __fastcall CarRenderConnUpdateSteering(char *self, void *, float dt, const char *packet)
{
    if (!Animates(self, *kRenderModifier * 100.0f))
    {
        At<float>(self, kSteerRight) = 0.0f;
        At<float>(self, kSteerLeft) = 0.0f;
        return;
    }
    const float limit = *Attribute(self, kMaxSteerAngle) * kDegrees;
    const float rate = dt * *Attribute(self, kSteerRate) * kDegrees;
    At<float>(self, kSteerLeft) = Ease(At<float>(self, kSteerLeft), At<float>(packet, kPacketSteerLeft), rate, limit);
    At<float>(self, kSteerRight) = Ease(At<float>(self, kSteerRight), At<float>(packet, kPacketSteerRight), rate, limit);
}

void __fastcall CarRenderConnBuildRenderMatrix(char *self, void *, float dt)
{
    float offset[4];
    for (int i = 0; i < 4; ++i)
        offset[i] = At<float>(self, kRenderOffset + i * 4);
    char *info = At<char *>(self, kInfo);
    void *matrix = At<void *>(self, kMatrix);
    if (dt > 0.0f)
    {
        float last[3], now[3];
        NFS_ORIGINAL(0x5d0a70, void(__cdecl *)(const void *, float *))(self + kLastMatrix, last);
        NFS_ORIGINAL(0x5d0a70, void(__cdecl *)(const void *, float *))(matrix, now);
        const float inverse = 1.0f / dt;
        const float turn = inverse * kTwoPi;
        for (int i = 0; i < 3; ++i)
            At<float>(info, kInfoSpin + i * 4) = (now[i] - last[i]) * turn;
        const float *position = At<float *>(self, kPosition);
        for (int i = 0; i < 3; ++i)
            At<float>(info, kInfoVelocity + i * 4) = (position[i] - At<float>(info, kInfoPosition + i * 4)) * inverse;
        position = At<float *>(self, kPosition);
        for (int i = 0; i < 3; ++i)
            At<float>(info, kInfoPosition + i * 4) = position[i];
    }
    NFS_ORIGINAL(0x444fe0, void(__cdecl *)(void *, const void *))(self + kLastMatrix, matrix);
    float moved[4];
    NFS_ORIGINAL(0x6bd920, void(__cdecl *)(float *, const void *, const float *))(moved, matrix, offset);
    for (int i = 0; i < 3; ++i)
        At<float>(self, kLastTranslation + i * 4) -= moved[i];
}

void __fastcall CarRenderConnAddRoadNoise(char *self, void *, float speed, unsigned int wheels, const float *record)
{
    if (record[1] * record[0] * record[3] <= 0.0f)
        return;
    const float ramp = UMath::Ramp(speed, record[2], record[3]);
    const float frequency = ramp * record[0];
    const float amplitude = *Attribute(self, kRoadNoiseAngle) * record[1] * ramp * kDegrees;
    const float spin = At<float>(self, kWheelSpin);
    float pitch = 0.0f;
    float roll = 0.0f;
    if (wheels & 0xf)
    {
        float v = Sin(frequency * spin * kTwoPi) * amplitude * 0.5f;
        if (!(wheels & 3) && v < 0.0f)
            v = -v;
        if (!(wheels & 0xc))
            v = -(v < 0.0f ? -v : v);
        pitch = v;
        float w = Sin((spin + 0.33f) * frequency * kTwoPi) * amplitude;
        if (!(wheels & 9) && w < 0.0f)
            w = -w;
        if (!(wheels & 6))
            w = -(w < 0.0f ? -w : w);
        roll = w;
    }
    At<float>(self, kPitch) = pitch + At<float>(self, kPitch);
    At<float>(self, kRoll) = roll + At<float>(self, kRoll);
}

void __fastcall CarRenderConnUpdateRoadNoise(char *self, void *, float, float speed, const char *packet)
{
    At<float>(self, kRoll) = 0.0f;
    At<float>(self, kPitch) = 0.0f;
    At<float>(self, kLift) = 0.0f;
    if (!Animates(self, *kRenderModifier * 30.0f) || *kDisableRoadNoise)
        return;
    const unsigned int onGround = At<unsigned int>(packet, kPacketOnGround);
    float record[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    for (int wheel = 0; wheel < 4; ++wheel)
    {
        if (!(onGround & (1u << wheel)))
            continue;
        const float *noise = &At<float>(At<char *>(At<char *>(self, kTyres + wheel * 4), kTyreSurface), kSurfaceNoise);
        const float strength = noise[1] * noise[0];
        if (wheel == 0 ? strength > 0.0f : record[0] * record[1] < strength)
            for (int i = 0; i < 4; ++i)
                record[i] = noise[i];
    }
    CarRenderConnAddRoadNoise(self, 0, speed, onGround, record);
    CarRenderConnAddRoadNoise(self, 0, speed, At<unsigned int>(packet, kPacketBlownTyres) & onGround, kBlowOutNoise);
    const float pitch = Sin(At<float>(self, kPitch));
    const float roll = Sin(At<float>(self, kRoll));
    float lift = pitch * At<float>(self, kLiftFront);
    if (!(lift > 0.0f))
        lift = 0.0f;
    const float candidates[3] = {pitch * At<float>(self, kLiftFrontAlso), roll * At<float>(self, kLiftRear),
                                 roll * At<float>(self, kLiftRearAlso)};
    for (float c : candidates)
        if (c > lift)
            lift = c;
    At<float>(self, kLift) = lift;
}

void __fastcall CarRenderConnUpdateRenderMatrix(char *self, void *, float)
{
    if (!Animates(self, *kRenderModifier * 80.0f))
        return;
    char *matrix = self + kLastMatrix;
    Matrix saved[2][4];
    for (int i = 0; i < 4; ++i)
    {
        MulMatrix(&saved[0][i], self + kWheelsA + i * 0x40, matrix);
        MulMatrix(&saved[1][i], self + kWheelsB + i * 0x40, matrix);
    }
    float pitch = At<float>(self, 0x3ac);
    for (int o : kPitchAngles)
        pitch += At<float>(self, o);
    float roll = *Attribute(self, kBodyRoll) * kDegrees + At<float>(self, 0x3b0);
    for (int o : kRollAngles)
        roll += At<float>(self, o);
    float lift = At<float>(At<char *>(self, kRide), kRideHeight) * kInches;
    for (int o : kLifts)
        lift += At<float>(self, o);
    Matrix identity = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    Matrix rx, ry, turn, before, inverse;
    NFS_ORIGINAL(0x6bdbb0, void(__cdecl *)(void *, const void *, int))(&rx, &identity, static_cast<int>(pitch * kAngleUnits));
    NFS_ORIGINAL(0x6bdc60, void(__cdecl *)(void *, const void *, int))(&ry, &identity, static_cast<int>(roll * kAngleUnits));
    MulMatrix(&turn, &rx, &ry);
    NFS_ORIGINAL(0x444fe0, void(__cdecl *)(void *, const void *))(&before, matrix);
    MulMatrix(matrix, &turn, &before);
    for (int i = 0; i < 3; ++i)
        At<float>(self, kLastTranslation + i * 4) = lift * At<float>(self, kMatrixUp + i * 4) + At<float>(self, kLastTranslation + i * 4);
    NFS_ORIGINAL(0x45e300, void(__cdecl *)(void *, const void *))(&inverse, matrix);
    for (int i = 0; i < 4; ++i)
    {
        MulMatrix(self + kWheelsA + i * 0x40, &saved[0][i], &inverse);
        MulMatrix(self + kWheelsB + i * 0x40, &saved[1][i], &inverse);
    }
    const float travel = *Attribute(self, kWheelTravel) * kInches;
    if (!(travel > 0.0f))
        return;
    float top = At<float>(self, kWheelRadii) + At<float>(self, kWheelsA + 0x38);
    for (int i = 1; i < 4; ++i)
    {
        const float z = At<float>(self, kWheelsA + i * 0x40 + 0x38) + At<float>(self, kWheelRadii + i * 4);
        if (!(top > z))
            top = z;
    }
    if (!(top > travel))
        return;
    const float rise = top - travel;
    for (int i = 0; i < 3; ++i)
        At<float>(self, kLastTranslation + i * 4) = At<float>(self, kMatrixUp + i * 4) * rise + At<float>(self, kLastTranslation + i * 4);
    At<float>(self, kLastTranslation + 12) = rise * 0.0f + At<float>(self, kLastTranslation + 12);
    for (int i = 0; i < 4; ++i)
    {
        At<float>(self, kWheelsA + i * 0x40 + 0x38) -= rise;
        At<float>(self, kWheelsB + i * 0x40 + 0x38) -= rise;
    }
}

void __fastcall CarRenderConnUpdateBodyAnimation(char *self, void *, float dt, const char *packet)
{
    if (!Animates(self, *kRenderModifier * 80.0f))
    {
        At<float>(self, kBodyRollAngle) = 0.0f;
        At<float>(self, kBodyPitchAngle) = 0.0f;
        return;
    }
    const float *a = At<float *>(self, kAcceleration);
    char *ride = At<char *>(self, kRide);
    float across = (At<float>(self, 0x348) * a[2] + At<float>(self, 0x344) * a[1] + a[0] * At<float>(self, 0x340)) * kPerG;
    float along = (At<float>(self, 0x338) * a[2] + At<float>(self, 0x334) * a[1] + At<float>(self, 0x330) * a[0]) * kPerG;
    const float *pitchSet = &At<float>(ride, along < 0.0f ? kRidePitchPower : kRidePitchBraking);
    const float *rollSet = &At<float>(ride, kRideRoll);
    const float pitchReach = At<float>(packet, kPacketPitchScale) * pitchSet[0] * kDegrees;
    const float pitchRate = dt * pitchSet[2] * kDegrees;
    const float rollReach = At<float>(packet, kPacketRollScale) * rollSet[0] * kDegrees;
    const float rollRate = dt * rollSet[2] * kDegrees;
    if ((across < 0.0f ? -across : across) < 0.2f)
        across = 0.0f;
    if ((along < 0.0f ? -along : along) < 0.2f)
        along = 0.0f;
    float roll = (UMath::Ramp(across, -rollSet[1], rollSet[1]) - 0.5f) * rollReach;
    roll = roll + roll;
    float pitch = (UMath::Ramp(along * -kPerG, -pitchSet[1], pitchSet[1]) - 0.5f) * pitchReach;
    pitch = pitch + pitch;
    const float *v = At<float *>(self, kPosition);
    if (v[0] * v[0] + v[1] * v[1] + v[2] * v[2] < 1.0f)
    {
        roll = 0.0f;
        pitch = 0.0f;
    }
    At<float>(self, kBodyRollAngle) = Approach(At<float>(self, kBodyRollAngle), roll, rollRate);
    At<float>(self, kBodyPitchAngle) = Approach(At<float>(self, kBodyPitchAngle), pitch, pitchRate);
}

void __fastcall CarRenderConnUpdateEngineAnimation(char *self, void *, float dt, const char *packet)
{
    if (!Animates(self, *kRenderModifier * 30.0f))
    {
        At<float>(self, kThrottle) = 0.0f;
        At<float>(self, kRock) = 0.0f;
        At<float>(self, kTwist) = 0.0f;
        At<float>(self, kRevShake) = 0.0f;
        At<float>(self, kRevTimer) = 0.0f;
        At<float>(self, kEngineSpeed) = 0.0f;
        return;
    }
    float &timer = At<float>(self, kRevTimer);
    if (timer == 0.0f)
        At<float>(self, kRevShake) = 0.0f;
    else
    {
        const float *v = At<float *>(self, kPosition);
        const float speed = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        const float rate = *Attribute(self, kRevShakeRate) * kDegrees;
        const float angle = *Attribute(self, kRevShakeAngle) * kDegrees;
        const int gears = At<int>(packet, kPacketGear) - 2;
        if (!(rate > 0.0f) || !(angle > 0.0f) || gears < 0 || !(speed > 10.0f))
        {
            timer = 0.0f;
            At<float>(self, kRevShake) = 0.0f;
        }
        else
        {
            const float lengthwise = NFS_ORIGINAL(0x402220, float(__cdecl *)(const void *, const void *))(
                                         At<void *>(self, kAcceleration), self + kLastMatrix) * kPerG;
            const float strength = UMath::Ramp(Abs(lengthwise), 0.1f, 0.5f);
            const float wave = Sin(Abs(timer) * 0.5f * kTwoPi);
            const float shake = static_cast<float>(wave * strength * pow(0.95, gears) * angle);
            At<float>(self, kRevShake) = shake;
            if (timer > 0.0f)
                timer = Max0(timer - rate / angle * dt);
            else if (timer < 0.0f)
            {
                At<float>(self, kRevShake) = shake * 0.25f;
                timer = Min0(rate / angle * dt + timer);
            }
        }
    }
    float change = At<float>(packet, kPacketThrottle) - At<float>(self, kThrottle);
    if ((change < 0.0f ? -change : change) < 0.005f)
        change = 0.0f;
    const float pace = change / dt;
    float throttle = change + At<float>(self, kThrottle);
    if (!(throttle < 1.0f))
        throttle = 1.0f;
    else if (0.0f > throttle)
        throttle = 0.0f;
    At<float>(self, kThrottle) = throttle;
    At<unsigned int>(self, kEngineSpeed) = At<unsigned int>(packet, kPacketEngineSpeed);
    if (At<float>(packet, kPacketTwist) == 0.0f)
    {
        const float most = At<float>(packet, kPacketLoad) * At<float>(packet, kPacketThrottle) * *Attribute(self, kTwistAngle) *
                           kDegrees;
        const float rate = *Attribute(self, kTwistRate) * kDegrees;
        const float target = UMath::Ramp(pace * *Attribute(self, kTwistResponse), 0.0f, 0.2f) * most;
        const float step = rate * dt;
        float twist = At<float>(self, kTwist);
        if (twist < target)
        {
            const float n = step + twist;
            twist = n < target ? n : target;
        }
        else
        {
            const float n = twist - step;
            twist = n > target ? n : target;
        }
        if (!(twist < most))
            twist = most;
        if (0.0f > twist)
            twist = 0.0f;
        At<float>(self, kTwist) = twist;
    }
    else
        At<unsigned int>(self, kTwist) = At<unsigned int>(packet, kPacketTwist);
    if (At<float>(packet, kPacketRock) == 0.0f)
    {
        const float load = *Attribute(self, kRockLoadAngle) * kDegrees;
        const float base = *Attribute(self, kRockAngle) * kDegrees;
        const float wave = NFS_ORIGINAL(0x45dbd0, float(__cdecl *)(float))(
            At<float>(self, kWheelSpin) * *Attribute(self, kRockFrequency) * kTwoPi);
        const float l = At<float>(packet, kPacketLoad);
        At<float>(self, kRock) = wave * (load * l + base) * l;
    }
    else
        At<unsigned int>(self, kRock) = At<unsigned int>(packet, kPacketRock);
}

void __fastcall CarRenderConnUpdate(char *self, void *, const char *packet, float dt)
{
    if (At<int>(self, kLoadState) < 2)
        return;
    char *info = At<char *>(self, kInfo);
    if (!info)
        return;
    CarRenderInfo *render = reinterpret_cast<CarRenderInfo *>(info);
    render->SetDamageInfo(*reinterpret_cast<const DamageZone::Info *>(packet + kPacketDamage));
    At<float>(info, kInfoDt) = dt;
    At<unsigned int>(info, kInfoLights) = At<unsigned int>(packet, kPacketLights);
    At<unsigned int>(info, kInfoBrokenLights) = At<unsigned int>(packet, kPacketBrokenLights);
    At<char>(info, kInfoFlashing) = At<char>(packet, kPacketFlashing);
    render->UpdateFlashing();
    At<unsigned int>(info, kInfoShifting) = At<unsigned char>(packet, kPacketShifting);
    At<short>(info, kInfoSteerLeft) = static_cast<short>(static_cast<int>(At<float>(self, kSteerLeft) * kAngleUnits));
    At<short>(info, kInfoSteerRight) = static_cast<short>(static_cast<int>(At<float>(self, kSteerRight) * kAngleUnits));
    const float *v = At<float *>(self, kPosition);
    const float speed = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    float spin = dt + At<float>(self, kWheelSpin);
    At<float>(self, kWheelSpin) = spin;
    if (spin >= 10.0f)
        At<float>(self, kWheelSpin) = spin - 10.0f;
    At<unsigned int>(info, 0) = At<unsigned int>(self, kWheelSpin);
    CarRenderConnUpdateParts(self, 0, dt, packet);
    CarRenderConnBuildRenderMatrix(self, 0, dt);
    VehicleRenderConn *conn = reinterpret_cast<VehicleRenderConn *>(self);
    if (VehicleCheckForRain(conn, 0, reinterpret_cast<eView *>(0x919650)) ||
        VehicleCheckForRain(conn, 0, reinterpret_cast<eView *>(0x9196c0)))
        At<unsigned int>(self, kFlags) |= kFlagRain;
    else
        At<unsigned int>(self, kFlags) &= ~kFlagRain;
    CarRenderConnUpdateSteering(self, 0, dt, packet);
    CarRenderConnUpdateTires(self, 0, dt, speed, packet);
    CarRenderConnUpdateRoadNoise(self, 0, dt, speed, packet);
    CarRenderConnUpdateBodyAnimation(self, 0, dt, packet);
    CarRenderConnUpdateEngineAnimation(self, 0, dt, packet);
    CarRenderConnUpdateRenderMatrix(self, 0, dt);
    CarRenderConnUpdateEffects(self, 0, packet, dt);
    if (At<int>(self, kLoadState) >= 2)
    {
        At<int>(self, kLoadState) = 3;
        At<char>(self, kRestartSkids) = 0;
    }
}

void __fastcall CarRenderConnUpdateEffects(char *self, void *, const char *packet, float dt)
{
    if (!Animates(self, *kRenderModifier * 80.0f))
    {
        DisableEmitters(self, kEffectsA);
        DisableEmitters(self, kEffectsB);
        return;
    }
    const unsigned int atHalf = EffectKey(self, kEffectAtHalf);
    const unsigned int atZero = EffectKey(self, kEffectAtZero);
    const unsigned int on = EffectKey(self, kEffectOn);
    const unsigned int fire = EffectKey(self, kEffectFire);
    const unsigned int shifting = EffectKey(self, kEffectShifting);
    const unsigned int flags = At<unsigned int>(self, kFlags);
    char *head = self + kEffectsA;
    for (char *node = At<char *>(self, kEffectsA); node != head; node = *reinterpret_cast<char **>(node))
    {
        if (At<char>(packet, kPacketShifting))
            EffectUpdate(self, node, shifting, dt);
        else if (flags & kFlagFire)
            NFS_ORIGINAL(0x744980, void(__thiscall *)(void *, const void *, unsigned int, float, const void *))(
                node, self + kLastMatrix, fire, 1.0f, At<void *>(self, kPosition));
        else if ((flags & kFlagShakeEffect) && !(At<float>(self, kRevTimer) == 0.0f))
            EffectUpdate(self, node, shifting, dt);
        else if (EmitterGroup *group = At<EmitterGroup *>(node, kEffectEmitters))
            group->Disable();
    }
    const float level = At<float>(packet, kPacketEffectLevel);
    head = self + kEffectsB;
    for (char *node = At<char *>(self, kEffectsB); node != head; node = *reinterpret_cast<char **>(node))
    {
        if (atZero && level <= 0.0f)
            EffectUpdate(self, node, atZero, dt);
        else if (atHalf && level <= 0.5f)
            EffectUpdate(self, node, atHalf, dt);
        else if (At<char>(packet, kPacketEffectOn))
            EffectUpdate(self, node, on, dt);
        else if (EmitterGroup *group = At<EmitterGroup *>(node, kEffectEmitters))
            group->Disable();
    }
    At<unsigned int>(self, kFlags) &= ~kFlagFire;
}

void __fastcall VehicleEffectFire(char *effect, void *, const bMatrix4 *matrix, unsigned int key, float intensity,
                                  const bVector3 *velocity)
{
    EmitterGroupAttributes attributes(key);
    if (!attributes.Collection())
        return;
    EmitterGroup *group = CreateGroup(attributes.Collection(), kOneShotGroup);
    if (!group)
        return;
    At<float>(group, kGroupIntensity) = intensity;
    group->MakeOneShot(true);
    At<unsigned int>(group, kGroupFlags) |= 1;
    PlaceGroup(group, effect, matrix);
    if (velocity)
        group->SetInheritVelocity(velocity);
}

void __fastcall VehicleEffectUpdate(char *effect, void *, const bMatrix4 *matrix, unsigned int key, float dt,
                                    float intensity, const bVector3 *velocity)
{
    EmitterGroup *&group = At<EmitterGroup *>(effect, kEffectEmitters);
    if (key != At<unsigned int>(effect, kEffectKey))
    {
        if (group)
        {
            group->UnSubscribe();
            if (EmitterGroup *old = group)
            {
                old->~EmitterGroup();
                bFree(EmitterGroupSlotPool, old);
            }
            group = 0;
        }
        At<unsigned int>(effect, kEffectKey) = key;
        if (!key)
            return;
        EmitterGroupAttributes attributes(key);
        if (!attributes.Collection())
            return;
        group = CreateGroup(attributes.Collection(), kLoopingGroup);
        if (group)
            group->SubscribeToDeletion(effect, &HandleEmitterGroupDelete);
    }
    if (!group)
        return;
    PlaceGroup(group, effect, matrix);
    group->Enable();
    At<float>(group, kGroupIntensity) = intensity;
    group->MakeOneShot(false);
    if (velocity)
        group->SetInheritVelocity(velocity);
    group->Update(dt);
}

/** HeliRenderConn: +0x170 its drawn matrix (translation at +0x1a0), +0x1bc the packet's rotor state. */
void __fastcall HeliRenderConnUpdate(char *self, void *, const char *packet, float)
{
    if (At<int>(self, kLoadState) < 2 || !At<void *>(self, kInfo))
        return;
    At<unsigned int>(self, 0x1bc) = At<unsigned int>(packet, 0xc);
    float offset[4];
    for (int i = 0; i < 4; ++i)
        offset[i] = At<float>(self, kRenderOffset + i * 4);
    void *matrix = At<void *>(self, kMatrix);
    NFS_ORIGINAL(0x444fe0, void(__cdecl *)(void *, const void *))(self + 0x170, matrix);
    float moved[4];
    NFS_ORIGINAL(0x6bd920, void(__cdecl *)(float *, const void *, const float *))(moved, matrix, offset);
    for (int i = 0; i < 3; ++i)
        At<float>(self, 0x1a0 + i * 4) -= moved[i];
    if (At<int>(self, kLoadState) >= 2)
    {
        At<int>(self, kLoadState) = 3;
        At<char>(self, kRestartSkids) = 0;
    }
}

namespace
{
/** Wheels: per wheel +0x90 its place on the car (4 floats), +0xd0 its radius, +0xe0 its rest height, +0x3b8 the most it
 * turns a frame; the body's blown-tyre lean +0x378/+0x37c/+0x380; the rear wheels' hop +0x384/+0x388/+0x38c.
 * Pkt_Car_Service per wheel: +0x04 its compression, +0x14 its speed, +0x24 its slip, +0x34 its spin difference.
 * A tyre state: +0x10 last frame's world place, +0x5c its SkidMaker, +0x60 its world place, +0x80 its roll angle.
 * The ride's per-wheel and per-axle attribute arrays at +0xd4, +0xc4 and +0x90 (Attrib arrays: their items at +8). */
const int kWheelPlace = 0x90;
const int kWheelRadius = 0xd0;
const int kWheelRest = 0xe0;
const int kWheelTurnLimit = 0x3b8;
const int kBlownLean = 0x378;
const int kHop = 0x384;
const int kPacketCompression = 0x04;
const int kPacketWheelSpeed = 0x14;
const int kPacketWheelSlip = 0x24;
const int kPacketWheelSpin = 0x34;
const int kTyreLast = 0x10;
const int kTyreSkids = 0x5c;
const int kTyreWorld = 0x60;
const int kTyreRoll = 0x80;
const int kRideSkidWidths = 0xd4;
const int kRideSmokeAlong = 0xc4;
const int kRideSmokeAcross = 0x90;
const int kInfoBlownTyres = 0x17cc;
const int kCollider = 0x64;
const unsigned int kHopAttribute = 0x44e52948;
const float kBlownDrop = 0.12f;

/** An item of one of the ride's Attrib arrays (its length at the array, its items 8 bytes on), or the default. */
const float *RideItem(char *ride, int array, unsigned int index)
{
    const unsigned int length = NFS_ORIGINAL(0x452940, unsigned int(__thiscall *)(void *))(ride + array);
    if (index < length)
        return &At<float>(ride, array + 8 + static_cast<int>(index) * 4);
    return static_cast<const float *>(NFS_ORIGINAL(0x6269b0, const void *(__cdecl *)(unsigned int))(4));
}

float BSin(float angle)
{
    return NFS_ORIGINAL(0x45dbd0, float(__cdecl *)(float))(angle);
}

float AbsF(float v)
{
    return v < 0.0f ? -v : v;
}

void ResetMatrix(float *m)
{
    for (int i = 0; i < 16; ++i)
        m[i] = (i % 5) ? 0.0f : 1.0f;
}
} // namespace

void __fastcall CarRenderConnUpdateTires(char *self, void *, float dt, float speed, const char *packet)
{
    At<float>(self, kBlownLean) = 0.0f;
    At<float>(self, kBlownLean + 4) = 0.0f;
    At<float>(self, kBlownLean + 8) = 0.0f;
    const bool animated = Animates(self, *kRenderModifier * 30.0f);
    float hopLift = 0.0f, hopRoll = 0.0f, hopBounce = 0.0f;
    bool hopping = false;
    if (animated)
    {
        const float strength = *Attribute(self, kHopAttribute);
        const float load = At<float>(packet, kPacketPitchScale);
        if (load > 0.0f && strength > 0.0f)
        {
            const float spin = At<float>(self, kWheelSpin);
            hopLift = AbsF(BSin(spin + 37.699112f)) * load * strength * strength * 0.00261795f;
            hopRoll = AbsF(BSin(spin * 150.79645f)) * load * strength * strength * 0.0034906f;
            hopBounce = AbsF(BSin((spin + 0.5f) * 150.79645f)) * load * strength * strength * 0.0052359f;
            hopping = true;
        }
    }
    At<float>(self, kHop) = 0.0f;
    At<float>(self, kHop + 4) = 0.0f;
    At<float>(self, kHop + 8) = 0.0f;
    reinterpret_cast<VehicleRenderConn *>(self)->IsViewAnchor();
    const bool close = Animates(self, 80.0f);
    char *info = At<char *>(self, kInfo);
    char *ride = At<char *>(self, kRide);
    const unsigned int onGroundBits = At<unsigned int>(packet, kPacketOnGround);
    const unsigned int blownBits = At<unsigned int>(packet, kPacketBlownTyres);
    for (unsigned int wheel = 0; wheel < 4; ++wheel)
    {
        const unsigned int bit = 1u << wheel;
        const bool onGround = (bit & onGroundBits) != 0;
        const bool blown = (blownBits & bit) != 0;
        char *tyre = At<char *>(self, kTyres + wheel * 4);
        float *a = &At<float>(self, kWheelsA + wheel * 0x40);
        float *b = &At<float>(self, kWheelsB + wheel * 0x40);
        ResetMatrix(a);
        ResetMatrix(b);
        const float *place = &At<float>(self, kWheelPlace + wheel * 16);
        const float limit = At<float>(self, kWheelTurnLimit);
        float turn = At<float>(packet, kPacketWheelSpeed + wheel * 4) / At<float>(self, kWheelRadius + wheel * 4) * dt;
        if (!(turn < limit))
            turn = limit;
        if (-limit > turn)
            turn = -limit;
        const float roll = turn + At<float>(tyre, kTyreRoll);
        At<float>(tyre, kTyreRoll) = roll;
        if (!(roll < kTwoPi))
            At<float>(tyre, kTyreRoll) = roll - kTwoPi;
        else if (roll <= -kTwoPi)
            At<float>(tyre, kTyreRoll) = roll + kTwoPi;
        NFS_ORIGINAL(0x6bdc60, void(__cdecl *)(void *, const void *, int))(
            a, a, static_cast<int>(At<float>(tyre, kTyreRoll) * kAngleUnits));
        if (wheel < 2)
        {
            const float steer = At<float>(self, kSteerLeft + static_cast<int>(wheel) * 4);
            NFS_ORIGINAL(0x6bdd20, void(__cdecl *)(void *, const void *, int))(a, a, static_cast<int>(steer * kAngleUnits));
            NFS_ORIGINAL(0x6bdd20, void(__cdecl *)(void *, const void *, int))(b, b, static_cast<int>(steer * kAngleUnits));
        }
        float height = At<float>(self, kWheelRadius + wheel * 4) - At<float>(self, kWheelRest + wheel * 4) +
                       At<float>(packet, kPacketCompression + wheel * 4);
        if (animated && blown)
        {
            height = height - kBlownDrop;
            float lean = NFS_ORIGINAL(0x5cb5d0, float(__cdecl *)(float, float))(kBlownDrop, AbsF(place[1])) * -3.1415927f;
            if (place[1] < 0.0f)
                lean = lean * -1.0f;
            float tilt = NFS_ORIGINAL(0x5cb5d0, float(__cdecl *)(float, float))(kBlownDrop, AbsF(place[0])) * 3.1415927f;
            if (place[0] < 0.0f)
                tilt = tilt * -1.0f;
            At<float>(self, kBlownLean) = lean + At<float>(self, kBlownLean);
            At<float>(self, kBlownLean + 4) = tilt + At<float>(self, kBlownLean + 4);
            At<float>(self, kBlownLean + 8) = At<float>(self, kBlownLean + 8) - 0.03f;
        }
        if (wheel >= 2 && onGround && hopping)
        {
            const float spin = At<float>(packet, kPacketWheelSpin + wheel * 4);
            const float r = spin > 0.0f ? UMath::Ramp(spin, 1.0f, 5.0f) : UMath::Ramp(-spin, 5.0f, 20.0f);
            const float before = At<float>(self, kHop + 8);
            At<float>(self, kHop + 4) = hopRoll * r;
            float bounce = Sin(hopRoll * r) * At<float>(self, kWheelPlace);
            if (!(bounce > before))
                bounce = before;
            At<float>(self, kHop + 8) = bounce;
            At<float>(self, kHop) = hopLift * r;
            height = BSin(r * hopBounce) * (At<float>(self, kWheelPlace) - place[0]) + height;
        }
        b[12] = place[0];
        b[13] = place[1];
        a[12] = place[0];
        b[14] = b[14] + place[2] + height;
        a[13] = place[1];
        a[14] = a[14] + place[2] + height;
        if (close)
            At<char>(info, kInfoBlownTyres + static_cast<int>(wheel)) = (bit & At<unsigned int>(packet, kPacketBlownTyres)) != 0;
        NFS_ORIGINAL(0x6bd920, void(__cdecl *)(void *, const void *, const void *))(tyre + kTyreWorld, self + kLastMatrix,
                                                                                    a + 12);
        NFS_ORIGINAL(0x738f50, void(__thiscall *)(void *, const void *, bool, bool))(
            tyre, At<void *>(self, kCollider), ((At<unsigned int>(self, kFlags) >> 3) & 1) != 0, blown);
        if (onGround && close)
        {
            float across = AbsF(At<float>(packet, kPacketWheelSlip + wheel * 4) * 0.05f) - 0.1f;
            if (!(across > 0.0f))
                across = 0.0f;
            float along = AbsF(At<float>(packet, kPacketWheelSpin + wheel * 4) * 0.2f) - 0.1f;
            if (!(along > 0.0f))
                along = 0.0f;
            const float skid = NFS_ORIGINAL(0x5c5e80, float(__cdecl *)(float))(along * along + across * across);
            if (skid > 0.0f)
            {
                float moved[4];
                for (int i = 0; i < 4; ++i)
                    moved[i] = At<float>(tyre, kTyreWorld + i * 4) - At<float>(tyre, kTyreLast + i * 4);
                const float width = *RideItem(ride, kRideSkidWidths, wheel);
                NFS_ORIGINAL(0x745580, void(__thiscall *)(void *, float, const void *, const void *, const void *, float))(
                    tyre, skid, moved, a, self + kLastMatrix, width);
            }
            else
                reinterpret_cast<SkidMaker *>(tyre + kTyreSkids)->MakeNoSkid();
            const float smokeAlong = *RideItem(ride, kRideSmokeAlong, wheel >> 1);
            const float smokeAcross = *RideItem(ride, kRideSmokeAcross, wheel >> 1);
            NFS_ORIGINAL(0x7457d0, void(__thiscall *)(void *, float, float, float, const void *, const void *, float))(
                tyre, smokeAlong * At<float>(packet, kPacketWheelSpin + wheel * 4),
                smokeAcross * At<float>(packet, kPacketWheelSlip + wheel * 4), speed, At<void *>(self, kPosition),
                self + kLastMatrix, dt);
        }
        else
            reinterpret_cast<SkidMaker *>(tyre + kTyreSkids)->MakeNoSkid();
        for (int i = 0; i < 4; ++i)
            At<unsigned int>(tyre, kTyreLast + i * 4) = At<unsigned int>(tyre, kTyreWorld + i * 4);
    }
}
