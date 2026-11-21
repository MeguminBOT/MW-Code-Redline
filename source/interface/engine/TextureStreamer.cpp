/** Textures streamed into front-end images one at a time (original: zFe2 SillyTextureStreamerManager). */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x56a7c0, StreamedImageLoaded)
NFS_REPLACE(0x57ada0, StreamedImageLoadedBridge)
NFS_REPLACE(0x58c5f0, StreamerPoolReady)

namespace
{
struct Slot
{
    FEImage *image;
    unsigned int hash;
    bool loaded;
};

struct Streamer
{
    char pack[0x100];
    Slot slots[4];
    bool busy;
    bool packRequested;
    int current;
};
static_assert(sizeof(Streamer) == 0x138, "SillyTextureStreamerManager");

const int kPoolBytes = 0x300000;
const int kTexturePool = 7;
const int kStreamerUsesPool = 0x2b1;

Streamer *Self(SillyTextureStreamerManager *s)
{
    return reinterpret_cast<Streamer *>(s);
}

bool &StreamerUsesPool()
{
    return *(reinterpret_cast<bool *>(&TheTrackStreamer) + kStreamerUsesPool);
}
} // namespace

void __fastcall StreamedImageLoaded(SillyTextureStreamerManager *manager);
void __cdecl StreamedImageLoadedBridge(unsigned int manager);
void __cdecl StreamerPoolReady(int manager);

SillyTextureStreamerManager::SillyTextureStreamerManager(const char *pack)
{
    Streamer *s = Self(this);
    bStrNCpy(s->pack, pack, sizeof(s->pack));
    for (Slot &slot : s->slots)
        slot = Slot();
    s->current = -1;
    s->busy = true;
    s->packRequested = false;
    StreamerUsesPool() = true;
    TheTrackStreamer.MakeSpaceInPool(kPoolBytes, &StreamerPoolReady, reinterpret_cast<int>(this));
}

SillyTextureStreamerManager::~SillyTextureStreamerManager()
{
    Streamer *s = Self(this);
    if (!s->packRequested)
        TheTrackStreamer.WaitForCurrentLoadingToComplete();
    eWaitForStreamingTexturePackLoading(nullptr);
    for (Slot &slot : s->slots)
    {
        if (slot.hash)
        {
            unsigned int hash = slot.hash;
            eUnloadStreamingTexture(&hash, 1);
        }
    }
    eUnloadStreamingTexturePack(s->pack);
    StreamerUsesPool() = false;
}

void SillyTextureStreamerManager::Load(unsigned int hash, FEImage *image)
{
    Streamer *s = Self(this);
    for (Slot &slot : s->slots)
    {
        if (slot.hash)
            continue;
        slot.hash = hash;
        slot.image = image;
        FEngSetInvisible(reinterpret_cast<FEObject *>(image));
        if (!s->busy)
            StreamedImageLoaded(this);
        return;
    }
}

void SillyTextureStreamerManager::UnloadAll()
{
    for (Slot &slot : Self(this)->slots)
    {
        if (!slot.loaded)
            continue;
        unsigned int hash = slot.hash;
        eUnloadStreamingTexture(&hash, 1);
        slot.hash = 0;
        slot.loaded = false;
    }
}

void __fastcall StreamedImageLoaded(SillyTextureStreamerManager *manager)
{
    Streamer *s = Self(manager);
    s->busy = false;
    if (s->current >= 0)
    {
        Slot &done = s->slots[s->current];
        FEngSetTextureHash(done.image, done.hash);
        FEngSetVisible(reinterpret_cast<FEObject *>(done.image));
        done.loaded = true;
        s->current = -1;
    }
    for (int i = 0; i < 4; ++i)
    {
        if (!s->slots[i].hash || s->slots[i].loaded)
            continue;
        s->busy = true;
        s->current = i;
        unsigned int hash = s->slots[i].hash;
        eLoadStreamingTexture(&hash, 1, &StreamedImageLoadedBridge, reinterpret_cast<unsigned int>(manager),
                              kTexturePool);
        return;
    }
    cFEng::mInstance->MakeLoadedPackagesDirty();
}

void __cdecl StreamedImageLoadedBridge(unsigned int manager)
{
    StreamedImageLoaded(reinterpret_cast<SillyTextureStreamerManager *>(manager));
}

void __cdecl StreamerPoolReady(int manager)
{
    Streamer *s = reinterpret_cast<Streamer *>(manager);
    s->packRequested = true;
    eLoadStreamingTexturePack(s->pack, reinterpret_cast<void(__cdecl *)(void *)>(&StreamedImageLoadedBridge), s, 0);
}
