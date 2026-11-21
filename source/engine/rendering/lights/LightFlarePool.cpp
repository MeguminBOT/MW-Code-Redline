/**
 * The light flare pool (engine/rendering/lights/flares.h; zEcstasy eRenderLightFlarePool 0x507770, eResestLightFlarePool 0x4fc800, and the
 * allocator sub_4fc780 the scenery culling calls).
 *
 * The original pool holds 50 flares a frame. Past that a lamp got no glow, and which lamps did depended on the order the
 * culling visited them in, so lamps flickered on and off as the player drove. The rebuild's pool holds kPoolSize; the
 * original one keeps what RenderSunAsFlare writes into it directly (the sun's flares), and both are drawn.
 *
 * A marker flare is drawn at the fade of the scenery it belongs to (engine/rendering/culling/culling.h), so a lamp fading into or out of
 * view does not pop its glow. The original drew the player view's pool flares at full intensity.
 */
#include "game/gen/api.h"
#include "game/platform/Platform.h"
#include "engine/rendering/lights/flares.h"
#include "engine/rendering/lights/lights.h"
#include "legacy/link.h"
#include "core/log.h"
#include "engine/rendering/vulkan/renderer.h"

extern "C" int blink[3];

namespace
{
const int kPoolSize = 1024;
/** Flare types (the marker's type + 14) with their own rules: blinking, blinking in three phases by position, the
 * second blink, and lights the player's car switches off (and tunnels hide). */
const unsigned char kBlinking = 0x12;
const unsigned char kBlinkingPhased = 0x11;
const unsigned char kBlinkingSlow = 0x13;
const unsigned char kCarDimmed = 0x15;
const int kMirrorView = 3;
const int kRoadReflectionView = 4;
const int kFirstEnvironmentView = 0x12;
const int kLastEnvironmentView = 0x17;
const int kPlayerView = 1;
const float kNoReflection = 999.0f;

struct PoolFlare
{
    unsigned char unknown00[0x10];
    float position[3];
    float reflectionHeight;
    float direction[3];
    unsigned char type;
    unsigned char flags;
    unsigned char unknown2e[2];
};
static_assert(sizeof(PoolFlare) == 0x30, "pool flare");

PoolFlare g_pool[kPoolSize];
unsigned int g_excluded[kPoolSize];
float g_intensity[kPoolSize];
int g_count;
bool g_initialised;
/** -popdebug: the most pool flares a frame and the flares that found no room, over the last report period. */
int g_peak;
int g_full;
DWORD g_reported;

void Initialise()
{
    g_initialised = true;
    for (PoolFlare &f : g_pool)
    {
        f.flags = 0;
        f.direction[0] = f.direction[1] = 0.0f;
        f.direction[2] = -1.0f;
        f.reflectionHeight = kNoReflection;
    }
}
} // namespace

namespace nfs::flares
{
eLightFlare *Allocate(unsigned int excludedViews, float intensity)
{
    if (!g_initialised)
        Initialise();
    if (g_count == kPoolSize)
    {
        ++g_full;
        return nullptr;
    }
    g_excluded[g_count] = excludedViews;
    g_intensity[g_count] = intensity;
    return reinterpret_cast<eLightFlare *>(&g_pool[g_count++]);
}
} // namespace nfs::flares

void eResestLightFlarePool()
{
    NFS_ORIGINAL(0x4fc800, void(__cdecl *)())();
    static const bool debug = nfs::vk::HasArgument("-popdebug");
    if (debug)
    {
        g_peak = g_count > g_peak ? g_count : g_peak;
        if (GetTickCount() - g_reported > 5000)
        {
            g_reported = GetTickCount();
            nfs::Log("flares: up to %d pool flares a frame (room for %d; the original had 50), %d found no room", g_peak,
                     kPoolSize, g_full);
            g_peak = g_full = 0;
        }
    }
    g_count = 0;
}

