#pragma once

/**
 * Vulkan for the rebuilt renderer (docs/VULKAN.md). The functions are loaded from vulkan-1.dll at run time, so the game
 * still starts on machines without Vulkan; include this header instead of <vulkan/vulkan.h> so every Vulkan call goes
 * through the pointers declared here.
 */
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <windows.h>
#include <vulkan/vulkan.h>

#define NFS_VK_GLOBAL_FUNCTIONS(X)                                                                                     \
    X(vkCreateInstance)                                                                                                \
    X(vkEnumerateInstanceExtensionProperties)                                                                          \
    X(vkEnumerateInstanceLayerProperties)                                                                              \
    X(vkEnumerateInstanceVersion)

#define NFS_VK_INSTANCE_FUNCTIONS(X)                                                                                   \
    X(vkDestroyInstance)                                                                                               \
    X(vkEnumeratePhysicalDevices)                                                                                      \
    X(vkGetPhysicalDeviceProperties)                                                                                   \
    X(vkGetPhysicalDeviceFeatures)                                                                                     \
    X(vkGetPhysicalDeviceFeatures2)                                                                                    \
    X(vkGetPhysicalDeviceMemoryProperties)                                                                             \
    X(vkGetPhysicalDeviceQueueFamilyProperties)                                                                        \
    X(vkGetPhysicalDeviceFormatProperties)                                                                             \
    X(vkEnumerateDeviceExtensionProperties)                                                                            \
    X(vkCreateDebugUtilsMessengerEXT)                                                                                  \
    X(vkCreateDevice)                                                                                                  \
    X(vkGetDeviceProcAddr)                                                                                             \
    X(vkDestroySurfaceKHR)                                                                                             \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)                                                                            \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)                                                                       \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)                                                                            \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR)

