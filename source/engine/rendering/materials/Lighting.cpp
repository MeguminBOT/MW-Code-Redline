/**
 * Lighting hooks of the Xbox 360 rendering: the world effect's Start (zPlatform 0x6d71f0 with X360 Stuff's
 * Rendering::FixSpecularCloudIntensitySource 0x1002e740: the specular cloud intensity is the time of day's own, not
 * dimmed by the camera's overcast), the sun's visibility test (Rendering::SunPlat__Render_Hook 0x1002e680), the car
 * flares without the textured headlights (X360 Stuff's NOP in RenderFlaresOnCar) and the light materials as loaded.
 */
#include "game/gen/api.h"
#include "game/platform/Effect.h"
#include "game/platform/Platform.h"
#include "legacy/link.h"
#include "engine/rendering/x360/frame.h"

NFS_REPLACE(0x6d71f0, WorldEffectStart)

extern "C" char *TimeOfDayPointer;
extern "C" void *dword_982A6C;
extern "C" float dword_8FADC0;
extern "C" int dword_982C08;
extern "C" void *dword_982CF0;
extern "C" void *dword_982CF8;
extern "C" int lpData_00901830;

extern "C" void __cdecl sub_6C6300(eEffect *effect);
extern "C" void __cdecl sub_6C6450(eEffect *effect);
int GetShadowWidth();
void __fastcall WorldEffectStart(eEffect *self, void *);

namespace
{
const int kParamSpecularCloud = 0xc7c;
const int kParamRoadReflection = 0xaec;
const int kParamFogBlend = 0xcf4;
const int kParamShadowMap = 0xc04;
const int kParamShadowTexel = 0xc2c;
const int kViewPlayer1 = 1;
} // namespace

/** The world effects' Start: specular cloud intensity, the road reflection, fog and lights, and the shadow map in the
 * player's view. */
void __fastcall WorldEffectStart(eEffect *self, void *)
{
    if (g_CurrentRenderTarget)
    {
        eEffect *current = g_CurrentEffect;
        current->Effect->SetVector(current->Parameter(kParamSpecularCloud), TimeOfDayPointer + 0x40);
    }
    eEffect *current = g_CurrentEffect;
    current->Effect->SetTexture(current->Parameter(kParamRoadReflection),
                                static_cast<IDirect3DBaseTexture9 *>(dword_982A6C));
    sub_6C6300(self);
    if (D3DXHANDLE h = self->Parameter(kParamFogBlend))
        self->Effect->SetFloat(h, dword_8FADC0);
    sub_6C6450(self);
    if (lpData_00901830 > 0 && g_CurrentRenderTarget->ViewID == kViewPlayer1)
    {
        void *shadow = dword_982C08 ? dword_982CF8 : dword_982CF0;
        self->Effect->SetTexture(self->Parameter(kParamShadowMap), static_cast<IDirect3DBaseTexture9 *>(shadow));
        self->Effect->SetFloat(self->Parameter(kParamShadowTexel), 1.0f / static_cast<float>(GetShadowWidth()));
    }
}

namespace nfs::x360
{
bool SunVisible(eView *view)
{
    const float *sun = reinterpret_cast<const float *>(TimeOfDayPointer + 0x170);
    const float *m = reinterpret_cast<const float *>(view->pCamera);
    const float w = sun[0] * m[3] + sun[1] * m[7] + sun[2] * m[11] + sun[3] * m[15];
    return w >= 0.0f;
}
} // namespace nfs::x360

/** The cars' light flares (RenderVehicleFlares, through X360 Stuff's FUN_100326a0): the third argument is always 0. */
void RenderVehicleFlares(eView *view, int reflection, int)
{
    VehicleRenderConn::RenderFlares(view, reflection, 0);
}

/** The textured headlight pools on the road (RenderFlaresOnCar's only call): X360 Stuff leaves them out. */
void CarRenderInfo::RenderTextureHeadlights(eView *, bMatrix4 *, unsigned int)
{
}

/** Light materials keep their loaded values (X360 Stuff skips this update in LoaderLights, its only caller): the Xbox
 * 360 light material constants are computed from them per draw. */
void eLightMaterialPlatInterface::UpdatePlatInfo()
{
}
