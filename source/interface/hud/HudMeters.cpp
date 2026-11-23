/**
 * The race HUD's meters and notices (original: zFe2 HeatMeter::Update 0x5666a0, SpeedBreakerMeter::Update 0x568460,
 * GenericMessage::Update 0x567f80, Infractions::Update 0x569a50, Countdown::GetSecondsBeforeRaceStart 0x567f40).
 *
 * HeatMeter: +0x10 the package, +0x38 changed, +0x3c the heat shown when above 0, +0x40 the heat, +0x44 the level
 * badge, +0x48 the flasher, +0x50/+0x54 the two half dials. A heat's whole part is its level; the fraction fills the
 * first dial to one half, the second beyond. SpeedBreakerMeter: +0x10 the package, +0x38 the charge (0 to 1), +0x3c
 * changed, +0x40 the icon, +0x44 the needle, +0x48 the bar's group, +0x4c the bar, +0x50 its full width.
 * GenericMessage: +0x38 the message, +0x40 its icon, +0x44 shown, +0x48 frames shown, +0x4c its text, +0x8c the
 * script it plays, +0x90 one frame only. Infractions: +0x38 the board, +0x3c four rows. Countdown: +0x14 the
 * seconds left as counted (4 before the count), +0x18 when it last changed.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
template <typename T> T &At(const void *p, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(p)) + offset);
}

const unsigned int kShown = 0x1744b3;
const unsigned int kHidden = 0x16a259;
const unsigned int kHalfway = 0x41e1fedc;
const unsigned int kRising = 0x77031c70;
const unsigned int kHot = 0xda600155;
const unsigned int kHeatLevel = 0x7f91da62;
const unsigned int kBarShown = 0x5b0d9106;
const unsigned int kBarLow = 0x26ded57;
const unsigned int kBarHigh = 0x620e4851;
const unsigned int kBoardIn = 0x5079c8f8;
const unsigned int kBoardDraining = 0x3826a28;
const unsigned int kBoardOut = 0x33113ac;
const float kDial = 175.0f;
const float kSweepDrag = -48.0f;
const float kTicksToSeconds = 0.00025f;
const char *const kEmpty = reinterpret_cast<const char *>(0x890978);

void EnsureScript(FEObject *o, unsigned int script)
{
    if (!FEngIsScriptSet(o, script))
        FEngSetScript(o, script, true);
}

float Dial(float half)
{
    return kDial - (half + half) * kDial;
}
} // namespace

void __fastcall HeatMeterUpdate(char *m, void *, void *player);
void __fastcall SpeedBreakerMeterUpdate(char *m, void *, void *player);
void __fastcall GenericMessageUpdate(char *m, void *, void *player);
void __fastcall InfractionsUpdate(char *m, void *, void *player);
float __fastcall CountdownSecondsBeforeRaceStart(char *c, void *);
NFS_REPLACE(0x5666a0, HeatMeterUpdate)
NFS_REPLACE(0x568460, SpeedBreakerMeterUpdate)
NFS_REPLACE(0x567f80, GenericMessageUpdate)
NFS_REPLACE(0x569a50, InfractionsUpdate)
NFS_REPLACE(0x567f40, CountdownSecondsBeforeRaceStart)

void __fastcall HeatMeterUpdate(char *m, void *, void *)
{
    float heat = At<float>(m, 0x40);
    At<bool>(m, 0x38) = false;
    if (At<float>(m, 0x3c) > 0.0f)
        heat = At<float>(m, 0x3c);
    const int level = static_cast<int>(heat);
    const float part = heat - static_cast<float>(level);
    FEngSetMultiImageRot(At<FEMultiImage *>(m, 0x50), Dial(part > 0.5f ? 0.5f : part));
    FEngSetMultiImageRot(At<FEMultiImage *>(m, 0x54), Dial(part > 0.5f ? part - 0.5f : 0.0f));
    FEObject *badge = At<FEObject *>(m, 0x44);
    if (!(heat >= 1.0f))
        FEngSetInvisible(badge);
    else
    {
        EnsureScript(badge, part < 0.5f ? kHalfway : kShown);
        FEPrintf(At<const char *>(m, 0x10), static_cast<int>(kHeatLevel), "%d", level);
        FEngSetVisible(badge);
    }
    FEObject *flasher = At<FEObject *>(m, 0x48);
    if (!(heat > 0.0f))
        EnsureScript(flasher, kShown);
    else if (!(part > 0.75f))
        EnsureScript(flasher, kRising);
    else
        EnsureScript(flasher, kHot);
}

void __fastcall SpeedBreakerMeterUpdate(char *m, void *, void *)
{
    if (!At<bool>(m, 0x3c))
        return;
    At<bool>(m, 0x3c) = false;
    const float charge = At<float>(m, 0x38);
    FEObject *bar = At<FEObject *>(m, 0x4c);
    if (At<FEMultiImage *>(m, 0x44) && bar)
    {
        const float sweep = bStrICmp(At<const char *>(m, 0x10), "HUD_Drag.fng") ? kDial : kSweepDrag;
        FEngSetMultiImageRot(At<FEMultiImage *>(m, 0x44), -sweep * charge + sweep);
        if (FEngIsScriptSet(At<FEObject *>(m, 0x48), kBarShown))
        {
            const float x = FEngGetTopLeftX(bar);
            FEngSetSizeX(bar, charge * At<float>(m, 0x50));
            FEngSetTopLeftX(bar, x);
            EnsureScript(bar, charge < 0.3f || charge == 0.3f ? kBarLow : kBarHigh);
        }
    }
    if (FEObject *icon = At<FEObject *>(m, 0x40))
        FEngSetScript(icon, charge > 0.0f ? kRising : kShown, true);
}

void __fastcall GenericMessageUpdate(char *m, void *, void *)
{
    if (At<int>(m, 0x44) <= 0)
        return;
    FEObject *message = At<FEObject *>(m, 0x38);
    if (!FEngIsScriptRunning(message, At<unsigned int>(m, 0x8c)) || (At<bool>(m, 0x90) && At<unsigned int>(m, 0x48)))
    {
        EnsureScript(message, kHidden);
        At<int>(m, 0x44) = 0;
        bStrCpy(m + 0x4c, kEmpty);
        EnsureScript(At<FEObject *>(m, 0x40), kHidden);
    }
    ++At<int>(m, 0x48);
}

void __fastcall InfractionsUpdate(char *m, void *, void *)
{
    for (int i = 0; i < 4; ++i)
        if (!FEngIsScriptSet(At<FEObject *>(m, 0x3c + i * 4), kHidden))
            return;
    FEObject *board = At<FEObject *>(m, 0x38);
    if (FEngIsScriptSet(board, kBoardIn) || FEngIsScriptSet(board, kBoardDraining))
        FEngSetScript(board, kBoardOut, true);
}

float __fastcall CountdownSecondsBeforeRaceStart(char *c, void *)
{
    const int left = At<int>(c, 0x14);
    if (left == 4)
        return 3.0f;
    if (left > 0)
        return static_cast<float>(static_cast<double>(left) -
                                  static_cast<double>(WorldTimer.PackedTime - At<int>(c, 0x18)) * kTicksToSeconds);
    return 0.0f;
}
