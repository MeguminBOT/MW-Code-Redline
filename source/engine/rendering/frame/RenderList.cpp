/**
 * The render list draw (original: zPlatform 0x6e0200): the views queue their solids' strips in a list that sub_6E2F50
 * sorts and this draws, changing effect only when the next entry needs another one. Immediate triangle strips
 * (eStripSlotPool) are drawn from memory and freed. With the Xbox 360 effects (X360 Stuff's patches to 0x6e0200) the
 * pass setup chooses the techniques, not the list: an effect starts with the Xbox 360 Begin flags, a solid's textures go
 * through eDataRender, and every pass of a strip is drawn without committing again, the effect going back to its first
 * pass before the next multi-pass strip of the same type.
 *
 * Changes for the Vulkan renderer: a run of strips that share a multi-pass effect is drawn a pass at a time
 * (DrawPassMajor), not a strip at a time; a run of strips with the same effect, textures, flags and lights, which only
 * differ in strip and transform, is one block run (DrawBlockRun, docs/NATIVE_WORLD.md): set up and committed once,
 * each entry's transform registers in a block of its own, and the entries of one strip (lamps, fences, signs) one
 * instanced draw. Within each sort key the entries of a strip are brought together (GroupStrips).
 */
#include <xmmintrin.h>

#include "game/gen/api.h"
#include "game/platform/Device.h"
#include "game/platform/Platform.h"
#include "game/platform/Solid.h"
#include "core/config.h"
#include "engine/rendering/culling/culling.h"
#include "legacy/link.h"
#include "core/log.h"
#include "dev/profile.h"
#include "gameplay/world/timeofday.h"
#include "engine/rendering/vulkan/d3dx_effect.h"
#include "engine/rendering/vulkan/renderer.h"
#include "engine/rendering/x360/render.h"
#include "engine/rendering/x360/shader.h"

NFS_REPLACE(0x6e0200, sub_6E0200)
NFS_REPLACE(0x6e0ab0, sub_6E0AB0)
NFS_REPLACE(0x6e2f50, sub_6E2F50)

extern "C" RenderListEntry unk_93E878[];
/** The list in draw order: entry index and sort key pairs. */
extern "C" unsigned int *dword_9877B4;
extern "C" int dword_982CDC;
extern "C" int dword_982CD8;
/** Entries queued since the list was last sorted (-1 in dword_8FAE70 starts a new run). */
extern "C" int dword_982CD4;
extern "C" int dword_8FAE70;
/** The vertex and index buffers bound last (the draws skip binding them again). */
extern "C" void *dword_982CA0;
extern "C" void *dword_982CA4;
/** Strips whose solids keep vertices in memory get vertex buffers made, at most three solids a frame. */
extern "C" int dword_982CE4;
extern "C" int lpData_00901830;
extern "C" float flt_8FAE68;
/** How far the camera is into a tunnel (0 to 1). */
extern "C" float flt_982BA8;
extern "C" SlotPool *dword_982AC0;
/** Statistics: vertex and index buffer changes, draws, immediate strips, triangles. */
extern "C" int dword_987B08;
extern "C" int dword_987B0C;
extern "C" int dword_987B00;
extern "C" int dword_987B04;
extern "C" int dword_987B10;
/** Texture changes (sub_6C6D10's statistic) and the diffuse texture bound last. */
extern "C" int dword_987B30;
extern "C" TextureInfo *dword_982CB4;

void __cdecl eDataRender(TextureInfo **textures, unsigned int flags);
extern "C" void __cdecl sub_6C7420(void *lightContext, bMatrix4 *transform);
extern "C" void __cdecl sub_6C76E0(void *lightMaterial, unsigned int flags);
extern "C" void __cdecl sub_6C7F50(void *blendMatrices, SolidStrip *strip);
extern "C" void __cdecl sub_6E0200();
/** The depth sort key of a strip with transparent textures. */
extern "C" unsigned int __cdecl sub_6C6F30(int immediate, void *strip, bMatrix4 *transform);
extern "C" void __cdecl sub_6E0AB0(void *strip, SolidPlatInfo **solid, unsigned int flags, eEffect *effect,
                                   TextureInfo **textures, bMatrix4 *transform, void *lightContext,
                                   void *lightMaterial, void *blendMatrices);
int __fastcall SolidCreateVertexBuffers(SolidPlatInfo *plat);

extern "C" void __cdecl sub_6E2F50();
/** std::sort of the list's (entry index, sort key) pairs by key, then index (the original's sort). */
extern "C" void __cdecl sub_6E2AC0(unsigned int *first, unsigned int *last, int count);

