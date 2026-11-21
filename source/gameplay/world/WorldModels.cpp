/** World models' placement and smokeable attributes (original: zGameplay WorldModel::SetMatrix, zPhysics
 * SmokeableSpawner::FindAttributes, Init). */
#include <string.h>

#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x66b690, SmokeableSpawnerInit)

namespace
{
const int kHasMatrix = 0x28;
const int kNode = 0x30;
const int kMatrix = 0x40;
const int kNodeMatrix = 0x60;
const int kNodeDirty = 0xf4;
const unsigned int kSmackableClass = 0xce70d7db;

void **SmackableClass()
{
    return reinterpret_cast<void **>(0x926180);
}
} // namespace

void WorldModel::SetMatrix(bMatrix4 *matrix)
{
    char *self = reinterpret_cast<char *>(this);
    if (!matrix)
    {
        self[kHasMatrix] = 0;
        return;
    }
    self[kHasMatrix] = 1;
    char *node = *reinterpret_cast<char **>(self + kNode);
    if (!node)
    {
        memcpy(self + kMatrix, matrix, sizeof(bMatrix4));
        return;
    }
    memcpy(node + kNodeMatrix, matrix, sizeof(bMatrix4));
    if (!*reinterpret_cast<int *>(node + kNodeDirty))
        reinterpret_cast<SpaceNode *>(node)->ReallySetDirty();
}

const Attrib::Collection *SmokeableSpawner::FindAttributes(UCrc32 key)
{
    return NFS_ORIGINAL(0x455960, const Attrib::Collection *(__thiscall *)(void *, UCrc32))(*SmackableClass(), key);
}

void __cdecl SmokeableSpawnerInit()
{
    *SmackableClass() =
        NFS_ORIGINAL(0x455bc0, void *(__thiscall *)(void *, unsigned int))(Attrib::Database::sThis, kSmackableClass);
}
