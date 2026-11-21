/** Texture names of vinyl layers and their masks (original: zWorld GetVinylLayerHash, GetVinylLayerMaskHash). */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
const int kCarTypeInfoSize = 0xd0;
const int kBaseModelName = 0x10;
const int kRideParts = 0x17c;

const char *BaseModelName(int type)
{
    return *reinterpret_cast<const char *const *>(0x9b09d8) + type * kCarTypeInfoSize + kBaseModelName;
}

const char *VinylTextureName(CarPart *part)
{
    const char *attribute = static_cast<const char *>(
        NFS_ORIGINAL(0x739730, void *(__thiscall *)(CarPart *, unsigned int, int))(part, bStringHash("TEXTURE"), 0));
    if (!attribute)
        return nullptr;
    const int index = *reinterpret_cast<const int *>(attribute + 4);
    return *reinterpret_cast<const char *const *>(0x9b09bc) + index * 4;
}

bool Precomposited()
{
    return *reinterpret_cast<const int *>(0x9b09fc) != 0;
}
} // namespace

unsigned int GetVinylLayerHash(CarPart *part, CarType type, int layer)
{
    const char *vinyl = VinylTextureName(part);
    if (!vinyl)
        return 0;
    char name[0x40];
    bStrCpy(name, BaseModelName(static_cast<int>(type)));
    bStrCat(name, name, Precomposited() || layer == 2 ? "_PRECOM_" : "_");
    bStrCat(name, name, vinyl);
    return bStringHash(name);
}

unsigned int GetVinylLayerMaskHash(RideInfo *ride, int layer)
{
    CarPart *part = *reinterpret_cast<CarPart **>(reinterpret_cast<char *>(ride) + kRideParts + layer * 4);
    if (!part)
        return 0;
    const char *vinyl = VinylTextureName(part);
    if (!vinyl)
        return 0;
    char name[0x40];
    bStrCpy(name, BaseModelName(*reinterpret_cast<const int *>(ride)));
    bStrCat(name, name, "_");
    bStrCat(name, name, vinyl);
    bStrCat(name, name, "_MASK");
    return bStringHash(name);
}
