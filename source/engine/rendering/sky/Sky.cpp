/**
 * The Xbox 360 sky (X360 Stuff's Rendering::SkyStartHook 0x1002e780 and SetSkyDiffuseScale 0x1002e790 in the sky
 * effect's Start, zPlatform 0x6c60f0; Rendering::StuffSky 0x100307a0 in place of sub_6DE210; NEXTGENSKY.BIN from
 * DelayedInit): the next generation sky caps and clouds, their diffuse scale from the time of day, and the sky drawn
 * once at double brightness. NEXTGENSKY.BIN (the sky's solid) loads with the global data at boot
 * (engine/resources/GlobalLoading.cpp), as X360 Stuff does:
 * the track's NotifySkyLoader looks for the solid once when the track has loaded, and without it there is no sky.
 */
#include "game/gen/api.h"
#include "game/platform/Effect.h"
#include "game/platform/Platform.h"
#include "legacy/link.h"
#include "gameplay/world/timeofday.h"
#include "engine/rendering/x360/frame.h"
#include "engine/rendering/x360/shader.h"
#include "engine/resources/globalload.h"
#include "core/log.h"

NFS_REPLACE(0x6c60f0, SkyEffectStart)
NFS_REPLACE(0x6de210, sub_6DE210)

extern "C" TextureInfo *__SKY_NEXGEN_CAP_A_ptr;
extern "C" TextureInfo *__SKY_NEXGEN_CLOUDS_B_ptr;
extern "C" TextureInfo *__SKY_NEXGEN_CAP_B_ptr;
/** The clouds' scroll, advanced once per frame. */
extern "C" int dword_98854C;
extern "C" float dword_988548;
extern "C" char *TimeOfDayPointer;
extern "C" int TheGameFlowManager_CurrentGameFlowState;

extern "C" void __cdecl StuffSkyLayer(eView *view, int layer, float value);
extern "C" void __cdecl sub_6E2F50();
extern "C" void __cdecl sub_6DE210(eView *view, int, int layer);
void __fastcall SkyEffectStart(eEffect *self, void *);

namespace
{
const int kParamCapA = 0xe0c;
const int kParamCloudsB = 0xe34;
const int kParamCapB = 0xe5c;
const int kParamCloudScroll = 0x1b4;

float Clamp(float v, float high)
{
    return v < 0.0f ? 0.0f : v > high ? high : v;
}

void SetSkyTexture(eEffect *self, int parameter, TextureInfo *texture)
{
    if (texture)
        self->Effect->SetTexture(self->Parameter(parameter),
                                 static_cast<IDirect3DBaseTexture9 *>(texture->PlatInfo->Texture));
}

/** SKY_DIFFUSESCALE from the time of day's sky brightness (Rendering::SetSkyDiffuseScale). */
void SetSkyDiffuseScale()
{
    float t = *reinterpret_cast<const float *>(TimeOfDayPointer + 0x160);
    t = t > 0.12f ? t : 0.12f;
    const float squared = t * t;
    float scale[4] = {squared, Clamp(t * 3.3333333f, 1.0f), Clamp(squared + squared, 0.8f), Clamp(squared, 0.8f)};
    nfs::timeofday::SkyLayers(scale);
    nfs::x360::Shader &s = nfs::x360::ShaderFor(g_CurrentEffect);
    if (ID3DXEffect *e = s.Effect())
        e->SetVector(s.skyDiffuseScale, scale);
}
} // namespace

/** The sky effect's Start (zPlatform 0x6c60f0): the next generation sky textures, the cloud scroll and the Xbox 360
 * diffuse scale. */
void __fastcall SkyEffectStart(eEffect *self, void *)
{
    SetSkyTexture(self, kParamCapA, __SKY_NEXGEN_CAP_A_ptr);
    SetSkyTexture(self, kParamCloudsB, __SKY_NEXGEN_CLOUDS_B_ptr);
    SetSkyTexture(self, kParamCapB, __SKY_NEXGEN_CAP_B_ptr);
    if (dword_98854C != WorldTimeFrames)
    {
        dword_98854C = WorldTimeFrames;
        dword_988548 += *reinterpret_cast<const float *>(TimeOfDayPointer) * 0.5f * WorldTimeElapsed * 0.04f;
    }
    self->Effect->SetFloat(self->Parameter(kParamCloudScroll), dword_988548);
    SetSkyDiffuseScale();
}

/** The sky of a view (Rendering::StuffSky): none in the front end unless the garage shows it, else one layer at
 * double brightness, with the time of day's night sky over it as far as the hour has it (gameplay/world/timeofday.h). */
extern "C" void __cdecl sub_6DE210(eView *view, int, int layer)
{
    GarageMainScreen *garage = GarageMainScreen::GetInstance();
    if (TheGameFlowManager_CurrentGameFlowState == 3 &&
        (!garage || *reinterpret_cast<const int *>(reinterpret_cast<const char *>(garage) + 0x6c)))
        return;
    if (nfs::timeofday::NightSky() < 1.0f)
    {
        const float brightness = nfs::timeofday::SkyBrightness(2.0f);
        StuffSkyLayer(view, layer, brightness);
        nfs::x360::Shader &sky = nfs::x360::g_shaders[EFFECT_SKY];
        if (ID3DXEffect *e = sky.Effect())
            e->SetFloat(sky.standardBrightness, brightness);
        sub_6E2F50();
    }
    if (nfs::timeofday::DrawNightSky(view))
        sub_6E2F50();
}

namespace nfs::x360
{
void LoadNextGenSky()
{
    // The file comes with the Xbox 360 data; without it the sky keeps the game's own textures.
    nfs::globalload::LoadResourceFile("FRONTEND\\PLATFORMS\\NEXTGENSKY.BIN");
}
} // namespace nfs::x360
