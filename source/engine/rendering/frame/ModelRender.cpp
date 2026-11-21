/**
 * A model's draw (original: zPlatform eViewPlatInterface::Render 0x6da9b0, for the scenery, props, cars and every
 * other model of a view): the model's replacement textures go into its solid for the time of the draw (zEcstasy
 * eModel::ApplyReplacementTextureTable 0x504f70 and RestoreReplacementTextureTable 0x4fb710), and each strip that is in
 * view is queued in the render list (sub_6E0AB0) with the effect the current render target draws it with.
 *
 * Change: in the sun shadow pass (dword_982B38) the original moved the model's and every strip's box into the shadow
 * view for its caster test (sub_6D7A40), which returns at once while that test is off (dword_8FAF14), as the cascades
 * leave it. The boxes are moved only when the test is on.
 */
#include "game/gen/api.h"
#include "game/platform/D3DX.h"
#include "game/platform/Effect.h"
#include "game/platform/Platform.h"
#include "game/platform/Solid.h"
#include "legacy/link.h"

/** Set while the sun shadow pass draws. */
extern "C" int dword_982B38;
/** The original shadow pass's caster test is on. */
extern "C" int dword_8FAF14;
/** The shadow view's matrix the caster test moves boxes by. */
extern "C" bMatrix4 *dword_982D04;
/** Strips whose diffuse texture has this name hash are not drawn. */
extern "C" unsigned int dword_982D40;
/** Statistics: strips queued, effect changes, models drawn, and models by their strip and effect change counts (the
 * last of each counts seven or more). */
extern "C" int dword_982B90;
extern "C" int dword_982B98;
extern "C" int dword_982B94;
extern "C" int dword_9828A0[8];
extern "C" int dword_93DE38[8];

extern "C" bool __cdecl sub_6D7A40(bVector3 *min, bVector3 *max);
extern "C" void __cdecl sub_6E0AB0(void *strip, SolidPlatInfo **solid, unsigned int flags, eEffect *effect,
                                   TextureInfo **textures, bMatrix4 *transform, void *lightContext,
                                   void *lightMaterial, void *blendMatrices);
int __fastcall VisibleStateSB(void *view, void *, const bVector3 *min, const bVector3 *max, bMatrix4 *matrix);

namespace
{
/** Render target 4: the original shadow map. */
const int kShadowMapTarget = 4;
/** Render flags: no visibility test for the strips; the shadow volume effect for world strips. */
const unsigned int kRenderNoCull = 4;
const unsigned int kRenderShadowVolume = 0x20000;
const unsigned char kDefaultLightMaterial = 0xff;
/** Models that are never drawn, drawn only with the sky effect's shaders, or only without them. */
const unsigned int kHiddenModel = 0x90f70174;
const unsigned int kShaderSkyModel = 0xed85ba82;
const unsigned int kFixedSkyModel = 0xbe43edbb;
/** The most textures a model replaces (the original's room on the stack). */
const int kMaxReplacedTextures = 131;

/** The original caster test of the shadow pass for a box in the model's space; true while that test is off. */
bool CastsShadow(bMatrix4 *matrix, bVector3 &min, bVector3 &max)
{
    if (!dword_8FAF14)
        return true;
    bMatrix4 shadow;
    D3DXMatrixMultiply(&shadow, matrix, dword_982D04);
    NFS_ORIGINAL(0x6be800, void(__cdecl *)(bMatrix4 *, bVector3 *, bVector3 *))(&shadow, &min, &max);
    return sub_6D7A40(&min, &max);
}

/** The effect a strip draws with in the current render target: the original shadow map has its own car and no fog
 * variants, the other targets but the screen (the reflections) take the no fog ones. */
eEffect *TargetEffect(eEffect *effect, unsigned int flags)
{
    const int target = g_CurrentRenderTarget->ID;
    if (target == kShadowMapTarget)
    {
        if (effect->Type == EFFECT_CAR)
            effect = g_Effects[EFFECT_CAR_SHADOW_MAP];
        const int type = effect->Type;
        if (type == EFFECT_WORLD || type == EFFECT_GLOSSY_WINDOW)
            effect = g_Effects[EFFECT_WORLD_NO_FOG];
        else if (type == EFFECT_WORLD_NORMAL_MAP || type == EFFECT_WORLD_REFLECT)
            effect = g_Effects[EFFECT_NORMAL_MAP_NO_FOG];
    }
    else if (target)
    {
        const int type = effect->Type;
        if (type == EFFECT_WORLD)
            effect = g_Effects[EFFECT_WORLD_NO_FOG];
        else if (type == EFFECT_WORLD_NORMAL_MAP || type == EFFECT_WORLD_REFLECT)
            effect = g_Effects[EFFECT_NORMAL_MAP_NO_FOG];
    }
    if ((flags & kRenderShadowVolume) && (effect->Type == EFFECT_WORLD || effect->Type == EFFECT_WORLD_NO_FOG))
        effect = g_Effects[EFFECT_SHADOW_CG];
    return effect;
}
} // namespace