namespace
{
const int kParamInvShadowStrength = 0x86c;

/** The effects with a technique per shadow level (the world, car, glossy window and particle effects). */
bool HasLevelTechniques(int type)
{
    return type == EFFECT_WORLD || type == EFFECT_WORLD_REFLECT || type == EFFECT_WORLD_NORMAL_MAP ||
           type == EFFECT_CAR || type == EFFECT_GLOSSY_WINDOW || type == EFFECT_PARTICLES;
}

/** The effect type and pass count of the last strip drawn (X360 Stuff's multi-pass fix). */
int s_lastType = -1;
int s_lastPasses;

void EndEffect(eEffect *effect)
{
    effect->Effect->EndPass();
    effect->End();
    effect->Effect->End();
    g_CurrentEffect = nullptr;
}

void StartEffect(eEffect *effect)
{
    g_CurrentEffect = effect;
    effect->Start();
    device::SetVertexDeclaration(effect);
    if (ID3DXEffect *e = nfs::x360::ShaderFor(effect).Effect())
    {
        e->Begin(nullptr, nfs::x360::g_beginFlags);
        e->BeginPass(0);
    }
}

/** Binds a strip's buffers (and sets its bone matrices); false when it is not drawn: its vertex buffers are yet to be
 * made, or it has no indices or too many. */
bool BindStrip(const RenderListEntry &entry, SolidPlatInfo *plat)
{
    SolidStrip *strip = static_cast<SolidStrip *>(entry.Strip);
    if (entry.BlendMatrices)
        sub_6C7F50(entry.BlendMatrices, strip);
    if (!strip->VertexBuffer && strip->IndexCount)
    {
        if (dword_982CE4 >= 3 || !plat->IndexBuffer)
            return false;
        SolidCreateVertexBuffers(plat);
        ++dword_982CE4;
    }
    if (strip->VertexBuffer != dword_982CA0)
    {
        device::SetStreamSource(0, strip->VertexBuffer, 0, static_cast<UINT>(g_CurrentEffect->Stride));
        dword_982CA0 = strip->VertexBuffer;
        ++dword_987B08;
    }
    if (plat->IndexBuffer != dword_982CA4)
    {
        device::SetIndices(plat->IndexBuffer);
        dword_982CA4 = plat->IndexBuffer;
        ++dword_987B0C;
    }
    return plat->IndexCount && strip->IndexCount < 0xFFFF;
}

/** One draw of a bound strip. */
void DrawBound(const SolidStrip *strip, int type)
{
    static int counts[2][32];
    static int frameCounter;
    static const bool debug = nfs::vk::HasArgument("-debugviews");
    if (debug)
    {
        const int view = g_CurrentRenderTarget->ViewID == 15 ? 1 : 0;
        ++counts[view][type & 31];
        if (g_CurrentRenderTarget->ViewID == 15 && counts[1][31] != static_cast<int>(eFrameCounter))
        {
            counts[1][31] = static_cast<int>(eFrameCounter);
            if (++frameCounter == 4000)
                for (int v = 0; v < 2; ++v)
                    nfs::Log("draws view %s: %d %d %d %d %d %d %d %d | %d %d %d %d %d %d %d %d | %d %d %d %d", v ? "shadow" : "other",
                             counts[v][0], counts[v][1], counts[v][2], counts[v][3], counts[v][4], counts[v][5],
                             counts[v][6], counts[v][7], counts[v][8], counts[v][9], counts[v][10], counts[v][11],
                             counts[v][12], counts[v][13], counts[v][14], counts[v][15], counts[v][16],
                             counts[v][17], counts[v][18], counts[v][19]);
            if (frameCounter == 3999)
                memset(counts, 0, sizeof(int) * 31), memset(counts[1], 0, sizeof(int) * 31);
        }
    }
    device::DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, strip->VertexCount, strip->StartIndex,
                                 strip->TriangleCount);
    ++dword_987B00;
    if (nfs::profile::g_enabled)
        nfs::profile::Count(nfs::profile::kCounterPasses, 1);
}

void DrawStrip(const RenderListEntry &entry, SolidPlatInfo *plat)
{
    if (!BindStrip(entry, plat))
        return;
    const SolidStrip *strip = static_cast<const SolidStrip *>(entry.Strip);
    const int passes = entry.Effect->PassCount;
    const eEffect *current = g_CurrentEffect;
    const int type = current->Type;
    current->Effect->CommitChanges();
    if (type == s_lastType && s_lastPasses > 1)
    {
        current->Effect->EndPass();
        current->Effect->BeginPass(0);
    }
    int pass = 0;
    for (; pass < passes; ++pass)
    {
        if (pass > 0)
        {
            entry.Effect->Effect->EndPass();
            entry.Effect->Effect->BeginPass(static_cast<UINT>(pass));
        }
        DrawBound(strip, type);
    }
    s_lastPasses = pass;
    s_lastType = g_CurrentEffect->Type;
    dword_987B10 += strip->IndexCount / 3;
}

