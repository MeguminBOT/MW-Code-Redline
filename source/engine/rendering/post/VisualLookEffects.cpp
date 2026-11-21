/** Timed visual look effects (bloom flashes) and their fading targets (original: zWorld VisualLookEffect::UpdateActive,
 * VisualLookEffectTarget::Update). */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x73dbe0, VisualLookEffectUpdateActive)

namespace
{
const int kStart = 4;
const int kDuration = 8;
const int kUseWorldTime = 0xc;
const int kStopBelow = 0xd;
const int kStopAfter = 0xe;
const int kTargetFrom = 4;
const int kTargetValue = 8;
const int kTargetGoal = 0xc;

const int kLayoutScale = 4;
const int kLayoutEndScale = 0x34;
const int kLayoutFadeTime = 0x44;
const int kLayoutIntensity = 0x48;
const int kLayoutThreshold = 0x4c;
const double kTicksToSeconds = 0.00025f;

template <typename T> T &At(void *base, int offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}

char *Layout(void *effect)
{
    return At<char *>(At<void *>(effect, 0), 8);
}
} // namespace

float __fastcall VisualLookEffectUpdateActive(char *effect, void *, float level)
{
    const int ticks = *reinterpret_cast<const int *>(effect[kUseWorldTime] ? 0x925ae8 : 0x925b14);
    const double elapsed = static_cast<double>(ticks) * kTicksToSeconds - At<float>(effect, kStart);
    if (effect[kStopBelow] && level < At<float>(Layout(effect), kLayoutThreshold))
        At<int>(effect, kStart) = 0;
    if (effect[kStopAfter] && !(elapsed < At<float>(effect, kDuration)))
        At<int>(effect, kStart) = 0;
    char *layout = Layout(effect);
    if (elapsed <= 0.0)
        return At<float>(layout, kLayoutIntensity) * At<float>(layout, kLayoutScale);
    if (!(elapsed < At<float>(effect, kDuration)))
        return At<float>(layout, kLayoutIntensity) * At<float>(layout, kLayoutEndScale);
    const float at = static_cast<float>(elapsed / At<float>(effect, kDuration));
    return GetValueFromSpline(at, reinterpret_cast<bMatrix4 *>(layout)) * At<float>(layout, kLayoutIntensity);
}

float VisualLookEffectTarget::Update()
{
    float &value = At<float>(this, kTargetValue);
    const float from = At<float>(this, kTargetFrom);
    if (from == 0.0f)
        return value;
    char *layout = Layout(this);
    float passed = *reinterpret_cast<const float *>(0x8f8308) - from;
    if (passed > At<float>(layout, kLayoutFadeTime))
    {
        value = At<float>(this, kTargetGoal);
        At<int>(this, kTargetFrom) = 0;
        return value;
    }
    if (passed < 0.0f)
        return value;
    passed = passed / At<float>(layout, kLayoutFadeTime);
    if (value > At<float>(this, kTargetGoal))
        passed = 1.0f - passed;
    value = GetValueFromSpline(passed, reinterpret_cast<bMatrix4 *>(layout));
    return value;
}
