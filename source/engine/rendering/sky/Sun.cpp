/**
 * The sun in the sky (original: SunTrackLoader 0x503790 and the sun render zPlatform 0x6d04e0 with its occlusion queries
 * 0x6d0350), the Xbox 360 way X360 Stuff draws it (its loader 0x10032420 and its patches to 0x6d04e0, with
 * Rendering::SetSunCloudIntensity): one quad of SUNCOMBINED, the sun info's second layer, faded by the time of day's
 * cloud cover instead of the camera's.
 *
 * The rebuild's own parts: the original asked occlusion queries how much of the sun the scene hides, which the Vulkan
 * renderer does not have; here the post processing samples the main view's depth around the sun instead and reads the
 * result back a few frames later (engine/rendering/x360/post.h), on both renderers. The quad keeps its shape on wide screens (the
 * front end units are 640x480 stretched over the screen). Until the Xbox 360 textures are in the game's packs
 * (docs/X360.md, step 2) there is no SUNCOMBINED; the sun then draws the original's four layers.
 */
#include "game/gen/api.h"
#include "game/platform/Device.h"
#include "game/platform/Effect.h"
#include "game/platform/Platform.h"
#include "legacy/link.h"
#include "engine/rendering/x360/frame.h"
#include "engine/rendering/x360/post.h"
#include "engine/rendering/x360/resources.h"

/** A sun layer's quad (source/engine/rendering/frame/PlatformMisc.cpp). */
void __fastcall MakeSunQuad(const char *sun, void *, ePoly *poly, const char *layer, float largest, float x, float y, float cloud);


extern "C" char *TimeOfDayPointer;
extern "C" int TheGameFlowManager_CurrentGameFlowState;
/** The sun's textures by layer type (the combined one alone in the first slot). */
extern "C" TextureInfo *SunTextures[5];
/** The sun object: its smoothed visibility (+0x18) and a fixed screen position when set (+0x1c, +0x20). */
extern "C" char dword_93E018[];

