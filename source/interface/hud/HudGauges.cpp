/**
 * The race HUD's gauges (original: zFe2 Tachometer::Update 0x57a6e0, GetLetterForGear 0x5687a0, SetGear 0x59eb80;
 * NitrousGauge::Update 0x568330, SetNos 0x5683a0; TurboMeter::SetInductionPsi 0x568ef0).
 *
 * Tachometer (the HUD element): +0x40 the needle (in split screen a bar sized to the revs), +0x48 the gear's string,
 * +0x4c the shift light, +0x54 the revs, +0x58 the red line, +0x5c the most revs, +0x60 the gear, +0x64 the gear
 * dimmed, +0x65 the needle inverted (asked), +0x68 the shift potential, +0x6c the needle inverted (shown), +0x70 the
 * bar's full width. Its ITachometer face: +0x30 the gear, +0x38 the shift potential. NitrousGauge: +0x10 the package,
 * +0x38 the nitrous left (0 to 1), +0x40 the needle; its INos face +8 the last value, +0xc the gauge. TurboMeter's
 * ITurbo face: +8 changed, +0xc the boost.
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
const unsigned int kRedLine = 0x61d30442;
const unsigned int kShiftNow = 0x2ddc8f0;
const unsigned int kNosEmpty = 0x1744b3;
const unsigned int kNosDraining = 0x3826a28;
const unsigned int kNosFilling = 0x77031c70;
const unsigned int kGearLit = 0xff000000;
const unsigned int kGearDim = 0x88000000;
const float kNosSweep = 175.0f;
const float kNosSweepSplit = 180.0f;
const float kNosSweepDrag = -48.0f;
const int kSplitScreen = 1;

int Clamp255(int v)
{
    return v < 0 ? 0 : v > 0xff ? 0xff : v;
}

/** An object's colour packed as the engine packs one (its data's blue, green, red, alpha, each kept to 0-255). */
unsigned int ColorOf(FEObject *o)
{
    if (!o)
        return 0;
    const int *c = reinterpret_cast<const int *>(o->pData);
    return static_cast<unsigned int>(Clamp255(c[3])) << 24 | static_cast<unsigned int>(Clamp255(c[2])) << 16 |
           static_cast<unsigned int>(Clamp255(c[1])) << 8 | static_cast<unsigned int>(Clamp255(c[0]));
}

void EnsureScript(FEObject *o, unsigned int script)
{
    if (!FEngIsScriptSet(o, script))
        FEngSetScript(o, script, true);
}
} // namespace

char __cdecl TachometerLetterForGear(int gear);
void __fastcall TachometerUpdate(char *t, void *, void *player);
void __fastcall TachometerSetGear(char *t, void *, int gear, unsigned int potential, bool shifting);
void __fastcall NitrousGaugeUpdate(char *g, void *, void *player);
void __fastcall NitrousGaugeSetNos(char *g, void *, float nos);
void __fastcall TurboMeterSetInductionPsi(char *m, void *, float psi);
NFS_REPLACE(0x5687a0, TachometerLetterForGear)
NFS_REPLACE(0x57a6e0, TachometerUpdate)
NFS_REPLACE(0x59eb80, TachometerSetGear)
NFS_REPLACE(0x568330, NitrousGaugeUpdate)
NFS_REPLACE(0x5683a0, NitrousGaugeSetNos)
NFS_REPLACE(0x568ef0, TurboMeterSetInductionPsi)

char __cdecl TachometerLetterForGear(int gear)
{
    if (gear >= 2 && gear <= 9)
        return static_cast<char>('1' + gear - 2);
    return gear == 0 ? 'R' : 'N';
}

void __fastcall TachometerUpdate(char *t, void *, void *)
{
    FEObject *needle = At<FEObject *>(t, 0x40);
    const float rpm = At<float>(t, 0x54);
    if (Sim::GetUserMode() == kSplitScreen)
    {
        float x, y, w, h, unused;
        FEngGetTopLeft(needle, x, y);
        const float width = rpm / At<float>(t, 0x5c) * At<float>(t, 0x70);
        FEngGetSize(needle, w, h);
        FEngSetSize(needle, width, h);
        FEngGetTopLeft(needle, unused, y);
        FEngSetTopLeft(needle, x, y);
        FEngSetScript(needle, !(rpm >= At<float>(t, 0x58)) ? kShown : kRedLine, true);
    }
    else
        FEngSetRotationZ(needle, CalcAngleForRPM(rpm, At<float>(t, 0x5c)));
    if (FEString *gear = At<FEString *>(t, 0x48))
    {
        FEPrintf(gear, "%c", static_cast<int>(TachometerLetterForGear(At<int>(t, 0x60))));
        if (Sim::GetUserMode() != kSplitScreen)
            FEngSetColor(reinterpret_cast<FEObject *>(gear), At<bool>(t, 0x64) ? kGearDim : kGearLit);
    }
    EnsureScript(At<FEObject *>(t, 0x4c), At<int>(t, 0x68) >= 2 ? kShiftNow : kShown);
    bool &inverted = At<bool>(t, 0x6c);
    if (At<bool>(t, 0x65) != inverted)
    {
        inverted = !inverted;
        FEngSetColor(needle, ~ColorOf(needle) | 0xff000000);
    }
}

void __fastcall TachometerSetGear(char *t, void *, int gear, unsigned int potential, bool shifting)
{
    if (gear != At<int>(t, 0x30))
    {
        At<int>(t, 0x30) = gear;
        At<unsigned int>(t, 0x38) = 0;
        return;
    }
    At<unsigned int>(t, 0x38) = shifting ? potential : 0;
}

void __fastcall NitrousGaugeUpdate(char *g, void *, void *)
{
    FEMultiImage *needle = At<FEMultiImage *>(g, 0x40);
    if (!needle)
        return;
    float sweep = kNosSweep;
    if (!bStrICmp(At<const char *>(g, 0x10), "HUD_Drag.fng"))
        sweep = kNosSweepDrag;
    else if (Sim::GetUserMode() == kSplitScreen)
        sweep = kNosSweepSplit;
    FEngSetMultiImageRot(needle, At<float>(g, 0x38) * -sweep + sweep);
}

void __fastcall NitrousGaugeSetNos(char *g, void *, float nos)
{
    FEObject *gauge = At<FEObject *>(g, 0xc);
    if (nos <= 0.0f)
        EnsureScript(gauge, kNosEmpty);
    else if (nos < At<float>(g, 8))
        EnsureScript(gauge, kNosDraining);
    else
        EnsureScript(gauge, kNosFilling);
    At<float>(g, 8) = nos;
}

void __fastcall TurboMeterSetInductionPsi(char *m, void *, float psi)
{
    if (At<float>(m, 0xc) != psi)
    {
        At<float>(m, 0xc) = psi;
        At<bool>(m, 8) = true;
    }
}
