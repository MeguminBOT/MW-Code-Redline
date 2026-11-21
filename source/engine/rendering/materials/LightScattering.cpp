/**
 * The Xbox 360 light scattering (fog) constants (X360 Stuff's eLightScattering::Update 0x1003bd30, which takes the
 * place of the original fog update sub_6D6CF0): Rayleigh and Mie scattering from the time of day lighting, the
 * Henyey-Greenstein phase terms, and whether the track's light scattering attribute turns them on.
 */
#include <math.h>
#include <string.h>

#include "game/gen/api.h"
#include "game/platform/Platform.h"
#include "legacy/link.h"
#include "gameplay/world/timeofday.h"
#include "engine/rendering/x360/render.h"
#include "engine/rendering/x360/resources.h"

extern "C" char *TimeOfDayPointer;

namespace nfs::x360
{
namespace
{
const unsigned int kScatteringClass = 0xebcee74c;
const unsigned int kScatteringCollection = 0xce3b435b;
const unsigned int kAttribEnabled = 0x110882d5;
/** 3 / (16 pi) and 1 / (4 pi): the Rayleigh and Mie phase function constants. */
const float kRayleighPhase = 0.0596831031f;
const float kMiePhase = 0.0795774683f;

/** The distance the sky's own haze is worked out for at its horizon (the sky's vertex shader). */
const float kSkyHorizonDistance = 20000.0f;
/** Carbon's night dome at its horizon: its brightest band, as the road reflection's target shows it. */
const float kDomeHorizon[3] = {0.11f, 0.14f, 0.2f};

/** FOG_BR_PLUS_BM, FOG_ONE_OVER_BRBM, FOG_CONST_1-3 (a vector each) and the distance scale; the world's and the sky's. */
alignas(16) float s_fog[24];
alignas(16) float s_skyFog[24];

float Tod(int offset)
{
    return *reinterpret_cast<const float *>(TimeOfDayPointer + offset);
}

/**
 * A far mountain lost in the haze should be the colour of the sky behind it. The sky is this same haze at its horizon
 * times the hour's sky brightness (0.5 x STANDARD_BRIGHTNESS), and at night Carbon's dome covers it, but the haze the
 * world adds took neither: at dusk and dawn the mountains stood out white against a sky half as bright. The world's
 * in-scattering is scaled to reach what the sky has at its horizon; by day the factor is 1, and it is never raised.
 */
void MatchWorldHazeToSky(float *f)
{
    if (!nfs::timeofday::Lit() || f[0] <= 0.0f || f[1] <= 0.0f || f[2] <= 0.0f)
        return;
    const float brightness = 0.5f * nfs::timeofday::SkyBrightness(2.0f);
    const float dome = nfs::timeofday::NightSky();
    const float spread = f[17];
    const float mie = f[16] / (spread * sqrtf(spread));
    for (int c = 0; c < 3; ++c)
    {
        // exp(x) as (1 + x / 16) ^ 16: this file builds without the math library's exp.
        float grown = 1.0f + f[c] * kSkyHorizonDistance / 16.0f;
        grown *= grown;
        grown *= grown;
        grown *= grown;
        grown *= grown;
        const float haze = (1.0f - 1.0f / grown) * f[4 + c] * (f[8 + c] + f[12 + c] * mie) * f[19];
        float scale = (1.0f - dome) * brightness;
        if (haze > 1e-5f)
            scale += dome * kDomeHorizon[c] / haze;
        if (scale < 1.0f)
        {
            f[8 + c] *= scale;
            f[12 + c] *= scale;
        }
    }
}
} // namespace

void UpdateLightScattering()
{
    g_fog = s_fog;
    float *f = s_fog;
    char instance[0x14];
    const void *collection = NFS_ORIGINAL(0x455fd0, const void *(__cdecl *)(unsigned int, unsigned int))(
        kScatteringClass, kScatteringCollection);
    reinterpret_cast<int *>(instance)[1] = 0;
    NFS_ORIGINAL(0x452380, void(__thiscall *)(void *, const void *, unsigned int, void *))(instance, collection, 0,
                                                                                          nullptr);
    float rayleigh[4] = {}, mie[4] = {}, g = 0.0f, extra = 0.0f;
    if (TimeOfDayPointer)
    {
        const float r = (Tod(0x90) + 1.0f) * 0.0001f;
        float m = (Tod(0x94) + 1.0f) * 0.0001f;
        m = m > 0.0f ? m : 0.0f;
        for (int i = 0; i < 4; ++i)
        {
            rayleigh[i] = r * Tod(0x70 + 4 * i);
            mie[i] = m * Tod(0x80 + 4 * i);
        }
        g = Tod(0x9c);
        extra = Tod(0x98);
        f[20] = Tod(0xa0);
    }
    else
    {
        f[0] = f[1] = f[2] = 0.0f;
    }
    const bool enabled = *reinterpret_cast<const unsigned char *>(AttributeFloat(instance, kAttribEnabled)) != 0;
    if (enabled)
        for (int i = 0; i < 4; ++i)
            f[i] = rayleigh[i] + mie[i];
    else
        f[0] = f[1] = f[2] = f[3] = 0.0f;
    f[4] = 1.0f / f[0];
    f[5] = 1.0f / f[1];
    f[6] = 1.0f / f[2];
    f[7] = 0.0f;
    // Fog and rain haze take the colour of the light they scatter (nfs::timeofday::HazeTint).
    float tint[3];
    nfs::timeofday::HazeTint(tint);
    for (int i = 0; i < 3; ++i)
    {
        f[8 + i] = rayleigh[i] * kRayleighPhase;
        f[12 + i] = mie[i] * kMiePhase * tint[i];
    }
    f[11] = f[15] = 0.0f;
    f[16] = (1.0f - g) * (1.0f - g);
    f[17] = g * g + 1.0f;
    f[18] = g * -2.0f;
    f[19] = enabled ? extra : 0.0f;
    memcpy(s_skyFog, s_fog, sizeof(s_fog));
    g_skyFog = s_skyFog;
    if (enabled)
        MatchWorldHazeToSky(f);
    NFS_ORIGINAL(0x45a430, void(__thiscall *)(void *))(instance);
}
} // namespace nfs::x360
