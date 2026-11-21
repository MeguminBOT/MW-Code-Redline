/**
 * The post processing after the views, the rebuild's own (assets/shaders/post.fx), giving the Xbox 360 look X360 Stuff gave
 * the game (its Rendering::ExecutePostProcessingAndFinalRender and IVisualTreatmentPlat::RenderMWVisualLook), on both
 * renderers.
 *
 * The frame is copied, downsampled to half and quarter of a 720-row frame (the Xbox 360's size, so the blurs keep their
 * size on screen at any resolution), and the quarter is reduced to one luminance texel that is read back a few frames
 * later without stalling. Eye adaptation follows the measured luminance in real time, so it behaves the same at any
 * frame rate. The final composite draws straight to the back buffer: exposure, motion blur, depth of field, the visual
 * treatment's curves and vignette, and bloom.
 *
 * Each effect follows its video option: the visual treatment (curves, tint, vignette and the treatment's own effects)
 * gives way to neutral curves when it is off, bloom follows Over Bright, motion blur follows Motion Blur and its amount,
 * depth of field needs the visual treatment and a camera that asks for it. Exposure always applies: the Xbox 360
 * effects draw the world for it.
 */
#include <math.h>
#include "game/gen/api.h"
#include "game/platform/Device.h"
#include "game/platform/Effect.h"
#include "game/platform/Platform.h"
#include "engine/rendering/display/display.h"
#include "engine/rendering/lights/lamps.h"
#include "engine/rendering/shadows/shadow.h"
#include "legacy/link.h"
#include "core/log.h"
#include "gameplay/world/timeofday.h"
#include "engine/rendering/vulkan/d3dx_effect.h"
#include "engine/rendering/vulkan/renderer.h"
#include "gameplay/world/weather.h"
#include "engine/rendering/x360/frame.h"
#include "engine/rendering/x360/post.h"
#include "engine/rendering/x360/render.h"
#include "engine/rendering/x360/resources.h"
#include "engine/rendering/x360/shader.h"

extern "C" int TheGameFlowManager_CurrentGameFlowState;
extern "C" int CurrentViewMode;
extern "C" unsigned char dword_982B2A;
extern "C" int lpData_00901810;
extern "C" int lpData_009017ec;
extern "C" unsigned char byte_8F9B5C;
extern "C" unsigned char byte_9B0A74;
extern "C" char *pVisualTreatmentPlat;
extern "C" int _mUserMode;
extern "C" int dword_982C60;

extern "C" void __cdecl sub_6D49A0(eView *view, bool toBackBuffer);

namespace nfs::x360
{
float g_middleGray = options::kMiddleGrayValue;

namespace
{
const int kGameFlowRacing = 6;
const int kPlayerView = 1;
const int kSecondPlayerView = 2;
const DWORD kFormatHalf4 = D3DFMT_A16B16G16R16F;
/** The Xbox 360's frame height; the post targets are fractions of it. */
const int kBaseRows = 720;
/** The sun visibility taps: 4x4 over a square 32 front end units (of 480 rows) wide, the original occlusion quad. */
const float kSunTapRadius = 16.0f / 480.0f;
/** Eye adaptation speed per second towards brighter and towards darker: X360 Stuff covers 2% and 1.5% of the way per
 * frame at 30 frames per second, -30 ln(1 - rate). And the darkest it adapts to. */
const float kAdaptBrighter = 0.606081f;
const float kAdaptDarker = 0.453408f;
const float kAdaptFloor = 0.06f;
/** Longer frames (loading hitches) adapt as if this long. */
const float kAdaptLongestFrame = 0.5f;
const float kBrightThreshold = 0.5f;
const float kBloomScale = 4.0f;
/** The Xbox 360 bloom: 15 taps a texel apart along each axis of the eighth size target (weights sum to about 2). */
const float kBloomWeights[8] = {0.265961f, 0.251589f, 0.212965f, 0.161314f,
                                0.10934f,  0.0663181f, 0.035994f, 0.0174813f};

/** A render target texture with its top level as a surface, and its size. */
struct PostTarget
{
    void *texture;
    void *surface;
    UINT width;
    UINT height;
};

/** The post effect and its techniques and parameters, looked up once when it is made. */
struct PostEffect
{
    ID3DXEffect *effect;
    D3DXHANDLE taps16, downsample, bright, blur, luminanceColour, luminanceReduce, sunVisibility, copy, haze, hazeShade,
        hazeUnlit, hazeGlow;
    D3DXHANDLE fxaa[display::kFxaaQualities], fxaaEdge;
    D3DXHANDLE composite[8];
    D3DXHANDLE sourceTex, sceneTex, localTex, bloomTex, vignetteTex, depthTex, dofTex;
    D3DXHANDLE sourceTexel, taps, weights, exposure, blurCorners, blurMask, dof, curve, look, vignetteScale;
    D3DXHANDLE hazeClip, hazeCamera;
};

PostEffect s_post;
PostTarget s_scene[2];
PostTarget s_half;
PostTarget s_quarter;
/** The share of each pixel's ray in shade, at half the frame's resolution (the shaded haze, DrawHaze). */
PostTarget s_hazeUnlit;
/** The lights' glow in the haze, at half the frame's resolution (DrawHaze). */
PostTarget s_hazeGlow;
PostTarget s_bloom[2];
PostTarget s_luminance16;
PostTarget s_luminance4;
/** Two texels read back a few frames later (Renderer::RequestReadback): the frame's average luminance and the sun's
 * visibility. */
PostTarget s_readback;
void *s_white;
void *s_black;

/** What the frame shows: the visual treatment's look (or the neutral one) and the exposure. */
struct Look
{
    float curve[4][4];
    float saturation;
    float brightness;
    float vignette;
    bool darken;
    /** The visual treatment's extra motion blur (0 without it). */
    float blurAmount;
};

Look s_look;
float s_measured = -1.0f;
/** The sun this frame (texture coordinates of the screen) and its measured visibility, a few frames old. */
float s_sunPosition[2];
bool s_sunDrawn;
float s_sunVisibility = 1.0f;
float s_adapted = -1.0f;
float s_gain = 1.0f;

/** A vertex declaration for both renderers, made once. */
struct Declaration
{
    const D3DVERTEXELEMENT9 *elements;
    void *object;

