/**
 * -vkcallprofile: the time the draw path's command recording spends in the driver, by kind of command. The device's
 * command function pointers (vk.h) are wrapped by ones that read the time stamp counter around the driver's; the
 * renderer's stats (-vkstats) log the totals every few seconds.
 */
#include <intrin.h>

#include "core/log.h"
#include "engine/rendering/vulkan/renderer.h"
#include "engine/rendering/vulkan/vk.h"

namespace nfs::vk::callprofile
{
namespace
{
enum Kind
{
    kPipeline,
    kPushConstants,
    kDescriptorSets,
    kPushDescriptors,
    kVertexBuffers,
    kIndexBuffer,
    kDynamicState,
    kDraw,
    kRendering,
    kBarrier,
    kSubmit,
    kResetPool,
    kBeginEnd,
    kWaitFence,
    kCopy,
    kKinds
};
const char *const kNames[kKinds] = {"pipeline", "push constants", "descriptor sets", "push descriptors",
                                    "vertex buffers", "index buffer", "dynamic state", "draw", "rendering",
                                    "barrier", "submit", "reset pool", "begin/end buffer", "fence wait", "copy"};
uint64_t s_ticks[kKinds];
uint32_t s_counts[kKinds];
uint64_t s_lastTsc;
LARGE_INTEGER s_lastQpc;
bool s_installed;
/** The kinds of state recorded since the last draw (a bit per kind up to kDynamicState), and the draws' time by what
 * was recorded before them: the driver works out most state at the draw. */
uint32_t s_since;
const uint32_t kStateKinds = kDynamicState + 1;
uint64_t s_drawTicks[1u << kStateKinds];
uint32_t s_drawCounts[1u << kStateKinds];

struct Timer
{
    Kind kind;
    uint64_t start;
    explicit Timer(Kind kind) : kind(kind), start(__rdtsc()) {}
    ~Timer()
    {
        const uint64_t ticks = __rdtsc() - start;
        s_ticks[kind] += ticks;
        ++s_counts[kind];
        if (kind < kStateKinds)
        {
            s_since |= 1u << kind;
        }
        else if (kind == kDraw)
        {
            s_drawTicks[s_since] += ticks;
            ++s_drawCounts[s_since];
            s_since = 0;
        }
    }
};

PFN_vkCmdBindPipeline s_bindPipeline;
PFN_vkCmdPushConstants s_pushConstants;
PFN_vkCmdBindDescriptorSets s_bindDescriptorSets;
PFN_vkCmdPushDescriptorSetKHR s_pushDescriptorSet;
PFN_vkCmdBindVertexBuffers2 s_bindVertexBuffers2;
PFN_vkCmdBindIndexBuffer s_bindIndexBuffer;
PFN_vkCmdDrawIndexed s_drawIndexed;
PFN_vkCmdDraw s_draw;
PFN_vkCmdSetCullMode s_setCullMode;
PFN_vkCmdSetDepthTestEnable s_setDepthTestEnable;
PFN_vkCmdSetDepthWriteEnable s_setDepthWriteEnable;
PFN_vkCmdSetDepthCompareOp s_setDepthCompareOp;
PFN_vkCmdSetPrimitiveTopology s_setPrimitiveTopology;
PFN_vkCmdSetViewport s_setViewport;
PFN_vkCmdSetScissor s_setScissor;
PFN_vkCmdSetDepthBias s_setDepthBias;
PFN_vkCmdBeginRendering s_beginRendering;
PFN_vkCmdEndRendering s_endRendering;
PFN_vkCmdPipelineBarrier2 s_pipelineBarrier2;
PFN_vkQueueSubmit2 s_queueSubmit2;
PFN_vkResetCommandPool s_resetCommandPool;
PFN_vkBeginCommandBuffer s_beginCommandBuffer;
PFN_vkEndCommandBuffer s_endCommandBuffer;
PFN_vkWaitForFences s_waitForFences;
PFN_vkCmdCopyBuffer s_copyBuffer;
PFN_vkCmdCopyImage s_copyImage;
PFN_vkCmdCopyBufferToImage s_copyBufferToImage;

VKAPI_ATTR VkResult VKAPI_CALL QueueSubmit2(VkQueue q, uint32_t n, const VkSubmitInfo2 *s, VkFence f)
{
    Timer t(kSubmit);
    return s_queueSubmit2(q, n, s, f);
}
VKAPI_ATTR VkResult VKAPI_CALL ResetCommandPool(VkDevice d, VkCommandPool p, VkCommandPoolResetFlags flags)
{
    Timer t(kResetPool);
    return s_resetCommandPool(d, p, flags);
}
VKAPI_ATTR VkResult VKAPI_CALL BeginCommandBuffer(VkCommandBuffer c, const VkCommandBufferBeginInfo *info)
{
    Timer t(kBeginEnd);
    return s_beginCommandBuffer(c, info);
}
VKAPI_ATTR VkResult VKAPI_CALL EndCommandBuffer(VkCommandBuffer c)
{
    Timer t(kBeginEnd);
    return s_endCommandBuffer(c);
}
VKAPI_ATTR VkResult VKAPI_CALL WaitForFences(VkDevice d, uint32_t n, const VkFence *f, VkBool32 all, uint64_t timeout)
{
    Timer t(kWaitFence);
    return s_waitForFences(d, n, f, all, timeout);
}
VKAPI_ATTR void VKAPI_CALL CopyBuffer(VkCommandBuffer c, VkBuffer s, VkBuffer d, uint32_t n, const VkBufferCopy *r)
{
    Timer t(kCopy);
    s_copyBuffer(c, s, d, n, r);
}
VKAPI_ATTR void VKAPI_CALL CopyImage(VkCommandBuffer c, VkImage s, VkImageLayout sl, VkImage d, VkImageLayout dl,
                                     uint32_t n, const VkImageCopy *r)
{
    Timer t(kCopy);
    s_copyImage(c, s, sl, d, dl, n, r);
}
VKAPI_ATTR void VKAPI_CALL CopyBufferToImage(VkCommandBuffer c, VkBuffer s, VkImage d, VkImageLayout l, uint32_t n,
                                             const VkBufferImageCopy *r)
{
    Timer t(kCopy);
    s_copyBufferToImage(c, s, d, l, n, r);
}

VKAPI_ATTR void VKAPI_CALL BindPipeline(VkCommandBuffer c, VkPipelineBindPoint p, VkPipeline pipeline)
{
    Timer t(kPipeline);
    s_bindPipeline(c, p, pipeline);
}
VKAPI_ATTR void VKAPI_CALL PushConstants(VkCommandBuffer c, VkPipelineLayout l, VkShaderStageFlags s, uint32_t o,
                                         uint32_t n, const void *v)
{
    Timer t(kPushConstants);
    s_pushConstants(c, l, s, o, n, v);
}
VKAPI_ATTR void VKAPI_CALL BindDescriptorSets(VkCommandBuffer c, VkPipelineBindPoint p, VkPipelineLayout l,
                                              uint32_t first, uint32_t count, const VkDescriptorSet *sets,
                                              uint32_t dynamicCount, const uint32_t *dynamic)
{
    Timer t(kDescriptorSets);
    s_bindDescriptorSets(c, p, l, first, count, sets, dynamicCount, dynamic);
}
VKAPI_ATTR void VKAPI_CALL PushDescriptorSet(VkCommandBuffer c, VkPipelineBindPoint p, VkPipelineLayout l,
                                             uint32_t set, uint32_t count, const VkWriteDescriptorSet *writes)
{
    Timer t(kPushDescriptors);
    s_pushDescriptorSet(c, p, l, set, count, writes);
}
VKAPI_ATTR void VKAPI_CALL BindVertexBuffers2(VkCommandBuffer c, uint32_t first, uint32_t count, const VkBuffer *b,
                                              const VkDeviceSize *o, const VkDeviceSize *s, const VkDeviceSize *st)
{
    Timer t(kVertexBuffers);
    s_bindVertexBuffers2(c, first, count, b, o, s, st);
}
VKAPI_ATTR void VKAPI_CALL BindIndexBuffer(VkCommandBuffer c, VkBuffer b, VkDeviceSize o, VkIndexType type)
{
    Timer t(kIndexBuffer);
    s_bindIndexBuffer(c, b, o, type);
}
VKAPI_ATTR void VKAPI_CALL DrawIndexed(VkCommandBuffer c, uint32_t n, uint32_t instances, uint32_t first, int32_t base,
                                       uint32_t firstInstance)
{
    Timer t(kDraw);
    s_drawIndexed(c, n, instances, first, base, firstInstance);
}
VKAPI_ATTR void VKAPI_CALL Draw(VkCommandBuffer c, uint32_t n, uint32_t instances, uint32_t first,
                                uint32_t firstInstance)
{
    Timer t(kDraw);
    s_draw(c, n, instances, first, firstInstance);
}
VKAPI_ATTR void VKAPI_CALL SetCullMode(VkCommandBuffer c, VkCullModeFlags m)
{
    Timer t(kDynamicState);
    s_setCullMode(c, m);
}
VKAPI_ATTR void VKAPI_CALL SetDepthTestEnable(VkCommandBuffer c, VkBool32 v)
{
    Timer t(kDynamicState);
    s_setDepthTestEnable(c, v);
}
VKAPI_ATTR void VKAPI_CALL SetDepthWriteEnable(VkCommandBuffer c, VkBool32 v)
{
    Timer t(kDynamicState);
    s_setDepthWriteEnable(c, v);
}
VKAPI_ATTR void VKAPI_CALL SetDepthCompareOp(VkCommandBuffer c, VkCompareOp v)
{
    Timer t(kDynamicState);
    s_setDepthCompareOp(c, v);
}
VKAPI_ATTR void VKAPI_CALL SetPrimitiveTopology(VkCommandBuffer c, VkPrimitiveTopology v)
{
    Timer t(kDynamicState);
    s_setPrimitiveTopology(c, v);
}
VKAPI_ATTR void VKAPI_CALL SetViewport(VkCommandBuffer c, uint32_t first, uint32_t count, const VkViewport *v)
{
    Timer t(kDynamicState);
    s_setViewport(c, first, count, v);
}
VKAPI_ATTR void VKAPI_CALL SetScissor(VkCommandBuffer c, uint32_t first, uint32_t count, const VkRect2D *v)
{
    Timer t(kDynamicState);
    s_setScissor(c, first, count, v);
}
VKAPI_ATTR void VKAPI_CALL SetDepthBias(VkCommandBuffer c, float a, float b, float d)
{
    Timer t(kDynamicState);
    s_setDepthBias(c, a, b, d);
}
VKAPI_ATTR void VKAPI_CALL BeginRendering(VkCommandBuffer c, const VkRenderingInfo *info)
{
    Timer t(kRendering);
    s_beginRendering(c, info);
}
VKAPI_ATTR void VKAPI_CALL EndRendering(VkCommandBuffer c)
{
    Timer t(kRendering);
    s_endRendering(c);
}
VKAPI_ATTR void VKAPI_CALL PipelineBarrier2(VkCommandBuffer c, const VkDependencyInfo *info)
{
    Timer t(kBarrier);
    s_pipelineBarrier2(c, info);
}

template <typename T> void Wrap(T &pointer, T &saved, T wrapper)
{
    if (!pointer)
        return;
    saved = pointer;
    pointer = wrapper;
}
} // namespace

void Install()
{
    if (s_installed || !HasArgument("-vkcallprofile"))
        return;
    s_installed = true;
    Wrap(vkCmdBindPipeline, s_bindPipeline, &BindPipeline);
    Wrap(vkCmdPushConstants, s_pushConstants, &PushConstants);
    Wrap(vkCmdBindDescriptorSets, s_bindDescriptorSets, &BindDescriptorSets);
    Wrap(vkCmdPushDescriptorSetKHR, s_pushDescriptorSet, &PushDescriptorSet);
    Wrap(vkCmdBindVertexBuffers2, s_bindVertexBuffers2, &BindVertexBuffers2);
    Wrap(vkCmdBindIndexBuffer, s_bindIndexBuffer, &BindIndexBuffer);
    Wrap(vkCmdDrawIndexed, s_drawIndexed, &DrawIndexed);
    Wrap(vkCmdDraw, s_draw, &Draw);
    Wrap(vkCmdSetCullMode, s_setCullMode, &SetCullMode);
    Wrap(vkCmdSetDepthTestEnable, s_setDepthTestEnable, &SetDepthTestEnable);
    Wrap(vkCmdSetDepthWriteEnable, s_setDepthWriteEnable, &SetDepthWriteEnable);
    Wrap(vkCmdSetDepthCompareOp, s_setDepthCompareOp, &SetDepthCompareOp);
    Wrap(vkCmdSetPrimitiveTopology, s_setPrimitiveTopology, &SetPrimitiveTopology);
    Wrap(vkCmdSetViewport, s_setViewport, &SetViewport);
    Wrap(vkCmdSetScissor, s_setScissor, &SetScissor);
    Wrap(vkCmdSetDepthBias, s_setDepthBias, &SetDepthBias);
    Wrap(vkCmdBeginRendering, s_beginRendering, &BeginRendering);
    Wrap(vkCmdEndRendering, s_endRendering, &EndRendering);
    Wrap(vkCmdPipelineBarrier2, s_pipelineBarrier2, &PipelineBarrier2);
    Wrap(vkQueueSubmit2, s_queueSubmit2, &QueueSubmit2);
    Wrap(vkResetCommandPool, s_resetCommandPool, &ResetCommandPool);
    Wrap(vkBeginCommandBuffer, s_beginCommandBuffer, &BeginCommandBuffer);
    Wrap(vkEndCommandBuffer, s_endCommandBuffer, &EndCommandBuffer);
    Wrap(vkWaitForFences, s_waitForFences, &WaitForFences);
    Wrap(vkCmdCopyBuffer, s_copyBuffer, &CopyBuffer);
    Wrap(vkCmdCopyImage, s_copyImage, &CopyImage);
    Wrap(vkCmdCopyBufferToImage, s_copyBufferToImage, &CopyBufferToImage);
    s_lastTsc = __rdtsc();
    QueryPerformanceCounter(&s_lastQpc);
    nfs::Log("Vulkan: timing the draw path's commands (-vkcallprofile)");
}

void Log(uint32_t frames)
{
    if (!s_installed || !frames)
        return;
    LARGE_INTEGER qpc, frequency;
    QueryPerformanceCounter(&qpc);
    QueryPerformanceFrequency(&frequency);
    const uint64_t tsc = __rdtsc();
    const double seconds = static_cast<double>(qpc.QuadPart - s_lastQpc.QuadPart) / static_cast<double>(frequency.QuadPart);
    const double tscPerNs = seconds > 0.0 ? static_cast<double>(static_cast<int64_t>(tsc - s_lastTsc)) / (seconds * 1e9) : 1.0;
    s_lastTsc = tsc;
    s_lastQpc = qpc;
    double total = 0.0;
    char line[1024];
    int n = wsprintfA(line, "Vulkan commands a frame:");
    for (int k = 0; k < kKinds; ++k)
    {
        const double us = static_cast<double>(static_cast<int64_t>(s_ticks[k])) / tscPerNs / 1000.0 / frames;
        const double each = s_counts[k] ? static_cast<double>(static_cast<int64_t>(s_ticks[k])) / tscPerNs / s_counts[k] : 0.0;
        total += us;
        n += wsprintfA(line + n, " %s %d x %d ns = %d us;", kNames[k], static_cast<int>(s_counts[k] / frames),
                       static_cast<int>(each), static_cast<int>(us));
        s_ticks[k] = 0;
        s_counts[k] = 0;
    }
    wsprintfA(line + n, " total %d us", static_cast<int>(total));
    nfs::Log("%s", line);
    n = wsprintfA(line, "Vulkan draws a frame by the state before them (p pipeline, c push constants, d descriptor sets, "
                        "s push descriptors, v vertex buffers, i index buffer, y dynamic state):");
    for (int shown = 0; shown < 16; ++shown)
    {
        uint32_t best = 0;
        for (uint32_t m = 1; m < (1u << kStateKinds); ++m)
            if (s_drawTicks[m] > s_drawTicks[best])
                best = m;
        if (!s_drawCounts[best])
            break;
        char mask[8];
        int k = 0;
        for (uint32_t b = 0; b < kStateKinds; ++b)
            if (best & (1u << b))
                mask[k++] = "pcdsviy"[b];
        if (!k)
            mask[k++] = '-';
        mask[k] = 0;
        const double each = static_cast<double>(static_cast<int64_t>(s_drawTicks[best])) / tscPerNs / s_drawCounts[best];
        n += wsprintfA(line + n, " %s %d x %d ns;", mask, static_cast<int>(s_drawCounts[best] / frames),
                       static_cast<int>(each));
        s_drawTicks[best] = 0;
        s_drawCounts[best] = 0;
    }
    for (uint32_t m = 0; m < (1u << kStateKinds); ++m)
        s_drawTicks[m] = s_drawCounts[m] = 0;
    nfs::Log("%s", line);
}
} // namespace nfs::vk::callprofile
