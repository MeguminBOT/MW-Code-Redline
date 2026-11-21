/**
 * The front end's garage environment map (original: zPlatform EnvMapTextureLoadedCallback 0x6bf5a0). Each of the six
 * face textures of the garage (<garage prefix>_<face>) calls it when loaded; from the sixth on, all six are copied
 * into the environment cube. With Direct3D 9 this runs the original code; in Vulkan mode the copy runs on the GPU,
 * and only once the cube exists (environment maps come with milestone 3, docs/VULKAN.md).
 */
#include "game/gen/api.h"
#include "game/platform/Platform.h"
#include "legacy/link.h"
#include "engine/rendering/vulkan/renderer.h"

NFS_REPLACE(0x6bf5a0, EnvMapTextureLoadedCallback)

/** Face textures loaded so far. */
extern "C" unsigned char dword_93E86C;
/** The cube face names, in D3DCUBEMAP_FACES order. */
extern "C" const char *off_8F9314[6];
/** The environment map: an IDirect3DCubeTexture9 render target, or the renderer's cube texture in Vulkan mode. */
extern "C" void *dword_982A64;

extern "C" void __cdecl EnvMapTextureLoadedCallback()
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    if (++dword_93E86C < 6)
        return;
    for (unsigned int face = 0; face < 6; ++face)
    {
        char name[64];
        const eGarageType garage = FEManager::Get()->GetGarageType();
        bSPrintf(name, "%s_%s", FEManager::Get()->GetGaragePrefixFromType(garage), off_8F9314[face]);
        TextureInfo *texture = GetTextureInfo(bStringHash(name), 1, 0);
        nfs::vk::Texture *cube = static_cast<nfs::vk::Texture *>(dword_982A64);
        if (cube && texture && texture->PlatInfo->Texture)
            r->CopyLevel(static_cast<nfs::vk::Texture *>(texture->PlatInfo->Texture), 0, 0, cube, face, 0);
    }
}
