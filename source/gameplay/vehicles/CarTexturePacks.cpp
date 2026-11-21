/** The car loader's texture packs (original: zWorld LoadedTexturePack, CarLoader::FindLoadedTexturePack 0x748820,
 * AllocateTexturePack 0x752f00, UnloadTexturePack 0x752f80, LoadTexturePack 0x75b400, LoadedTexturePackCallbackBridge
 * 0x73ab00). */
#include <new>

#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x748820, FindCarTexturePack)
NFS_REPLACE(0x752f00, AllocateCarTexturePack)
NFS_REPLACE(0x752f80, UnloadCarTexturePack)
NFS_REPLACE(0x75b400, LoadCarTexturePack)
NFS_REPLACE(0x73ab00, CarTexturePackLoaded)

struct CarTexturePack
{
    CarTexturePack *next;
    CarTexturePack *prev;
    const char *name;
    short references;
    unsigned char state;
    unsigned char unused;
    int size;
    eStreamingPack *streaming;
};
static_assert(sizeof(CarTexturePack) == 0x18, "LoadedTexturePack");

namespace
{
enum State : unsigned char
{
    kNotLoaded = 0,
    kLoading = 1,
    kLoaded = 2,
};

const int kPacks = 0x30;
const int kBusy = 0x14;

CarTexturePack *Packs(CarLoader *loader)
{
    return reinterpret_cast<CarTexturePack *>(reinterpret_cast<char *>(loader) + kPacks);
}

} // namespace

void __cdecl CarTexturePackLoaded(unsigned int pack);

LoadedTexturePack::LoadedTexturePack(const char *name, int size)
{
    CarTexturePack *self = reinterpret_cast<CarTexturePack *>(this);
    self->name = bAllocateSharedString(name);
    self->references = 0;
    self->state = kNotLoaded;
    self->unused = 0;
    self->size = size;
    self->streaming = nullptr;
    if (bFileSize(self->name) == 0)
        self->state = kLoaded;
}

LoadedTexturePack::~LoadedTexturePack()
{
    bFreeSharedString(reinterpret_cast<CarTexturePack *>(this)->name);
}

CarTexturePack *__fastcall FindCarTexturePack(CarLoader *loader, void *, const char *name)
{
    CarTexturePack *head = Packs(loader);
    for (CarTexturePack *p = head->next; p != head; p = p->next)
        if (bStrCmp(p->name, name) == 0)
            return p;
    return nullptr;
}

CarTexturePack *__fastcall AllocateCarTexturePack(CarLoader *loader, void *, const char *name, int size)
{
    CarTexturePack *pack = FindCarTexturePack(loader, nullptr, name);
    if (!pack)
    {
        void *memory = bOMalloc(LoadedTexturePackSlotPool);
        pack = memory ? reinterpret_cast<CarTexturePack *>(new (memory) LoadedTexturePack(name, size)) : nullptr;
        CarTexturePack *head = Packs(loader);
        CarTexturePack *tail = head->prev;
        tail->next = pack;
        head->prev = pack;
        pack->prev = tail;
        pack->next = head;
    }
    ++pack->references;
    return pack;
}

int __fastcall UnloadCarTexturePack(CarLoader *, void *, CarTexturePack *pack)
{
    if (pack->references != 0)
        return 0;
    if (pack->state == kLoaded && pack->streaming)
        eUnloadStreamingTexturePack(pack->name);
    pack->prev->next = pack->next;
    pack->next->prev = pack->prev;
    reinterpret_cast<LoadedTexturePack *>(pack)->~LoadedTexturePack();
    bFree(LoadedTexturePackSlotPool, pack);
    return 1;
}

int __fastcall LoadCarTexturePack(CarLoader *loader, void *, CarTexturePack *pack, int makeSpace)
{
    int pool = 0;
    if (makeSpace)
    {
        pool = CarLoaderMemoryPoolNumber;
        NFS_ORIGINAL(0x759ff0, int(__thiscall *)(CarLoader *, int, int, bool))(loader, pack->size, 0, true);
    }
    *reinterpret_cast<int *>(reinterpret_cast<char *>(loader) + kBusy) = 1;
    pack->state = kLoading;
    eLoadStreamingTexturePack(pack->name, reinterpret_cast<void(__cdecl *)(void *)>(&CarTexturePackLoaded), pack, pool);
    pack->streaming = StreamingTexturePackLoader.GetLoadedStreamingPack(pack->name);
    return 1;
}

void __cdecl CarTexturePackLoaded(unsigned int pack)
{
    reinterpret_cast<CarTexturePack *>(pack)->state = kLoaded;
    *reinterpret_cast<int *>(reinterpret_cast<char *>(&TheCarLoader) + kBusy) = 0;
    NFS_ORIGINAL(0x75bed0, void(__thiscall *)(CarLoader *))(&TheCarLoader);
}
