/**
 * The shadow receiver meshes (original: zPlatform 0x6d7b30, 0x6c9810, 0x6d7c40, 0x6c9aa0, 0x6d7f70 and the pass
 * 0x6e54e0). For each scenery section the scenery that receives the dynamic shadow is merged into one position-only
 * mesh (0x6d7c40); the player view draws those meshes again with the shadow map effect, depth biased, so the shadow
 * map's shadows land on the world. With Direct3D 9 the meshes are the original code; in Vulkan mode they are renderer
 * buffers the game fills in memory, uploaded once built. The pass is this version on both.
 */
#include <string.h>

#include "game/gen/api.h"
#include "game/platform/Device.h"
#include "game/platform/Platform.h"
#include "game/platform/Solid.h"
#include "engine/rendering/shadows/shadow.h"
#include "legacy/link.h"
#include "engine/rendering/vulkan/renderer.h"
#include "engine/rendering/x360/render.h"

/** A scenery instance's rotation (source/engine/rendering/frame/PlatformMisc.cpp). */
void __fastcall InstanceGetMatrix(const char *instance, void *, bMatrix4 *out);


NFS_REPLACE(0x6d7b30, ShadowMeshConstruct)
NFS_REPLACE(0x6c9810, ShadowMeshLock)
NFS_REPLACE(0x6d7c40, ShadowMeshBuild)
NFS_REPLACE(0x6c9aa0, ShadowMeshDraw)
NFS_REPLACE(0x6d7f70, ShadowMeshDrawList)
NFS_REPLACE(0x6e54e0, sub_6E54E0)

/** Meshes made so far; each gets a debug colour from its number. */
extern "C" int dword_98856C;
/** Holds the slot pool the meshes come from (at +8). */
extern "C" char *dword_982D10;
/** Set, the list draw stops after the first mesh. */
extern "C" unsigned char dword_988570;
/** The depth bias of the receiver pass: a depth step count and the slope scale (a float). */
extern "C" int dword_901AC0;
extern "C" DWORD dword_901ABC;
/** The receiver pass draws (the shadow pass, sub_6e44c0) only when set. */
extern "C" unsigned char byte_8FAED0;
/** Shadow map method: 0 none, 1 depth textures (NVIDIA), 2 DF16 (ATI). */
extern "C" int dword_982C08;
extern "C" int lpData_00901830;

extern "C" bool __cdecl sub_73D4E0(int);
extern "C" void __cdecl sub_6E54E0(eView *view, void *cull);

/** A receiver mesh (vtable 0x8afb74): positions (12 bytes each) and 16-bit indices, filled through the write
 * pointers between lock and unlock; +0x10 links it into its section's list. */
struct ShadowMesh
{
    const void *vtable;
    int unknown_04[3];
    void *Next;              // 0x10
    void *Prev;              // 0x14
    int Section;             // 0x18
    int unknown_1c;
    float Colour[4];         // 0x20
    int IndexCount;          // 0x30
    void *IndexBuffer;       // 0x34
    unsigned short *Indices; // 0x38
    int IndexCapacity;       // 0x3c
    int VertexCount;         // 0x40
    float *Vertices;         // 0x44
    void *VertexBuffer;      // 0x48
    int VertexCapacity;      // 0x4c
};

static_assert(sizeof(ShadowMesh) == 0x50, "ShadowMesh layout");

namespace
{

const int kParamColorWriteMode = 0x18c;

nfs::vk::GeometryBuffer *Buffer(void *buffer)
{
    return static_cast<nfs::vk::GeometryBuffer *>(buffer);
}
} // namespace

ShadowMesh *__fastcall ShadowMeshConstruct(ShadowMesh *self, void *, int section, int indices, int vertices)
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    self->Section = section;
    self->VertexCapacity = vertices;
    self->vtable = reinterpret_cast<const void *>(0x8AFB74);
    self->IndexCount = 0;
    self->VertexBuffer = r->CreateDynamicGeometryBuffer(static_cast<uint32_t>(vertices) * 12, false);
    self->IndexCapacity = indices;
    self->IndexBuffer = r->CreateDynamicGeometryBuffer(static_cast<uint32_t>(indices) * 2, true);
    int n = dword_98856C;
    if (n % 8 == 7)
        ++n;
    if (n % 8 == 0)
        ++n;
    self->Colour[0] = (n & 1) ? 1.0f : 0.0f;
    self->Colour[1] = (n & 2) ? 1.0f : 0.0f;
    self->Colour[2] = (n & 4) ? 1.0f : 0.0f;
    dword_98856C = n + 1;
    self->Colour[3] = 1.0f;
    return self;
}

