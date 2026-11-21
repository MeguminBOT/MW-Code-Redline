/**
 * The particle batches (original: zPlatform 0x6c0e80, 0x6c0f80, 0x6c1010, PlatStartParticleRender 0x6ceee0,
 * PlatEndParticleRender 0x6da2f0 and the other batch ends 0x6d1f60, 0x6d20c0, 0x6da2b0). The emitters write quads of
 * 24-byte vertices into the batch's locked vertex memory; ending a batch draws them with a fixed quad index buffer.
 * With Direct3D 9 these run the original code; in Vulkan mode the vertex memory is plain memory that the draw copies
 * into the frame, and the index buffer is the renderer's.
 *
 * Change from the original: particles that take light - smoke, dust, debris, leaves, water spray, glass, rain - are
 * lit as the hour lights the world (the rebuild's particle shaders, assets/shaders/owned/x360_particles_*; docs/NIGHT.md),
 * where the original drew them as bright at night as by day. Particles that give light (sparks, glows, fire, the
 * world's icons) stay as they are. A batch's texture says which: alpha blended, with a name of the first kind.
 */
#include <string.h>

#include "game/gen/api.h"
#include "game/platform/Device.h"
#include "game/platform/Platform.h"
#include "legacy/link.h"
#include "engine/rendering/vulkan/renderer.h"

NFS_REPLACE(0x6c0e80, ParticleBuffersInit)
NFS_REPLACE(0x6c0f80, ParticleBatchBegin)
NFS_REPLACE(0x6c1010, ParticleBatchDraw)
NFS_REPLACE(0x6d1f60, ParticleBatchEndFlagged)
NFS_REPLACE(0x6d20c0, sub_6D20C0)
NFS_REPLACE(0x6da2b0, sub_6DA2B0)

/** The particle batch (AutoClass1 at 0x93caf4). */
struct ParticleBatch
{
    /** IDirect3DVertexBuffer9 (dynamic), or a renderer buffer whose memory the emitters fill in Vulkan mode. */
    void *VertexBuffer;       // 0x00
    void *IndexBuffer;        // 0x04
    int Quads;                // 0x08
    int VertexCapacity;       // 0x0c
    int VertexOffset;         // 0x10
    /** Vertices to draw. */
    int VertexCount;          // 0x14
    int FirstVertex;          // 0x18
    bool Additive;            // 0x1c
    char pad_1d[3];
    int Stride;               // 0x20
    bool Locked;              // 0x24
    bool Discard;             // 0x25
    char pad_26[2];
    char *Start;              // 0x28
    char *End;                // 0x2c
    char *Cursor;             // 0x30
    bool Active;              // 0x34
};

extern "C" ParticleBatch AutoClass1_0093caf4;
extern "C" unsigned char unk_9877CC;
/** The diffuse texture bound last: the batch's (sub_6C6CC0). */
extern "C" TextureInfo *dword_982CB4;

void __fastcall ParticleBuffersInit(void *batch, void *, const int *fvf, const int *quads);
const char *__fastcall ParticleBatchBegin(ParticleBatch *self, void *, const int *quads);
void __fastcall ParticleBatchDraw(ParticleBatch *self);
void __fastcall ParticleBatchEndFlagged(char *self);
extern "C" void __cdecl sub_6D20C0();
extern "C" void __cdecl sub_6DA2B0();
extern "C" void __cdecl sub_6C6CC0(TextureInfo *texture, unsigned int flags);

namespace
{
nfs::vk::GeometryBuffer *Buffer(void *buffer)
{
    return static_cast<nfs::vk::GeometryBuffer *>(buffer);
}

/** Ends writing (the original unlocks the vertex buffer). */
void Unlock(ParticleBatch &batch)
{
    if (batch.Locked && batch.VertexBuffer)
        batch.Locked = false;
}

/** Parts of the names of particle textures whose particles take light (the game's FX_SMK01_BLEND, FX_DEBRIS01_BLEND,
 * FX_LEAF01_BLEND, FX_WATER01_BLEND, FX_GLASS01_BLEND_ANIM, RAINDROP, ...); fire and glows are alpha blended too. */
const char *const kLitParticles[] = {"SMK", "SMOKE", "DEBRIS", "LEAF", "WATER", "BIRD", "GLASS", "GRASS",
                                     "DUST",  "DIRT",  "MIST",   "STEAM", "SPRAY", "SPLASH", "SNOW", "RAIN"};
/** TextureInfo::AlphaBlendType: alpha blended (the others add light, or darken). */
const char kAlphaBlend = 1;
const char kAlphaBlendToo = 4;

bool TakesLight(const TextureInfo *texture, bool additive)
{
    static const TextureInfo *last;
    static bool lastLit;
    if (!texture || additive || (texture->AlphaBlendType != kAlphaBlend && texture->AlphaBlendType != kAlphaBlendToo))
        return false;
    if (texture == last)
        return lastLit;
    char name[sizeof(texture->Name) + 1];
    memcpy(name, texture->Name, sizeof(texture->Name));
    name[sizeof(texture->Name)] = 0;
    bool lit = false;
    for (const char *part : kLitParticles)
        lit |= strstr(name, part) != nullptr;
    last = texture;
    lastLit = lit;
    return lit;
}
} // namespace