void Lap(long long &t, nfs::profile::Timer timer)
{
    if (!nfs::profile::g_enabled)
        return;
    const long long now = nfs::profile::Now();
    nfs::profile::AddTime(timer, now - t);
    t = now;
}

/** What an entry sets before its strip is drawn (the effect is current): its textures, transform, shadow strength,
 * light context and light material. Returns its solid's platform data (null for an immediate strip). */
SolidPlatInfo *SetUpEntry(RenderListEntry &entry, long long &t)
{
    eDataRender(entry.Textures, entry.Flags);
    Lap(t, nfs::profile::kTimerTextures);
    eSetTransform(entry.Transform, g_CurrentRenderTarget->ViewID);
    SolidPlatInfo *plat = nullptr;
    if (!entry.Immediate)
    {
        plat = *entry.Solid;
        eEffect *e = g_CurrentEffect;
        if (HasLevelTechniques(e->Type) && lpData_00901830 > 0)
            e->Effect->SetFloat(e->Parameter(kParamInvShadowStrength), flt_982BA8 + (1.0f - flt_8FAE68));
    }
    Lap(t, nfs::profile::kTimerTransform);
    if (entry.LightContext)
        sub_6C7420(const_cast<void *>(nfs::timeofday::LightContext(entry.LightContext)), entry.Transform);
    Lap(t, nfs::profile::kTimerLights);
    if (entry.LightMaterial)
        sub_6C76E0(entry.LightMaterial, entry.Flags);
    Lap(t, nfs::profile::kTimerMaterial);
    return plat;
}

/** How many entries from `order` on can be drawn a pass at a time with the first (DrawPassMajor): consecutive strips
 * of its multi-pass effect, while that has as many passes as its technique (the depth techniques of a multi-pass slot
 * keep its pass count, and draw a strip again for the passes they lack, as before); 1 otherwise. */
int PassMajorRun(const unsigned int *order, int left)
{
    static const bool enabled = nfs::config::Int("Debug", "PassMajor", 1) != 0;
    const RenderListEntry &first = unk_93E878[order[0]];
    eEffect *effect = first.Effect;
    if (!enabled || !nfs::vk::g_renderer || first.Immediate || effect->PassCount < 2 || !effect->Effect)
        return 1;
    if (static_cast<uint32_t>(effect->PassCount) > nfs::vk::NativeEffect(effect->Effect)->TechniquePasses())
        return 1;
    int n = 1;
    while (n < left && unk_93E878[order[n * 2]].Effect == effect && !unk_93E878[order[n * 2]].Immediate)
        ++n;
    return n;
}

/** True when a grass strip's shells (the passes after the first) cannot show: its solid lies wholly past LODSTART from
 * the camera, where the shell vertex shaders flag every vertex for their pixel shaders to discard (x360_grass_03_vs_3_0
 * and the others: the camera's distance in the solid's space against LODSTART). The effect holds the strip's
 * parameters. */
bool ShellsOutOfReach(const RenderListEntry &entry, eEffect *effect)
{
    const nfs::x360::Shader &shader = nfs::x360::g_shaders[effect->Type];
    float lodStart;
    if (!shader.lodStart || FAILED(effect->Effect->GetFloat(shader.lodStart, &lodStart)))
        return false;
    const eView &view = eViews[g_CurrentRenderTarget->ViewID];
    const float *solid = reinterpret_cast<const float *>(entry.Solid);
    if (!view.pCamera || !solid)
        return false;
    const bVector3 &eye = view.pCamera->CurrentKey.Position;
    float local[3] = {eye.x, eye.y, eye.z};
    if (entry.Transform)
    {
        bMatrix4 inverse;
        eInvertTransformationMatrix(&inverse, entry.Transform);
        const float *m = reinterpret_cast<const float *>(&inverse);
        for (int a = 0; a < 3; ++a)
            local[a] = eye.x * m[a] + eye.y * m[4 + a] + eye.z * m[8 + a] + m[12 + a];
    }
    float d2 = 0.0f;
    for (int a = 0; a < 3; ++a)
    {
        const float below = solid[8 + a] - local[a], above = local[a] - solid[12 + a];
        const float d = below > 0.0f ? below : above > 0.0f ? above : 0.0f;
        d2 += d * d;
    }
    // A margin for the shaders' own rounding of the distance.
    const float reach = lodStart * 1.001f + 0.01f;
    return d2 > reach * reach;
}

/** The parameters eSetTransform sets (eEffect parameter offsets), which differ between the instances of a run. */
const int kTransformParameters[] = {0xfc4, 0xf4c, 0xfec, 0x8e4, 0x90c, 0xc54};
const int kTransformParameterCount = sizeof(kTransformParameters) / sizeof(kTransformParameters[0]);
/** The most instances one draw takes (and the blocks' room). */
const int kMaxInstances = 64;
const int kMaxBlockRows = 64;