/** Opens the buffers for writing from their start. */
void __fastcall ShadowMeshLock(ShadowMesh *self)
{
    self->IndexCount = 0;
    self->VertexCount = 0;
    self->Indices = self->IndexBuffer ? static_cast<unsigned short *>(Buffer(self->IndexBuffer)->cpu) : nullptr;
    self->Vertices = self->VertexBuffer ? static_cast<float *>(Buffer(self->VertexBuffer)->cpu) : nullptr;
}

/**
 * Builds a section's receiver mesh from its scenery instances that receive shadows (flags 0x300000), are not hidden
 * and whose strips all use world-like effects with textures that are not alpha tested. Instances of models named
 * SHADOW* are flagged 0x10; the ones merged are flagged 0x4000000.
 */
void __fastcall ShadowMeshBuild(void *list, void *, char *section)
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    static char *instances[0x1000];
    static char *solids[0x1000];
    int count = 0, indices = 0, vertices = 0;
    const int instanceCount = *reinterpret_cast<int *>(section + 0x24);
    for (int i = 0; i < instanceCount; ++i)
    {
        char *instance = *reinterpret_cast<char **>(section + 0x20) + i * 0x40;
        char *model = *reinterpret_cast<char **>(section + 0x18) + *reinterpret_cast<short *>(instance + 0x3e) * 0x48;
        if (sub_73D4E0(*reinterpret_cast<int *>(model + 0x18)))
            continue;
        const unsigned int flags = *reinterpret_cast<unsigned int *>(instance + 0x18);
        if ((flags & 0x40000000) || !(flags & 0x300000))
            continue;
        char *object = *reinterpret_cast<char **>(model + 0x28);
        if (!object)
            continue;
        char *solid = *reinterpret_cast<char **>(object + 0x0c);
        if (!solid)
            continue;
        const SolidPlatInfo *plat = *reinterpret_cast<SolidPlatInfo **>(solid);
        int solidIndices = 0, solidVertices = 0;
        bool merge = plat->StripCount > 0;
        for (int s = 0; s < plat->StripCount; ++s)
        {
            const char *strip = reinterpret_cast<const char *>(&plat->Strips[s]);
            solidIndices += *reinterpret_cast<const int *>(strip + 0x5c);
            const SolidStrip &st = plat->Strips[s];
            if (st.VertexBytes)
                solidVertices += static_cast<int>(st.VertexBytes / static_cast<unsigned int>(st.Effect->Stride));
            const int effect = st.EffectId;
            if (effect != EFFECT_WORLD && effect != EFFECT_WORLD_NORMAL_MAP && effect != EFFECT_GLOSSY_WINDOW &&
                effect != EFFECT_WORLD_REFLECT && effect != EFFECT_TREE)
            {
                merge = false;
                break;
            }
            const unsigned char textureIndex = *reinterpret_cast<const unsigned char *>(strip + 0x18);
            const TextureInfo *texture =
                *reinterpret_cast<TextureInfo **>(*reinterpret_cast<char **>(solid + 0x2c) + textureIndex * 8 + 4);
            if (!texture || texture->AlphaUsageType == 1)
            {
                merge = false;
                break;
            }
        }
        if (merge)
        {
            indices += solidIndices;
            vertices += solidVertices;
            instances[count] = instance;
            solids[count] = solid;
            ++count;
        }
        if (!_strnicmp(solid + 0xa0, "SHADOW", 6))
            *reinterpret_cast<unsigned int *>(instance + 0x18) |= 0x10;
    }
    if (indices <= 0 || vertices <= 0 || lpData_00901830 <= 0)
        return;
    ShadowMesh *mesh = static_cast<ShadowMesh *>(bOMalloc(*reinterpret_cast<SlotPool **>(dword_982D10 + 8)));
    if (mesh)
        mesh = ShadowMeshConstruct(mesh, nullptr, *reinterpret_cast<int *>(section + 0x0c), indices, vertices);
    ShadowMeshLock(mesh);
    for (int k = 0; k < count; ++k)
    {
        char *instance = instances[k];
        bMatrix4 transform;
        InstanceGetMatrix(instance, nullptr, &transform);
        transform.v3 = bVector4(*reinterpret_cast<float *>(instance + 0x20), *reinterpret_cast<float *>(instance + 0x24),
                                *reinterpret_cast<float *>(instance + 0x28), 1.0f);
        NFS_ORIGINAL(0x6c9860, void(__thiscall *)(ShadowMesh *, char *, bMatrix4 *))(mesh, solids[k], &transform);
        *reinterpret_cast<unsigned int *>(instance + 0x18) |= 0x4000000;
    }
    r->UploadGeometryBuffer(Buffer(mesh->IndexBuffer), static_cast<uint32_t>(mesh->IndexCount) * 2);
    mesh->Indices = nullptr;
    r->UploadGeometryBuffer(Buffer(mesh->VertexBuffer), static_cast<uint32_t>(mesh->VertexCount) * 12);
    mesh->Vertices = nullptr;
    void **head = static_cast<void **>(list);
    void **node = reinterpret_cast<void **>(&mesh->Next);
    void **tail = static_cast<void **>(head[1]);
    tail[0] = node;
    head[1] = node;
    node[1] = tail;
    node[0] = head;
}

