/**
 * The parts shop's menu from the customization options (gameplay/vehicles/customize.h, docs/CUSTOMIZATION.md):
 * CustomizeSub's parts menu (original 0x7bbe40) lists what the options say for the car, in their order, and a slot
 * option's screen is the roof scoops' (CustomizeParts::Setup 0x7bd060) with the option's slot, name and icon.
 * The original listed body kits, spoilers, rims, hoods and roof scoops, the same for every car.
 */
#include <windows.h>

#include <string.h>

#include "game/gen/api.h"
#include "legacy/link.h"
#include "core/log.h"
#include "gameplay/vehicles/customize.h"
#include "interface/menus/rimbrands.h"

NFS_REPLACE(0x7bbe40, PartsMenu::SetupParts)
NFS_REPLACE(0x7bd060, PartsMenu::SetupPartsScreen)
NFS_REPLACE(0x7a5f40, PartsMenu::CartCategoryName)
NFS_REPLACE(0x7bad10, PartsMenu::IsCategoryLocked)
NFS_REPLACE(0x7a50e0, PartsMenu::IsCategoryNew)

namespace
{
using nfs::customize::Option;

const unsigned int kCategoryLike = 0x105;
const int kScreenCategory = 0x148;
const int kScreenMenu = 0x2c;
const int kMenuSetInitial = 0x40;
const int kScreenInitial = 0x154;
const unsigned int kInitialMask = 0xffff00ff;
const int kScreenTitle = 0x1b0;
const int kScreenPackage = 0x14c;
const int kScreenRedraw = 0x129;
const int kScreenRedrawA = 0x11c;
const int kScreenRedrawB = 0x119;
const int kScreenRedrawC = 0x11a;
const int kScreenRedrawD = 0x110;
const char *const *const kMainScreenPackage = reinterpret_cast<const char *const *>(0x905ea4);
const char *const *const kGenericScreenPackage = reinterpret_cast<const char *const *>(0x905eac);
const char *const *const kSpoilersScreenPackage = reinterpret_cast<const char *const *>(0x905ecc);
const char **const kPartsScreenPackage = reinterpret_cast<const char **>(0x905eb0);
/** CarCustomizeManager (ecx of its calls). */
char *const kCustomizeManager = reinterpret_cast<char *>(0x9b9ec0);
const int kSlotBody = 23;
const int kMaxMenu = 64;

/** The original's own categories: their screen, name and icons (out of and in the back room). */
struct Original
{
    unsigned int category;
    int screen;
    unsigned int title;
    unsigned int icon;
    unsigned int iconBackRoom;
};
enum Screen
{
    kParts,
    kSpoilers,
    kRims,
};
const Original kOriginals[] = {{0x101, kParts, 0x6134c218, 0x28c24f6, 0xaf393dba},
                               {0x102, kSpoilers, 0x94e73021, 0xbb034ea6, 0xc51a4f62},
                               {0x103, kRims, 0xf868eb0b, 0x294d2a3, 0xc19491cc},
                               {0x104, kParts, 0x4d4a88d, 0x28f7092, 0xf375276e},
                               {0x105, kParts, 0x61e8f83c, 0x79165861, 0x25a4375e}};

const Original *OriginalOf(unsigned int category)
{
    for (const Original &o : kOriginals)
    {
        if (o.category == category)
            return &o;
    }
    return nullptr;
}

/** What CustomizeParts::Setup (0x7bd060) has for the roof scoops, its last category in code, as immediates in its
 * machine code (name, slot, a hash the slot options have none of, icon); a slot option's screen takes its own for the
 * time the screen is set up. */
unsigned int *const kImmediates[] = {reinterpret_cast<unsigned int *>(0x7bd0da), reinterpret_cast<unsigned int *>(0x7bd0e6),
                                     reinterpret_cast<unsigned int *>(0x7bd0ed), reinterpret_cast<unsigned int *>(0x7bd0f2)};
const unsigned int kScoops[] = {0x61e8f83c, 0x3e, 0xac8ddefd, 0x79165861};

bool PatchPartsScreen(const unsigned int *from, const unsigned int *to)
{
    for (int i = 0; i < 4; ++i)
    {
        if (*kImmediates[i] != from[i])
        {
            nfs::Log("parts screen: unexpected code at %p: %08x", kImmediates[i], *kImmediates[i]);
            return false;
        }
    }
    DWORD protection;
    const SIZE_T size = 0x7bd0f6 - 0x7bd0da;
    if (!VirtualProtect(kImmediates[0], size, PAGE_EXECUTE_READWRITE, &protection))
        return false;
    for (int i = 0; i < 4; ++i)
        *kImmediates[i] = to[i];
    VirtualProtect(kImmediates[0], size, protection, &protection);
    FlushInstructionCache(GetCurrentProcess(), kImmediates[0], size);
    return true;
}

CarPart *CustomizedPart(int slot)
{
    return NFS_ORIGINAL(0x7a4b10, CarPart *(__thiscall *)(char *, int))(kCustomizeManager, slot);
}

/** The car type being customized (its body's type name), or -1. */
int CustomizedCarType()
{
    CarPart *body = CustomizedPart(kSlotBody);
    if (!body)
        return -1;
    return static_cast<int>(CarPartDB.GetCarType(body->GetCarTypeNameHash()));
}

bool IsSlotOption(unsigned int category)
{
    const Option *o = nfs::customize::ByCategory(category);
    return o && o->kind == nfs::customize::kSlot;
}
} // namespace

