/**
 * The front end's 2D quads (original: zPlatform eViewPlatInterface::Render 0x6d1260 and its masked variant 0x6d1930,
 * which eViewPlatInterface::FERender calls for every image, string and movie frame of the front end and HUD), and the
 * quads placed in the world with a transform (eViewPlatInterfaceImpl::Render 0x6d1a40). With Direct3D 9 these run the
 * original code.
 */
#include "game/gen/api.h"
#include "game/platform/Effect.h"
#include "game/platform/Platform.h"
#include "legacy/link.h"
#include "engine/rendering/vulkan/renderer.h"
#include "engine/rendering/worldquads.h"
#include "engine/rendering/x360/frame.h"
#include "engine/rendering/x360/shader.h"

NFS_REPLACE(0x6d1930, RenderMaskedPoly)
NFS_REPLACE(0x6d1a40, RenderPolyTransformed)

/** The mask texture of the quad being drawn (set around the masked variant's draw). */
extern "C" TextureInfo *dword_982BC8;
/** A texture bound to the DIFFUSETEX parameter of every quad when set (nothing sets it on PC). */
extern "C" IDirect3DBaseTexture9 *dword_982BC4;
/** Cleared, the quads are still set up and counted but not drawn. */
extern "C" unsigned char byte_8F9358;
extern "C" int dword_982CA0;

extern "C" void __cdecl sub_6C6CC0(TextureInfo *texture, unsigned int flags);
extern "C" void __cdecl sub_6C6ED0();
void RenderMaskedPoly(ePoly *poly, TextureInfo *texture, TextureInfo *mask, int mode);
void RenderPolyTransformed(ePoly *poly, TextureInfo *texture, bMatrix4 *transform, int unused);

namespace
{
/** Mode of movie frames: projected texture coordinates (u, v, mask u as z, mask v as w), not nudged by half a texel. */
const int kModeProjected = 0x61;
const int kParamDiffuseTex = 0x2a4;
const int kParamMaskMap = 0x4ac;
const int kAlphaRef = 0x5f;
/** Textures that mark the HUD's masks into the stencil buffer with the Matrox workarounds on. */
const unsigned int kStencilMarkTextures[2] = {0x40CDC515, 0x1149C7DA};
/** How quads of the 'r' shape show through the stencil marks where the card has a blend factor. */
const DWORD kStencilBlendFactor = 0xFFB0B0B0;

struct QuadVertex
{
    float x, y, z;
    D3DCOLOR color;
    float u, v;
    float maskU, maskV;
};

void NudgeHalfTexel(QuadVertex *v, float QuadVertex::*u, float QuadVertex::*vv, const TextureInfo *texture)
{
    const float du = 0.5f / static_cast<float>(texture->Width), dv = 0.5f / static_cast<float>(texture->Height);
    v[0].*u += du;
    v[0].*vv += dv;
    v[1].*u -= du;
    v[1].*vv += dv;
    v[2].*u -= du;
    v[2].*vv -= dv;
    v[3].*u += du;
    v[3].*vv -= dv;
}

void BeginEffect(nfs::vk::Renderer *r, eEffect *effect)
{
    g_CurrentEffect = effect;
    effect->Start();
    r->SetVertexLayout(reinterpret_cast<nfs::vk::VertexLayout *>(effect->VertexDeclaration));
    UINT passes;
    effect->Effect->Begin(&passes, 0);
}

void EndEffect(eEffect *effect)
{
    effect->End();
    effect->Effect->End();
}

/** A masked quad left its mask in the front end effects' mask parameter, which the Xbox 360 effects share through the
 * effect pool; the next plain quad puts BASEPOLY back (X360 Stuff's ePolyRender does it for every plain quad). */
bool s_maskInUse;

void RestoreFrontEndMask()
{
    if (dword_982BC8)
    {
        s_maskInUse = true;
        return;
    }
    if (!s_maskInUse || !g_CurrentEffect)
        return;
    s_maskInUse = false;
    nfs::x360::Shader &s = nfs::x360::ShaderFor(g_CurrentEffect);
    void *base = nfs::x360::FrontEndBasePoly();
    if (ID3DXEffect *e = s.Effect())
        if (s.feMaskMap && base)
            e->SetTexture(s.feMaskMap, static_cast<IDirect3DBaseTexture9 *>(base));
}
} // namespace

/**
 * Draws one front-end quad through the current effect: screen positions (640x480) to clip space, colours to
 * D3DCOLOR, texture coordinates moved half a texel inwards, then the texture's state (sub_6C6CC0) and a triangle fan.
 */