/** How many entries from `order` on can go in one block run with the first (DrawBlockRun): those of its effect (a
 * single-pass one, unless `anyPasses`: a pass-major run's pass) and its solid with the same textures, flags, light
 * context and light material, drawn whole (the same fade), each with its own strip and transform; 1 when the run
 * cannot be one. */
int BlockRun(const unsigned int *order, int left, bool anyPasses)
{
    static const bool enabled = nfs::config::Int("Debug", "InstanceRuns", 1) != 0;
    const RenderListEntry &first = unk_93E878[order[0]];
    if (!enabled || left < 2 || !nfs::vk::g_renderer || first.Immediate || first.BlendMatrices || !first.Transform ||
        (!anyPasses && first.Effect->PassCount != 1) || !first.Effect->Effect)
        return 1;
    const float fade = nfs::culling::DrawFade(first.Transform);
    int n = 1;
    while (n < left && n < kMaxInstances)
    {
        const RenderListEntry &e = unk_93E878[order[n * 2]];
        if (e.Immediate || e.Effect != first.Effect || e.Flags != first.Flags ||
            e.LightContext != first.LightContext || e.LightMaterial != first.LightMaterial || e.BlendMatrices ||
            !e.Transform || e.Textures[0] != first.Textures[0] || e.Textures[1] != first.Textures[1] ||
            e.Textures[2] != first.Textures[2] || e.Textures[3] != first.Textures[3] ||
            e.Textures[4] != first.Textures[4] || nfs::culling::DrawFade(e.Transform) != fade)
            break;
        // Strips of other solids drew darker than on their own (a road verge black next to the road, 2026-11-13; the
        // cause in the per-solid state was not found): a run keeps to one solid's strips and its instances.
        if (e.Solid != first.Solid)
            break;
        ++n;
    }
    return n;
}

/**
 * Draws `count` entries of a BlockRun as one block run, the first already set up, its strip bound and its effect
 * committed: its vertex shader's registers and the world rows are the first block. For each of the others
 * eSetTransform sets the transform's parameters, and only the registers those feed are copied into a copy of the
 * first block (with its world rows): everything else they share. Each strip is bound in turn and queued
 * (Renderer::QueueBlockDraw: the entries of one strip become instances of one draw); the renderer records the pipeline,
 * states and blocks once for all. Entries whose strip cannot be drawn (BindStrip) are left out, as before. Returns the
 * triangles drawn, or -1, with nothing drawn, when the vertex shader does not read instance blocks or the pixel shader
 * reads a transform's parameter (the blocks would give every instance the first one's).
 */
int DrawBlockRun(const unsigned int *order, int count, const SolidStrip *strip)
{
    nfs::vk::Renderer &r = *nfs::vk::g_renderer;
    const uint32_t rows = r.InstanceBlockRows();
    if (!rows || rows > static_cast<uint32_t>(kMaxBlockRows))
        return -1;
    const eEffect *current = g_CurrentEffect;
    static float blocks[kMaxInstances * kMaxBlockRows][4];
    const uint32_t registers = rows - 3;
    memcpy(blocks, r.VertexConstants(), registers * 16);
    memcpy(blocks[registers], r.WorldRows(), 3 * 16);
    nfs::vk::Effect *native = nfs::vk::NativeEffect(current->Effect);
    const int shader = r.GetVertexShader();
    int parameters[kTransformParameterCount];
    int parameterCount = 0;
    for (int p = 0; p < kTransformParameterCount; ++p)
        if (D3DXHANDLE h = current->Parameter(kTransformParameters[p]))
            parameters[parameterCount++] = nfs::vk::NativeParameter(current->Effect, h);
    uint16_t runs[32];
    // The blocks carry the vertex shader's registers only: a pixel shader that reads a transform's parameter (the eye
    // or the sun in the object's space, the shadow map's transform) would light every instance as the first. Such an
    // effect draws its entries one at a time.
    if (native->ParameterRuns(r.GetPixelShader(), parameters, parameterCount, runs, 32) > 0)
        return -1;
    const int runCount = native->ParameterRuns(shader, parameters, parameterCount, runs, 32);
    r.QueueBlockDraw(strip->StartIndex, strip->TriangleCount);
    int drawn = 1;
    int triangles = strip->IndexCount / 3;
    const void *bound = strip;
    for (int i = 1; i < count; ++i)
    {
        RenderListEntry &e = unk_93E878[order[i * 2]];
        const SolidStrip *s = static_cast<const SolidStrip *>(e.Strip);
        if (s != bound)
        {
            bound = BindStrip(e, *e.Solid) ? s : nullptr;
            if (!bound)
            {
                static const bool logSkips = nfs::config::Int("Debug", "BlockLog", 0) != 0;
                if (logSkips)
                    nfs::Log("block run: entry %d of %d skipped (strip %p, vb %p, indices %d)", i, count, s,
                             s->VertexBuffer, s->IndexCount);
                continue;
            }
        }
        float(*block)[4] = blocks + drawn * rows;
        memcpy(block, blocks, rows * 16);
        eSetTransform(e.Transform, g_CurrentRenderTarget->ViewID);
        native->CopyRuns(shader, runs, runCount, block);
        memcpy(block[registers], r.WorldRows(), 3 * 16);
        r.QueueBlockDraw(s->StartIndex, s->TriangleCount);
        ++drawn;
        triangles += s->IndexCount / 3;
    }
    r.DrawBlockRun(D3DPT_TRIANGLELIST, blocks, rows);
    dword_987B00 += drawn;
    return triangles;
}

