/** The HUD speedometer's digits and units (original: zFe2 Speedometer::Update, SetSpeed, ISpeedometer destructor). */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x57a540, SpeedometerUpdate)
NFS_REPLACE(0x59e900, SpeedometerSetSpeed)
NFS_REPLACE(0x565130, ISpeedometerDestroy)
NFS_REPLACE(0x565170, ISpeedometerDelete)

namespace
{
const int kSpeed = 0x48;
const int kSetSpeed = 0x18;
const int kUnits = 0x44;
const int kHundreds = 0x40;
const int kTens = 0x3c;
const int kOnes = 0x38;
const int kProfileMetric = 0x3b;
const unsigned int kKmh = 0x8569a25f;
const unsigned int kMph = 0x8569ab44;
const unsigned int kMissing = 0x9bb9ccc3;

template <typename T> T &At(const void *base, int offset)
{
    return *reinterpret_cast<T *>(const_cast<char *>(static_cast<const char *>(base) + offset));
}

const char *Text(unsigned int hash)
{
    const void *search = NFS_ORIGINAL(0x56bb80, const void *);
    const char *found;
    __asm {
        mov edx, hash
        call search
        mov found, eax
    }
    return found;
}

const char *Units(unsigned int hash)
{
    const char *text = Text(hash);
    return text ? text : Text(kMissing);
}
} // namespace

void __fastcall SpeedometerUpdate(void *speedometer, void *, IPlayer *)
{
    char *self = static_cast<char *>(speedometer);
    float speed = At<float>(self, kSpeed);
    speed = speed < 0.0f ? -speed : speed;
    const bool metric = At<char>(FEDatabase->CurrentUserProfile, kProfileMetric) == 1;
    speed *= metric ? 3.6f : 2.23699f;
    FEPrintf(At<FEString *>(self, kUnits), Units(metric ? kKmh : kMph));

    const int hundreds = static_cast<int>(speed * 0.01f);
    speed = speed - static_cast<float>(hundreds) * 100.0f;
    const int tens = static_cast<int>(speed * 0.1f);
    FEPrintf(At<FEString *>(self, kHundreds), "%d", hundreds);
    FEPrintf(At<FEString *>(self, kTens), "%d", tens);
    FEPrintf(At<FEString *>(self, kOnes), "%d", static_cast<int>(speed - static_cast<float>(tens) * 10.0f));

    FEObject *hundredsObject = At<FEObject *>(self, kHundreds);
    FEObject *tensObject = At<FEObject *>(self, kTens);
    if (hundreds > 0)
    {
        FEngSetVisible(hundredsObject);
        FEngSetVisible(tensObject);
    }
    else
    {
        FEngSetInvisible(hundredsObject);
        if (tens > 0)
            FEngSetVisible(tensObject);
        else
            FEngSetInvisible(tensObject);
    }
    FEngSetVisible(At<FEObject *>(self, kOnes));
}

void __fastcall SpeedometerSetSpeed(void *speedometer, void *, float speed)
{
    At<float>(speedometer, kSetSpeed) = speed;
}

void __fastcall ISpeedometerDestroy(void *speedometer)
{
    void **self = static_cast<void **>(speedometer);
    self[0] = reinterpret_cast<void *>(0x890970);
    NFS_ORIGINAL(0x5d5a50, void(__thiscall *)(void *, void *))(self[1], speedometer);
}

void *__fastcall ISpeedometerDelete(void *speedometer, void *, unsigned int flags)
{
    ISpeedometerDestroy(speedometer);
    if (flags & 1)
        NFS_ORIGINAL(0x652b00, void(__cdecl *)(void *))(speedometer);
    return speedometer;
}