void eViewPlatInterface::Render(ePoly *poly, TextureInfo *texture, int mode)
{
    RestoreFrontEndMask();
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    bool stencilMark = false, stencilTest = false;
    if (g_MatroxWorkarounds)
    {
        stencilMark =
            texture && (texture->NameHash == kStencilMarkTextures[0] || texture->NameHash == kStencilMarkTextures[1]);
        stencilTest = poly->Shape == 'r' || poly->Shape == 's';
    }
    QuadVertex v[4];
    const bool depth = (poly->Flags & 2) != 0;
    for (int i = 0; i < 4; ++i)
    {
        v[i].x = poly->Vertices[i][0] * 0.003125f - 1.0f;
        v[i].y = -(poly->Vertices[i][1] * 0.004166667f - 1.0f);
        v[i].z = depth ? poly->Vertices[i][2] : 1.0f;
        v[i].color = D3DCOLOR_ARGB(poly->Colours[i][3], poly->Colours[i][0], poly->Colours[i][1], poly->Colours[i][2]);
        v[i].u = poly->UVs[i][0];
        v[i].v = poly->UVs[i][1];
        v[i].maskU = poly->UVsMask[i][0];
        v[i].maskV = poly->UVsMask[i][1];
    }
    if (texture)
    {
        if (mode != kModeProjected)
        {
            NudgeHalfTexel(v, &QuadVertex::u, &QuadVertex::v, texture);
            if (dword_982BC8)
                NudgeHalfTexel(v, &QuadVertex::maskU, &QuadVertex::maskV, dword_982BC8);
        }
        sub_6C6CC0(texture, 0);
    }
    if (dword_982BC4)
        g_CurrentEffect->Effect->SetTexture(g_CurrentEffect->Parameter(kParamDiffuseTex), dword_982BC4);
    // The original also sets D3DTTFF_PROJECTED on stage 0 and an identity texture transform; the projection is not
    // carried to the Vulkan shaders yet (movies, docs/VULKAN.md).
    if (mode == kModeProjected)
        r->SetFVF(g_CurrentEffect->HasShaders ? D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE4(0)
                                              : D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE3(0));
    dword_982CA0 = 0;
    if (stencilMark)
    {
        r->SetRenderState(D3DRS_STENCILENABLE, TRUE);
        r->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
        r->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_INCR);
        eSetAlphaTest(1, kAlphaRef);
        eCommitBlendState();
    }
    if (stencilTest)
    {
        r->SetRenderState(D3DRS_STENCILENABLE, TRUE);
        r->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_LESS);
        eSetAlphaTest(0, kAlphaRef);
        if (poly->Shape != 'r')
        {
            eSetAlphaBlend(1, D3DBLEND_SRCALPHA, D3DBLEND_ONE);
        }
        else if (g_DeviceCaps.SrcBlendCaps & D3DPBLENDCAPS_BLENDFACTOR)
        {
            r->SetRenderState(D3DRS_BLENDFACTOR, kStencilBlendFactor);
            eSetAlphaBlend(1, D3DBLEND_BLENDFACTOR, D3DBLEND_INVBLENDFACTOR);
        }
        else
        {
            eSetAlphaBlend(1, D3DBLEND_ONE, D3DBLEND_ZERO);
        }
        eCommitBlendState();
    }
    if (byte_8F9358)
    {
        g_CurrentEffect->Effect->CommitChanges();
        g_CurrentEffect->Effect->BeginPass(0);
        r->DrawUP(D3DPT_TRIANGLEFAN, 2, v, sizeof(QuadVertex));
        g_CurrentEffect->Effect->EndPass();
    }
    if (stencilMark || stencilTest)
        r->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    sub_6C6ED0();
}

/** A quad with a second, mask texture: drawn alpha blended through the FE mask effect, which replaces the current
 * effect for the draw. */
void RenderMaskedPoly(ePoly *poly, TextureInfo *texture, TextureInfo *mask, int mode)
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    eEffect *previous = g_CurrentEffect;
    dword_982BC8 = mask;
    if (previous)
        EndEffect(previous);
    eEffect *masked = g_Effects[EFFECT_FE_MASK];
    BeginEffect(r, masked);
    masked->Effect->SetTexture(masked->Parameter(kParamMaskMap),
                               static_cast<IDirect3DBaseTexture9 *>(mask->PlatInfo->Texture));
    texture->AlphaBlendType = 1;
    eViewPlatInterface::Render(poly, texture, mode);
    EndEffect(g_Effects[EFFECT_FE_MASK]);
    g_CurrentEffect = nullptr;
    if (previous)
        BeginEffect(r, previous);
    dword_982BC8 = nullptr;
}

/** A quad in world (or any) space: its positions go through `transform` (identity when null) and the view's matrices,
 * with the texture's state and a triangle fan as for the front end's quads. */
