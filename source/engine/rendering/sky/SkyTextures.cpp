/** Sky layer textures, light colour blends and small platform state (original: zPlatform ReplaceSkyTextures,
 * sub_6C0DC0, sub_6C10E0, sub_6C2470). */
#include "game/gen/api.h"
#include "legacy/link.h"

NFS_REPLACE(0x6c0d50, ReplaceSkyLayerTextures)
NFS_REPLACE(0x6c0dc0, BlendLightColour)
NFS_REPLACE(0x6c10e0, ResetPlatformState)
NFS_REPLACE(0x6c2470, AdvanceClampedCounter)

namespace
{
struct Replacement
{
    unsigned int from;
    unsigned int to;
    int texture;
};

Replacement *const kSkyReplacements = reinterpret_cast<Replacement *>(0x901798);
const unsigned int *const kSkyFrom = reinterpret_cast<const unsigned int *>(0x93e864);
const unsigned int *const kLayerTextures = reinterpret_cast<const unsigned int *>(0x982878);
eModel *const kSkyModel = reinterpret_cast<eModel *>(0x986f50);

const float *const kBlendOverride = reinterpret_cast<const float *>(0x8fae40);
const float *const kBlendTable = reinterpret_cast<const float *>(0x8f9370);

int *const kCounterOn = reinterpret_cast<int *>(0x982c24);
int *const kCounter = reinterpret_cast<int *>(0x982c20);
const int *const kCounterMax = reinterpret_cast<const int *>(0x982c1c);
} // namespace

void __cdecl ReplaceSkyLayerTextures(int layer)
{
    kSkyReplacements[0].from = kSkyFrom[0];
    kSkyReplacements[1].from = kSkyFrom[1];
    kSkyReplacements[0].to = kLayerTextures[layer * 2];
    kSkyReplacements[1].to = kLayerTextures[layer * 2 + 1];
    kSkyReplacements[0].texture = -1;
    kSkyReplacements[1].texture = -1;
    kSkyModel->AttachReplacementTextureTable(reinterpret_cast<eReplacementTextureTable *>(kSkyReplacements), 2, 0);
}

void __cdecl BlendLightColour(const char *object, int index, float *r, float *g, float *b, float *a)
{
    float from = 0.0f;
    float to = 1.0f;
    if (const char *source = *reinterpret_cast<const char *const *>(object + 0x68))
    {
        from = *reinterpret_cast<const float *>(source + 0x290);
        to = 1.0f - from;
    }
    if (*kBlendOverride > 0.0f)
    {
        from = *kBlendOverride;
        to = 1.0f - from;
    }
    const float *t = kBlendTable + index * 8;
    *r = to * t[1] + from * t[0];
    *g = to * t[3] + from * t[2];
    *b = to * t[5] + from * t[4];
    *a = to * t[7] + from * t[6];
}

void *__fastcall ResetPlatformState(unsigned int *state)
{
    const unsigned int one = 0x3f800000;
    state[0] = state[1] = state[2] = 0;
    state[4] = 0x80808080;
    state[5] = state[6] = 0;
    state[7] = state[8] = one;
    state[12] = one;
    state[13] = state[14] = 0;
    state[16] = state[17] = 0;
    state[18] = one;
    return state;
}

void __cdecl AdvanceClampedCounter(int by)
{
    if (!*kCounterOn)
        return;
    int value = *kCounter + by;
    if (value >= *kCounterMax)
        value = *kCounterMax;
    *kCounter = value > 0 ? value : 0;
}
