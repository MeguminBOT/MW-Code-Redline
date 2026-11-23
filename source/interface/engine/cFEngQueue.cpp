/**
 * The game's side of the front-end engine: finding packages and queueing their changes and messages (original: zFe
 * cFEng::FindPackageAtBase 0x516ac0, FindPackage 0x516af0, IsPackagePushed 0x516b50, IsPackageInControl 0x516b90,
 * QueueMessage 0x516be0, QueueGameMessage 0x516c50, QueuePackageMessage 0x516c90, PushNoControlPackage 0x516990,
 * QueuePackagePush 0x5257f0, QueuePackagePop 0x5258b0, QueuePackageSwitch 0x525940).
 *
 * cFEng: +0 the FEngine, whose package list is at +0xd8 (head +0xe0, next at +4; tail +0xe4, previous at +8).
 * While a screen is being made (FEPackageData::mInScreenConstructor 0x91d218 above 0) packages are found through the
 * package manager instead. Pushing or switching a package in a race (game flow 6) counts a pause request (FEManager
 * 0x91cae4, the packages' names from 0x91cae8), hides the HUD and puts InGameBackground.fng under it; pushing or
 * switching enables the menus' action queue (cFEngJoyInput 0x91cad0, port 0) and popping the last one under
 * priority 100 disables it.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
template <typename T> T &At(const void *p, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(p)) + offset);
}

const char kInGameBackground[] = "InGameBackground.fng";
const int kRacing = 6;
const int *const kGameFlow = reinterpret_cast<const int *>(0x925e90);
int *const kPauseRequests = reinterpret_cast<int *>(0x91cae4);
const char **const kPauseReasons = reinterpret_cast<const char **>(0x91cae8);
const int *const kInScreenConstructor = reinterpret_cast<const int *>(0x91d218);
void ***const kJoyInput = reinterpret_cast<void ***>(0x91cad0);

using QueueTest = bool(__thiscall *)(void *);
using QueueCall = void(__thiscall *)(void *);
using QueueEnable = void(__thiscall *)(void *, bool);
const QueueTest kIsEnabled = reinterpret_cast<QueueTest>(0x626dd0);
const QueueEnable kEnable = reinterpret_cast<QueueEnable>(0x644060);
const QueueCall kFlush = reinterpret_cast<QueueCall>(0x642d70);
using Plain = void(__cdecl *)();
const Plain kHideEverySingleHud = reinterpret_cast<Plain>(0x57d0f0);

FEngine *Engine(const cFEng *c)
{
    return *reinterpret_cast<FEngine *const *>(c);
}

const char *PackageList(const cFEng *c)
{
    return reinterpret_cast<const char *>(Engine(c)) + 0xd8;
}

FEPackage *FindPushed(const cFEng *c, const char *name)
{
    return reinterpret_cast<FEPackage *>(reinterpret_cast<const FEList *>(PackageList(c))->FindNode(name));
}

/** A push or switch in a race: the pause request, the HUD hidden, the background under the menus. */
void PauseForMenu(cFEng *c, const char *name, bool noPause)
{
    if (*kGameFlow != kRacing || noPause || *kPauseRequests > 0)
        return;
    kPauseReasons[*kPauseRequests] = name;
    ++*kPauseRequests;
    kHideEverySingleHud();
    if (!c->IsPackagePushed(kInGameBackground))
        Engine(c)->QueuePackagePush(kInGameBackground, 0);
}

void EnableMenuInput()
{
    void *queue = (*kJoyInput)[0];
    if (!kIsEnabled(queue))
    {
        kEnable(queue, true);
        kFlush(queue);
    }
}
} // namespace

void __fastcall cFEngQueueMessage(cFEng *c, void *, unsigned int message, const char *package, FEObject *target,
                                  unsigned int pads);
NFS_REPLACE(0x516be0, cFEngQueueMessage)

FEPackage *cFEng::FindPackageAtBase()
{
    const char *list = PackageList(this);
    return list ? At<FEPackage *>(list, 8) : nullptr;
}

FEPackage *cFEng::FindPackage(char const *name)
{
    if (!name || !name[0])
        return nullptr;
    if (*kInScreenConstructor > 0)
        return FEPackageManager::Get()->FindPackage(name);
    if (FEPackage *p = FindPushed(this, name))
        return p;
    return Engine(this)->FindIdlePackage(name);
}

bool cFEng::IsPackagePushed(char const *name)
{
    if (*kInScreenConstructor > 0)
        return FEPackageManager::Get()->FindPackage(name) != nullptr;
    return FindPushed(this, name) != nullptr;
}

bool cFEng::IsPackageInControl(char const *name)
{
    const char *list = PackageList(this);
    if (!list)
        return false;
    for (const char *p = At<const char *>(list, 0xc); p; p = At<const char *>(p, 8))
        if (At<unsigned int>(p, 0x20))
            return bStrCmp(name, At<const char *>(p, 0xc)) == 0;
    return false;
}

/** cFEng::QueueMessage (private, 0x516be0): to the named package, or to every package. */
void __fastcall cFEngQueueMessage(cFEng *c, void *, unsigned int message, const char *package, FEObject *target,
                                  unsigned int pads)
{
    if (package)
    {
        if (FEPackage *p = c->FindPackage(package))
            Engine(c)->QueueMessage(message, nullptr, p, target, pads);
        return;
    }
    const char *list = PackageList(c);
    if (!list)
        return;
    for (FEPackage *p = At<FEPackage *>(list, 8); p; p = At<FEPackage *>(p, 4))
        Engine(c)->QueueMessage(message, nullptr, p, target, 0);
}

void cFEng::QueueGameMessage(unsigned int message, char const *package, unsigned int pads)
{
    cFEngQueueMessage(this, nullptr, message, package, reinterpret_cast<FEObject *>(0xffffffff), pads);
}

void cFEng::QueuePackageMessage(unsigned int message, char const *package, FEObject *target)
{
    cFEngQueueMessage(this, nullptr, message, package, target ? target : reinterpret_cast<FEObject *>(0xfffffffc), 0xff);
}

void cFEng::PushNoControlPackage(char const *name, FE_PACKAGE_PRIORITY priority)
{
    Engine(this)->PushPackage(name, static_cast<unsigned char>(priority), 0);
}

void cFEng::QueuePackagePush(char const *name, int arg, unsigned int pads, bool noPause)
{
    PauseForMenu(this, name, noPause);
    FEPackageManager::Get()->SetPackageDataArg(name, arg);
    Engine(this)->QueuePackagePush(name, pads);
    EnableMenuInput();
}

/** cFEng::QueuePackageSwitch (0x525940), which ProfileOptions.cpp's replacement ends in. */
void cFEngQueuePackageSwitch(cFEng *c, char const *name, int arg, unsigned int pads, bool noPause)
{
    PauseForMenu(c, name, noPause);
    FEPackageManager::Get()->SetPackageDataArg(name, arg);
    Engine(c)->QueuePackageSwitch(name, pads);
    if (*kJoyInput)
        EnableMenuInput();
}

void cFEng::QueuePackagePop(int count)
{
    const int below = Engine(this)->GetNumPackagesBelowPriority(100);
    if (count < 1 || below < count)
        count = below;
    for (; count > 0; --count)
        Engine(this)->QueuePackagePop();
    if (Engine(this)->GetNumPackagesBelowPriority(100) >= 1)
        return;
    if (*kGameFlow == kRacing && *kPauseRequests > 0)
        --*kPauseRequests;
    void *queue = (*kJoyInput)[0];
    if (kIsEnabled(queue))
    {
        kEnable(queue, false);
        kFlush(queue);
    }
}