    void Set()
    {
        nfs::vk::Renderer *r = nfs::vk::g_renderer;
        if (!object)
            object = r->CreateVertexLayout(elements);
        r->SetVertexLayout(static_cast<nfs::vk::VertexLayout *>(object));
    }
};

const D3DVERTEXELEMENT9 kScreenElements[] = {
    {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    D3DDECL_END(),
};
Declaration s_screen = {kScreenElements, nullptr};

void ReleaseObject(void *&object)
{
    if (!object)
        return;
    nfs::vk::g_renderer->Release(static_cast<nfs::vk::Object *>(object));
    object = nullptr;
}

void Release(PostTarget &t)
{
    ReleaseObject(t.texture);
    t = {};
}

PostTarget CreatePostTarget(UINT width, UINT height, DWORD format)
{
    PostTarget t = {};
    width = width ? width : 1;
    height = height ? height : 1;
    t.texture = t.surface = nfs::vk::g_renderer->CreateTexture(width, height, 1, format, true);
    if (t.texture)
    {
        t.width = width;
        t.height = height;
    }
    return t;
}

bool Ready(const PostTarget &t)
{
    return t.texture != nullptr;
}

D3DXHANDLE Technique(const char *name)
{
    D3DXHANDLE h = s_post.effect->GetTechniqueByName(name);
    if (!h)
        nfs::Log("post: no technique %s", name);
    return h;
}

D3DXHANDLE Parameter(const char *name)
{
    D3DXHANDLE h = s_post.effect->GetParameterByName(nullptr, name);
    if (!h)
        nfs::Log("post: no parameter %s", name);
    return h;
}

void BindEffect()
{
    PostEffect &p = s_post;
    p.taps16 = Technique("taps16");
    p.downsample = Technique("downsample");
    p.bright = Technique("bright");
    p.blur = Technique("blur");
    p.luminanceColour = Technique("luminance_colour");
    p.luminanceReduce = Technique("luminance_reduce");
    p.sunVisibility = Technique("sun_visibility");
    p.copy = Technique("copy");
    p.haze = Technique("haze");
    p.hazeShade = Technique("haze_shade");
    p.hazeUnlit = Technique("haze_unlit");
    p.hazeGlow = Technique("haze_glow");
    p.fxaa[0] = Technique("fxaa_0");
    p.fxaa[1] = Technique("fxaa_1");
    p.fxaa[2] = Technique("fxaa_2");
    p.fxaaEdge = Parameter("FxaaEdge");
    char name[] = "composite_000";
    for (int i = 0; i < 8; ++i)
    {
        name[10] = static_cast<char>('0' + ((i >> 2) & 1));
        name[11] = static_cast<char>('0' + ((i >> 1) & 1));
        name[12] = static_cast<char>('0' + (i & 1));
        p.composite[i] = Technique(name);
    }
    p.sourceTex = Parameter("SourceTex");
    p.sceneTex = Parameter("SceneTex");
    p.localTex = Parameter("LocalTex");
    p.bloomTex = Parameter("BloomTex");
    p.vignetteTex = Parameter("VignetteTex");
    p.depthTex = Parameter("DepthTex");
    p.dofTex = Parameter("DofTex");
    p.sourceTexel = Parameter("SourceTexel");
    p.taps = Parameter("Taps");
    p.weights = Parameter("Weights");
    p.exposure = Parameter("Exposure");
    p.blurCorners = Parameter("BlurCorners");
    p.blurMask = Parameter("BlurMask");
    p.dof = Parameter("Dof");
    p.curve = Parameter("Curve");
    p.look = Parameter("Look");
    p.vignetteScale = Parameter("VignetteScale");
    p.hazeClip = Parameter("HazeClip");
    p.hazeCamera = Parameter("HazeCamera");
}

void SetTexture(D3DXHANDLE parameter, void *texture)
{
    s_post.effect->SetTexture(parameter, static_cast<IDirect3DBaseTexture9 *>(texture));
}

/** A quad over the whole target; Direct3D 9 pixel centres, which the Vulkan translation keeps. */
void DrawQuad(UINT width, UINT height)
{
    const float dx = 1.0f / static_cast<float>(width), dy = -1.0f / static_cast<float>(height);
    const float v[4][5] = {
        {-1.0f - dx, 1.0f - dy, 0.0f, 0.0f, 0.0f},
        {1.0f - dx, 1.0f - dy, 0.0f, 1.0f, 0.0f},
        {1.0f - dx, -1.0f - dy, 0.0f, 1.0f, 1.0f},
        {-1.0f - dx, -1.0f - dy, 0.0f, 0.0f, 1.0f},
    };
    s_screen.Set();
    device::DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, v, sizeof(v[0]));
}

/** One pass of `technique` over all of `target` (a surface of that size). */
void Run(D3DXHANDLE technique, void *target, UINT width, UINT height)
{
    ID3DXEffect *e = s_post.effect;
    device::SetTarget(target, nullptr, static_cast<int>(width), static_cast<int>(height));
    e->SetTechnique(technique);
    e->Begin(nullptr, 0);
    e->BeginPass(0);
    DrawQuad(width, height);
    e->EndPass();
    e->End();
}

void Run(D3DXHANDLE technique, const PostTarget &target)
{
    Run(technique, target.surface, target.width, target.height);
}

/** One pass of `technique` into a single texel of `target`. */
void RunTexel(D3DXHANDLE technique, const PostTarget &target, UINT x)
{
    ID3DXEffect *e = s_post.effect;
    device::SetTarget(target.surface, nullptr, static_cast<int>(target.width), static_cast<int>(target.height));
    device::SetViewport(x, 0, 1, 1);
    e->SetTechnique(technique);
    e->Begin(nullptr, 0);
    e->BeginPass(0);
    DrawQuad(1, 1);
    e->EndPass();
    e->End();
}

void SetSource(void *texture, UINT width, UINT height)
{
    SetTexture(s_post.sourceTex, texture);
    const float texel[4] = {1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height), 0.0f, 0.0f};
    s_post.effect->SetVector(s_post.sourceTexel, texel);
}

/** 16 taps on a 4x4 grid covering `footprintX` by `footprintY` source texels around each target texel. */
void SetGridTaps(UINT sourceWidth, UINT sourceHeight, float footprintX, float footprintY)
{
    float taps[16][4] = {};
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
        {
            taps[y * 4 + x][0] = (static_cast<float>(x) - 1.5f) * 0.25f * footprintX / static_cast<float>(sourceWidth);
            taps[y * 4 + x][1] = (static_cast<float>(y) - 1.5f) * 0.25f * footprintY / static_cast<float>(sourceHeight);
        }
    s_post.effect->SetVectorArray(s_post.taps, taps, 16);
}

/** `source` (that size) into `target`, averaging over the size ratio. */
void DownsampleGrid(void *source, UINT width, UINT height, const PostTarget &target, D3DXHANDLE technique)
{
    SetSource(source, width, height);
    SetGridTaps(width, height, static_cast<float>(width) / static_cast<float>(target.width),
                static_cast<float>(height) / static_cast<float>(target.height));
    Run(technique, target);
}

float HalfToFloat(unsigned short h)
{
    const unsigned int sign = (h >> 15) & 1, exponent = (h >> 10) & 0x1F, mantissa = h & 0x3FF;
    float value;
    if (exponent == 0)
        value = static_cast<float>(mantissa) * (1.0f / 16777216.0f);
    else if (exponent == 31)
        value = 65504.0f;
    else
    {
        value = 1.0f + static_cast<float>(mantissa) * (1.0f / 1024.0f);
        const int e = static_cast<int>(exponent) - 15;
        for (int i = 0; i < e; ++i)
            value *= 2.0f;
        for (int i = 0; i > e; --i)
            value *= 0.5f;
    }
    return sign ? -value : value;
}

/** The first channel of a readback texel (16-bit float, or 8-bit when the target fell back to A8R8G8B8). */
float ReadbackValue(const void *texel, bool half)
{
    if (half)
        return HalfToFloat(*static_cast<const unsigned short *>(texel));
    return static_cast<float>((*static_cast<const unsigned int *>(texel) >> 16) & 0xFF) * (1.0f / 255.0f);
}

/** The two readback texels: the average luminance of the 4x4 reduction, and the share of depth taps around the sun
 * that see the sky (none when the sun was not drawn). */
void RenderReadback(const PostTarget &target)
{
    SetSource(s_luminance4.texture, 4, 4);
    SetGridTaps(4, 4, 4.0f, 4.0f);
    RunTexel(s_post.luminanceReduce, target, 0);
    float taps[16][4] = {}, weights[16][4] = {};
    if (s_sunDrawn)
    {
        const float ry = kSunTapRadius, rx = ry * static_cast<float>(g_renderHeight) / static_cast<float>(g_renderWidth);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
            {
                float *tap = taps[y * 4 + x];
                tap[0] = s_sunPosition[0] + (static_cast<float>(x) - 1.5f) * (2.0f / 3.0f) * rx;
                tap[1] = s_sunPosition[1] + (static_cast<float>(y) - 1.5f) * (2.0f / 3.0f) * ry;
                const bool inside = tap[0] >= 0.0f && tap[0] <= 1.0f && tap[1] >= 0.0f && tap[1] <= 1.0f;
                weights[y * 4 + x][0] = inside ? 1.0f / 16.0f : 0.0f;
            }
    }
    s_post.effect->SetVectorArray(s_post.taps, taps, 16);
    s_post.effect->SetVectorArray(s_post.weights, weights, 16);
    SetTexture(s_post.depthTex, g_depthTexture && ShouldDrawDepth() ? g_depthTexture : s_black);
    RunTexel(s_post.sunVisibility, target, 1);
    s_sunDrawn = false;
}

/** The readback texels as read: luminance, then sun visibility (each 8 bytes in 16-bit float, else 4). */
void TakeReadback(const unsigned char *texels, bool half)
{
    s_measured = ReadbackValue(texels, half);
    s_sunVisibility = ReadbackValue(texels + (half ? 8 : 4), half);
}

/** Reduces the quarter target to one luminance texel, measures the sun's visibility beside it, and queues both for
 * reading; picks up the newest finished pair. */
void MeasureLuminance()
{
    if (!Ready(s_luminance16) || !Ready(s_luminance4))
        return;
    DownsampleGrid(s_quarter.texture, s_quarter.width, s_quarter.height, s_luminance16, s_post.luminanceColour);
    DownsampleGrid(s_luminance16.texture, 16, 16, s_luminance4, s_post.luminanceReduce);
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    const PostTarget &pair = s_readback;
    if (!Ready(pair))
        return;
    RenderReadback(pair);
    r->RequestReadback(static_cast<nfs::vk::Texture *>(pair.texture));
    unsigned char texels[16];
    const nfs::vk::Texture *t = static_cast<const nfs::vk::Texture *>(pair.texture);
    if (r->ReadbackResult(texels, sizeof(texels)))
        TakeReadback(texels, t->d3dFormat == kFormatHalf4);
}

/** 1 - e^-x for 0 <= x <= 0.5 (the rebuild has no CRT exp): its series to x^6, within 2e-6. */
float ExponentialStep(float x)
{
    if (x <= 0.0f)
        return 0.0f;
    return x * (1.0f - x * (0.5f - x * (1.0f / 6.0f - x * (1.0f / 24.0f - x * (1.0f / 120.0f - x * (1.0f / 720.0f))))));
}

/** Loading and the menus around the race reset the adaptation: the race starts at the luminance it shows. */
bool LoadingFlow()
{
    const int state = TheGameFlowManager_CurrentGameFlowState;
    return state == 4 || state == 5 || state == 1;
}

/** Eye adaptation towards the measured luminance, by elapsed time; the gain that brings the adapted luminance to
 * middle gray. */
void Adapt(float seconds)
{
    const float middle = nfs::timeofday::MiddleGray(g_middleGray);
    const float target = options::kForcedLuminance > 0.0f ? options::kForcedLuminance
                         : s_measured >= 0.0f            ? s_measured
                                                         : middle * 2.0f;
    if (s_adapted < 0.0f || LoadingFlow() || options::kForcedLuminance > 0.0f)
    {
        s_adapted = target;
    }
    else
    {
        s_adapted += (target - s_adapted) * ExponentialStep((target > s_adapted ? kAdaptBrighter : kAdaptDarker) *
                                                            (seconds < kAdaptLongestFrame ? seconds : kAdaptLongestFrame));
    }
    const float floor = nfs::timeofday::AdaptFloor(kAdaptFloor);
    if (s_adapted < floor)
        s_adapted = floor;
    s_gain = middle * 2.0f / (s_adapted + 0.001f);
}

/** The bright parts of the quarter target, downsampled to an eighth and blurred both ways. */
void Bloom()
{
    if (!Ready(s_bloom[0]) || !Ready(s_bloom[1]))
        return;
    ID3DXEffect *e = s_post.effect;
    const float exposure[4] = {s_gain, nfs::timeofday::MiddleGray(g_middleGray) / (s_adapted + 0.001f),
                               kBrightThreshold, kBloomScale};
    e->SetVector(s_post.exposure, exposure);
    SetSource(s_quarter.texture, s_quarter.width, s_quarter.height);
    Run(s_post.bright, s_bloom[0]);
    // The 15 Xbox 360 taps folded into 8 bilinear ones: the centre is split between the first pair on each side.
    float taps[16][4] = {}, weights[16][4] = {};
    const float pairs[4][2] = {{0.0f, 1.0f}, {2.0f, 3.0f}, {4.0f, 5.0f}, {6.0f, 7.0f}};
    for (int side = 0; side < 2; ++side)
        for (int p = 0; p < 4; ++p)
        {
            const float wa = p == 0 ? kBloomWeights[0] * 0.5f : kBloomWeights[static_cast<int>(pairs[p][0])];
            const float wb = kBloomWeights[static_cast<int>(pairs[p][1])];
            const float w = wa + wb;
            const float offset = (pairs[p][0] * wa + pairs[p][1] * wb) / w;
            taps[side * 4 + p][0] = side ? -offset : offset;
            weights[side * 4 + p][0] = w;
        }
    for (int pass = 0; pass < 2; ++pass)
    {
        const PostTarget &from = s_bloom[pass], &to = s_bloom[pass ^ 1];
        float scaled[16][4] = {};
        for (int i = 0; i < 8; ++i)
        {
            if (pass == 0)
                scaled[i][1] = taps[i][0] / static_cast<float>(from.height);
            else
                scaled[i][0] = taps[i][0] / static_cast<float>(from.width);
        }
        SetSource(from.texture, from.width, from.height);
        e->SetVectorArray(s_post.taps, scaled, 16);
        e->SetVectorArray(s_post.weights, weights, 16);
        Run(s_post.blur, to);
    }
}

/** One curve's cubic from its Bezier control points: c0 + c1 t + c2 t^2 + c3 t^3. */
void CurveCoefficients(const float p[4], float c[4])
{
    c[0] = p[0];
    c[1] = p[1] * 3.0f - p[0] * 3.0f;
    c[2] = p[2] * 3.0f - p[1] * 6.0f + p[0] * 3.0f;
    c[3] = p[1] * 3.0f - (p[2] * 3.0f - p[3]) - p[0];
}

float Field(const char *object, int offset)
{
    return *reinterpret_cast<const float *>(object + offset);
}

Look NeutralLook()
{
    Look look = {};
    look.curve[0][0] = 1.0f;
    look.saturation = 1.0f;
    look.brightness = 1.0f;
    return look;
}

/**
 * The visual treatment's look from its state (IVisualTreatmentPlat): the black bloom curve (also used for the colour
 * bloom, as the retail Xbox 360 look does) weighted by the black and colour bloom amounts and the tint, the
 * desaturation, and the treatment's brightness, vignette and motion blur amounts.
 */
Look TreatmentLook(const char *plat)
{
    Look look = {};
    float points[4], coefficients[4];
    for (int i = 0; i < 4; ++i)
        points[i] = Field(plat, 0x14 + 0x10 * i);
    CurveCoefficients(points, coefficients);
    const float black = Field(plat, 0x90), colour = Field(plat, 0x94);
    float tint[3] = {colour * Field(plat, 0xa0), colour * Field(plat, 0xa4), colour * Field(plat, 0xa8)};
    // Fog greys whatever light passes through it; the look's yellow-green tint turned it olive. In fog the tint goes
    // to its own brightness in grey.
    const float fog = weather::Fog();
    if (fog > 0.0f)
    {
        const float grey = 0.299f * tint[0] + 0.587f * tint[1] + 0.114f * tint[2];
        for (float &t : tint)
            t += (grey - t) * fog;
    }
    for (int k = 0; k < 4; ++k)
    {
        look.curve[k][0] = black * coefficients[k] + (k == 0 ? 1.0f - black : 0.0f);
        for (int i = 0; i < 3; ++i)
            look.curve[k][1 + i] = coefficients[k] * tint[i];
    }
    look.saturation = Field(plat, 0x98);
    look.brightness = Field(plat, 0x174) + 1.0f;
    look.vignette = Field(plat, 0x17c);
    look.darken = true;
    look.blurAmount = Field(plat, 0x178);
    return look;
}

/** The look `t` of the way from neutral to `full` (nfs::display::VisualTreatment). */
Look BlendLook(const Look &full, float t)
{
    if (t >= 1.0f)
        return full;
    Look look = NeutralLook();
    for (int k = 0; k < 4; ++k)
        for (int c = 0; c < 4; ++c)
            look.curve[k][c] += (full.curve[k][c] - look.curve[k][c]) * t;
    look.saturation += (full.saturation - look.saturation) * t;
    look.brightness += (full.brightness - look.brightness) * t;
    look.vignette = full.vignette * t;
    look.blurAmount = full.blurAmount * t;
    look.darken = full.darken;
    return look;
}

bool TreatmentOn()
{
    return g_VideoOptions.VisualTreatment && byte_8F9B5C && pVisualTreatmentPlat;
}

/** A camera mover that asks for the motion blur without its radial mask (the first link of the view's mover list). */
bool UnmaskedBlur(eView &view)
{
    char *head = reinterpret_cast<char *>(&view) + 0x44;
    if (head == *reinterpret_cast<char **>(head + 4))
        return false;
    char *link = *reinterpret_cast<char **>(reinterpret_cast<char *>(&view) + 0x48);
    char *mover = link ? link - 4 : nullptr;
    if (!mover || *reinterpret_cast<int *>(mover + 0xc) != 1)
        return false;
    typedef bool(__thiscall * Query)(char *);
    return (*reinterpret_cast<Query **>(mover))[9](mover);
}

/** A camera (ICE) track is playing (0x8f9b28; the delinked objects have no symbol for it): the motion blur follows
 * the camera then. */
bool IceTrackPlaying()
{
    return *reinterpret_cast<const volatile unsigned char *>(0x8F9B28) != 0;
}

/**
 * The motion blur for the player view: per-step offsets at the view's corners along the camera's motion on screen,
 * growing with speed from 15 to 125, and the radial mask and speed vignette terms (IVisualTreatmentPlat::
 * GetMotionBlurParameters). The camera's velocity is per second, so the blur does not depend on the frame rate.
 * False when there is nothing to blur.
 */
bool MotionBlurParameters(float corners[4], float mask[4])
{
    eView &view = eViews[kPlayerView];
    const Camera *camera = view.pCamera;
    if (!camera || !IceTrackPlaying())
        return false;
    const bVector3 &velocity = camera->VelocityKey.Position;
    const float strength = options::kDirectionalMotionBlurStrength;
    float v[4] = {velocity.x * strength, velocity.y * strength, velocity.z * strength, 0.0f};
    const float speed = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (speed <= 15.0f)
        return false;
    const float *m = reinterpret_cast<const float *>(&camera->CurrentKey.Matrix);
    float r[4];
    for (int c = 0; c < 4; ++c)
        r[c] = v[0] * m[c] + v[1] * m[4 + c] + v[2] * m[8 + c] + v[3] * m[12 + c];
    const float length = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2] + r[3] * r[3]);
    const float inverse = length > 0.0f ? 1.0f / length : 0.0f;
    float divisor = g_renderAspect * 0.5625f;
    divisor = divisor > 1.0f ? divisor : 1.0f;
    const float x = r[0] * inverse / divisor, y = r[1] * inverse, z = r[2] * inverse;
    // Towards the screen's edges when moving forwards (X360 Stuff's FixDiagonalMotionBlur 1).
    corners[0] = -(x + z);
    corners[1] = -(z + y);
    corners[2] = -(x - z);
    corners[3] = -(y - z);
    float ramp = (speed - 15.0f) * 0.00909090880f;
    if (speed >= 125.0f || ramp > 0.65f)
        ramp = 0.65f;
    const float amount = s_look.blurAmount;
    const float scale = (amount * 0.0046875f + 0.0075f) * ramp * static_cast<float>(display::MotionBlur()) * 0.01f;
    for (int i = 0; i < 4; ++i)
        corners[i] *= scale;
    float vignette = speed * 0.02f;
    vignette = vignette < 1.0f ? vignette : 1.0f;
    mask[0] = amount * 0.1f + (UnmaskedBlur(view) ? 0.0f : 1.0f);
    mask[1] = amount * vignette;
    return scale > 0.0f;
}

