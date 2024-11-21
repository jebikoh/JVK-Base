#pragma once

#include <vulkan/vulkan_core.h>
namespace jvk {

VkImageSubresourceRange imageSubresourceRange(VkImageAspectFlags aspectMask);

void transitionImage(
        VkCommandBuffer cmd,
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout
        );
}