namespace
{
const int kGameFlowRacing = 6;
const int kTimeOfDaySunset = 1;
const int kParamTechnique = 0x484;
const int kTechniqueAll = 15;
/** The original sun textures: center, halo, major rays, minor rays, ring. */
const unsigned int kOriginalTextures[5] = {0x47BEB4B6, 0xD4D26C59, 0x1744B82D, 0xAD3C5239, 0xD4D80A65};
/** The visibility follows the measurement about this fast (per second). */
const float kVisibilityRate = 20.0f;

/** One layer of a sun info (SunChunkInfo + 0x38, four of them). */
struct SunLayer
{
    int texture;
    int unknown_04;
    float intensity;
    float size;
    float offsetX;
    float offsetY;
    unsigned char colour[4];
    unsigned short angle;
    unsigned short unknown_1e;
    /** How far the layer turns as the sun crosses the screen (a full turn is 1). */
    float sweep;
};

static_assert(sizeof(SunLayer) == 0x24, "SunLayer layout");

bool s_combined;
float s_visibility;

const SunLayer *Layers(const SunChunkInfo *info)
{
    return reinterpret_cast<const SunLayer *>(reinterpret_cast<const char *>(info) + 0x38);
}

float &SunField(int offset)
{
    return *reinterpret_cast<float *>(dword_93E018 + offset);
}

/** The largest layer the flares turn by: of the sun layers (texture 0) that show. */
float LargestSunLayer(const SunLayer *layers, int first)
{
    float largest = 0.0f;
    if (layers[0].intensity > 0.0f && layers[first].texture == 0 && layers[first].size > 0.0f)
        largest = layers[first].size;
    for (int i = 1; i < 4; ++i)
        if (layers[i].intensity > 0.0f && layers[i].texture == 0 && layers[i].size > largest)
            largest = layers[i].size;
    return largest;
}

/** A layer's quad (sub_6C0160: size, turn, colour, the visibility in its alpha), narrowed on wide screens so it stays
 * square. */
void DrawLayer(const SunLayer &layer, TextureInfo *texture, float largest, float x, float y, float cloud)
{
    ePoly poly;
    static const float kUVs[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    for (int i = 0; i < 4; ++i)
    {
        poly.UVs[i][0] = kUVs[i][0];
        poly.UVs[i][1] = kUVs[i][1];
    }
    poly.Flags = 0;
    MakeSunQuad(reinterpret_cast<const char *>(dword_93E018), nullptr, &poly, reinterpret_cast<const char *>(&layer), largest, x, y, cloud);
    const float squeeze = (640.0f / 480.0f) / nfs::x360::g_renderAspect;
    const float centre = x + layer.offsetX;
    for (int i = 0; i < 4; ++i)
        poly.Vertices[i][0] = centre + (poly.Vertices[i][0] - centre) * squeeze;
    eViewPlatInterface::Render(&poly, texture, 0);
}
} // namespace

/** Picks the sun for the time of day and its texture: SUNCOMBINED when the packs have it, else the original five. */
void SunTrackLoader()
{
    SetCurrentSunInfo(bStringHash(static_cast<int>(GetCurrentTimeOfDay()) == kTimeOfDaySunset ? "SUNSET" : "MIDDAY"));
    TextureInfo *combined = GetTextureInfo(bStringHash("SUNCOMBINED"), 0, 0);
    s_combined = combined && combined != DefaultTextureInfo;
    for (int i = 0; i < 5; ++i)
        SunTextures[i] = s_combined ? (i ? nullptr : combined) : GetTextureInfo(kOriginalTextures[i], 1, 0);
}

namespace nfs::x360
{
void RenderSun(eView *view)
{
    // As the original: this frame draws the sun info that was current before it asks for E3DEMO's.
    const SunChunkInfo *info = SunInfo;
    SetCurrentSunInfo(bStringHash("E3DEMO"));
    if (TheGameFlowManager_CurrentGameFlowState != kGameFlowRacing || !info)
        return;
    // The sun's place on the screen in front end units (640x480 over the whole screen), unless the sun object fixes it.
    float x = SunField(0x1c), y = SunField(0x20);
    if (x == 0.0f && y == 0.0f)
    {
        const float *sun = reinterpret_cast<const float *>(TimeOfDayPointer + 0x170);
        const float *m = reinterpret_cast<const float *>(&view->PlatInfo->WorldScreenMatrix);
        float clip[4];
        for (int c = 0; c < 4; ++c)
            clip[c] = sun[0] * m[c] + sun[1] * m[4 + c] + sun[2] * m[8 + c] + m[12 + c];
        // Behind the camera the projection mirrors it onto the screen (the original drew it there).
        if (clip[3] <= 0.0f)
        {
            s_visibility = 0.0f;
            return;
        }
        x = (clip[0] / clip[3] + 1.0f) * 320.0f;
        y = (1.0f - clip[1] / clip[3]) * 240.0f;
    }
    SetSunPosition(x * (1.0f / 640.0f), y * (1.0f / 480.0f));
    float step = g_frameSeconds * kVisibilityRate;
    step = step < 1.0f ? step : 1.0f;
    s_visibility += (SunVisibility() - s_visibility) * step;
    SunField(0x18) = s_visibility;

    eEffect *fe = g_Effects[EFFECT_FE];
    device::BeginEffect(fe);
    bMatrix4 identity;
    identity.v0 = bVector4(1.0f, 0.0f, 0.0f, 0.0f);
    identity.v1 = bVector4(0.0f, 1.0f, 0.0f, 0.0f);
    identity.v2 = bVector4(0.0f, 0.0f, 1.0f, 0.0f);
    identity.v3 = bVector4(0.0f, 0.0f, 0.0f, 1.0f);
    eSetTransform(&identity, 0);
    device::SetRenderState(D3DRS_ZENABLE, FALSE);
    fe->Effect->SetInt(fe->Parameter(kParamTechnique), kTechniqueAll);
    const SunLayer *layers = Layers(info);
    // The Xbox 360 sun fades with the time of day's cloud cover (SetSunCloudIntensity), not the camera's.
    const float cloud = *reinterpret_cast<const float *>(TimeOfDayPointer + 0x160);
    if (s_combined)
    {
        if (SunTextures[0])
            DrawLayer(layers[1], SunTextures[0], LargestSunLayer(layers, 1), x, y, cloud);
    }
    else
    {
        const float largest = LargestSunLayer(layers, 0);
        for (int i = 0; i < 4; ++i)
        {
            const int type = layers[i].texture;
            if (type >= 0 && type < 5 && SunTextures[type])
                DrawLayer(layers[i], SunTextures[type], largest, x, y, cloud);
        }
    }
    device::EndEffect(fe);
    device::SetRenderState(D3DRS_ZENABLE, TRUE);
}
} // namespace nfs::x360
