#pragma once

#include <jvk.hpp>

namespace jvk {

struct Buffer {
    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo info;

    operator VkBuffer() const { return buffer; }

    void destroy(VmaAllocator allocator) const {
        vmaDestroyBuffer(allocator, buffer, allocation);
    }
};

}