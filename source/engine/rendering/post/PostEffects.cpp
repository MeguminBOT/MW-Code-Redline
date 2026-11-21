/**
 * Post effect targets and the passes the overbright (bloom) effect is built from (original: zPlatform
 * eCreatePostProcessTargets 0x6c2e60 and its grid buffers 0x6c2aa0, the filter pass 0x6d2cd0, the screen copy
 * 0x6dbec0, the composite 0x6d3b80 and the overbright effect 0x6dc040).
 *
 * The passes draw a screen quad through the filter (or overbright) effect into a target, with up to four textures
 * sampled at kernel offsets and weighted; the screen is first copied (StretchRect) to quarter-size targets and blurred
 * there. The targets are the Vulkan renderer's.
 */
#include "game/gen/api.h"
#include "game/platform/D3DX.h"
#include "game/platform/Device.h"
#include "game/platform/Platform.h"
#include "legacy/link.h"
#include "engine/rendering/x360/resources.h"
#include "engine/rendering/vulkan/renderer.h"

/** The visual treatment's tone curve (source/engine/rendering/frame/PlatformMisc.cpp). */
float __cdecl TreatmentCurve(float x, float slope);


NFS_REPLACE(0x6c2aa0, sub_6C2AA0)
NFS_REPLACE(0x6d2cd0, sub_6D2CD0)
NFS_REPLACE(0x6dbec0, sub_6DBEC0)
NFS_REPLACE(0x6d3b80, sub_6D3B80)
NFS_REPLACE(0x6dc040, sub_6DC040)

/** Screen-sized targets: the copies of the frame (with the window's size) and the overbright target. */
extern "C" int UINT_008f9adc;
extern "C" int UINT_008f9ae0;
extern "C" int D3DFORMAT_008f9ad8;
extern "C" int dword_8F9AE8;
extern "C" void *dword_93E7BC;
extern "C" void *dword_982960;
extern "C" void *dword_93E7B8;
extern "C" int __multisamplequality;
/** Quarter-size targets for the blur (four textures and their surfaces) and their sizes. */
extern "C" int UINT_ARRAY_0093e81c[3];
extern "C" int UINT_ARRAY_0093cae0[3];
extern "C" int dword_93CAEC;
extern "C" int dword_93E828;
extern "C" void *dword_982964[4];
extern "C" void *dword_982990[4];
/** Lookup textures (made once): sizes and formats. */
extern "C" unsigned char dword_988540;
extern "C" void *dword_98298C;
extern "C" void *dword_93E870;
extern "C" void *dword_93D06C;
extern "C" void *dword_982900;
extern "C" int D3DFORMAT_008f9aec;
extern "C" int UINT_008f9af0;
extern "C" int UINT_008f9af8;
extern "C" int D3DFORMAT_008f9afc;
extern "C" int UINT_008f9b00;
extern "C" int D3DFORMAT_008f9b04;
extern "C" int UINT_008f9b08;
/** The grid buffers (made but not drawn by this build). */
extern "C" void *dword_982C64;
extern "C" void *dword_982C68;
/** Effect switches the targets depend on, cleared when targets cannot be made. */
extern "C" int lpData_009017dc;
extern "C" int dword_901800;
extern "C" int dword_9017F0;
extern "C" int lpData_00901810;
extern "C" int lpData_00901828;
extern "C" int INT_009017d0;
extern "C" int g_X360EffectsEnable;
extern "C" int lpData_009017fc;
extern "C" int dword_901898;
extern "C" int dword_901878;
extern "C" int dword_901888;
extern "C" int dword_901864;
extern "C" int dword_901884;
extern "C" int dword_901868;
/** The filter passes write alpha only when set (colour write mode 8 instead of 15). */
extern "C" unsigned char dword_982C51;
/** Blur iterations of the overbright effect. */
extern "C" int dword_8F9474;
/** Which screen copy holds the frame for the overbright effect, -1 for the back buffer. */
extern "C" int dword_982C60;
extern "C" unsigned char dword_982B26;
extern "C" unsigned char dword_982B2A;
extern "C" float unk_8F9B38[4];
extern "C" float flt_8F9B4C;
extern "C" float flt_8F9B48;
extern "C" float flt_8F9B54;
extern "C" float flt_8F9B50;
extern "C" float flt_8F9B2C;
extern "C" float flt_8F9B30;
extern "C" float flt_8FAE64;
extern "C" void *dword_982CA0;

extern "C" void __cdecl sub_6E2F50();
extern "C" void __cdecl sub_6C2AA0();
extern "C" void __cdecl sub_6D2CD0(void *target, void *texture, int taps, const float *kernel, int mode);
extern "C" void __cdecl sub_6DBEC0();
extern "C" void __cdecl sub_6D3B80(void *target, int count, void *const *textures, const unsigned int *colours,
                                   int blend, int source, int destination, unsigned int colour);
extern "C" void __cdecl sub_6DC040(bool overbright);

namespace
{
const int kParamColorWriteMode = 0x18c;
const int kParamFilterWeights = 0x59c;
const int kParamOverbrightOffset = 0x9fc;
const int kParamOverbrightAmount = 0xa24;
/** FILTERTEXTURE0 to 3. */
const int kParamFilterTextures[4] = {0x4fc, 0x524, 0x54c, 0x574};

/** A screen quad vertex of the filter effects: position, colour and four texture coordinates. */
struct FilterVertex
{
    float x, y, z;
    D3DCOLOR colour;
    float uv[4][2];
};

/** The screen quad over a target of `width` x `height`: corners at texel centres, the same in all four sets. */
void ScreenQuad(FilterVertex v[4], UINT width, UINT height, D3DCOLOR colour)
{
    const float hx = -0.5f / static_cast<float>(width), hy = -0.5f / static_cast<float>(height);
    const float a = -hx, b = -hy, c = 1.0f - hx, d = 1.0f - hy;
    static const float kCorners[4][2] = {{-1.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, -1.0f}, {-1.0f, -1.0f}};
    const float uv[4][2] = {{a, b}, {c, b}, {c, d}, {a, d}};
    for (int i = 0; i < 4; ++i)
    {
        v[i].x = kCorners[i][0];
        v[i].y = kCorners[i][1];
        v[i].z = 0.0f;
        v[i].colour = colour;
        for (int k = 0; k < 4; ++k)
        {
            v[i].uv[k][0] = uv[i][0];
            v[i].uv[k][1] = uv[i][1];
        }
    }
}

void BeginEffectPass(eEffect *effect, UINT pass)
{
    g_CurrentEffect = effect;
    effect->Start();
    device::SetVertexDeclaration(effect);
    UINT passes;
    effect->Effect->Begin(&passes, 0);
    effect->Effect->BeginPass(pass);
}

/** Draws the quad through the current effect, ends it, and binds the view's target again. */
void DrawQuadAndRestore(eEffect *effect, const FilterVertex v[4])
{
    dword_982CA0 = nullptr;
    g_CurrentEffect->Effect->CommitChanges();
    device::DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, v, sizeof(FilterVertex));
    effect->Effect->EndPass();
    effect->End();
    effect->Effect->End();
    eRenderTarget *target = g_CurrentRenderTarget;
    g_CurrentEffect = nullptr;
    sub_6E2F50();
    device::SetTarget(target->Surface, target->DepthSurface, target->Width, target->Height);
    g_CurrentRenderTarget = target;
    g_CurrentEffect = nullptr;
}

