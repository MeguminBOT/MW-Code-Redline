struct TextureInfoPlatInfo *Next;
struct TextureInfoPlatInfo *Prev;
int unknown_08[3];
/** D3DFORMAT the texture is created with (P8 is expanded to A8R8G8B8). */
int Format;
/** IDirect3DTexture9 with Direct3D 9; the renderer's nfs::vk::Texture in Vulkan mode. */
void *Texture;
