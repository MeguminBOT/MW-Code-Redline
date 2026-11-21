#include <string.h>

#include "core/log.h"
#include "engine/rendering/vulkan/resources.h"

namespace nfs::vk
{
namespace
{
const VkDeviceSize kBlockSize = 64ull << 20;

struct FreeRange
{
    VkDeviceSize offset;
    VkDeviceSize size;
};

struct MemoryBlock
{
    VkDeviceMemory memory;
    uint64_t address;
    uint32_t type;
    /** Buffers and images keep to separate blocks, so bufferImageGranularity never matters. */
    bool linear;
    uint32_t live;
    uint32_t freeCount;
    uint32_t freeCapacity;
    /** Free ranges in offset order. */
    FreeRange *free;
};
} // namespace

struct MemoryAllocator
{
    MemoryBlock *blocks;
    uint32_t blockCount;
    uint32_t blockCapacity;
};

namespace
{
bool AllocateMemory(const Device &device, VkDeviceSize size, uint32_t type, VkDeviceMemory &memory,
                    uint64_t *address = nullptr)
{
    // With ray queries, any buffer may be read through its device address (acceleration structure inputs).
    VkMemoryAllocateFlagsInfo flags = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    if (device.caps.addressLog)
        flags.flags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT;
    VkMemoryAllocateInfo info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                 device.caps.rayQuery || device.caps.addressLog ? &flags : nullptr};
    info.allocationSize = size;
    info.memoryTypeIndex = type;
    const DWORD start = GetTickCount();
    VkResult result = vkAllocateMemory(device.device, &info, nullptr, &memory);
    if (GetTickCount() - start >= 250)
        nfs::Log("Vulkan: vkAllocateMemory of %d bytes took %u ms", static_cast<int>(size), GetTickCount() - start);
    if (result != VK_SUCCESS)
    {
        nfs::Log("Vulkan: vkAllocateMemory of %d bytes failed (%s)", static_cast<int>(size), ResultName(result));
        return false;
    }
    if (device.caps.addressLog)
    {
        VkDeviceMemoryOpaqueCaptureAddressInfo where = {VK_STRUCTURE_TYPE_DEVICE_MEMORY_OPAQUE_CAPTURE_ADDRESS_INFO};
        where.memory = memory;
        const uint64_t at = vkGetDeviceMemoryOpaqueCaptureAddress(device.device, &where);
        if (address)
            *address = at;
        nfs::Log("Vulkan: memory %08x%08x-%08x%08x allocated (%u bytes, type %u)", static_cast<uint32_t>(at >> 32),
                 static_cast<uint32_t>(at), static_cast<uint32_t>((at + size) >> 32), static_cast<uint32_t>(at + size),
                 static_cast<uint32_t>(size), type);
    }
    return true;
}

void InsertRange(MemoryBlock &block, uint32_t at, VkDeviceSize offset, VkDeviceSize size)
{
    if (block.freeCount == block.freeCapacity)
    {
        uint32_t capacity = block.freeCapacity ? block.freeCapacity * 2 : 64;
        FreeRange *ranges = static_cast<FreeRange *>(::operator new(sizeof(FreeRange) * capacity));
        if (block.free)
        {
            memcpy(ranges, block.free, sizeof(FreeRange) * block.freeCount);
            ::operator delete(block.free);
        }
        block.free = ranges;
        block.freeCapacity = capacity;
    }
    memmove(block.free + at + 1, block.free + at, sizeof(FreeRange) * (block.freeCount - at));
    block.free[at] = {offset, size};
    ++block.freeCount;
}

void RemoveRange(MemoryBlock &block, uint32_t at)
{
    memmove(block.free + at, block.free + at + 1, sizeof(FreeRange) * (block.freeCount - at - 1));
    --block.freeCount;
}

/** First fit: takes `needs` out of a free range, keeping what is left before and after it free. */
bool Carve(MemoryBlock &block, const VkMemoryRequirements &needs, Allocation &out)
{
    for (uint32_t i = 0; i < block.freeCount; ++i)
    {
        const VkDeviceSize offset = block.free[i].offset, end = offset + block.free[i].size;
        const VkDeviceSize start = (offset + needs.alignment - 1) & ~(needs.alignment - 1);
        if (start + needs.size > end)
            continue;
        if (start > offset)
        {
            block.free[i].size = start - offset;
            if (start + needs.size < end)
                InsertRange(block, i + 1, start + needs.size, end - start - needs.size);
        }
        else if (start + needs.size < end)
        {
            block.free[i] = {start + needs.size, end - start - needs.size};
        }
        else
        {
            RemoveRange(block, i);
        }
        out.memory = block.memory;
        out.address = block.address ? block.address + start : 0;
        out.offset = start;
        out.size = needs.size;
        ++block.live;
        return true;
    }
    return false;
}

bool Allocate(const Device &device, const VkMemoryRequirements &needs, VkMemoryPropertyFlags required,
              VkMemoryPropertyFlags preferred, bool linear, Allocation &out, VkMemoryPropertyFlags *flags)
{
    out = {};
    uint32_t type = FindMemoryType(device, needs.memoryTypeBits, required, preferred);
    if (type == ~0u)
    {
        nfs::Log("Vulkan: no memory type for %x (allowed types %x)", required, needs.memoryTypeBits);
        return false;
    }
    const VkMemoryPropertyFlags properties = device.memory.memoryTypes[type].propertyFlags;
    if (flags)
        *flags = properties;
    MemoryAllocator *allocator = device.allocator;
    if (allocator && !(properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && needs.size <= kBlockSize / 4)
    {
        for (uint32_t b = 0; b < allocator->blockCount; ++b)
        {
            MemoryBlock &block = allocator->blocks[b];
            if (block.type == type && block.linear == linear && Carve(block, needs, out))
            {
                out.block = b + 1;
                return true;
            }
        }
        VkDeviceMemory memory;
        uint64_t address = 0;
        if (AllocateMemory(device, kBlockSize, type, memory, &address))
        {
            if (allocator->blockCount == allocator->blockCapacity)
            {
                uint32_t capacity = allocator->blockCapacity ? allocator->blockCapacity * 2 : 16;
                MemoryBlock *blocks = static_cast<MemoryBlock *>(::operator new(sizeof(MemoryBlock) * capacity));
                if (allocator->blocks)
                {
                    memcpy(blocks, allocator->blocks, sizeof(MemoryBlock) * allocator->blockCount);
                    ::operator delete(allocator->blocks);
                }
                allocator->blocks = blocks;
                allocator->blockCapacity = capacity;
            }
            MemoryBlock &block = allocator->blocks[allocator->blockCount];
            block = {memory, address, type, linear, 0, 0, 0, nullptr};
            InsertRange(block, 0, 0, kBlockSize);
            ++allocator->blockCount;
            if (Carve(block, needs, out))
            {
                out.block = allocator->blockCount;
                return true;
            }
        }
    }
    if (!AllocateMemory(device, needs.size, type, out.memory, &out.address))
        return false;
    out.size = needs.size;
    return true;
}

void Free(const Device &device, Allocation &allocation)
{
    if (!allocation.memory)
        return;
    if (!allocation.block)
    {
        if (device.caps.addressLog)
            nfs::Log("Vulkan: memory %08x%08x freed (%u bytes)", static_cast<uint32_t>(allocation.address >> 32),
                     static_cast<uint32_t>(allocation.address), static_cast<uint32_t>(allocation.size));
        vkFreeMemory(device.device, allocation.memory, nullptr);
        allocation = {};
        return;
    }
    MemoryBlock &block = device.allocator->blocks[allocation.block - 1];
    uint32_t i = 0;
    while (i < block.freeCount && block.free[i].offset < allocation.offset)
        ++i;
    const bool joinsPrevious = i > 0 && block.free[i - 1].offset + block.free[i - 1].size == allocation.offset;
    const bool joinsNext = i < block.freeCount && allocation.offset + allocation.size == block.free[i].offset;
    if (joinsPrevious && joinsNext)
    {
        block.free[i - 1].size += allocation.size + block.free[i].size;
        RemoveRange(block, i);
    }
    else if (joinsPrevious)
    {
        block.free[i - 1].size += allocation.size;
    }
    else if (joinsNext)
    {
        block.free[i].offset = allocation.offset;
        block.free[i].size += allocation.size;
    }
    else
    {
        InsertRange(block, i, allocation.offset, allocation.size);
    }
    --block.live;
    allocation = {};
}
} // namespace

bool CreateAllocator(Device &device)
{
    device.allocator = static_cast<MemoryAllocator *>(::operator new(sizeof(MemoryAllocator)));
    memset(device.allocator, 0, sizeof(MemoryAllocator));
    return true;
}

void DestroyAllocator(Device &device)
{
    MemoryAllocator *allocator = device.allocator;
    if (!allocator)
        return;
    for (uint32_t b = 0; b < allocator->blockCount; ++b)
    {
        MemoryBlock &block = allocator->blocks[b];
        if (block.live)
            nfs::Log("Vulkan: memory block %d still has %d resources", b, block.live);
        vkFreeMemory(device.device, block.memory, nullptr);
        ::operator delete(block.free);
    }
    ::operator delete(allocator->blocks);
    ::operator delete(allocator);
    device.allocator = nullptr;
}

bool CreateBuffer(const Device &device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags required,
                  Buffer &out, VkMemoryPropertyFlags preferred)
{
    out = {};
    VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    if (vkCreateBuffer(device.device, &info, nullptr, &out.buffer) != VK_SUCCESS)
        return false;
    VkMemoryRequirements needs;
    vkGetBufferMemoryRequirements(device.device, out.buffer, &needs);
    VkMemoryPropertyFlags flags = 0;
    if (!Allocate(device, needs, required, preferred, true, out.allocation, &flags) ||
        vkBindBufferMemory(device.device, out.buffer, out.allocation.memory, out.allocation.offset) != VK_SUCCESS)
    {
        DestroyBuffer(device, out);
        return false;
    }
    if ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
        vkMapMemory(device.device, out.allocation.memory, out.allocation.offset, out.allocation.size, 0,
                    &out.mapped) != VK_SUCCESS)
    {
        DestroyBuffer(device, out);
        return false;
    }
    out.size = size;
    if (device.caps.addressLog && needs.size >= (1u << 20))
        nfs::Log("Vulkan: buffer of %u bytes (usage %x) at %08x%08x", static_cast<uint32_t>(size), usage,
                 static_cast<uint32_t>(out.allocation.address >> 32), static_cast<uint32_t>(out.allocation.address));
    return true;
}

