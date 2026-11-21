/**
 * Small PC platform pieces of the renderer (original: zPlatform/zSim): the no-op console hooks (eWaitUntilRenderingDone,
 * eGetRenderTargetTextureInfo, the palette swizzles), a solid's platform data unload (eSolidPlatInterface::
 * UnloaderPlatChunks), a texture's release (TextureInfoPlatInterface::Close), an animated texture's frame switch
 * (TextureInfoPlatInterface::SetAnimData) and the per-frame render services (RenderConn::UpdateServices).
 *
 * Buffers and textures go back through the same queues as in the original (0x9877e8 vertex buffers, 0x987f08
 * textures), which the Vulkan renderer serves as well.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x6bee20, SetTextureAnimData)

void __fastcall SetTextureAnimData(TextureInfoPlatInterface *texture, void *, void *frame);

namespace
{
template <typename T> T &At(void *p, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(p) + offset);
}

struct Node
{
    Node *next;
    Node *prev;
};

void Unlink(Node *n)
{
    n->prev->next = n->next;
    n->next->prev = n->prev;
}

const int kStripSize = 0x68;

/** Whether animated textures switch frames (0x9017e8). */
int AnimatedTextures()
{
    return *reinterpret_cast<int *>(0x9017e8);
}
} // namespace

void __cdecl eWaitUntilRenderingDone()
{
}

TextureInfo *__cdecl eGetRenderTargetTextureInfo(int)
{
    return nullptr;
}

void __cdecl eSwizzle8bitPalette(unsigned int *)
{
}

void __cdecl eUnSwizzle8bitPalette(unsigned int *)
{
}

int eSolidPlatInterface::UnloaderPlatChunks(bChunk *)
{
    char *info = *reinterpret_cast<char **>(this);
    // The index buffer (released through its COM-style vtable), then each strip's vertex buffer.
    if (void *indices = At<void *>(info, 0x28))
    {
        typedef void(__stdcall * Release)(void *);
        (*reinterpret_cast<Release **>(indices))[2](indices);
        At<void *>(info, 0x28) = nullptr;
    }
    for (int i = 0; i < At<int>(info, 0x10); ++i)
    {
        char *strip = At<char *>(info, 0x14) + i * kStripSize;
        if (void *vertices = At<void *>(strip, 0x54))
        {
            if (At<unsigned int>(strip, 0x50))
                NFS_ORIGINAL(0x6d9980, void(__thiscall *)(void *, void *))(reinterpret_cast<void *>(0x9877e8), vertices);
            At<void *>(At<char *>(info, 0x14) + i * kStripSize, 0x54) = nullptr;
        }
    }
    Unlink(reinterpret_cast<Node *>(info));
    *reinterpret_cast<char **>(this) = nullptr;
    return 1;
}

void TextureInfoPlatInterface::Close()
{
    TextureInfoPlatInfo *plat = PlatInfo;
    if (!plat->Texture)
        return;
    NFS_ORIGINAL(0x6d99c0, void(__thiscall *)(void *, void *))(reinterpret_cast<void *>(0x987f08), plat->Texture);
    plat->Texture = nullptr;
    plat->Prev->Next = plat->Next;
    plat->Next->Prev = plat->Prev;
}

/** TextureInfoPlatInterface::SetAnimData (thiscall): an animation frame's texture becomes the one drawn. */
void __fastcall SetTextureAnimData(TextureInfoPlatInterface *texture, void *, void *frame)
{
    if (AnimatedTextures())
        texture->PlatInfo->Texture = *static_cast<void **>(frame);
}

namespace RenderConn
{
void __cdecl UpdateServices(float dt)
{
    SmackableRender_Service(dt);
    CarRender_Service(dt);
    gEmitterSystem.Update(dt);
    TheAnimPlayer.UpdateTime(dt);
}
} // namespace RenderConn
