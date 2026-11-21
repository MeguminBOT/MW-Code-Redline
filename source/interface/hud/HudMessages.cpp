/** Script calls that put messages on the HUD (original: zLua HUD_ShowMessage, HUD_ShowTimeExtension; zFEng
 * FEMultiImage::GetTexture). */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x611320, HudShowMessage)
NFS_REPLACE(0x6113e0, HudShowTimeExtension)

namespace
{
const char kLanguagePrefix[] = "LANGUAGE_";
void *const kGenericMessageHandle = reinterpret_cast<void *>(0x5650b0);
const unsigned int kMessageStyle = 0x8ab83edb;
const int kMessagePriority = 5;
const int kMultiImageTextures = 3;

void *FirstPlayer()
{
    if (!*reinterpret_cast<const int *>(0x92d884))
        return nullptr;
    return **reinterpret_cast<void ***>(0x92d87c);
}

template <typename R> R Virtual(void *object, int slot)
{
    using Fn = R(__thiscall *)(void *);
    return reinterpret_cast<Fn>((*reinterpret_cast<void ***>(object))[slot / 4])(object);
}
} // namespace

void __cdecl HudShowMessage(const char *name)
{
    if (!name || !*name)
        return;
    const int prefix = bStrLen(kLanguagePrefix);
    if (bStrNCmp(name, kLanguagePrefix, prefix) == 0)
        name += prefix;
    const unsigned int hash = bStringHash(name);
    void *player = FirstPlayer();
    if (!player)
        return;
    void *hud = Virtual<void *>(player, 0x20);
    if (!hud)
        return;
    void *list = *reinterpret_cast<void **>(static_cast<char *>(hud) + 4);
    void *message = NFS_ORIGINAL(0x5d59f0, void *(__thiscall *)(void *, void *))(list, kGenericMessageHandle);
    if (!message)
        return;
    using Show = void(__thiscall *)(void *, const char *, int, unsigned int, int, int, int);
    reinterpret_cast<Show>((*reinterpret_cast<void ***>(message))[1])(message, GetTranslatedString(hash), 0,
                                                                      kMessageStyle, 0, 0, kMessagePriority);
}

void __cdecl HudShowTimeExtension(float seconds)
{
    void *event = NFS_ORIGINAL(0x627400, void *(__cdecl *)(unsigned int))(0x10);
    if (event)
        NFS_ORIGINAL(0x6252d0, void(__thiscall *)(void *, void *, float))(event, FirstPlayer(), seconds);
}

unsigned int FEMultiImage::GetTexture(unsigned int index)
{
    if (index >= kMultiImageTextures)
        return 0xffffffffu;
    return reinterpret_cast<const unsigned int *>(reinterpret_cast<const char *>(this) + 0x60)[index];
}
