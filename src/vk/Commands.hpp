#pragma once
#include "../jvk.hpp"


#include <vulkan/vulkan.h>

namespace jvk {

struct CommandPool {
    CommandPool(CommandPool const &) = delete;
    CommandPool &operator=(CommandPool const &) = delete;

    CommandPool() {}

    CommandPool(
        VkDevice device,
        uint32_t familyIndex,
        VkCommandPoolCreateFlags flags
        );
    ~CommandPool() { destroy(); }

    void init(
        VkDevice device,
        uint32_t familyIndex,
        VkCommandPoolCreateFlags flags
        );

    void destroy() const;

    operator VkCommandPool() const { return commandPool; } // NOLINT(*-explicit-constructor)

    [[nodiscard]]
    VkCommandBuffer createCommandBuffer(const VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY) const;

    VkDevice device           = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    uint32_t familyIndex      = 0;
};

void beginCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferUsageFlags flags = 0);
void endCommandBuffer(VkCommandBuffer commandBuffer);
void submitCommandBuffer(
        VkQueue queue,
        VkCommandBuffer cmd,
        VkSemaphoreSubmitInfo *wait,
        VkSemaphoreSubmitInfo *signal,
        VkFence fence
);
}