void *TextureNamed(const char *name)
{
    TextureInfo *info = GetTextureInfo(bStringHash(name), 1, 0);
    return info && info != DefaultTextureInfo && info->PlatInfo ? info->PlatInfo->Texture : nullptr;
}

/** Pixelation boxes over the views (sub_6D49A0, through the game's own screen copies). */
bool PixelationPending()
{
    if (!byte_9B0A74 || !g_ScreenCopySurfaces[0] || !g_ScreenCopySurfaces[1])
        return false;
    return eViews[kPlayerView].unknown_64[2] || eViews[kSecondPlayerView].unknown_64[2];
}

void Pixelate()
{
    const int source = dword_982C60 < 0 ? 0 : dword_982C60 & 1;
    dword_982C60 = source;
    device::StretchRect(g_BackBufferSurface, nullptr, g_ScreenCopySurfaces[source], nullptr, D3DTEXF_NONE);
    static const int kViews[2] = {kPlayerView, kSecondPlayerView};
    for (int v : kViews)
    {
        void *look = reinterpret_cast<void *>(eViews[v].unknown_64[2]);
        if (!look)
            continue;
        NFS_ORIGINAL(0x73d0c0, void(__thiscall *)(void *))(look);
        sub_6D49A0(&eViews[v], true);
    }
}

