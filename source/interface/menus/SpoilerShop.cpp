/**
 * The spoiler shop (original: xPCFrontEnd CustomizeSpoiler::BuildPartOptionListFromFilter 0x7b76c0, ScrollFilters
 * 0x7b78f0, RefreshHeader 0x7b1f30, NotificationMessage 0x7bd560, Setup 0x7bd6e0).
 *
 * The original listed a spoiler under its upgrade group's low bits: Sport, Tuner and the same two in carbon fibre,
 * switched with the triggers. Here the triggers pick the game the spoilers come from (Most Wanted, Carbon,
 * Underground 2) and up and down pick the finish (regular or carbon fibre), on a second switch beneath the first in
 * its style. A part's game is its group's low bits as tools/shopparts.py sort_spoilers sets them (0 Most Wanted's
 * Sport, 1 its Tuner, 2 Carbon, 3 Underground 2, 4 every list: a car's stock spoiler and its own), its finish the
 * CARBONFIBRE attribute. A list runs in unlock order; Most Wanted's takes turns Sport, Tuner, Sport ... within each
 * level, so its Sport spoilers stand at the odd places and its Tuner ones at the even. The car's own spoilers (every
 * list, above level 0) come right after the stock ones, before every game's.
 *
 * CustomizeSpoiler: +0x10 its package, +0x2c its icon menu (+0x38 the option under the cursor), +0x148 the
 * category, +0x129 the menu's scroll to reset, +0x1b4/+0x1b8 the preview's wait and time, +0x1bc the game shown.
 * An option of the part list: +0 vtable (0: delete), +4 its list node, +0xc its CarPart, +0x14 its unlock index.
 */
#include <string.h>

#include "game/gen/api.h"
#include "legacy/link.h"
#include "interface/engine/feng.h"
#include "core/config.h"
#include "core/input.h"
#include "core/log.h"

NFS_REPLACE(0x7b76c0, SpoilerShopBuild)
NFS_REPLACE(0x7b78f0, SpoilerShopScroll)
NFS_REPLACE(0x7b1f30, SpoilerShopHeader)
NFS_REPLACE(0x7bd560, SpoilerShopMessage)
NFS_REPLACE(0x7bd6e0, SpoilerShopSetup)

void __fastcall SpoilerShopBuild(char *self, void *, void *selected);
void __fastcall SpoilerShopScroll(char *self, void *, int direction);
void __fastcall SpoilerShopHeader(char *self, void *);
void __fastcall SpoilerShopMessage(char *self, void *, unsigned int message, unsigned int a, unsigned int b,
                                   unsigned int c);
void __fastcall SpoilerShopSetup(char *self, void *);

