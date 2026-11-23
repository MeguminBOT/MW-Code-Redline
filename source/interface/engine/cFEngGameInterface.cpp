/**
 * The game's side of the front-end engine's interface: resources, notifications, transforms, the packages drawn, pads
 * and mouse, packages loaded and unloaded (original: zFe cFEngGameInterface::LoadResources 0x516450, UnloadResources
 * 0x5164f0, NotificationMessage 0x516520, NotifySoundMessage 0x516560, GetContextTransform 0x5165a0,
 * GetViewTransformation 0x516690, BeginPackageRendering 0x5166a0, EndPackageRendering 0x516710, GetPackageData
 * 0x516840, GetJoyPadMask 0x516860, GetMouseInfo 0x516870, PackageWasLoaded 0x525520, PackageWillUnload 0x525640,
 * DoesPointTouchObject 0x54ea20, GetBaseName 0x516300 (name in esi, out in eax); cFEng::MakeLoadedPackagesDirty
 * 0x525a00).
 *
 * A resource request (0x18): +4 its name, +8 its kind (4: a name kept as text, the others hashed), +0x10 what it
 * becomes, +0x14 cleared. Its name: the file name without folders or extension, upper case. cFEngGameInterface: +4
 * whether the package draws (not while the disc error is up unless it is the error's package, only the single package
 * asked for, only visible ones), +8 the colour mode of objects asking for it (1 racing, 2 the database's flag 0x200).
 * The mouse: its position around the 640x480 screen's middle, buttons left and right (0x987f54/58, 0x987f70/73).
 * A loaded package gets its objects' movies started, PC-only objects shown, flag 4 handed to group children; an
 * unloaded one has its movies stopped and its objects let go of by the renderer.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
template <typename T> T &At(const void *p, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(p)) + offset);
}

const unsigned int kSkipA = 0x5922615;
const unsigned int kSkipB = 0x7e4d1288;
const unsigned int kDiscErrorPackage = 0x942c98b5;
const int *const kDiscError = reinterpret_cast<const int *>(0x9257f8);
const char *const *const kSinglePackage = reinterpret_cast<const char *const *>(0x91cad8);
const int *const kRaceStatus = reinterpret_cast<const int *>(0x91e000);
char *const *const kFEDatabase = reinterpret_cast<char *const *>(0x91cf90);
void *const *const kJoyInput = reinterpret_cast<void *const *>(0x91cad0);
const int *const kMouseX = reinterpret_cast<const int *>(0x987f54);
const int *const kMouseY = reinterpret_cast<const int *>(0x987f58);
const unsigned char *const kMouseLeft = reinterpret_cast<const unsigned char *>(0x987f70);
const unsigned char *const kMouseRight = reinterpret_cast<const unsigned char *>(0x987f73);
const float kHalfWidth = 320.0f;
const float kHalfHeight = 240.0f;
const int kKeptName = 4;
const int kNameBytes = 0x100;

void *const kMovieStarter = reinterpret_cast<void *>(0x89cc08);
void *const kHidePCObjects = reinterpret_cast<void *>(0x89b4a0);
void *const kTransferFlags = reinterpret_cast<void *>(0x89b378);
void *const kMovieStopper = reinterpret_cast<void *>(0x89b4a8);
void *const kRenderDisconnect = reinterpret_cast<void *>(0x89b4b0);
void *const kDirtySetter = reinterpret_cast<void *>(0x89b4c0);

using JoyPadMask = unsigned int(__thiscall *)(void *, unsigned char);
const JoyPadMask kJoyPadMask = reinterpret_cast<JoyPadMask>(0x514b90);

/** One of the original's object callbacks: its vtable and up to three words. */
struct Callback
{
    void *vtable;
    unsigned int a, b;
    FEObjectCallback &Ref()
    {
        return *reinterpret_cast<FEObjectCallback *>(this);
    }
};

void BaseName(const char *name, char *out)
{
    int length = 0, start = 0;
    while (name[length])
    {
        const char c = name[length++];
        if (c == '\\' || c == '/')
            start = length;
    }
    int end = length;
    for (int i = length; i; --i)
        if (name[i] == '.')
        {
            end = i;
            break;
        }
    int n = 0;
    for (int i = start; i < end; ++i)
        out[n++] = name[i];
    out[n] = 0;
}
} // namespace

void __fastcall PackageNotification(FEPackageManager *m, void *, unsigned int message, FEObject *o, unsigned int a,
                                    unsigned int package);