nfs::vk::Texture *Target(uint32_t width, uint32_t height, uint32_t format)
{
    return nfs::vk::g_renderer->CreateTexture(width, height, 1, format, true);
}

/** A 2D texture's top surface: the texture itself, with a reference of its own. */
void *TopSurface(nfs::vk::Texture *texture)
{
    if (texture)
        ++texture->refs;
    return texture;
}

void ReleaseObject(void *&object)
{
    if (object)
        nfs::vk::g_renderer->Release(static_cast<nfs::vk::Object *>(object));
    object = nullptr;
}

/** Binds a new target and clears its colour, as the original does after making each. */
void ClearTarget(void *surface)
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    r->SetRenderTarget(static_cast<nfs::vk::Object *>(surface), nullptr);
    r->Clear(D3DCLEAR_TARGET, 0, 0.0f, 0);
}
} // namespace

/** The post effect targets for the effects that are on; an effect whose targets cannot be made is switched off. */
void eCreatePostProcessTargets()
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    UINT_008f9adc = g_WindowWidth;
    UINT_008f9ae0 = g_WindowHeight;
    const bool copies = lpData_009017dc || dword_901800 || dword_9017F0 || lpData_00901810 || lpData_00901828 ||
                        INT_009017d0;
    const bool overbright = g_X360EffectsEnable || lpData_009017fc;
    for (int i = 0; i < 2; ++i)
    {
        g_ScreenCopySurfaces[i] = nullptr;
        g_ScreenCopyTextures[i] = nullptr;
    }
    if (copies)
    {
        bool ok = true;
        for (int i = 0; ok && i < 2; ++i)
        {
            nfs::vk::Texture *copy = Target(UINT_008f9adc, UINT_008f9ae0, D3DFORMAT_008f9ad8);
            g_ScreenCopyTextures[i] = reinterpret_cast<IDirect3DTexture9 *>(copy);
            ok = copy != nullptr;
            if (ok)
            {
                g_ScreenCopySurfaces[i] = static_cast<IDirect3DSurface9 *>(TopSurface(copy));
                ClearTarget(g_ScreenCopySurfaces[i]);
            }
        }
        if (!ok)
        {
            for (int i = 0; i < 2; ++i)
            {
                ReleaseObject(reinterpret_cast<void *&>(g_ScreenCopySurfaces[i]));
                ReleaseObject(reinterpret_cast<void *&>(g_ScreenCopyTextures[i]));
            }
            lpData_00901810 = dword_9017F0 = dword_901800 = lpData_009017dc = 0;
            dword_901898 = dword_901878 = dword_901888 = dword_901864 = 0;
        }
    }
    dword_93E7BC = dword_982960 = dword_93E7B8 = nullptr;
    if (overbright)
    {
        nfs::vk::Texture *target = Target(g_WindowWidth, g_WindowHeight, dword_8F9AE8);
        dword_93E7BC = target;
        if (target)
        {
            dword_982960 = TopSurface(target);
            ClearTarget(dword_982960);
        }
        // The multisampled copy the original makes with antialiasing on is not used by the passes ported here.
        if (!target)
        {
            ReleaseObject(dword_982960);
            ReleaseObject(dword_93E7BC);
            ReleaseObject(dword_93E7B8);
            lpData_009017fc = g_X360EffectsEnable = dword_901884 = dword_901868 = 0;
        }
    }
    const int width = g_WindowWidth, height = g_WindowHeight;
    UINT_ARRAY_0093e81c[0] = UINT_ARRAY_0093e81c[1] = UINT_ARRAY_0093e81c[2] = height / 4;
    dword_93CAEC = width / 32;
    UINT_ARRAY_0093cae0[0] = UINT_ARRAY_0093cae0[1] = UINT_ARRAY_0093cae0[2] = width / 4;
    dword_93E828 = height / 32;
    bool ok = true;
    for (int i = 0; i < 4; ++i)
        dword_982990[i] = dword_982964[i] = nullptr;
    for (int i = 0; ok && i < 4; ++i)
    {
        // The fourth one is the eighth-size target, whose sizes follow the three quarter sizes in memory.
        nfs::vk::Texture *quarter = Target(static_cast<uint32_t>((&UINT_ARRAY_0093cae0[0])[i]),
                                           static_cast<uint32_t>((&UINT_ARRAY_0093e81c[0])[i]), D3DFORMAT_008f9ad8);
        dword_982964[i] = quarter;
        ok = quarter != nullptr;
        if (ok)
        {
            dword_982990[i] = TopSurface(quarter);
            ClearTarget(dword_982990[i]);
        }
    }
    if (!ok)
    {
        for (int i = 0; i < 4; ++i)
        {
            ReleaseObject(dword_982990[i]);
            ReleaseObject(dword_982964[i]);
        }
        dword_9017F0 = lpData_009017fc = dword_901878 = dword_901884 = 0;
    }
    sub_6C2AA0();
    if (!dword_988540)
    {
        dword_988540 = 1;
        dword_98298C = r->CreateTexture(UINT_008f9af0, UINT_008f9af0, 1, D3DFORMAT_008f9aec, false);
        dword_93E870 = r->CreateTexture(1, UINT_008f9af8, 1, D3DFORMAT_008f9aec, false);
        dword_93D06C = r->CreateTexture(UINT_008f9b00, 2, 1, D3DFORMAT_008f9afc, false);
        dword_982900 = r->CreateTexture(UINT_008f9b08, 1, 1, D3DFORMAT_008f9b04, false);
    }
}