/** Draws a BlockRun of a single-pass effect (DrawBlockRun), or entry by entry when that cannot be. Returns how many
 * entries it took: only the first when its own strip cannot be drawn. */
int DrawSinglePassRun(const unsigned int *order, int count, long long &t)
{
    RenderListEntry &first = unk_93E878[order[0]];
    SolidPlatInfo *plat = SetUpEntry(first, t);
    if (!BindStrip(first, plat))
        return 1;
    const eEffect *current = g_CurrentEffect;
    current->Effect->CommitChanges();
    if (current->Type == s_lastType && s_lastPasses > 1)
    {
        current->Effect->EndPass();
        current->Effect->BeginPass(0);
    }
    const SolidStrip *strip = static_cast<const SolidStrip *>(first.Strip);
    int triangles = DrawBlockRun(order, count, strip);
    if (triangles < 0)
    {
        DrawBound(strip, current->Type);
        triangles = strip->IndexCount / 3;
        for (int i = 1; i < count; ++i)
        {
            RenderListEntry &e = unk_93E878[order[i * 2]];
            SolidPlatInfo *p = SetUpEntry(e, t);
            if (!BindStrip(e, p))
                continue;
            const SolidStrip *s = static_cast<const SolidStrip *>(e.Strip);
            current->Effect->CommitChanges();
            DrawBound(s, current->Type);
            triangles += s->IndexCount / 3;
        }
    }
    s_lastPasses = 1;
    s_lastType = current->Type;
    dword_987B10 += triangles;
    Lap(t, nfs::profile::kTimerDraw);
    return count;
}

/**
 * Draws a run of strips that share a multi-pass effect a pass at a time: each pass begins once and draws all the
 * strips, each set up again for it, where the original takes every strip through all the passes. The pipeline then
 * changes at each pass of the run rather than at each pass of each strip (the trees' opaque and blended passes, the
 * grass's base and shells: most of a frame's pipeline changes), and a run's blended passes go over all its opaque ones
 * instead of only their own strip's. Each draw sees the states the strip-at-a-time order gives it: its own setup's,
 * over which those the passes up to its own set (nfs::vk::Effect::ApplyPassStates). Grass starts another technique
 * when its opacity map changes, which begins that technique's first pass: the run's pass is begun again then.
 */