/** The effect holds a reference to each texture it was given (Direct3D 9). */
void UnbindTextures()
{
    if (ID3DXEffect *e = s_post.effect)
    {
        const D3DXHANDLE textures[7] = {s_post.sourceTex,   s_post.sceneTex, s_post.localTex, s_post.bloomTex,
                                        s_post.vignetteTex, s_post.depthTex, s_post.dofTex};
        for (D3DXHANDLE h : textures)
            if (h)
                e->SetTexture(h, nullptr);
    }
}

/**
 * The lamps' and the cars' lights' glow in the haze (engine/rendering/lights/lamps.h Haze) and the rain falling through the air
 * (gameplay/world/weather.h), added to the frame before it is copied: the rebuild's own shaders (Vulkan) gather each light's
 * scattering along the rays up to the depth the player view drew at half resolution (haze_glow), and add that with the
 * drops at a few distances along every pixel's ray (haze). It takes the view's clip space back to world space around its camera: the projection's inverse,
 * then the view's rotation turned back (the view matrix is a rotation and a translation).
 */
void DrawHaze()
{
    eView &view = eViews[kPlayerView];
    const bool glow = s_post.haze && (nfs::lamps::HazeStrength() > 0.0f || nfs::weather::Rain() > 0.0f);
    const bool shade = s_post.hazeShade && s_post.hazeUnlit && Ready(s_hazeUnlit) && nfs::shadow::HazeShading() > 0.0f;
    if ((!glow && !shade) || !g_depthTexture || !ShouldDrawDepth() ||
        TheGameFlowManager_CurrentGameFlowState != kGameFlowRacing || !view.PlatInfo || !view.pCamera)
        return;
    eViewPlatInfo &plat = *view.PlatInfo;
    bMatrix4 projection = plat.ViewScreenMatrix, inverse;
    fInvertMatrix(&inverse, &projection);
    const float *p = &inverse.v0.x, *v = &plat.WorldViewMatrix.v0.x;
    // Row vectors: view = world R + t, so world - camera = view R^T, and the camera is -t R^T.
    float columns[4][4];
    for (int i = 0; i < 4; ++i)
    {
        for (int r = 0; r < 3; ++r)
            columns[r][i] = p[i * 4 + 0] * v[r * 4 + 0] + p[i * 4 + 1] * v[r * 4 + 1] + p[i * 4 + 2] * v[r * 4 + 2];
        columns[3][i] = p[i * 4 + 3];
    }
    float camera[4] = {};
    for (int r = 0; r < 3; ++r)
        camera[r] = -(v[12] * v[r * 4 + 0] + v[13] * v[r * 4 + 1] + v[14] * v[r * 4 + 2]);
    ID3DXEffect *e = s_post.effect;
    e->SetVectorArray(s_post.hazeClip, columns, 4);
    e->SetVector(s_post.hazeCamera, camera);
    SetTexture(s_post.depthTex, g_depthTexture);
    // The shaded haze: what the air in shade does not scatter, taken away (the world's shaders add it whole); the share
    // of each ray in shade at half resolution first.
    if (shade)
    {
        Run(s_post.hazeUnlit, s_hazeUnlit);
        SetSource(s_hazeUnlit.texture, s_hazeUnlit.width, s_hazeUnlit.height);
        Run(s_post.hazeShade, g_BackBufferSurface, g_renderWidth, g_renderHeight);
    }
    if (glow)
    {
        if (s_post.hazeGlow && Ready(s_hazeGlow) && nfs::lamps::HazeStrength() > 0.0f)
        {
            Run(s_post.hazeGlow, s_hazeGlow);
            SetSource(s_hazeGlow.texture, s_hazeGlow.width, s_hazeGlow.height);
        }
        else
        {
            SetSource(s_black, 1, 1);
        }
        Run(s_post.haze, g_BackBufferSurface, g_renderWidth, g_renderHeight);
    }
}

