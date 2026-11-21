#pragma once

class CarPart;
class FECustomizationRecord;

namespace nfs::parts
{
/** The part saved in a slot of a customization record, or null. */
CarPart *Installed(const FECustomizationRecord *record, int slot);
} // namespace nfs::parts
