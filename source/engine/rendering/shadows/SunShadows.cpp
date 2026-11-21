/**
 * The rebuild's sun shadows (engine/rendering/shadows/shadow.h, docs/SHADOWS.md), in place of the original perspective shadow map (0x6e44c0)
 * in Vulkan mode.
 *
 * Four cascades around the player camera, each an orthographic map from the sun in one quarter of a 4096 x 4096 depth
 * atlas. A cascade covers a slice of the camera's view (to 12, 36, 110 and 330 m) inside a sphere whose size only
 * changes in whole metres, and its light-space origin snaps to whole texels, so shadow edges hold still as the camera
 * moves. Each cascade culls the scenery itself (the culling's draw table is shared, so this runs before the views'
 * culling) and draws what the original shadow pass drew: world props (RenderWorldModels), the scenery, and the cars -
 * but every scenery model casts, overpasses and roofs included.
 */
#include <math.h>
#include <string.h>

#include "game/gen/api.h"
#include "game/platform/Device.h"
#include "game/platform/Platform.h"
#include "core/config.h"
#include "engine/rendering/culling/culling.h"
#include "engine/rendering/display/display.h"
#include "engine/rendering/lights/lamps.h"
#include "legacy/link.h"
#include "core/log.h"
#include "engine/rendering/shadows/shadow.h"
#include "engine/rendering/shadows/shadowdebug.h"
#include "engine/rendering/surface.h"
#include "gameplay/world/timeofday.h"
#include "engine/rendering/view.h"
#include "gameplay/world/weather.h"
#include "engine/rendering/vulkan/renderer.h"
#include "engine/rendering/x360/frame.h"
#include "engine/rendering/x360/render.h"

/** The cull flags a view culls with (source/engine/resources/EcstasyMisc.cpp). */
void __cdecl SetViewCullFlags(eView *view, char *cull, unsigned int flags);


extern "C" char *TimeOfDayPointer;
extern "C" int TheGameFlowManager_CurrentGameFlowState;
extern "C" int lpData_00901830;
extern "C" int dword_8FAE6C;
extern "C" int dword_982B38;
/** The original shadow pass's caster test (sub_6D7A40, from eViewPlatInterface::Render while dword_982B38 is set): a
 * model or solid is drawn only when its box, or its shadow along the original's light direction, meets the player
 * view's frustum. */
extern "C" int dword_8FAF14;
extern "C" int DrawCarShadow;
/** Strips drawn so far (the render list's statistics). */
extern "C" int dword_987B00;
extern "C" void __cdecl sub_6E2F50();
extern "C" void __cdecl sub_750B10(eView *view, int flags);

static_assert(sizeof(nfs::shadow::SceneConstants) <= nfs::vk::kSceneConstantsBytes, "scene set constants");