/** FXAA (engine/rendering/display/display.h) over the frame as it stands in the back buffer: the 3D views with their post processing,
 * before the mirror, the HUD and the menus are drawn over them. */
void AntiAlias()
{
    const PostTarget &copy = s_scene[1];
    const D3DXHANDLE technique = s_post.fxaa[display::FxaaQuality()];
    if (!display::FxaaOn() || !technique || !s_post.fxaaEdge || !Ready(copy))
        return;
    // Edges under a twelfth of the brightest neighbour's brightness (and under 1/32 of the range in the dark) are left
    // alone; lone pixels are softened three quarters of the way.
    static const float kEdge[4] = {0.0312f, 0.083f, 0.75f, 0.0f};
    device::StretchRect(g_BackBufferSurface, nullptr, copy.surface, nullptr, D3DTEXF_NONE);
    s_post.effect->SetVector(s_post.fxaaEdge, kEdge);
    SetSource(copy.texture, copy.width, copy.height);
    Run(technique, g_BackBufferSurface, g_renderWidth, g_renderHeight);
}

/** The composite into `target` (the back buffer, or a copy the raindrops then distort). */
void Composite(void *target, const PostTarget &scene, bool blur, bool depthOfField, bool bloom)
{
    ID3DXEffect *e = s_post.effect;
    const float exposure[4] = {s_gain, nfs::timeofday::MiddleGray(g_middleGray) / (s_adapted + 0.001f),
                               kBrightThreshold, bloom ? kBloomScale : 0.0f};
    e->SetVector(s_post.exposure, exposure);
    SetTexture(s_post.sceneTex, scene.texture);
    SetTexture(s_post.localTex, s_quarter.texture);
    SetTexture(s_post.bloomTex, bloom ? s_bloom[0].texture : s_black);
    void *vignette = TextureNamed("UVESVIGNETTE");
    SetTexture(s_post.vignetteTex, vignette ? vignette : s_white);
    SetTexture(s_post.depthTex, g_depthTexture && ShouldDrawDepth() ? g_depthTexture : s_black);
    SetTexture(s_post.dofTex, s_half.texture);
    e->SetVectorArray(s_post.curve, s_look.curve, 4);
    const float look[4] = {s_look.saturation, s_look.brightness, s_look.vignette, s_look.darken ? 1.0f : 0.0f};
    e->SetVector(s_post.look, look);
    e->SetFloat(s_post.vignetteScale,
                TheGameFlowManager_CurrentGameFlowState == kGameFlowRacing && _mUserMode == 1 ? 2.0f : 1.0f);
    float corners[4] = {}, mask[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (blur)
        blur = MotionBlurParameters(corners, mask);
    if (blur)
    {
        e->SetVector(s_post.blurCorners, corners);
        e->SetVector(s_post.blurMask, mask);
    }
    if (depthOfField)
    {
        const CameraParams &key = eViews[kPlayerView].pCamera->CurrentKey;
        const float range = key.DepthOfField * 3.0f, focus = key.FocalDistance * 2.0f;
        depthOfField = focus > 0.0001f || range > 0.0001f;
        const float dof[4] = {focus, range, range != 0.0f ? 1.0f / range : 0.0f, 0.0f};
        e->SetVector(s_post.dof, dof);
    }
    const int variant = (blur ? 4 : 0) | (depthOfField ? 2 : 0) | (bloom ? 1 : 0);
    Run(s_post.composite[variant], target, g_renderWidth, g_renderHeight);
}
} // namespace