void __fastcall PackageNotifySound(FEPackageManager *m, void *, unsigned int message, FEObject *o, unsigned int a,
                                   unsigned int package);
void *__fastcall GetPackageData(FEPackageManager *m, void *, const char *name);
void __fastcall PackageWasLoaded(FEPackageManager *m, void *, FEPackage *p);
void __fastcall PackageWillBeUnloaded(FEPackageManager *m, void *, FEPackage *p);

bool __fastcall GILoadResources(void *gi, void *, FEPackage *p, int count, char *requests);
bool __fastcall GIUnloadResources(void *gi, void *, FEPackage *p, int count, char *requests);
void __fastcall GINotificationMessage(void *gi, void *, unsigned int message, FEObject *o, unsigned int a,
                                      unsigned int package);
void __fastcall GINotifySoundMessage(void *gi, void *, unsigned int message, FEObject *o, unsigned int a,
                                     unsigned int package);
bool __fastcall GIGetContextTransform(void *gi, void *, unsigned short context, FEMatrix4 *out);
void __fastcall GIGetViewTransformation(void *gi, void *, FEMatrix4 *out);
void __fastcall GIBeginPackageRendering(char *gi, void *, FEPackage *p);
void __fastcall GIEndPackageRendering(void *gi, void *, FEPackage *p);
void *__fastcall GIGetPackageData(void *gi, void *, const char *name, unsigned char **bytes, bool *owned);
unsigned int __fastcall GIGetJoyPadMask(void *gi, void *, unsigned char port);
void __fastcall GIGetMouseInfo(void *gi, void *, short *info);
void __fastcall GIPackageWasLoaded(char *gi, void *, FEPackage *p);
bool __fastcall GIPackageWillUnload(void *gi, void *, FEPackage *p);
bool __fastcall GIDoesPointTouchObject(void *gi, void *, float x, float y, FEObject *o);
NFS_REPLACE(0x516450, GILoadResources)
NFS_REPLACE(0x5164f0, GIUnloadResources)
NFS_REPLACE(0x516520, GINotificationMessage)
NFS_REPLACE(0x516560, GINotifySoundMessage)
NFS_REPLACE(0x5165a0, GIGetContextTransform)
NFS_REPLACE(0x516690, GIGetViewTransformation)
NFS_REPLACE(0x5166a0, GIBeginPackageRendering)
NFS_REPLACE(0x516710, GIEndPackageRendering)
NFS_REPLACE(0x516840, GIGetPackageData)
NFS_REPLACE(0x516860, GIGetJoyPadMask)
NFS_REPLACE(0x516870, GIGetMouseInfo)
NFS_REPLACE(0x525520, GIPackageWasLoaded)
NFS_REPLACE(0x525640, GIPackageWillUnload)
NFS_REPLACE(0x54ea20, GIDoesPointTouchObject)

bool __fastcall GILoadResources(void *, void *, FEPackage *, int count, char *requests)
{
    char name[kNameBytes];
    for (char *r = requests; count > 0; --count, r += 0x18)
    {
        BaseName(At<const char *>(r, 4), name);
        bToUpper(name);
        if (At<int>(r, 8) == kKeptName)
        {
            char *kept = static_cast<char *>(bMalloc(kNameBytes, 0));
            bStrNCpy(kept, name, kNameBytes);
            At<char *>(r, 0x10) = kept;
        }
        else
            At<unsigned int>(r, 0x10) = bStringHash(name);
        At<unsigned int>(r, 0x14) = 0;
    }
    return true;
}

bool __fastcall GIUnloadResources(void *, void *, FEPackage *, int count, char *requests)
{
    for (char *r = requests; count > 0; --count, r += 0x18)
        if (At<int>(r, 8) == kKeptName)
            bFree(At<void *>(r, 0x10));
    return true;
}

void __fastcall GINotificationMessage(void *, void *, unsigned int message, FEObject *o, unsigned int a,
                                      unsigned int package)
{
    if (message != kSkipA && message != kSkipB)
        PackageNotification(FEPackageManager::Get(), nullptr, message, o, a, package);
}

void __fastcall GINotifySoundMessage(void *, void *, unsigned int message, FEObject *o, unsigned int a,
                                     unsigned int package)
{
    PackageNotifySound(FEPackageManager::Get(), nullptr, message, o, a, package);
}

