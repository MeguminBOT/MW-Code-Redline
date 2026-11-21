/** Customization screens: the screen title and the custom HUD previews' textures (original: unassigned
 * CustomizationScreenHelper::DrawTitle, UnLoadCustomHUDPacksAndTextures, HUDLayerOption::React). */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x7a6770, CustomizationDrawTitle)
NFS_REPLACE(0x7a6c60, UnloadCustomHudPreviews)
NFS_REPLACE(0x7b79c0, HudLayerOptionReact)

namespace
{
const int kHudPacks = 11;
const int kTexturesPerPack = 5;
unsigned int *const kPreviewTextures = reinterpret_cast<unsigned int *>(0x9b9d60);
bool *const kPackLoaded = reinterpret_cast<bool *>(0x9b9d50);
bool *const kPreviewsLoaded = reinterpret_cast<bool *>(0x9b9eb0);
const unsigned int kTitle = 0xb71b576d;
} // namespace

void __fastcall CustomizationDrawTitle(char *helper)
{
    char title[0x40];
    bSNPrintf(title, sizeof(title), "%s", GetLocalizedString(*reinterpret_cast<unsigned int *>(helper + 8)));
    const int language = static_cast<int>(GetCurrentLanguage());
    if (language != 2 && language != 13)
        for (char *c = title; *c; ++c)
            if (*c >= 'A' && *c <= 'Z')
                *c |= 0x20;
    FEPrintf(*reinterpret_cast<const char **>(helper + 4), static_cast<int>(kTitle), "%s", title);
}

void __cdecl UnloadCustomHudPreviews()
{
    for (int pack = 0; pack < kHudPacks; ++pack)
    {
        for (int i = 0; i < kTexturesPerPack; ++i)
        {
            unsigned int &texture = kPreviewTextures[pack * kTexturesPerPack + i];
            if (texture)
            {
                unsigned int hash = texture;
                eUnloadStreamingTexture(&hash, 1);
            }
            texture = 0;
        }
        if (kPackLoaded[pack])
        {
            char name[0x40];
            bSPrintf(name, "GLOBAL\\HUDS_Custom_%2.2d.bin", pack);
            eUnloadStreamingTexturePack(name);
        }
        kPackLoaded[pack] = false;
    }
    *kPreviewsLoaded = false;
}

void __fastcall HudLayerOptionReact(void *, void *, int, int, int, int, int)
{
}
