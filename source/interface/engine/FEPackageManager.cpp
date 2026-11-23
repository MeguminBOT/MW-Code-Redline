/**
 * The front end's package data and the screens made for them (original: zFe2 FEPackageData::FEPackageData 0x585400,
 * ~FEPackageData 0x5854e0, Activate 0x585520, Close 0x5855b0; FEPackageManager::NotifySoundMessage 0x585700,
 * CloseAllPackages 0x585770, FindFEPackageData 0x5857e0 and 0x585820, SetPackageDataArg 0x585920, PackageWasLoaded
 * 0x585950, PackageWillBeUnloaded 0x585980, FindPackage 0x591d90, GetPackageData 0x591db0, GetVisibility 0x591e30,
 * BroadcastMessage 0x5a8b00, NotificationMessage 0x5a8ba0, Tick 0x5a8c20).
 *
 * FEPackageManager (0xc): vtable, then its list of package data (next at +0, previous at +4; the list head is the
 * manager's +4, so the list ends at the data "at" the manager). FEPackageData (0x38): vtable 0x8a129c, list links at
 * +4/+8, +0xc the pads its package had, +0x14 the package's bytes, +0x18 its chunk (0x30203: the package itself from
 * +8; 0x30210: its name hash at +8, the packed package from +0xc, its unpacked size at +0x14), +0x1c the screen made for
 * it, +0x20 the loaded package, +0x24 kept loaded, +0x26 visible, +0x28 its screen table entry, +0x34 the argument the
 * screen is made with. Finding package data by name moves it to the list's front.
 *
 * The screen table (0x8f4320, 0xb2 entries): a package name and the function that makes its screen from the name, the
 * package and the argument.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
template <typename T> T &At(const void *p, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(p)) + offset);
}

const unsigned int kPlain = 0x30203;
const unsigned int kPacked = 0x30210;
const unsigned int kInputMessage = 0x9803f6e2;
const unsigned int kTickMessage = 0xc98356ba;
void *const kDataVtable = reinterpret_cast<void *>(0x8a129c);
void *const kDisconnectVtable = reinterpret_cast<void *>(0x89b4b0);
int *const kInScreenConstructor = reinterpret_cast<int *>(0x91d218);
void *const kMouse = reinterpret_cast<void *>(0x987f50);
using MouseActive = bool(__thiscall *)(void *);
const MouseActive kMouseActive = reinterpret_cast<MouseActive>(0x6c9d00);

struct ScreenData
{
    const char *name;
    FEPackage *package;
    int arg;
};

struct ScreenEntry
{
    const char *name;
    MenuScreen *(__cdecl *create)(ScreenData *);
};

ScreenEntry *const kScreens = reinterpret_cast<ScreenEntry *>(0x8f4320);
const int kScreenCount = 0xb2;

struct Disconnect
{
    void *vtable;
    FEPackageRenderInfo *info;
    cFEngRender *render;
};

char *DataOf(char *node)
{
    return node ? node - 4 : nullptr;
}

char *First(FEPackageManager *m)
{
    return DataOf(At<char *>(m, 4));
}

char *Next(char *data)
{
    return DataOf(At<char *>(data, 4));
}

bool AtEnd(FEPackageManager *m, char *data)
{
    return data == reinterpret_cast<char *>(m);
}

unsigned int PackageHash(const char *data)
{
    const unsigned int *chunk = At<unsigned int *>(data, 0x18);
    if (chunk[0] == kPlain)
    {
        const unsigned int *package = At<unsigned int *>(data, 0x14);
        if (package[0] == 0xe76e4546 && package[2] == 0x64486b50 && package[4] > 0x1ffff)
            return FEHashUpper(reinterpret_cast<const char *>(package + 10));
        return 0;
    }
    return chunk[0] == kPacked ? chunk[2] : 0;
}

MenuScreen *MakeScreen(unsigned int hash, FEPackage *p, int arg)
{
    for (int i = 0; i < kScreenCount; ++i)
        if (FEHashUpper(kScreens[i].name) == hash && kScreens[i].create)
        {
            ScreenData d = {kScreens[i].name, p, arg};
            return kScreens[i].create(&d);
        }
    return nullptr;
}

/** The screen deleted, the package let go and a packed package's bytes freed. */
void Release(char *data)
{
    if (void *screen = At<void *>(data, 0x1c))
        reinterpret_cast<void(__thiscall *)(void *, int)>((*static_cast<void ***>(screen))[0])(screen, 1);
    At<void *>(data, 0x1c) = nullptr;
    if (FEPackage *p = At<FEPackage *>(data, 0x20))
        At<void *>(p, 0x40) = nullptr;
    At<void *>(data, 0x20) = nullptr;
    if (At<unsigned int *>(data, 0x18)[0] == kPacked)
    {
        bFree(At<void *>(data, 0x14));
        At<void *>(data, 0x14) = nullptr;
    }
}
} // namespace

