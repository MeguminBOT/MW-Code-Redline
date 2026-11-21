/** A vehicle's glare flags and speedometer value (original: zPhysics PVehicle::GlareOn, GlareOff, IsGlareOn,
 * GetSpeedometer). */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x669360, VehicleGlareOn)
NFS_REPLACE(0x669380, VehicleGlareOff)
NFS_REPLACE(0x6881c0, VehicleIsGlareOn)
NFS_REPLACE(0x6881a0, VehicleGetSpeedometer)

namespace
{
const int kGlare = 0x9c;
const int kSpeedometer = 0x78;

unsigned int &Glare(void *vehicle)
{
    return *reinterpret_cast<unsigned int *>(static_cast<char *>(vehicle) + kGlare);
}
} // namespace

void __fastcall VehicleGlareOn(void *vehicle, void *, unsigned int glare)
{
    Glare(vehicle) |= glare;
}

void __fastcall VehicleGlareOff(void *vehicle, void *, unsigned int glare)
{
    Glare(vehicle) &= ~glare;
}

int __fastcall VehicleIsGlareOn(void *vehicle, void *, unsigned int glare)
{
    return (Glare(vehicle) & glare) != 0 ? 1 : 0;
}

float __fastcall VehicleGetSpeedometer(void *vehicle)
{
    return *reinterpret_cast<const float *>(static_cast<char *>(vehicle) + kSpeedometer);
}