/** The grid buffers of a post effect; nothing in this build draws them, so in Vulkan mode there are none. */
extern "C" void __cdecl sub_6C2AA0()
{
    dword_982C64 = nullptr;
    dword_982C68 = nullptr;
}

/**
 * One filter pass into `target`: `taps` samples of `texture` at the kernel's offsets (in texels: weight, x, y per
 * tap), weighted and summed by the filter effect (pass taps-1; mode 2 and 3 its passes 4 and 5); mode 1 uses the
 * overbright effect instead, with its offset and the amount from the camera's flash.
 */
extern "C" void __cdecl sub_6D2CD0(void *target, void *texture, int taps, const float *kernel, int mode)
{
    UINT width, height;
    device::SurfaceSize(target, width, height);
    device::SetTarget(target, nullptr, static_cast<int>(width), static_cast<int>(height));
    eEffect *filter = g_Effects[EFFECT_FILTER];
    filter->Effect->SetInt(filter->Parameter(kParamColorWriteMode), dword_982C51 ? 8 : 15);
    FilterVertex v[4];
    ScreenQuad(v, width, height, 0xFFFFFFFF);
    eEffect *effect = mode == 1 ? g_Effects[EFFECT_OVERBRIGHT] : filter;
    BeginEffectPass(effect, mode == 1 ? 0 : mode == 2 ? 4 : mode == 3 ? 5 : static_cast<UINT>(taps - 1));
    UINT textureWidth, textureHeight;
    device::TextureSize(texture, textureWidth, textureHeight);
    const float tw = 1.0f / static_cast<float>(textureWidth), th = 1.0f / static_cast<float>(textureHeight);
    float weights[4][4];
    int i = 0;
    for (; i < taps; ++i)
    {
        effect->Effect->SetTexture(effect->Parameter(kParamFilterTextures[i]),
                                   static_cast<IDirect3DBaseTexture9 *>(texture));
        const float *tap = kernel + i * 3;
        weights[i][0] = weights[i][1] = weights[i][2] = weights[i][3] = tap[0];
        const float du = tw * tap[1], dv = th * tap[2];
        for (int k = 0; k < 4; ++k)
        {
            v[k].uv[i][0] += du;
            v[k].uv[i][1] += dv;
        }
    }
    for (; i < 4; ++i)
    {
        effect->Effect->SetTexture(effect->Parameter(kParamFilterTextures[i]), nullptr);
        weights[i][0] = weights[i][1] = weights[i][2] = weights[i][3] = 0.0f;
    }
    effect->Effect->SetVectorArray(effect->Parameter(kParamFilterWeights), weights, 4);
    if (effect->Type == EFFECT_OVERBRIGHT)
    {
        effect->Effect->SetVector(effect->Parameter(kParamOverbrightOffset), unk_8F9B38);
        const char *flash = reinterpret_cast<const char *const *>(eViews[1].unknown_64)[0];
        const char *mover = reinterpret_cast<const char *const *>(eViews[1].unknown_64)[1];
        const float lighting = *reinterpret_cast<const float *>(mover + 0x290);
        float amount;
        bool clampLow = false;
        if (*reinterpret_cast<const int *>(flash + 0x30))
        {
            amount = (1.0f - *reinterpret_cast<const float *>(flash + 0x140)) * (flt_8F9B4C * lighting + flt_8F9B48);
            clampLow = true;
        }
        else
        {
            amount = flt_8F9B54 * lighting + flt_8F9B50;
        }
        if (clampLow && amount < 0.0f)
            amount = 0.0f;
        else if (amount > 1.0f)
            amount = 1.0f;
        effect->Effect->SetFloat(effect->Parameter(kParamOverbrightAmount), amount);
    }
    eSetAlphaBlend(0, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA);
    eCommitBlendState();
    DrawQuadAndRestore(effect, v);
}

/**
 * Adds `count` textures (up to four) into `target` with the filter effect's pass count-1, each weighted by its colour
 * (bytes as b, g, r, a over 255) and blended as asked; `colour` is the quad's vertex colour.
 */
extern "C" void __cdecl sub_6D3B80(void *target, int count, void *const *textures, const unsigned int *colours,
                                   int blend, int source, int destination, unsigned int colour)
{
    UINT width, height;
    device::SurfaceSize(target, width, height);
    if (__multisamplequality > 0)
        device::SetTarget(target, nullptr, static_cast<int>(width), static_cast<int>(height));
    else
        device::SetTarget(target, g_CurrentRenderTarget->DepthSurface, static_cast<int>(width),
                          static_cast<int>(height));
    FilterVertex v[4];
    ScreenQuad(v, width, height, colour);
    eEffect *effect = g_Effects[EFFECT_FILTER];
    BeginEffectPass(effect, static_cast<UINT>(count - 1));
    effect->Effect->SetInt(effect->Parameter(kParamColorWriteMode), 15);
    eSetAlphaBlend(blend, source, destination);
    float weights[4][4];
    int i = 0;
    for (; i < count; ++i)
    {
        effect->Effect->SetTexture(effect->Parameter(kParamFilterTextures[i]),
                                   static_cast<IDirect3DBaseTexture9 *>(textures[i]));
        const unsigned char *c = reinterpret_cast<const unsigned char *>(&colours[i]);
        for (int k = 0; k < 4; ++k)
            weights[i][k] = static_cast<float>(c[k]) * 0.003921569f;
    }
    for (; i < 4; ++i)
    {
        effect->Effect->SetTexture(effect->Parameter(kParamFilterTextures[i]), nullptr);
        weights[i][0] = weights[i][1] = weights[i][2] = weights[i][3] = 0.0f;
    }
    eSetAlphaTest(0, 0);
    eCommitBlendState();
    effect->Effect->SetVectorArray(effect->Parameter(kParamFilterWeights), weights, 4);
    DrawQuadAndRestore(effect, v);
}

/** Copies the frame for the post effects: to the first quarter-size target (blurred down by the filter pass) or, for
 * the effects that need the full frame, to the first screen copy. */
