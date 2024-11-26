#include "Allocator.hpp"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

namespace jvk {

void Allocator::init(VkDevice device, VkPhysicalDevice physicalDevice, VkInstance instance) {
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device         = device;
    allocatorInfo.instance       = instance;
    allocatorInfo.flags          = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    vmaCreateAllocator(&allocatorInfo, &allocator);
}

}// namespace jvk