void ReleasePostEffect()
{
    if (!s_post.effect)
        return;
    UnbindTextures();
    s_post.effect->Release();
    s_post = {};
}

void LoadPostEffect()
{
    if (s_post.effect)
        return;
    s_post.effect = nfs::vk::CreateEffect("NFS_post");
    if (!s_post.effect)
    {
        nfs::Log("post: no effect NFS_post");
        return;
    }
    BindEffect();
}

void CreatePostTargets()
{
    const UINT width = g_renderWidth, height = g_renderHeight;
    const int rows = static_cast<int>(height) < kBaseRows ? static_cast<int>(height) : kBaseRows;
    const UINT baseHeight = static_cast<UINT>(rows) & ~7u;
    const UINT baseWidth = static_cast<UINT>(static_cast<float>(rows) * g_renderAspect) & ~7u;
    for (PostTarget &t : s_scene)
        t = CreatePostTarget(width, height, D3DFMT_A8R8G8B8);
    s_half = CreatePostTarget(baseWidth >> 1, baseHeight >> 1, kFormatHalf4);
    s_hazeUnlit = CreatePostTarget((width + 1) >> 1, (height + 1) >> 1, D3DFMT_A8R8G8B8);
    s_hazeGlow = CreatePostTarget((width + 1) >> 1, (height + 1) >> 1, kFormatHalf4);
    s_quarter = CreatePostTarget(baseWidth >> 2, baseHeight >> 2, kFormatHalf4);
    for (PostTarget &t : s_bloom)
        t = CreatePostTarget(baseWidth >> 3, baseHeight >> 3, kFormatHalf4);
    s_luminance16 = CreatePostTarget(16, 16, kFormatHalf4);
    s_luminance4 = CreatePostTarget(4, 4, kFormatHalf4);
    s_readback = CreatePostTarget(2, 1, kFormatHalf4);
    {
        nfs::vk::Renderer *r = nfs::vk::g_renderer;
        nfs::vk::Texture *white = r->CreateTexture(1, 1, 1, D3DFMT_A8R8G8B8, false);
        nfs::vk::Texture *black = r->CreateTexture(1, 1, 1, D3DFMT_A8R8G8B8, false);
        const unsigned int kWhite = 0xFFFFFFFF, kBlack = 0xFF000000;
        if (white)
            r->UploadTexture(white, 0, 0, &kWhite, 4);
        if (black)
            r->UploadTexture(black, 0, 0, &kBlack, 4);
        s_white = white;
        s_black = black;
    }
    if (s_post.effect)
        s_post.effect->OnResetDevice();
}

