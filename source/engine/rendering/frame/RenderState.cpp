/**
 * Render state helpers the draw code shares (original: zPlatform eSetAlphaTest 0x6c67c0, eSetAlphaBlend 0x6c6810, the
 * texture setup 0x6c68b0, unbinding all textures 0x6c00b0 and the texture transform reset 0x6c5c90). While an effect is current, the alpha test and blending
 * go into the BlendState array that eCommitBlendState hands to the effect; without one they are render states. With
 * Direct3D 9 these run the original code.
 */
#include "game/gen/api.h"
#include "game/platform/Effect.h"
#include "game/platform/Platform.h"
#include "legacy/link.h"
#include "engine/rendering/vulkan/renderer.h"

/** A texture animation's fraction (source/engine/resources/EcstasyMisc.cpp). */
float __stdcall AnimationFraction(float time, float rate, int mode, float step);


NFS_REPLACE(0x6c68b0, sub_6C68B0)
NFS_REPLACE(0x6c00b0, sub_6C00B0)
NFS_REPLACE(0x6c5c90, sub_6C5C90)

/** BlendState: alpha test enable and reference, then alpha blend enable, source and destination blend. */
extern "C" int dword_93DE20[5];
/** Back-face culling on: for opaque textures without alpha test, and for the car effect's blended ones. */
extern "C" unsigned char byte_8FAE44;
/** Culling of the current pass: 1 none, 2 the normal winding, anything else the reverse. */
extern "C" int dword_8FAE6C;
/** Set while the current draws use a texture scroll, so the next texture resets the effect's offset. */
extern "C" int dword_982CCC;
/** A UV scroll that overrides the texture's own (in texels) and its two extra parameters. */
extern "C" float dword_982C98;
extern "C" float dword_982C9C;
extern "C" int dword_8FAE48;
extern "C" int dword_8FAE4C;
extern "C" int AnimatedTextureEnable;

extern "C" void __cdecl sub_6C68B0(TextureInfo *texture, unsigned int flags);
extern "C" void __cdecl sub_6C00B0();
extern "C" void __cdecl sub_6C5C90();

namespace
{
const int kParamBlendState = 0x9c;
const int kParamCullMode = 0x1dc;
const int kParamDiffuseMap = 0x22c;
const int kParamTextureOffset = 0xed4;
const int kParamTextureOffsetMatrix = 0xefc;

const int kAlphaRef = 0x5f;
/** Flags bit of a mirrored model, whose faces wind the other way. */
const unsigned int kFlagMirrored = 1u << 18;

/** Source and destination blend of each TextureInfo::AlphaBlendType after 0 (opaque). */
const int kBlendFactors[6][2] = {
    {D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA},   {D3DBLEND_SRCALPHA, D3DBLEND_ONE},
    {D3DBLEND_ZERO, D3DBLEND_INVSRCCOLOR},       {D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA},
    {D3DBLEND_DESTALPHA, D3DBLEND_INVDESTALPHA}, {D3DBLEND_DESTALPHA, D3DBLEND_ONE},
};

float ScrollOffset(TextureInfo *texture, float time, float texels, short size)
{
    float step;
    memcpy(&step, &dword_8FAE4C, sizeof(step));
    return AnimationFraction(time, texels / size, dword_8FAE48, step);
}
} // namespace

void eSetAlphaTest(int enable, int reference)
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    if (g_CurrentEffect)
    {
        dword_93DE20[0] = enable;
        dword_93DE20[1] = reference;
        return;
    }
    r->SetRenderState(D3DRS_ALPHATESTENABLE, enable);
    r->SetRenderState(D3DRS_ALPHAREF, reference);
}

void eSetAlphaBlend(int enable, int source, int destination)
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    if (g_CurrentEffect)
    {
        dword_93DE20[2] = enable;
        dword_93DE20[3] = source;
        dword_93DE20[4] = destination;
        return;
    }
    r->SetRenderState(D3DRS_ALPHABLENDENABLE, enable);
    r->SetRenderState(D3DRS_SRCBLEND, source);
    r->SetRenderState(D3DRS_DESTBLEND, destination);
}

/**
 * Prepares a texture for the next draw: binds it as the effect's DiffuseMap (sampler 0 without an effect), sets its
 * alpha test, addressing and blending, the cull mode that goes with the blending, and the UV scroll of animated
 * textures. The cull mode, blending and scroll need a current effect, as in the original.
 */