void __fastcall ShadowMeshDraw(ShadowMesh *self)
{
    if (!self->VertexBuffer || !self->IndexBuffer)
        return;
    device::SetStreamSource(0, self->VertexBuffer, 0, 12);
    if (!self->IndexCount)
        return;
    device::SetIndices(self->IndexBuffer);
    device::DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, static_cast<UINT>(self->VertexCount), 0,
                                 static_cast<UINT>(self->IndexCount / 3));
}

/** Draws a section list's meshes with the shadow map mesh effect (its depth-only version without a shadow map
 * method), then leaves clockwise culling on. */
void __fastcall ShadowMeshDrawList(void **list, void *, eView *view)
{
    if (list[0] == list)
        return;
    eEffect *effect = dword_982C08 ? g_Effects[EFFECT_SHADOW_MAP_MESH] : g_Effects[EFFECT_SHADOW_MAP_DEPTH];
    g_CurrentEffect = effect;
    effect->Start();
    device::SetVertexDeclaration(effect);
    UINT passes;
    effect->Effect->Begin(&passes, 0);
    effect->Effect->BeginPass(0);
    eSetTransform(&eMathIdentityMatrix, view->ID);
    effect->Effect->CommitChanges();
    char *end = reinterpret_cast<char *>(list) - 0x10;
    char *mesh = list[0] ? static_cast<char *>(list[0]) - 0x10 : nullptr;
    while (mesh != end)
    {
        ShadowMeshDraw(reinterpret_cast<ShadowMesh *>(mesh));
        if (dword_988570)
            break;
        void *next = reinterpret_cast<ShadowMesh *>(mesh)->Next;
        mesh = next ? static_cast<char *>(next) - 0x10 : nullptr;
    }
    effect->Effect->EndPass();
    effect->End();
    effect->Effect->End();
    g_CurrentEffect = nullptr;
    device::SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);
}

/** The receiver pass of the player view, depth biased so the meshes win against the world they copy; both renderers
 * (the shadow pass inside it has the Xbox 360 changes, source/engine/rendering/shadows/Shadows.cpp). */
extern "C" void __cdecl sub_6E54E0(eView *view, void *cull)
{
    const float bias = static_cast<float>(dword_901AC0) * 5.960465e-08f;
    device::SetRenderState(D3DRS_DEPTHBIAS, *reinterpret_cast<const DWORD *>(&bias));
    device::SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, dword_901ABC);
    if (byte_8FAED0 && !nfs::shadow::Active())
        nfs::x360::RenderShadowPass(view, cull);
    device::SetRenderState(D3DRS_DEPTHBIAS, 0);
    device::SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, 0);
    device::SetRenderState(D3DRS_ZENABLE, TRUE);
}