namespace
{
template <typename T> T &At(void *base, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}

const int kPackage = 0x10;
const int kMenu = 0x2c;
const int kMenuCurrent = 0x38;
const int kCategory = 0x148;
const int kResetScroll = 0x129;
const int kPreviewWaiting = 0x1b4;
const int kPreviewTime = 0x1b8;
const int kGame = 0x1bc;
const int kNoOption = 0xec;
const int kListHead = 0x30;

const int kOptionNode = 4;
const int kOptionPart = 0xc;
const int kOptionCategoryPart = 0x10;
const int kOptionUnlock = 0x14;

const int kSlotSpoiler = 0x2c;
const unsigned int kAttrCarbon = 0x721aff7c;
const unsigned int kAttrLanguage = 0x4732da07;
const int kEveryList = 4;
const int kGames = 3;
const int kFinishes = 2;
enum Game
{
    kMostWanted,
    kCarbon,
    kUnderground2,
};
/** The games' and finishes' names (tools/shopparts.py SPOILER_TABS). */
const unsigned int kGameNames[kGames] = {0x1f0e2b2, 0x9912746, 0xe7416fc};
const unsigned int kFinishNames[kFinishes] = {0xc051ca8e, 0x20a36711};

const unsigned int kMessageLeftTrigger = 0x5073ef13;
const unsigned int kMessageRightTrigger = 0xd9feec59;
/** The screen's tick, every frame. Up and down reach no package that does not list them (Spoilers.fng lists neither:
 * FEngine::ProcessPadsForPackage 0x5c20a0 queues a pad's message only to a package with a response to it), so the
 * finish switch reads the front end's button table (FEPad_Up, FEPad_Down: their down words per pad) each tick. */
const unsigned int kMessageTick = 0xc98356ba;
unsigned int *const kButtonTable = reinterpret_cast<unsigned int *>(0x8f3428);
const int kButtonWords = 7;
const int kButtonDown = 3;
const int kPads = 4;
const int kButtonUp = 0;
const int kButtonDownArrow = 1;
const unsigned int kMessageLeft = 0x9120409e;
const unsigned int kMessageRight = 0xb5971bf1;
const unsigned int kMessageShowcase = 0xc519bfbf;

/** The switch's name. The original also gave two shoulder pictures their buttons (0x91c4a50, 0x2d145be3), which the PC's
 * package does not have. */
const unsigned int kSwitchName = 0x78008599;
const unsigned int kPartName = 0x5e7b09c9;
/** The switch's black box (its height is a row's) and the area over it a mouse click goes to. */
const unsigned int kSwitchBox = 0xc6afdd7e;
const unsigned int kSwitchHitArea = 0x2c3cc2d3;
const float kSwitchGap = 2.0f;
/** The switch's arrows, which a click on goes to the previous or the next. */
const unsigned int kSwitchArrowPrevious = 0xd1cda594;
const unsigned int kSwitchArrowNext = 0x9345e4a7;
/** The arrows' scripts, as the package's mouse responses play them on the game switch's. */
const unsigned int kScriptHover = 0x02ee84a3;
const unsigned int kScriptPressed = 0xe647b375;
const unsigned int kScriptOff = 0x0000ccfa;
/** The pad's buttons for the switches (action table indices): the game's FE_ACTION_LTRIG and RTRIG (the shoulders),
 * the finish's FE_ACTION_LTRIGGER and RTRIGGER (the triggers; F2 and F1 on the keyboard). Neither trigger is in the
 * front end's button table, so the finish switch reads them from the action table. */
const int kActionGamePrevious = 35;
const int kActionGameNext = 36;
const int kActionFinishPrevious = 30;
const int kActionFinishNext = 29;
/** The front end's cursor (0x987f54, 640 by 480) and the left button gone down this frame. */
const int *const kMouseX = reinterpret_cast<const int *>(0x987f54);
const int *const kMouseY = reinterpret_cast<const int *>(0x987f58);
const unsigned char *const kMouseClicked = reinterpret_cast<const unsigned char *>(0x987f71);
const unsigned int kHiddenWithoutOption[] = {0x8d1559a4, 0x5e7b09c9, 0xf2492598};

void *const kCustomizeManager = reinterpret_cast<void *>(0x9b9ec0);
int *const kShowcaseFilter = reinterpret_cast<int *>(0x905e38);
int *const kShowcaseIndex = reinterpret_cast<int *>(0x9b9e84);
const unsigned int *const kRealTimer = reinterpret_cast<const unsigned int *>(0x925b14);
const unsigned int kPreviewDelay = 0x4b0;

int g_finish;
/** Where each list was left: the option's place from 1 (1 also when the list was never opened). */
int g_place[kGames][kFinishes];

int GameOf(unsigned int group)
{
    switch (group & 0x1f)
    {
    case 0:
    case 1:
        return kMostWanted;
    case 2:
        return kCarbon;
    case 3:
        return kUnderground2;
    }
    return -1;
}

bool IsCarbonFibre(void *part)
{
    return NFS_ORIGINAL(0x747ac0, int(__thiscall *)(void *, unsigned int, unsigned int))(part, kAttrCarbon, 0) != 0;
}

unsigned int GroupOf(void *part)
{
    return At<unsigned char>(part, 5);
}

int &Place(char *self)
{
    return g_place[At<int>(self, kGame)][g_finish];
}

void Virtual(char *object, int offset)
{
    using Fn = void(__thiscall *)(void *);
    reinterpret_cast<Fn>((*reinterpret_cast<void ***>(object))[offset / 4])(object);
}

void VirtualInt(char *object, int offset, int value)
{
    using Fn = void(__thiscall *)(void *, int);
    reinterpret_cast<Fn>((*reinterpret_cast<void ***>(object))[offset / 4])(object, value);
}

void Rebuild(char *self)
{
    SpoilerShopBuild(self, nullptr, nullptr);
    Virtual(self, 0xc);
}

bool Held(int button)
{
    const unsigned int *entry = kButtonTable + button * kButtonWords;
    for (int pad = 0; pad < kPads; ++pad)
        if (entry[kButtonDown + pad])
            return true;
    return false;
}

struct Entry
{
    char *option;
    int level;
    int kind;
    bool own;
    int order;
};
const int kMaxEntries = 256;

/** The list's order: the stock ones, the car's own (user, 2026-11-19), then by unlock level; Most Wanted's Sport and
 * Tuner spoilers taking turns within a level. */
void Order(Entry *entries, int count)
{
    int seen[2][9] = {};
    for (int i = 0; i < count; ++i)
    {
        Entry &e = entries[i];
        const int band = e.level == 0 ? 0 : e.own ? 1 : (e.level & 7) + 1;
        e.order = band * 1024 + seen[e.kind][band]++ * 2 + e.kind;
    }
    for (int i = 1; i < count; ++i)
    {
        const Entry e = entries[i];
        int j = i - 1;
        while (j >= 0 && entries[j].order > e.order)
        {
            entries[j + 1] = entries[j];
            --j;
        }
        entries[j + 1] = e;
    }
}

// ------------------------------------------------------------------------------------------------- the second switch

const int kGroupType = 5;
const int kGroupChildren = 0x64;
const unsigned int kCopyHash = 0x5b7f0000;
const unsigned int kMouseObject = 0x20000;
const int kMaxSwitchObjects = 8;

struct FinishSwitch
{
    FEPackage *package;
    FEObject *name;
    FEObject *box;
    FEObject *arrows[2];
    bool hovered[2];
    unsigned int arrowTextures[2];
    int count;
};
FinishSwitch g_switch;

FEObject *FindIn(FEObject *first, unsigned int hash)
{
    for (FEObject *o = first; o; o = o->Next)
    {
        if (o->NameHash == hash)
            return o;
        if (o->Type == kGroupType)
            if (FEObject *found = FindIn(*reinterpret_cast<FEObject **>(reinterpret_cast<char *>(o) + kGroupChildren), hash))
                return found;
    }
    return nullptr;
}

/** The copy of the game switch's object `hash`: the copy made of it, or a copy of it in a copied group. */
FEObject *CopyOf(FEObject *source, FEObject *copy, unsigned int hash)
{
    if (source->NameHash == hash)
        return copy;
    if (copy->Type == kGroupType)
        return FindIn(*reinterpret_cast<FEObject **>(reinterpret_cast<char *>(copy) + kGroupChildren), hash);
    return nullptr;
}

/** The arrows' pictures: the pad's buttons for the switches while the pad is in use, the arrows' own otherwise. */
int g_padShown = -1;

void ShowPicture(FEObject *arrow, int action, unsigned int otherwise, bool pad)
{
    if (!arrow || !otherwise)
        return;
    const char *picture = pad ? nfs::input::ActionPicture(action) : nullptr;
    FEngSetButtonTexture(reinterpret_cast<FEImage *>(arrow), picture ? bStringHash(picture) : otherwise);
}

void ShowPictures(const char *package, bool force)
{
    const int pad = nfs::input::PadInUse() ? 1 : 0;
    if ((pad == g_padShown && !force) || !g_switch.name)
        return;
    g_padShown = pad;
    ShowPicture(FEngFindObject(package, kSwitchArrowPrevious), kActionGamePrevious, g_switch.arrowTextures[0], pad);
    ShowPicture(FEngFindObject(package, kSwitchArrowNext), kActionGameNext, g_switch.arrowTextures[1], pad);
    ShowPicture(g_switch.arrows[0], kActionFinishPrevious, g_switch.arrowTextures[0], pad);
    ShowPicture(g_switch.arrows[1], kActionFinishNext, g_switch.arrowTextures[1], pad);
}

bool Over(FEObject *object)
{
    return object && FEngTestForIntersection(static_cast<float>(*kMouseX) - 320.0f, static_cast<float>(*kMouseY) - 240.0f,
                                              object);
}

bool g_wasHeld[4] = {true, true, true, true};

/** Up, down, the pad's triggers or a click on the finish switch's row switches the finish (the game switch takes
 * its clicks through the package's mouse objects). */
void PollSwitches(char *self)
{
    const char *package = At<const char *>(self, kPackage);
    const bool held[4] = {Held(kButtonUp), Held(kButtonDownArrow), nfs::input::ActionHeld(kActionFinishPrevious),
                          nfs::input::ActionHeld(kActionFinishNext)};
    int pressed = -1;
    for (int i = 0; i < 4; ++i)
    {
        if (held[i] && !g_wasHeld[i])
            pressed = i & 1;
        g_wasHeld[i] = held[i];
    }
    for (int i = 0; i < 2; ++i)
    {
        FEObject *arrow = g_switch.arrows[i];
        const bool over = Over(arrow);
        if (arrow && over != g_switch.hovered[i])
            FEngSetScript(arrow, over ? kScriptHover : kScriptOff, true);
        g_switch.hovered[i] = over;
        if (*kMouseClicked && over)
            pressed = i;
    }
    if (pressed < 0 && *kMouseClicked && Over(g_switch.box))
        pressed = 1;
    ShowPictures(package, false);
    if (pressed < 0)
        return;
    if (g_switch.arrows[pressed])
        FEngSetScript(g_switch.arrows[pressed], kScriptPressed, true);
    g_finish ^= 1;
    Rebuild(self);
}

FEObject *ParentOf(FEObject *first, FEObject *group, FEObject *target)
{
    for (FEObject *o = first; o; o = o->Next)
    {
        if (o == target)
            return group;
        if (o->Type == kGroupType)
            if (FEObject *parent = ParentOf(*reinterpret_cast<FEObject **>(reinterpret_cast<char *>(o) + kGroupChildren), o,
                                            target))
                return parent;
    }
    return nullptr;
}

/** The objects of the game switch: the group that holds its name, every object in it. */
int SwitchObjects(FEPackage *package, FEObject *name, FEObject **out, FEObject **group)
{
    FEObject *parent = ParentOf(package->GetFirstObject(), nullptr, name);
    *group = parent;
    if (!parent)
        return 0;
    int count = 0;
    for (FEObject *o = *reinterpret_cast<FEObject **>(reinterpret_cast<char *>(parent) + kGroupChildren);
         o && count < kMaxSwitchObjects; o = o->Next)
        out[count++] = o;
    return count;
}

const float *Position(FEObject *object)
{
    return object->pData ? object->pData->Pos : nullptr;
}

unsigned int HighestGuid(FEObject *first)
{
    unsigned int highest = 0;
    for (FEObject *o = first; o; o = o->Next)
    {
        if (o->GUID > highest)
            highest = o->GUID;
        if (o->Type == kGroupType)
        {
            const unsigned int inner =
                HighestGuid(*reinterpret_cast<FEObject **>(reinterpret_cast<char *>(o) + kGroupChildren));
            if (inner > highest)
                highest = inner;
        }
    }
    return highest;
}

/** A copy and what it holds as objects of their own: new GUIDs (FEObject's copy constructor numbers a copy from a
 * counter, which can give it another object's: the footer's buttons), and no mouse objects among them. The package
 * counts its mouse objects once, as it loads (FEPackage::BuildMouseObjectStateList), and each update walks every
 * object with the flag into that array (MouseStateArrayOffsetUpdater): a copied arrow ran past its end, over the heap
 * (the next screen's allocations failed). The finish switch takes its clicks itself (PollSwitches). */
void Renumber(FEObject *object, unsigned int &guid)
{
    object->GUID = ++guid;
    object->Flags &= ~kMouseObject;
    if (object->Type == kGroupType)
        for (FEObject *o = *reinterpret_cast<FEObject **>(reinterpret_cast<char *>(object) + kGroupChildren); o; o = o->Next)
            Renumber(o, guid);
}

/** The finish switch: a copy of the game switch's objects where it was, the game switch moved up a row over it. */
void MakeFinishSwitch(const char *package_name)
{

    FEPackage *package = cFEng::mInstance->FindPackage(package_name);
    if (!package)
        return;
    if (g_switch.package == package && g_switch.name)
        return;
    g_switch = FinishSwitch();
    g_switch.package = package;
    FEObject *name = package->FindObjectByHash(kSwitchName);
    if (!name)
        return;
    FEObject *objects[kMaxSwitchObjects];
    FEObject *group = nullptr;
    const int count = SwitchObjects(package, name, objects, &group);
    if (!count)
    {
        nfs::Log("spoiler shop: the game switch's group was not found");
        return;
    }
    FEObject *box = package->FindObjectByHash(kSwitchBox);
    const float step = (box && box->pData ? box->pData->Size[1] : 32.0f) + kSwitchGap;
    unsigned int guid = HighestGuid(package->GetFirstObject());
    for (int i = 0; i < count; ++i)
    {
        if (!Position(objects[i]))
            continue;
        const float y = Position(objects[i])[1];
        FEObject *copy = nfs::feng::Clone(objects[i], group, kCopyHash + i);
        nfs::feng::SetTrack(objects[i], nfs::feng::kPosition, 1, y - step);
        if (!copy)
            continue;
        Renumber(copy, guid);
        if (objects[i] == name)
            g_switch.name = copy;
        if (FEObject *found = CopyOf(objects[i], copy, kSwitchBox))
            g_switch.box = found;
        if (FEObject *found = CopyOf(objects[i], copy, kSwitchArrowPrevious))
        {
            g_switch.arrows[0] = found;
            g_switch.arrowTextures[0] = found->Handle;
        }
        if (FEObject *found = CopyOf(objects[i], copy, kSwitchArrowNext))
        {
            g_switch.arrows[1] = found;
            g_switch.arrowTextures[1] = found->Handle;
        }
    }
    if (FEObject *hit = package->FindObjectByHash(kSwitchHitArea))
        if (Position(hit))
            nfs::feng::SetTrack(hit, nfs::feng::kPosition, 1, Position(hit)[1] - step);
    g_switch.count = count;
    nfs::Log("spoiler shop: finish switch box %p arrows %p %p (textures %08x %08x)", g_switch.box, g_switch.arrows[0],
             g_switch.arrows[1], g_switch.arrowTextures[0], g_switch.arrowTextures[1]);
    if (nfs::config::Int("Debug", "FEDump", 0) == 3)
        nfs::feng::Dump(package_name);
    g_padShown = -1;
}

void ShowFinish(char *self)
{
    if (!g_switch.name)
        return;
    FEngSetLanguageHash(At<const char *>(self, kPackage), g_switch.name->NameHash, kFinishNames[g_finish]);
}
} // namespace