void DrawPassMajor(const unsigned int *order, int count, eEffect *effect, long long &t)
{
    ID3DXEffect *e = effect->Effect;
    nfs::vk::Effect *native = nfs::vk::NativeEffect(e);
    nfs::vk::Renderer &sink = *nfs::vk::g_renderer;
    // The trees' opaque pass, alpha tested, in two (Renderer::SetDepthPrepass): its depth, then its colour on the
    // front leaves only. The sun shadow's filter was most of a forest's cost, for every leaf layer of every crown; the
    // extra pass costs the CPU a set-up per strip, so by default (Display.TreeDepthPass 2) it is done while the GPU is
    // what holds the frames up (1 always, 0 never).
    static const int treeDepth = nfs::config::Int("Display", "TreeDepthPass", 2);
    const bool split = effect->Type == EFFECT_TREE && (treeDepth == 1 || (treeDepth == 2 && sink.GpuBound()));
    // Grass: the shells of strips past their reach are left out, set-up and all.
    static unsigned char shellsHidden[0x1000];
    static const bool shellCulling = nfs::config::Int("Debug", "GrassShellCull", 1) != 0;
    const bool grass = shellCulling && nfs::x360::g_passMode == nfs::x360::kPassMain &&
                       (effect->Type == EFFECT_TREE_DEPTH || effect->Type == EFFECT_WORLD_NORMAL_DEPTH);
    int passes = effect->PassCount;
    for (int step = 0, pass = 0; pass < passes; ++step)
    {
        const int prepass = split && pass == 0 ? (step == 0 ? 1 : 2) : 0;
        sink.SetDepthPrepass(prepass);
        if (native->CurrentPass() != pass)
        {
            e->EndPass();
            e->BeginPass(static_cast<UINT>(pass));
        }
        bool statesDirty = true;
        for (int i = 0; i < count; ++i)
        {
            RenderListEntry &entry = unk_93E878[order[i * 2]];
            if (grass && pass > 0 && shellsHidden[i])
                continue;
            const int textureChanges = dword_987B30;
            const TextureInfo *diffuse = dword_982CB4;
            SolidPlatInfo *plat = SetUpEntry(entry, t);
            if (grass && pass == 0)
                shellsHidden[i] = ShellsOutOfReach(entry, effect);
            statesDirty |= textureChanges != dword_987B30 || diffuse != dword_982CB4;
            if (pass == 0 && effect->PassCount > passes)
                passes = effect->PassCount;
            if (pass >= effect->PassCount || static_cast<uint32_t>(pass) >= native->TechniquePasses())
                continue;
            if (native->CurrentPass() != pass)
            {
                e->EndPass();
                e->BeginPass(static_cast<UINT>(pass));
            }
            // The states of their own setups set come with new textures (sub_6C68B0, AssignTextures): while the
            // textures stay, the pass's states are still in place.
            if (statesDirty)
                native->ApplyPassStates(static_cast<uint32_t>(pass), sink);
            statesDirty = false;
            if (BindStrip(entry, plat))
            {
                const SolidStrip *strip = static_cast<const SolidStrip *>(entry.Strip);
                e->CommitChanges();
                // The run's next entries of the same textures go with it as a block run (not the grass, whose
                // shells are left out entry by entry).
                const int group = grass ? 1 : BlockRun(order + i * 2, count - i, true);
                int triangles = group > 1 ? DrawBlockRun(order + i * 2, group, strip) : -1;
                if (triangles >= 0)
                {
                    i += group - 1;
                }
                else
                {
                    DrawBound(strip, effect->Type);
                    triangles = strip->IndexCount / 3;
                }
                if (pass == 0 && prepass != 1)
                    dword_987B10 += triangles;
            }
            Lap(t, nfs::profile::kTimerDraw);
        }
        if (prepass != 1)
            ++pass;
    }
    sink.SetDepthPrepass(0);
    s_lastPasses = passes;
    s_lastType = effect->Type;
}

/** An immediate triangle strip: its vertex count, then 24-byte vertices. Drawn once per pass of the effect (without
 * moving to the next pass, as in the original), then freed. */
void DrawImmediateStrip(const RenderListEntry &entry)
{
    int *list = static_cast<int *>(entry.Strip);
    device::SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dword_982CA0 = nullptr;
    entry.Effect->Effect->CommitChanges();
    for (int n = entry.Effect->PassCount; n > 0; --n)
        device::DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, static_cast<UINT>(list[0] - 2), list + 1, 24);
    device::SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    ++dword_987B04;
    dword_982AC0->Free(list);
}
} // namespace

/**
 * Queues one strip of a model (sub_6E0AB0, for eViewPlatInterface::Render) with its sort key: transparent textures sort
 * by depth, textures with a draw order by that, the rest by effect type and a hash of the textures and flags.
 *
 * Change, X360 Stuff's (its jump at 0x6e0bf4): the player view no longer works out a shadow level per model from its
 * distance and puts it in the key. The Xbox 360 pass setup chooses the techniques, not the level, so that only split
 * the models of one effect into more batches and cost a bound transform per strip.
 */
extern "C" void __cdecl sub_6E0AB0(void *strip, SolidPlatInfo **solid, unsigned int flags, eEffect *effect,
                                   TextureInfo **textures, bMatrix4 *transform, void *lightContext,
                                   void *lightMaterial, void *blendMatrices)
{
    // Not in the original, which dropped the rest of a batch once the list held 4096 strips: a full list is drawn and
    // emptied first (the sun shadow cascades queue all the scenery around the player).
    if (dword_982CDC >= 0x1000)
        sub_6E2F50();
    const int index = dword_982CDC;
    if (index >= 0x1000)
        return;
    RenderListEntry &e = unk_93E878[index];
    e.Strip = strip;
    e.Immediate = 0;
    e.Solid = solid;
    e.Flags = flags;
    e.Effect = effect;
    e.LightContext = lightContext;
    e.LightMaterial = lightMaterial;
    e.Transform = transform;
    e.BlendMatrices = blendMatrices;
    for (int i = 0; i < 5; ++i)
        e.Textures[i] = textures[i];
    e.Level = 0;
    e.unknown_40 = -1.0f;
    const char *first = reinterpret_cast<const char *>(textures[0]);
    const signed char order = static_cast<signed char>(first[0x51]);
    unsigned int key;
    if (first[0x54] == 2)
    {
        key = sub_6C6F30(0, strip, transform) | 0x80000000u;
    }
    else if (order)
    {
        key = static_cast<unsigned int>(static_cast<int>(order)) | 0x80000000u;
    }
    else
    {
        unsigned int hash = 0;
        for (int i = 0; i < 5; ++i)
            if (const char *t = reinterpret_cast<const char *>(e.Textures[i]))
                hash = (hash << 1) ^ *reinterpret_cast<const unsigned int *>(t + 0x24);
        hash ^= flags;
        key = (hash & 0x00FFFFFFu) | (static_cast<unsigned int>(effect->Type) << 24);
    }
    // The sixth texture slot holds the key.
    reinterpret_cast<unsigned int &>(e.Textures[5]) = key;
    dword_9877B4[index * 2] = static_cast<unsigned int>(index);
    dword_9877B4[index * 2 + 1] = key;
    if (dword_8FAE70 == -1)
    {
        dword_8FAE70 = 0;
        dword_982CD4 = 1;
    }
    else
    {
        ++dword_982CD4;
    }
    dword_982CD8 += *reinterpret_cast<const int *>(static_cast<const char *>(strip) + 0x5c);
    dword_982CDC = index + 1;
}

