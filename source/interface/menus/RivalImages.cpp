/** The rap sheet's rival pictures streamed in (original: zFe uiRepSheetRivalStreamer, uiRepSheetRival::
 * TextureLoadedCallback 0x52edf0). */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x5119d0, RivalTexturesToLoad)
NFS_REPLACE(0x530240, LoadRivalTextures)
NFS_REPLACE(0x51f520, RivalTexturesLoaded)

namespace
{
struct Streamer
{
    void *vtable;
    int pool;
    bool inGame;
    unsigned char pad[3];
    const char *package;
    int wanted;
    int loaded;
    bool busy;
    unsigned char pad19[3];
    int count;
    unsigned int hashes[3];
    FEImage *images[3];
};
static_assert(sizeof(Streamer) == 0x38, "uiRepSheetRivalStreamer");

const char *const kNames[3][2] = {
    {"RIVAL_%02d", "RIVAL_%02d_IG"},
    {"RIVAL_%02d_GRAF", "RIVAL_%02d_IG_GRAF"},
    {"RIVAL_%02d_BG", "RIVAL_%02d_IG_BG"},
};
const unsigned int kShown[3] = {0xc0942e85, 0x8c9d4547, 0xd22b95d0};
const int kImageTexture = 0x24;
const int kImageFlags = 0x1c;
const unsigned int kImageDirty = 0x2400000;
FEObject *const kAllObjects = reinterpret_cast<FEObject *>(-4);
const unsigned int kAnyController = 0xff;

Streamer *Self(uiRepSheetRivalStreamer *s)
{
    return reinterpret_cast<Streamer *>(s);
}

void SetTexture(FEImage *image, unsigned int hash)
{
    char *p = reinterpret_cast<char *>(image);
    if (*reinterpret_cast<unsigned int *>(p + kImageTexture) == hash)
        return;
    *reinterpret_cast<unsigned int *>(p + kImageTexture) = hash;
    *reinterpret_cast<unsigned int *>(p + kImageFlags) |= kImageDirty;
}
} // namespace

int __fastcall RivalTexturesToLoad(uiRepSheetRivalStreamer *streamer, void *, unsigned int *hashes, int rival)
{
    Streamer *s = Self(streamer);
    int count = 0;
    for (int i = 0; i < 3; ++i)
        if (s->images[i])
            hashes[count++] = FEngHashString(kNames[i][s->inGame ? 1 : 0], rival);
    return count;
}

void __fastcall LoadRivalTextures(uiRepSheetRivalStreamer *streamer)
{
    Streamer *s = Self(streamer);
    if (s->loaded == s->wanted)
        return;
    if (s->count)
    {
        eUnloadStreamingTexture(s->hashes, s->count);
        s->count = 0;
        s->loaded = -1;
    }
    s->busy = true;
    s->loaded = s->wanted;
    s->count = RivalTexturesToLoad(streamer, nullptr, s->hashes, s->wanted);
    eLoadStreamingTexture(
        s->hashes, s->count,
        reinterpret_cast<void(__cdecl *)(void *)>(&uiRepSheetRivalStreamer::TexturesLoadedCallbackBridge), streamer,
        s->pool);
}

void __fastcall RivalTexturesLoaded(uiRepSheetRivalStreamer *streamer)
{
    Streamer *s = Self(streamer);
    s->busy = false;
    if (s->loaded != s->wanted)
    {
        LoadRivalTextures(streamer);
        return;
    }
    int next = 0;
    for (int i = 0; i < 3; ++i)
    {
        if (!s->images[i])
            continue;
        NFS_ORIGINAL(0x516be0, void(__thiscall *)(cFEng *, unsigned int, const char *, FEObject *, unsigned int))(
            cFEng::mInstance, kShown[i], s->package, kAllObjects, kAnyController);
        if (i == 0)
            GetTextureInfo(s->hashes[0], 0, 0);
        SetTexture(s->images[i], s->hashes[next++]);
        FEngSetVisible(reinterpret_cast<FEObject *>(s->images[i]));
    }
}

void uiRepSheetRivalStreamer::TexturesLoadedCallbackBridge(void *streamer)
{
    RivalTexturesLoaded(static_cast<uiRepSheetRivalStreamer *>(streamer));
}

void uiRepSheetRivalStreamer::TexturePackLoadedCallbackBridge(void *streamer)
{
    Self(static_cast<uiRepSheetRivalStreamer *>(streamer))->busy = false;
    LoadRivalTextures(static_cast<uiRepSheetRivalStreamer *>(streamer));
}

void uiRepSheetRival::TextureLoadedCallback(unsigned int screen)
{
    char *p = reinterpret_cast<char *>(screen);
    FEngSetVisible(*reinterpret_cast<FEObject **>(p + 0x38));
    FEngSetVisible(*reinterpret_cast<FEObject **>(p + 0x3c));
}
