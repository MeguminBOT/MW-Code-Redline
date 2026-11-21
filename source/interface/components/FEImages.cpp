/** Front-end images picked by name (original: zFe2 UIWidgetMenu::GetCurrentFEImage, FEngFont::GetJoyEventTextureInfo,
 * StatsPanel::Draw, zFe WorldMap::DrawItemType). */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x573d10, GetCurrentFEImage)
NFS_REPLACE(0x572210, GetJoyEventTextureInfo)
NFS_REPLACE(0x5316a0, DrawItemType)

namespace
{
template <typename T> T &At(const void *base, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(base) + offset));
}

const int kWidgetPackage = 0x10;
const int kWidgetIndex = 0x104;

const int kPanelStats = 0x4;
const int kPanelTitle = 0x10;
const int kPanelPackage = 0x14;
const unsigned int kPanelGroup = 0x8a41f5b9;
const unsigned int kPanelShowTitle = 0x5079c8f8;
const unsigned int kPanelHideTitle = 0x16a259;
const unsigned int kPanelTitleText = 0xeb43ccb0;
const int kPanelButtons[] = {0x5bc, 0x682};

const int kMapPackage = 0x10;
const int kMapSelection = 0x15c;
const int kSelectionType = 0x2c;
const unsigned int kMapItemType = 0x9331fd4f;

struct IconInfo
{
    int unknown;
    int type;
    int unknown8;
    unsigned int name;
    int unknown10;
};
static_assert(sizeof(IconInfo) == 0x14, "Minimap::GameplayIconInfo");
const IconInfo *const kIcons = reinterpret_cast<const IconInfo *>(0x8f3ab8);
const int kIconCount = 17;

FEObject *FindInPackage(const char *package, unsigned int hash)
{
    if (!package)
        return nullptr;
    FEPackage *p = cFEng::mInstance->FindPackage(package);
    return p ? p->FindObjectByHash(hash) : nullptr;
}
} // namespace

FEImage *__fastcall GetCurrentFEImage(UIWidgetMenu *menu, void *, const char *name)
{
    char format[0x20];
    char full[0x20];
    bStrCat(format, name, "%d");
    FEngSNPrintf(full, sizeof(full), format, At<int>(menu, kWidgetIndex));
    const char *package = At<const char *>(menu, kWidgetPackage);
    if (FEImage *image = FEngFindImage(package, FEHashUpper(full)))
        return image;
    return FEngFindImage(package, FEngHashString("%s0", name));
}

const TextureInfo *__fastcall GetJoyEventTextureInfo(FEngFont *, void *, const short *)
{
    return GetTextureInfo(0, 1, 0);
}

void StatsPanel::Draw(unsigned int players)
{
    const char *title = At<const char *>(this, kPanelTitle);
    const char *package = At<const char *>(this, kPanelPackage);
    if (players > 1 && title && bStrCmp(title, "") != 0)
    {
        if (!FEngIsScriptSet(package, kPanelGroup, kPanelShowTitle))
            FEngSetScript(package, kPanelGroup, kPanelShowTitle, true);
        for (int button : kPanelButtons)
            FEngSetButtonTexture(FEngFindImage(package, button), button);
        FEPrintf(package, static_cast<int>(kPanelTitleText), "%s", title);
    }
    else if (!FEngIsScriptSet(package, kPanelGroup, kPanelHideTitle))
        FEngSetScript(package, kPanelGroup, kPanelHideTitle, true);

    char *const head = reinterpret_cast<char *>(this) + kPanelStats;
    for (char *node = *reinterpret_cast<char **>(head); node && node != head; node = *reinterpret_cast<char **>(node))
    {
        void *stat = node - 4;
        using DrawStat = void(__thiscall *)(void *);
        reinterpret_cast<DrawStat>((*reinterpret_cast<void ***>(stat))[3])(stat);
    }
}

void __fastcall DrawItemType(WorldMap *map, void *)
{
    const int type = At<int>(At<void *>(map, kMapSelection), kSelectionType);
    const IconInfo *icon = kIcons;
    for (int i = 0; i < kIconCount; ++i)
    {
        if (kIcons[i].type == type)
        {
            icon = &kIcons[i];
            break;
        }
    }
    const char *package = At<const char *>(map, kMapPackage);
    FEngSetLanguageHash(package, kMapItemType, icon->name);
    FEObject *label = FindInPackage(package, kMapItemType);
    if (icon->name)
        FEngSetVisible(label);
    else
        FEngSetInvisible(label);
}