extern "C" void __cdecl sub_6E0200()
{
    if (!dword_982CDC)
        return;
    nfs::profile::TimerScope profile(nfs::profile::kTimerFlush);
    if (nfs::profile::g_enabled)
    {
        nfs::profile::Count(nfs::profile::kCounterDraws, dword_982CDC);
        nfs::profile::Count(nfs::profile::kCounterFlushes, 1);
    }
    eEffect *current = nullptr;
    const unsigned int *order = dword_9877B4;
    for (int i = 0; i < dword_982CDC; ++i, order += 2)
    {
        // The entries go in sorted order, all over the list: fetch the one four ahead, and what the one two ahead
        // points at (its solid, matrix and diffuse texture), while this one is drawn.
        if (i + 4 < dword_982CDC)
        {
            const char *ahead = reinterpret_cast<const char *>(&unk_93E878[order[8]]);
            _mm_prefetch(ahead, _MM_HINT_T0);
            _mm_prefetch(ahead + 64, _MM_HINT_T0);
        }
        if (i + 2 < dword_982CDC)
        {
            const RenderListEntry &next = unk_93E878[order[4]];
            _mm_prefetch(reinterpret_cast<const char *>(next.Solid), _MM_HINT_T0);
            _mm_prefetch(reinterpret_cast<const char *>(next.Transform), _MM_HINT_T0);
            _mm_prefetch(reinterpret_cast<const char *>(next.Textures[0]), _MM_HINT_T0);
            _mm_prefetch(reinterpret_cast<const char *>(next.Strip), _MM_HINT_T0);
        }
        RenderListEntry &entry = unk_93E878[order[0]];
        eEffect *effect = entry.Effect;
        long long t = nfs::profile::g_enabled ? nfs::profile::Now() : 0;
        if (effect != current)
        {
            if (current)
                EndEffect(current);
            StartEffect(effect);
            current = effect;
            if (nfs::profile::g_enabled)
                nfs::profile::Count(nfs::profile::kCounterEffectSwitches, 1);
        }
        Lap(t, nfs::profile::kTimerEffects);
        const int run = PassMajorRun(order, dword_982CDC - i);
        if (run > 1)
        {
            DrawPassMajor(order, run, effect, t);
            i += run - 1;
            order += (run - 1) * 2;
            continue;
        }
        const int block = BlockRun(order, dword_982CDC - i, false);
        if (block > 1)
        {
            const int taken = DrawSinglePassRun(order, block, t);
            i += taken - 1;
            order += (taken - 1) * 2;
            continue;
        }
        SolidPlatInfo *plat = SetUpEntry(entry, t);
        if (entry.Immediate)
            DrawImmediateStrip(entry);
        else
            DrawStrip(entry, plat);
        Lap(t, nfs::profile::kTimerDraw);
    }
    if (current)
        EndEffect(current);
    dword_982CA4 = nullptr;
    dword_982CD8 = 0;
    dword_982CDC = 0;
}

