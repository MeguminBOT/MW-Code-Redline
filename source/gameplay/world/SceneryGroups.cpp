/** Scenery groups and barriers switched on and off (original: zMisc EnableBarrierSceneryGroup,
 * TrackLoader::InitTopologyAndSceneryGroups, CloseTopologyAndSceneryGroups, RedoTopologyAndSceneryGroups,
 * ResetRenderEggs). */
#include <string.h>

#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x659640, CloseTopologyAndSceneryGroups)

namespace
{
const char kDoorGroup[] = "SCENERY_GROUP_DOOR";
unsigned char *const kTopologyBits = reinterpret_cast<unsigned char *>(0x9a9f80);
const int kTopologyBytes = 0x400;
const int kStompers = 7;

void EnableGroup(const char *name, bool flipped)
{
    const unsigned int hash = bStringHash(name);
    TheVisibleSectionManager.EnableGroup(hash);
    EnableSceneryGroup(hash, flipped);
    TheTrackPathManager.EnableBarriers(name);
}
} // namespace

void __cdecl CloseTopologyAndSceneryGroups()
{
    TheTrackPathManager.DisableAllBarriers();
    memset(kTopologyBits, 0, kTopologyBytes);
    DisableAllSceneryGroups();
}

void EnableBarrierSceneryGroup(const char *name, bool flipped)
{
    if (VisibleSectionManager::GetGroupInfo(name))
        EnableGroup(name, flipped);
}

void TrackLoader::InitTopologyAndSceneryGroups()
{
    if (FindSceneryGroup(bStringHash(kDoorGroup)) && VisibleSectionManager::GetGroupInfo(kDoorGroup))
        EnableGroup(kDoorGroup, false);
    CloseAllGarageDoors();
}

void RedoTopologyAndSceneryGroups()
{
    CloseTopologyAndSceneryGroups();
    reinterpret_cast<TrackLoader *>(0x8f834c)->InitTopologyAndSceneryGroups();
}

void ResetRenderEggs()
{
    char *stomper = reinterpret_cast<char *>(pStomper);
    if (!stomper)
        return;
    int &active = *reinterpret_cast<int *>(stomper + 8);
    if (active > 0 && active < kStompers)
        reinterpret_cast<StompEntry **>(&StomperTable)[active]->ClearAll(*reinterpret_cast<void **>(stomper + 4));
    active = 0;
}
