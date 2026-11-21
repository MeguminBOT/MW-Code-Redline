/**
 * A texture drawn over the whole current target (original: zPlatform 0x6cff40). The mirror uses it for its frame and
 * the alpha mask that rounds its corners. With Direct3D 9 this runs the original code.
 */
#include "game/gen/api.h"
#include "game/platform/D3DX.h"
#include "game/platform/Device.h"
#include "game/platform/Platform.h"
#include "legacy/link.h"
#include "engine/rendering/vulkan/renderer.h"

NFS_REPLACE(0x6cff40, sub_6CFF40)

/** The transform eSetTransform last uploaded; -1 makes the next call upload again. */
extern "C" int dword_8FAE9C;

extern "C" void __cdecl sub_6CFF40(void *texture, D3DCOLOR colour, int source, int destination);

namespace
{
const int kParamDiffuseMap = 0x22c;

struct TargetVertex
{
    float x, y, z;
    D3DCOLOR colour;
    float u, v;
};

void SetIdentity(bMatrix4 &m)
{
    m.v0 = bVector4(1.0f, 0.0f, 0.0f, 0.0f);
    m.v1 = bVector4(0.0f, 1.0f, 0.0f, 0.0f);
    m.v2 = bVector4(0.0f, 0.0f, 1.0f, 0.0f);
    m.v3 = bVector4(0.0f, 0.0f, 0.0f, 1.0f);
}
} // namespace

/**
 * Draws `texture` over the current target through the current effect, blended with `source` and `destination`, without
 * depth test. The target's view gets identity view and projection matrices for the draw and its own back afterwards.
 */
extern "C" void __cdecl sub_6CFF40(void *texture, D3DCOLOR colour, int source, int destination)
{
    const TargetVertex v[4] = {
        {-1.0f, -1.0f, 0.0f, colour, 0.0f, 1.0f},
        {-1.0f, 1.0f, 0.0f, colour, 0.0f, 0.0f},
        {1.0f, 1.0f, 0.0f, colour, 1.0f, 0.0f},
        {1.0f, -1.0f, 0.0f, colour, 1.0f, 1.0f},
    };
    bMatrix4 identity;
    SetIdentity(identity);
    eView &view = eViews[g_CurrentRenderTarget->ViewID];
    eViewPlatInfo *plat = view.PlatInfo;
    const bMatrix4 worldView = plat->WorldViewMatrix, viewScreen = plat->ViewScreenMatrix;
    plat->WorldViewMatrix = identity;
    plat->ViewScreenMatrix = identity;
    D3DXMatrixMultiply(&plat->WorldScreenMatrix, &plat->WorldViewMatrix, &plat->ViewScreenMatrix);
    dword_8FAE9C = -1;
    eSetTransform(&identity, view.ID);
    for (DWORD stage = 0; stage < 4; ++stage)
    {
        device::SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        device::SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    }
    eEffect *effect = g_CurrentEffect;
    effect->Effect->SetTexture(effect->Parameter(kParamDiffuseMap), static_cast<IDirect3DBaseTexture9 *>(texture));
    eSetAlphaBlend(1, source, destination);
    eSetAlphaTest(0, 0);
    eCommitBlendState();
    device::SetRenderState(D3DRS_ZENABLE, FALSE);
    device::SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    g_CurrentEffect->Effect->CommitChanges();
    g_CurrentEffect->Effect->BeginPass(0);
    device::DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, v, sizeof(TargetVertex));
    g_CurrentEffect->Effect->EndPass();
    device::SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    device::SetRenderState(D3DRS_ZENABLE, TRUE);
    eSetAlphaBlend(0, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA);
    plat->WorldViewMatrix = worldView;
    plat->ViewScreenMatrix = viewScreen;
    D3DXMatrixMultiply(&plat->WorldScreenMatrix, &plat->WorldViewMatrix, &plat->ViewScreenMatrix);
    eSetTransform(&identity, g_CurrentRenderTarget->ViewID);
}
