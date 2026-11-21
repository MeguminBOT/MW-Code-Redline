/** Front-end image objects: construction, destruction and cloning (original: zFEng FEImage). */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x5be510, FEImageConstruct)
NFS_REPLACE(0x5be530, FEImageDestroy)
NFS_REPLACE(0x5be540, FEImageDelete)
NFS_REPLACE(0x5c44b0, FEImageClone)

namespace
{
void *const kFEImageVtable = reinterpret_cast<void *>(0x8a2e34);
const int kType = 0x18;
const int kImageType = 1;
const int kImageFlags = 0x5c;
const unsigned int kImageSize = 0x60;

template <typename T> T &At(void *base, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}
} // namespace

void *__fastcall FEImageConstruct(void *image)
{
    NFS_ORIGINAL(0x5bc050, void *(__thiscall *)(void *))(image);
    At<void *>(image, 0) = kFEImageVtable;
    At<int>(image, kType) = kImageType;
    At<int>(image, kImageFlags) = 0;
    return image;
}

void __fastcall FEImageDestroy(void *image)
{
    At<void *>(image, 0) = kFEImageVtable;
    NFS_ORIGINAL(0x5bc0c0, void(__thiscall *)(void *))(image);
}

void *__fastcall FEImageDelete(void *image, void *, unsigned int flags)
{
    FEImageDestroy(image);
    if (flags & 1)
        NFS_ORIGINAL(0x652b00, void(__cdecl *)(void *))(image);
    return image;
}

void *__fastcall FEImageClone(void *image, void *, bool reference)
{
    void *copy = NFS_ORIGINAL(0x652ac0, void *(__cdecl *)(unsigned int))(kImageSize);
    if (!copy)
        return nullptr;
    NFS_ORIGINAL(0x5c31a0, void *(__thiscall *)(void *, void *, bool))(copy, image, reference);
    At<void *>(copy, 0) = kFEImageVtable;
    At<int>(copy, kImageFlags) = At<int>(image, kImageFlags);
    return copy;
}