#define NFS_VK_DEVICE_FUNCTIONS(X)                                                                                     \
    X(vkDestroyDevice)                                                                                                 \
    X(vkGetDeviceQueue)                                                                                                \
    X(vkDeviceWaitIdle)                                                                                                \
    X(vkQueueSubmit)                                                                                                   \
    X(vkCmdPipelineBarrier)                                                                                            \
    X(vkCmdWriteTimestamp)                                                                                             \
    X(vkCmdBindVertexBuffers)                                                                                          \
    X(vkCreateRenderPass)                                                                                              \
    X(vkDestroyRenderPass)                                                                                             \
    X(vkCreateFramebuffer)                                                                                             \
    X(vkDestroyFramebuffer)                                                                                            \
    X(vkCmdBeginRenderPass)                                                                                            \
    X(vkCmdEndRenderPass)                                                                                              \
    X(vkResetDescriptorPool)                                                                                           \
    X(vkQueueSubmit2)                                                                                                  \
    X(vkQueueWaitIdle)                                                                                                 \
    X(vkAllocateMemory)                                                                                                \
    X(vkFreeMemory)                                                                                                    \
    X(vkMapMemory)                                                                                                     \
    X(vkUnmapMemory)                                                                                                   \
    X(vkCreateBuffer)                                                                                                  \
    X(vkDestroyBuffer)                                                                                                 \
    X(vkGetBufferMemoryRequirements)                                                                                   \
    X(vkBindBufferMemory)                                                                                              \
    X(vkCreateImage)                                                                                                   \
    X(vkDestroyImage)                                                                                                  \
    X(vkGetImageMemoryRequirements)                                                                                    \
    X(vkBindImageMemory)                                                                                               \
    X(vkCreateImageView)                                                                                               \
    X(vkDestroyImageView)                                                                                              \
    X(vkCreateSampler)                                                                                                 \
    X(vkDestroySampler)                                                                                                \
    X(vkCreateCommandPool)                                                                                             \
    X(vkDestroyCommandPool)                                                                                            \
    X(vkResetCommandPool)                                                                                              \
    X(vkAllocateCommandBuffers)                                                                                        \
    X(vkFreeCommandBuffers)                                                                                            \
    X(vkBeginCommandBuffer)                                                                                            \
    X(vkEndCommandBuffer)                                                                                              \
    X(vkCreateFence)                                                                                                   \
    X(vkDestroyFence)                                                                                                  \
    X(vkWaitForFences)                                                                                                 \
    X(vkResetFences)                                                                                                   \
    X(vkCreateSemaphore)                                                                                               \
    X(vkDestroySemaphore)                                                                                              \
    X(vkCmdPipelineBarrier2)                                                                                           \
    X(vkCmdClearColorImage)                                                                                            \
    X(vkCmdCopyImageToBuffer)                                                                                          \
    X(vkCmdCopyBufferToImage)                                                                                          \
    X(vkCmdBlitImage)                                                                                                  \
    X(vkCmdBeginRendering)                                                                                             \
    X(vkCmdEndRendering)                                                                                               \
    X(vkFlushMappedMemoryRanges)                                                                                       \
    X(vkInvalidateMappedMemoryRanges)                                                                                  \
    X(vkCreateShaderModule)                                                                                            \
    X(vkDestroyShaderModule)                                                                                           \
    X(vkCreateDescriptorSetLayout)                                                                                     \
    X(vkDestroyDescriptorSetLayout)                                                                                    \
    X(vkCreatePipelineLayout)                                                                                          \
    X(vkDestroyPipelineLayout)                                                                                         \
    X(vkCreateGraphicsPipelines)                                                                                       \
    X(vkDestroyPipeline)                                                                                               \
    X(vkCreatePipelineCache)                                                                                           \
    X(vkDestroyPipelineCache)                                                                                          \
    X(vkGetPipelineCacheData)                                                                                          \
    X(vkCmdBindPipeline)                                                                                               \
    X(vkCmdBindVertexBuffers2)                                                                                         \
    X(vkCmdBindIndexBuffer)                                                                                            \
    X(vkCmdDraw)                                                                                                       \
    X(vkCmdDrawIndexed)                                                                                                \
    X(vkCmdSetViewport)                                                                                                \
    X(vkCmdSetScissor)                                                                                                 \
    X(vkCmdPushConstants)                                                                                              \
    X(vkCmdPushDescriptorSetKHR)                                                                                           X(vkCmdWriteBufferMarkerAMD)                                                                                           X(vkGetDeviceFaultInfoEXT)                                                                                             X(vkGetDeviceMemoryOpaqueCaptureAddress)                                                                           \
    X(vkCmdSetCullMode)                                                                                                \
    X(vkCmdSetFrontFace)                                                                                               \
    X(vkCmdSetPrimitiveTopology)                                                                                       \
    X(vkCmdSetDepthTestEnable)                                                                                         \
    X(vkCmdSetDepthWriteEnable)                                                                                        \
    X(vkCmdSetDepthCompareOp)                                                                                          \
    X(vkCmdSetDepthBoundsTestEnable)                                                                                   \
    X(vkCmdSetStencilTestEnable)                                                                                       \
    X(vkCmdSetStencilOp)                                                                                               \
    X(vkCmdSetStencilReference)                                                                                        \
    X(vkCmdSetStencilCompareMask)                                                                                      \
    X(vkCmdSetStencilWriteMask)                                                                                        \
    X(vkCmdSetDepthBias)                                                                                               \
    X(vkCmdSetDepthBiasEnable)                                                                                         \
    X(vkCmdSetBlendConstants)                                                                                          \
    X(vkCmdSetRasterizerDiscardEnable)                                                                                 \
    X(vkCmdSetPrimitiveRestartEnable)                                                                                  \
    X(vkCmdCopyBuffer)                                                                                                 \
    X(vkCmdCopyImage)                                                                                                  \
    X(vkCmdClearAttachments)                                                                                           \
    X(vkCmdClearDepthStencilImage)                                                                                     \
    X(vkCreateQueryPool)                                                                                               \
    X(vkDestroyQueryPool)                                                                                              \
    X(vkGetQueryPoolResults)                                                                                           \
    X(vkCmdResetQueryPool)                                                                                             \
    X(vkCmdWriteTimestamp2)                                                                                            \
    X(vkCreateDescriptorPool)                                                                                          \
    X(vkDestroyDescriptorPool)                                                                                         \
    X(vkAllocateDescriptorSets)                                                                                        \
    X(vkUpdateDescriptorSets)                                                                                          \
    X(vkCmdBindDescriptorSets)                                                                                         \
    X(vkCreateSwapchainKHR)                                                                                            \
    X(vkDestroySwapchainKHR)                                                                                           \
    X(vkGetSwapchainImagesKHR)                                                                                         \
    X(vkAcquireNextImageKHR)                                                                                           \
    X(vkQueuePresentKHR)

#define NFS_VK_DECLARE_FUNCTION(name) extern PFN_##name name;
NFS_VK_GLOBAL_FUNCTIONS(NFS_VK_DECLARE_FUNCTION)
NFS_VK_INSTANCE_FUNCTIONS(NFS_VK_DECLARE_FUNCTION)
NFS_VK_DEVICE_FUNCTIONS(NFS_VK_DECLARE_FUNCTION)
#undef NFS_VK_DECLARE_FUNCTION

namespace nfs::vk
{
/** Loads vulkan-1.dll and the functions that need no instance. False if the system has no Vulkan. */
bool LoadLoader();
/** Loads the instance-level functions, after vkCreateInstance. */
void LoadInstanceFunctions(VkInstance instance);
/** Loads the device-level functions, after vkCreateDevice. */
void LoadDeviceFunctions(VkDevice device);
/** -vkcallprofile (source/engine/rendering/vulkan/CallProfile.cpp): times the draw path's commands in the driver; Log reports them a frame. */
namespace callprofile
{
void Install();
void Log(uint32_t frames);
} // namespace callprofile
/** The render thread (source/engine/rendering/vulkan/CommandStream.cpp): the frame's command buffer is recorded on a thread of its own. */
namespace stream
{
/** Swaps the device's command functions for ones that send the tracked command buffer's commands to the thread
 * (after LoadDeviceFunctions; nothing when [Display] RenderThread is 0 or -vkcallprofile is on). */
void Install();
/** The command buffer (and its pool) whose commands go to the thread from now on. */
void Track(VkCommandBuffer cmd, VkCommandPool pool);
/** Waits until the thread has made every call sent. */
void Drain();
} // namespace stream
} // namespace nfs::vk