extern "C" void __cdecl sub_6DBEC0()
{
    static const float *const kQuarterKernel = reinterpret_cast<const float *>(0x8F94F4);
    static const float *const kDownKernel = reinterpret_cast<const float *>(0x8F9560);
    if (!lpData_00901828 && !dword_982B2A && !INT_009017d0)
    {
        device::StretchRect(g_BackBufferSurface, nullptr, dword_982990[0], nullptr, D3DTEXF_NONE);
        sub_6D2CD0(dword_982990[2], dword_982964[0], 1, kQuarterKernel, 3);
        dword_982C60 = -1;
    }
    else
    {
        device::StretchRect(g_BackBufferSurface, nullptr, g_ScreenCopySurfaces[0], nullptr, D3DTEXF_NONE);
        dword_982C60 = 0;
        if (!lpData_00901828 && !dword_982B26)
        {
            device::SetRenderState(D3DRS_COLORWRITEENABLE, 7);
            return;
        }
        device::SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
        if (lpData_00901828)
            sub_6D2CD0(dword_982990[0], g_ScreenCopyTextures[0], 4, kDownKernel, 2);
        if (dword_982B26)
            sub_6D2CD0(dword_982990[2], g_ScreenCopyTextures[0], 4, kDownKernel, 3);
    }
    device::SetRenderState(D3DRS_COLORWRITEENABLE, 7);
}

/** The overbright effect: the bright parts of the frame, blurred at quarter size, added back over the frame with the
 * blend factor as their strength. */
extern "C" void __cdecl sub_6DC040(bool overbright)
{
    static const float *const kExtract = reinterpret_cast<const float *>(0x8F9590);
    static const float *const kBlurX = reinterpret_cast<const float *>(0x8F9530);
    static const float *const kBlurY = reinterpret_cast<const float *>(0x8F9500);
    static const float *const kCopy = reinterpret_cast<const float *>(0x8F94F4);
    sub_6D2CD0(dword_982990[0], dword_982964[2], 1, kExtract, 1);
    for (int i = 0; i < dword_8F9474; ++i)
    {
        sub_6D2CD0(dword_982990[1], dword_982964[0], 4, kBlurX, 0);
        sub_6D2CD0(dword_982990[0], dword_982964[1], 4, kBlurY, 0);
    }
    const char *flash = reinterpret_cast<const char *const *>(eViews[1].unknown_64)[0];
    float level;
    if (!*reinterpret_cast<const int *>(flash + 0x30))
    {
        level = (lpData_00901828 ? flt_8F9B30 : flt_8F9B2C) * 255.0f;
        if (level > 255.0f)
            level = 255.0f;
    }
    else
    {
        level = *reinterpret_cast<const float *>(flash + 0x140) * flt_8FAE64 * 200.0f;
    }
    const unsigned int c = static_cast<unsigned int>(static_cast<int>(level));
    device::SetRenderState(D3DRS_BLENDFACTOR, c | c << 8 | c << 16 | c << 24);
    static const unsigned int kWhite = 0xFFFFFFFF;
    void *const *quarter = dword_982964;
    void *const *bright = &dword_93E7BC;
    if (g_X360EffectsEnable)
        sub_6D3B80(dword_982960, 1, quarter, &kWhite, 1, D3DBLEND_BLENDFACTOR, D3DBLEND_ONE, 0xFFFFFFFF);
    void *const *added = g_X360EffectsEnable ? bright : quarter;
    const int source = g_X360EffectsEnable ? D3DBLEND_ONE : D3DBLEND_BLENDFACTOR;
    if (overbright && dword_982C60 == -1)
    {
        sub_6D3B80(g_BackBufferSurface, 1, quarter, &kWhite, 1, D3DBLEND_BLENDFACTOR, D3DBLEND_ONE, 0xFFFFFFFF);
    }
    else
    {
        sub_6D3B80(g_ScreenCopySurfaces[dword_982C60], 1, added, &kWhite, 1, source, D3DBLEND_ONE, 0xFFFFFFFF);
        if (overbright)
            sub_6D2CD0(g_BackBufferSurface, g_ScreenCopyTextures[dword_982C60], 1, kCopy, 0);
    }
    sub_6E2F50();
    device::SetTarget(g_BackBufferTarget.Surface, g_BackBufferTarget.DepthSurface, g_BackBufferTarget.Width,
                      g_BackBufferTarget.Height);
    g_CurrentRenderTarget = &g_BackBufferTarget;
    g_CurrentEffect = nullptr;
}

namespace
{
const float *const kCopyKernel = reinterpret_cast<const float *>(0x8F94F4);
const float *const kBlurXKernel = reinterpret_cast<const float *>(0x8F9530);
const float *const kBlurYKernel = reinterpret_cast<const float *>(0x8F9500);
const int kParamDiffuseMap = 0x22c;
const int kParamRainDropMap = 0x2cc;
const int kParamRainDropFade = 0xa74;
const int kParamRainDropRect = 0xa4c;
const int kParamScreenFilterTexture = 0x2a4;
const int kParamMotionBlurMap = 0x72c;
const int kParamMotionBlurCurve = 0xa9c;
const int kParamMotionBlurAmount = 0x4d4;
const int kParamMotionBlurConstant = 0xb3c;
const int kParamMotionBlurColour = 0xf24;

/** The screen copy after `index` (the post effects ping-pong between the two). */
int NextCopy(int index)
{
    return (index + 1) % 2;
}

/** The player view's flash (0), mover with the rain (1) and look (2) objects. */
void *PlayerViewObject(int index)
{
    return reinterpret_cast<void *>(eViews[1].unknown_64[index]);
}

void SetIdentity(bMatrix4 &m)
{
    m.v0 = bVector4(1.0f, 0.0f, 0.0f, 0.0f);
    m.v1 = bVector4(0.0f, 1.0f, 0.0f, 0.0f);
    m.v2 = bVector4(0.0f, 0.0f, 1.0f, 0.0f);
    m.v3 = bVector4(0.0f, 0.0f, 0.0f, 1.0f);
}

/** Binds a target without depth buffer, with a viewport over all of it. */
void BindTarget(void *target)
{
    UINT width, height;
    device::SurfaceSize(target, width, height);
    device::SetTarget(target, nullptr, static_cast<int>(width), static_cast<int>(height));
}

void BeginEffect(eEffect *effect)
{
    g_CurrentEffect = effect;
    effect->Start();
    device::SetVertexDeclaration(effect);
    UINT passes;
    effect->Effect->Begin(&passes, 0);
}

void EndEffect(eEffect *effect)
{
    effect->End();
    effect->Effect->End();
    g_CurrentEffect = nullptr;
}

/** A screen-space vertex: position, colour and one texture coordinate pair. */
struct ScreenVertex
{
    float x, y, z;
    D3DCOLOR colour;
    float u, v;
};
} // namespace