void RenderPolyTransformed(ePoly *poly, TextureInfo *texture, bMatrix4 *transform, int unused)
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    if (!transform)
        transform = &eMathIdentityMatrix;
    QuadVertex v[4];
    for (int i = 0; i < 4; ++i)
    {
        v[i].x = poly->Vertices[i][0];
        v[i].y = poly->Vertices[i][1];
        v[i].z = poly->Vertices[i][2];
        v[i].color = D3DCOLOR_ARGB(poly->Colours[i][3], poly->Colours[i][0], poly->Colours[i][1], poly->Colours[i][2]);
        v[i].u = poly->UVs[i][0];
        v[i].v = poly->UVs[i][1];
        // The original leaves these unset.
        v[i].maskU = 0.0f;
        v[i].maskV = 0.0f;
    }
    if (texture)
        sub_6C6CC0(texture, 0);
    eSetTransform(transform, g_CurrentRenderTarget->ViewID);
    if (dword_982BC4)
        g_CurrentEffect->Effect->SetTexture(g_CurrentEffect->Parameter(kParamDiffuseTex), dword_982BC4);
    if (byte_8F9358)
    {
        g_CurrentEffect->Effect->CommitChanges();
        dword_982CA0 = 0;
        g_CurrentEffect->Effect->BeginPass(0);
        r->DrawUP(D3DPT_TRIANGLEFAN, 2, v, sizeof(QuadVertex));
        g_CurrentEffect->Effect->EndPass();
    }
    sub_6C6ED0();
}

namespace nfs::worldquads
{
static_assert(sizeof(Vertex) == sizeof(QuadVertex), "world quad vertex");

void Draw(const Vertex *corners, int quads, TextureInfo *texture)
{
    if (quads <= 0 || !g_CurrentEffect)
        return;
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    if (!r)
    {
        // Direct3D 9: one quad at a time through the original.
        for (int q = 0; q < quads; ++q)
        {
            ePoly poly;
            for (int i = 0; i < 4; ++i)
            {
                const Vertex &c = corners[q * 4 + i];
                poly.Vertices[i][0] = c.x;
                poly.Vertices[i][1] = c.y;
                poly.Vertices[i][2] = c.z;
                poly.Vertices[i][3] = 1.0f;
                poly.UVs[i][0] = c.u;
                poly.UVs[i][1] = c.v;
                poly.Colours[i][0] = static_cast<unsigned char>(c.color >> 16);
                poly.Colours[i][1] = static_cast<unsigned char>(c.color >> 8);
                poly.Colours[i][2] = static_cast<unsigned char>(c.color);
                poly.Colours[i][3] = static_cast<unsigned char>(c.color >> 24);
            }
            RenderPolyTransformed(&poly, texture, nullptr, 0);
        }
        return;
    }
    const int kBatch = 1024;
    static QuadVertex triangles[kBatch * 6];
    if (texture)
        sub_6C6CC0(texture, 0);
    eSetTransform(&eMathIdentityMatrix, g_CurrentRenderTarget->ViewID);
    if (dword_982BC4)
        g_CurrentEffect->Effect->SetTexture(g_CurrentEffect->Parameter(kParamDiffuseTex), dword_982BC4);
    if (byte_8F9358)
    {
        g_CurrentEffect->Effect->CommitChanges();
        dword_982CA0 = 0;
        g_CurrentEffect->Effect->BeginPass(0);
        for (int start = 0; start < quads; start += kBatch)
        {
            const int n = quads - start < kBatch ? quads - start : kBatch;
            const QuadVertex *in = reinterpret_cast<const QuadVertex *>(corners) + start * 4;
            for (int q = 0; q < n; ++q)
            {
                QuadVertex *out = triangles + q * 6;
                out[0] = in[q * 4 + 0];
                out[1] = in[q * 4 + 1];
                out[2] = in[q * 4 + 2];
                out[3] = in[q * 4 + 0];
                out[4] = in[q * 4 + 2];
                out[5] = in[q * 4 + 3];
            }
            r->DrawUP(D3DPT_TRIANGLELIST, static_cast<uint32_t>(n * 2), triangles, sizeof(QuadVertex));
        }
        g_CurrentEffect->Effect->EndPass();
    }
    sub_6C6ED0();
}
} // namespace nfs::worldquads

void eViewPlatInterface::FEBeginBatchRender(int)
{
}

void eViewPlatInterface::FEEndBatchRender()
{
}

void eViewPlatInterface::FERender(ePoly *poly, TextureInfo *texture, int mode)
{
    eViewPlatInterface::Render(poly, texture, mode);
}

void eViewPlatInterface::FERender(ePoly *poly, TextureInfo *texture, TextureInfo *mask, int mode)
{
    RenderMaskedPoly(poly, texture, mask, mode);
}

void eViewPlatInterface::Render(ePoly *poly, TextureInfo *texture, bMatrix4 *transform, int mode, float)
{
    RenderPolyTransformed(poly, texture, transform, mode);
}
