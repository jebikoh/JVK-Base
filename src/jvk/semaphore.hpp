#pragma once

#include <jvk.hpp>

namespace jvk {

struct Semaphore {
    VkSemaphore semaphore;
    VkDevice device;

    Semaphore() {};

    VkResult init(VkDevice device_, VkSemaphoreCreateFlags flags = 0) {
        device = device_;
        VkSemaphoreCreateInfo info = {};
        info.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        info.pNext                 = nullptr;
        info.flags                 = flags;
        return vkCreateSemaphore(device_, &info, nullptr, &semaphore);
    }

    operator VkSemaphore() const { return semaphore; }

    VkSemaphoreSubmitInfo submitInfo(VkPipelineStageFlags2 stageMask) const {
        VkSemaphoreSubmitInfo info = {};
        info.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        info.pNext                 = nullptr;
        info.semaphore             = semaphore;
        info.stageMask             = stageMask;
        info.deviceIndex           = 0;
        info.value                 = 1;
        return info;
    }

    void destroy() {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
};

}