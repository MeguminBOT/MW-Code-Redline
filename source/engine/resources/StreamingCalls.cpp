/**
 * The solid and texture streaming front doors (original: zEcstasy): each goes to its pack loader
 * (StreamingSolidPackLoader or StreamingTexturePackLoader), plus the texture loader's per-entry callbacks that attach
 * a streamed texture to its pack and take it off again.
 *
 * A streamed texture entry ends with its TextureInfo (0x9c bytes before the end of the uncompressed data) and its
 * platform info (0x20 before the end); the pack's header (eStreamingPack +0x40) holds the TexturePack at +0x70.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x505340, FindSolidAnywhere)
NFS_REPLACE(0x507740, SolidUnloadingStreamingPack)
NFS_REPLACE(0x5032b0, TextureUnloadedStreamingEntryCallback)

void __fastcall UnloadStreamingEntries(eStreamPackLoader *loader, void *, const unsigned int *hashes, int count);

namespace
{
template <typename T> T &At(void *p, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(p) + offset);
}

/** The streamed texture's TextureInfo, at the end of its entry's data. */
char *EntryTexture(bChunk *chunk, eStreamingEntry *entry)
{
    return reinterpret_cast<char *>(chunk) + At<int>(entry, 0xc) - 0x9c;
}

TexturePack *PackOf(eStreamingPack *pack)
{
    return At<TexturePack *>(At<char *>(pack, 0x40), 0x70);
}

/** Where a texture's data ends: its palette's end when it has one (+0x34 not -1), else data plus size. */
int DataEnd(char *info)
{
    const int palette = At<int>(info, 0x34);
    return palette != -1 ? At<int>(info, 0x3c) + palette : At<int>(info, 0x38) + At<int>(info, 0x30);
}
} // namespace

void __cdecl eUnloadAllStreamingTextures(const char *name)
{
    StreamingTexturePackLoader.UnloadAllStreamingEntries(name);
}

/** eFindSolid(hash) in every solid list. */
eSolid *__cdecl FindSolidAnywhere(unsigned int hash)
{
    return eFindSolid(hash, nullptr);
}

int __cdecl eUnloadStreamingSolidPack(const char *name)
{
    return StreamingSolidPackLoader.DeleteStreamingPack(name);
}

int __cdecl eUnloadStreamingTexturePack(const char *name)
{
    return StreamingTexturePackLoader.DeleteStreamingPack(name);
}

/** The solid loader's pack-deleted callback: the pack's header chunks go. */
void __cdecl SolidUnloadingStreamingPack(eStreamingPack *pack)
{
    InternalUnloaderSolidHeaderChunks(At<bChunk *>(pack, 0x1c));
}

void __cdecl TextureUnloadingStreamingPack(eStreamingPack *pack)
{
    InternalUnloadTexturePackHeaderChunks(At<bChunk *>(pack, 0x1c));
}

void __cdecl eUnloadStreamingSolid(unsigned int *hashes, int count)
{
    UnloadStreamingEntries(&StreamingSolidPackLoader, nullptr, hashes, count);
}

void __cdecl eUnloadStreamingTexture(unsigned int *hashes, int count)
{
    UnloadStreamingEntries(&StreamingTexturePackLoader, nullptr, hashes, count);
}

int __cdecl eIsStreamingTexturePackLoaded(const char *name)
{
    return StreamingTexturePackLoader.GetLoadedStreamingPack(name) != nullptr;
}

void __cdecl eLoadStreamingSolid(unsigned int *hashes, int count, void(__cdecl *callback)(void *), void *argument, int pool)
{
    StreamingSolidPackLoader.LoadStreamingEntry(hashes, count, callback, argument, pool);
}

void __cdecl eLoadStreamingTexture(unsigned int *hashes, int count, void(__cdecl *callback)(void *), void *argument, int pool)
{
    StreamingTexturePackLoader.LoadStreamingEntry(hashes, count, callback, argument, pool);
}

int __cdecl eLoadStreamingSolidPack(const char *name, void(__cdecl *callback)(void *), void *argument, int pool)
{
    return StreamingSolidPackLoader.CreateStreamingPack(name, callback, argument, pool) != nullptr;
}

int __cdecl eLoadStreamingTexturePack(const char *name, void(__cdecl *callback)(void *), void *argument, int pool)
{
    return StreamingTexturePackLoader.CreateStreamingPack(name, callback, argument, pool) != nullptr;
}

void __cdecl eWaitForStreamingTexturePackLoading(const char *name)
{
    while (StreamingTexturePackLoader.IsLoading(name))
    {
        bThreadYield(8);
        WaitForResourceLoadingComplete();
    }
}

void __cdecl SolidUnloadedStreamingEntryCallback(bChunk *chunk, eStreamingEntry *entry, eStreamingPack *pack)
{
    if (eFindSolid(At<unsigned int>(entry, 0), nullptr))
        InternalUnloaderSolidChunks(chunk, At<eSolidListHeader *>(pack, 0x3c));
}

void __cdecl TextureLoadingStreamingPackPhase2(eStreamingPackHeaderLoadingInfoPhase2 *info)
{
    bChunk *header = At<bChunk *>(info, 4);
    if (!At<int>(info, 8))
        EndianSwapChunkHeader(header);
    char *h = reinterpret_cast<char *>(header);
    EndianSwapChunkHeadersRecursive(reinterpret_cast<bChunk *>(h + 8), At<int>(h, 4) + 8 + reinterpret_cast<int>(h));
    char *pack = reinterpret_cast<char *>(InternalLoadTexturePackHeaderChunks(header));
    At<int>(info, 0x10) = At<int>(pack, 0x78);
    At<int>(info, 0x14) = At<int>(At<char *>(pack, 0x70), 0x18);
    At<char *>(info, 0x1c) = pack;
}

void __cdecl TextureLoadedStreamingEntryCallback(bChunk *chunk, eStreamingEntry *entry, eStreamingPack *pack)
{
    TexturePack *textures = PackOf(pack);
    char *info = EntryTexture(chunk, entry);
    textures->AttachTextureInfo(reinterpret_cast<TextureInfo *>(info), reinterpret_cast<TextureInfoPlatInfo *>(info + 0x7c), nullptr);
    const int data = At<int>(info, 0x30);
    textures->AssignTextureData(reinterpret_cast<char *>(chunk), data, DataEnd(info) - data);
}

void __cdecl TextureUnloadedStreamingEntryCallback(bChunk *chunk, eStreamingEntry *entry, eStreamingPack *pack)
{
    char *textures = reinterpret_cast<char *>(PackOf(pack));
    char *info = EntryTexture(chunk, entry);
    const int data = At<int>(info, 0x30);
    const int end = DataEnd(info);
    eFixupReplacementTexturesAfterUnloading(reinterpret_cast<TextureInfo *>(info));
    reinterpret_cast<TexturePack *>(textures)->UnAssignTextureData(data, end - data);
    char *index = At<char *>(textures, 0x14);
    if (index && At<void *>(index, 0x74))
    {
        const unsigned int key = At<unsigned int>(info, 0x24);
        if (char *slot = static_cast<char *>(ScanHashTableKey32(key, At<void *>(index, 0x74), At<int>(textures, 0x18), 0, 8)))
        {
            At<int>(slot, 4) = 0;
            --*reinterpret_cast<int *>(0x91a0d8);
            --reinterpret_cast<signed char *>(&TextureLoadedTable)[key & 0x1fff];
        }
    }
    if (reinterpret_cast<TextureInfo *>(info) == DefaultTextureInfo)
        DefaultTextureInfo = nullptr;
}
