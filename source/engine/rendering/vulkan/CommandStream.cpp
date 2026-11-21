/**
 * The render thread (docs/VULKAN.md "Render thread"): the frame's command buffer is recorded on a thread of its own.
 *
 * The main thread works out every draw as before, but the device's command functions for the frame's command buffer
 * (vk.h's pointers, swapped here) only copy their arguments, arrays and structures included, into a ring as a packet:
 * the replay function and its arguments. The render thread takes the packets in order and makes the driver's calls,
 * which were most of the main thread's time in the driver (about a fifth of a frame). Commands for other command
 * buffers (uploads, one-off copies) are made at once, as before.
 *
 * The thread also begins and ends the frame's command buffer; the main thread waits until it has made every call sent
 * (Drain) when the buffer ends (before it is submitted), before its pool is reset or freed and before the device waits
 * idle. Nothing else changes order: the queue, submits and presents stay on the main thread; resources are freed with
 * the frames that used them, which the thread is done with long before.
 *
 * The driver writes the commands into write-combined memory, whose stores wait in the writing core's buffers until a
 * fence: the thread fences each time it runs out of packets, before it reports them done, or the main thread could
 * submit commands that are not in memory yet (the GPU hung on half-written clears and draws without it).
 *
 * `[Display] RenderThread` (1, 0 off). Off with -vkcallprofile, which times the driver's calls on the main thread.
 * `-set Debug.RenderThreadSync=1` waits for each call (to tell the thread's faults from the commands').
 */
#include <intrin.h>
#include <string.h>
#include <windows.h>

#include "core/config.h"
#include "core/log.h"
#include "engine/rendering/vulkan/renderer.h"
#include "engine/rendering/vulkan/vk.h"