void ReleasePostTargets()
{
    UnbindTextures();
    for (PostTarget &t : s_scene)
        Release(t);
    Release(s_half);
    Release(s_quarter);
    Release(s_hazeUnlit);
    Release(s_hazeGlow);
    for (PostTarget &t : s_bloom)
        Release(t);
    Release(s_luminance16);
    Release(s_luminance4);
    Release(s_readback);
    ReleaseObject(s_white);
    ReleaseObject(s_black);
    if (s_post.effect)
        s_post.effect->OnLostDevice();
}

void SetSunPosition(float u, float v)
{
    s_sunPosition[0] = u;
    s_sunPosition[1] = v;
    s_sunDrawn = true;
}

float SunVisibility()
{
    return s_sunVisibility;
}

void ApplyLook(Shader &s)
{
    ID3DXEffect *e = s.Effect();
    if (!e)
        return;
    if (s.desaturation)
        e->SetFloat(s.desaturation, s_look.saturation);
    if (s.combinedBrightness)
        e->SetFloat(s.combinedBrightness, s_gain * s_look.brightness);
    for (int k = 0; k < 4; ++k)
        if (s.curveCoeffs[k])
            e->SetVector(s.curveCoeffs[k], s_look.curve[k]);
}

/** Direct3D 9: copies the oldest readback slot whose event has passed to memory and takes its texels.
 * GetRenderTargetData waits for all work queued before it, so here it stalls until the frame so far has run (1 to 2 ms
 * of CPU). Done at the start of the frame instead, the exposure came out wrong (banding); to be looked at again. */
