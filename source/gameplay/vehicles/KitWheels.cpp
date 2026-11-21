/**
 * Wheel positions for body kits past the ecar's arrays (original: zWorld CarRenderInfo::UpdateWheelYRenderOffset
 * 0x74d780).
 *
 * The original moves the wheels out by ecar KitWheelOffsetFront/Rear[kit number], a track width scale from
 * TireSkidWidthKitScale[kit number]. Those arrays hold kits 0-5 and 0-6, and a car's added kits number on from its own
 * (tools/ug2merge.py), so a kit past them carries its wheel offsets in its BODY part's attributes (in millimetres), and
 * takes the stock width scale.
 */
#include "game/gen/api.h"
#include "legacy/link.h"

namespace
{
const int kCarTypeInfo = 0x88;
const int kRideInfo = 0x84;
const int kAttributes = 0x17B4;
const int kWheelYOffset = 0x110;
const int kWheelWidths = 0x120;
const int kWidthScales = 0x130;
const int kSkidWidths = 0xDC;
const int kWheels = 4;
const int kFrontWheels = 2;
const int kSlotBody = 23;
const int kWidthScaleKits = 7;
const unsigned int kKitNumber = 0x796C0CB0;
/** bStringHash of KITWHEELOFFSETFRONT and KITWHEELOFFSETREAR. */
const unsigned int kOffsetFront = 0x4D597C0C;
const unsigned int kOffsetRear = 0x95C3402D;
const float kMillimetres = 0.001f;
} // namespace

void CarRenderInfo::UpdateWheelYRenderOffset()
{
    NFS_ORIGINAL(0x74d780, void(__thiscall *)(CarRenderInfo *))(this);
    unsigned char *self = reinterpret_cast<unsigned char *>(this);
    RideInfo *ride = *reinterpret_cast<RideInfo **>(self + kRideInfo);
    if (!ride || !*reinterpret_cast<void **>(self + kCarTypeInfo))
        return;
    CarPart *body = ride->GetPart(kSlotBody);
    if (!body)
        return;
    const bool added = body->HasAppliedAttribute(kOffsetFront) != 0;
    const int kit = body->GetAppliedAttributeIParam(kKitNumber, 0);
    if (!added && kit < kWidthScaleKits)
        return;
    const float front = static_cast<float>(body->GetAppliedAttributeIParam(kOffsetFront, 0)) * kMillimetres;
    const float rear = static_cast<float>(body->GetAppliedAttributeIParam(kOffsetRear, 0)) * kMillimetres;
    float *offsets = reinterpret_cast<float *>(self + kWheelYOffset);
    float *scales = reinterpret_cast<float *>(self + kWidthScales);
    const float *modelWidths = reinterpret_cast<const float *>(self + kWheelWidths);
    const float *skids =
        reinterpret_cast<const float *>(*reinterpret_cast<const unsigned char *const *>(self + kAttributes) + kSkidWidths);
    for (int wheel = 0; wheel < kWheels; ++wheel)
    {
        const bool isFront = wheel < kFrontWheels;
        const float move = isFront ? front : rear;
        offsets[wheel] += offsets[wheel] <= 0.0f ? -move : move;
        const float width = modelWidths[isFront ? 0 : 1];
        if (width > 0.0f && skids[wheel] > 0.0f)
            scales[wheel] = skids[wheel] / width;
    }
}
