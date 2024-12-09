#pragma once

#include "../jvk.hpp"
#include <vma/vk_mem_alloc.h>

namespace jvk {
struct Buffer {
    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo allocationInfo;

    Buffer() = default;

    Buffer(size_t allocSize, VmaAllocator allocator, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) { // NOLINT(*-pro-type-member-init)
        VkBufferCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.pNext = nullptr;
        info.size = allocSize;
        info.usage = usage;

        VmaAllocationCreateInfo vmaInfo = {};
        vmaInfo.usage = memoryUsage;
        vmaInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VK_CHECK(vmaCreateBuffer(allocator, &info, &vmaInfo, &buffer, &allocation, &allocationInfo));
    }

    void destroy(VmaAllocator allocator) const {
        vmaDestroyBuffer(allocator, buffer, allocation);
    }
};
}

