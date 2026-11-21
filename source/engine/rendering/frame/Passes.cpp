/**
 * The Xbox 360 effects' per-pass setup (X360 Stuff's Rendering::DoLowLOD 0x10036140, DoShadowMapLOD 0x10032470,
 * DoMainViewLOD 0x10035bd0 and PreRender 0x10034970): every effect gets the technique and shadow map constants of the
 * pass before its draws, since the render list no longer chooses techniques.
 */
#include "game/gen/api.h"
#include "game/platform/Device.h"
#include "game/platform/Effect.h"
#include "game/platform/Platform.h"
#include "engine/rendering/shadows/shadow.h"
#include "legacy/link.h"
#include "gameplay/world/timeofday.h"
#include "engine/rendering/x360/render.h"
#include "engine/rendering/x360/resources.h"
#include "engine/rendering/x360/shader.h"

extern "C" char *TimeOfDayPointer;
extern "C" int TheGameFlowManager_CurrentGameFlowState;
extern "C" int RenderingViewMode;
extern "C" int CurrentViewMode;
extern "C" float WorldTimeSeconds;
extern "C" int g_X360EffectsEnable;
extern "C" int __multisamplequality;
/** The shadow map's colour texture and the road reflection texture. */
extern "C" void *dword_982CEC;
extern "C" void *dword_982CF8;
extern "C" void *dword_982A6C;
/** A time of day override a cutscene sets (-1: none). */
extern "C" float flt_905CE8;

extern "C" void __cdecl sub_6C83D0();
extern "C" void __cdecl sub_6E2F50();

