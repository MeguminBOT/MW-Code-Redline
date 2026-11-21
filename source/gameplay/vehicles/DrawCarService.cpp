/** What a car's draw behaviour tells its render connection each frame: lights on and broken, wheels, hidden parts, and
 * for a performance car its gear, engine speed and turbo (original: zPhysicsBehaviors DrawCar::OnService 0x6b6fb0,
 * DrawPerformanceCar::OnService 0x6b7360, filling RenderConn::Pkt_Car_Service). */
#include <math.h>

#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x6b6fb0, DrawCarServicePacket)
NFS_REPLACE(0x6b7360, DrawPerformanceCarServicePacket)

namespace
{
template <typename T> T &At(void *base, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}

template <typename R, typename... A> R Virtual(void *object, int offset, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*reinterpret_cast<void ***>(object))[offset / 4])(object, args...);
}

/** VehicleFX::GetMaps: 0x20-byte entries, the light's bit first, ended by a zero bit; a light the packet carries has
 * its seventh word set. */
const unsigned int *const kLightMaps = reinterpret_cast<const unsigned int *>(0x905958);
const int kLightMapWords = 8;
const int kLightMapCarried = 6;
void *const kDynamicsEntityHandle = reinterpret_cast<void *>(0x622540);
const float kTwoPi = 6.2831855f;
const float kPi = 3.1415927f;

/** DrawCar: its simable, in view and distance (from the packet), and the interfaces it reads. */
const int kSimable = 0x34;
const int kVehicle = 0x90;
const int kDamage = 0x94;
const int kDamageZones = 0x98;
const int kEngine = 0x9c;
const int kTyres = 0xa0;
const int kHiddenParts = 0xa8;
const int kInView = 0xb0;
const int kViewDistance = 0xb4;
const int kSuspension = 0x88;
const int kSteering = 0x8c;

/** Pkt_Car_Service. */
const int kPacketWheelRoll = 0x04;
const int kPacketWheelLoad = 0x14;
const int kPacketWheelSlip = 0x24;
const int kPacketWheelSkid = 0x34;
const int kPacketSteer = 0x44;
const int kPacketWheelsOnGround = 0x4c;
const int kPacketGear = 0x50;
const int kPacketLights = 0x60;
const int kPacketBrokenLights = 0x64;
const int kPacketInView = 0x68;
const int kPacketViewDistance = 0x6c;
const int kPacketDamaged = 0x70;
const int kPacketRideHeight = 0x84;
const int kPacketRideHeightRear = 0x88;
const int kPacketBlownTyres = 0x8c;
const int kPacketSteeringWheel = 0x90;
const int kPacketEngine = 0x94;
const int kTyreBlown = 2;

/** DrawPerformanceCar: the camera mover's view, its transmission and engine. */
const int kPerformanceView = 0x48;
const int kTransmission = 0xbc;
const int kPerformanceEngine = 0xc0;
const int kPacketShifting = 0x71;
const int kPacketGearState = 0x74;
const int kPacketTransmissionGear = 0x78;
const int kPacketBoost = 0x7c;
const int kPacketRpm = 0x80;

/** The packet's wheel slot of a suspension wheel: the fronts in order, the rears the other way round. */
unsigned int WheelSlot(unsigned int wheel)
{
    return wheel < 2 ? (wheel & 1) : ((~wheel & 1) | 2);
}

void SetBit(unsigned int &bits, unsigned int bit, bool on)
{
    if (on)
        bits |= bit;
    else
        bits &= ~bit;
}
} // namespace

