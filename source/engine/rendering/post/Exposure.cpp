/** The smoothed scene exposure and the time-of-day object (original: zPlatform sub_6C37F0, sub_6C3870, sub_6BF290). */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x6c37f0, SmoothExposure)
NFS_REPLACE(0x6c3870, UpdateExposure)
NFS_REPLACE(0x6bf290, CreateTimeOfDay)

namespace
{
template <typename T> T &Global(unsigned int address)
{
    return *reinterpret_cast<T *>(address);
}

const unsigned int kExposure = 0x988544;
const unsigned int kSmoothing = 0x9018c0;
const unsigned int kLevelA = 0x8f9478;
const unsigned int kLevelAFrom = 0x8f947c;
const unsigned int kLevelATo = 0x8f9480;
const unsigned int kLevelB = 0x8f9470;
const unsigned int kLevelBFrom = 0x8f9484;
const unsigned int kLevelBTo = 0x8f9488;
const unsigned int kFixedOn = 0x982c70;
const unsigned int kFixedAmount = 0x8f9b58;
const unsigned int kGameFlowState = 0x925e90;
const unsigned int kCamera = 0x9196b4;
const unsigned int kLow = 0x8f9460;
const unsigned int kHigh = 0x8f9464;
const unsigned int kTimeOfDay = 0x9b392c;
const int kGameFlowRacing = 6;
const float kFrontEndAmount = 0.45f;

int Level(unsigned int from, unsigned int to, double exposure)
{
    const int base = Global<int>(from);
    return static_cast<int>(static_cast<double>(Global<int>(to) - base) * exposure + static_cast<double>(base));
}
} // namespace

void __cdecl SmoothExposure(float target)
{
    const double k = Global<float>(kSmoothing);
    float &exposure = Global<float>(kExposure);
    exposure = static_cast<float>((1.0 - k) * exposure + k * target);
    Global<int>(kLevelA) = Level(kLevelAFrom, kLevelATo, exposure);
    Global<int>(kLevelB) = Level(kLevelBFrom, kLevelBTo, exposure);
}

void __cdecl UpdateExposure()
{
    float amount = 0.0f;
    if (Global<char>(kFixedOn))
        amount = Global<float>(kFixedAmount);
    else if (Global<int>(kGameFlowState) != kGameFlowRacing)
        amount = kFrontEndAmount;
    else
    {
        const char *camera = Global<const char *>(kCamera);
        if (*reinterpret_cast<const int *>(camera + 0x30))
            amount = *reinterpret_cast<const float *>(camera + 0x140);
    }
    const double low = Global<float>(kLow);
    SmoothExposure(static_cast<float>((Global<float>(kHigh) - low) * amount + low));
}

void __cdecl CreateTimeOfDay()
{
    void *memory = NFS_ORIGINAL(0x652ac0, void *(__cdecl *)(unsigned int))(0x190);
    Global<void *>(kTimeOfDay) = memory ? NFS_ORIGINAL(0x770d70, void *(__thiscall *)(void *))(memory) : nullptr;
}
