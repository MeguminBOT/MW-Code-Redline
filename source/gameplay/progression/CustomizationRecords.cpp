/** Saved part indices, read unsigned (original: FECustomizationRecord::GetInstalledPart 0x56f250, SetInstalledPart
 * 0x56f270, WriteRideIntoRecord 0x56f2f0). */
#include "game/gen/api.h"
#include "gameplay/progression/parts.h"

namespace
{
const int kSlots = 0x8B;
const unsigned short kNoPart = 0xFFFF;

inline unsigned short *Indices(FECustomizationRecord *record)
{
    return reinterpret_cast<unsigned short *>(record);
}

inline const unsigned short *Indices(const FECustomizationRecord *record)
{
    return reinterpret_cast<const unsigned short *>(record);
}

inline unsigned short IndexOf(CarPart *part)
{
    return part ? static_cast<unsigned short>(CarPartDB.GetPartIndex(part)) : kNoPart;
}
} // namespace

namespace nfs::parts
{
CarPart *Installed(const FECustomizationRecord *record, int slot)
{
    const unsigned short index = Indices(record)[slot];
    return index == kNoPart ? nullptr : CarPartDB.GetCarPartByIndex(index);
}
} // namespace nfs::parts

CarPart *FECustomizationRecord::GetInstalledPart(CarType, int slot) const
{
    return nfs::parts::Installed(this, slot);
}

void FECustomizationRecord::SetInstalledPart(int slot, CarPart *part)
{
    Indices(this)[slot] = IndexOf(part);
}

void FECustomizationRecord::WriteRideIntoRecord(const RideInfo *ride)
{
    unsigned short *indices = Indices(this);
    for (int slot = 0; slot < kSlots; ++slot)
        indices[slot] = IndexOf(ride->GetPart(slot));
}