namespace nfs::x360
{
namespace
{
const int kGameFlowFrontEnd = 3;
const int kGameFlowRacing = 6;
const unsigned int kAttribBrightness = 0x483b6e25;
const unsigned int kAttribContrast = 0x42ff3188;
const unsigned int kAttribSaturation = 0xe0985f58;

float Float(const char *object, int offset)
{
    return *reinterpret_cast<const float *>(object + offset);
}

char *Views()
{
    return reinterpret_cast<char *>(eViews);
}

/** eViews[view] + offset as the plugin reads it (from eViews[0]). */
template <typename T> T &ViewField(int offset)
{
    return *reinterpret_cast<T *>(Views() + offset);
}

/** The camera mover of the player's view (lighting level at 0x36a0, rain at 0x28c). */
const char *PlayerMover()
{
    return ViewField<const char *>(0xd8);
}

/** The tree effect's brightness vector: the visual look attributes, or the plugin's defaults. */
void SetTreeBrightness(float contrast)
{
    float v[4] = {0.13f, contrast, -1.0f, 0.24f};
    if (void *look = g_lookAttributes)
    {
        v[0] = *AttributeFloat(look, kAttribBrightness);
        v[1] = *AttributeFloat(look, kAttribContrast);
        v[2] = *reinterpret_cast<const float *>(AttributeLayout(look) + 0x6c);
        v[3] = *AttributeFloat(look, kAttribSaturation);
    }
    const float light = nfs::timeofday::TreeLight();
    v[0] *= light;
    v[1] *= light;
    Shader &tree = g_shaders[EFFECT_TREE];
    if (ID3DXEffect *e = tree.Effect())
        e->SetVector(tree.standardBrightness, v);
}

bool CarShadowsWanted()
{
    const int state = TheGameFlowManager_CurrentGameFlowState;
    return state == kGameFlowRacing || state == 4 || state == 5;
}

/** Shadow map constants of a depth-only pass: alpha test from the diffuse texture, no shadow lookup. */
void SetDepthPassConstants(Shader &s, ID3DXEffect *e, float bias)
{
    if (s.shadowMapAlphaMin)
        e->SetFloat(s.shadowMapAlphaMin, 0.5f);
    if (s.shadowMapAlphaEnabled)
        e->SetBool(s.shadowMapAlphaEnabled, FALSE);
    if (s.shadowMapBias)
        e->SetFloat(s.shadowMapBias, bias);
    if (s.shadowMapEnabled)
        e->SetInt(s.shadowMapEnabled, 0);
}
} // namespace

void DoLowLod()
{
    g_passMode = kPassLowLod;
    g_depthPassDone = false;
    device::SetRenderState(D3DRS_COLORWRITEENABLE, 7);
    Shader &car = g_shaders[EFFECT_CAR];
    if (ID3DXEffect *e = car.Effect())
        e->SetInt(car.colorWriteMode, 0xF);
    Shader &carNormalMap = g_shaders[EFFECT_CAR_DEPTH];
    if (ID3DXEffect *e = carNormalMap.Effect())
        e->SetInt(carNormalMap.colorWriteMode, 0xF);
    for (Shader &s : g_shaders)
    {
        ID3DXEffect *e = s.Effect();
        if (!e)
            continue;
        e->SetTechnique(s.lowLod ? s.lowLod : s.mainTechnique);
        if (s.standardBrightness)
            e->SetFloat(s.standardBrightness, 1.5f);
        if (s.shadowMapEnabled)
            e->SetInt(s.shadowMapEnabled, 0);
    }
    SetTreeBrightness(1.2f);
    sub_6C83D0();
}

void DoShadowMapLod()
{
    g_passMode = kPassDepth;
    if (!dword_982CF8)
    {
        for (Shader &s : g_shaders)
            if (ID3DXEffect *e = s.Effect())
                if (s.shadowMapEnabled)
                    e->SetInt(s.shadowMapEnabled, 0);
    }
    else
    {
        UINT width, height;
        device::TextureSize(dword_982CF8, width, height);
        for (Shader &s : g_shaders)
        {
            ID3DXEffect *e = s.Effect();
            if (!e)
                continue;
            if (s.renderLightAlphaTex)
                e->SetTechnique(s.renderLightAlphaTex);
            else if (D3DXHANDLE t = s.zPrePass ? s.zPrePass : s.lowLod ? s.lowLod : s.mainTechnique)
                e->SetTechnique(t);
            SetDepthPassConstants(s, e, -0.0f);
            if (s.shadowMapScaleX)
                e->SetFloat(s.shadowMapScaleX, 1.0f / static_cast<float>(width));
            if (s.shadowMapScaleY)
                e->SetFloat(s.shadowMapScaleY, 1.0f / static_cast<float>(height));
        }
    }
    Shader &bone = g_shaders[EFFECT_WORLD_BONE];
    if (ID3DXEffect *e = bone.Effect())
    {
        D3DXHANDLE skinned = e->GetTechniqueByName("RenderLightSkinned");
        e->SetTechnique(skinned ? skinned : bone.mainTechnique);
    }
    sub_6E2F50();
}

void DoMainViewLod()
{
    device::SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    Shader &car = g_shaders[EFFECT_CAR];
    Shader &carNormalMap = g_shaders[EFFECT_CAR_DEPTH];
    if (!g_depthPassDone && DepthPassActive())
    {
        // The depth pass before the main view: depth-only techniques, alpha tested.
        g_passMode = kPassDepth;
        for (Shader &s : g_shaders)
        {
            ID3DXEffect *e = s.Effect();
            if (!e)
                continue;
            if (s.renderLightAlphaTex)
                e->SetTechnique(s.renderLightAlphaTex);
            else if (s.zPrePass)
                e->SetTechnique(s.zPrePass);
            SetDepthPassConstants(s, e, -0.01f);
        }
        // X360 Stuff also gives the normal-mapped car effect the car effect's handle, which Direct3D rejects.
        if (ID3DXEffect *e = car.Effect())
            e->SetTechnique(car.renderLight);
        carNormalMap.Effect();
        return;
    }
    // The rebuild's own receivers read the sun shadows from the scene set; the others go without while it is on.
    const BOOL shadows = dword_982CF8 != nullptr && !nfs::shadow::Active();
    for (Shader &s : g_shaders)
        if (ID3DXEffect *e = s.Effect())
            if (s.shadowMapEnabled)
                e->SetInt(s.shadowMapEnabled, shadows);
    if (shadows)
    {
        if (ID3DXEffect *e = car.Effect())
            e->SetTechnique(car.renderLight);
        if (ID3DXEffect *e = carNormalMap.Effect())
            e->SetTechnique(carNormalMap.renderLight);
        if (ID3DXEffect *e = car.Effect())
            e->SetInt(car.colorWriteMode, 1);
        if (ID3DXEffect *e = carNormalMap.Effect())
            e->SetInt(carNormalMap.colorWriteMode, 1);
    }
    g_passMode = kPassMain;
    for (eEffect *effect : g_Effects)
        if (effect && effect->Effect)
            effect->Effect->SetTechnique(effect->Effect->GetTechnique(0));
    const char *mover = PlayerMover();
    Shader &reflect = g_shaders[EFFECT_WORLD_REFLECT];
    if (ID3DXEffect *e = reflect.Effect())
    {
        e->SetFloat(reflect.surfaceReflection, Float(mover, 0x36a0));
        e->SetFloat(reflect.rainIntensity, Float(mover, 0x28c));
        e->SetTechnique(Float(mover, 0x36a0) > 0.001f ? reflect.wetRoad : reflect.dryRoad);
    }
    Shader &normalMap = g_shaders[EFFECT_WORLD_NORMAL_MAP];
    if (ID3DXEffect *e = normalMap.Effect())
        e->SetTechnique(normalMap.worldNormalMap);
    Shader &world = g_shaders[EFFECT_WORLD];
    if (ID3DXEffect *e = world.Effect())
        e->SetTechnique(world.world);
    Shader &tree = g_shaders[EFFECT_TREE];
    if (ID3DXEffect *e = tree.Effect())
    {
        struct
        {
            const char *name;
            UINT passes, annotations;
        } desc = {};
        e->GetTechniqueDesc(tree.mainTechnique, &desc);
        if (*tree.slot)
            (*tree.slot)->PassCount = static_cast<int>(desc.passes);
    }
    Shader &bone = g_shaders[EFFECT_WORLD_BONE];
    if (ID3DXEffect *e = bone.Effect())
        e->SetTechnique(bone.mainTechnique);
}

void PreRender()
{
    InitAttributes();
    const eRenderTarget *target = eViews[1].RenderTarget;
    const DWORD width = static_cast<DWORD>(target->Width), height = static_cast<DWORD>(target->Height);
    const bool secondPlayer = ViewField<int>(0x74) == 2;
    const DWORD x = RenderingViewMode == 4 && secondPlayer ? g_renderWidth >> 1 : 0;
    DWORD y = RenderingViewMode == 3 && secondPlayer ? g_renderHeight >> 1 : 0;
    device::SetViewport(x, y, width, height);
    const float rows = static_cast<float>(g_renderHeight) < 720.0f ? static_cast<float>(g_renderHeight) : 720.0f;
    g_screenUnits[0] = rows * g_renderAspect;
    g_screenUnits[1] = rows * (static_cast<float>(height) / static_cast<float>(g_renderHeight));
    g_time = WorldTimeSeconds;
    if (options::kResetOvercastOverrideAfterNIS && TheGameFlowManager_CurrentGameFlowState == kGameFlowFrontEnd)
    {
        if (flt_905CE8 >= 0.0f)
        {
            *reinterpret_cast<float *>(ViewField<char *>(0xd8) + 0x290) = flt_905CE8;
            *reinterpret_cast<float *>(ViewField<char *>(0x148) + 0x290) = flt_905CE8;
            flt_905CE8 = -1.0f;
        }
        float *tod = reinterpret_cast<float *>(TimeOfDayPointer);
        tod[3] = 3.28f;
        tod[4] = 0.5f;
        tod[0] = 1.0f;
    }
    // X360 Stuff gives each new time of day lighting its car shadow colour (Rendering::InitExtraTODStuff).
    static char *initialised;
    if (TimeOfDayPointer != initialised)
    {
        static const float kCarShadow[4] = {0.4f, 0.4f, 0.45f, 1.0f};
        for (int i = 0; i < 4; ++i)
            reinterpret_cast<float *>(TimeOfDayPointer + 0x50)[i] = kCarShadow[i];
        initialised = TimeOfDayPointer;
    }
    SetGlobalVector("CARSHADOWCOLOUR", reinterpret_cast<const float *>(TimeOfDayPointer + 0x50));
    if (!g_depthPassDone && ShouldDrawDepth())
    {
        // The main view's depth pass draws depth alone (Direct3D 9 drew its alpha into a colour target too).
        if (__multisamplequality)
            device::SetColorTarget(nullptr);
        device::SetDepthTarget(g_depthSurface);
    }
    else
    {
        device::Clear(D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    }
    DoMainViewLod();
    // The animated rain splashes on the wet road, the sea's waves and the reflection it shows.
    const int frame = static_cast<int>(Fmod(static_cast<double>(g_time * 20.0f), 16.0)) & 15;
    Shader &reflect = g_shaders[EFFECT_WORLD_REFLECT];
    if (ID3DXEffect *e = reflect.Effect())
        e->SetTexture(reflect.rainSplashTex, static_cast<IDirect3DBaseTexture9 *>(g_rainSplash[frame]));
    Shader &sea = g_shaders[EFFECT_WORLD_DEPTH];
    if (ID3DXEffect *e = sea.Effect())
    {
        TextureInfo *waves = GetTextureInfo(bStringHash("SFX_WAVES_NORM"), 1, 0);
        e->SetTexture(sea.waveNormalMapTex, static_cast<IDirect3DBaseTexture9 *>(waves->PlatInfo->Texture));
        e->SetTexture(sea.reflectedTex, static_cast<IDirect3DBaseTexture9 *>(dword_982A6C));
        const float offset[4] = {g_time * 0.05f, g_time * -0.025f, 0.0f, 0.0f};
        e->SetVector(sea.textureAnimOffset, offset);
    }
    SetGlobalFloat("STANDARD_BRIGHTNESS", 2.0f);
    Shader &bone = g_shaders[EFFECT_WORLD_BONE];
    if (ID3DXEffect *e = bone.Effect())
    {
        e->SetFloat(bone.standardBrightness, 1.0f);
        e->SetTechnique(bone.mainTechnique);
    }
    if (CurrentViewMode == 3 || options::kCarSelfShadow)
    {
        void *previous = device::ColorTarget();
        device::SetColorTarget(dword_982CEC);
        device::Clear(D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0);
        device::SetColorTarget(previous);
        y = secondPlayer ? g_renderHeight >> 1 : 0;
        device::SetViewport(0, y, width, height);
    }
    SetTreeBrightness(1.2f);
    Shader &car = g_shaders[EFFECT_CAR];
    if (ID3DXEffect *e = car.Effect())
        e->SetBool(car.shadowMapCarShadowEnabled, CarShadowsWanted());
    Shader &carNormalMap = g_shaders[EFFECT_CAR_DEPTH];
    if (ID3DXEffect *e = carNormalMap.Effect())
        e->SetBool(carNormalMap.shadowMapCarShadowEnabled, CarShadowsWanted());
    device::SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    g_X360EffectsEnable = 0;
}
} // namespace nfs::x360