namespace nfs::shadow
{
namespace
{
/** The rear view mirror looks behind the camera: the lights there show in it. */
bool MirrorOn()
{
    const int kMirrorView = 3;
    return eViews[kMirrorView].Active != 0;
}
} // namespace

namespace
{
/** The world's time the rain's animation takes, wrapped (float precision). */
const float kWeatherTimeWrap = 1024.0f;

/** The farthest distance the light scattering takes (the Xbox 360 effects' kMaxDist, in scaled metres). */
const float kFogMaxDistance = 7000.0f;

/** The weather's terms for the rebuild's shaders (lighting.hlsli weather, hazeLight) and the light scattering's
 * (fog, for HazeAt). */
void WeatherConstants(SceneConstants &scene)
{
    scene.weather[0] = nfs::weather::RainShown() ? nfs::weather::Rain() : 0.0f;
    scene.weather[1] = nfs::weather::Wetness();
    scene.weather[2] = nfs::weather::Puddles();
    scene.weather[3] = WorldTimeSeconds - floorf(WorldTimeSeconds / kWeatherTimeWrap) * kWeatherTimeWrap;
    nfs::surface::SceneConstants(scene.surface);
    nfs::timeofday::HazeLight(scene.hazeLight);
    memset(scene.fog, 0, sizeof(scene.fog));
    const float *fog = nfs::x360::g_fog;
    if (!fog || !TimeOfDayPointer || fog[1] <= 0.0f)
        return;
    memcpy(scene.fog, fog, sizeof(float) * 20);
    scene.fog[5][0] = fog[20];
    scene.fog[5][1] = kFogMaxDistance;
    const float *light = reinterpret_cast<const float *>(TimeOfDayPointer + 0x180);
    const float length = sqrtf(light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
    for (int c = 0; c < 3 && length > 0.0f; ++c)
        scene.fog[6][c] = light[c] / length;
}

const int kShadowView = 15;
const int kPlayerView = 1;
const int kGameFlowRacing = 6;
/** Each cascade's radius around the player camera, in metres (receivers pick their cascade by distance). */
const float kSplits[kCascades] = {12.0f, 40.0f, 140.0f, 500.0f};
/** The cascades from this one on keep a static layer (State::staticDepth): the scenery shown in full is drawn into it
 * only when the cascade moves or that scenery changes, and each frame the layer is copied into the atlas and only the
 * rest is drawn over it - cars, world props, scenery fading in or out or loaded since. The two far cascades hold most
 * of the casters (about 300 and 1000 strips a drawing), so most frames draw over a thousand strips less, and their
 * cars and fades now move every frame. */
const int kFirstLayeredCascade = 1;
const int kLayeredCascades = kCascades - kFirstLayeredCascade;
/** A layered cascade stays put while the camera moves; it covers this much more (metres, and a fraction of its radius)
 * so that the view does not run off its edge before it moves on. The second cascade's texel stays under what the
 * shadow detail needs at its inner edge (ShadowDetail: distance x lightInfo.w), so its shadows look the same. */
const float kLayerMargin[kCascades] = {0.0f, 6.0f, 10.0f, 10.0f};
const float kLayerMarginFraction[kCascades] = {0.0f, 0.0f, 0.05f, 0.05f};
/** Sections shown in full that a static layer does not hold are drawn each frame; after this many frames the layer is
 * redrawn to take them in. */
const unsigned int kUnbakedFrames = 30;
/** A static layer that only has to catch up (scenery changed, the sun moved, sections it lacks) is drawn again no
 * sooner than this after its last drawing (seconds), and only one such layer a frame, none in a frame that has to draw
 * one anyway (the cascade moved or the camera jumped): three far layers drawn in one frame, a thousand strips and
 * more, made the frame times jump while driving through streaming sections. The scenery meanwhile stays as the layer
 * holds it, at 40 m and more from the camera: a fading section's full shadow a little longer. */
const double kCatchUpSeconds[kCascades] = {0.0, 0.05, 0.15, 0.3};
/** The far layers catch up over several frames: drawn into a back layer this many frames a slice of the sections (by
 * section number), one cascade's slice a frame, then copied over the layer in use, which meanwhile stays as it was.
 * A layer is started early, when the camera has used half of the cascade's margin, so that it is ready before the view
 * would run off the cascade's edge (which still draws a layer at once). */
const int kLayerSlices[kCascades] = {1, 2, 3, 6};
/** The cascades from this one on are drawn into the atlas less often than every frame at high frame rates: their tile
 * (static layer and what is drawn over it) stays from the last time for up to kFarRefreshFrames frames, as long as that
 * was under kFarRefreshSeconds ago, and is drawn again whenever its static layer is. They cover 140 and 500 metres; a
 * fading section's shadows there step a little less often, and below about 80 frames a second they are drawn every frame
 * as before. Most of a frame's shadow drawing is the far cascade's fading sections. */
const int kFirstThrottledCascade = 2;
const unsigned int kFarRefreshFrames = 3;
const double kFarRefreshSeconds = 0.012;
/** Where a cascade starts blending into the next (fraction of its radius; lighting.hlsli kCascadeBlendStart). */
const float kCascadeBlendStart = 0.65f;
/** The vehicles' render connections (sub_750B10's list of VehicleRenderConn pointers, and its length). */
const unsigned int kVehicleRenderConns = 0x9b37cc;
const unsigned int kVehicleRenderConnCount = 0x9b37d4;
/** CarRenderConn's VehicleRenderConn vtable: a car, with its render matrix's position at +0x360. */
const unsigned int kCarRenderConnVtable = 0x8b5254;
const int kCarPosition = 0x360;
/** For how far a car's shadow can reach from its position: half the length of the longest (a bus) and the height of
 * the tallest, in metres. */
const float kCarRadius = 10.0f;
const float kCarHeight = 4.0f;
/** The headlight shadow map: a perspective view from between the player's headlights, along their beam, as far as they
 * reach. It takes the casters the original's shadow pass did (StuffScenery's flag 0x100: scenery marked to cast, without
 * the road and terrain, which at a headlight's grazing angle would only shadow themselves), props and the other cars. The
 * player's car is left out: nothing of it stands in the beam. */
const int kBeamMapSize = 2048;
const float kBeamNear = 0.8f;
/** The view's extent, as slopes from the beam's axis: its cone to the sides, and above and below as far as the beam's shape
 * lets any light through to speak of (lighting.hlsli MovingLights), so the map's height goes where the light is. */
const float kBeamSide = 0.9f;
const float kBeamBelow = 0.5f;
const float kBeamAbove = 0.4f;
const unsigned int kBeamStuffFlags = 0x100;
const float kBeamConstantBias = 0.00005f;
/** A headlight's lens, across (metres): how much it blurs the edges of its shadow (lighting.hlsli HeadlightVisibility). */
const float kHeadlightLens = 0.18f;
const float kBeamSlopeBias = 1.5f;
/** All cascades are drawn again when the camera jumps more than this (metres) or the sun turns. */
const float kJumpDistance = 30.0f;
const float kSunTurnCosine = 0.9999f;
/** A layered cascade's static layer is drawn again once the sun has moved this far from where it was drawn for (0.2
 * degrees: the time of day's clock moves it all the time; a few centimetres at the end of a long shadow), one layer a
 * frame. */
const float kSunStepCosine = 0.999994f;
/** At the highest shadow detail the static layers follow the sun every 0.03 degrees instead (a step every half a second
 * at the clock's 5 minutes an hour, a few millimetres at the end of a long shadow), so shadows move smoothly with the
 * time of day: the far layers redraw a slice a frame, a few times a second. */
const float kSmoothSunStepCosine = 0.99999986f;

float SunStepCosine()
{
    return g_VideoOptions.ShadowDetail >= g_VideoOptionLimits.ShadowDetail ? kSmoothSunStepCosine : kSunStepCosine;
}
/** Casters as far as this towards the sun from a cascade's sphere still shadow it. */
const float kCasterReach = 1500.0f;
/** The sun's angular radius (0.27 degrees) as a tangent: penumbrae widen by this much per metre to the caster. */
const float kSunTangent = 0.0047f;
/** The light stays at least this high: lower suns give shadows hundreds of metres long and a few texels thin. */
const float kMinimumSunHeight = 0.2f;
const float kLastFadeStart = 0.85f;
/** Shadow detail a little coarser than the coarsest cascade needs, so blended edges match. */
const float kDetailMargin = 1.1f;
/** Depth bias while drawing casters (Direct3D 9 units: depth, and per unit of depth slope). */
const float kConstantBias = 0.0001f;
const float kSlopeBias = 2.0f;
/** The culling setup of a shadow view in the original (sub_6BFDD0's flags for view 15). */
const unsigned int kShadowCullFlags = 0x4100;

/** World props and cars measure their size in pixels from the shadow view's camera (the player's) with its focal
 * length, and are left out under a few pixels: as if seen this many times closer, they cast within the cascades' range
 * however far from the player they are (the scenery culling has its own shadow caster mode, engine/rendering/culling/culling.h). */
const float kCasterDetailScale = 16.0f;
/** ScenerySectionHeaderList (a bTList of the loaded sections' headers, each with its section number at 0xc). */
const unsigned int kLoadedSections = 0x9aa478;

struct SectionNode
{
    SectionNode *next;
    SectionNode *prev;
    int unknown8;
    int number;
};

/** GrandSceneryCullInfo: the views' entries, their count, and the draw table range DoCulling fills. */
struct alignas(16) CullInfo
{
    char views[12][0xd0];
    int count;
    void *drawBegin;
    void *drawEnd;
    void *drawLimit;
};

struct Cascade
{
    bMatrix4 view;
    bMatrix4 projection;
    bMatrix4 viewProjection;
    bVector3 eye;
    bVector3 center;
    float texel;
    float radius;
    float depthRange;
};

struct State
{
    /** The atlas, and the layered cascades' static layers side by side from kFirstLayeredCascade on: depth only (the
     * casters' colour was never read). */
    nfs::vk::Texture *depth;
    nfs::vk::Texture *staticDepth;
    /** The player's headlights' shadow map, while Dynamic Shadows is on. */
    nfs::vk::Texture *headlightDepth;
    eRenderTarget headlightTarget;
    /** Per layered cascade: its static layer was drawn for the cascade as it is. */
    bool staticValid[kCascades];
    /** The frame since which a layered cascade has had sections in full that its static layer lacks (0: none). */
    unsigned int unbakedSince[kCascades];
    /** When each static layer was last drawn (Ticks). */
    LONGLONG layerTicks[kCascades];
    /** The back layers (kLayerSlices), the cascade and sun each is drawn for, and its next slice; building while true. */
    nfs::vk::Texture *backDepth;
    Cascade buildCascade[kCascades];
    bVector3 buildSun[kCascades];
    int buildSlice[kCascades];
    bool building[kCascades];
    eRenderTarget target;
    SceneConstants scene;
    bool active;
    /** The cascades as last drawn, with the camera position and sun they were drawn for. */
    Cascade cascades[kCascades];
    bool drawn;
    bVector3 drawnFrom;
    bVector3 drawnSun;
    /** The camera's velocity (metres a second, smoothed) and where and when it was last seen: a layered cascade is
     * placed ahead of a moving camera and its next layer started in time (kLeadSeconds, StartsInTime). */
    bVector3 cameraVelocity;
    bVector3 lastCamera;
    LONGLONG lastCameraTicks;
    float frameSeconds;
    /** The sun each cascade was drawn for. */
    bVector3 sunAt[kCascades];
    unsigned int frame;
    /** The largest sphere each cascade has needed. */
    float radius[kCascades];
    /** When each cascade's tile was last drawn into the atlas (frame, and Ticks); valid while tileValid. */
    unsigned int tileFrame[kCascades];
    LONGLONG tileTicks[kCascades];
    bool tileValid[kCascades];
};

State g_state;

float Dot(const bVector3 &a, const bVector3 &b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

bVector3 Add(const bVector3 &a, const bVector3 &b, float scale)
{
    return {a.x + b.x * scale, a.y + b.y * scale, a.z + b.z * scale};
}

bVector3 Normalized(const bVector3 &v)
{
    const float length = sqrtf(Dot(v, v));
    const float inverse = length > 0.0f ? 1.0f / length : 0.0f;
    return {v.x * inverse, v.y * inverse, v.z * inverse};
}

bVector3 Column(const bMatrix4 &m, int c)
{
    const float *f = reinterpret_cast<const float *>(&m);
    return {f[c], f[4 + c], f[8 + c]};
}

void Identity(bMatrix4 &m)
{
    memset(&m, 0, sizeof(m));
    m.v0.x = m.v1.y = m.v2.z = m.v3.w = 1.0f;
}

bool EnsureTargets(nfs::vk::Renderer *r)
{
    if (g_state.depth)
        return true;
    g_state.depth = r->CreateTexture(kAtlasSize, kAtlasSize, 1, D3DFMT_D24S8, true);
    g_state.staticDepth = r->CreateTexture(kLayeredCascades * kTileSize, kTileSize, 1, D3DFMT_D24S8, true);
    g_state.backDepth = r->CreateTexture(kLayeredCascades * kTileSize, kTileSize, 1, D3DFMT_D24S8, true);
    if (!g_state.depth || !g_state.staticDepth || !g_state.backDepth)
    {
        ReleaseTargets();
        return false;
    }
    eRenderTarget &t = g_state.target;
    memset(&t, 0, sizeof(t));
    t.ViewID = kShadowView;
    t.Surface = nullptr;
    t.DepthSurface = reinterpret_cast<IDirect3DSurface9 *>(g_state.depth);
    t.unknown_10 = 1;
    t.Width = t.Height = kAtlasSize;
    r->SetSunShadowAtlas(g_state.depth);
    if (nfs::vk::HasArgument("-shadowdebug"))
        r->SetDebugTexture(g_state.depth);
    return true;
}

/** The sun's direction (towards it), kept at least kMinimumSunHeight high. */
bVector3 SunDirection()
{
    const float *light = reinterpret_cast<const float *>(TimeOfDayPointer + 0x180);
    bVector3 sun = Normalized({light[0], light[1], light[2]});
    if (sun.z < kMinimumSunHeight)
    {
        const float flat = sqrtf(sun.x * sun.x + sun.y * sun.y);
        const float scale = flat > 0.0f ? sqrtf(1.0f - kMinimumSunHeight * kMinimumSunHeight) / flat : 0.0f;
        sun = {sun.x * scale, sun.y * scale, kMinimumSunHeight};
    }
    return sun;
}

/** The sphere cascade `index` must cover: everything within its distance of the player camera. The cascades are
 * centred on the camera and receivers pick theirs by distance, not by depth along the view: turning the camera then
 * changes nothing about which cascade shadows a spot. Picked by view depth, the boundaries swept across the ground as
 * the camera turned, and shadows of poles, fences and signs faded in and out with the resolution of the cascade they
 * landed in (most at the sides of the view, where view depth is shortest). */
void CascadeSphere(int index, const eView &player, bVector3 &center, float &radius)
{
    center = player.pCamera->CurrentKey.Position;
    radius = kSplits[index];
}

/** A layered cascade is placed this far ahead of a moving camera (seconds of its travel), at most its margin: the
 * camera then crosses the whole margin before the cascade has to move, twice as far as from the centre, so at
 * motorway speeds the layers are drawn half as often. */
const float kLeadSeconds = 0.6f;

/** Cascade `index`: its sphere around the player camera (ahead of it while it moves, for a layered one), and the sun's
 * orthographic view of it. */
void MakeCascade(int index, const eView &player, const bVector3 &sun, Cascade &c)
{
    bVector3 center;
    float radius;
    CascadeSphere(index, player, center, radius);
    if (index >= kFirstLayeredCascade)
    {
        const bVector3 &v = g_state.cameraVelocity;
        const float speed = sqrtf(Dot(v, v));
        const float margin = kLayerMargin[index] + radius * kLayerMarginFraction[index];
        if (speed > 1.0f && margin > 0.0f)
        {
            const float lead = speed * kLeadSeconds < margin ? speed * kLeadSeconds : margin;
            center = Add(center, v, lead / speed);
        }
    }
    radius += kLayerMargin[index] + radius * kLayerMarginFraction[index];
    // The sphere only grows (the field of view widens with speed): a new size moves every texel, so it changes
    // rarely, in whole metres, and stays at the widest view's.
    radius = ceilf(radius);
    if (radius < g_state.radius[index])
        radius = g_state.radius[index];
    g_state.radius[index] = radius;
    c.radius = radius;
    c.texel = 2.0f * radius / static_cast<float>(kTileSize);
    c.center = center;
    c.eye = Add(center, sun, radius + kCasterReach);
    const bVector3 worldUp = fabsf(sun.z) > 0.99f ? bVector3{1.0f, 0.0f, 0.0f} : bVector3{0.0f, 0.0f, 1.0f};
    NFS_ORIGINAL(0x6cf0a0, void(__cdecl *)(bMatrix4 *, const bVector3 *, const bVector3 *, const bVector3 *))(
        &c.view, &c.eye, &center, &worldUp);
    // Whole texels in light space: the grid stays fixed in the world while the sphere moves.
    c.view.v3.x = floorf(c.view.v3.x / c.texel + 0.5f) * c.texel;
    c.view.v3.y = floorf(c.view.v3.y / c.texel + 0.5f) * c.texel;
    const float zNear = 1.0f, zFar = 2.0f * radius + kCasterReach;
    c.depthRange = zFar - zNear;
    Identity(c.projection);
    c.projection.v0.x = 1.0f / radius;
    c.projection.v1.y = 1.0f / radius;
    c.projection.v2.z = 1.0f / c.depthRange;
    c.projection.v3.z = -zNear / c.depthRange;
    D3DXMatrixMultiply(&c.viewProjection, &c.view, &c.projection);
}

/** Makes the shadow view (15) see the cascade: its matrices and clip planes, as the culling and eSetTransform read
 * them. Its camera stays where the player's is: the culling picks the track sections, the detail and the cars to draw
 * by the camera's position, and from the light camera, far up towards the sun, it found those around the player out of
 * range. The camera has no direction: eView::GetPixelSize leaves out what lies behind it by its direction, which for
 * a shadow view dropped the props and cars towards the sun, whose shadows reach the player. */
void AimShadowView(const Cascade &c, const bVector3 &sun, const eView &player)
{
    eView &view = eViews[kShadowView];
    eViewPlatInfo *plat = view.PlatInfo;
    plat->WorldViewMatrix = c.view;
    plat->ViewScreenMatrix = c.projection;
    plat->WorldScreenMatrix = c.viewProjection;
    plat->ViewScreenBiasedMatrix = c.projection;
    plat->WorldScreenBiasedMatrix = c.viewProjection;
    eBuildFrustumPlanes(c.viewProjection, plat->FrustumPlanes);
    if (Camera *camera = view.pCamera)
    {
        const CameraParams &from = player.pCamera->CurrentKey;
        CameraParams &key = camera->CurrentKey;
        key.Matrix = c.view;
        key.Position = from.Position;
        key.Direction = {0.0f, 0.0f, 0.0f};
        key.Target = c.center;
    }
    view.H = player.H * kCasterDetailScale;
}

/** World position to (u, v) in cascade `index`'s quarter of the atlas and light-space depth. */
void AtlasMatrix(const Cascade &c, int index, float out[16])
{
    bMatrix4 bias;
    Identity(bias);
    bias.v0.x = 0.25f;
    bias.v1.y = -0.25f;
    bias.v3.x = 0.25f + 0.5f * static_cast<float>(index & 1);
    bias.v3.y = 0.25f + 0.5f * static_cast<float>(index >> 1);
    bMatrix4 m;
    D3DXMatrixMultiply(&m, &c.viewProjection, &bias);
    memcpy(out, &m, sizeof(m));
}

DWORD AsWord(float f)
{
    DWORD word;
    memcpy(&word, &f, 4);
    return word;
}

/** -shadowprofile: CPU time of each cascade's parts (culling, world models, scenery, cars) and its static layer's
 * drawings, logged every 5 s. */
LONGLONG g_partTicks[kCascades][4];
int g_partDraws[kCascades][4];
int g_partFrames;
/** Static layer drawings by reason (debug::Redraw). */
int g_layerDrawings[kCascades][5];
/** The slowest frame's shadow drawing (Ticks) and the frames over 2 ms, since the last log. */
LONGLONG g_worstTicks;
int g_slowFrames;

LONGLONG Ticks()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

LONGLONG TicksPerSecond()
{
    static const LONGLONG frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    return frequency;
}

/** The vehicles that can shadow what cascade `index` is sampled for: receivers between where the previous cascade
 * starts blending into it and its edge (lighting.hlsli SunVisibility picks the cascade by distance from the camera).
 * A car whose shadow cannot reach that ring is left out, which only depends on where it is, never on where the camera
 * looks: the player's car and those around it are drawn into the two near cascades only, the cars far away into the
 * far ones. Helicopters (and anything but a car) are drawn into every cascade. */
void DrawVehicleCasters(eView *view, int index, const bVector3 &sun, const bVector3 &eye)
{
    const float reach = kCarRadius + kCarHeight * sqrtf(1.0f - sun.z * sun.z) / sun.z;
    const float inner = index > 0 ? kCascadeBlendStart * kSplits[index - 1] : 0.0f;
    const float outer = g_state.cascades[index].radius;
    char **conns = *reinterpret_cast<char ***>(kVehicleRenderConns);
    const int count = *reinterpret_cast<const int *>(kVehicleRenderConnCount);
    for (int i = 0; i < count; ++i)
    {
        char *conn = conns[i];
        if (*reinterpret_cast<const unsigned int *>(conn) == kCarRenderConnVtable)
        {
            const float *p = reinterpret_cast<const float *>(conn + kCarPosition);
            const float dx = p[0] - eye.x, dy = p[1] - eye.y, dz = p[2] - eye.z;
            const float distance = sqrtf(dx * dx + dy * dy + dz * dz);
            if (distance + reach < inner || distance - reach > outer)
                continue;
        }
        typedef void(__thiscall * OnRender)(void *, eView *, int);
        (*reinterpret_cast<OnRender *const *>(conn))[3](conn, view, 0);
    }
}

/** The casters of cascade `index`, culled for it: world props, scenery, cars. A static layer takes only the scenery
 * shown in full; the dynamic pass over it the rest. */
void DrawCasters(int index, nfs::culling::CasterSet set, const bVector3 &sun, const bVector3 &eye)
{
    static const bool profile = nfs::vk::HasArgument("-shadowprofile");
    LONGLONG t0 = profile ? Ticks() : 0;
    auto lap = [&](int part) {
        if (!profile)
            return;
        const LONGLONG t1 = Ticks();
        g_partTicks[index][part] += t1 - t0;
        t0 = t1;
    };
    eView &view = eViews[kShadowView];
    CullInfo cull;
    memset(&cull, 0, sizeof(cull));
    SetViewCullFlags(&view, reinterpret_cast<char *>(cull.views[0]), kShadowCullFlags);
    cull.count = 1;
    nfs::culling::SetShadowCasters(true, index, set);
    reinterpret_cast<GrandSceneryCullInfo *>(&cull)->DoCulling();
    nfs::culling::SetShadowCasters(false);
    lap(0);
    const bool everything = set == nfs::culling::kAllCasters || set == nfs::culling::kDynamicCasters;
    int draws = dword_987B00;
    if (everything)
        RenderWorldModels(&view, 0);
    sub_6E2F50();
    g_partDraws[index][1] += dword_987B00 - draws;
    draws = dword_987B00;
    lap(1);
    // All the scenery: the original's shadow flags (0x100, 0x200, 0x2000) leave out the road and terrain models, and
    // with them overpass decks, tunnel roofs and the roofs of covered areas.
    reinterpret_cast<GrandSceneryCullInfo *>(&cull)->StuffScenery(&view, 0);
    sub_6E2F50();
    g_partDraws[index][2] += dword_987B00 - draws;
    draws = dword_987B00;
    lap(2);
    DrawCarShadow = 1;
    if (everything)
        DrawVehicleCasters(&view, index, sun, eye);
    sub_6E2F50();
    g_partDraws[index][3] += dword_987B00 - draws;
    lap(3);
}

/** The headlights' view: from `position` along `direction`. */
void MakeBeamView(const float position[3], const float direction[3], Cascade &c)
{
    const bVector3 forward = {direction[0], direction[1], direction[2]};
    c.eye = {position[0], position[1], position[2]};
    c.center = Add(c.eye, forward, 10.0f);
    const bVector3 worldUp = {0.0f, 0.0f, 1.0f};
    NFS_ORIGINAL(0x6cf0a0, void(__cdecl *)(bMatrix4 *, const bVector3 *, const bVector3 *, const bVector3 *))(
        &c.view, &c.eye, &c.center, &worldUp);
    const float zFar = nfs::lights::kHeadlightRadius;
    c.depthRange = zFar - kBeamNear;
    Identity(c.projection);
    c.projection.v0.x = 1.0f / kBeamSide;
    c.projection.v1.y = 2.0f / (kBeamAbove + kBeamBelow);
    // The view's y points down (0x6cf0a0 with the world's z as up).
    c.projection.v2.y = (kBeamAbove - kBeamBelow) / (kBeamAbove + kBeamBelow);
    c.projection.v2.z = zFar / c.depthRange;
    c.projection.v2.w = 1.0f;
    c.projection.v3.z = -kBeamNear * zFar / c.depthRange;
    c.projection.v3.w = 0.0f;
    D3DXMatrixMultiply(&c.viewProjection, &c.view, &c.projection);
}

/** The vehicles in the beam's reach but for the player's car, the one that stands at `origin`. */
void DrawBeamVehicles(eView *view, const bVector3 &eye, const float origin[3])
{
    char **conns = *reinterpret_cast<char ***>(kVehicleRenderConns);
    const int count = *reinterpret_cast<const int *>(kVehicleRenderConnCount);
    int own = -1;
    float nearest = 1e12f;
    for (int i = 0; i < count; ++i)
        if (*reinterpret_cast<const unsigned int *>(conns[i]) == kCarRenderConnVtable)
        {
            const float *p = reinterpret_cast<const float *>(conns[i] + kCarPosition);
            const float dx = p[0] - origin[0], dy = p[1] - origin[1], dz = p[2] - origin[2];
            const float distance2 = dx * dx + dy * dy + dz * dz;
            if (distance2 < nearest)
            {
                nearest = distance2;
                own = i;
            }
        }
    const float reach = nfs::lights::kHeadlightRadius + kCarRadius;
    for (int i = 0; i < count; ++i)
    {
        char *conn = conns[i];
        if (i == own)
            continue;
        if (*reinterpret_cast<const unsigned int *>(conn) == kCarRenderConnVtable)
        {
            const float *p = reinterpret_cast<const float *>(conn + kCarPosition);
            const float dx = p[0] - eye.x, dy = p[1] - eye.y, dz = p[2] - eye.z;
            if (dx * dx + dy * dy + dz * dz > reach * reach)
                continue;
        }
        typedef void(__thiscall * OnRender)(void *, eView *, int);
        (*reinterpret_cast<OnRender *const *>(conn))[3](conn, view, 0);
    }
}

/** Dynamic Shadows: draws the player's headlights' shadow map, marks them for it (engine/rendering/lights/lights.h) and publishes its matrix.
 * Called inside the shadow pass's state, after the cascades. */
void RenderHeadlightShadow(nfs::vk::Renderer *r, const bVector3 &sun, const eView &player)
{
    if (!nfs::display::DynamicShadows())
    {
        if (g_state.headlightDepth)
        {
            r->SetHeadlightShadow(nullptr);
            r->Release(reinterpret_cast<nfs::vk::Object *>(g_state.headlightDepth));
            g_state.headlightDepth = nullptr;
        }
        return;
    }
    float position[3], direction[3], origin[3], spread;
    if (nfs::timeofday::LocalLights() <= 0.0f || !nfs::lights::PlayerHeadlights(position, direction, origin, &spread))
        return;
    if (!g_state.headlightDepth)
    {
        g_state.headlightDepth = r->CreateTexture(kBeamMapSize, kBeamMapSize, 1, D3DFMT_D24S8, true);
        if (!g_state.headlightDepth)
            return;
        eRenderTarget &t = g_state.headlightTarget;
        memset(&t, 0, sizeof(t));
        t.ViewID = kShadowView;
        t.DepthSurface = reinterpret_cast<IDirect3DSurface9 *>(g_state.headlightDepth);
        t.unknown_10 = 1;
        t.Width = t.Height = kBeamMapSize;
        r->SetHeadlightShadow(g_state.headlightDepth);
        if (nfs::vk::HasArgument("-beamdebug"))
            r->SetDebugTexture(g_state.headlightDepth);
    }
    Cascade beam;
    MakeBeamView(position, direction, beam);
    AimShadowView(beam, sun, player);
    device::SetTarget(nullptr, g_state.headlightDepth, kBeamMapSize, kBeamMapSize);
    g_CurrentRenderTarget = &g_state.headlightTarget;
    device::SetViewport(0, 0, kBeamMapSize, kBeamMapSize);
    device::Clear(D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);
    device::SetRenderState(D3DRS_DEPTHBIAS, AsWord(kBeamConstantBias));
    device::SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, AsWord(kBeamSlopeBias));
    eView &view = eViews[kShadowView];
    CullInfo cull;
    memset(&cull, 0, sizeof(cull));
    SetViewCullFlags(&view, reinterpret_cast<char *>(cull.views[0]), kShadowCullFlags);
    cull.count = 1;
    nfs::culling::SetShadowCasters(true);
    reinterpret_cast<GrandSceneryCullInfo *>(&cull)->DoCulling();
    nfs::culling::SetShadowCasters(false);
    RenderWorldModels(&view, 0);
    sub_6E2F50();
    const int drawsProps = dword_987B00;
    reinterpret_cast<GrandSceneryCullInfo *>(&cull)->StuffScenery(&view, kBeamStuffFlags);
    sub_6E2F50();
    const int drawsScenery = dword_987B00;
    DrawCarShadow = 1;
    DrawBeamVehicles(&view, beam.eye, origin);
    sub_6E2F50();
    static DWORD logged;
    if (nfs::vk::HasArgument("-beamdebug") && GetTickCount() - logged > 2000)
    {
        logged = GetTickCount();
        nfs::Log("beam: at %d %d %d aim %d %d %d (x100), draws scenery %d cars %d", static_cast<int>(position[0]),
                 static_cast<int>(position[1]), static_cast<int>(position[2]), static_cast<int>(direction[0] * 100),
                 static_cast<int>(direction[1] * 100), static_cast<int>(direction[2] * 100), drawsScenery - drawsProps,
                 dword_987B00 - drawsScenery);
    }
    bMatrix4 bias;
    Identity(bias);
    bias.v0.x = 0.5f;
    bias.v1.y = -0.5f;
    bias.v3.x = 0.5f;
    bias.v3.y = 0.5f;
    bMatrix4 m;
    D3DXMatrixMultiply(&m, &beam.viewProjection, &bias);
    memcpy(g_state.scene.headlightMatrix, &m, sizeof(m));
    // Each lamp's shadow and its blur (lighting.hlsli HeadlightVisibility): the map's u per radian is 0.5 / kBeamSide,
    // and 1 / z = 1 / near - depth * (far - near) / (near * far), so a lamp `spread` metres to the side moves a
    // caster's shadow by 0.5 / kBeamSide * spread * (far - near) / (near * far) in u per unit of depth between them.
    const float zFar = nfs::lights::kHeadlightRadius;
    const float perDepth = 0.5f / kBeamSide * (zFar - kBeamNear) / (kBeamNear * zFar);
    g_state.scene.headlightInfo[0] = 1.0f / static_cast<float>(kBeamMapSize);
    g_state.scene.headlightInfo[1] = perDepth * spread;
    g_state.scene.headlightInfo[2] = perDepth * kHeadlightLens;
    g_state.scene.headlightInfo[3] = static_cast<float>(kBeamMapSize);
}

void LogPartTimes()
{
    static DWORD logged;
    ++g_partFrames;
    if (GetTickCount() - logged < 5000)
        return;
    logged = GetTickCount();
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    const double us = 1000000.0 / static_cast<double>(frequency.QuadPart) / g_partFrames;
    for (int i = 0; i < kCascades; ++i)
        nfs::Log("shadowprofile c%d (us a frame): cull %d, world models %d (%d draws), scenery %d (%d draws), cars %d "
                 "(%d draws); static layer drawn %d times (view left %d, jump %d, scenery %d) in %d frames",
                 i, static_cast<int>(g_partTicks[i][0] * us), static_cast<int>(g_partTicks[i][1] * us),
                 g_partDraws[i][1] / g_partFrames, static_cast<int>(g_partTicks[i][2] * us),
                 g_partDraws[i][2] / g_partFrames, static_cast<int>(g_partTicks[i][3] * us),
                 g_partDraws[i][3] / g_partFrames,
                 g_layerDrawings[i][debug::kScheduled] + g_layerDrawings[i][debug::kViewLeft] +
                     g_layerDrawings[i][debug::kJump] + g_layerDrawings[i][debug::kSceneryChanged],
                 g_layerDrawings[i][debug::kViewLeft], g_layerDrawings[i][debug::kJump],
                 g_layerDrawings[i][debug::kSceneryChanged], g_partFrames);
    nfs::Log("shadowprofile: slowest frame %d us, %d frames over 2 ms of %d", static_cast<int>(g_worstTicks * us * g_partFrames),
             g_slowFrames, g_partFrames);
    g_worstTicks = 0;
    g_slowFrames = 0;
    memset(g_partTicks, 0, sizeof(g_partTicks));
    memset(g_layerDrawings, 0, sizeof(g_layerDrawings));
    memset(g_partDraws, 0, sizeof(g_partDraws));
    g_partFrames = 0;
}
} // namespace

bool Active()
{
    return g_state.active;
}

float HazeShading()
{
    return g_state.active ? g_state.scene.lightInfo[1] : 0.0f;
}

void RenderSunShadows()
{
    nfs::shadow::debug::Poll();
    g_state.active = false;
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    const eView &player = eViews[kPlayerView];
    SceneConstants &scene = g_state.scene;
    static const bool off = nfs::config::Int("Debug", "SunShadows", 1) == 0;
    if (!r || off || TheGameFlowManager_CurrentGameFlowState != kGameFlowRacing || lpData_00901830 <= 0 ||
        !TimeOfDayPointer || !player.Active || !player.pCamera || !EnsureTargets(r))
    {
        scene.cameraPosition[3] = 0.0f;
        if (player.pCamera)
        {
            const bVector3 &eye = player.pCamera->CurrentKey.Position;
            const float camera[3] = {eye.x, eye.y, eye.z};
            const bVector3 forward = Normalized(player.pCamera->CurrentKey.Direction);
            scene.cameraPosition[0] = eye.x;
            scene.cameraPosition[1] = eye.y;
            scene.cameraPosition[2] = eye.z;
            scene.cameraForward[0] = forward.x;
            scene.cameraForward[1] = forward.y;
            scene.cameraForward[2] = forward.z;
            scene.lightInfo[0] = static_cast<float>(nfs::lights::Collect(scene.lights, nfs::lights::kMaxLights, camera, scene.cameraForward, MirrorOn()));
            nfs::lamps::Update(camera, scene.lampVolume);
        }
        nfs::timeofday::CarShade(scene.timeOfDay);
        scene.timeOfDay[3] = nfs::timeofday::LocalLights();
        nfs::timeofday::ParticleLight(scene.particleSun, scene.particleSky);
        nfs::timeofday::SkyLight(scene.skyLight);
        nfs::timeofday::TunnelLight(scene.tunnelLight);
        scene.tunnelLight[3] = nfs::lamps::Tunnels() ? 1.0f : 0.0f;
        const float none[3] = {};
        nfs::lamps::Haze(none, none, scene.haze, scene.hazeLamps);
        scene.haze[3] = 0.0f;
        WeatherConstants(scene);
        if (r)
            r->SetSceneConstants(&scene, sizeof(scene));
        return;
    }
    const bVector3 sun = SunDirection();
    const CameraParams &key = player.pCamera->CurrentKey;
    {
        // The camera's velocity, smoothed over a few frames; a jump (a warp, a new race) is not a speed.
        const LONGLONG now = Ticks();
        if (g_state.lastCameraTicks && now > g_state.lastCameraTicks)
        {
            const float dt = static_cast<float>(static_cast<double>(now - g_state.lastCameraTicks) /
                                                static_cast<double>(TicksPerSecond()));
            const bVector3 step = Add(key.Position, g_state.lastCamera, -1.0f);
            const float moved2 = Dot(step, step);
            if (dt > 0.0f && dt < 0.25f && moved2 < kJumpDistance * kJumpDistance)
            {
                const float k = dt / (dt + 0.05f);
                const bVector3 v = {step.x / dt, step.y / dt, step.z / dt};
                g_state.cameraVelocity = Add(g_state.cameraVelocity, Add(v, g_state.cameraVelocity, -1.0f), k);
                g_state.frameSeconds = g_state.frameSeconds ? g_state.frameSeconds * 0.9f + dt * 0.1f : dt;
            }
            else
                g_state.cameraVelocity = {0.0f, 0.0f, 0.0f};
        }
        g_state.lastCamera = key.Position;
        g_state.lastCameraTicks = now;
    }
    const bVector3 moved = Add(key.Position, g_state.drawnFrom, -1.0f);
    const bool all = !g_state.drawn || Dot(moved, moved) > kJumpDistance * kJumpDistance ||
                     Dot(sun, g_state.drawnSun) < kSunTurnCosine;
    ++g_state.frame;
    bool redraw[kCascades];
    debug::Redraw why[kCascades];
    bool sunStepped = false;
    Cascade *cascades = g_state.cascades;
    // redraw: the near cascades are drawn every frame; a layered one's static layer is drawn at once when the cascade
    // has to move now or the camera jumped. A layer that only has to catch up (the camera used half the cascade's
    // margin, the scenery it holds changed, the sun moved, sections in full it lacks have waited) is drawn no sooner
    // than kCatchUpSeconds after its last, one a frame, none in a frame that draws one at once; the far ones a slice a
    // frame (kLayerSlices).
    const LONGLONG startTicks = Ticks();
    static const bool catchUpLimit = nfs::config::Int("Debug", "ShadowCatchUp", 1) != 0;
    bool urgent[kCascades] = {}, soon[kCascades] = {}, slice[kCascades] = {};
    bool layerDrawn = false;
    for (int i = kFirstLayeredCascade; i < kCascades; ++i)
    {
        if (all || !g_state.staticValid[i])
            urgent[i] = true;
        else
        {
            bVector3 center;
            float radius;
            CascadeSphere(i, player, center, radius);
            const bVector3 d = Add(center, cascades[i].center, -1.0f);
            const float reach = sqrtf(Dot(d, d)) + radius;
            const float margin = kLayerMargin[i] + radius * kLayerMarginFraction[i];
            urgent[i] = reach > cascades[i].radius;
            soon[i] = reach > cascades[i].radius - 0.5f * margin;
            // A fast camera reaches the edge before a sliced layer started at half the margin is done: start it when
            // the time left is about what its slices take, with room to spare.
            const bVector3 &v = g_state.cameraVelocity;
            const float speed = sqrtf(Dot(v, v));
            if (!soon[i] && speed > 1.0f && g_state.frameSeconds > 0.0f)
                soon[i] = (cascades[i].radius - reach) / speed <
                          static_cast<float>(kLayerSlices[i] + 4) * g_state.frameSeconds + 0.05f;
        }
        layerDrawn |= urgent[i];
    }
    bool sliceTaken = false;
    for (int i = kFirstLayeredCascade; i < kCascades && !sliceTaken; ++i)
        if (g_state.building[i] && !urgent[i])
            slice[i] = sliceTaken = true;
    for (int i = 0; i < kCascades; ++i)
    {
        redraw[i] = i < kFirstLayeredCascade || urgent[i];
        why[i] = !redraw[i] ? debug::kNotRedrawn
                 : all      ? debug::kJump
                 : !g_state.staticValid[i] ? debug::kScheduled
                                           : debug::kViewLeft;
        if (redraw[i])
            g_state.building[i] = false;
        if (i >= kFirstLayeredCascade && !redraw[i] && !g_state.building[i])
        {
            debug::Redraw reason = debug::kNotRedrawn;
            if (kLayerSlices[i] > 1 && soon[i])
                reason = debug::kScheduled;
            else if (nfs::culling::StaticCastersChanged(i))
                reason = debug::kSceneryChanged;
            else if (Dot(sun, g_state.sunAt[i]) < SunStepCosine())
                reason = debug::kSunMoved;
            if (!nfs::culling::UnbakedSections(i))
                g_state.unbakedSince[i] = 0;
            else if (!g_state.unbakedSince[i])
                g_state.unbakedSince[i] = g_state.frame;
            else if (reason == debug::kNotRedrawn && g_state.frame - g_state.unbakedSince[i] >= kUnbakedFrames)
                reason = debug::kSceneryChanged;
            const bool mayCatchUp = !catchUpLimit ||
                                    (!layerDrawn && !sliceTaken &&
                                     static_cast<double>(startTicks - g_state.layerTicks[i]) >=
                                         kCatchUpSeconds[i] * static_cast<double>(TicksPerSecond()));
            if (reason != debug::kNotRedrawn && mayCatchUp)
            {
                ++g_layerDrawings[i][reason];
                if (kLayerSlices[i] > 1 && catchUpLimit)
                {
                    MakeCascade(i, player, sun, g_state.buildCascade[i]);
                    g_state.buildSun[i] = sun;
                    g_state.buildSlice[i] = 0;
                    g_state.building[i] = true;
                    nfs::culling::BeginStaticBuild(i);
                    slice[i] = sliceTaken = true;
                }
                else
                {
                    redraw[i] = true;
                    why[i] = reason;
                    layerDrawn = true;
                }
            }
        }
        if (redraw[i])
        {
            MakeCascade(i, player, sun, cascades[i]);
            g_state.sunAt[i] = sun;
            if (i >= kFirstLayeredCascade)
                g_state.layerTicks[i] = startTicks;
        }
    }
    g_state.drawn = true;
    g_state.drawnFrom = key.Position;
    g_state.drawnSun = sun;

    eRenderTarget *savedTarget = g_CurrentRenderTarget;
    const int savedPass = dword_8FAE6C;
    dword_8FAE6C = 1;
    dword_982B38 = 1;
    // Off while the cascades draw: with the original's narrower view and its own light direction it left out casters
    // whose shadows lay in plain view, and which ones changed with every move of the camera - shop fronts, awnings and
    // overhangs lost their shadows on the facades and the street the moment the camera passed a certain point, and got
    // them back as it moved on. The cascades pick their casters by their own volumes.
    const int savedCasterTest = dword_8FAF14;
    dword_8FAF14 = 0;
    nfs::x360::DoShadowMapLod();
    device::SetTarget(nullptr, g_state.depth, kAtlasSize, kAtlasSize);
    g_CurrentRenderTarget = &g_state.target;
    static const float slopeBias = nfs::config::Float("Debug", "CasterSlopeBias", kSlopeBias);
    static const float constantBias = nfs::config::Float("Debug", "CasterConstantBias", kConstantBias);
    device::SetRenderState(D3DRS_DEPTHBIAS, AsWord(constantBias));
    device::SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, AsWord(slopeBias));
    static int shaderLogFrames = nfs::vk::HasArgument("-shaderlog") ? 600 : 0;
    const bool logShaders = shaderLogFrames > 0 && --shaderLogFrames == 0;
    if (logShaders)
        r->SetShaderLog(true);
    for (int i = 0; i < kCascades; ++i)
    {
        const DWORD x = static_cast<DWORD>((i & 1) * kTileSize), y = static_cast<DWORD>((i >> 1) * kTileSize);
        AimShadowView(cascades[i], sun, player);
        debug::SetCurrentCascade(i);
        if (i < kFirstLayeredCascade)
        {
            device::SetViewport(x, y, kTileSize, kTileSize);
            device::Clear(D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);
            DrawCasters(i, nfs::culling::kAllCasters, sun, key.Position);
        }
        else
        {
            const DWORD layerX = static_cast<DWORD>((i - kFirstLayeredCascade) * kTileSize);
            bool finished = false;
            if (slice[i])
            {
                AimShadowView(g_state.buildCascade[i], g_state.buildSun[i], player);
                device::SetTarget(nullptr, g_state.backDepth, kLayeredCascades * kTileSize, kTileSize);
                device::SetViewport(layerX, 0, kTileSize, kTileSize);
                if (g_state.buildSlice[i] == 0)
                    device::Clear(D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);
                nfs::culling::SetStaticSlice(g_state.buildSlice[i], kLayerSlices[i]);
                DrawCasters(i, nfs::culling::kStaticSlice, g_state.buildSun[i], key.Position);
                if (++g_state.buildSlice[i] == kLayerSlices[i])
                {
                    r->CopyRect(g_state.backDepth, layerX, 0, g_state.staticDepth, layerX, 0, kTileSize, kTileSize);
                    cascades[i] = g_state.buildCascade[i];
                    g_state.sunAt[i] = g_state.buildSun[i];
                    nfs::culling::FinishStaticBuild(i);
                    g_state.building[i] = false;
                    g_state.staticValid[i] = true;
                    g_state.unbakedSince[i] = 0;
                    g_state.layerTicks[i] = startTicks;
                    finished = true;
                }
                device::SetTarget(nullptr, g_state.depth, kAtlasSize, kAtlasSize);
                AimShadowView(cascades[i], sun, player);
            }
            if (redraw[i])
            {
                device::SetTarget(nullptr, g_state.staticDepth, kLayeredCascades * kTileSize, kTileSize);
                device::SetViewport(layerX, 0, kTileSize, kTileSize);
                device::Clear(D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);
                DrawCasters(i, nfs::culling::kStaticCasters, sun, key.Position);
                if (why[i] != debug::kSceneryChanged && why[i] != debug::kSunMoved)
                    ++g_layerDrawings[i][why[i]];
                g_state.staticValid[i] = true;
                g_state.unbakedSince[i] = 0;
                device::SetTarget(nullptr, g_state.depth, kAtlasSize, kAtlasSize);
            }
            const LONGLONG now = Ticks();
            if (i >= kFirstThrottledCascade && !redraw[i] && !finished && g_state.tileValid[i] &&
                g_state.frame - g_state.tileFrame[i] < kFarRefreshFrames &&
                static_cast<double>(now - g_state.tileTicks[i]) < kFarRefreshSeconds * static_cast<double>(TicksPerSecond()))
            {
                debug::SetCurrentCascade(-1);
                continue;
            }
            g_state.tileFrame[i] = g_state.frame;
            g_state.tileTicks[i] = now;
            g_state.tileValid[i] = true;
            r->CopyRect(g_state.staticDepth, layerX, 0, g_state.depth, x, y, kTileSize, kTileSize);
            device::SetViewport(x, y, kTileSize, kTileSize);
            DrawCasters(i, nfs::culling::kDynamicCasters, sun, key.Position);
        }
        debug::SetCurrentCascade(-1);
    }
    {
        const bVector3 forward = Normalized(key.Direction);
        const float sunVector[3] = {sun.x, sun.y, sun.z}, view[3] = {forward.x, forward.y, forward.z};
        debug::NoteFrame(sunVector, view);
        for (int i = 0; i < kCascades; ++i)
            debug::NoteCascade(i, why[i], kSplits[i], cascades[i].radius, cascades[i].texel);
    }
    {
        const float eye[3] = {key.Position.x, key.Position.y, key.Position.z};
        nfs::lights::RecordCars(eye);
    }
    RenderHeadlightShadow(r, sun, player);
    if (logShaders)
        r->SetShaderLog(false);
    static const bool profileParts = nfs::vk::HasArgument("-shadowprofile");
    if (profileParts)
    {
        const LONGLONG spent = Ticks() - startTicks;
        if (spent > g_worstTicks)
            g_worstTicks = spent;
        g_slowFrames += spent * 1000 > TicksPerSecond() * 2;
        LogPartTimes();
    }
    device::SetRenderState(D3DRS_DEPTHBIAS, 0);
    device::SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, 0);
    dword_982B38 = 0;
    dword_8FAF14 = savedCasterTest;
    dword_8FAE6C = savedPass;
    g_CurrentRenderTarget = savedTarget;