namespace nfs::vk::stream
{
namespace
{
typedef void (*Replay)(const uint8_t *payload);
struct Header
{
    /** Null: the rest of the ring is unused, the next packet is at its start. */
    Replay replay;
    /** Header and payload, a multiple of 8. */
    uint32_t size;
};
static_assert(sizeof(Header) == 8, "packets are 8-byte aligned");

const uint32_t kRingBytes = 4u << 20;
/** Packets sent before the thread is told (each publish is a fenced store). */
const uint32_t kPublishEvery = 24;
/** Packets the thread makes before it reports its progress (also when it runs out of them). */
const uint32_t kReportEvery = 64;
/** Pauses the thread spins for more before it sleeps. */
const uint32_t kSpins = 20000;

uint8_t *s_ring;
/** Positions grow without wrapping back (unsigned arithmetic); the ring offset is position % kRingBytes. */
__declspec(align(64)) volatile LONG s_published;
__declspec(align(64)) volatile LONG s_done;
__declspec(align(64)) volatile LONG s_sleeping;
uint32_t s_write;
uint32_t s_unpublished;
HANDLE s_wake;
HANDLE s_thread;
bool s_installed;
VkCommandBuffer s_cmd = VK_NULL_HANDLE;
VkCommandPool s_pool = VK_NULL_HANDLE;

uint32_t Align8(uint32_t bytes)
{
    return (bytes + 7u) & ~7u;
}

void Publish()
{
    if (!s_unpublished)
        return;
    s_unpublished = 0;
    _ReadWriteBarrier();
    InterlockedExchange(&s_published, static_cast<LONG>(s_write));
    if (s_sleeping)
        SetEvent(s_wake);
}

/** s_done as last read by the main thread. */
uint32_t s_knownDone;

/** Room for a packet of `payload` bytes; returns where its payload goes. */
uint8_t *Reserve(Replay replay, uint32_t payload)
{
    const uint32_t size = sizeof(Header) + Align8(payload);
    uint32_t offset = s_write % kRingBytes;
    const uint32_t tail = kRingBytes - offset;
    const uint32_t need = size <= tail ? size : tail + size;
    // The thread's progress is read only when the room known of runs out (the line it is on is the thread's to write).
    if (static_cast<uint32_t>(s_write - s_knownDone) + need > kRingBytes)
    {
        s_knownDone = static_cast<uint32_t>(s_done);
        if (static_cast<uint32_t>(s_write - s_knownDone) + need > kRingBytes)
        {
            Publish();
            while (static_cast<uint32_t>(s_write - (s_knownDone = static_cast<uint32_t>(s_done))) + need > kRingBytes)
                _mm_pause();
        }
    }
    if (size > tail)
    {
        Header *wrap = reinterpret_cast<Header *>(s_ring + offset);
        wrap->replay = nullptr;
        wrap->size = tail;
        s_write += tail;
        offset = 0;
    }
    Header *h = reinterpret_cast<Header *>(s_ring + offset);
    h->replay = replay;
    h->size = size;
    s_write += size;
    return s_ring + offset + sizeof(Header);
}

void DrainCalls();
bool s_sync;
void Commit()
{
    if (++s_unpublished >= kPublishEvery)
        Publish();
    if (s_sync)
        DrainCalls();
}

template <typename T> T *Copy(uint8_t *&cursor, const T *source, uint32_t count)
{
    if (!source || !count)
        return nullptr;
    T *out = reinterpret_cast<T *>(cursor);
    memcpy(out, source, sizeof(T) * count);
    cursor += Align8(static_cast<uint32_t>(sizeof(T) * count));
    return out;
}

template <typename T> uint32_t Bytes(const T *source, uint32_t count)
{
    return source && count ? Align8(static_cast<uint32_t>(sizeof(T) * count)) : 0u;
}

template <typename Args> Args *Emit(Replay replay, uint32_t extra, uint8_t *&cursor)
{
    uint8_t *p = Reserve(replay, Align8(sizeof(Args)) + extra);
    cursor = p + Align8(sizeof(Args));
    return reinterpret_cast<Args *>(p);
}

DWORD WINAPI Run(void *)
{
    uint32_t read = 0, sinceReport = 0;
    for (;;)
    {
        uint32_t end = static_cast<uint32_t>(s_published);
        if (read == end)
        {
            for (uint32_t spin = 0; spin < kSpins && read == end; ++spin)
            {
                _mm_pause();
                end = static_cast<uint32_t>(s_published);
            }
            if (read == end)
            {
                InterlockedExchange(&s_sleeping, 1);
                end = static_cast<uint32_t>(s_published);
                if (read == end)
                    WaitForSingleObject(s_wake, INFINITE);
                InterlockedExchange(&s_sleeping, 0);
                continue;
            }
        }
        while (read != end)
        {
            const Header *h = reinterpret_cast<const Header *>(s_ring + read % kRingBytes);
            if (h->replay)
                h->replay(reinterpret_cast<const uint8_t *>(h) + sizeof(Header));
            read += h->size;
            // Progress goes out every few packets (the main thread reads it only when the ring fills) and when the
            // packets run out. The driver writes the commands into write-combined memory: those stores sit in this
            // core's buffers until a fence, so they are flushed before the main thread may take the command buffer
            // back (and submit it).
            if (read == end)
            {
                _mm_sfence();
                _ReadWriteBarrier();
                s_done = static_cast<LONG>(read);
            }
            else if (++sinceReport >= kReportEvery)
            {
                sinceReport = 0;
                _ReadWriteBarrier();
                s_done = static_cast<LONG>(read);
            }
        }
    }
}

// The driver's functions, as loaded.
PFN_vkCmdBindPipeline r_bindPipeline;
PFN_vkCmdPushConstants r_pushConstants;
PFN_vkCmdBindDescriptorSets r_bindDescriptorSets;
PFN_vkCmdPushDescriptorSetKHR r_pushDescriptorSet;
PFN_vkCmdBindVertexBuffers2 r_bindVertexBuffers2;
PFN_vkCmdBindVertexBuffers r_bindVertexBuffers;
PFN_vkCmdBindIndexBuffer r_bindIndexBuffer;
PFN_vkCmdDraw r_draw;
PFN_vkCmdDrawIndexed r_drawIndexed;
PFN_vkCmdSetViewport r_setViewport;
PFN_vkCmdSetScissor r_setScissor;
PFN_vkCmdSetCullMode r_setCullMode;
PFN_vkCmdSetFrontFace r_setFrontFace;
PFN_vkCmdSetPrimitiveTopology r_setPrimitiveTopology;
PFN_vkCmdSetDepthTestEnable r_setDepthTestEnable;
PFN_vkCmdSetDepthWriteEnable r_setDepthWriteEnable;
PFN_vkCmdSetDepthCompareOp r_setDepthCompareOp;
PFN_vkCmdSetDepthBoundsTestEnable r_setDepthBoundsTestEnable;
PFN_vkCmdSetStencilTestEnable r_setStencilTestEnable;
PFN_vkCmdSetStencilOp r_setStencilOp;
PFN_vkCmdSetStencilReference r_setStencilReference;
PFN_vkCmdSetStencilCompareMask r_setStencilCompareMask;
PFN_vkCmdSetStencilWriteMask r_setStencilWriteMask;
PFN_vkCmdSetDepthBias r_setDepthBias;
PFN_vkCmdSetDepthBiasEnable r_setDepthBiasEnable;
PFN_vkCmdSetBlendConstants r_setBlendConstants;
PFN_vkCmdSetRasterizerDiscardEnable r_setRasterizerDiscardEnable;
PFN_vkCmdSetPrimitiveRestartEnable r_setPrimitiveRestartEnable;
PFN_vkCmdBeginRendering r_beginRendering;
PFN_vkCmdEndRendering r_endRendering;
PFN_vkCmdPipelineBarrier2 r_pipelineBarrier2;
PFN_vkCmdPipelineBarrier r_pipelineBarrier;
PFN_vkCmdCopyBuffer r_copyBuffer;
PFN_vkCmdCopyImage r_copyImage;
PFN_vkCmdCopyBufferToImage r_copyBufferToImage;
PFN_vkCmdCopyImageToBuffer r_copyImageToBuffer;
PFN_vkCmdBlitImage r_blitImage;
PFN_vkCmdClearColorImage r_clearColorImage;
PFN_vkCmdClearDepthStencilImage r_clearDepthStencilImage;
PFN_vkCmdClearAttachments r_clearAttachments;
PFN_vkCmdResetQueryPool r_resetQueryPool;
PFN_vkCmdWriteTimestamp2 r_writeTimestamp2;
PFN_vkCmdWriteTimestamp r_writeTimestamp;
PFN_vkCmdWriteBufferMarkerAMD r_writeBufferMarker;
PFN_vkCmdBeginRenderPass r_beginRenderPass;
PFN_vkCmdEndRenderPass r_endRenderPass;
PFN_vkBeginCommandBuffer r_beginCommandBuffer;
PFN_vkEndCommandBuffer r_endCommandBuffer;
PFN_vkResetCommandPool r_resetCommandPool;
PFN_vkFreeCommandBuffers r_freeCommandBuffers;
PFN_vkDestroyCommandPool r_destroyCommandPool;
PFN_vkDeviceWaitIdle r_deviceWaitIdle;

bool Deferred(VkCommandBuffer c)
{
    return c == s_cmd && c != VK_NULL_HANDLE;
}

// Commands with arguments by value only.

struct BindPipelineArgs
{
    VkCommandBuffer c;
    VkPipelineBindPoint point;
    VkPipeline pipeline;
};
VKAPI_ATTR void VKAPI_CALL BindPipeline(VkCommandBuffer c, VkPipelineBindPoint point, VkPipeline pipeline)
{
    if (!Deferred(c))
        return r_bindPipeline(c, point, pipeline);
    uint8_t *cursor;
    BindPipelineArgs *a = Emit<BindPipelineArgs>(
        [](const uint8_t *p) {
            const BindPipelineArgs &a = *reinterpret_cast<const BindPipelineArgs *>(p);
            r_bindPipeline(a.c, a.point, a.pipeline);
        },
        0, cursor);
    a->c = c;
    a->point = point;
    a->pipeline = pipeline;
    Commit();
}

struct BindIndexBufferArgs
{
    VkCommandBuffer c;
    VkBuffer buffer;
    VkDeviceSize offset;
    VkIndexType type;
};
VKAPI_ATTR void VKAPI_CALL BindIndexBuffer(VkCommandBuffer c, VkBuffer buffer, VkDeviceSize offset, VkIndexType type)
{
    if (!Deferred(c))
        return r_bindIndexBuffer(c, buffer, offset, type);
    uint8_t *cursor;
    BindIndexBufferArgs *a = Emit<BindIndexBufferArgs>(
        [](const uint8_t *p) {
            const BindIndexBufferArgs &a = *reinterpret_cast<const BindIndexBufferArgs *>(p);
            r_bindIndexBuffer(a.c, a.buffer, a.offset, a.type);
        },
        0, cursor);
    a->c = c;
    a->buffer = buffer;
    a->offset = offset;
    a->type = type;
    Commit();
}

struct DrawArgs
{
    VkCommandBuffer c;
    uint32_t count;
    uint32_t instances;
    uint32_t first;
    int32_t base;
    uint32_t firstInstance;
};
VKAPI_ATTR void VKAPI_CALL DrawIndexed(VkCommandBuffer c, uint32_t count, uint32_t instances, uint32_t first,
                                       int32_t base, uint32_t firstInstance)
{
    if (!Deferred(c))
        return r_drawIndexed(c, count, instances, first, base, firstInstance);
    uint8_t *cursor;
    DrawArgs *a = Emit<DrawArgs>(
        [](const uint8_t *p) {
            const DrawArgs &a = *reinterpret_cast<const DrawArgs *>(p);
            r_drawIndexed(a.c, a.count, a.instances, a.first, a.base, a.firstInstance);
        },
        0, cursor);
    a->c = c;
    a->count = count;
    a->instances = instances;
    a->first = first;
    a->base = base;
    a->firstInstance = firstInstance;
    Commit();
}

VKAPI_ATTR void VKAPI_CALL Draw(VkCommandBuffer c, uint32_t count, uint32_t instances, uint32_t first,
                                uint32_t firstInstance)
{
    if (!Deferred(c))
        return r_draw(c, count, instances, first, firstInstance);
    uint8_t *cursor;
    DrawArgs *a = Emit<DrawArgs>(
        [](const uint8_t *p) {
            const DrawArgs &a = *reinterpret_cast<const DrawArgs *>(p);
            r_draw(a.c, a.count, a.instances, a.first, a.firstInstance);
        },
        0, cursor);
    a->c = c;
    a->count = count;
    a->instances = instances;
    a->first = first;
    a->base = 0;
    a->firstInstance = firstInstance;
    Commit();
}

/** A command with one 32-bit value. */
struct ValueArgs
{
    VkCommandBuffer c;
    uint32_t value;
    uint32_t value2;
    uint32_t value3;
    uint32_t value4;
    uint32_t value5;
};
#define NFS_STREAM_VALUE(Name, real, Type)                                                                             \
    VKAPI_ATTR void VKAPI_CALL Name(VkCommandBuffer c, Type value)                                                     \
    {                                                                                                                  \
        if (!Deferred(c))                                                                                         \
            return real(c, value);                                                                                     \
        uint8_t *cursor;                                                                                               \
        ValueArgs *a = Emit<ValueArgs>(                                                                                \
            [](const uint8_t *p) {                                                                                     \
                const ValueArgs &a = *reinterpret_cast<const ValueArgs *>(p);                                          \
                real(a.c, static_cast<Type>(a.value));                                                                 \
            },                                                                                                         \
            0, cursor);                                                                                                \
        a->c = c;                                                                                                      \
        a->value = static_cast<uint32_t>(value);                                                                       \
        Commit();                                                                                                      \
    }
NFS_STREAM_VALUE(SetCullMode, r_setCullMode, VkCullModeFlags)
NFS_STREAM_VALUE(SetFrontFace, r_setFrontFace, VkFrontFace)
NFS_STREAM_VALUE(SetPrimitiveTopology, r_setPrimitiveTopology, VkPrimitiveTopology)
NFS_STREAM_VALUE(SetDepthTestEnable, r_setDepthTestEnable, VkBool32)
NFS_STREAM_VALUE(SetDepthWriteEnable, r_setDepthWriteEnable, VkBool32)
NFS_STREAM_VALUE(SetDepthCompareOp, r_setDepthCompareOp, VkCompareOp)
NFS_STREAM_VALUE(SetDepthBoundsTestEnable, r_setDepthBoundsTestEnable, VkBool32)
NFS_STREAM_VALUE(SetStencilTestEnable, r_setStencilTestEnable, VkBool32)
NFS_STREAM_VALUE(SetDepthBiasEnable, r_setDepthBiasEnable, VkBool32)
NFS_STREAM_VALUE(SetRasterizerDiscardEnable, r_setRasterizerDiscardEnable, VkBool32)
NFS_STREAM_VALUE(SetPrimitiveRestartEnable, r_setPrimitiveRestartEnable, VkBool32)
#undef NFS_STREAM_VALUE

#define NFS_STREAM_FACE_VALUE(Name, real)                                                                              \
    VKAPI_ATTR void VKAPI_CALL Name(VkCommandBuffer c, VkStencilFaceFlags faces, uint32_t value)                       \
    {                                                                                                                  \
        if (!Deferred(c))                                                                                         \
            return real(c, faces, value);                                                                              \
        uint8_t *cursor;                                                                                               \
        ValueArgs *a = Emit<ValueArgs>(                                                                                \
            [](const uint8_t *p) {                                                                                     \
                const ValueArgs &a = *reinterpret_cast<const ValueArgs *>(p);                                          \
                real(a.c, a.value, a.value2);                                                                          \
            },                                                                                                         \
            0, cursor);                                                                                                \
        a->c = c;                                                                                                      \
        a->value = faces;                                                                                              \
        a->value2 = value;                                                                                             \
        Commit();                                                                                                      \
    }
NFS_STREAM_FACE_VALUE(SetStencilReference, r_setStencilReference)
NFS_STREAM_FACE_VALUE(SetStencilCompareMask, r_setStencilCompareMask)
NFS_STREAM_FACE_VALUE(SetStencilWriteMask, r_setStencilWriteMask)
#undef NFS_STREAM_FACE_VALUE

VKAPI_ATTR void VKAPI_CALL SetStencilOp(VkCommandBuffer c, VkStencilFaceFlags faces, VkStencilOp fail,
                                        VkStencilOp pass, VkStencilOp depthFail, VkCompareOp compare)
{
    if (!Deferred(c))
        return r_setStencilOp(c, faces, fail, pass, depthFail, compare);
    uint8_t *cursor;
    ValueArgs *a = Emit<ValueArgs>(
        [](const uint8_t *p) {
            const ValueArgs &a = *reinterpret_cast<const ValueArgs *>(p);
            r_setStencilOp(a.c, a.value, static_cast<VkStencilOp>(a.value2), static_cast<VkStencilOp>(a.value3),
                           static_cast<VkStencilOp>(a.value4), static_cast<VkCompareOp>(a.value5));
        },
        0, cursor);
    a->c = c;
    a->value = faces;
    a->value2 = static_cast<uint32_t>(fail);
    a->value3 = static_cast<uint32_t>(pass);
    a->value4 = static_cast<uint32_t>(depthFail);
    a->value5 = static_cast<uint32_t>(compare);
    Commit();
}

struct FloatsArgs
{
    VkCommandBuffer c;
    float v[4];
};
VKAPI_ATTR void VKAPI_CALL SetDepthBias(VkCommandBuffer c, float constant, float clamp, float slope)
{
    if (!Deferred(c))
        return r_setDepthBias(c, constant, clamp, slope);
    uint8_t *cursor;
    FloatsArgs *a = Emit<FloatsArgs>(
        [](const uint8_t *p) {
            const FloatsArgs &a = *reinterpret_cast<const FloatsArgs *>(p);
            r_setDepthBias(a.c, a.v[0], a.v[1], a.v[2]);
        },
        0, cursor);
    a->c = c;
    a->v[0] = constant;
    a->v[1] = clamp;
    a->v[2] = slope;
    a->v[3] = 0.0f;
    Commit();
}

VKAPI_ATTR void VKAPI_CALL SetBlendConstants(VkCommandBuffer c, const float constants[4])
{
    if (!Deferred(c))
        return r_setBlendConstants(c, constants);
    uint8_t *cursor;
    FloatsArgs *a = Emit<FloatsArgs>(
        [](const uint8_t *p) {
            const FloatsArgs &a = *reinterpret_cast<const FloatsArgs *>(p);
            r_setBlendConstants(a.c, a.v);
        },
        0, cursor);
    a->c = c;
    memcpy(a->v, constants, sizeof(a->v));
    Commit();
}

VKAPI_ATTR void VKAPI_CALL EndRendering(VkCommandBuffer c)
{
    if (!Deferred(c))
        return r_endRendering(c);
    uint8_t *cursor;
    ValueArgs *a = Emit<ValueArgs>(
        [](const uint8_t *p) { r_endRendering(reinterpret_cast<const ValueArgs *>(p)->c); }, 0, cursor);
    a->c = c;
    Commit();
}

struct QueryArgs
{
    VkCommandBuffer c;
    VkQueryPool pool;
    VkPipelineStageFlags2 stage;
    uint32_t first;
    uint32_t count;
};
VKAPI_ATTR void VKAPI_CALL ResetQueryPool(VkCommandBuffer c, VkQueryPool pool, uint32_t first, uint32_t count)
{
    if (!Deferred(c))
        return r_resetQueryPool(c, pool, first, count);
    uint8_t *cursor;
    QueryArgs *a = Emit<QueryArgs>(
        [](const uint8_t *p) {
            const QueryArgs &a = *reinterpret_cast<const QueryArgs *>(p);
            r_resetQueryPool(a.c, a.pool, a.first, a.count);
        },
        0, cursor);
    a->c = c;
    a->pool = pool;
    a->first = first;
    a->count = count;
    Commit();
}

VKAPI_ATTR void VKAPI_CALL WriteTimestamp2(VkCommandBuffer c, VkPipelineStageFlags2 stage, VkQueryPool pool,
                                           uint32_t query)
{
    if (!Deferred(c))
        return r_writeTimestamp2(c, stage, pool, query);
    uint8_t *cursor;
    QueryArgs *a = Emit<QueryArgs>(
        [](const uint8_t *p) {
            const QueryArgs &a = *reinterpret_cast<const QueryArgs *>(p);
            r_writeTimestamp2(a.c, a.stage, a.pool, a.first);
        },
        0, cursor);
    a->c = c;
    a->pool = pool;
    a->stage = stage;
    a->first = query;
    Commit();
}

struct MarkerArgs
{
    VkCommandBuffer c;
    VkPipelineStageFlagBits stage;
    VkBuffer buffer;
    VkDeviceSize offset;
    uint32_t marker;
};
VKAPI_ATTR void VKAPI_CALL WriteBufferMarker(VkCommandBuffer c, VkPipelineStageFlagBits stage, VkBuffer buffer,
                                             VkDeviceSize offset, uint32_t marker)
{
    if (!Deferred(c))
        return r_writeBufferMarker(c, stage, buffer, offset, marker);
    uint8_t *cursor;
    MarkerArgs *a = Emit<MarkerArgs>(
        [](const uint8_t *p) {
            const MarkerArgs &a = *reinterpret_cast<const MarkerArgs *>(p);
            r_writeBufferMarker(a.c, a.stage, a.buffer, a.offset, a.marker);
        },
        0, cursor);
    a->c = c;
    a->stage = stage;
    a->buffer = buffer;
    a->offset = offset;
    a->marker = marker;
    Commit();
}

// Commands with arrays and structures, copied into the packet.

struct PushConstantsArgs
{
    VkCommandBuffer c;
    VkPipelineLayout layout;
    VkShaderStageFlags stages;
    uint32_t offset;
    uint32_t size;
    const void *values;
};
VKAPI_ATTR void VKAPI_CALL PushConstants(VkCommandBuffer c, VkPipelineLayout layout, VkShaderStageFlags stages,
                                         uint32_t offset, uint32_t size, const void *values)
{
    if (!Deferred(c))
        return r_pushConstants(c, layout, stages, offset, size, values);
    uint8_t *cursor;
    PushConstantsArgs *a = Emit<PushConstantsArgs>(
        [](const uint8_t *p) {
            const PushConstantsArgs &a = *reinterpret_cast<const PushConstantsArgs *>(p);
            r_pushConstants(a.c, a.layout, a.stages, a.offset, a.size, a.values);
        },
        Bytes(static_cast<const uint8_t *>(values), size), cursor);
    a->c = c;
    a->layout = layout;
    a->stages = stages;
    a->offset = offset;
    a->size = size;
    a->values = Copy(cursor, static_cast<const uint8_t *>(values), size);
    Commit();
}

struct BindDescriptorSetsArgs
{
    VkCommandBuffer c;
    VkPipelineBindPoint point;
    VkPipelineLayout layout;
    uint32_t first;
    uint32_t count;
    const VkDescriptorSet *sets;
    uint32_t dynamicCount;
    const uint32_t *dynamic;
};
VKAPI_ATTR void VKAPI_CALL BindDescriptorSets(VkCommandBuffer c, VkPipelineBindPoint point, VkPipelineLayout layout,
                                              uint32_t first, uint32_t count, const VkDescriptorSet *sets,
                                              uint32_t dynamicCount, const uint32_t *dynamic)
{
    if (!Deferred(c))
        return r_bindDescriptorSets(c, point, layout, first, count, sets, dynamicCount, dynamic);
    uint8_t *cursor;
    BindDescriptorSetsArgs *a = Emit<BindDescriptorSetsArgs>(
        [](const uint8_t *p) {
            const BindDescriptorSetsArgs &a = *reinterpret_cast<const BindDescriptorSetsArgs *>(p);
            r_bindDescriptorSets(a.c, a.point, a.layout, a.first, a.count, a.sets, a.dynamicCount, a.dynamic);
        },
        Bytes(sets, count) + Bytes(dynamic, dynamicCount), cursor);
    a->c = c;
    a->point = point;
    a->layout = layout;
    a->first = first;
    a->count = count;
    a->sets = Copy(cursor, sets, count);
    a->dynamicCount = dynamicCount;
    a->dynamic = Copy(cursor, dynamic, dynamicCount);
    Commit();
}

bool IsImageDescriptor(VkDescriptorType t)
{
    return t == VK_DESCRIPTOR_TYPE_SAMPLER || t == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
           t == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || t == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
           t == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
}

bool IsBufferDescriptor(VkDescriptorType t)
{
    return t == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || t == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
           t == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || t == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
}

void DrainCalls();

struct PushDescriptorSetArgs
{
    VkCommandBuffer c;
    VkPipelineBindPoint point;
    VkPipelineLayout layout;
    uint32_t set;
    uint32_t count;
    VkWriteDescriptorSet *writes;
};
VKAPI_ATTR void VKAPI_CALL PushDescriptorSet(VkCommandBuffer c, VkPipelineBindPoint point, VkPipelineLayout layout,
                                             uint32_t set, uint32_t count, const VkWriteDescriptorSet *writes)
{
    if (!Deferred(c))
        return r_pushDescriptorSet(c, point, layout, set, count, writes);
    uint32_t extra = Bytes(writes, count);
    for (uint32_t i = 0; i < count; ++i)
    {
        const VkWriteDescriptorSet &w = writes[i];
        if (w.pNext)
        {
            DrainCalls();
            return r_pushDescriptorSet(c, point, layout, set, count, writes);
        }
        if (IsImageDescriptor(w.descriptorType))
            extra += Bytes(w.pImageInfo, w.descriptorCount);
        else if (IsBufferDescriptor(w.descriptorType))
            extra += Bytes(w.pBufferInfo, w.descriptorCount);
        else
            extra += Bytes(w.pTexelBufferView, w.descriptorCount);
    }
    uint8_t *cursor;
    PushDescriptorSetArgs *a = Emit<PushDescriptorSetArgs>(
        [](const uint8_t *p) {
            const PushDescriptorSetArgs &a = *reinterpret_cast<const PushDescriptorSetArgs *>(p);
            r_pushDescriptorSet(a.c, a.point, a.layout, a.set, a.count, a.writes);
        },
        extra, cursor);
    a->c = c;
    a->point = point;
    a->layout = layout;
    a->set = set;
    a->count = count;
    a->writes = Copy(cursor, writes, count);
    for (uint32_t i = 0; i < count; ++i)
    {
        VkWriteDescriptorSet &w = a->writes[i];
        const bool image = IsImageDescriptor(w.descriptorType), buffer = IsBufferDescriptor(w.descriptorType);
        w.pImageInfo = image ? Copy(cursor, w.pImageInfo, w.descriptorCount) : nullptr;
        w.pBufferInfo = buffer ? Copy(cursor, w.pBufferInfo, w.descriptorCount) : nullptr;
        w.pTexelBufferView = !image && !buffer ? Copy(cursor, w.pTexelBufferView, w.descriptorCount) : nullptr;
    }
    Commit();
}

struct BindVertexBuffersArgs
{
    VkCommandBuffer c;
    uint32_t first;
    uint32_t count;
    const VkBuffer *buffers;
    const VkDeviceSize *offsets;
    const VkDeviceSize *sizes;
    const VkDeviceSize *strides;
};
VKAPI_ATTR void VKAPI_CALL BindVertexBuffers2(VkCommandBuffer c, uint32_t first, uint32_t count,
                                              const VkBuffer *buffers, const VkDeviceSize *offsets,
                                              const VkDeviceSize *sizes, const VkDeviceSize *strides)
{
    if (!Deferred(c))
        return r_bindVertexBuffers2(c, first, count, buffers, offsets, sizes, strides);
    uint8_t *cursor;
    BindVertexBuffersArgs *a = Emit<BindVertexBuffersArgs>(
        [](const uint8_t *p) {
            const BindVertexBuffersArgs &a = *reinterpret_cast<const BindVertexBuffersArgs *>(p);
            r_bindVertexBuffers2(a.c, a.first, a.count, a.buffers, a.offsets, a.sizes, a.strides);
        },
        Bytes(buffers, count) + Bytes(offsets, count) + Bytes(sizes, count) + Bytes(strides, count), cursor);
    a->c = c;
    a->first = first;
    a->count = count;
    a->buffers = Copy(cursor, buffers, count);
    a->offsets = Copy(cursor, offsets, count);
    a->sizes = Copy(cursor, sizes, count);
    a->strides = Copy(cursor, strides, count);
    Commit();
}

struct RectsArgs
{
    VkCommandBuffer c;
    uint32_t first;
    uint32_t count;
    const void *items;
};
VKAPI_ATTR void VKAPI_CALL SetViewport(VkCommandBuffer c, uint32_t first, uint32_t count, const VkViewport *viewports)
{
    if (!Deferred(c))
        return r_setViewport(c, first, count, viewports);
    uint8_t *cursor;
    RectsArgs *a = Emit<RectsArgs>(
        [](const uint8_t *p) {
            const RectsArgs &a = *reinterpret_cast<const RectsArgs *>(p);
            r_setViewport(a.c, a.first, a.count, static_cast<const VkViewport *>(a.items));
        },
        Bytes(viewports, count), cursor);
    a->c = c;
    a->first = first;
    a->count = count;
    a->items = Copy(cursor, viewports, count);
    Commit();
}

VKAPI_ATTR void VKAPI_CALL SetScissor(VkCommandBuffer c, uint32_t first, uint32_t count, const VkRect2D *scissors)
{
    if (!Deferred(c))
        return r_setScissor(c, first, count, scissors);
    uint8_t *cursor;
    RectsArgs *a = Emit<RectsArgs>(
        [](const uint8_t *p) {
            const RectsArgs &a = *reinterpret_cast<const RectsArgs *>(p);
            r_setScissor(a.c, a.first, a.count, static_cast<const VkRect2D *>(a.items));
        },
        Bytes(scissors, count), cursor);
    a->c = c;
    a->first = first;
    a->count = count;
    a->items = Copy(cursor, scissors, count);
    Commit();
}

struct BeginRenderingArgs
{
    VkCommandBuffer c;
    VkRenderingInfo info;
};
VKAPI_ATTR void VKAPI_CALL BeginRendering(VkCommandBuffer c, const VkRenderingInfo *info)
{
    bool chained = info->pNext != nullptr || (info->pDepthAttachment && info->pDepthAttachment->pNext) ||
                   (info->pStencilAttachment && info->pStencilAttachment->pNext);
    for (uint32_t i = 0; i < info->colorAttachmentCount && !chained; ++i)
        chained = info->pColorAttachments[i].pNext != nullptr;
    if (!Deferred(c) || chained)
    {
        if (Deferred(c))
            DrainCalls();
        return r_beginRendering(c, info);
    }
    uint8_t *cursor;
    BeginRenderingArgs *a = Emit<BeginRenderingArgs>(
        [](const uint8_t *p) {
            const BeginRenderingArgs &a = *reinterpret_cast<const BeginRenderingArgs *>(p);
            r_beginRendering(a.c, &a.info);
        },
        Bytes(info->pColorAttachments, info->colorAttachmentCount) + Bytes(info->pDepthAttachment, 1) +
            Bytes(info->pStencilAttachment, 1),
        cursor);
    a->c = c;
    a->info = *info;
    a->info.pColorAttachments = Copy(cursor, info->pColorAttachments, info->colorAttachmentCount);
    a->info.pDepthAttachment = Copy(cursor, info->pDepthAttachment, 1);
    a->info.pStencilAttachment = Copy(cursor, info->pStencilAttachment, 1);
    Commit();
}

struct PipelineBarrier2Args
{
    VkCommandBuffer c;
    VkDependencyInfo info;
};
VKAPI_ATTR void VKAPI_CALL PipelineBarrier2(VkCommandBuffer c, const VkDependencyInfo *info)
{
    bool chained = info->pNext != nullptr;
    for (uint32_t i = 0; i < info->memoryBarrierCount && !chained; ++i)
        chained = info->pMemoryBarriers[i].pNext != nullptr;
    for (uint32_t i = 0; i < info->bufferMemoryBarrierCount && !chained; ++i)
        chained = info->pBufferMemoryBarriers[i].pNext != nullptr;
    for (uint32_t i = 0; i < info->imageMemoryBarrierCount && !chained; ++i)
        chained = info->pImageMemoryBarriers[i].pNext != nullptr;
    if (!Deferred(c) || chained)
    {
        if (Deferred(c))
            DrainCalls();
        return r_pipelineBarrier2(c, info);
    }
    uint8_t *cursor;
    PipelineBarrier2Args *a = Emit<PipelineBarrier2Args>(
        [](const uint8_t *p) {
            const PipelineBarrier2Args &a = *reinterpret_cast<const PipelineBarrier2Args *>(p);
            r_pipelineBarrier2(a.c, &a.info);
        },
        Bytes(info->pMemoryBarriers, info->memoryBarrierCount) +
            Bytes(info->pBufferMemoryBarriers, info->bufferMemoryBarrierCount) +
            Bytes(info->pImageMemoryBarriers, info->imageMemoryBarrierCount),
        cursor);
    a->c = c;
    a->info = *info;
    a->info.pMemoryBarriers = Copy(cursor, info->pMemoryBarriers, info->memoryBarrierCount);
    a->info.pBufferMemoryBarriers = Copy(cursor, info->pBufferMemoryBarriers, info->bufferMemoryBarrierCount);
    a->info.pImageMemoryBarriers = Copy(cursor, info->pImageMemoryBarriers, info->imageMemoryBarrierCount);
    Commit();
}

/** Copies and clears: two handles, two layouts or values, and an array of regions. */
struct TransferArgs
{
    VkCommandBuffer c;
    uint64_t source;
    uint64_t target;
    uint32_t sourceLayout;
    uint32_t targetLayout;
    uint32_t count;
    uint32_t filter;
    const void *regions;
    uint8_t value[sizeof(VkClearColorValue) > sizeof(VkClearDepthStencilValue) ? sizeof(VkClearColorValue)
                                                                                 : sizeof(VkClearDepthStencilValue)];
};

VKAPI_ATTR void VKAPI_CALL CopyBuffer(VkCommandBuffer c, VkBuffer source, VkBuffer target, uint32_t count,
                                      const VkBufferCopy *regions)
{
    if (!Deferred(c))
        return r_copyBuffer(c, source, target, count, regions);
    uint8_t *cursor;
    TransferArgs *a = Emit<TransferArgs>(
        [](const uint8_t *p) {
            const TransferArgs &a = *reinterpret_cast<const TransferArgs *>(p);
            r_copyBuffer(a.c, (VkBuffer)(a.source), (VkBuffer)(a.target), a.count,
                         static_cast<const VkBufferCopy *>(a.regions));
        },
        Bytes(regions, count), cursor);
    a->c = c;
    a->source = (uint64_t)(source);
    a->target = (uint64_t)(target);
    a->count = count;
    a->regions = Copy(cursor, regions, count);
    Commit();
}

VKAPI_ATTR void VKAPI_CALL CopyImage(VkCommandBuffer c, VkImage source, VkImageLayout sourceLayout, VkImage target,
                                     VkImageLayout targetLayout, uint32_t count, const VkImageCopy *regions)
{
    if (!Deferred(c))
        return r_copyImage(c, source, sourceLayout, target, targetLayout, count, regions);
    uint8_t *cursor;
    TransferArgs *a = Emit<TransferArgs>(
        [](const uint8_t *p) {
            const TransferArgs &a = *reinterpret_cast<const TransferArgs *>(p);
            r_copyImage(a.c, (VkImage)(a.source), static_cast<VkImageLayout>(a.sourceLayout),
                        (VkImage)(a.target), static_cast<VkImageLayout>(a.targetLayout), a.count,
                        static_cast<const VkImageCopy *>(a.regions));
        },
        Bytes(regions, count), cursor);
    a->c = c;
    a->source = (uint64_t)(source);
    a->target = (uint64_t)(target);
    a->sourceLayout = static_cast<uint32_t>(sourceLayout);
    a->targetLayout = static_cast<uint32_t>(targetLayout);
    a->count = count;
    a->regions = Copy(cursor, regions, count);
    Commit();
}

VKAPI_ATTR void VKAPI_CALL CopyBufferToImage(VkCommandBuffer c, VkBuffer source, VkImage target,
                                             VkImageLayout targetLayout, uint32_t count,
                                             const VkBufferImageCopy *regions)
{
    if (!Deferred(c))
        return r_copyBufferToImage(c, source, target, targetLayout, count, regions);
    uint8_t *cursor;
    TransferArgs *a = Emit<TransferArgs>(
        [](const uint8_t *p) {
            const TransferArgs &a = *reinterpret_cast<const TransferArgs *>(p);
            r_copyBufferToImage(a.c, (VkBuffer)(a.source), (VkImage)(a.target),
                                static_cast<VkImageLayout>(a.targetLayout), a.count,
                                static_cast<const VkBufferImageCopy *>(a.regions));
        },
        Bytes(regions, count), cursor);
    a->c = c;
    a->source = (uint64_t)(source);
    a->target = (uint64_t)(target);
    a->targetLayout = static_cast<uint32_t>(targetLayout);
    a->count = count;
    a->regions = Copy(cursor, regions, count);
    Commit();
}

VKAPI_ATTR void VKAPI_CALL CopyImageToBuffer(VkCommandBuffer c, VkImage source, VkImageLayout sourceLayout,
                                             VkBuffer target, uint32_t count, const VkBufferImageCopy *regions)
{
    if (!Deferred(c))
        return r_copyImageToBuffer(c, source, sourceLayout, target, count, regions);
    uint8_t *cursor;
    TransferArgs *a = Emit<TransferArgs>(
        [](const uint8_t *p) {
            const TransferArgs &a = *reinterpret_cast<const TransferArgs *>(p);
            r_copyImageToBuffer(a.c, (VkImage)(a.source), static_cast<VkImageLayout>(a.sourceLayout),
                                (VkBuffer)(a.target), a.count,
                                static_cast<const VkBufferImageCopy *>(a.regions));
        },
        Bytes(regions, count), cursor);
    a->c = c;
    a->source = (uint64_t)(source);
    a->target = (uint64_t)(target);
    a->sourceLayout = static_cast<uint32_t>(sourceLayout);
    a->count = count;
    a->regions = Copy(cursor, regions, count);
    Commit();
}

VKAPI_ATTR void VKAPI_CALL BlitImage(VkCommandBuffer c, VkImage source, VkImageLayout sourceLayout, VkImage target,
                                     VkImageLayout targetLayout, uint32_t count, const VkImageBlit *regions,
                                     VkFilter filter)
{
    if (!Deferred(c))
        return r_blitImage(c, source, sourceLayout, target, targetLayout, count, regions, filter);
    uint8_t *cursor;
    TransferArgs *a = Emit<TransferArgs>(
        [](const uint8_t *p) {
            const TransferArgs &a = *reinterpret_cast<const TransferArgs *>(p);
            r_blitImage(a.c, (VkImage)(a.source), static_cast<VkImageLayout>(a.sourceLayout),
                        (VkImage)(a.target), static_cast<VkImageLayout>(a.targetLayout), a.count,
                        static_cast<const VkImageBlit *>(a.regions), static_cast<VkFilter>(a.filter));
        },
        Bytes(regions, count), cursor);
    a->c = c;
    a->source = (uint64_t)(source);
    a->target = (uint64_t)(target);
    a->sourceLayout = static_cast<uint32_t>(sourceLayout);
    a->targetLayout = static_cast<uint32_t>(targetLayout);
    a->count = count;
    a->filter = static_cast<uint32_t>(filter);
    a->regions = Copy(cursor, regions, count);
    Commit();
}

VKAPI_ATTR void VKAPI_CALL ClearColorImage(VkCommandBuffer c, VkImage image, VkImageLayout layout,
                                           const VkClearColorValue *color, uint32_t count,
                                           const VkImageSubresourceRange *ranges)
{
    if (!Deferred(c))
        return r_clearColorImage(c, image, layout, color, count, ranges);
    uint8_t *cursor;
    TransferArgs *a = Emit<TransferArgs>(
        [](const uint8_t *p) {
            const TransferArgs &a = *reinterpret_cast<const TransferArgs *>(p);
            r_clearColorImage(a.c, (VkImage)(a.target), static_cast<VkImageLayout>(a.targetLayout),
                              reinterpret_cast<const VkClearColorValue *>(a.value), a.count,
                              static_cast<const VkImageSubresourceRange *>(a.regions));
        },
        Bytes(ranges, count), cursor);
    a->c = c;
    a->target = (uint64_t)(image);
    a->targetLayout = static_cast<uint32_t>(layout);
    memcpy(a->value, color, sizeof(*color));
    a->count = count;
    a->regions = Copy(cursor, ranges, count);
    Commit();
}

VKAPI_ATTR void VKAPI_CALL ClearDepthStencilImage(VkCommandBuffer c, VkImage image, VkImageLayout layout,
                                                  const VkClearDepthStencilValue *value, uint32_t count,
                                                  const VkImageSubresourceRange *ranges)
{
    if (!Deferred(c))
        return r_clearDepthStencilImage(c, image, layout, value, count, ranges);
    uint8_t *cursor;
    TransferArgs *a = Emit<TransferArgs>(
        [](const uint8_t *p) {
            const TransferArgs &a = *reinterpret_cast<const TransferArgs *>(p);
            r_clearDepthStencilImage(a.c, (VkImage)(a.target),
                                     static_cast<VkImageLayout>(a.targetLayout),
                                     reinterpret_cast<const VkClearDepthStencilValue *>(a.value), a.count,
                                     static_cast<const VkImageSubresourceRange *>(a.regions));
        },
        Bytes(ranges, count), cursor);
    a->c = c;
    a->target = (uint64_t)(image);
    a->targetLayout = static_cast<uint32_t>(layout);
    memcpy(a->value, value, sizeof(*value));
    a->count = count;
    a->regions = Copy(cursor, ranges, count);
    Commit();
}

struct ClearAttachmentsArgs
{
    VkCommandBuffer c;
    uint32_t count;
    const VkClearAttachment *attachments;
    uint32_t rectCount;
    const VkClearRect *rects;
};
VKAPI_ATTR void VKAPI_CALL ClearAttachments(VkCommandBuffer c, uint32_t count, const VkClearAttachment *attachments,
                                            uint32_t rectCount, const VkClearRect *rects)
{
    if (!Deferred(c))
        return r_clearAttachments(c, count, attachments, rectCount, rects);
    uint8_t *cursor;
    ClearAttachmentsArgs *a = Emit<ClearAttachmentsArgs>(
        [](const uint8_t *p) {
            const ClearAttachmentsArgs &a = *reinterpret_cast<const ClearAttachmentsArgs *>(p);
            r_clearAttachments(a.c, a.count, a.attachments, a.rectCount, a.rects);
        },
        Bytes(attachments, count) + Bytes(rects, rectCount), cursor);
    a->c = c;
    a->count = count;
    a->attachments = Copy(cursor, attachments, count);
    a->rectCount = rectCount;
    a->rects = Copy(cursor, rects, rectCount);
    Commit();
}

// The Vulkan 1.0 path's rare commands (source/engine/rendering/vulkan/Compat.cpp) are made directly, after the thread has caught up.

VKAPI_ATTR void VKAPI_CALL PipelineBarrier(VkCommandBuffer c, VkPipelineStageFlags from, VkPipelineStageFlags to,
                                           VkDependencyFlags flags, uint32_t memoryCount,
                                           const VkMemoryBarrier *memory, uint32_t bufferCount,
                                           const VkBufferMemoryBarrier *buffers, uint32_t imageCount,
                                           const VkImageMemoryBarrier *images)
{
    if (Deferred(c))
        DrainCalls();
    r_pipelineBarrier(c, from, to, flags, memoryCount, memory, bufferCount, buffers, imageCount, images);
}

VKAPI_ATTR void VKAPI_CALL WriteTimestamp(VkCommandBuffer c, VkPipelineStageFlagBits stage, VkQueryPool pool,
                                          uint32_t query)
{
    if (Deferred(c))
        DrainCalls();
    r_writeTimestamp(c, stage, pool, query);
}

VKAPI_ATTR void VKAPI_CALL BindVertexBuffers(VkCommandBuffer c, uint32_t first, uint32_t count,
                                             const VkBuffer *buffers, const VkDeviceSize *offsets)
{
    if (Deferred(c))
        DrainCalls();
    r_bindVertexBuffers(c, first, count, buffers, offsets);
}

VKAPI_ATTR void VKAPI_CALL BeginRenderPass(VkCommandBuffer c, const VkRenderPassBeginInfo *info,
                                           VkSubpassContents contents)
{
    if (Deferred(c))
        DrainCalls();
    r_beginRenderPass(c, info, contents);
}

VKAPI_ATTR void VKAPI_CALL EndRenderPass(VkCommandBuffer c)
{
    if (Deferred(c))
        DrainCalls();
    r_endRenderPass(c);
}

// What hands the frame's command buffer back to the main thread.

struct BeginArgs
{
    VkCommandBuffer c;
    VkCommandBufferBeginInfo info;
};
VKAPI_ATTR VkResult VKAPI_CALL BeginCommandBuffer(VkCommandBuffer c, const VkCommandBufferBeginInfo *info)
{
    if (!Deferred(c) || info->pNext || info->pInheritanceInfo)
    {
        if (c == s_cmd)
            DrainCalls();
        return r_beginCommandBuffer(c, info);
    }
    uint8_t *cursor;
    BeginArgs *a = Emit<BeginArgs>(
        [](const uint8_t *p) {
            const BeginArgs &a = *reinterpret_cast<const BeginArgs *>(p);
            r_beginCommandBuffer(a.c, &a.info);
        },
        0, cursor);
    a->c = c;
    a->info = *info;
    Commit();
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL EndCommandBuffer(VkCommandBuffer c)
{
    if (!Deferred(c))
        return r_endCommandBuffer(c);
    uint8_t *cursor;
    ValueArgs *a = Emit<ValueArgs>(
        [](const uint8_t *p) { r_endCommandBuffer(reinterpret_cast<const ValueArgs *>(p)->c); }, 0, cursor);
    a->c = c;
    Commit();
    DrainCalls();
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL ResetCommandPool(VkDevice d, VkCommandPool pool, VkCommandPoolResetFlags flags)
{
    if (pool == s_pool)
        DrainCalls();
    return r_resetCommandPool(d, pool, flags);
}

VKAPI_ATTR void VKAPI_CALL FreeCommandBuffers(VkDevice d, VkCommandPool pool, uint32_t count,
                                              const VkCommandBuffer *buffers)
{
    DrainCalls();
    for (uint32_t i = 0; i < count; ++i)
        if (buffers[i] == s_cmd)
            s_cmd = VK_NULL_HANDLE;
    r_freeCommandBuffers(d, pool, count, buffers);
}

VKAPI_ATTR void VKAPI_CALL DestroyCommandPool(VkDevice d, VkCommandPool pool, const VkAllocationCallbacks *allocator)
{
    DrainCalls();
    if (pool == s_pool)
        s_cmd = VK_NULL_HANDLE, s_pool = VK_NULL_HANDLE;
    r_destroyCommandPool(d, pool, allocator);
}

VKAPI_ATTR VkResult VKAPI_CALL DeviceWaitIdle(VkDevice d)
{
    DrainCalls();
    return r_deviceWaitIdle(d);
}

void DrainCalls()
{
    Publish();
    for (uint32_t spin = 0; static_cast<uint32_t>(s_done) != s_write; ++spin)
    {
        if (spin < 4096)
            _mm_pause();
        else
            SwitchToThread();
    }
}

template <typename T> void Wrap(T &pointer, T &real, T wrapper)
{
    if (!pointer || pointer == wrapper)
        return;
    real = pointer;
    pointer = wrapper;
}
} // namespace

void Install()
{
    static const bool wanted = nfs::config::Int("Display", "RenderThread", 1) != 0 && !HasArgument("-vkcallprofile");
    if (!wanted)
        return;
    if (!s_installed)
    {
        s_ring = static_cast<uint8_t *>(VirtualAlloc(nullptr, kRingBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        s_wake = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        if (!s_ring || !s_wake || !(s_thread = CreateThread(nullptr, 0, Run, nullptr, 0, nullptr)))
        {
            nfs::Log("Vulkan: no render thread (could not start it)");
            return;
        }
        SetThreadPriority(s_thread, THREAD_PRIORITY_ABOVE_NORMAL);
        s_installed = true;
        s_sync = nfs::config::Int("Debug", "RenderThreadSync", 0) != 0;
        nfs::Log("Vulkan: the frame's commands are recorded on a render thread");
    }
    else
    {
        DrainCalls();
        s_cmd = VK_NULL_HANDLE;
        s_pool = VK_NULL_HANDLE;
    }
    Wrap(vkCmdBindPipeline, r_bindPipeline, &BindPipeline);
    Wrap(vkCmdPushConstants, r_pushConstants, &PushConstants);
    Wrap(vkCmdBindDescriptorSets, r_bindDescriptorSets, &BindDescriptorSets);
    Wrap(vkCmdPushDescriptorSetKHR, r_pushDescriptorSet, &PushDescriptorSet);
    Wrap(vkCmdBindVertexBuffers2, r_bindVertexBuffers2, &BindVertexBuffers2);
    Wrap(vkCmdBindVertexBuffers, r_bindVertexBuffers, &BindVertexBuffers);
    Wrap(vkCmdBindIndexBuffer, r_bindIndexBuffer, &BindIndexBuffer);
    Wrap(vkCmdDraw, r_draw, &Draw);
    Wrap(vkCmdDrawIndexed, r_drawIndexed, &DrawIndexed);
    Wrap(vkCmdSetViewport, r_setViewport, &SetViewport);
    Wrap(vkCmdSetScissor, r_setScissor, &SetScissor);
    Wrap(vkCmdSetCullMode, r_setCullMode, &SetCullMode);
    Wrap(vkCmdSetFrontFace, r_setFrontFace, &SetFrontFace);
    Wrap(vkCmdSetPrimitiveTopology, r_setPrimitiveTopology, &SetPrimitiveTopology);
    Wrap(vkCmdSetDepthTestEnable, r_setDepthTestEnable, &SetDepthTestEnable);
    Wrap(vkCmdSetDepthWriteEnable, r_setDepthWriteEnable, &SetDepthWriteEnable);
    Wrap(vkCmdSetDepthCompareOp, r_setDepthCompareOp, &SetDepthCompareOp);
    Wrap(vkCmdSetDepthBoundsTestEnable, r_setDepthBoundsTestEnable, &SetDepthBoundsTestEnable);
    Wrap(vkCmdSetStencilTestEnable, r_setStencilTestEnable, &SetStencilTestEnable);
    Wrap(vkCmdSetStencilOp, r_setStencilOp, &SetStencilOp);
    Wrap(vkCmdSetStencilReference, r_setStencilReference, &SetStencilReference);
    Wrap(vkCmdSetStencilCompareMask, r_setStencilCompareMask, &SetStencilCompareMask);
    Wrap(vkCmdSetStencilWriteMask, r_setStencilWriteMask, &SetStencilWriteMask);
    Wrap(vkCmdSetDepthBias, r_setDepthBias, &SetDepthBias);
    Wrap(vkCmdSetDepthBiasEnable, r_setDepthBiasEnable, &SetDepthBiasEnable);
    Wrap(vkCmdSetBlendConstants, r_setBlendConstants, &SetBlendConstants);
    Wrap(vkCmdSetRasterizerDiscardEnable, r_setRasterizerDiscardEnable, &SetRasterizerDiscardEnable);
    Wrap(vkCmdSetPrimitiveRestartEnable, r_setPrimitiveRestartEnable, &SetPrimitiveRestartEnable);
    Wrap(vkCmdBeginRendering, r_beginRendering, &BeginRendering);
    Wrap(vkCmdEndRendering, r_endRendering, &EndRendering);
    Wrap(vkCmdPipelineBarrier2, r_pipelineBarrier2, &PipelineBarrier2);
    Wrap(vkCmdPipelineBarrier, r_pipelineBarrier, &PipelineBarrier);
    Wrap(vkCmdCopyBuffer, r_copyBuffer, &CopyBuffer);
    Wrap(vkCmdCopyImage, r_copyImage, &CopyImage);
    Wrap(vkCmdCopyBufferToImage, r_copyBufferToImage, &CopyBufferToImage);
    Wrap(vkCmdCopyImageToBuffer, r_copyImageToBuffer, &CopyImageToBuffer);
    Wrap(vkCmdBlitImage, r_blitImage, &BlitImage);
    Wrap(vkCmdClearColorImage, r_clearColorImage, &ClearColorImage);
    Wrap(vkCmdClearDepthStencilImage, r_clearDepthStencilImage, &ClearDepthStencilImage);
    Wrap(vkCmdClearAttachments, r_clearAttachments, &ClearAttachments);
    Wrap(vkCmdResetQueryPool, r_resetQueryPool, &ResetQueryPool);
    Wrap(vkCmdWriteTimestamp2, r_writeTimestamp2, &WriteTimestamp2);
    Wrap(vkCmdWriteTimestamp, r_writeTimestamp, &WriteTimestamp);
    Wrap(vkCmdWriteBufferMarkerAMD, r_writeBufferMarker, &WriteBufferMarker);
    Wrap(vkCmdBeginRenderPass, r_beginRenderPass, &BeginRenderPass);
    Wrap(vkCmdEndRenderPass, r_endRenderPass, &EndRenderPass);
    Wrap(vkBeginCommandBuffer, r_beginCommandBuffer, &BeginCommandBuffer);
    Wrap(vkEndCommandBuffer, r_endCommandBuffer, &EndCommandBuffer);
    Wrap(vkResetCommandPool, r_resetCommandPool, &ResetCommandPool);
    Wrap(vkFreeCommandBuffers, r_freeCommandBuffers, &FreeCommandBuffers);
    Wrap(vkDestroyCommandPool, r_destroyCommandPool, &DestroyCommandPool);
    Wrap(vkDeviceWaitIdle, r_deviceWaitIdle, &DeviceWaitIdle);
}

void Track(VkCommandBuffer cmd, VkCommandPool pool)
{
    if (!s_installed)
        return;
    if (cmd != s_cmd)
        DrainCalls();
    s_cmd = cmd;
    s_pool = pool;
}

void Drain()
{
    if (s_installed)
        DrainCalls();
}
} // namespace nfs::vk::stream