NFS_REPLACE(0x6d4620, sub_6D4620)
NFS_REPLACE(0x6d49a0, sub_6D49A0)
NFS_REPLACE(0x6d3850, sub_6D3850)
NFS_REPLACE(0x6d33b0, sub_6D33B0)
NFS_REPLACE(0x6c3570, sub_6C3570)
NFS_REPLACE(0x6dbfe0, sub_6DBFE0)
NFS_REPLACE(0x6d4960, sub_6D4960)

/** Motion blur: the amount, its colour and curve settings blended between the low and high presets, and the values the
 * lookup textures were last made for. */
extern "C" float dword_93E00C;
extern "C" float flt_8F9490[4];
extern "C" float flt_8F94A0[4];
extern "C" float flt_8F94B0[4];
extern "C" float dword_93DE68[4];
extern "C" float flt_8F948C;
extern "C" float dword_982C58;
extern "C" float flt_8F94D0;
extern "C" float flt_8F94C0[3];
extern "C" float dword_8F94EC;
extern "C" float flt_8F94F0;
extern "C" float dword_982C5C;
extern "C" float dword_8F9B0C;
extern "C" float dword_98855C;
extern "C" int dword_988560;
extern "C" int dword_988558;
extern "C" int dword_8FAE9C;
extern "C" unsigned char byte_9B0A74;
/** The shadow map blur targets (surface and texture pairs). */
extern "C" void *dword_982A68;
extern "C" void *dword_982A6C;
extern "C" void *dword_982A70;
extern "C" void *dword_982A74;

extern "C" void __cdecl sub_6D4620(bool toBackBuffer);
extern "C" void __cdecl sub_6D49A0(eView *view, bool toBackBuffer);
extern "C" void __cdecl sub_6D3850(float amount, bool toBackBuffer);
extern "C" void __cdecl sub_6D33B0(void *target, void *texture, int effectIndex, int blend, int source,
                                   int destination, UINT pass);
extern "C" void __cdecl sub_6C3570();
extern "C" void __cdecl sub_6DBFE0();
extern "C" void __cdecl sub_6D4960();

/**
 * Rain drops on the screen: the frame is copied into the target (the next screen copy, or the back buffer), then each
 * drop of the player view's rain draws a quad there through the rain drop effect, which refracts the copy.
 */
extern "C" void __cdecl sub_6D4620(bool toBackBuffer)
{
    const void *rain = PlayerViewObject(1);
    const int *count = static_cast<const int *>(rain);
    if (!*count)
        return;
    const int source = dword_982C60;
    void *target = g_BackBufferSurface;
    if (!toBackBuffer)
    {
        dword_982C60 = NextCopy(source);
        target = g_ScreenCopySurfaces[dword_982C60];
    }
    void *frame = g_ScreenCopyTextures[source];
    sub_6D2CD0(target, frame, 1, kCopyKernel, 0);
    BindTarget(target);
    eEffect *effect = g_Effects[EFFECT_RAIN_DROP];
    BeginEffect(effect);
    ID3DXEffect *fx = effect->Effect;
    fx->SetTexture(effect->Parameter(kParamDiffuseMap), static_cast<IDirect3DBaseTexture9 *>(frame));
    TextureInfo *drop = GetTextureInfo(bStringHash("RAINDROPPC"), 1, 0);
    fx->SetTexture(effect->Parameter(kParamRainDropMap),
                   static_cast<IDirect3DBaseTexture9 *>(drop->PlatInfo->Texture));
    ScreenVertex v[4];
    for (int i = 0; i < *count; ++i)
    {
        float x, y, fade = 0.0f, size = 1.0f;
        NFS_ORIGINAL(0x73cfa0, void(__thiscall *)(const void *, int, float *, float *, float *, float *, float *))(
            rain, i, &x, &y, &fade, &size, nullptr);
        fx->SetFloat(effect->Parameter(kParamRainDropFade), fade * 3.0f);
        const float r = size * 0.12f, left = x + x - 1.0f, top = 1.0f - (y + y);
        const float rect[4] = {x - r * 0.25f, y + r * 0.25f, r, -r};
        fx->SetVector(effect->Parameter(kParamRainDropRect), rect);
        v[0] = {left, top, 0.0f, 0xFFFFFFFF, 0.0f, 0.0f};
        v[1] = {left + r, top, 0.0f, 0xFFFFFFFF, 1.0f, 0.0f};
        v[2] = {left, top + r, 0.0f, 0xFFFFFFFF, 0.0f, 1.0f};
        v[3] = {left + r, top + r, 0.0f, 0xFFFFFFFF, 1.0f, 1.0f};
        g_CurrentEffect->Effect->CommitChanges();
        fx->BeginPass(0);
        device::DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(ScreenVertex));
        fx->EndPass();
    }
    device::SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    device::SetRenderState(D3DRS_ZENABLE, TRUE);
    EndEffect(effect);
}

/**
 * Pixelation in a rectangle of the screen (the view's look object gives it, centre and size in 640x480 units): the
 * frame is copied into the target and to the eighth-size target, which a quad then draws over the rectangle with the
 * Xbox 360 debug polygon effect (slot 27).
 */
