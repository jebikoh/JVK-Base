#pragma once
#include <vk_mem_alloc.h>
#include "Context.hpp"

namespace jvk {

struct Allocator {
    VmaAllocator allocator;

    Allocator() = default;

    void init(VkDevice device, VkPhysicalDevice physicalDevice, VkInstance instance);
    void init(Context & context) { init(context.device, context.physicalDevice, context.instance); }

    void destroy() const { vmaDestroyAllocator(allocator); }
};

}// namespace jvk
