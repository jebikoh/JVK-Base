#include "Commands.hpp"

#include "../jvk.hpp"

namespace jvk {

CommandPool::CommandPool(const VkDevice device, const uint32_t familyIndex, const VkCommandPoolCreateFlags flags) {
    init(device, familyIndex, flags);
}

void CommandPool::init(const VkDevice device, const uint32_t familyIndex, const VkCommandPoolCreateFlags flags) {
    VkCommandPoolCreateInfo commandPoolInfo = {};
    commandPoolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolInfo.pNext            = nullptr;
    commandPoolInfo.flags            = flags;
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
    cmdAllocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.pNext              = nullptr;
    cmdAllocInfo.commandPool        = commandPool;
    cmdAllocInfo.commandBufferCount = 1;
    cmdAllocInfo.level              = level;

    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd));
    return cmd;
}

void beginCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferUsageFlags flags) {
    VkCommandBufferBeginInfo info{};
    info.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.pNext            = nullptr;
    info.pInheritanceInfo = nullptr;
    info.flags            = flags;

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &info));
}

void endCommandBuffer(VkCommandBuffer commandBuffer) {
    VK_CHECK(vkEndCommandBuffer(commandBuffer));
}

VkCommandBufferSubmitInfo submitCommandBufferInfo(VkCommandBuffer cmd) {
    VkCommandBufferSubmitInfo info{};
    info.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    info.pNext         = nullptr;
    info.commandBuffer = cmd;
    info.deviceMask    = 0;
    return info;
}

void submitCommandBuffer(
        VkQueue queue,
        VkCommandBuffer cmd,
        VkSemaphoreSubmitInfo *wait,
        VkSemaphoreSubmitInfo *signal,
        VkFence fence
        ) {
    VkCommandBufferSubmitInfo info = submitCommandBufferInfo(cmd);

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.pNext = nullptr;

    submitInfo.waitSemaphoreInfoCount = wait == nullptr ? 0 : 1;
    submitInfo.pWaitSemaphoreInfos = wait;

    submitInfo.signalSemaphoreInfoCount = signal == nullptr ? 0 : 1;
    submitInfo.pSignalSemaphoreInfos = signal;

    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &info;

    VK_CHECK(vkQueueSubmit2(queue, 1, &submitInfo, fence));
}

}// namespace jvk