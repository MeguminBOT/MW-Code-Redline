/**
 * Texture creation (original: zPlatform TextureInfoPlatInterface::Init 0x6ddd90). A texture pack's image becomes a
 * texture: palettized (P8) images are expanded to A8R8G8B8, the Video options' texture detail drops top mip levels
 * (except for vinyls), and each texture made is linked into the platform texture list. With Direct3D 9 this runs the
 * original code; in Vulkan mode the texture is the renderer's. Also the image lock (0x6bed80, 0x6bedb0) that car skin
 * and wheel compositing use to edit textures on the CPU.
 */
#include "game/gen/api.h"
#include "engine/resources/streamprofile.h"
#include "game/platform/Platform.h"
#include "legacy/link.h"
#include "core/log.h"
#include "engine/rendering/surface.h"
#include "engine/rendering/vulkan/renderer.h"

/** Textures to release over the next frames (a bPList the frame drains), and the list of created textures. */
extern "C" char unk_987F08[];
extern "C" TextureInfoPlatInfo *dword_987EC0[2];
/** Set, no textures are created. */
extern "C" unsigned char dword_982AE8;
/** Top mip levels the texture detail option drops (0 to 2). */
extern "C" int dword_982AE0;

namespace
{
const int kFormatP8 = 41;
/** bStringHash("VINYL"): vinyl packs keep every mip level. */
const unsigned int kVinylPackHash = 0x3E871F1;

unsigned int PackHash(const TexturePack *pack)
{
    return *reinterpret_cast<const unsigned int *>(reinterpret_cast<const char *>(pack) + 0x10);
}

void Unlink(TextureInfoPlatInfo *plat)
{
    NFS_ORIGINAL(0x6d99c0, void(__thiscall *)(void *, void *))(unk_987F08, plat->Texture);
    plat->Texture = nullptr;
    plat->Prev->Next = plat->Next;
    plat->Next->Prev = plat->Prev;
}
} // namespace

void TextureInfoPlatInterface::Init()
{
    nfs::streamprofile::Scope profile(nfs::streamprofile::kTextureImages);
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    TextureInfo *info = static_cast<TextureInfo *>(this);
    TextureInfoPlatInfo *plat = PlatInfo;
    if (plat->Texture)
        Unlink(plat);
    if (!bStrNCmp(info->Name, "SFX_TRACKBARRIER", 16) && info->unknown_54)
        info->unknown_54 = 2;
    const unsigned char *image = static_cast<const unsigned char *>(info->ImageData);
    if (!image)
        return;

    int format = plat->Format;
    int levelBytes = info->BaseImageSize;
    unsigned int *expanded = nullptr;
    if (format == kFormatP8 && !info->PaletteData)
    {
        static int logged;
        if (++logged <= 20)
            nfs::Log("textures: %s (%08x) is palettized without a palette; not made", info->Name, info->NameHash);
        return;
    }
    if (format == kFormatP8)
    {
        const unsigned int *palette = static_cast<const unsigned int *>(info->PaletteData);
        expanded = static_cast<unsigned int *>(::operator new(static_cast<size_t>(info->ImageSize) * 4));
        unsigned int *out = expanded;
        for (int level = 0; level < info->NumMipMapLevels; ++level)
            for (int n = (info->Width >> level) * (info->Height >> level); n > 0; --n)
                *out++ = palette[*image++];
        image = reinterpret_cast<const unsigned char *>(expanded);
        levelBytes *= 4;
        format = D3DFMT_A8R8G8B8;
        info->BitsPerPixel = 32;
    }
    if (dword_982AE8)
    {
        plat->Texture = nullptr;
        ::operator delete(expanded);
        return;
    }
    // A normal map with a height in its alpha (engine/rendering/surface.h) in place of the pack's own, level for level.
    if (const nfs::surface::Image *relief = nfs::surface::Replacement(info->NameHash))
    {
        if (relief->sourceBaseBytes == static_cast<unsigned int>(levelBytes) &&
            relief->levels >= static_cast<unsigned int>(info->NumMipMapLevels))
        {
            image = relief->data;
            levelBytes = static_cast<int>(relief->baseBytes);
            format = static_cast<int>(relief->format);
        }
    }

    int skip = 0;
    if (dword_982AE0 > 0 && !(info->Pack && PackHash(info->Pack) == kVinylPackHash))
    {
        if (info->NumMipMapLevels > dword_982AE0)
            skip = dword_982AE0;
        else if (info->NumMipMapLevels > 1)
            skip = info->NumMipMapLevels - 1;
        if (skip)
        {
            info->Width = static_cast<short>(info->Width / (skip + skip));
            info->Height = static_cast<short>(info->Height / (skip + skip));
        }
    }
    const int levels = info->NumMipMapLevels - skip;
    static const bool debug = nfs::vk::HasArgument("-texdebug");
    if (debug && levels == 1 && info->Width >= 64 && info->Height >= 64)
    {
        static int logged;
        if (++logged <= 200)
            nfs::Log("textures: %s %dx%d format %d has no mip levels", info->Name, info->Width, info->Height, format);
    }
    nfs::vk::Texture *texture = r->CreateTexture(static_cast<uint32_t>(info->Width), static_cast<uint32_t>(info->Height),
                                                 static_cast<uint32_t>(levels), static_cast<uint32_t>(format), false);
    plat->Texture = texture;
    if (!texture)
    {
        ::operator delete(expanded);
        return;
    }
    TextureInfoPlatInfo *tail = dword_987EC0[1];
    tail->Next = plat;
    dword_987EC0[1] = plat;
    plat->Prev = tail;
    plat->Next = reinterpret_cast<TextureInfoPlatInfo *>(dword_987EC0);

    // Image data holds every level at a quarter of the one before, whatever the texture's shape.
    for (; skip > 0; --skip)
    {
        image += levelBytes;
        levelBytes /= 4;
    }
    r->UploadTextureLevels(texture, image, static_cast<uint32_t>(levelBytes), static_cast<uint32_t>(levels));
    ::operator delete(expanded);
}

void *TextureInfoPlatInterface::LockImage(TextureLockType type)
{
    return nfs::vk::g_renderer->LockLevel(static_cast<nfs::vk::Texture *>(PlatInfo->Texture), 0);
}

void TextureInfoPlatInterface::UnlockImage(void *image)
{
    nfs::vk::g_renderer->UnlockLevel(static_cast<nfs::vk::Texture *>(PlatInfo->Texture), 0);
}
