/** The car loader's wheel textures (original: zWorld CarLoader::LoadAllWheelTextures 0x75b640,
 * LoadedWheelTexturesCallback 0x7484e0, LoadedWheelTexturesCallbackBridge 0x748ac0). */
#include "game/gen/api.h"
#include "gameplay/vehicles/cars.h"
#include "legacy/link.h"

NFS_REPLACE(0x75b640, LoadCarWheelTextures)
NFS_REPLACE(0x7484e0, CarWheelTexturesLoaded)
NFS_REPLACE(0x748ac0, CarWheelTexturesLoadedBridge)

namespace
{
struct Texture
{
    void *node[2];
    unsigned int hash;
    short references;
    unsigned char state;
};

struct Wheels
{
    unsigned char unknown[0xc];
    int frontState;
    int rearState;
    unsigned char unknown14[0x40];
    Texture *front[4];
    Texture *rear[4];
};
static_assert(sizeof(Wheels) == 0x74, "wheel textures");

struct Node
{
    Node *next;
};

enum State
{
    kNotLoaded = 0,
    kLoading = 1,
    kLoaded = 2,
};

const int kBusy = 0x14;
const int kSkins = 0x58;
const int kSkinCount = 8;
const int kSkinWheelsLoading = 0xc;
const int kSkinWheels = 0x34c;
const int kMaxWheelTextures = 0x7c;

template <typename T> T &At(void *base, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}

void MarkLoaded(Texture *const (&textures)[4])
{
    for (Texture *t : textures)
        if (t && t->state == kLoading)
            t->state = kLoaded;
}
} // namespace

void __cdecl CarWheelTexturesLoadedBridge(unsigned int);

int __fastcall LoadCarWheelTextures(CarLoader *loader)
{
    unsigned int wanted[kMaxWheelTextures];
    int count = 0;
    Node *head = &At<Node>(loader, kSkins);
    for (Node *skin = head->next; skin != head; skin = skin->next)
    {
        if (!At<int>(skin, kSkinCount))
            continue;
        Wheels *wheels = At<Wheels *>(skin, kSkinWheels);
        if (wheels->frontState == kNotLoaded)
        {
            int added = 0;
            for (Texture *t : wheels->front)
            {
                if (t && t->state == kNotLoaded)
                {
                    t->state = kLoading;
                    wanted[count + added++] = t->hash;
                }
            }
            wheels->frontState = added ? kLoading : kLoaded;
            At<int>(skin, kSkinWheelsLoading) = 1;
            count += added;
        }
        if (wheels->rearState == kNotLoaded)
            wheels->rearState = kLoaded;
    }
    if (count <= 0)
        return 0;
    const int pool = CarLoaderMemoryPoolNumber;
    while (StreamingTexturePackLoader.TestLoadStreamingEntry(wanted, count, pool, true) &&
           nfs::cars::MakeRoomInLoaderPool(loader, true))
    {
    }
    At<int>(loader, kBusy) = 1;
    eLoadStreamingTexture(wanted, count, &CarWheelTexturesLoadedBridge, 0, pool);
    return 1;
}

void __fastcall CarWheelTexturesLoaded(CarLoader *loader)
{
    Node *head = &At<Node>(loader, kSkins);
    for (Node *skin = head->next; skin != head; skin = skin->next)
    {
        Wheels *wheels = At<Wheels *>(skin, kSkinWheels);
        if (wheels->frontState == kLoading)
        {
            wheels->frontState = kLoaded;
            MarkLoaded(wheels->front);
        }
        if (wheels->rearState == kLoading)
        {
            wheels->rearState = kLoaded;
            MarkLoaded(wheels->rear);
        }
    }
    At<int>(loader, kBusy) = 0;
    NFS_ORIGINAL(0x75bed0, void(__thiscall *)(CarLoader *))(loader);
}

void __cdecl CarWheelTexturesLoadedBridge(unsigned int)
{
    CarWheelTexturesLoaded(&TheCarLoader);
}