/** The vertex buffer for `*quads` quads (and 10 spare vertices) and the index buffer of their two triangles each. */
void __fastcall ParticleBuffersInit(void *batch, void *, const int *fvf, const int *quads)
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    ParticleBatch *self = static_cast<ParticleBatch *>(batch);
    self->Quads = *quads;
    self->VertexBuffer = nullptr;
    self->VertexOffset = 0;
    self->Discard = true;
    self->Locked = false;
    self->Additive = false;
    self->VertexCapacity = *quads * 4;
    self->Stride = 0x18;
    self->FirstVertex = 0;
    self->Start = nullptr;
    self->End = nullptr;
    self->VertexBuffer = r->CreateDynamicGeometryBuffer((self->VertexCapacity + 10) * 0x18, false);
    const int count = *quads;
    unsigned short *indices = static_cast<unsigned short *>(::operator new(count * 12));
    for (int i = 0, v = 1; i < count; ++i, v += 4)
    {
        unsigned short *quad = indices + i * 6;
        quad[0] = static_cast<unsigned short>(v + 2);
        quad[1] = static_cast<unsigned short>(v - 1);
        quad[2] = static_cast<unsigned short>(v);
        quad[3] = static_cast<unsigned short>(v);
        quad[4] = static_cast<unsigned short>(v + 1);
        quad[5] = static_cast<unsigned short>(v + 2);
    }
    self->IndexBuffer = r->CreateGeometryBuffer(indices, static_cast<uint32_t>(count * 12), true);
    ::operator delete(indices);
}

/** Starts a batch of up to `*quads` quads: the write cursor is at the start of the vertex memory. Returns it. */
const char *__fastcall ParticleBatchBegin(ParticleBatch *self, void *, const int *quads)
{
    self->Discard = true;
    int vertices = *quads * 4;
    if (vertices > self->VertexCapacity)
        vertices = self->VertexCapacity;
    self->Active = true;
    self->VertexCount = vertices;
    self->FirstVertex = 0;
    if (!self->VertexBuffer)
        return nullptr;
    self->Discard = false;
    self->VertexOffset = 0;
    char *memory = static_cast<char *>(Buffer(self->VertexBuffer)->cpu);
    self->FirstVertex = self->VertexOffset;
    self->VertexOffset += vertices;
    self->Locked = true;
    self->Start = memory;
    self->Cursor = memory;
    self->End = memory + vertices * 0x18;
    return memory;
}

/** Draws the batch's quads (additively when set), then empties it. */
void __fastcall ParticleBatchDraw(ParticleBatch *self)
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    r->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    if (self->Additive)
    {
        eSetAlphaBlend(1, D3DBLEND_SRCALPHA, D3DBLEND_ONE);
        // The particle shaders dim added light in the haze, and fade blended particles into it (D3D9State::blend).
        r->SetDrawBlend(2);
    }
    r->SetRenderState(D3DRS_COLORWRITEENABLE, 7);
    const uint32_t startIndex = (static_cast<uint32_t>(self->FirstVertex) >> 2) * 6;
    const int triangles = self->VertexCount >> 1;
    if (triangles > 0 && self->VertexBuffer)
    {
        g_CurrentEffect->Effect->BeginPass(0);
        g_CurrentEffect->Effect->CommitChanges();
        r->SetDrawLitParticles(TakesLight(dword_982CB4, self->Additive));
        r->DrawIndexedUP(D3DPT_TRIANGLELIST, static_cast<uint32_t>(self->FirstVertex),
                         static_cast<uint32_t>(self->VertexCount), startIndex, static_cast<uint32_t>(triangles),
                         Buffer(self->VertexBuffer)->cpu, 0x18, Buffer(self->IndexBuffer));
        r->SetDrawLitParticles(false);
        g_CurrentEffect->Effect->EndPass();
    }
    self->VertexCount = 0;
}

bool PlatStartParticleRender(eView *view, TextureInfo *texture, unsigned int quads)
{
    device::SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    unk_9877CC = 1;
    eSetTransform(&eMathIdentityMatrix, view->ID);
    sub_6C6CC0(texture, 0);
    const int count = static_cast<int>(quads);
    ParticleBatchBegin(&AutoClass1_0093caf4, nullptr, &count);
    g_CurrentEffect->Effect->BeginPass(0);
    return true;
}

void PlatEndParticleRender()
{
    unk_9877CC = 0;
    Unlock(AutoClass1_0093caf4);
    ParticleBatchDraw(&AutoClass1_0093caf4);
    g_CurrentEffect->Effect->EndPass();
    device::SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
}

/** Another renderer's batch end: clears its flag, then draws. */
void __fastcall ParticleBatchEndFlagged(char *self)
{
    *self = 0;
    Unlock(AutoClass1_0093caf4);
    ParticleBatchDraw(&AutoClass1_0093caf4);
}

/** Draws what is left and releases the vertex buffer (before a device reset or at shut down). */
extern "C" void __cdecl sub_6D20C0()
{
    Unlock(AutoClass1_0093caf4);
    ParticleBatchDraw(&AutoClass1_0093caf4);
    if (AutoClass1_0093caf4.VertexBuffer)
        nfs::vk::g_renderer->Release(Buffer(AutoClass1_0093caf4.VertexBuffer));
}

extern "C" void __cdecl sub_6DA2B0()
{
    Unlock(AutoClass1_0093caf4);
    ParticleBatchDraw(&AutoClass1_0093caf4);
    if (AutoClass1_0093caf4.VertexBuffer)
        nfs::vk::g_renderer->Release(Buffer(AutoClass1_0093caf4.VertexBuffer));
}