class PartsMenu
{
public:
    void SetupParts();
    void SetupPartsScreen();
    unsigned int CartCategoryName(unsigned int slot);
    bool IsCategoryLocked(unsigned int category, int backroom);
    bool IsCategoryNew(unsigned int category);
};

/** CustomizeSub's parts menu (original 0x7bbe40): the options that show for the car, in their order. */
void PartsMenu::SetupParts()
{
    typedef bool(__cdecl * InBackRoom)();
    typedef int(__thiscall * AddOption)(PartsMenu *, const char *, unsigned int, unsigned int, unsigned int);
    const InBackRoom in_back_room = NFS_ORIGINAL(0x575b00, InBackRoom);
    const AddOption add = NFS_ORIGINAL(0x7bb560, AddOption);
    char *self = reinterpret_cast<char *>(this);
    *reinterpret_cast<unsigned int *>(self + kScreenTitle) = in_back_room() ? 0x5d285ae7u : 0x55dce1au;
    NFS_ORIGINAL(0x575b50, void(__cdecl *)(int))(1);
    *reinterpret_cast<const char **>(self + kScreenPackage) = *kMainScreenPackage;
    const bool back_room = in_back_room();
    const Option *menu[kMaxMenu];
    const int count = nfs::customize::PartsMenu(CustomizedCarType(), menu, kMaxMenu);
    if (!count)
    {
        for (const Original &o : kOriginals)
        {
            const char *package = o.screen == kSpoilers ? *kSpoilersScreenPackage
                                  : o.screen == kRims   ? *kGenericScreenPackage
                                                        : *kPartsScreenPackage;
            add(this, package, back_room ? o.iconBackRoom : o.icon, o.title, o.category);
        }
    }
    for (int i = 0; i < count; ++i)
    {
        const Option &o = *menu[i];
        if (const Original *original = OriginalOf(o.category))
        {
            const char *package = original->screen == kSpoilers ? *kSpoilersScreenPackage
                                  : original->screen == kRims   ? *kGenericScreenPackage
                                                                : *kPartsScreenPackage;
            const unsigned int icon = back_room ? (o.iconBackRoom ? o.iconBackRoom : original->iconBackRoom)
                                                : (o.icon ? o.icon : original->icon);
            add(this, package, icon, o.title ? o.title : original->title, o.category);
        }
        else
            add(this, *kPartsScreenPackage, back_room ? o.iconBackRoom : o.icon, o.title, o.category);
    }
    if (self[kScreenRedraw])
    {
        self[kScreenRedrawA] = 0;
        self[kScreenRedrawB] = 1;
        self[kScreenRedrawC] = 0;
        *reinterpret_cast<int *>(self + kScreenRedrawD) = 0;
    }
    char *list = self + kScreenMenu;
    typedef void(__thiscall * SetInitial)(char *, unsigned int);
    (*reinterpret_cast<SetInitial *>(*reinterpret_cast<char **>(list) + kMenuSetInitial))(
        list, *reinterpret_cast<unsigned int *>(self + kScreenInitial) & kInitialMask);
}

/** CustomizeParts::Setup: a slot option's screen is the roof scoops' with its slot, name and icon. */
void PartsMenu::SetupPartsScreen()
{
    unsigned int *category = reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(this) + kScreenCategory);
    const unsigned int own = *category;
    const Option *o = nfs::customize::ByCategory(own);
    unsigned int values[4] = {};
    bool patched = false;
    if (o && o->kind == nfs::customize::kSlot)
    {
        const bool back_room = NFS_ORIGINAL(0x575b00, bool(__cdecl *)())();
        const unsigned int option[] = {o->title, static_cast<unsigned int>(o->slots[0]), 0,
                                       back_room ? o->iconBackRoom : o->icon};
        memcpy(values, option, sizeof(values));
        patched = PatchPartsScreen(kScoops, values);
    }
    if (patched)
        *category = kCategoryLike;
    NFS_ORIGINAL(0x7bd060, void(__thiscall *)(PartsMenu *))(this);
    if (patched)
    {
        PatchPartsScreen(values, kScoops);
        *category = own;
    }
}

/** The name of a cart item's category: a slot option's for its slots. */
unsigned int PartsMenu::CartCategoryName(unsigned int slot)
{
    if (const Option *o = nfs::customize::BySlot(static_cast<int>(slot)))
        return o->title;
    return NFS_ORIGINAL(0x7a5f40, unsigned int(__thiscall *)(PartsMenu *, unsigned int))(this, slot);
}

bool PartsMenu::IsCategoryLocked(unsigned int category, int backroom)
{
    return !IsSlotOption(category) && !nfs::rimbrands::IsCategory(category) &&
           NFS_ORIGINAL(0x7bad10, bool(__thiscall *)(PartsMenu *, unsigned int, int))(this, category, backroom);
}

bool PartsMenu::IsCategoryNew(unsigned int category)
{
    return !IsSlotOption(category) && !nfs::rimbrands::IsCategory(category) &&
           NFS_ORIGINAL(0x7a50e0, bool(__thiscall *)(PartsMenu *, unsigned int))(this, category);
}