void __fastcall SpoilerShopBuild(char *self, void *, void *selected)
{
    char *menu = self + kMenu;
    Virtual(menu, 0xc);
    Virtual(menu, 0x3c);
    char *head[2];
    head[0] = reinterpret_cast<char *>(head);
    head[1] = reinterpret_cast<char *>(head);
    NFS_ORIGINAL(0x7af4f0, void(__thiscall *)(void *, int, void *, int))(kCustomizeManager, kSlotSpoiler, head, 0);
    const int game = At<int>(self, kGame);
    Entry entries[kMaxEntries];
    int count = 0;
    while (head[0] != reinterpret_cast<char *>(head))
    {
        char *node = head[0];
        char *next = *reinterpret_cast<char **>(node);
        char *previous = *reinterpret_cast<char **>(node + 4);
        *reinterpret_cast<char **>(previous) = next;
        *reinterpret_cast<char **>(next + 4) = previous;
        char *option = node - kOptionNode;
        void *part = At<void *>(option, kOptionPart);
        const unsigned int group = GroupOf(part);
        const int level = static_cast<int>(group >> 5);
        const bool every = (group & 0x1f) == kEveryList;
        const bool shown = (every || GameOf(group) == game) &&
                           ((every && level == 0) || IsCarbonFibre(part) == (g_finish != 0)) && count < kMaxEntries;
        if (!shown)
        {
            using Delete = void(__thiscall *)(void *, int);
            reinterpret_cast<Delete>((*reinterpret_cast<void ***>(option))[0])(option, 1);
            continue;
        }
        entries[count].option = option;
        entries[count].level = level;
        entries[count].kind = (game == kMostWanted && (group & 0x1f) == 1) ? 1 : 0;
        entries[count].own = every;
        ++count;
    }
    Order(entries, count);
    int &place = Place(self);
    for (int i = 0; i < count; ++i)
    {
        char *option = entries[i].option;
        void *part = At<void *>(option, kOptionPart);
        const unsigned int unlock = NFS_ORIGINAL(0x7b5ef0, unsigned int(__thiscall *)(void *, int, int))(
            kCustomizeManager, At<int>(self, kCategory), At<int>(option, kOptionUnlock));
        const bool back_room = NFS_ORIGINAL(0x575b00, bool(__cdecl *)())();
        unsigned int icon = back_room ? 0xc5174f62 : 0xbb034ea6;
        if (IsCarbonFibre(part))
            icon = back_room ? 0x611d142a : 0x4d1c18ba;
        const int locked = NFS_ORIGINAL(0x7a4e50, int(__thiscall *)(void *, void *, int))(kCustomizeManager, option, 0);
        NFS_ORIGINAL(0x7b1740, void(__thiscall *)(void *, void *, unsigned int, int, int, unsigned int, int))(
            self, option, icon, static_cast<signed char>(GroupOf(part) >> 5), 0, unlock, locked);
        if (place == 1 && selected && part == selected)
            place = i + 1;
    }
    const bool reset = At<char>(self, kResetScroll) != 0;
    int show = place;
    if (*kShowcaseIndex)
    {
        place = *kShowcaseIndex;
        show = *kShowcaseIndex;
        *kShowcaseIndex = 0;
    }
    if (reset)
    {
        At<char>(menu, 0xf0) = 0;
        At<char>(menu, 0xed) = 1;
        At<char>(menu, 0xee) = 0;
        At<int>(menu, 0xe4) = 0;
    }
    VirtualInt(menu, 0x40, show);
    NFS_ORIGINAL(0x7b3f00, void(__thiscall *)(void *))(head);
}