extern "C" void __cdecl sub_6D49A0(eView *view, bool toBackBuffer)
{
    void *look = reinterpret_cast<void *>(view->unknown_64[2]);
    if (!look || !byte_9B0A74)
        return;
    const int source = dword_982C60;
    void *target = toBackBuffer ? g_BackBufferSurface : g_ScreenCopySurfaces[NextCopy(source)];
    void *eighth = dword_982964[3];
    sub_6D2CD0(dword_982990[3], g_ScreenCopyTextures[source], 1, kCopyKernel, 0);
    sub_6D2CD0(target, g_ScreenCopyTextures[source], 1, kCopyKernel, 0);
    dword_982C60 = NextCopy(source);
    BindTarget(target);
    float x, y, w, h;
    NFS_ORIGINAL(0x73d060, void(__thiscall *)(void *, float *, float *, float *, float *))(look, &x, &y, &w, &h);
    x -= w * 0.5f;
    y -= h * 0.5f;
    // X360 Stuff draws the quad with the debug polygon effect, which the post processing set up.
    eEffect *fe = g_Effects[EFFECT_GLOSSY_DEPTH];
    BeginEffect(fe);
    bMatrix4 identity;
    SetIdentity(identity);
    eViewPlatInfo *plat = view->PlatInfo;
    plat->WorldViewMatrix = identity;
    D3DXMatrixMultiply(&plat->WorldScreenMatrix, &plat->WorldViewMatrix, &plat->ViewScreenMatrix);
    plat->ViewScreenMatrix = identity;
    D3DXMatrixMultiply(&plat->WorldScreenMatrix, &plat->WorldViewMatrix, &plat->ViewScreenMatrix);
    eSetTransform(&identity, 0);
    ePoly poly;
    const float right = x + w, bottom = y + h;
    const float corners[4][2] = {{x, y}, {right, y}, {right, bottom}, {x, bottom}};
    for (int i = 0; i < 4; ++i)
    {
        poly.Vertices[i][0] = corners[i][0];
        poly.Vertices[i][1] = corners[i][1];
        poly.UVs[i][0] = corners[i][0] * (1.0f / nfs::x360::g_screenUnits[0]);
        poly.UVs[i][1] = corners[i][1] * (1.0f / nfs::x360::g_screenUnits[1]);
        poly.Colours[i][0] = poly.Colours[i][1] = poly.Colours[i][2] = poly.Colours[i][3] = 0xFF;
    }
    device::SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device::SetRenderState(D3DRS_ZENABLE, FALSE);
    fe->Effect->SetTexture(fe->Parameter(kParamDiffuseMap), static_cast<IDirect3DBaseTexture9 *>(eighth));
    eSetAlphaTest(0, 1);
    eSetAlphaBlend(0, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA);
    eCommitBlendState();
    eViewPlatInterface::Render(&poly, nullptr, 0);
    EndEffect(fe);
    device::SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    device::SetRenderState(D3DRS_ZENABLE, TRUE);
}

/** Fills the motion blur's lookup textures for the current colour curves (the 2D map) and blur power (the 1D curve). */
extern "C" void __cdecl sub_6C3570()
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    const auto curve = TreatmentCurve;
    const int size = UINT_008f9af0;
    unsigned int *texels = static_cast<unsigned int *>(::operator new(static_cast<size_t>(size) * size * 4));
    const float last = static_cast<float>(size) - 1.0f;
    for (int column = 0; column < size; ++column)
    {
        const float u = static_cast<float>(column) / last;
        for (int row = 0; row < size; ++row)
        {
            const float v = static_cast<float>(row) / last;
            const unsigned int red = static_cast<unsigned char>(static_cast<int>(curve(v, flt_8F94B0[0]) * 255.0f));
            const unsigned int green = static_cast<unsigned char>(static_cast<int>(curve(u, flt_8F94B0[1]) * 255.0f));
            const unsigned int blue = static_cast<unsigned char>(static_cast<int>(curve(v, flt_8F94B0[2]) * 255.0f));
            const unsigned int alpha = static_cast<unsigned char>(static_cast<int>(curve(u, flt_8F94B0[3]) * 255.0f));
            texels[row * size + column] = alpha << 24 | red << 16 | green << 8 | blue;
        }
    }
    r->UploadTexture(static_cast<nfs::vk::Texture *>(dword_98298C), 0, 0, texels,
                     static_cast<uint32_t>(size * size * 4));
    ::operator delete(texels);
    for (int k = 0; k < 4; ++k)
        dword_93DE68[k] = flt_8F94B0[k];

    const int length = UINT_008f9af8;
    texels = static_cast<unsigned int *>(::operator new(static_cast<size_t>(length) * 4));
    const float end = static_cast<float>(length) - 1.0f;
    for (int i = 0; i < length; ++i)
    {
        const float t = static_cast<float>(i) / end;
        float g;
        if (t <= 0.0f)
            g = 0.0f;
        else if (t >= 1.0f)
            g = 1.0f;
        else
            g = t / ((1.0f / flt_8F94F0 - 2.0f) * (1.0f - t) + 1.0f);
        const unsigned int level = static_cast<unsigned char>(static_cast<int>(t * 255.0f));
        texels[i] = level << 24 | level << 16 | level << 8 | static_cast<unsigned char>(static_cast<int>(g * 255.0f));
    }
    r->UploadTexture(static_cast<nfs::vk::Texture *>(dword_93E870), 0, 0, texels, static_cast<uint32_t>(length * 4));
    ::operator delete(texels);
    dword_982C5C = flt_8F94F0;
}

/**
 * One pass of a screen effect: a full-screen quad through effect `effectIndex` (pass `pass`) into `target` (the
 * current target when null), sampling `texture`. The screen filter (motion blur) also gets its lookup textures and
 * settings; the world effect gets identity transforms.
 */
