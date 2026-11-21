/**
 * The rear-view mirror and the raindrops on the screen (X360 Stuff's Rendering::DrawRVM 0x100340d0 and BuildRVMPoly
 * 0x10034540, eDoOnScreenRain 0x1003e960, XRainDropManager 0x1003e420-0x1003e920). The mirror shows the rear face of
 * the car's environment map through the car effect's rvm technique, a quad in the top right of the screen.
 */
#include "game/gen/api.h"
#include "game/platform/Device.h"
#include "game/platform/Effect.h"
#include "game/platform/Platform.h"
#include "gameplay/world/weather.h"
#include "engine/rendering/x360/frame.h"
#include "engine/rendering/x360/post.h"
#include "engine/rendering/x360/render.h"
#include "engine/rendering/x360/resources.h"
#include "engine/rendering/x360/shader.h"

extern "C" void *dword_93DAC0;
/** The mirror's own render target texture (without the cube map mirror). */
extern "C" void *__g_ID3Texture;
extern "C" char *pVisualTreatmentPlat;
extern "C" int TheGameFlowManager_CurrentGameFlowState;
extern "C" unsigned char dword_982B2A;

namespace nfs::x360
{
namespace
{
/** The car effect's vertex: position, normal, colour, UV. */
struct MirrorVertex
{
    float x, y, z;
    float normal[3];
    unsigned int colour;
    float u, v;
};

/** The front end's projection for FEScalingMode 1 (Xbox 360 scale; Rendering::FESetMatrix1): the mirror quad is in
 * front end units. */
void FrontEndMatrix(float *m)
{
    float scale = 1.0f;
    if (FEDatabase)
        scale = *reinterpret_cast<const float *>(*reinterpret_cast<char **>(reinterpret_cast<char *>(FEDatabase) + 0x10) +
                                                 0x28);
    const double aspect = static_cast<double>(g_renderWidth) / static_cast<double>(g_renderHeight);
    for (int i = 0; i < 16; ++i)
        m[i] = 0.0f;
    if (!g_wideUi)
    {
        const double rows = static_cast<double>(g_renderHeight);
        m[0] = static_cast<float>(rows * 1.2 * scale / static_cast<double>(g_renderWidth));
        m[5] = static_cast<float>(rows * 0.9333333333333333 * scale / rows);
    }
    else
    {
        const double s = scale * 0.92;
        m[5] = static_cast<float>(s);
        m[0] = static_cast<float>(static_cast<float>(s) * (1.0 / aspect * 1.3333333333333333));
    }
    m[10] = 1.0f;
    m[15] = 1.0f;
    m[12] = -1.0f / static_cast<float>(g_renderWidth);
    m[13] = 1.0f / static_cast<float>(g_renderWidth);
}

/** The mirror quad (Rendering::BuildRVMPoly with FEScalingMode 1): corners from the top left (x, y), width and height in
 * front end units, the normals the car effect lights it with. */
void BuildMirrorQuad(MirrorVertex *v, float x, float y, float width, float height)
{
    const float dx = 1.0f / static_cast<float>(g_renderWidth), dy = -1.0f / static_cast<float>(g_renderHeight);
    const float fit = static_cast<float>(g_renderHeight) * (g_wideUi ? 1.77777779f : 1.33333337f) /
                      static_cast<float>(g_renderWidth);
    const float x0 = x + x - 1.0f, y0 = y + y - 1.0f;
    const float y1 = height + height + y0, x1 = width + width + x0;
    static const float kNormals[4][3] = {
        {0.74645114f, -0.23637618f, -0.6220426f},
        {0.7577957f, 0.16418906f, -0.63149637f},
        {-0.74645114f, -0.23637618f, -0.6220426f},
        {-0.7577957f, 0.16418906f, -0.63149637f},
    };
    const float corners[4][4] = {{x0, y1, 0.0f, 0.0f}, {x0, y0, 0.0f, 1.0f}, {x1, y1, 1.0f, 0.0f}, {x1, y0, 1.0f, 1.0f}};
    for (int i = 0; i < 4; ++i)
    {
        v[i].x = corners[i][0] * fit - dx;
        v[i].y = corners[i][1] - dy;
        v[i].z = 1.0f;
        for (int c = 0; c < 3; ++c)
            v[i].normal[c] = kNormals[i][c];
        v[i].colour = 0xFFFFFFFF;
        v[i].u = corners[i][2];
        v[i].v = corners[i][3];
    }
}
} // namespace


namespace
{
/** One raindrop: a quad (position, colour, UV), its centre and radius, and its distortion and frame. */
struct RainDrop
{
    struct
    {
        float x, y, z;
        unsigned int colour;
        float u, v;
    } corners[4];
    float centre[4];
    float shape[4];
};

static_assert(sizeof(RainDrop) == 0x80, "RainDrop layout");

/** The drops each player view shows this frame (XRainDropManager's lists). */
struct RainDropList
{
    RainDrop drops[128];
    unsigned int count;
};

RainDropList s_rain[2];

/** The rain object's drops: count, then x, y, distortion over size, size and frame. */
struct ScreenDrop
{
    float x, y, distortion, size, scale, unknown;
    int frame;
};

/** The rain (nfs::weather::Rain) below which the screen has no drops. */
const float kNoRain = 0.02f;

/** The view's raindrops as quads (eDoOnScreenRain). */
void CollectRainDrops(eView &view)
{
    const int id = view.ID;
    if (id != 1 && id != 2)
        return;
    RainDropList &list = s_rain[id - 1];
    list.count = 0;
    char *rain = reinterpret_cast<char *const *>(view.unknown_64)[1];
    if (!rain)
        return;
    // No rain, no drops on the screen: the game's list keeps the drops it has while nothing makes more, which stayed
    // on the screen after the rain had stopped. Emptied, it is empty when the rain returns.
    if (!nfs::weather::RainShown() || nfs::weather::Rain() < kNoRain)
        *reinterpret_cast<int *>(rain) = 0;
    const int count = *reinterpret_cast<const int *>(rain);
    const ScreenDrop *drops = reinterpret_cast<const ScreenDrop *>(rain + 4);
    float width = 640.0f, height = 480.0f;
    if (const eRenderTarget *target = view.RenderTarget)
    {
        width = static_cast<float>(target->Width);
        height = static_cast<float>(target->Height);
    }
    for (int i = 0; i < count && list.count < 128; ++i)
    {
        const ScreenDrop &d = drops[i];
        RainDrop &q = list.drops[list.count++];
        const float size = d.scale * 0.1f;
        q.shape[0] = d.distortion / d.size * 3.0f;
        q.shape[1] = static_cast<float>(d.frame) * 0.25f;
        const float x0 = d.x + d.x - 1.0f;
        const float row = 1.0f - height / static_cast<float>(g_renderHeight) * d.y;
        const float y0 = row + row - 1.0f;
        const float x1 = size * height / width + x0, y1 = y0 + size;
        q.centre[0] = d.x - size * 0.5f;
        q.centre[1] = size * 0.5f + d.y;
        q.centre[2] = size * 0.5f;
        q.centre[3] = size * -0.5f;
        const float corners[4][4] = {{x0, y0, 0.0f, 1.0f}, {x1, y0, 1.0f, 1.0f}, {x0, y1, 0.0f, 0.0f}, {x1, y1, 1.0f, 0.0f}};
        for (int c = 0; c < 4; ++c)
        {
            q.corners[c].x = corners[c][0];
            q.corners[c].y = corners[c][1];
            q.corners[c].u = corners[c][2];
            q.corners[c].v = corners[c][3];
        }
    }
}

void *TextureNamed(const char *name)
{
    TextureInfo *info = GetTextureInfo(bStringHash(name), 1, 0);
    return info && info->PlatInfo ? info->PlatInfo->Texture : nullptr;
}

/** The view's drops over the frame, each distorting what is behind it (XRainDropManager::DrawBatch). */
void DrawRainDrops(eView &view, void *frame, void *target)
{
    const int id = view.ID;
    if (id != 1 && id != 2)
        return;
    RainDropList &list = s_rain[id - 1];
    Shader &s = g_shaders[EFFECT_PARTICLES];
    ID3DXEffect *e = s.Effect();
    if (!e)
        return;
    device::SetVertexDeclaration(g_Effects[EFFECT_PARTICLES]);
    e->SetTechnique(e->GetTechniqueByName("onscreen_distort"));
    e->Begin(nullptr, 0);
    device::SetColorTarget(target);
    device::SetDepthTarget(nullptr);
    device::SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device::SetRenderState(D3DRS_ZENABLE, FALSE);
    e->SetTexture(s.miscMapTexture[0], static_cast<IDirect3DBaseTexture9 *>(frame));
    e->SetTexture(s.diffuseMap, static_cast<IDirect3DBaseTexture9 *>(TextureNamed("RAINDROPPC")));
    e->SetTexture(s.opacityMapTexture, static_cast<IDirect3DBaseTexture9 *>(TextureNamed("ONSCREENRAINDROPALPHA")));
    device::SetViewport(0, id == 2 ? g_renderHeight >> 1 : 0, g_renderWidth, g_renderHeight);
    for (unsigned int i = 0; i < list.count; ++i)
    {
        RainDrop &d = list.drops[i];
        e->SetVector(s.baseAlphaRef, d.shape);
        e->SetVector(s.localCenter, d.centre);
        e->CommitChanges();
        e->BeginPass(0);
        device::DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, d.corners, sizeof(d.corners[0]));
        e->EndPass();
    }
    e->End();
    device::SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    device::SetRenderState(D3DRS_ZENABLE, TRUE);
    list.count = 0;
}
} // namespace

