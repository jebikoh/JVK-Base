#pragma once

#include <jvk.hpp>

namespace jvk {

struct CommandPool {
    VkDevice device      = VK_NULL_HANDLE;
    VkCommandPool pool   = VK_NULL_HANDLE;
    uint32_t familyIndex = 0;

    CommandPool(CommandPool const &)            = delete;
    CommandPool &operator=(CommandPool const &) = delete;

    CommandPool() {}

    VkResult init(VkDevice device, uint32_t familyIndex, VkCommandPoolCreateFlags flags);
    VkResult allocateCommandBuffer(VkCommandBuffer *cmd, uint32_t count = 1, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY) const;

    operator VkCommandPool() const { return pool; }

    void destroy();
};

}// namespace jvk