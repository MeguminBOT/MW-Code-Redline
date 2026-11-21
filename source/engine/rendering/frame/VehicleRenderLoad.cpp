/**
 * Loading and unloading a vehicle's render data (original: zWorld VehicleRenderConn::Load 0x75d220, SetupLoading
 * 0x75d1a0, Unload 0x75c4d0).
 *
 * VehicleRenderConn: +0x28 its load state (0 none, 1 loading, 2 loaded, 3 drawn), +0x2c its car type (-1 none), +0x30
 * its world reference, +0x40 its RideInfo (0x310 bytes; +4 its loaded flag, +0x304 its car loader handle), +0x44 its
 * CarRenderInfo, +0x68 its composite skin slot (1-4, or 5-12 with precomposited vinyls; 0 none). Skin slots free for
 * use are the bits of mOpenSkinSlots.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x75d220, VehicleRenderConnLoad)
NFS_REPLACE(0x75d1a0, VehicleRenderConnSetupLoading)
NFS_REPLACE(0x75c4d0, VehicleRenderConnUnload)

bool __fastcall VehicleRenderConnLoad(char *self, void *, unsigned int world, int usage, bool commit,
                                      const void *customization);
void __fastcall VehicleRenderConnSetupLoading(char *self, void *, bool commit);
void __fastcall VehicleRenderConnUnload(char *self, void *);

namespace
{
template <typename T> T &At(void *base, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}

const int kLoadState = 0x28;
const int kCarType = 0x2c;
const int kWorldReference = 0x30;
const int kRide = 0x40;
const int kInfo = 0x44;
const int kSkinSlot = 0x68;
const int kRideBytes = 0x310;
const int kRideLoaded = 4;
const int kRideLoaderHandle = 0x304;
unsigned int *const kOpenSkinSlots = reinterpret_cast<unsigned int *>(0x903690);
const int *const kUsePrecompositeVinyls = reinterpret_cast<const int *>(0x9b09fc);
void *const kCarLoader = reinterpret_cast<void *>(0x9b2c00);

void RideInit(void *ride, int carType, int usage)
{
    NFS_ORIGINAL(0x739a70, void(__thiscall *)(void *, int, int, int, int))(ride, carType, usage, 0, 0);
}

void Free(void *p)
{
    NFS_ORIGINAL(0x652b00, void(__cdecl *)(void *))(p);
}
} // namespace

bool __fastcall VehicleRenderConnLoad(char *self, void *, unsigned int world, int usage, bool commit,
                                      const void *customization)
{
    if (At<void *>(self, kInfo) || At<int>(self, kCarType) == -1)
        return false;
    NFS_ORIGINAL(0x7869c0, void(__thiscall *)(void *, unsigned int))(self + kWorldReference, world);
    void *ride = NFS_ORIGINAL(0x652ad0, void *(__cdecl *)(unsigned int))(kRideBytes);
    if (ride)
        RideInit(ride, -1, 0);
    At<void *>(self, kRide) = ride;
    RideInit(ride, At<int>(self, kCarType), usage);
    if (NFS_ORIGINAL(0x748230, bool(__cdecl *)(int))(At<int>(self, kCarType)))
    {
        const int first = *kUsePrecompositeVinyls ? 5 : 1;
        const int last = *kUsePrecompositeVinyls ? 12 : 4;
        for (int slot = first; slot <= last; ++slot)
        {
            const unsigned int bit = 1u << (slot - 1);
            if (bit & *kOpenSkinSlots)
            {
                *kOpenSkinSlots &= ~bit;
                At<int>(self, kSkinSlot) = slot;
                break;
            }
        }
        if (At<int>(self, kSkinSlot))
            NFS_ORIGINAL(0x747f10, void(__thiscall *)(void *, int))(At<void *>(self, kRide), At<int>(self, kSkinSlot));
    }
    // The rebuild's (gameplay/vehicles/RideParts.cpp): a car's kit pieces and trim, and Debug.RideParts.
    RideInfo *built = At<RideInfo *>(self, kRide);
    built->SetStockParts();
    if (customization)
        static_cast<const FECustomizationRecord *>(customization)->WriteRecordIntoRide(built);
    VehicleRenderConnSetupLoading(self, 0, commit);
    return true;
}

void __fastcall VehicleRenderConnSetupLoading(char *self, void *, bool commit)
{
    char *ride = At<char *>(self, kRide);
    At<int>(ride, kRideLoaderHandle) = NFS_ORIGINAL(0x75c6c0, int(__thiscall *)(void *, void *))(kCarLoader, ride);
    At<char>(At<char *>(self, kRide), kRideLoaded) = 0;
    if (commit)
    {
        void *event = NFS_ORIGINAL(0x627400, void *(__cdecl *)(unsigned int))(8);
        if (event)
            NFS_ORIGINAL(0x621280, void(__thiscall *)(void *))(event);
    }
    At<int>(self, kLoadState) = 1;
}

void __fastcall VehicleRenderConnUnload(char *self, void *)
{
    if (!At<int>(self, kLoadState))
        return;
    if (char *ride = At<char *>(self, kRide))
    {
        NFS_ORIGINAL(0x75bad0, void(__thiscall *)(void *, int))(kCarLoader, At<int>(ride, kRideLoaderHandle));
        Free(At<void *>(self, kRide));
        At<void *>(self, kRide) = nullptr;
        if (const int slot = At<int>(self, kSkinSlot))
        {
            *kOpenSkinSlots |= 1u << (slot - 1);
            At<int>(self, kSkinSlot) = 0;
        }
    }
    NFS_ORIGINAL(0x781e70, void(__thiscall *)(void *))(self + kWorldReference);
    if (CarRenderInfo *info = At<CarRenderInfo *>(self, kInfo))
    {
        NFS_ORIGINAL(0x7556c0, void(__thiscall *)(void *))(info);
        Free(info);
        At<void *>(self, kInfo) = nullptr;
    }
    At<int>(self, kLoadState) = 0;
}
