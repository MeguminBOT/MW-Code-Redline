#pragma once

#include "engine/rendering/vulkan/vk.h"

namespace nfs::vk
{
struct MemoryAllocator;

/** What the renderer needs beyond Vulkan 1.3 core, found on the chosen GPU. */
struct DeviceCaps
{
    /** The card or its driver is older than Vulkan 1.3 (or -vk10 asks for it): source/engine/rendering/vulkan/Compat.cpp stands in for what
     * the renderer uses of 1.1 to 1.3. */
    bool compat;
    bool swapchain;
    bool pushDescriptor;
    bool anisotropy;
    bool clipDistance;
    bool timelineSemaphore;
    /** VK_EXT_graphics_pipeline_library: shaders compile once as pipeline libraries and draws link them. */
    bool pipelineLibrary;
    /** Acceleration structures, ray queries in shaders and buffer device addresses (docs/SHADOWS.md); off with
     * [Display] RayTracing=off or -noraytracing. */
    bool rayQuery;
    /** -gpucrumbs and VK_AMD_buffer_marker: the GPU writes the serial of each command it starts and finishes
     * (Renderer::Crumb), so after a hang the log names the command it stopped at. */
    bool bufferMarker;
    /** VK_AMD_device_coherent_memory, for the markers. */
    bool uncachedMemory;
    /** VK_EXT_device_fault (with -gpucrumbs): what the driver knows about a lost device. */
    bool deviceFault;
    /** -gpucrumbs: memory allocations log their GPU addresses (to place a fault address). */
    bool addressLog;
    /** -gpucrumbs and VK_EXT_device_address_binding_report: the driver reports every address range it binds. */
    bool bindingReport;
    float maxAnisotropy;
};

struct Device
{
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    uint32_t queueFamily;
    uint32_t apiVersion;
    /** The version the instance was made for: 1.0 on a loader that knows no other, and with -vk10. */
    uint32_t instanceVersion;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceMemoryProperties memory;
    DeviceCaps caps;
    /** Sub-allocates device-local images and buffers from large blocks (resources.h). */
    MemoryAllocator *allocator;
};

struct DeviceOptions
{
    /** Enables VK_LAYER_KHRONOS_validation when the layer is installed (Vulkan SDK). */
    bool validation;
    /** Needs a queue that can present to windows and VK_KHR_swapchain; off for headless use. */
    bool present;
};

/** Loads Vulkan and creates the instance and device on the best GPU (discrete first). Logs the reason on failure. */
bool CreateDevice(Device &out, const DeviceOptions &options);
void DestroyDevice(Device &device);

/** -gpucrumbs: logs the address ranges the driver reported bound around `address` (VK_EXT_device_address_binding_report),
 * newest first. */
void ReportAddressBindings(uint64_t address);

/** A memory type allowed by `typeBits` with all of `required` (and `preferred` when one has it), or ~0u. */
uint32_t FindMemoryType(const Device &device, uint32_t typeBits, VkMemoryPropertyFlags required,
                        VkMemoryPropertyFlags preferred = 0);
const char *ResultName(VkResult result);

/** Vulkan 1.0 cards (source/engine/rendering/vulkan/Compat.cpp). */
namespace compat
{
/** After the device's functions are loaded: points those Vulkan 1.0 lacks at their stand-ins; `emulatePush` also
 * VK_KHR_push_descriptor's. */
void Install(const Device &device, bool emulatePush);
/** Once a frame, before its draws: the descriptor sets of frames the GPU has finished are reused. */
void BeginFrame();
} // namespace compat
} // namespace nfs::vk