char *__fastcall FindPackageDataByChunk(FEPackageManager *m, void *, const void *chunk);
char *__fastcall FindPackageDataByName(FEPackageManager *m, void *, const char *name);
void __fastcall PackageWasLoaded(FEPackageManager *m, void *, FEPackage *p);
void __fastcall PackageWillBeUnloaded(FEPackageManager *m, void *, FEPackage *p);
void __fastcall PackageNotifySound(FEPackageManager *m, void *, unsigned int message, FEObject *o, unsigned int a,
                                   unsigned int package);
void __fastcall PackageNotification(FEPackageManager *m, void *, unsigned int message, FEObject *o, unsigned int a,
                                    unsigned int package);
void __fastcall PackageBroadcast(FEPackageManager *m, void *, unsigned int message);
void *__fastcall GetPackageData(FEPackageManager *m, void *, const char *name);
void __fastcall DestroyPackageData(char *data, void *);
NFS_REPLACE(0x5857e0, FindPackageDataByChunk)
NFS_REPLACE(0x585820, FindPackageDataByName)
NFS_REPLACE(0x585950, PackageWasLoaded)
NFS_REPLACE(0x585980, PackageWillBeUnloaded)
NFS_REPLACE(0x585700, PackageNotifySound)
NFS_REPLACE(0x5a8ba0, PackageNotification)
NFS_REPLACE(0x5a8b00, PackageBroadcast)
NFS_REPLACE(0x591db0, GetPackageData)
NFS_REPLACE(0x5854e0, DestroyPackageData)

FEPackageData::FEPackageData(bChunk *chunk)
{
    char *self = reinterpret_cast<char *>(this);
    const unsigned int *c = reinterpret_cast<const unsigned int *>(chunk);
    At<void *>(self, 0) = kDataVtable;
    At<short>(self, 0x26) = 1;
    At<const unsigned int *>(self, 0x18) = c;
    At<void *>(self, 0x1c) = nullptr;
    At<void *>(self, 0x20) = nullptr;
    At<unsigned int>(self, 0xc) = 0;
    At<unsigned int>(self, 0x10) = 0;
    At<short>(self, 0x24) = 0;
    At<void *>(self, 0x28) = nullptr;
    At<unsigned int>(self, 0x2c) = 0;
    At<unsigned char>(self, 0x30) = 0;
    At<int>(self, 0x34) = 0;
    if (c[0] == kPlain)
        At<const unsigned int *>(self, 0x14) = c + 2;
    else if (c[0] == kPacked)
        At<void *>(self, 0x14) = nullptr;
    const unsigned int hash = PackageHash(self);
    for (int i = 0; i < kScreenCount; ++i)
        if (FEHashUpper(kScreens[i].name) == hash)
        {
            At<ScreenEntry *>(self, 0x28) = kScreens + i;
            return;
        }
}

void __fastcall DestroyPackageData(char *data, void *)
{
    At<void *>(data, 0) = kDataVtable;
    if (At<unsigned int *>(data, 0x18)[0] == kPacked)
        bFree(At<void *>(data, 0x14));
    At<void *>(data, 0x14) = nullptr;
}

void FEPackageData::Activate(FEPackage *p, int arg)
{
    char *self = reinterpret_cast<char *>(this);
    At<FEPackage *>(self, 0x20) = p;
    At<char *>(p, 0x40) = self;
    ++*kInScreenConstructor;
    At<MenuScreen *>(self, 0x1c) = MakeScreen(PackageHash(self), p, arg);
    At<unsigned int>(self, 0xc) = At<unsigned int>(p, 0x20);
    --*kInScreenConstructor;
}

void FEPackageData::Close()
{
    char *self = reinterpret_cast<char *>(this);
    FEPackage *p = At<FEPackage *>(self, 0x20);
    if (!p)
        return;
    HackClearCache(p);
    Disconnect d = {kDisconnectVtable, HACK_FEPkgMgr_GetPackageRenderInfo(p), cFEngRender::mInstance};
    p->ForAllObjects(*reinterpret_cast<FEObjectCallback *>(&d));
    (*reinterpret_cast<FEngine **>(cFEng::mInstance))->UnloadPackage(At<FEPackage *>(self, 0x20));
    Release(self);
}

char *__fastcall FindPackageDataByChunk(FEPackageManager *m, void *, const void *chunk)
{
    for (char *d = First(m); !AtEnd(m, d); d = Next(d))
        if (At<const void *>(d, 0x18) == chunk)
            return d;
    return nullptr;
}