void __fastcall SpoilerShopScroll(char *self, void *, int direction)
{
    int &game = At<int>(self, kGame);
    const int next = (game + (direction < 0 ? kGames - 1 : 1)) % kGames;
    if (direction == 0 || next == game)
        return;
    game = next;
    Rebuild(self);
}

void __fastcall SpoilerShopHeader(char *self, void *)
{
    NFS_ORIGINAL(0x7b1620, void(__thiscall *)(void *))(self);
    const char *package = At<const char *>(self, kPackage);
    MakeFinishSwitch(package);
    const int first = NFS_ORIGINAL(0x45d3a0, int(__thiscall *)(void *, int))(self + kListHead, 0);
    if (first == At<int>(self, kNoOption))
    {
        for (unsigned int hash : kHiddenWithoutOption)
            FEngSetInvisible(FEngFindObject(package, hash));
        return;
    }
    FEngSetLanguageHash(package, kSwitchName, kGameNames[At<int>(self, kGame) % kGames]);
    ShowFinish(self);
    using Current = char *(__thiscall *)(void *);
    char *option = reinterpret_cast<Current>((*reinterpret_cast<void ***>(self))[0x14 / 4])(self);
    if (static_cast<int>(*kRealTimer - At<unsigned int>(self, kPreviewTime)) > static_cast<int>(kPreviewDelay))
        NFS_ORIGINAL(0x7af110, void(__thiscall *)(void *, void *, void *))(kCustomizeManager, At<void *>(option, kOptionCategoryPart),
                                                                          At<void *>(option, kOptionPart));
    else
        At<char>(self, kPreviewWaiting) = 1;
    void *part = At<void *>(option, kOptionPart);
    if (NFS_ORIGINAL(0x747a50, int(__thiscall *)(void *, unsigned int))(part, kAttrLanguage))
        FEngSetLanguageHash(package, kPartName,
                            NFS_ORIGINAL(0x747ae0, unsigned int(__thiscall *)(void *, unsigned int, unsigned int))(part, kAttrLanguage, 0));
    else
        FEPrintf(package, static_cast<int>(kPartName), "%s", NFS_ORIGINAL(0x739850, const char *(__thiscall *)(void *))(part));
}