extern "C" void __cdecl sub_6D33B0(void *target, void *texture, int effectIndex, int blend, int source,
                                   int destination, UINT pass)
{
    if (target)
        BindTarget(target);
    const float hx = -0.5f / static_cast<float>(g_WindowWidth), hy = -0.5f / static_cast<float>(g_WindowHeight);
    const D3DCOLOR grey = 0xFF7F7F7F;
    const ScreenVertex v[4] = {
        {-1.0f, 1.0f, 0.0f, grey, -hx, -hy},
        {1.0f, 1.0f, 0.0f, grey, 1.0f - hx, -hy},
        {1.0f, -1.0f, 0.0f, grey, 1.0f - hx, 1.0f - hy},
        {-1.0f, -1.0f, 0.0f, grey, -hx, 1.0f - hy},
    };
    eEffect *effect = g_Effects[effectIndex];
    BeginEffect(effect);
    ID3DXEffect *fx = effect->Effect;
    fx->SetTexture(effect->Parameter(kParamScreenFilterTexture), static_cast<IDirect3DBaseTexture9 *>(texture));
    if (effectIndex == EFFECT_SCREEN_FILTER)
    {
        fx->SetTexture(effect->Parameter(kParamMotionBlurMap), static_cast<IDirect3DBaseTexture9 *>(dword_98298C));
        fx->SetTexture(effect->Parameter(kParamMotionBlurCurve), static_cast<IDirect3DBaseTexture9 *>(dword_93E870));
        const float colour[4] = {flt_8F94D0 * flt_8F94C0[0], flt_8F94C0[1] * flt_8F94D0, flt_8F94C0[2] * flt_8F94D0,
                                 dword_8F94EC};
        fx->SetFloat(effect->Parameter(kParamMotionBlurAmount), dword_93E00C);
        fx->SetFloat(effect->Parameter(kParamMotionBlurConstant), 0.4f);
        fx->SetVector(effect->Parameter(kParamMotionBlurColour), colour);
    }
    else if (effectIndex == EFFECT_WORLD)
    {
        bMatrix4 identity;
        SetIdentity(identity);
        eView &view = eViews[g_CurrentRenderTarget->ViewID];
        eViewPlatInfo *plat = view.PlatInfo;
        plat->WorldViewMatrix = identity;
        plat->ViewScreenMatrix = identity;
        D3DXMatrixMultiply(&plat->WorldScreenMatrix, &plat->WorldViewMatrix, &plat->ViewScreenMatrix);
        dword_8FAE9C = -1;
        eSetTransform(&identity, view.ID);
    }
    eSetAlphaBlend(blend, source, destination);
    eSetAlphaTest(0, 0);
    eCommitBlendState();
    dword_982CA0 = nullptr;
    fx->BeginPass(pass);
    g_CurrentEffect->Effect->CommitChanges();
    device::DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, v, sizeof(ScreenVertex));
    fx->EndPass();
    EndEffect(effect);
    eRenderTarget *current = g_CurrentRenderTarget;
    sub_6E2F50();
    device::SetTarget(current->Surface, current->DepthSurface, current->Width, current->Height);
    g_CurrentRenderTarget = current;
    g_CurrentEffect = nullptr;
}

/**
 * Motion blur: blends the effect's settings for `amount` (remaking the lookup textures when they changed), then runs
 * the screen filter's passes (three with its first technique, two otherwise) between the screen copies, the last one
 * into the back buffer when asked.
 */
extern "C" void __cdecl sub_6D3850(float amount, bool toBackBuffer)
{
    dword_93E00C = amount;
    for (int k = 0; k < 4; ++k)
        flt_8F94B0[k] = (flt_8F94A0[k] - flt_8F9490[k]) * amount + flt_8F9490[k];
    flt_8F94D0 = (flt_8F948C - dword_982C58) * amount + dword_982C58;
    if (!(dword_988560 & 1))
    {
        dword_988560 |= 1;
        dword_98855C = dword_8F9B0C;
    }
    bool changed = dword_8F9B0C != dword_98855C || flt_8F94F0 != dword_982C5C || !dword_988558;
    for (int k = 0; k < 4; ++k)
        changed = changed || dword_93DE68[k] != flt_8F94B0[k];
    if (changed)
    {
        dword_98855C = dword_8F9B0C;
        sub_6C3570();
        dword_988558 = 1;
    }
    device::SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    const int current = dword_982C60, next = NextCopy(current);
    const bool threePasses = g_Effects[EFFECT_SCREEN_FILTER]->TechniqueIndex == 0;
    const auto pass = [](void *target, void *texture, UINT index) {
        sub_6D33B0(target, texture, EFFECT_SCREEN_FILTER, 0, D3DBLEND_ZERO, D3DBLEND_ONE, index);
    };
    pass(g_ScreenCopySurfaces[next], g_ScreenCopyTextures[current], 0);
    if (toBackBuffer)
    {
        if (threePasses)
        {
            pass(g_ScreenCopySurfaces[current], g_ScreenCopyTextures[next], 1);
            pass(g_BackBufferSurface, g_ScreenCopyTextures[current], 2);
        }
        else
        {
            pass(g_BackBufferSurface, g_ScreenCopyTextures[next], 1);
        }
        dword_982C60 = -1;
        return;
    }
    pass(g_ScreenCopySurfaces[current], g_ScreenCopyTextures[next], 1);
    if (threePasses)
    {
        pass(g_ScreenCopySurfaces[next], g_ScreenCopyTextures[current], 2);
        dword_982C60 = next;
    }
}

/** Blurs the first quarter-size target (the visual treatment's), as often as the overbright blur. */
extern "C" void __cdecl sub_6DBFE0()
{
    for (int i = 0; i < dword_8F9474; ++i)
    {
        sub_6D2CD0(dword_982990[1], dword_982964[0], 4, kBlurXKernel, 0);
        sub_6D2CD0(dword_982990[0], dword_982964[1], 4, kBlurYKernel, 0);
    }
}

/** Blurs the shadow map's colour target through the second one. */
extern "C" void __cdecl sub_6D4960()
{
    sub_6D2CD0(dword_982A70, dword_982A6C, 4, kBlurXKernel, 0);
    sub_6D2CD0(dword_982A68, dword_982A74, 4, kBlurYKernel, 0);
}

NFS_REPLACE(0x6c3b20, VisualTreatmentFillLookup)
NFS_REPLACE(0x6d53d0, VisualTreatmentRender)

void __fastcall VisualTreatmentFillLookup(char *plat);
void __fastcall VisualTreatmentRender(char *plat, void *, bool toBackBuffer);

/** The PC visual treatment's colour curve texture. X360 Stuff leaves it unfilled (0x6c3b20 returns at once): the Xbox
 * 360 visual look evaluates its curves in the shader (CURVE_COEFFS). */
void __fastcall VisualTreatmentFillLookup(char *)
{
}

/**
 * The visual treatment (colour grading): one pass of the visual treatment effect from the current screen copy (and
 * the blurred quarter-size copy) into the next screen copy or the back buffer; effects without shader model 2 shaders
 * take a second pass. The treatment's parameters come from 0x6d4ef0.
 */
