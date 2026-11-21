/** Language-dependent menu textures (original: zFe uiRepSheetMain/uiRepSheetRival, UITrackMapStreamer, zFe2
 * PostPursuitInfractionsScreen). */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x510ac0, CalcMapTextureHash)
NFS_REPLACE(0x57d380, CalcBustedTexture)

namespace
{
const int kLanguages = 14;
const unsigned int kDefeated[kLanguages] = {0x87b7d0a, 0x87b81cd, 0x87b846e, 0x87b8ece, 0x87bb8d4,
                                            0x87b79bd, 0x87bb9bf, 0x87b7723, 0x87b96bc, 0x87b73c4,
                                            0x87b90ab, 0x87bbc0d, 0x87babfb, 0x87b80ad};
const unsigned int kRivalDefeated[kLanguages] = {0x87b7d0a, 0x87b81cd, 0x87b846e, 0x87b8ece, 0x87bb8d4,
                                                 0x87b79bd, 0x87bb9bf, 0x87b7723, 0x87b7d0a, 0x87b7d0a,
                                                 0x87b7d0a, 0x87b7d0a, 0x87babfb, 0x87b80ad};
const unsigned int kBusted[kLanguages] = {0xb419ec5f, 0xb419f122, 0xb419f3c3, 0xb419fe23, 0xb41a2829,
                                          0xb419e912, 0xb41a2914, 0xb419e678, 0xb41a0611, 0xb419e319,
                                          0xb41a0000, 0xb41a2b62, 0xb41a1b50, 0xb419f002};
const int kScreenPackage = 0x10;
const unsigned int kRepSheetDefeated = 0x7fe4020f;
const unsigned int kInfractionsBusted = 0x2347122a;

unsigned int ByLanguage(const unsigned int *table)
{
    const unsigned int language = static_cast<unsigned int>(GetCurrentLanguage());
    return language < kLanguages ? table[language] : table[0];
}

const char *Package(unsigned int screen)
{
    return *reinterpret_cast<const char **>(screen + kScreenPackage);
}
} // namespace

unsigned int uiRepSheetMain::GetDefeatedTexture()
{
    return ByLanguage(kDefeated);
}

unsigned int uiRepSheetRival::GetDefeatedTexture()
{
    return ByLanguage(kRivalDefeated);
}

unsigned int __fastcall CalcBustedTexture(PostPursuitInfractionsScreen *)
{
    return ByLanguage(kBusted);
}

unsigned int __fastcall CalcMapTextureHash(UITrackMapStreamer *streamer)
{
    const char *p = reinterpret_cast<const char *>(streamer);
    GRaceParameters *race = *reinterpret_cast<GRaceParameters *const *>(p + 0xc);
    if (race)
        return CalcLanguageHash("TRACK_MAP_", race);
    const int unlock = *reinterpret_cast<const int *>(p + 0x18);
    if (unlock)
        return FEngHashString("TRACK_MAP_UNLOCK_%d", unlock);
    return FEHashUpper("TRACK_MAP");
}

void uiRepSheetMain::TextureLoadedCallback(unsigned int screen)
{
    FEObject *image = nullptr;
    if (const char *name = Package(screen))
        if (FEPackage *package = cFEng::mInstance->FindPackage(name))
            image = package->FindObjectByHash(kRepSheetDefeated);
    FEngSetVisible(image);
}

void PostPursuitInfractionsScreen::TextureLoadedCallback(unsigned int screen)
{
    FEngSetVisible(FEngFindObject(Package(screen), kInfractionsBusted));
}
