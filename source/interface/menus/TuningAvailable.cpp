/**
 * The aerodynamics tuning for a car that comes with a spoiler (original: CustomTuningScreen::IsTuningAvailable 0x593f20).
 * The original offers it once a bought spoiler or body kit differs from the stock one, which leaves a car whose stock
 * parts already have a wing without it. tools/carbonmerge.py marks a stock SPOILER part that has a model, and a body
 * with a wing of its own, with INCLUDED_SPOILER.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
/** Physics::Tunings: aerodynamics. */
const int kAerodynamics = 4;
const int kSlotSpoiler = 44;
const int kCustomizationHandle = 0x10;
const unsigned char kCustomizationHandles = 0x4c;
} // namespace

bool CustomTuningScreen::IsTuningAvailable(FEPlayerCarDB *database, FECarRecord *record, Physics::Tunings::Path path)
{
    if (NFS_ORIGINAL(0x593f20, bool(__cdecl *)(FEPlayerCarDB *, FECarRecord *, Physics::Tunings::Path))(database, record,
                                                                                                        path))
        return true;
    if (static_cast<int>(path) != kAerodynamics || !record ||
        reinterpret_cast<const unsigned char *>(record)[kCustomizationHandle] >= kCustomizationHandles)
        return false;
    CarPart *stock = FindPartWithLevel(record->GetType(), static_cast<CAR_SLOT_ID>(kSlotSpoiler), 0);
    static const unsigned int included = bStringHash("INCLUDED_SPOILER");
    return stock && stock->GetAppliedAttributeIParam(included, 0) != 0;
}
