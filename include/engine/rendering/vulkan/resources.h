#pragma once

#include "engine/rendering/vulkan/device.h"

namespace nfs::vk
{
/**
 * Memory of an image or buffer. Device-local resources share 64 MB blocks (the game has thousands of textures and
 * vertex buffers, more than drivers allow allocations); large and host-visible ones get their own allocation.
 */
struct Allocation
{
    VkDeviceMemory memory;
    VkDeviceSize offset;
    VkDeviceSize size;
    /** The block it is part of, plus one; 0 for an allocation of its own. */
    uint32_t block;
    /** Its GPU address when the device logs them (DeviceCaps::addressLog), else 0. */
    uint64_t address;
};

bool CreateAllocator(Device &device);
/** Frees every block; the resources in them must be gone. */
void DestroyAllocator(Device &device);

struct Buffer
{
    VkBuffer buffer;
    Allocation allocation;
    /** Mapped for the buffer's lifetime when the memory is host visible. */
    void *mapped;
    VkDeviceSize size;
};

/** A buffer and its memory. `preferred` memory flags are used when a type has them. */
bool CreateBuffer(const Device &device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags required,
                  Buffer &out, VkMemoryPropertyFlags preferred = 0);
void DestroyBuffer(const Device &device, Buffer &buffer);

struct ImageDesc
{
    VkImageViewType viewType;
    VkFormat format;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t mipLevels;
    /** 6 for cube maps. */
    uint32_t layers;
    VkImageUsageFlags usage;
    VkImageAspectFlags aspect;
    VkComponentMapping swizzle;
};

struct Image
{
    VkImage image;
    Allocation allocation;
    VkImageView view;
    ImageDesc desc;
};

/** A device-local image and a view of all of it. */
bool CreateImage(const Device &device, const ImageDesc &desc, Image &out);
void DestroyImage(const Device &device, Image &image);

void ImageBarrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                  VkAccessFlags2 dstAccess);

/** A command buffer for setup work and tests: Begin, record into `cmd`, then Submit to run it and wait. */
class OneShot
{
  public:
    explicit OneShot(const Device &device);
    ~OneShot();
    bool Begin();
    bool Submit();

    VkCommandBuffer cmd;

  private:
    const Device &device_;
    VkCommandPool pool_;
    VkFence fence_;
};
} // namespace nfs::vk
