/**
 * The Xbox 360 effects for a model's strips (X360 Stuff's Rendering::eViewPlatInterface_Render_Hook 0x1002f930, which
 * runs before SimpleModelAnim::Animate in eViewPlatInterface::Render, its only caller): normal-mapped car parts and
 * their shadow map effect, tree draw order, the sea, glossy windows on particular buildings and the grass shells. The
 * changes are made to the solid once; its flag then says it is done.
 */
#include "game/gen/api.h"
#include "game/platform/Effect.h"
#include "game/platform/Platform.h"
#include "legacy/link.h"
#include "engine/rendering/x360/render.h"

namespace
{
/** Solids whose 0x4f384a54 texture is a glossy window. */
const unsigned int kGlossySolids[40] = {
    0x24ed5f32, 0xe2a75534, 0xbd2cb743, 0xb01e6563, 0x22a7ec1a, 0x0a26f404, 0xdaf8d6b8, 0x32e8086b,
    0x32e8086a, 0x32e80869, 0x2e85ae80, 0xcfb266fa, 0xcfb266f9, 0x2e87e004, 0x2f4b67fc, 0x2f4b67fb,
    0x053e1712, 0x07933fb3, 0x42eb5bd2, 0x42eacf71, 0xeccd3e96, 0xeccd3e95, 0xeccd3e94, 0xeccd3e93,
    0xadcce5b2, 0xadcd7213, 0xadcdfe74, 0xadce8ad5, 0x069b7613, 0x069b7612, 0x069b7611, 0x069b7610,
    0x069b760f, 0x069b760e, 0x2f4b67fa, 0x2f4b67f9, 0x09e86854, 0x0c3d90f5, 0x32e8086c, 0x32e80868,
};
const unsigned int kGlossyTexture = 0x4f384a54;
const unsigned int kSeaTexture = 0x636c197d;

/** Grass textures that get the shell effects (0x1004a8d0). */
const unsigned int kGrassTextures[52] = {
    0x2e89693c, 0xa0a80048, 0x6aa93d1e, 0xe0dba68c, 0xae8358b3, 0xe150af06, 0x23f259a6, 0x4ca3caf1, 0x94234913,
    0x2f11023a, 0x2e082f4b, 0x4404ff0f, 0x5d161ee1, 0x282e0ce7, 0x23e89206, 0x23ea3729, 0x7bc13b0c, 0x0b3a4f9c,
    0xab7ee48b, 0x28f9d695, 0x6f6adeb4, 0x030f2699, 0x030f269a, 0x0310cbbc, 0xaaaba4b6, 0x4724f701, 0x9717031d,
    0x7d096209, 0xe4ea131f, 0x5b1c7c8d, 0x761cfb14, 0x49de48e7, 0xc9f5ec67, 0xe834aa72, 0xf34f8d74, 0xf257c5fb,
    0x633b764c, 0x074bc2d0, 0x0319b1a2, 0xc3beec68, 0x319561a7, 0x319706ca, 0x896e0aad, 0x1908a9dd, 0x5c5c9cac,
    0xc48ab616, 0x156e7175, 0x10bbf63a, 0x10bbf63b, 0x10bd9b5d, 0x50af3777, 0x7a676a62,
};

/** A strip of a solid: its texture indices and effect. */
struct Strip
{
    char unknown_00[0x18];
    signed char textures[5];     // 0x18
    char unknown_1d[0x13];
    int effectId;                // 0x30
    eEffect *effect;             // 0x34
    char unknown_38[0x30];
};

static_assert(sizeof(Strip) == 0x68, "Strip layout");

bool Contains(const unsigned int *table, int count, unsigned int hash)
{
    for (int i = 0; i < count; ++i)
        if (table[i] == hash)
            return true;
    return false;
}

unsigned int ClassHash(const char *name)
{
    return bStringHash(name);
}

char *Field(void *object, int offset)
{
    return static_cast<char *>(object) + offset;
}

/** The solid's texture for one of a strip's indices. */
TextureInfo *TextureAt(char *solid, int index)
{
    char *table = *reinterpret_cast<char **>(solid + 0x2c);
    return *reinterpret_cast<TextureInfo **>(table + 8 * index + 4);
}

/** Assigns the Xbox 360 effects to a solid's strips (once). */
void AssignEffects(char *solid)
{
    static const unsigned int treeLeaves = ClassHash("Tree Leaves");
    static const unsigned int treeCards = ClassHash("Tree Cards");
    static const unsigned int multiPassBlend = ClassHash("MultiPass Blend");
    char *plat = *reinterpret_cast<char **>(solid);
    const int count = *reinterpret_cast<int *>(plat + 0x10);
    Strip *strips = *reinterpret_cast<Strip **>(plat + 0x14);
    unsigned char &flag = *reinterpret_cast<unsigned char *>(solid + 0xd);
    const unsigned int solidName = *reinterpret_cast<unsigned int *>(solid + 0x10);
    bool changed = false;
    for (int i = 0; i < count; ++i)
    {
        Strip &s = strips[i];
        if (!s.effect)
            continue;
        const int type = s.effect->Type;
        TextureInfo *texture = TextureAt(solid, s.textures[0]);
        if (type == EFFECT_CAR || type == EFFECT_CAR_DEPTH || type == EFFECT_CAR_SHADOW_MAP)
        {
            // A second texture makes a car part normal mapped (or, in the shadow map, the car shadow effect's).
            if (s.textures[0] != s.textures[1] && TextureAt(solid, s.textures[1]) != DefaultTextureInfo)
            {
                if (g_CurrentRenderTarget->ID == 4)
                {
                    s.effect = g_Effects[EFFECT_CAR_SHADOW_MAP];
                }
                else
                {
                    s.effectId = EFFECT_CAR_DEPTH;
                    s.effect = g_Effects[EFFECT_CAR_DEPTH];
                }
            }
            continue;
        }
        if (!texture)
            continue;
        char *t = reinterpret_cast<char *>(texture);
        const unsigned int textureClass = *reinterpret_cast<unsigned int *>(t + 0x28);
        if (textureClass == treeLeaves || textureClass == treeCards || textureClass == multiPassBlend)
        {
            // Fixed draw order values for the trees instead of depth sorting.
            if (!nfs::x360::options::kFixedTreeRenderOrder)
                *Field(t, 0x54) = 2;
            else if (*reinterpret_cast<signed char *>(t + 0x51) < 3)
                *Field(t, 0x51) = static_cast<char>((textureClass != treeCards) + 3);
            *reinterpret_cast<short *>(t + 0x55) = 1;
            changed = true;
            continue;
        }
        if (type == EFFECT_WORLD && texture->NameHash == kSeaTexture)
        {
            s.effect = g_Effects[EFFECT_WORLD_DEPTH];
            changed = true;
            continue;
        }
        if (s.effectId != EFFECT_GLOSSY_WINDOW && texture->NameHash == kGlossyTexture &&
            Contains(kGlossySolids, 40, solidName))
        {
            s.effectId = EFFECT_GLOSSY_WINDOW;
            s.effect = g_Effects[EFFECT_GLOSSY_WINDOW];
            for (int k = 0; k < 5; ++k)
                *Field(TextureAt(solid, s.textures[k]), 0x54) = 2;
            changed = true;
            continue;
        }
        if (!nfs::x360::options::kGrassShader || flag == 2 ||
            !(type == EFFECT_WORLD || type == EFFECT_WORLD_NORMAL_MAP || type == EFFECT_WORLD_REFLECT))
            continue;
        changed = true;
        if (!Contains(kGrassTextures, 52, texture->NameHash))
            continue;
        if (s.effect->Type == EFFECT_WORLD)
        {
            if (texture == TextureAt(solid, s.textures[3]))
                s.effectId = EFFECT_TREE_DEPTH;
            s.effect = g_Effects[EFFECT_TREE_DEPTH];
        }
        else
        {
            s.effectId = EFFECT_WORLD_NORMAL_DEPTH;
            s.effect = g_Effects[EFFECT_WORLD_NORMAL_DEPTH];
        }
    }
    if (changed && flag)
        flag = 2;
}
} // namespace

void SimpleModelAnim::Animate(eModel *model, eSolid *solid, bMatrix4 *matrix)
{
    AssignEffects(reinterpret_cast<char *>(solid));
    NFS_ORIGINAL(0x73d3c0, void(__cdecl *)(eModel *, eSolid *, bMatrix4 *))(model, solid, matrix);
}
