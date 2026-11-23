/**
 * The race HUD's frame (original: zFe2 FEngHud::Update 0x58ca30): the features shown follow the player's state, a
 * fade left over is lifted once input is back, then each element whose features are shown updates, in the original's
 * order, and the HUD's keys are read.
 *
 * FEngHud: +0x18 the features shown (64 bits), +0x30 its action queue, +0x2c0..+0x32c the elements. An element:
 * vtable +4 Update(IPlayer *), +0x18 the features it belongs to, +0x20 the features it shows in. Some original element
 * updates do not keep EBX/EDI (the original loop only relied on ESI and EBP), so the loop keeps its values in memory.
 */
#include <new>

#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
template <typename T> T &At(const void *p, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(p)) + offset);
}

const int kElements[] = {0x2c0, 0x2c4, 0x2c8, 0x2cc, 0x320, 0x2f8, 0x2fc, 0x300, 0x304, 0x308, 0x30c, 0x2d0, 0x2d4, 0x2d8,
                         0x2e4, 0x2e8, 0x324, 0x2ec, 0x2f0, 0x2dc, 0x2e0, 0x318, 0x31c, 0x328, 0x310};
const int kAlwaysElement = 0x314;
const int kLastElement = 0x32c;
const int kOnlineElement = 0x318;
const unsigned int kFadeOff = 0x14035fb;
const int *const kGameFlowState = reinterpret_cast<const int *>(0x925e90);
const int kLoadingFrontEnd = 4;
const int kUnloadingFrontEnd = 5;
void *const *const kNIS = reinterpret_cast<void *const *>(0x9885c8);
void *const kOnlineManager = reinterpret_cast<void *>(0x9b0fa0);
using IsOnlineRace = bool(__thiscall *)(void *);
const IsOnlineRace kIsOnlineRace = reinterpret_cast<IsOnlineRace>(0x735620);
using SetWideScreenMode = void(__thiscall *)(void *);
const SetWideScreenMode kSetWideScreenMode = reinterpret_cast<SetWideScreenMode>(0x5696c0);

void UpdateElement(void *element, IPlayer *player)
{
    reinterpret_cast<void(__thiscall *)(void *, IPlayer *)>((*reinterpret_cast<void ***>(element))[1])(element, player);
}

bool Shown(const char *element)
{
    return (At<unsigned int>(element, 0x20) & At<unsigned int>(element, 0x18)) ||
           (At<unsigned int>(element, 0x24) & At<unsigned int>(element, 0x1c));
}
} // namespace

unsigned long long __fastcall DetermineHudFeatures(FEngHud *hud, void *, IPlayer *player);
void __fastcall SetHudFeatures(FEngHud *hud, void *, unsigned long long features);
void __fastcall FEngHudUpdate(char *hud, void *, IPlayer *player, float seconds);
NFS_REPLACE(0x58ca30, FEngHudUpdate)

void __fastcall FEngHudUpdate(char *hud, void *, IPlayer *player, float)
{
    FEngHud *self = reinterpret_cast<FEngHud *>(hud);
    const unsigned long long features = DetermineHudFeatures(self, nullptr, player);
    if (features != At<unsigned long long>(hud, 0x18))
        SetHudFeatures(self, nullptr, features);
    if (reinterpret_cast<ActionQueue *>(hud + 0x30)->IsEnabled() && *kGameFlowState != kLoadingFrontEnd &&
        *kGameFlowState != kUnloadingFrontEnd && !FEngHud::bIsRestartingRace && !*kNIS && FadeScreen::IsFadeScreenOn())
    {
        if (void *event = NFS_ORIGINAL(0x627400, void *(__cdecl *)(unsigned int))(0xc))
            new (event) EFadeScreenOff(kFadeOff);
    }
    kSetWideScreenMode(hud);
    if (!features)
    {
        if (kIsOnlineRace(kOnlineManager))
            if (void *online = At<void *>(hud, kOnlineElement))
                UpdateElement(online, player);
    }
    else
    {
        char *volatile frame = hud;
        IPlayer *volatile who = player;
        for (volatile int i = 0; i < static_cast<int>(sizeof(kElements) / sizeof(kElements[0])); ++i)
        {
            char *element = At<char *>(frame, kElements[i]);
            if (element && Shown(element))
                UpdateElement(element, who);
        }
        if (void *always = At<void *>(frame, kAlwaysElement))
            UpdateElement(always, who);
        char *last = At<char *>(frame, kLastElement);
        if (last && Shown(last))
            UpdateElement(last, who);
        char *card = reinterpret_cast<char *>(MemoryCard::s_pThis);
        if (At<bool>(card, 0x1f))
            At<bool>(card, 0x2a) = true;
        hud = frame;
        player = who;
        self = reinterpret_cast<FEngHud *>(frame);
    }
    self->JoyHandle(player);
}