void eModel::ApplyReplacementTextureTable(TextureInfo ***replaced)
{
    eReplacementTextureTable *table = pReplacementTextureTable;
    const int count = NumReplacementTextures;
    if (!table || !count)
        return;
    eSolid *solid = Solid;
    for (int i = 0; i < count; ++i)
    {
        replaced[i] = nullptr;
        if (table[i].pTextureInfo == reinterpret_cast<TextureInfo *>(-1))
            table[i].pTextureInfo = GetTextureInfo(table[i].hNewNameHash, 1, 0);
    }
    eTextureEntry *textures = solid->pTextureTable;
    const int entries = solid->NumTextureTableEntries;
    for (int s = 0; s < entries; ++s)
    {
        const unsigned int hash = textures[s].NameHash;
        for (int i = 0; i < count; ++i)
        {
            if (table[i].hNewNameHash && table[i].hOldNameHash == hash)
            {
                TextureInfo *own = textures[s].Texture;
                textures[s].Texture = table[i].pTextureInfo;
                replaced[i] = &textures[s].Texture;
                table[i].pTextureInfo = own;
                break;
            }
        }
    }
}

void eModel::RestoreReplacementTextureTable(TextureInfo ***replaced)
{
    eReplacementTextureTable *table = pReplacementTextureTable;
    const int count = NumReplacementTextures;
    if (!table || !Solid || !count)
        return;
    for (int i = 0; i < count; ++i)
    {
        if (TextureInfo **slot = replaced[i])
        {
            TextureInfo *own = *slot;
            *slot = table[i].pTextureInfo;
            table[i].pTextureInfo = own;
        }
    }
}

void eViewPlatInterface::Render(eModel *model, bMatrix4 *matrix, eLightContext *lightContext, unsigned int flags,
                                bMatrix4 *blendMatrices)
{
    const unsigned int name = model->NameHash;
    if (name == kHiddenModel)
        return;
    if (name == kShaderSkyModel ? !g_Effects[EFFECT_SKY]->HasShaders
                                : name == kFixedSkyModel && g_Effects[EFFECT_SKY]->HasShaders)
        return;
    eSolid *solid = model->Solid;
    if (!solid)
        return;
    SolidPlatInfo *plat = solid->PlatInfo;
    if (!plat->IndexBuffer)
        return;
    SimpleModelAnim::Animate(model, solid, matrix);
    const bool shadowPass = dword_982B38 != 0;
    if (shadowPass)
    {
        bVector3 min = {solid->AABBMinX, solid->AABBMinY, solid->AABBMinZ};
        bVector3 max = {solid->AABBMaxX, solid->AABBMaxY, solid->AABBMaxZ};
        if (!CastsShadow(matrix, min, max))
            return;
    }
    TextureInfo **replaced[kMaxReplacedTextures];
    if (model->pReplacementTextureTable)
        model->ApplyReplacementTextureTable(replaced);

    int queued = 0, effectChanges = 0, lastType = -1;
    const bool cull = !(flags & kRenderNoCull);
    eTextureEntry *textureTable = solid->pTextureTable;
    SolidStrip *strip = plat->Strips;
    for (int i = 0, count = plat->StripCount; i < count; ++i, ++strip)
    {
        const bVector3 *stripMin = reinterpret_cast<const bVector3 *>(strip->Min);
        const bVector3 *stripMax = reinterpret_cast<const bVector3 *>(strip->Max);
        if (cull && !(strip->Flags & 1) && !VisibleStateSB(this, nullptr, stripMin, stripMax, matrix))
            continue;
        if (shadowPass)
        {
            bVector3 min = *stripMin, max = *stripMax;
            if (!CastsShadow(matrix, min, max))
                continue;
        }
        eEffect *effect = TargetEffect(strip->Effect, flags);
        TextureInfo *textures[5];
        bool missing = false;
        for (int t = 0; t < 5; ++t)
        {
            textures[t] = textureTable[strip->TextureIndices[t]].Texture;
            missing |= !textures[t];
        }
        if (missing || !effect->Technique)
        {
            if (effect->Type != EFFECT_GLOSSY_WINDOW)
                continue;
            effect = g_Effects[EFFECT_WORLD];
        }
        if (!textures[0] || textures[0]->NameHash == dword_982D40)
            continue;
        ++queued;
        if (effect->Type != lastType)
        {
            ++effectChanges;
            lastType = effect->Type;
        }
        void *lightMaterial = strip->LightMaterialIndex == kDefaultLightMaterial
                                  ? elGetDefaultLightMaterial()
                                  : solid->LightMaterialTable[strip->LightMaterialIndex].LightMaterial;
        if (effect->Type == EFFECT_CAR && !lightContext)
            lightContext = *reinterpret_cast<eLightContext **>(reinterpret_cast<char *>(this) + 0x5c);
        sub_6E0AB0(strip, &solid->PlatInfo, flags, effect, textures, matrix, lightContext, lightMaterial,
                   blendMatrices);
    }
    dword_982B90 += queued;
    dword_982B98 += effectChanges;
    if (queued > 0)
        ++dword_982B94;
    ++dword_9828A0[queued < 7 ? queued : 7];
    ++dword_93DE38[effectChanges < 7 ? effectChanges : 7];
    model->RestoreReplacementTextureTable(replaced);
}