void DestroyBuffer(const Device &device, Buffer &buffer)
{
    if (device.caps.addressLog && buffer.allocation.size >= (1u << 20))
        nfs::Log("Vulkan: buffer at %08x%08x destroyed", static_cast<uint32_t>(buffer.allocation.address >> 32),
                 static_cast<uint32_t>(buffer.allocation.address));
    if (buffer.buffer)
        vkDestroyBuffer(device.device, buffer.buffer, nullptr);
    Free(device, buffer.allocation);
    buffer = {};
}

bool CreateImage(const Device &device, const ImageDesc &desc, Image &out)
{
    out = {};
    out.desc = desc;
    VkImageCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = desc.viewType == VK_IMAGE_VIEW_TYPE_3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    if (desc.viewType == VK_IMAGE_VIEW_TYPE_CUBE)
        info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    info.format = desc.format;
    info.extent = {desc.width, desc.height, desc.depth ? desc.depth : 1};
    info.mipLevels = desc.mipLevels ? desc.mipLevels : 1;
    info.arrayLayers = desc.layers ? desc.layers : 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = desc.usage;
    VkResult result = vkCreateImage(device.device, &info, nullptr, &out.image);
    if (result != VK_SUCCESS)
    {
        nfs::Log("Vulkan: vkCreateImage %dx%d format %d failed (%s)", desc.width, desc.height, desc.format,
                 ResultName(result));
        return false;
    }
    VkMemoryRequirements needs;
    vkGetImageMemoryRequirements(device.device, out.image, &needs);
    if (!Allocate(device, needs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, false, out.allocation, nullptr) ||
        vkBindImageMemory(device.device, out.image, out.allocation.memory, out.allocation.offset) != VK_SUCCESS)
    {
        DestroyImage(device, out);
        return false;
    }
    if (device.caps.addressLog && needs.size >= (1u << 20))
        nfs::Log("Vulkan: image %ux%ux%u format %d, %u levels, %u layers at %08x%08x +%u", desc.width, desc.height,
                 desc.depth ? desc.depth : 1, desc.format, info.mipLevels, info.arrayLayers,
                 static_cast<uint32_t>(out.allocation.address >> 32), static_cast<uint32_t>(out.allocation.address),
                 static_cast<uint32_t>(needs.size));
    VkImageViewCreateInfo view = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = out.image;
    view.viewType = desc.viewType;
    view.format = desc.format;
    view.components = desc.swizzle;
    view.subresourceRange = {desc.aspect, 0, info.mipLevels, 0, info.arrayLayers};
    if (vkCreateImageView(device.device, &view, nullptr, &out.view) != VK_SUCCESS)
    {
        DestroyImage(device, out);
        return false;
    }
    return true;
}