namespace
{
/** The original draws a flare of the kinds that do not blink (all but 0x11-0x14) in the player views only within 16
 * degrees of where the view's first camera mover looks (eRenderLightFlare 0x505380: the mover's anchor at +0x20 and
 * its direction at +0x50 against the cosine at 0x8f2924, 0.96), and then at the share of that angle in place of the
 * intensity it was given. The lamps of a tunnel glowed as they came to the middle of the screen and went dim or dark
 * beside it, and their fade was lost. While the rebuild's pool is drawn, the view's mover list (eView +0x44, a bTList)
 * reads as empty, which the original takes as no mover: every lamp glows at its own intensity wherever it is. */
class SteadyPoolFlares
{
  public:
    explicit SteadyPoolFlares(eView *view)
        : head_(reinterpret_cast<void **>(reinterpret_cast<char *>(view) + 0x44)), saved_(*head_)
    {
        *head_ = head_;
    }
    ~SteadyPoolFlares()
    {
        *head_ = saved_;
    }

  private:
    void **head_;
    void *saved_;
};
} // namespace

/** Draws the frame's pool flares in the view: the original pool's (the sun's), then the rebuild's (sub_507770's rules,
 * without the angle to the view's mover: SteadyPoolFlares). */
void eRenderLightFlarePool(eView *view)
{
    NFS_ORIGINAL(0x507770, void(__cdecl *)(eView *))(view);
    const SteadyPoolFlares anyAngle(view);
    const int id = view->ID;
    const unsigned int mask = 1u << id;
    // -lightdebug: the player view's pool flares by kind, every 4 s.
    static const bool debug = nfs::vk::HasArgument("-lightdebug");
    static DWORD logged;
    int kinds[256] = {};
    for (int i = 0; i < g_count; ++i)
    {
        PoolFlare &f = g_pool[i];
        const unsigned char type = f.type;
        if (debug && id == kPlayerView && !(g_excluded[i] & mask))
            ++kinds[type];
        // The lamps' glows (engine/rendering/lights/lamps.h) take the blinking types' amber but burn steadily.
        const bool steady = (f.flags & nfs::lights::kFlareBaked) != 0;
        if (type == kBlinking && !blink[0] && !steady)
            continue;
        int phase = static_cast<int>(f.position[0]) % 3;
        phase = phase < 0 ? -phase : phase;
        if (type == kBlinkingPhased && !blink[phase] && !steady)
            continue;
        if (type == kBlinkingSlow && !blink[2] && !steady)
            continue;
        float intensity = g_intensity[i];
        if (type == kCarDimmed)
        {
            const char *car = *reinterpret_cast<char *const *>(reinterpret_cast<const char *>(&eViews[kPlayerView]) + 0x68);
            if (car)
                intensity *= 1.0f - *reinterpret_cast<const float *>(car + 0x290);
            if (AmIinATunnel(&eViews[kPlayerView], 1))
                intensity = 0.0f;
        }
        if (g_excluded[i] & mask)
            continue;
        eLightFlare *flare = reinterpret_cast<eLightFlare *>(&f);
        if (id == kMirrorView || (id >= kFirstEnvironmentView && id <= kLastEnvironmentView))
            eRenderLightFlare(view, flare, &eMathIdentityMatrix, intensity, static_cast<eLightReflexionType>(0),
                              static_cast<flareType>(1), 0.0f, 0, 1.0f);
        else if (id == kRoadReflectionView)
            eRenderLightFlare(view, flare, &eMathIdentityMatrix, intensity, static_cast<eLightReflexionType>(2),
                              static_cast<flareType>(2), 0.0f, 0, 1.0f);
        else
            eRenderLightFlare(view, flare, &eMathIdentityMatrix, g_intensity[i], static_cast<eLightReflexionType>(0),
                              static_cast<flareType>(id != kPlayerView ? 4 : 3), 0.0f, 0, 1.0f);
    }
    if (debug && id == kPlayerView && GetTickCount() - logged > 4000)
    {
        logged = GetTickCount();
        char line[512];
        int n = wsprintfA(line, "flares: player view, %d in the pool; by kind:", g_count);
        for (int k = 0; k < 256 && n < 480; ++k)
            if (kinds[k])
                n += wsprintfA(line + n, " %02x:%d", k, kinds[k]);
        nfs::Log("%s", line);
    }
}
