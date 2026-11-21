/**
 * The career's one-time cash bonus, for every career (original: zFe MemoryCard::StartListingOldSaveFiles 0x5268a0, zFe2
 * CareerSettings::AwardOneTimeCashBonus 0x56d7c0).
 *
 * The first time a career reaches the safehouse, the original looked for Need for Speed Underground 2 saves in the
 * user's documents (MemoryCard::ListOldSaveFilesPC 0x518b40) and, finding more than two, showed a message and added
 * kBonus to the career's cash. Now there is no search and no message, and every career gets the cash: the listing
 * ends at once as if it had found nothing, and the award adds the bonus either way (it still marks the career as
 * awarded, so it comes once).
 */
#include "game/gen/api.h"
#include "core/log.h"

namespace
{
/** CareerSettings: flags (bit 1: the bonus has been awarded) and cash. */
const int kCareerFlags = 0x4;
const int kCareerCash = 0xc;
const unsigned int kAwarded = 2;
const int kBonus = 10000;
} // namespace

void MemoryCard::StartListingOldSaveFiles()
{
    EndListingOldSaveFiles();
}

void CareerSettings::AwardOneTimeCashBonus(bool)
{
    unsigned char *settings = reinterpret_cast<unsigned char *>(this);
    *reinterpret_cast<unsigned int *>(settings + kCareerFlags) |= kAwarded;
    *reinterpret_cast<int *>(settings + kCareerCash) += kBonus;
    nfs::Log("career: the one-time bonus of %d", kBonus);
}
