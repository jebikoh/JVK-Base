#pragma once
#include <jvk/commands.hpp>

VkResult jvk::CommandPool::init(VkDevice device_, uint32_t familyIndex_, VkCommandPoolCreateFlags flags) {
    device = device_;
    familyIndex = familyIndex_;

    VkCommandPoolCreateInfo info = {};
    info.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.pNext                   = nullptr;
    info.flags                   = flags;
    info.queueFamilyIndex        = familyIndex_;
    return vkCreateCommandPool(device_, &info, nullptr, &pool);
}

void jvk::CommandPool::destroy() {
    vkDestroyCommandPool(device, pool, nullptr);
}

VkResult jvk::CommandPool::allocateCommandBuffer(VkCommandBuffer *cmd, uint32_t count, VkCommandBufferLevel level) const {
    VkCommandBufferAllocateInfo info = {};
    info.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.pNext                       = nullptr;
    info.commandPool                 = pool;
    info.commandBufferCount          = count;
    info.level                       = level;
    return vkAllocateCommandBuffers(device, &info, cmd);
}
