/**
 * Solid loading (original: zPlatform eSolidPlatInterface::LoaderPlatChunks 0x6e3ee0): the platform chunks of a solid
 * give its header (linked into the solid list), strips, index data, vertex data and light materials; and the vertex
 * buffers made later for solids that kept their vertices in memory (0x6be590). With Direct3D 9 these run the original
 * code; in Vulkan mode the index and vertex buffers are the renderer's.
 */
#include "game/gen/api.h"
#include "engine/resources/streamprofile.h"
#include "game/platform/Solid.h"
#include "legacy/link.h"
#include "engine/rendering/vulkan/renderer.h"

NFS_REPLACE(0x6e3ee0, SolidLoaderPlatChunks)
NFS_REPLACE(0x6be590, SolidCreateVertexBuffers)

/** The loaded solids' platform data (a bList: head node, Next then Prev). */
extern "C" SolidPlatInfo *unk_987AF0[2];
/** Set, solids keep their vertices in memory and draw from there. */
extern "C" unsigned char byte_982A90;
/** Light materials by Attrib key (a std::map; its head node, end(), is at +4). */
extern "C" char unk_9877C0[];

int __fastcall SolidLoaderPlatChunks(SolidPlatInfo **self, void *, const unsigned int *chunk);
int __fastcall SolidCreateVertexBuffers(SolidPlatInfo *plat);

namespace
{
const unsigned int kChunkHeader = 0x134900;
const unsigned int kChunkStrips = 0x134B02;
const unsigned int kChunkIndices = 0x134B03;
const unsigned int kChunkVertices = 0x134B01;
const unsigned int kChunkMaterial = 0x134C02;

/** An Attrib::Instance (and the generated lightmaterial class that is one). */
struct AttribInstance
{
    char data[0x14];
};

template <typename T> T *Aligned(const unsigned int *chunk, unsigned int alignment)
{
    return reinterpret_cast<T *>((reinterpret_cast<uintptr_t>(chunk) + 8 + alignment - 1) & ~(alignment - 1));
}

/** The strips after `first` with the same effect share its vertices (and `buffer`). Returns the index after them. */
int ShareVertices(SolidPlatInfo *plat, int first, void *buffer)
{
    const unsigned int count = plat->Strips[first].VertexCount;
    int i = first + 1;
    for (; i < plat->StripCount && plat->Strips[i].EffectId == plat->Strips[i - 1].EffectId; ++i)
    {
        SolidStrip &strip = plat->Strips[i];
        strip.VertexData = nullptr;
        strip.VertexBytes = 0;
        strip.VertexBuffer = buffer;
        strip.VertexCount = count;
    }
    return i;
}

void *FindMaterial(const char *name)
{
    unsigned int key = NFS_ORIGINAL(0x454640, unsigned int(__cdecl *)(const char *))(name);
    char *node;
    NFS_ORIGINAL(0x6e19d0, void(__thiscall *)(void *, char **, const unsigned int *))(unk_9877C0, &node, &key);
    if (node == *reinterpret_cast<char **>(unk_9877C0 + 4))
    {
        AttribInstance material;
        NFS_ORIGINAL(0x6cf230, void(__thiscall *)(AttribInstance *, unsigned int, int, int))(&material, key, 0, 0);
        struct
        {
            unsigned int key;
            AttribInstance material;
        } entry;
        entry.key = key;
        NFS_ORIGINAL(0x6be360, void(__thiscall *)(AttribInstance *, const AttribInstance *))(&entry.material,
                                                                                            &material);
        struct
        {
            char *node;
            bool inserted;
        } result;
        NFS_ORIGINAL(0x6e3a00, void(__thiscall *)(void *, void *, const void *))(unk_9877C0, &result, &entry);
        node = result.node;
        NFS_ORIGINAL(0x45a430, void(__thiscall *)(AttribInstance *))(&entry.material);
        NFS_ORIGINAL(0x45a430, void(__thiscall *)(AttribInstance *))(&material);
    }
    return node + 0x10;
}
} // namespace

