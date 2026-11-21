/**
 * Opponents and cops where the world is not loaded (AIVehicle::UpdateSimplePhysics 0x422780). A car with no collision under
 * its wheels (PVehicle::IsOffWorld) leaves the real physics and is moved along the road by this function, which should take
 * it towards the speed the AI asks. In the original it loses half its acceleration a step instead of gaining it: logged with
 * Debug.AiWatch, an opponent 400 m from a standing player slowed from 44 m/s to nothing in 7 seconds and stood there for the
 * rest of the race, however fast the AI asked it to go. Why the sign comes out that way has not been found in the machine
 * code; what the function has to give has.
 *
 * With the vanilla bug fixes on, or the improved AI (gameplay/ai/aituning.h), the car's speed is put right afterwards: towards the speed the AI asks as the original
 * means it to be, the car's acceleration times (skill + 1) / 2, no faster than three quarters of its top speed and a quarter
 * more with skill; the speed the original leaves goes on being used only for a car going down to the speed it was asked.
 */
#include <math.h>

#include "game/gen/api.h"
#include "legacy/link.h"
#include "gameplay/ai/aituning.h"
#include "gameplay/vanillafixes.h"

NFS_REPLACE(0x422780, AIVehicle::UpdateSimplePhysics)

namespace
{
const int kAiInterface = 0x4c;
const int kDriveSpeed = 0x84;
const int kReverseOverride = 0xd4;
/** IVehicleAI: its simable, vehicle, top speed, acceleration at a speed, skill. */
const int kAiSimable = 0x04;
const int kAiVehicle = 0x08;
const int kAiTopSpeed = 0x74;
const int kAiAcceleration = 0x78;
const int kAiSkill = 0xcc;
const int kVehicleMode = 0x38;
const int kSimpleMode = 2;
/** ISimable's rigid body; IRigidBody: position, velocity, set position, set velocity. */
const int kSimableBody = 0x54;
const int kBodyPosition = 0x20;
const int kBodyVelocity = 0x24;
const int kBodySpeed = 0x2c;
const int kBodySetPosition = 0x5c;
const int kBodySetVelocity = 0x60;
const float kTopSpeedShare = 0.75f;
const float kTopSpeedSkillShare = 0.25f;
const float kLeast = 1e-3f;

template <typename R, typename... A> R Virtual(void *object, int offset, A... args)
{
    typedef R(__thiscall * Function)(void *, A...);
    return (*reinterpret_cast<Function *>(*reinterpret_cast<char **>(object) + offset))(object, args...);
}
} // namespace

void AIVehicle::UpdateSimplePhysics(float dt)
{
    typedef void(__thiscall * Original)(AIVehicle *, float);
    const Original original = NFS_ORIGINAL(0x422780, Original);
    if (!nfs::vanillafixes::On() && !nfs::aituning::Improved())
    {
        original(this, dt);
        return;
    }
    char *self = reinterpret_cast<char *>(this);
    void *ai = self + kAiInterface;
    void *vehicle = Virtual<void *>(ai, kAiVehicle);
    if (Virtual<int>(vehicle, kVehicleMode) != kSimpleMode)
    {
        original(this, dt);
        return;
    }
    void *body = Virtual<void *>(Virtual<void *>(ai, kAiSimable), kSimableBody);
    const float *from = Virtual<const float *>(body, kBodyPosition);
    const float start[3] = {from[0], from[1], from[2]};
    const float speed = Virtual<float>(body, kBodySpeed);
    const float drive = *reinterpret_cast<const float *>(self + kDriveSpeed);
    original(this, dt);
    // Slowing down to the speed asked, and a car the AI is backing out, are as the original has them.
    void *reverse = *reinterpret_cast<void **>(self + kReverseOverride);
    if (speed > drive || Virtual<bool>(reverse, 0x14))
        return;
    const float skill = Virtual<float>(ai, kAiSkill);
    float want = speed + Virtual<float>(ai, kAiAcceleration, speed) * (skill + 1.0f) * 0.5f * dt;
    if (want > drive)
        want = drive;
    const float most = Virtual<float>(ai, kAiTopSpeed) * (kTopSpeedShare + kTopSpeedSkillShare * skill);
    if (want > most)
        want = most;
    if (want < 0.0f)
        want = 0.0f;
    const float *velocity = Virtual<const float *>(body, kBodyVelocity);
    const float *to = Virtual<const float *>(body, kBodyPosition);
    // The way it went, or from standing, the way the target lies (the original gives no direction at no speed).
    float direction[3] = {velocity[0], velocity[1], velocity[2]};
    float length = sqrtf(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
    if (length < kLeast)
    {
        const float *target = reinterpret_cast<const float *>(self + kDriveSpeed + 4);
        direction[0] = target[0] - start[0];
        direction[1] = target[1] - start[1];
        direction[2] = target[2] - start[2];
        length = sqrtf(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
        if (length < kLeast)
            return;
    }
    const float unit[3] = {direction[0] / length, direction[1] / length, direction[2] / length};
    const float moved[3] = {start[0] + unit[0] * want * dt, to[1], start[2] + unit[2] * want * dt};
    const float setVelocity[3] = {unit[0] * want, unit[1] * want, unit[2] * want};
    Virtual<void>(body, kBodySetPosition, moved);
    Virtual<void>(body, kBodySetVelocity, setVelocity);
}
