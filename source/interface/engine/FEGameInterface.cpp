/**
 * The front end's game interface and texture helpers (original: zFe FEGameInterface::RenderObjectList,
 * DrawMousePointer, cFEngGameInterface::RenderObject, RenderObjectDisconnect, FEManager::Render, FEngSetTextureHash and
 * FEngSetButtonTexture): the FEngine hands each object it draws to the interface, which lists it with the renderer.
 *
 * cFEngGameInterface: +4 whether anything draws, +8 the colour mode applied to objects asking for it (FEObject flag
 * 0x10). A texture change marks the image for rebuilding (FEObject flags 0x2400000).
 */
#include "game/gen/api.h"
#include "legacy/link.h"
#include "interface/components/padprompts.h"

NFS_REPLACE(0x516390, FEInterfaceRenderObjectList)
NFS_REPLACE(0x5163c0, FEInterfaceDrawMousePointer)
NFS_REPLACE(0x516630, FEngInterfaceRenderObject)
NFS_REPLACE(0x5162c0, RenderObjectDisconnectCallback)
NFS_REPLACE(0x516790, RenderObjectDisconnectDestroy)
NFS_REPLACE(0x5167c0, RenderObjectDisconnectDelete)

void __fastcall FEInterfaceRenderObjectList(void *self, void *, void **list, int count);
void __fastcall FEInterfaceDrawMousePointer(void *self, void *, void *mouse);
void __fastcall FEngInterfaceRenderObject(void *self, void *, FEObject *object);
bool __fastcall RenderObjectDisconnectCallback(void *self, void *, FEObject *object);
void __fastcall RenderObjectDisconnectDestroy(void *self);
void *__fastcall RenderObjectDisconnectDelete(void *self, void *, int flags);

namespace
{
template <typename T> T &At(void *p, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(p) + offset);
}

const unsigned int kImageChanged = 0x2400000;
/** bStringHash of the pad "A" button picture, and its copy for language 1. */
const unsigned int kAcceptButton = 0x1bb3c514;
const unsigned int kAcceptButtonLanguage1 = 0xec617269;
/** The interface's vtable slot for drawing one object. */
const int kRenderObjectSlot = 13;
const unsigned int kDisconnectVtable = 0x89b064;

void SetImageHash(char *image, unsigned int hash)
{
    if (At<unsigned int>(image, 0x24) == hash)
        return;
    At<unsigned int>(image, 0x24) = hash;
    At<unsigned int>(image, 0x1c) |= kImageChanged;
}
} // namespace

void __cdecl FEngSetTextureHash(FEImage *image, unsigned int hash)
{
    if (image)
        SetImageHash(reinterpret_cast<char *>(image), hash);
}

void __cdecl FEngSetTextureHash(const char *packageName, unsigned int objectHash, unsigned int hash)
{
    if (!packageName)
        return;
    FEPackage *package = (*reinterpret_cast<cFEng **>(0x91cadc))->FindPackage(packageName);
    if (!package)
        return;
    FEObject *object = package->FindObjectByHash(objectHash);
    if (object && At<int>(object, 0x18) == 1)
        SetImageHash(reinterpret_cast<char *>(object), hash);
}

void __cdecl FEngSetButtonTexture(FEImage *image, unsigned int hash)
{
    if (!image)
        return;
    if (hash == kAcceptButton && static_cast<int>(GetCurrentLanguage()) == 1)
        hash = kAcceptButtonLanguage1;
    SetImageHash(reinterpret_cast<char *>(image), hash);
}

void __fastcall FEInterfaceRenderObjectList(void *self, void *, void **list, int count)
{
    typedef void(__thiscall * RenderOne)(void *, void *);
    for (int i = count; i != 0; --i)
        (*static_cast<RenderOne **>(self))[kRenderObjectSlot](self, list[(i - 1) * 2]);
}

void __fastcall FEInterfaceDrawMousePointer(void *, void *, void *)
{
}

void __fastcall FEngInterfaceRenderObject(void *self, void *, FEObject *object)
{
    const unsigned int flags = At<unsigned int>(object, 0x1c);
    const bool visible = !(flags & 1) && At<unsigned char>(self, 4);
    if (flags & 0x10)
    {
        const FEColor *colour = nullptr;
        switch (At<int>(self, 8))
        {
        case 0:
            colour = reinterpret_cast<const FEColor *>(0x91cc28);
            break;
        case 1:
            colour = reinterpret_cast<const FEColor *>(0x91cc14);
            break;
        case 2:
            colour = reinterpret_cast<const FEColor *>(0x91cc38);
            break;
        }
        if (colour)
            *At<FEColor *>(object, 0x2c) = *colour;
    }
    if (visible)
        cFEngRender::mInstance->AddToRenderList(object);
}

/** RenderObjectDisconnect::Callback (thiscall): +4 the package's render info, +8 the renderer. */
bool __fastcall RenderObjectDisconnectCallback(void *self, void *, FEObject *object)
{
    At<cFEngRender *>(self, 8)->RemoveCachedRender(object, At<FEPackageRenderInfo *>(self, 4));
    return true;
}

void __fastcall RenderObjectDisconnectDestroy(void *self)
{
    *static_cast<unsigned int *>(self) = kDisconnectVtable;
}

void *__fastcall RenderObjectDisconnectDelete(void *self, void *, int flags)
{
    RenderObjectDisconnectDestroy(self);
    if (flags & 1)
        ::operator delete(self);
    return self;
}

void FEManager::Render()
{
    if (!*reinterpret_cast<int *>(0x8f374c))
        return;
    nfs::prompts::Update();
    (*reinterpret_cast<FEngine **>(*reinterpret_cast<char **>(0x91cadc)))->Render();
    NFS_ORIGINAL(0x56fdb0, void(__cdecl *)(int, int))(*reinterpret_cast<int *>(0x987f54),
                                                      *reinterpret_cast<int *>(0x987f58));
}