void CollectPostReadback()
{
    return;
}

void ExecutePostProcessing()
{
    dword_982B2A = TheGameFlowManager_CurrentGameFlowState == kGameFlowRacing && lpData_00901810 && lpData_009017ec;
    const bool rain = CollectOnScreenRain();
    const PostTarget &scene = s_scene[0];
    if (!s_post.effect || !Ready(scene) || !Ready(s_half) || !Ready(s_quarter))
    {
        if (CurrentViewMode == 2)
            DrawRvm();
        return;
    }
    device::SetDepthTarget(nullptr);
    device::SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    DrawHaze();
    device::StretchRect(g_BackBufferSurface, nullptr, scene.surface, nullptr, D3DTEXF_NONE);

    const bool treatment = TreatmentOn();
    s_look = treatment ? BlendLook(TreatmentLook(pVisualTreatmentPlat), display::VisualTreatment() / 100.0f)
                       : NeutralLook();
    const bool bloom = g_VideoOptions.OverBrightEnable != 0;
    const bool blur = g_VideoOptions.MotionBlurEnable && display::MotionBlur() > 0 &&
                      TheGameFlowManager_CurrentGameFlowState == kGameFlowRacing;
    const bool depthOfField = treatment && DepthOfFieldActive() && g_depthTexture && ShouldDrawDepth();

    DownsampleGrid(scene.texture, scene.width, scene.height, s_half, s_post.taps16);
    SetSource(s_half.texture, s_half.width, s_half.height);
    Run(s_post.downsample, s_quarter);
    CollectPostReadback();
    MeasureLuminance();
    Adapt(g_frameSeconds);
    if (bloom)
        Bloom();

    if (rain)
    {
        const PostTarget &copy = s_scene[1];
        Composite(copy.surface, scene, blur, depthOfField, bloom);
        device::StretchRect(copy.surface, nullptr, g_BackBufferSurface, nullptr, D3DTEXF_NONE);
        DrawOnScreenRain(copy.texture, g_BackBufferSurface);
    }
    else
    {
        Composite(g_BackBufferSurface, scene, blur, depthOfField, bloom);
    }
    AntiAlias();
    device::SetTarget(g_BackBufferSurface, nullptr, static_cast<int>(g_renderWidth), static_cast<int>(g_renderHeight));
    if (PixelationPending())
        Pixelate();
    if (CurrentViewMode == 2)
        DrawRvm();
}
} // namespace nfs::x360