void __fastcall SpoilerShopMessage(char *self, void *, unsigned int message, unsigned int a, unsigned int b, unsigned int c)
{
    if (message == kMessageTick)
    {
        PollSwitches(self);
        NFS_ORIGINAL(0x7bd560, void(__thiscall *)(void *, unsigned int, unsigned int, unsigned int, unsigned int))(self, message,
                                                                                                                 a, b, c);
        return;
    }
    switch (message)
    {
    case kMessageLeftTrigger:
    case kMessageRightTrigger:
        NFS_ORIGINAL(0x7b7080, void(__thiscall *)(void *, unsigned int, unsigned int, unsigned int, unsigned int))(self, message,
                                                                                                                a, b, c);
        SpoilerShopScroll(self, nullptr, message == kMessageLeftTrigger ? -1 : 1);
        return;
    case kMessageLeft:
    case kMessageRight:
    {
        NFS_ORIGINAL(0x7b7080, void(__thiscall *)(void *, unsigned int, unsigned int, unsigned int, unsigned int))(self, message,
                                                                                                                a, b, c);
        char *menu = self + kMenu;
        void *current = At<void *>(self, kMenuCurrent);
        using IndexOf = int(__thiscall *)(void *, void *);
        Place(self) = current ? reinterpret_cast<IndexOf>((*reinterpret_cast<void ***>(menu))[0x24 / 4])(menu, current) : 0;
        return;
    }
    case kMessageShowcase:
        NFS_ORIGINAL(0x7b7080, void(__thiscall *)(void *, unsigned int, unsigned int, unsigned int, unsigned int))(self, message,
                                                                                                                a, b, c);
        *kShowcaseFilter = At<int>(self, kGame) * kFinishes + g_finish;
        return;
    }
    NFS_ORIGINAL(0x7bd560, void(__thiscall *)(void *, unsigned int, unsigned int, unsigned int, unsigned int))(self, message, a,
                                                                                                             b, c);
}

void __fastcall SpoilerShopSetup(char *self, void *)
{
    g_switch = FinishSwitch();
    At<unsigned int>(self, 0x15c) = 0x94e73021;
    const char *package = At<const char *>(self, kPackage);
    void *active = NFS_ORIGINAL(0x7aed20, void *(__thiscall *)(void *, int))(kCustomizeManager, kSlotSpoiler);
    int &game = At<int>(self, kGame);
    if (*kShowcaseFilter != -1)
    {
        game = (*kShowcaseFilter / kFinishes) % kGames;
        g_finish = *kShowcaseFilter % kFinishes;
        *kShowcaseFilter = -1;
    }
    else
    {
        for (auto &row : g_place)
            for (int &place : row)
                place = 1;
        game = kMostWanted;
        g_finish = 0;
        for (bool &held : g_wasHeld)
            held = true;
        if (active && (GroupOf(active) & 0x1f) != kEveryList)
        {
            game = GameOf(GroupOf(active));
            if (game < 0)
                game = kMostWanted;
            g_finish = IsCarbonFibre(active) ? 1 : 0;
        }
    }
    SpoilerShopBuild(self, nullptr, active);
    Virtual(self, 0xc);
    ShowPictures(package, true);
}
