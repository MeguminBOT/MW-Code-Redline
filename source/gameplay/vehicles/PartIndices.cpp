/** A part's index in the part table and back (original: CarPartDatabase::GetPartIndex 0x747bb0, GetCarPartByIndex
 * 0x747c00). */
#include "game/gen/api.h"

namespace
{
const int kPartSize = 0xe;
const int kPackParts = 0x34;
const int kPackCount = 0x38;

struct PackNode
{
    PackNode *next;
};

inline const unsigned char *PartsOf(const PackNode *pack)
{
    return *reinterpret_cast<const unsigned char *const *>(reinterpret_cast<const unsigned char *>(pack) + kPackParts);
}

inline int CountOf(const PackNode *pack)
{
    return *reinterpret_cast<const int *>(reinterpret_cast<const unsigned char *>(pack) + kPackCount);
}
} // namespace

int CarPartDatabase::GetPartIndex(CarPart *part)
{
    const PackNode *head = reinterpret_cast<const PackNode *>(this);
    int first = 0;
    for (const PackNode *pack = head->next; pack != head; pack = pack->next)
    {
        const int offset = static_cast<int>(reinterpret_cast<const unsigned char *>(part) - PartsOf(pack)) / kPartSize;
        const int count = CountOf(pack);
        if (offset >= 0 && offset < count)
            return first + offset;
        first += count;
    }
    return -1;
}

CarPart *CarPartDatabase::GetCarPartByIndex(int index)
{
    if (index < 0)
        return nullptr;
    const PackNode *head = reinterpret_cast<const PackNode *>(this);
    for (const PackNode *pack = head->next; pack != head; pack = pack->next)
    {
        const int count = CountOf(pack);
        if (index < count)
            return reinterpret_cast<CarPart *>(const_cast<unsigned char *>(PartsOf(pack)) + index * kPartSize);
        index -= count;
    }
    return nullptr;
}