    for (int i = 0; i < kCascades; ++i)
    {
        AtlasMatrix(cascades[i], i, scene.cascadeMatrix[i]);
        scene.cascadeSplits[i] = kSplits[i];
        scene.cascadeTexelWorld[i] = cascades[i].texel;
        scene.cascadeDepthRange[i] = cascades[i].depthRange;
    }
    const bVector3 forward = Normalized(key.Direction);
    scene.cameraPosition[0] = key.Position.x;
    scene.cameraPosition[1] = key.Position.y;
    scene.cameraPosition[2] = key.Position.z;
    scene.cameraPosition[3] = 1.0f;
    scene.cameraForward[0] = forward.x;
    scene.cameraForward[1] = forward.y;
    scene.cameraForward[2] = forward.z;
    scene.cameraForward[3] = kLastFadeStart;
    scene.sun[0] = sun.x;
    scene.sun[1] = sun.y;
    scene.sun[2] = sun.z;
    scene.sun[3] = kSunTangent;
    scene.atlas[0] = 1.0f / static_cast<float>(kAtlasSize);
    scene.atlas[1] = static_cast<float>(kTileSize);
    scene.atlas[2] = static_cast<float>(kCascades);
    static const bool fullDebugView = nfs::vk::HasArgument("-shadowdebug");
    scene.atlas[3] = fullDebugView ? 1.0f : debug::ViewMode();
    scene.lightInfo[0] = static_cast<float>(nfs::lights::Collect(scene.lights, nfs::lights::kMaxLights, scene.cameraPosition, scene.cameraForward, MirrorOn()));
    static const bool shadowedHaze = nfs::config::Int("Debug", "ShadowedHaze", 1) != 0;
    // The moon lights too little of the air for its shade to show.
    scene.lightInfo[1] = shadowedHaze ? nfs::timeofday::HazeSunShare() * (1.0f - nfs::timeofday::LocalLights()) : 0.0f;
    scene.lightInfo[2] = static_cast<float>(g_state.frame % 64);
    // The finest shadow detail per metre of distance: at each cascade's inner edge as coarse as its texel, so detail
    // never jumps where cascades meet (ShadowDetail in lighting.hlsli).
    float detailPerMetre = 0.0f;
    for (int i = 1; i < kCascades; ++i)
    {
        const float d = cascades[i].texel / kSplits[i - 1];
        detailPerMetre = d > detailPerMetre ? d : detailPerMetre;
    }
    scene.lightInfo[3] = detailPerMetre * kDetailMargin;
    // Debug.MinCascade=-1 cycles 0, 1, 2 every 30 frames (-2: 1, 2, 3), to compare cascades on one held view.
    static const int minCascade = nfs::config::Int("Debug", "MinCascade", 0);
    scene.testing[0] = static_cast<float>(minCascade >= 0 ? minCascade
                                                          : -1 - minCascade + static_cast<int>(g_state.frame / 30 % 3));
    debug::NoteForcedCascade(static_cast<int>(scene.testing[0]));
    static const int oldBias = nfs::config::Int("Debug", "OldShadowBias", 0);
    scene.testing[1] = static_cast<float>(oldBias);
    nfs::timeofday::CarShade(scene.timeOfDay);
    scene.timeOfDay[3] = nfs::timeofday::LocalLights();
    nfs::timeofday::ParticleLight(scene.particleSun, scene.particleSky);
    nfs::timeofday::SkyLight(scene.skyLight);
    nfs::timeofday::TunnelLight(scene.tunnelLight);
    scene.tunnelLight[3] = nfs::lamps::Tunnels() ? 1.0f : 0.0f;
    nfs::lamps::Update(scene.cameraPosition, scene.lampVolume);
    nfs::lamps::Haze(scene.cameraPosition, scene.cameraForward, scene.haze, scene.hazeLamps);
    WeatherConstants(scene);
    r->SetSceneConstants(&scene, sizeof(scene));
    g_state.active = true;
    static DWORD logged;
    if (fullDebugView && GetTickCount() - logged > 4000)
    {
        logged = GetTickCount();
        nfs::Log("shadow: camera %d %d %d forward %d %d %d (x1000) sun %d %d %d radius %d %d %d %d",
                 static_cast<int>(scene.cameraPosition[0]), static_cast<int>(scene.cameraPosition[1]),
                 static_cast<int>(scene.cameraPosition[2]), static_cast<int>(forward.x * 1000),
                 static_cast<int>(forward.y * 1000), static_cast<int>(forward.z * 1000), static_cast<int>(sun.x * 1000),
                 static_cast<int>(sun.y * 1000), static_cast<int>(sun.z * 1000), static_cast<int>(cascades[0].radius),
                 static_cast<int>(cascades[1].radius), static_cast<int>(cascades[2].radius),
                 static_cast<int>(cascades[3].radius));
        char line[1024];
        int length = 0, all = 0;
        const SectionNode *head = reinterpret_cast<const SectionNode *>(kLoadedSections);
        for (const SectionNode *node = head->next; node && node != head; node = node->next, ++all)
            if (length < 1000)
                length += wsprintfA(line + length, " %d", node->number);
        line[length] = 0;
        nfs::Log("shadow: %d loaded sections:%s", all, line);
        const float *m = scene.cascadeMatrix[0];
        nfs::Log("shadow: cascade 0 matrix rows %d %d %d %d / %d %d %d %d / %d %d %d %d / %d %d %d %d (x1000)",
                 (int)(m[0]*1000),(int)(m[1]*1000),(int)(m[2]*1000),(int)(m[3]*1000),(int)(m[4]*1000),(int)(m[5]*1000),
                 (int)(m[6]*1000),(int)(m[7]*1000),(int)(m[8]*1000),(int)(m[9]*1000),(int)(m[10]*1000),(int)(m[11]*1000),
                 (int)(m[12]*1000),(int)(m[13]*1000),(int)(m[14]*1000),(int)(m[15]*1000));
    }
}

void ReleaseTargets()
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    nfs::lamps::Release();
    if (r)
    {
        r->SetSunShadowAtlas(nullptr);
        r->SetHeadlightShadow(nullptr);
    }
    if (r && g_state.headlightDepth)
        r->Release(reinterpret_cast<nfs::vk::Object *>(g_state.headlightDepth));
    g_state.headlightDepth = nullptr;
    if (r && g_state.depth)
        r->Release(reinterpret_cast<nfs::vk::Object *>(g_state.depth));
    if (r && g_state.staticDepth)
        r->Release(reinterpret_cast<nfs::vk::Object *>(g_state.staticDepth));
    if (r && g_state.backDepth)
        r->Release(reinterpret_cast<nfs::vk::Object *>(g_state.backDepth));
    g_state.depth = g_state.staticDepth = g_state.backDepth = nullptr;
    for (bool &building : g_state.building)
        building = false;
    for (bool &valid : g_state.staticValid)
        valid = false;
    for (bool &valid : g_state.tileValid)
        valid = false;
    g_state.active = false;
    g_state.drawn = false;
    for (float &r : g_state.radius)
        r = 0.0f;
}
} // namespace nfs::shadow

