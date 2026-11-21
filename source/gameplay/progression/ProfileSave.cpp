/**
 * A profile's save (original: zFe2 UserProfile::SaveToBuffer 0x58fd10) with the rebuild's settings of the profile
 * (difficulty, realistic car performance and the rest: gameplay/progression/profileoptions.h) in the first bytes of its data, which
 * the original zeroes and never reads. Not in the original game.
 */
#include "game/gen/api.h"
#include "legacy/link.h"
#include "gameplay/progression/profileoptions.h"

NFS_REPLACE(0x58fd10, UserProfile::SaveToBuffer)

void UserProfile::SaveToBuffer(void *buffer, int size)
{
    NFS_ORIGINAL(0x58fd10, void(__thiscall *)(UserProfile *, void *, int))(this, buffer, size);
    if (buffer && size >= nfs::profileoptions::kSaveBlockSize)
        nfs::profileoptions::Saving(m_aProfileName, static_cast<unsigned char *>(buffer));
}