void __fastcall VisualTreatmentRender(char *plat, void *, bool toBackBuffer)
{
    eEffect *effect = g_Effects[EFFECT_VISUAL_TREATMENT];
    const bool twoPasses = effect->LevelShaders[0] == 1;
    void *target = toBackBuffer && !twoPasses ? g_BackBufferSurface : g_ScreenCopySurfaces[NextCopy(dword_982C60)];
    UINT width, height;
    device::SurfaceSize(target, width, height);
    device::SetTarget(target, nullptr, static_cast<int>(width), static_cast<int>(height));
    BeginEffect(effect);
    ID3DXEffect *fx = effect->Effect;
    fx->SetTexture(effect->Parameter(kParamDiffuseMap),
                   static_cast<IDirect3DBaseTexture9 *>(g_ScreenCopyTextures[dword_982C60]));
    fx->SetTexture(effect->Parameter(kParamScreenFilterTexture), static_cast<IDirect3DBaseTexture9 *>(dword_982964[0]));
    device::SetRenderState(D3DRS_ZENABLE, FALSE);
    NFS_ORIGINAL(0x6d4ef0, void(__thiscall *)(char *, eEffect *))(plat, effect);
    const float hx = -0.5f / static_cast<float>(width), hy = -0.5f / static_cast<float>(height);
    // The original writes 1.0f's bits as the vertex colour.
    const D3DCOLOR colour = 0x3F800000;
    const ScreenVertex v[4] = {
        {-1.0f, 1.0f, 0.0f, colour, -hx, -hy},
        {1.0f, 1.0f, 0.0f, colour, 1.0f - hx, -hy},
        {1.0f, -1.0f, 0.0f, colour, 1.0f - hx, 1.0f - hy},
        {-1.0f, -1.0f, 0.0f, colour, -hx, 1.0f - hy},
    };
    dword_982CA0 = nullptr;
    fx->CommitChanges();
    fx->BeginPass(0);
    device::DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, v, sizeof(ScreenVertex));
    fx->EndPass();
    dword_982C60 = NextCopy(dword_982C60);
    if (twoPasses)
    {
        void *second = toBackBuffer ? g_BackBufferSurface : g_ScreenCopySurfaces[NextCopy(dword_982C60)];
        device::SurfaceSize(second, width, height);
        device::SetTarget(second, nullptr, static_cast<int>(width), static_cast<int>(height));
        fx->SetTexture(effect->Parameter(kParamDiffuseMap),
                       static_cast<IDirect3DBaseTexture9 *>(g_ScreenCopyTextures[dword_982C60]));
        fx->CommitChanges();
        fx->BeginPass(1);
        device::DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, v, sizeof(ScreenVertex));
        fx->EndPass();
        dword_982C60 = NextCopy(dword_982C60);
    }
    EndEffect(effect);
    eRenderTarget *current = g_CurrentRenderTarget;
    sub_6E2F50();
    device::SetTarget(current->Surface, current->DepthSurface, current->Width, current->Height);
    g_CurrentRenderTarget = current;
    g_CurrentEffect = nullptr;
}

NFS_REPLACE(0x6d4100, sub_6D4100)

extern "C" void __cdecl sub_6D4100(void *target, void *texture, D3DCOLOR colour, int blend, int source,
                                   int destination, bool keepStates);
extern "C" void __cdecl sub_6C6CC0(TextureInfo *texture, unsigned int flags);

/**
 * A full-screen front end quad over `target` in `colour`, with `texture` (white without one) blended as asked. With
 * `keepStates` the world-min effect draws it (alpha only) and the current blending stays.
 */
extern "C" void __cdecl sub_6D4100(void *target, void *texture, D3DCOLOR colour, int blend, int source,
                                   int destination, bool keepStates)
{
    eEffect *effect = g_Effects[keepStates ? EFFECT_WORLD_MIN : EFFECT_FE];
    // The world-min effect is fixed function only, which the Vulkan renderer does not translate.
    if (!effect->Technique)
        return;
    UINT width, height;
    device::SurfaceSize(target, width, height);
    device::SetTarget(target, nullptr, static_cast<int>(width), static_cast<int>(height));
    BeginEffect(effect);
    bMatrix4 identity;
    SetIdentity(identity);
    eViewPlatInfo *plat = eViews[0].PlatInfo;
    plat->WorldViewMatrix = identity;
    D3DXMatrixMultiply(&plat->WorldScreenMatrix, &plat->WorldViewMatrix, &plat->ViewScreenMatrix);
    plat->ViewScreenMatrix = identity;
    D3DXMatrixMultiply(&plat->WorldScreenMatrix, &plat->WorldViewMatrix, &plat->ViewScreenMatrix);
    eSetTransform(&identity, 0);
    ePoly poly;
    const float hx = -0.5f / static_cast<float>(width), hy = -0.5f / static_cast<float>(height);
    const float corners[4][2] = {{0.0f, 0.0f}, {640.0f, 0.0f}, {640.0f, 480.0f}, {0.0f, 480.0f}};
    const float uvs[4][2] = {{-hx, -hy}, {1.0f - hx, -hy}, {1.0f - hx, 1.0f - hy}, {-hx, 1.0f - hy}};
    for (int i = 0; i < 4; ++i)
    {
        poly.Vertices[i][0] = corners[i][0];
        poly.Vertices[i][1] = corners[i][1];
        poly.UVs[i][0] = uvs[i][0];
        poly.UVs[i][1] = uvs[i][1];
        poly.Colours[i][0] = static_cast<unsigned char>(colour >> 16);
        poly.Colours[i][1] = static_cast<unsigned char>(colour >> 8);
        poly.Colours[i][2] = static_cast<unsigned char>(colour);
        poly.Colours[i][3] = static_cast<unsigned char>(colour >> 24);
    }
    device::SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device::SetRenderState(D3DRS_ZENABLE, FALSE);
    if (texture)
        effect->Effect->SetTexture(effect->Parameter(kParamDiffuseMap), static_cast<IDirect3DBaseTexture9 *>(texture));
    else if (!keepStates)
        sub_6C6CC0(GetTextureInfo(bStringHash("WHITE16X16_NOALPHA"), 1, 0), 0);
    else
        device::SetTexture(0, nullptr);
    if (!keepStates)
    {
        eSetAlphaTest(0, 1);
        eSetAlphaBlend(static_cast<unsigned char>(blend), source, destination);
        eCommitBlendState();
    }
    eViewPlatInterface::Render(&poly, nullptr, 0);
    EndEffect(effect);
    device::SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    device::SetRenderState(D3DRS_ZENABLE, TRUE);
}