bool CollectOnScreenRain()
{
    CollectRainDrops(eViews[1]);
    CollectRainDrops(eViews[2]);
    return TheGameFlowManager_CurrentGameFlowState == 6 && dword_982B2A && (s_rain[0].count || s_rain[1].count);
}

void DrawOnScreenRain(void *frame, void *target)
{
    DrawRainDrops(eViews[1], frame, target);
    DrawRainDrops(eViews[2], frame, target);
}

void DrawRvm()
{
    Shader &car = g_shaders[EFFECT_CAR];
    ID3DXEffect *e = car.Effect();
    eEffect *effect = g_Effects[EFFECT_CAR];
    if (!e || !pVisualTreatmentPlat)
        return;
    device::SetVertexDeclaration(effect);
    e->SetTechnique(options::kCubemapRVM ? car.rvm : car.rvmNoCubemap);
    device::SetColorTarget(dword_93DAC0);
    device::SetDepthTarget(nullptr);
    device::SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device::SetRenderState(D3DRS_ZENABLE, FALSE);
    device::SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    ApplyLook(car);
    TextureInfo *mask = GetTextureInfo(bStringHash("RVM_MASK"), 1, 0);
    void *maskTexture = mask->PlatInfo->Texture;
    if (options::kCubemapRVM)
    {
        e->SetTexture(car.diffuseMap, static_cast<IDirect3DBaseTexture9 *>(maskTexture));
    }
    else
    {
        e->SetTexture(car.diffuseMap, static_cast<IDirect3DBaseTexture9 *>(__g_ID3Texture));
        e->SetTexture(car.miscMapTexture[0], static_cast<IDirect3DBaseTexture9 *>(maskTexture));
    }
    float projection[16];
    FrontEndMatrix(projection);
    e->SetMatrix(car.worldViewProjection, projection);
    e->SetFloat(car.rvmWorldBrightness, 1.4f);
    device::SetVertexDeclaration(effect);
    MirrorVertex quad[4];
    if (g_wideUi)
        BuildMirrorQuad(quad, 0.638f, 0.785f, -0.275f, 0.128f);
    else
        BuildMirrorQuad(quad, 0.68f, 0.79f, -0.358f, 0.135f);
    e->Begin(nullptr, 0);
    e->BeginPass(0);
    device::DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(MirrorVertex));
    e->EndPass();
    e->End();
    device::SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    device::SetRenderState(D3DRS_ZENABLE, TRUE);
}
} // namespace nfs::x360
