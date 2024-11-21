#include "Commands.hpp"

#include "jvk.hpp"

namespace jvk {

CommandPool::CommandPool(const VkDevice device, const uint32_t familyIndex, const VkCommandPoolCreateFlags flags) {
    init(device, familyIndex, flags);
}

void CommandPool::init(const VkDevice device, const uint32_t familyIndex, const VkCommandPoolCreateFlags flags) {
    VkCommandPoolCreateInfo commandPoolInfo = {};
    commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolInfo.pNext = nullptr;
    commandPoolInfo.flags = flags;
    commandPoolInfo.queueFamilyIndex = familyIndex;

    VK_CHECK(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool));
    this->device = device;
    this->familyIndex = familyIndex;
}

void CommandPool::destroy() const {
    vkDestroyCommandPool(device, commandPool, nullptr);
}

[[nodiscard]]
VkCommandBuffer CommandPool::createCommandBuffer(const VkCommandBufferLevel level) const {
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.pNext = nullptr;
    cmdAllocInfo.commandPool = commandPool;
    cmdAllocInfo.commandBufferCount = 1;
    cmdAllocInfo.level = level;

    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd));
    return cmd;
}
}// namespace jvk