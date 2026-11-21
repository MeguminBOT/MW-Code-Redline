/**
 * Front-end pass of a frame (original: zPlatform 0x6e6e40). Resets render state, binds the back buffer, gives the
 * front-end view (EVIEW 0) identity matrices and the Xbox 360 front end constants, then draws the front end (menus and
 * HUD) through the FE effect; the mirror is drawn before, from the environment map (nfs::x360::DrawRvm). In Vulkan mode
 * its few device calls go to the renderer.
 *
 * Widescreen: the front end is laid out for a 640x480 screen and arrives here in clip space. On a wider screen the
 * 2D transform is narrowed so the layout keeps its proportions, centred, and eIsWidescreen() lets the HUD use its
 * widescreen layout in the extra width at the sides.
 */
#include "game/platform/Effect.h"
#include "core/config.h"
#include "engine/rendering/display/display.h"
#include "dev/profile.h"
#include "engine/rendering/vulkan/renderer.h"
#include "engine/rendering/x360/frame.h"

static_assert(sizeof(eView) == 0x70, "eView layout");

extern "C" short sub_6E6380(int, int);
extern "C" void sub_6E2F50();
extern "C" int dword_901794;
extern "C" int TheGameFlowManager_CurrentGameFlowState;

namespace
{
/** Value the copy-protection trigger decodes to when the check passes; otherwise the front end is not drawn. */
const short kProtectionCheckPassed = 0x188;

/** The rear-view mirror's view (the mirror image is rendered into g_RearViewMirrorTexture). */
const int EVIEW_RVM = 3;

const int kParamTechnique = 0x484;
const int kParamDiffuseMap = 0x22c;

void SetIdentity(bMatrix4 &m)
{
    m.v0 = bVector4(1.0f, 0.0f, 0.0f, 0.0f);
    m.v1 = bVector4(0.0f, 1.0f, 0.0f, 0.0f);
    m.v2 = bVector4(0.0f, 0.0f, 1.0f, 0.0f);
    m.v3 = bVector4(0.0f, 0.0f, 0.0f, 1.0f);
}

void SetRenderState(D3DRENDERSTATETYPE state, DWORD value)
{
    {
        nfs::vk::Renderer *r = nfs::vk::g_renderer;
        r->SetRenderState(state, value);
    }
}

void SetSamplerState(DWORD stage, D3DSAMPLERSTATETYPE state, DWORD value)
{
    {
        nfs::vk::Renderer *r = nfs::vk::g_renderer;
        r->SetSamplerState(stage, state, value);
    }
}

void BindBackBuffer()
{
    const DWORD width = static_cast<DWORD>(g_BackBufferTarget.Width);
    const DWORD height = static_cast<DWORD>(g_BackBufferTarget.Height);
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    r->SetRenderTarget(reinterpret_cast<nfs::vk::Texture *>(g_BackBufferTarget.Surface),
                       reinterpret_cast<nfs::vk::Texture *>(g_BackBufferTarget.DepthSurface));
    r->SetViewport({0, 0, width, height, 0.0f, 1.0f});
}

void BeginEffect(eEffect *effect)
{
    g_CurrentEffect = effect;
    effect->Start();
    {
        nfs::vk::Renderer *r = nfs::vk::g_renderer;
        r->SetVertexLayout(reinterpret_cast<nfs::vk::VertexLayout *>(effect->VertexDeclaration));
    }
    UINT passes;
    effect->Effect->Begin(&passes, 0);
}

void EndEffect(eEffect *effect)
{
    effect->End();
    effect->Effect->End();
}

} // namespace

void eRenderFrontEndPass(int skip_front_end)
{
    nfs::profile::Scope profile(skip_front_end ? nfs::profile::kFrontEndFirst : nfs::profile::kFrontEnd);
    eResetRenderStates();
    sub_6E2F50();
    BindBackBuffer();

    eEffect *fe = g_Effects[EFFECT_FE];
    g_CurrentRenderTarget = &g_BackBufferTarget;
    g_CurrentEffect = nullptr;
    fe->Effect->SetInt(fe->Parameter(kParamTechnique), 15);
    nfs::x360::SetFrontEndConstants();

    bMatrix4 identity;
    SetIdentity(identity);
    bMatrix4 screen = identity;
    screen.v0.x = nfs::display::FrontEndScaleX();

    BeginEffect(fe);
    eViewPlatInfo *fe_view = eViews[0].PlatInfo;
    fe_view->WorldViewMatrix = identity;
    D3DXMatrixMultiply(&fe_view->WorldScreenMatrix, &fe_view->WorldViewMatrix, &fe_view->ViewScreenMatrix);
    fe_view->ViewScreenMatrix = identity;
    D3DXMatrixMultiply(&fe_view->WorldScreenMatrix, &fe_view->WorldViewMatrix, &fe_view->ViewScreenMatrix);
    eSetTransform(&screen, 0);
    EndEffect(fe);

    eEffect *mask = g_Effects[EFFECT_FE_MASK];
    BeginEffect(mask);
    eSetTransform(&screen, 0);
    EndEffect(mask);

    BeginEffect(fe);
    SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    SetRenderState(D3DRS_ZENABLE, FALSE);
    // -set Debug.HideHud=1: no front end while racing (clean screenshots: the HUD is the front end's).
    static const bool hideHud = nfs::config::Int("Debug", "HideHud", 0) != 0;
    const bool racing = TheGameFlowManager_CurrentGameFlowState == 6;
    if (!skip_front_end && !(hideHud && racing) && sub_6E6380(dword_901794, dword_901794) == kProtectionCheckPassed)
    {
        // The mirror is drawn from the rear environment map face after the post processing (nfs::x360::DrawRvm).
        FEManager::Get()->Render();
    }
    EndEffect(fe);
    g_CurrentEffect = nullptr;
    SetRenderState(D3DRS_ZENABLE, TRUE);
    SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
}

/** Original: a stub returning false on PC (the Xbox 360 build used it for its widescreen layouts). */
bool eIsWidescreen()
{
    return nfs::display::FrontEndScaleX() < 0.9f;
}
