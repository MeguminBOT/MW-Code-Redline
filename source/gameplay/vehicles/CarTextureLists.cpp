/** Which textures a car uses (original: zWorld UsedCarTextureAddToTable 0x739e90, LoadedSkin::GetTextureHashes
 * 0x7528d0, CarPart::GetTextureNameHash 0x74d0d0). */
#include <string.h>

#include "game/gen/api.h"

namespace
{
const int kUsedListEntries = 0x57;
const int kRide = 8;
const int kSkinNumber = 0x14;
const int kPartAttributes = 0xa;
const unsigned short kNoAttributes = 0xffff;
const int kPackAttributeTables = 0x14;
const int kPackAttributes = 0x1c;
const unsigned int kTextureName = 0x10c98090;

struct UsedTextures
{
    unsigned int first[kUsedListEntries];
    unsigned int second[kUsedListEntries];
    int firstCount;
    int secondCount;
    unsigned char rest[0x380 - 0x2c0];
};
static_assert(sizeof(UsedTextures) == 0x380, "UsedCarTextureInfo");

struct Attribute
{
    unsigned int key;
    unsigned int value;
};

template <typename T> T &At(void *base, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}
} // namespace

int __cdecl UsedCarTextureAddToTable(unsigned int *table, int count, int room, unsigned int hash)
{
    if (!hash)
        return 0;
    for (int i = 0; i < count; ++i)
        if (table[i] == hash)
            return 0;
    if (count >= room)
        return count;
    table[count] = hash;
    return 1;
}

int LoadedSkin::GetTextureHashes(unsigned int *out, int room, int first)
{
    memset(out, 0, room * sizeof(*out));
    UsedTextures used;
    GetUsedCarTextureInfo(reinterpret_cast<UsedCarTextureInfo *>(&used), At<RideInfo *>(this, kRide),
                          At<int>(this, kSkinNumber));
    const unsigned int *from = first ? used.first : used.second;
    const int count = first ? used.firstCount : used.secondCount;
    memcpy(out, from, count * sizeof(*out));
    return count;
}

unsigned int CarPart::GetTextureNameHash()
{
    const unsigned short table = At<unsigned short>(this, kPartAttributes);
    if (table == kNoAttributes)
        return 0;
    void *pack = MasterCarPartPack;
    const short *entries = At<const short *>(pack, kPackAttributeTables) + table;
    const Attribute *attributes = At<const Attribute *>(pack, kPackAttributes);
    for (int i = 0; i < entries[0]; ++i)
    {
        const Attribute *a = &attributes[entries[1 + i]];
        if (a->key == kTextureName)
            return a->value;
    }
    return 0;
}
