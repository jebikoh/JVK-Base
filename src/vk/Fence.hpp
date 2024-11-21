#pragma once
#include <vulkan/vulkan_core.h>

namespace jvk {
    struct Fence {
        VkFence fence;

        void init(VkDevice device, const VkFenceCreateFlags flags = 0) {
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.pNext = nullptr;
            fenceInfo.flags = flags;

            vkCreateFence(device, &fenceInfo, nullptr, &fence);
        }

        operator VkFence() const { return fence; }

        void wait(VkDevice device, uint64_t timeout = 1000000000) {
            VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, timeout));
        }

        void reset(VkDevice device) {
            VK_CHECK(vkResetFences(device, 1, &fence));
        }

        void destroy(VkDevice device) const {
            vkDestroyFence(device, fence, nullptr);
        }
    };
}