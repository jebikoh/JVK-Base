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

    void destroy() {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
};

}