void __fastcall DrawCarServicePacket(char *self, void *, char *packet)
{
    At<unsigned char>(self, kInView) = At<unsigned char>(packet, kPacketInView);
    At<unsigned int>(self, kViewDistance) = At<unsigned int>(packet, kPacketViewDistance);
    if (void *zones = At<void *>(self, kDamageZones))
    {
        const int level = Virtual<int>(zones, 0x14);
        if (level > 0 && level <= 2)
            At<unsigned char>(packet, kPacketDamaged) = 1;
    }
    for (const unsigned int *map = kLightMaps; map[0]; map += kLightMapWords)
    {
        if (!map[kLightMapCarried])
            continue;
        void *damage = At<void *>(self, kDamage);
        if (damage && Virtual<bool, unsigned int>(damage, 4, map[0]))
        {
            At<unsigned int>(packet, kPacketBrokenLights) |= map[0];
            continue;
        }
        const bool lit = Virtual<bool, unsigned int>(At<void *>(self, kVehicle), 0x9c, map[0]);
        SetBit(At<unsigned int>(packet, kPacketLights), map[0], lit);
    }
    void *objects = At<void *>(At<void *>(self, kSimable), 4);
    char *dynamics = static_cast<char *>(
        NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(objects, kDynamicsEntityHandle));
    if (dynamics && NFS_ORIGINAL(0x48aee0, bool(__cdecl *)(void *))(dynamics + 8))
    {
        At<float>(packet, kPacketRideHeight) = 0.0f;
        At<float>(packet, kPacketRideHeightRear) = 0.0f;
    }
    else if (void *suspension = At<void *>(self, kSuspension))
    {
        const float height = Virtual<float>(suspension, 0x74);
        At<float>(packet, kPacketRideHeightRear) = height;
        At<float>(packet, kPacketRideHeight) = height;
    }
    if (void *steering = At<void *>(self, kSteering))
    {
        unsigned int gear;
        At<unsigned int>(packet, kPacketGear) = *Virtual<const unsigned int *, unsigned int *>(steering, 0x20, &gear);
        At<float>(packet, kPacketSteeringWheel) = Virtual<float>(steering, 0x14);
    }
    char *head = At<char *>(self, kHiddenParts);
    char *node = *reinterpret_cast<char **>(head);
    while (node != At<char *>(self, kHiddenParts))
    {
        NFS_ORIGINAL(0x745930, void(__thiscall *)(void *, const void *))(packet, node + 0xc);
        NFS_ORIGINAL(0x6955b0, void(__thiscall *)(char **))(&node);
    }
    if (void *suspension = At<void *>(self, kSuspension))
    {
        Virtual<void>(At<void *>(self, kSimable), 0x54);
        for (unsigned int wheel = 0; wheel < Virtual<unsigned int>(At<void *>(self, kSuspension), 8); ++wheel)
        {
            suspension = At<void *>(self, kSuspension);
            Virtual<void, unsigned int>(suspension, 0x48, wheel);
            Virtual<void, unsigned int>(At<void *>(self, kSuspension), 0x10, wheel);
            float steer = Virtual<float, unsigned int>(At<void *>(self, kSuspension), 0x58, wheel) * kTwoPi;
            if (!(steer < kPi))
                steer = kPi;
            else if (steer < -kPi)
                steer = -kPi;
            suspension = At<void *>(self, kSuspension);
            float roll = Virtual<float, unsigned int>(suspension, 0x28, wheel);
            roll -= Virtual<float, unsigned int>(suspension, 0x64, wheel);
            roll += Virtual<float, unsigned int>(suspension, 0x68, wheel);
            const float load = Virtual<float, unsigned int>(At<void *>(self, kSuspension), 0x50, wheel);
            const float compression = Virtual<float, unsigned int>(At<void *>(self, kSuspension), 0x68, wheel);
            float skid = 0.0f;
            float slip = 0.0f;
            bool onGround = false;
            if (Virtual<bool, unsigned int>(At<void *>(self, kSuspension), 0x24, wheel))
            {
                slip = fabsf(Virtual<float, unsigned int>(At<void *>(self, kSuspension), 0x38, wheel));
                const float spin = fabsf(Virtual<float, unsigned int>(At<void *>(self, kSuspension), 0x30, wheel));
                const float grip = Virtual<float, unsigned int>(At<void *>(self, kSuspension), 0x34, wheel);
                skid = spin > grip ? spin - grip : 0.0f;
                onGround = true;
            }
            const unsigned int slot = WheelSlot(wheel);
            if (wheel < 2)
                At<float>(packet, kPacketSteer + static_cast<int>(slot) * 4) = -steer;
            if (void *tyres = At<void *>(self, kTyres))
            {
                if (Virtual<int, unsigned int>(tyres, 4, wheel) == kTyreBlown)
                    At<unsigned int>(packet, kPacketBlownTyres) |= 1u << slot;
            }
            At<float>(packet, kPacketWheelSkid + static_cast<int>(slot) * 4) = skid;
            At<float>(packet, kPacketWheelLoad + static_cast<int>(slot) * 4) = compression * load;
            At<float>(packet, kPacketWheelRoll + static_cast<int>(slot) * 4) = roll;
            At<float>(packet, kPacketWheelSlip + static_cast<int>(slot) * 4) = slip;
            SetBit(At<unsigned int>(packet, kPacketWheelsOnGround), 1u << slot, onGround);
        }
    }
    if (void *engine = At<void *>(self, kEngine))
    {
        At<float>(packet, kPacketEngine) = Virtual<float>(engine, 0x2c);
        At<float>(packet, kPacketEngine + 4) = Virtual<float>(At<void *>(self, kEngine), 0x30);
        At<float>(packet, kPacketEngine + 8) = Virtual<float>(At<void *>(self, kEngine), 0x34);
    }
}

void __fastcall DrawPerformanceCarServicePacket(char *self, void *, char *packet)
{
    DrawCarServicePacket(self, nullptr, packet);
    void *engine = At<void *>(self, kPerformanceEngine);
    void *transmission = At<void *>(self, kTransmission);
    if (!engine || !transmission)
        return;
    At<int>(packet, kPacketTransmissionGear) = Virtual<int>(transmission, 4);
    At<int>(packet, kPacketGearState) = Virtual<int>(At<void *>(self, kTransmission), 0x28);
    At<unsigned char>(packet, kPacketShifting) = Virtual<unsigned char>(At<void *>(self, kPerformanceEngine), 0x20);
    if (Virtual<int>(At<void *>(self, kTransmission), 4) > 1 && !Virtual<bool>(At<void *>(self, kPerformanceView), 0x20))
        return;
    engine = At<void *>(self, kPerformanceEngine);
    const float redline = Virtual<float>(engine, 0x0c);
    const float idle = Virtual<float>(engine, 0x10);
    const float rpm = UMath::Ramp(Virtual<float>(engine, 0x04), idle, redline);
    const float boostMost = Virtual<float>(At<void *>(self, kPerformanceEngine), 0x34);
    const float boostLeast = Virtual<float>(At<void *>(self, kPerformanceEngine), 0x38);
    const float boost = Virtual<float>(At<void *>(self, kPerformanceEngine), 0x3c);
    At<float>(packet, kPacketBoost) = UMath::Ramp(boost, boostLeast, boostMost);
    At<float>(packet, kPacketRpm) = rpm;
}
