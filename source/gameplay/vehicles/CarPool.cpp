/** The car loader's pool: its size from [Streaming] CarPoolMegabytes (the original's 48 MB runs out with the upscaled
 * car textures), and making room in it (original: CarLoader::RemoveSomethingFromCarMemoryPool 0x759f00, whose debug
 * breaks ended the game; they are logged here). */
#include "game/gen/api.h"
#include "core/config.h"
#include "core/log.h"
#include "gameplay/vehicles/cars.h"
#include "legacy/link.h"

NFS_REPLACE(0x759f00, RemoveFromCarPool)

namespace
{
const int kDefaultMegabytes = 192;
const int kMinMegabytes = 48;
const int kMaxMegabytes = 512;
const int kKilobytesPerMegabyte = 1024;

const int kMode = 8;
const int kFreedBlocks = 0x24;
const int kRides = 0x58;
const int kBlockCount = 0x68;
const int kBlocks = 0x6c;
const int kRideSkinPending = 0x10;
const int kRideSkin = 0x350;
const int kSkinState = 0xc;
const int kSkinRide = 8;
const unsigned char kSkinLoaded = 2;

struct Node
{
    Node *next;
};

template <typename T> T &At(void *base, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}
} // namespace

void nfs::cars::ConfigureLoaderPool()
{
    int megabytes = nfs::config::Int("Streaming", "CarPoolMegabytes", kDefaultMegabytes);
    if (megabytes < kMinMegabytes)
        megabytes = kMinMegabytes;
    if (megabytes > kMaxMegabytes)
        megabytes = kMaxMegabytes;
    // An array: single player, split screen.
    int *sizes = reinterpret_cast<int *>(&CarLoaderPoolSizes);
    const int original = sizes[0];
    sizes[0] = megabytes * kKilobytesPerMegabyte;
    nfs::Log("car loader: pool %d MB (the original's %d MB)", megabytes, original / kKilobytesPerMegabyte);
}

int __fastcall RemoveFromCarPool(CarLoader *loader, void *, bool urgent)
{
    Node *rides = &At<Node>(loader, kRides);
    for (Node *ride = rides->next; ride != rides; ride = ride->next)
        if (NFS_ORIGINAL(0x759c60, int(__thiscall *)(CarLoader *, Node *, int))(loader, ride, 0))
            return 1;
    if (!urgent)
        return 0;
    int &blocks = At<int>(loader, kBlockCount);
    if (blocks)
    {
        --blocks;
        bFree(At<void *>(loader, kBlocks + blocks * 4));
        ++At<int>(loader, kFreedBlocks);
        return 1;
    }
    for (int pass = 0; pass < 2; ++pass)
    {
        for (Node *ride = rides->next; ride != rides; ride = ride->next)
        {
            if (At<int>(ride, kRideSkinPending) && pass != 1)
                continue;
            void *skin = At<void *>(ride, kRideSkin);
            if (At<unsigned char>(skin, kSkinState) != kSkinLoaded)
                continue;
            if (!NFS_ORIGINAL(0x759ba0, int(__thiscall *)(CarLoader *, void *))(loader, skin))
                continue;
            if (!At<int>(loader, kMode))
                nfs::Log("car loader: a skin's textures unloaded in the front end");
            eFixupReplacementTextureTables();
            RefreshAllRenderInfo(**At<CarType **>(skin, kSkinRide));
            return 1;
        }
    }
    NFS_ORIGINAL(0x7573c0, void(__thiscall *)(CarLoader *, bool))(loader, false);
    nfs::Log("car loader: the pool is full and nothing more can be unloaded");
    return 0;
}

int nfs::cars::MakeRoomInLoaderPool(CarLoader *loader, bool urgent)
{
    return RemoveFromCarPool(loader, nullptr, urgent);
}