extern "C" void __cdecl sub_6C68B0(TextureInfo *texture, unsigned int flags)
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    void *image = texture->PlatInfo->Texture;
    if (!image)
        return;
    eEffect *effect = g_CurrentEffect;
    if (effect)
        effect->Effect->SetTexture(effect->Parameter(kParamDiffuseMap), static_cast<IDirect3DBaseTexture9 *>(image));
    else
        r->SetTexture(0, image);
    const bool alphaTest = texture->AlphaUsageType == 1;
    eSetAlphaTest(alphaTest, alphaTest ? kAlphaRef : 0);
    r->SetSamplerState(0, D3DSAMP_ADDRESSU, (texture->TilableUV & 1) ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP);
    r->SetSamplerState(0, D3DSAMP_ADDRESSV, (texture->TilableUV & 2) ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP);

    bool cull;
    const char blendType = texture->AlphaBlendType;
    r->SetDrawBlend(blendType);
    if (blendType == 0)
    {
        r->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        eSetAlphaBlend(0, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA);
        cull = !alphaTest && !(texture->Flags57 & 2) && byte_8FAE44;
    }
    else
    {
        const int *factors = blendType >= 1 && blendType <= 6 ? kBlendFactors[blendType - 1] : kBlendFactors[0];
        eSetAlphaBlend(1, factors[0], factors[1]);
        cull = g_CurrentEffect->Type == EFFECT_CAR && byte_8FAE44;
        r->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    }

    effect = g_CurrentEffect;
    if (D3DXHANDLE cullMode = effect->Parameter(kParamCullMode))
    {
        int mode = D3DCULL_NONE;
        if (cull && dword_8FAE6C != 1)
            mode = ((flags & kFlagMirrored) != 0) != (dword_8FAE6C == 2) ? D3DCULL_CW : D3DCULL_CCW;
        effect->Effect->SetInt(cullMode, mode);
        effect = g_CurrentEffect;
    }
    effect->Effect->SetIntArray(effect->Parameter(kParamBlendState), dword_93DE20, 5);

    effect = g_CurrentEffect;
    float offset[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (effect->Parameter(kParamTextureOffset))
    {
        if (AnimatedTextureEnable)
        {
            const float time = static_cast<float>(WorldTimer.PackedTime) * 0.00025f;
            if (texture->ScrollType == 3)
            {
                offset[0] = static_cast<float>(texture->ScrollSpeedS) * -0.0009765625f;
                offset[1] = static_cast<float>(texture->ScrollSpeedT) * -0.0009765625f;
            }
            else if (dword_982C98 == 0.0f && dword_982C9C == 0.0f)
            {
                offset[0] = NFS_ORIGINAL(0x6bc660, float(__thiscall *)(TextureInfo *, float))(texture, time);
                offset[1] = NFS_ORIGINAL(0x6bc6a0, float(__thiscall *)(TextureInfo *, float))(texture, time);
            }
            else
            {
                offset[0] = ScrollOffset(texture, time, dword_982C98, texture->Width);
                offset[1] = ScrollOffset(texture, time, dword_982C9C, texture->Height);
            }
        }
        dword_982CCC = 1;
    }
    if (effect && dword_982CCC && effect->Parameter(kParamTextureOffset))
    {
        bMatrix4 transform;
        transform.v0 = bVector4(1.0f, 0.0f, 0.0f, 0.0f);
        transform.v1 = bVector4(0.0f, 1.0f, 0.0f, 0.0f);
        transform.v2 = bVector4(offset[0], offset[1], 1.0f, 0.0f);
        transform.v3 = bVector4(0.0f, 0.0f, 0.0f, 1.0f);
        effect->Effect->SetMatrix(effect->Parameter(kParamTextureOffsetMatrix), &transform);
        g_CurrentEffect->Effect->SetVector(g_CurrentEffect->Parameter(kParamTextureOffset), offset);
    }
    if (texture->ScrollType == 0)
        dword_982CCC = 0;
}

/** Unbinds the textures of the first eight samplers. */
extern "C" void __cdecl sub_6C00B0()
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    for (uint32_t stage = 0; stage < 8; ++stage)
        r->SetTexture(stage, nullptr);
}

/** Turns off the texture transforms of the first four stages. The Vulkan shaders have the passes' projected sampling
 * built in, so there is nothing to reset. */
extern "C" void __cdecl sub_6C5C90()
{
}
