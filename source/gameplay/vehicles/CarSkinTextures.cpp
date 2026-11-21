/** The car loader's skin textures (original: zWorld CarLoader::LoadAllTexturesFromPack 0x75b7a0,
 * LoadedAllTexturesFromPackCallback 0x759a20, LoadedAllTexturesFromPackCallbackBridge 0x75a120). */
#include "game/gen/api.h"
#include "gameplay/vehicles/cars.h"
#include "legacy/link.h"

NFS_REPLACE(0x75b7a0, LoadCarSkinTextures)
NFS_REPLACE(0x759a20, CarSkinTexturesLoaded)
NFS_REPLACE(0x75a120, CarSkinTexturesLoadedBridge)

namespace
{
struct Layer
{
    void *node[2];
    unsigned int hash;
    short references;
    unsigned char state;
};

enum State : unsigned char
{
    kNotLoaded = 0,
    kLoading = 1,
    kLoaded = 2,
};

const int kMaxLayers = 0x200;
const int kMaxSkinHashes = 0x80;
const int kMode = 8;
const int kBusy = 0x14;
const int kSkins = 0x58;
const int kSkinCount = 8;
const int kSkinLoaded = 0xc;
const int kSkinRecord = 0x350;
const int kLayerCount = 0xac;
const int kLayers = 0xb0;
const int kModeRace = 2;

template <typename T> T &At(void *base, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}

struct Node
{
    Node *next;
};

Layer **Layers(CarLoader *loader)
{
    return &At<Layer *>(loader, kLayers);
}

void ReleaseLayers(CarLoader *loader)
{
    Layer **layers = Layers(loader);
    for (int i = 0; i < At<int>(loader, kLayerCount); ++i)
        if (layers[i])
            --layers[i]->references;
}

int AllocateSkinLayers(CarLoader *loader, unsigned int *hashes, int count, Layer **out, int room, const char *name)
{
    return NFS_ORIGINAL(0x753060, int(__thiscall *)(CarLoader *, unsigned int *, int, Layer **, int, const char *))(
        loader, hashes, count, out, room, name);
}

int UnloadSkinLayers(CarLoader *loader, unsigned int *hashes, int room, Layer **layers, int count)
{
    return NFS_ORIGINAL(0x757610, int(__thiscall *)(CarLoader *, unsigned int *, int, Layer **, int))(
        loader, hashes, room, layers, count);
}

int RemoveSomethingFromPool(CarLoader *loader, bool urgent)
{
    return nfs::cars::MakeRoomInLoaderPool(loader, urgent);
}

void ServiceLoading(CarLoader *loader)
{
    NFS_ORIGINAL(0x75bed0, void(__thiscall *)(CarLoader *))(loader);
}
} // namespace

void __cdecl CarSkinTexturesLoadedBridge(unsigned int);

int __fastcall LoadCarSkinTextures(CarLoader *loader, void *, const char *name, int makeSpace)
{
    Node *head = &At<Node>(loader, kSkins);
    for (Node *skin = head->next; skin != head; skin = skin->next)
    {
        if (At<int>(skin, kSkinCount) <= 0 || At<int>(skin, kSkinLoaded) != 0)
            continue;
        unsigned int hashes[kMaxSkinHashes];
        const int count = At<LoadedSkin *>(skin, kSkinRecord)->GetTextureHashes(hashes, kMaxSkinHashes, makeSpace);
        int &layers = At<int>(loader, kLayerCount);
        layers += AllocateSkinLayers(loader, hashes, count, Layers(loader) + layers, kMaxLayers - layers, name);
    }
    unsigned int wanted[kMaxLayers];
    int count = 0;
    Layer **layers = Layers(loader);
    for (int i = 0; i < At<int>(loader, kLayerCount); ++i)
    {
        if (layers[i] && layers[i]->state == kNotLoaded)
        {
            layers[i]->state = kLoading;
            wanted[count++] = layers[i]->hash;
        }
    }
    if (!count)
    {
        ReleaseLayers(loader);
        At<int>(loader, kLayerCount) = 0;
        return 0;
    }
    int pool = 0;
    if (makeSpace || At<int>(loader, kMode) == kModeRace)
    {
        pool = CarLoaderMemoryPoolNumber;
        while (StreamingTexturePackLoader.TestLoadStreamingEntry(wanted, count, pool, true) &&
               RemoveSomethingFromPool(loader, true))
        {
        }
    }
    At<int>(loader, kBusy) = 1;
    eLoadStreamingTexture(wanted, count, &CarSkinTexturesLoadedBridge, 0, pool);
    return 1;
}

void __fastcall CarSkinTexturesLoaded(CarLoader *loader)
{
    Layer **layers = Layers(loader);
    const int count = At<int>(loader, kLayerCount);
    for (int i = 0; i < count; ++i)
        if (layers[i] && layers[i]->state == kLoading)
            layers[i]->state = kLoaded;
    ReleaseLayers(loader);
    unsigned int unload[kMaxLayers];
    const int unloaded = UnloadSkinLayers(loader, unload, kMaxLayers, layers, At<int>(loader, kLayerCount));
    if (unloaded)
        eUnloadStreamingTexture(unload, unloaded);
    At<int>(loader, kLayerCount) = 0;
    At<int>(loader, kBusy) = 0;
    ServiceLoading(loader);
}

void __cdecl CarSkinTexturesLoadedBridge(unsigned int)
{
    CarSkinTexturesLoaded(&TheCarLoader);
}
