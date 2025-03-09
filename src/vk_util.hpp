#pragma once

#include <vulkan/vulkan.h>

namespace VkUtil {

void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);

void copyImageToImage(VkCommandBuffer cmd, VkImage src, VkImage dst, VkExtent2D srcSize, VkExtent2D dstSize);

void generateMipmaps(VkCommandBuffer cmd, VkImage image, VkExtent2D imageSize);
}