int __fastcall SolidLoaderPlatChunks(SolidPlatInfo **self, void *, const unsigned int *chunk)
{
    nfs::streamprofile::Scope profile(nfs::streamprofile::kSolidPlat);
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    const unsigned int *end = reinterpret_cast<const unsigned int *>(reinterpret_cast<const char *>(chunk + 2) +
                                                                     chunk[1]);
    int vertexStrip = 0, materialStrip = 0;
    for (const unsigned int *sub = chunk + 2; sub < end;
         sub = reinterpret_cast<const unsigned int *>(reinterpret_cast<const char *>(sub + 2) + sub[1]))
    {
        SolidPlatInfo *plat = *self;
        switch (sub[0])
        {
        case kChunkHeader:
        {
            plat = Aligned<SolidPlatInfo>(sub, 16);
            plat->SystemMemoryVertices = 0;
            *self = plat;
            SolidPlatInfo *tail = unk_987AF0[1];
            tail->Next = plat;
            unk_987AF0[1] = plat;
            plat->Prev = tail;
            plat->Next = reinterpret_cast<SolidPlatInfo *>(unk_987AF0);
            break;
        }
        case kChunkStrips:
            plat->Strips = Aligned<SolidStrip>(sub, 16);
            for (int i = 0; i < plat->StripCount; ++i)
                plat->Strips[i].Effect = g_Effects[plat->Strips[i].EffectId];
            break;
        case kChunkIndices:
            plat->IndexData = Aligned<const unsigned short>(sub, 16);
            if (plat->IndexCount)
            {
                nfs::streamprofile::Scope part(nfs::streamprofile::kSolidUploads);
                plat->IndexBuffer = r->CreateGeometryBuffer(plat->IndexData, plat->IndexCount * 2, true);
            }
            else
            {
                plat->IndexBuffer = nullptr;
                plat->IndexData = nullptr;
            }
            break;
        case kChunkVertices:
        {
            const char *data = Aligned<const char>(sub, 128);
            const unsigned int bytes =
                static_cast<unsigned int>(reinterpret_cast<const char *>(sub + 2) + sub[1] - data);
            if (!bytes)
                break;
            SolidStrip &strip = plat->Strips[vertexStrip];
            strip.VertexData = data;
            strip.VertexBytes = bytes;
            if (byte_982A90 && plat->unknown_0c >= 0)
            {
                if (!plat->SystemMemoryVertices)
                    plat->SystemMemoryVertices = 1;
                vertexStrip = ShareVertices(plat, vertexStrip, nullptr);
                break;
            }
            nfs::streamprofile::Scope part(nfs::streamprofile::kSolidUploads);
            strip.VertexBuffer =
                r->CreateStaticVertexBuffer(data, bytes, static_cast<unsigned int>(strip.Effect->Stride));
            strip.VertexCount = bytes / static_cast<unsigned int>(strip.Effect->Stride);
            vertexStrip = ShareVertices(plat, vertexStrip, strip.VertexBuffer);
            break;
        }
        case kChunkMaterial:
        {
            SolidStrip &strip = plat->Strips[materialStrip++];
            strip.MaterialName = reinterpret_cast<const char *>(sub + 2);
            nfs::streamprofile::Scope part(nfs::streamprofile::kSolidMaterials);
            strip.Material = FindMaterial(strip.MaterialName);
            break;
        }
        default:
            break;
        }
    }
    return 1;
}

/** Vertex buffers for a solid that kept its vertices in memory: one per run of strips with the same effect. Returns how
 * many were made. */
int __fastcall SolidCreateVertexBuffers(SolidPlatInfo *plat)
{
    nfs::vk::Renderer *r = nfs::vk::g_renderer;
    int made = 0, effect = -1;
    void *buffer = nullptr;
    unsigned int count = 0;
    for (int i = 0; i < plat->StripCount; ++i)
    {
        SolidStrip &strip = plat->Strips[i];
        if (strip.EffectId != effect)
        {
            strip.VertexBuffer = r->CreateStaticVertexBuffer(strip.VertexData, strip.VertexBytes,
                                                             static_cast<unsigned int>(strip.Effect->Stride));
            strip.VertexCount = strip.VertexBytes / static_cast<unsigned int>(strip.Effect->Stride);
            buffer = strip.VertexBuffer;
            count = strip.VertexCount;
            ++made;
        }
        else
        {
            strip.VertexBuffer = buffer;
            strip.VertexCount = count;
        }
        effect = strip.EffectId;
    }
    plat->SystemMemoryVertices = 2;
    return made;
}