void DestroyImage(const Device &device, Image &image)
{
    if (device.caps.addressLog && image.allocation.size >= (1u << 20))
        nfs::Log("Vulkan: image %ux%u at %08x%08x destroyed", image.desc.width, image.desc.height,
                 static_cast<uint32_t>(image.allocation.address >> 32), static_cast<uint32_t>(image.allocation.address));
    if (image.view)
        vkDestroyImageView(device.device, image.view, nullptr);
    if (image.image)
        vkDestroyImage(device.device, image.image, nullptr);
    Free(device, image.allocation);
    image = {};
}

void ImageBarrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                  VkAccessFlags2 dstAccess)
{
    VkImageMemoryBarrier2 barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
    VkDependencyInfo dependency = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

OneShot::OneShot(const Device &device) : cmd(VK_NULL_HANDLE), device_(device), pool_(VK_NULL_HANDLE), fence_(VK_NULL_HANDLE)
{
    VkCommandPoolCreateInfo pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool.queueFamilyIndex = device.queueFamily;
    if (vkCreateCommandPool(device.device, &pool, nullptr, &pool_) != VK_SUCCESS)
        return;
    VkCommandBufferAllocateInfo alloc = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = pool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    vkAllocateCommandBuffers(device.device, &alloc, &cmd);
    VkFenceCreateInfo fence = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(device.device, &fence, nullptr, &fence_);
}

OneShot::~OneShot()
{
    if (fence_)
        vkDestroyFence(device_.device, fence_, nullptr);
    if (pool_)
        vkDestroyCommandPool(device_.device, pool_, nullptr);
}

bool OneShot::Begin()
{
    if (!cmd || !fence_)
        return false;
    vkResetCommandPool(device_.device, pool_, 0);
    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    return vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS;
}

bool OneShot::Submit()
{
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        return false;
    VkCommandBufferSubmitInfo cmdInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = cmd;
    VkSubmitInfo2 submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    VkResult result = vkQueueSubmit2(device_.queue, 1, &submit, fence_);
    if (result == VK_SUCCESS)
        result = vkWaitForFences(device_.device, 1, &fence_, VK_TRUE, 10000000000ull);
    vkResetFences(device_.device, 1, &fence_);
    if (result != VK_SUCCESS)
        nfs::Log("Vulkan: one-shot submission failed (%s)", ResultName(result));
    return result == VK_SUCCESS;
}
} // namespace nfs::vk