char *__fastcall FindPackageDataByName(FEPackageManager *m, void *, const char *name)
{
    const char *base = name + bStrLen(name);
    for (; base != name; --base)
        if (*base == '\\')
        {
            ++base;
            break;
        }
    const unsigned int hash = FEHashUpper(base);
    char **head = &At<char *>(m, 4);
    if (!*head)
        return nullptr;
    for (char *d = DataOf(*head); !AtEnd(m, d); d = Next(d))
    {
        if (PackageHash(d) != hash)
            continue;
        char **node = reinterpret_cast<char **>(d + 4);
        char *next = node[0];
        char **prev = reinterpret_cast<char **>(node[1]);
        *prev = next;
        reinterpret_cast<char **>(next)[1] = reinterpret_cast<char *>(prev);
        char *oldFirst = *head;
        *head = reinterpret_cast<char *>(node);
        reinterpret_cast<char **>(oldFirst)[1] = reinterpret_cast<char *>(node);
        node[1] = reinterpret_cast<char *>(head);
        node[0] = oldFirst;
        return d;
    }
    return nullptr;
}

bool FEPackageManager::SetPackageDataArg(char const *name, int arg)
{
    char *d = FindPackageDataByName(this, nullptr, name);
    if (!d)
        return false;
    At<int>(d, 0x34) = arg;
    return true;
}

FEPackage *FEPackageManager::FindPackage(char const *name)
{
    char *d = FindPackageDataByName(this, nullptr, name);
    return d ? At<FEPackage *>(d, 0x20) : nullptr;
}

bool FEPackageManager::GetVisibility(char const *name)
{
    char *d = FindPackageDataByName(this, nullptr, name);
    return d && At<short>(d, 0x26) != 0;
}

void __fastcall PackageWasLoaded(FEPackageManager *m, void *, FEPackage *p)
{
    if (char *d = FindPackageDataByName(m, nullptr, At<const char *>(p, 0xc)))
        reinterpret_cast<FEPackageData *>(d)->Activate(p, At<int>(d, 0x34));
}

void __fastcall PackageWillBeUnloaded(FEPackageManager *m, void *, FEPackage *p)
{
    if (char *d = FindPackageDataByName(m, nullptr, At<const char *>(p, 0xc)))
        Release(d);
}

void *__fastcall GetPackageData(FEPackageManager *m, void *, const char *name)
{
    char *d = FindPackageDataByName(m, nullptr, name);
    if (!d)
        return nullptr;
    const unsigned int *chunk = At<unsigned int *>(d, 0x18);
    if (chunk[0] == kPlain)
        return At<void *>(d, 0x14);
    if (chunk[0] != kPacked)
        return nullptr;
    unsigned char *bytes = static_cast<unsigned char *>(bMalloc(static_cast<int>(chunk[5]), 0));
    At<void *>(d, 0x14) = bytes;
    LZDecompress(reinterpret_cast<unsigned char *>(const_cast<unsigned int *>(chunk + 3)), bytes);
    return bytes;
}

void FEPackageManager::CloseAllPackages(int all)
{
    for (char *d = First(this); !AtEnd(this, d); d = Next(d))
    {
        if (At<short>(d, 0x24) == 0 || all)
            reinterpret_cast<FEPackageData *>(d)->Close();
        else if (FEPackage *p = At<FEPackage *>(d, 0x20))
            HackClearCache(p);
    }
}

void __fastcall PackageNotifySound(FEPackageManager *m, void *, unsigned int message, FEObject *o, unsigned int a,
                                   unsigned int package)
{
    for (char *d = First(m); !AtEnd(m, d); d = Next(d))
        if (At<unsigned int>(d, 0x20) && package == At<unsigned int>(d, 0x20) && At<MenuScreen *>(d, 0x1c))
            At<MenuScreen *>(d, 0x1c)->BaseNotifySound(message, o, a, package);
}

void __fastcall PackageNotification(FEPackageManager *m, void *, unsigned int message, FEObject *o, unsigned int a,
                                    unsigned int package)
{
    for (char *d = First(m); !AtEnd(m, d); d = Next(d))
        if (At<unsigned int>(d, 0x20) && package == At<unsigned int>(d, 0x20) && At<MenuScreen *>(d, 0x1c))
            At<MenuScreen *>(d, 0x1c)->BaseNotify(message, o, a, package);
}

void __fastcall PackageBroadcast(FEPackageManager *m, void *, unsigned int message)
{
    char *to[32];
    int count = 0;
    for (char *d = First(m); !AtEnd(m, d); d = Next(d))
    {
        FEPackage *p = At<FEPackage *>(d, 0x20);
        if (p && count < 32 && (message != kInputMessage || At<char>(p, 0x28)))
            to[count++] = d;
    }
    for (int i = 0; i < count; ++i)
    {
        FEPackage *p = At<FEPackage *>(to[i], 0x20);
        if (p && At<MenuScreen *>(to[i], 0x1c))
            At<MenuScreen *>(to[i], 0x1c)->BaseNotify(message, nullptr, 0, reinterpret_cast<unsigned int>(p));
    }
}

void FEPackageManager::Tick()
{
    if (kMouseActive(kMouse))
        PackageBroadcast(this, nullptr, kInputMessage);
    PackageBroadcast(this, nullptr, kTickMessage);
}
