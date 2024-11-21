#pragma once
#include <vulkan/vulkan_core.h>

namespace jvk {
struct Semaphore {
    VkSemaphore semaphore;

    void init(VkDevice device, VkSemaphoreCreateFlags flags = 0) {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphoreInfo.pNext = nullptr;
        semaphoreInfo.flags = flags;

        vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore);
    }

    operator VkSemaphore() const { return semaphore; }

    VkSemaphoreSubmitInfo submitInfo(VkPipelineStageFlags2 stageMask) {
        VkSemaphoreSubmitInfo submitInfo{};
        submitInfo.sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        submitInfo.pNext       = nullptr;
        submitInfo.semaphore   = semaphore;
        submitInfo.stageMask   = stageMask;
        submitInfo.deviceIndex = 0;
        submitInfo.value       = 1;
        return submitInfo;
    }

    void destroy(VkDevice device) const {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
};
}