bool __fastcall GIGetContextTransform(void *, void *, unsigned short context, FEMatrix4 *out)
{
    out->Identify();
    if (context)
        if (const void *c = cFEngRender::mInstance->GetRenderContext(context))
        {
            const unsigned int *from = static_cast<const unsigned int *>(c);
            unsigned int *to = reinterpret_cast<unsigned int *>(out);
            for (int i = 0; i < 16; ++i)
                to[i] = from[i];
        }
    return true;
}

void __fastcall GIGetViewTransformation(void *, void *, FEMatrix4 *out)
{
    out->Identify();
}

void __fastcall GIBeginPackageRendering(char *gi, void *, FEPackage *p)
{
    bool &draws = At<bool>(gi, 4);
    draws = true;
    if (*kDiscError && At<unsigned int>(p, 0x10) != kDiscErrorPackage)
        draws = false;
    if (*kSinglePackage && FEHashUpper(*kSinglePackage) != At<unsigned int>(p, 0x10))
        draws = false;
    if (!FEPackageManager::Get()->GetVisibility(At<const char *>(p, 0xc)))
        draws = false;
    cFEngRender::mInstance->PrepForPackage(p);
}

void __fastcall GIEndPackageRendering(void *, void *, FEPackage *p)
{
    cFEngRender::mInstance->PackageFinished(p);
}

void *__fastcall GIGetPackageData(void *, void *, const char *name, unsigned char **, bool *owned)
{
    *owned = false;
    return GetPackageData(FEPackageManager::Get(), nullptr, name);
}

unsigned int __fastcall GIGetJoyPadMask(void *, void *, unsigned char port)
{
    return kJoyPadMask(*kJoyInput, port);
}

void __fastcall GIGetMouseInfo(void *, void *, short *info)
{
    info[0] = static_cast<short>(static_cast<int>(static_cast<float>(*kMouseX) - kHalfWidth));
    info[1] = static_cast<short>(static_cast<int>(static_cast<float>(*kMouseY) - kHalfHeight));
    info[3] = static_cast<short>((*kMouseRight << 1) | *kMouseLeft);
}

void __fastcall GIPackageWasLoaded(char *gi, void *, FEPackage *p)
{
    p->InitializePackage();
    At<bool>(p, 0x14) = true;
    if (!At<bool>(p, 0x16))
        p->Update(*reinterpret_cast<FEngine **>(cFEng::mInstance), 0);
    PackageWasLoaded(FEPackageManager::Get(), nullptr, p);
    Callback movies = {kMovieStarter, reinterpret_cast<unsigned int>(p), 0};
    p->ForAllObjects(movies.Ref());
    Callback pc = {kHidePCObjects, 0, 0};
    p->ForAllObjects(pc.Ref());
    Callback transfer = {kTransferFlags, 4, 0};
    p->ForAllObjects(transfer.Ref());
    if (*kRaceStatus)
        At<int>(gi, 8) = 1;
    else if (*kFEDatabase && (At<unsigned int>(*kFEDatabase, 0x12c) >> 9 & 1))
        At<int>(gi, 8) = 2;
    else
        At<int>(gi, 8) = 0;
}

bool __fastcall GIPackageWillUnload(void *, void *, FEPackage *p)
{
    Callback movies = {kMovieStopper, 0, 0};
    p->ForAllObjects(movies.Ref());
    Callback disconnect = {kRenderDisconnect, reinterpret_cast<unsigned int>(HACK_FEPkgMgr_GetPackageRenderInfo(p)),
                           reinterpret_cast<unsigned int>(cFEngRender::mInstance)};
    p->ForAllObjects(disconnect.Ref());
    PackageWillBeUnloaded(FEPackageManager::Get(), nullptr, p);
    return true;
}

bool __fastcall GIDoesPointTouchObject(void *, void *, float x, float y, FEObject *o)
{
    return FEngTestForIntersection(x, y, o);
}

void cFEng::MakeLoadedPackagesDirty()
{
    FEngine *e = *reinterpret_cast<FEngine **>(this);
    if (!e)
        return;
    Callback dirty = {kDirtySetter, 0, 0};
    for (FEPackage *p = At<FEPackage *>(e, 0xe0); p; p = At<FEPackage *>(p, 4))
    {
        dirty.a = reinterpret_cast<unsigned int>(HACK_FEPkgMgr_GetPackageRenderInfo(p));
        p->ForAllObjects(dirty.Ref());
    }
}
