/**
 * The Xbox 360 changes to the dynamic shadow pass (sub_6e44c0, run by the receiver pass 0x6e54e0): X360 Stuff's
 * Rendering::ShadowDirectionFix (the light comes from no lower than a minimum height, so shadows do not stretch across
 * the road at a low sun) and Rendering::GrandSceneryCullInfo_StuffScenery_ShadowHook (overpasses, bridges and the
 * like cast no shadow; in the front end the garage casts one).
 *
 * The shadow pass reads the light direction once, so it gets the corrected one for the length of the pass and the
 * time of day keeps its own. The scenery hook applies only while the pass runs; StuffScenery's other callers are
 * unchanged.
 */
#include <math.h>
#include <string.h>

#include "game/gen/api.h"
#include "core/config.h"
#include "legacy/link.h"
#include "engine/rendering/x360/render.h"

extern "C" char *TimeOfDayPointer;
extern "C" int TheGameFlowManager_CurrentGameFlowState;
extern "C" void __cdecl sub_6E44C0(eView *view, void *cull, int, int);

namespace
{
const int kGameFlowFrontEnd = 3;
/** Garage geometry drawn as a shadow caster. */
const int kGarageShadowCasters = 4;
/** A scenery instance that casts no shadow, and one this pass has looked at. */
const unsigned int kNoShadow = 0x100000;
const unsigned int kSeen = 0x80000000;
/** The models that cast no shadow (name hashes), and a solid named alike. */
const unsigned int kNoShadowModels[35] = {
    0x6ce646b6, 0x6ce6d317, 0x22548da4, 0x225491e5, 0x639117d1, 0x639117d2, 0x639117d3, 0x639117d4, 0x639117d5,
    0x23db74e8, 0x23ce4bd0, 0x8287a146, 0xe4ed3c4c, 0x707e6a31, 0x2152dd60, 0x8294ca5f, 0x8294ca60, 0x804bb4e3,
    0xcfaabe64, 0x8287a147, 0x8287a148, 0x8287a149, 0x8287a14a, 0x3e518547, 0x4092f045, 0x4092f046, 0x4092f047,
    0x4092f049, 0x4092f044, 0x119109a1, 0x0cfa7606, 0x374f37b1, 0x84be19c4, 0xfe73d6cf, 0xa0cc74ae,
};
const char kNoShadowSolid[] = "TRN_TC_INDUSTRIALHIGHWAY_OVERPASS";

bool s_shadowPass;

bool CastsNoShadow(const char *model)
{
    const unsigned int hash = *reinterpret_cast<const unsigned int *>(model + 8);
    for (unsigned int h : kNoShadowModels)
        if (h == hash)
            return true;
    const char *solid = *reinterpret_cast<const char *const *>(model + 0xc);
    return solid && strncmp(solid + 0xa0, kNoShadowSolid, sizeof(kNoShadowSolid) - 1) == 0;
}

/** Marks the cull info's scenery that casts no shadow, once: the view's entry (or, as the plugin, the last one) and
 * each instance remember they were seen. */
void MarkShadowlessScenery(GrandSceneryCullInfo *cull, eView *view)
{
    char *info = reinterpret_cast<char *>(cull);
    const int views = *reinterpret_cast<int *>(info + 0x9c0);
    if (views <= 0)
        return;
    char *entry = info;
    for (int i = 0; i < views; ++i, entry += 0xd0)
        if (*reinterpret_cast<eView **>(entry + 0x80) == view)
            break;
    if (*reinterpret_cast<eView **>(entry + 0x80) != view)
        entry -= 0xd0;
    unsigned int &entryFlags = *reinterpret_cast<unsigned int *>(entry + 0x84);
    if (entryFlags & kSeen)
        return;
    char *begin = *reinterpret_cast<char **>(info + 0x9c4), *end = *reinterpret_cast<char **>(info + 0x9c8);
    const unsigned int count = end ? static_cast<unsigned int>(end - begin) / 0xc : 1;
    for (unsigned int i = 0; i < count; ++i)
    {
        char *item = begin + i * 0xc;
        const char *model = reinterpret_cast<const char *>(*reinterpret_cast<unsigned int *>(item) & ~3u);
        unsigned int &flags = *reinterpret_cast<unsigned int *>(*reinterpret_cast<char **>(item + 8) + 0x18);
        static const bool allCast = nfs::config::Int("Debug", "AllCasters", 0) != 0;
        if (!(flags & kSeen) && model && !allCast && CastsNoShadow(model))
            flags |= kNoShadow;
        flags |= kSeen;
    }
    entryFlags |= kSeen;
}
} // namespace

namespace nfs::x360
{
void PrepareShadowScenery(GrandSceneryCullInfo *cull, eView *view)
{
    if (!s_shadowPass)
        return;
    if (TheGameFlowManager_CurrentGameFlowState == kGameFlowFrontEnd)
        if (GarageMainScreen *garage = GarageMainScreen::GetInstance())
            NFS_ORIGINAL(0x7a2200, void(__thiscall *)(GarageMainScreen *, int))(garage, kGarageShadowCasters);
    MarkShadowlessScenery(cull, view);
}

void RenderShadowPass(eView *view, void *cull)
{
    float *light = reinterpret_cast<float *>(TimeOfDayPointer + 0x180);
    float saved[4];
    for (int i = 0; i < 4; ++i)
        saved[i] = light[i];
    if (options::kXbox360ShadowAngle)
    {
        const float length = sqrtf(light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
        if (length == 0.0f)
        {
            light[0] = 1.0f;
            light[1] = 0.0f;
            light[2] = options::kMinimumShadowAngleHeight;
        }
        else
        {
            const float inverse = 1.0f / length;
            for (int i = 0; i < 4; ++i)
                light[i] *= inverse;
            if (light[2] < options::kMinimumShadowAngleHeight)
                light[2] = options::kMinimumShadowAngleHeight;
        }
    }
    s_shadowPass = true;
    sub_6E44C0(view, cull, 0, 1);
    s_shadowPass = false;
    for (int i = 0; i < 4; ++i)
        light[i] = saved[i];
}
} // namespace nfs::x360
