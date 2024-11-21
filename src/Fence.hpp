#pragma once
#include <vulkan/vulkan_core.h>

namespace jvk {
    struct Fence {
        VkFence fence;

        void init(const VkDevice device, const VkFenceCreateFlags flags = 0) {
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.pNext = nullptr;
            fenceInfo.flags = flags;

            vkCreateFence(device, &fenceInfo, nullptr, &fence);
        }

        operator VkFence() const { return fence; }
    };
}