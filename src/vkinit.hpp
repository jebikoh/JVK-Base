#include <jvk.hpp>

namespace VkInit {

inline VkCommandPoolCreateInfo commandPool(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags = 0) {
    VkCommandPoolCreateInfo info = {};
    info.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.pNext                   = nullptr;
    info.flags                   = flags;
    info.queueFamilyIndex        = queueFamilyIndex;
    return info;
}

inline VkCommandBufferAllocateInfo commandBuffer(VkCommandPool pool, uint32_t count = 1, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
    VkCommandBufferAllocateInfo info = {};
    info.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.pNext                       = nullptr;
    info.commandPool                 = pool;
    info.commandBufferCount          = count;
    info.level                       = level;
    return info;
}

inline VkFenceCreateInfo fence(VkFenceCreateFlags flags = 0) {
    VkFenceCreateInfo info = {};
    info.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    info.pNext             = nullptr;
    info.flags             = flags;
    return info;
}

inline VkSemaphoreCreateInfo semaphore(VkSemaphoreCreateFlags flags = 0) {
    VkSemaphoreCreateInfo info = {};
    info.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    info.pNext                 = nullptr;
    info.flags                 = flags;
    return info;
}

inline VkCommandBufferBeginInfo commandBufferBegin(VkCommandBufferUsageFlags flags = 0) {
    VkCommandBufferBeginInfo info = {};
    info.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.pNext                    = nullptr;
    info.pInheritanceInfo         = nullptr;
    info.flags                    = flags;
    return info;
}

inline VkImageSubresourceRange imageSubresourceRange(VkImageAspectFlags aspectMask) {
    VkImageSubresourceRange range = {};
    range.aspectMask              = aspectMask;
    range.baseMipLevel            = 0;
    range.levelCount              = VK_REMAINING_MIP_LEVELS;
    range.baseArrayLayer          = 0;
    range.layerCount              = VK_REMAINING_ARRAY_LAYERS;
    return range;
}

inline VkSemaphoreSubmitInfo semaphoreSubmit(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore) {
    VkSemaphoreSubmitInfo info = {};
    info.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    info.pNext                 = nullptr;
    info.semaphore             = semaphore;
    info.stageMask             = stageMask;
    info.deviceIndex           = 0;
    info.value                 = 1;
    return info;
}

inline VkCommandBufferSubmitInfo commandBufferSubmit(VkCommandBuffer cmd) {
    VkCommandBufferSubmitInfo info = {};
    info.sType                     = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    info.pNext                     = nullptr;
    info.commandBuffer             = cmd;
    info.deviceMask                = 0;
    return info;
}

inline VkSubmitInfo2 submit(VkCommandBufferSubmitInfo *cmdInfo, VkSemaphoreSubmitInfo *signalSemaphoreInfo, VkSemaphoreSubmitInfo *waitSemaphoreInfo) {
    VkSubmitInfo2 info            = {};
    info.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    info.pNext                    = nullptr;
    info.waitSemaphoreInfoCount   = waitSemaphoreInfo == nullptr ? 0 : 1;
    info.pWaitSemaphoreInfos      = waitSemaphoreInfo;
    info.signalSemaphoreInfoCount = signalSemaphoreInfo == nullptr ? 0 : 1;
    info.pSignalSemaphoreInfos    = signalSemaphoreInfo;
    info.commandBufferInfoCount   = 1;
    info.pCommandBufferInfos      = cmdInfo;
    return info;
}

}// namespace VkInit