namespace
{
/** Sorts the list's (entry index, sort key) pairs as the original's std::sort does (by key, then by index): a stable
 * radix sort by key, four passes of a byte (those where all the keys share their byte are left out), of pairs that are
 * already in index order, which they are as queued. Over a frame's twenty flushes the original's introsort, with its
 * insertion sort rotating ranges by modulo, cost more than drawing a list of the same length. */
void SortList(unsigned int *pairs, int count)
{
    static unsigned int scratch[0x1000 * 2];
    unsigned int histogram[4][256];
    memset(histogram, 0, sizeof(histogram));
    for (int i = 0; i < count; ++i)
    {
        if (pairs[i * 2] != static_cast<unsigned int>(i))
        {
            sub_6E2AC0(pairs, pairs + count * 2, count);
            return;
        }
        const unsigned int key = pairs[i * 2 + 1];
        ++histogram[0][key & 255];
        ++histogram[1][key >> 8 & 255];
        ++histogram[2][key >> 16 & 255];
        ++histogram[3][key >> 24];
    }
    unsigned int *from = pairs, *to = scratch;
    for (int pass = 0; pass < 4; ++pass)
    {
        unsigned int *h = histogram[pass];
        const int shift = pass * 8;
        if (h[from[1] >> shift & 255] == static_cast<unsigned int>(count))
            continue;
        unsigned int sum = 0;
        for (int d = 0; d < 256; ++d)
        {
            const unsigned int n = h[d];
            h[d] = sum;
            sum += n;
        }
        for (int i = 0; i < count; ++i)
        {
            const unsigned int key = from[i * 2 + 1];
            const unsigned int at = h[key >> shift & 255]++;
            to[at * 2] = from[i * 2];
            to[at * 2 + 1] = key;
        }
        unsigned int *t = from;
        from = to;
        to = t;
    }
    if (from != pairs)
        for (int i = 0; i < count * 2; ++i)
            pairs[i] = from[i];
}

/** Within one key's run of `count` pairs, brings the entries of each strip together, the strips in the order they
 * first come and each strip's entries in their own order. */
void GroupRun(unsigned int *pairs, int count)
{
    const int kTableSize = 0x2000;
    struct Slot
    {
        const void *strip;
        unsigned int stamp;
        unsigned int group;
    };
    static Slot table[kTableSize];
    static unsigned int stamp;
    static unsigned short groupOf[0x1000];
    static unsigned int start[0x1000 + 1];
    static unsigned int scratch[0x1000 * 2];
    if (++stamp == 0)
    {
        memset(table, 0, sizeof(table));
        stamp = 1;
    }
    unsigned int groups = 0, changes = 0;
    for (int i = 0; i < count; ++i)
    {
        const void *strip = unk_93E878[pairs[i * 2]].Strip;
        const uintptr_t p = reinterpret_cast<uintptr_t>(strip);
        unsigned int h = static_cast<unsigned int>((p >> 4) * 0x9E3779B1u) >> 19;
        while (table[h].stamp == stamp && table[h].strip != strip)
            h = (h + 1) & (kTableSize - 1);
        if (table[h].stamp != stamp)
        {
            table[h].stamp = stamp;
            table[h].strip = strip;
            table[h].group = groups;
            start[groups++] = 0;
        }
        const unsigned int g = table[h].group;
        groupOf[i] = static_cast<unsigned short>(g);
        ++start[g];
        changes += i > 0 && groupOf[i - 1] != g;
    }
    if (groups == static_cast<unsigned int>(count) || changes + 1 == groups)
        return;
    unsigned int sum = 0;
    for (unsigned int g = 0; g < groups; ++g)
    {
        const unsigned int n = start[g];
        start[g] = sum;
        sum += n;
    }
    for (int i = 0; i < count; ++i)
    {
        const unsigned int at = start[groupOf[i]]++;
        scratch[at * 2] = pairs[i * 2];
        scratch[at * 2 + 1] = pairs[i * 2 + 1];
    }
    memcpy(pairs, scratch, static_cast<size_t>(count) * 8);
}

/** Brings the instances of a strip together within each run of one sort key of the opaque entries (the key is the
 * effect and a hash of the textures and flags, so such entries share every state), so that the draw takes them as one
 * instanced draw (DrawBlockRun). The depth sorted and ordered entries keep their order. `-set Debug.GroupStrips=0`
 * keeps the original's order. */
void GroupStrips(unsigned int *pairs, int count)
{
    static const bool enabled = nfs::config::Int("Debug", "GroupStrips", 1) != 0;
    if (!enabled || !nfs::vk::g_renderer)
        return;
    for (int first = 0; first < count;)
    {
        const unsigned int key = pairs[first * 2 + 1];
        int end = first + 1;
        while (end < count && pairs[end * 2 + 1] == key)
            ++end;
        if (!(key & 0x80000000u) && end - first > 2)
            GroupRun(pairs + first * 2, end - first);
        first = end;
    }
}
} // namespace

/** Sorts and draws the list (original: zPlatform 0x6e2f50). */
extern "C" void __cdecl sub_6E2F50()
{
    if (!dword_982CDC)
    {
        dword_982CD4 = 0;
        return;
    }
    SortList(dword_9877B4, dword_982CDC);
    GroupStrips(dword_9877B4, dword_982CDC);
    sub_6E0200();
    dword_8FAE70 = -1;
}
