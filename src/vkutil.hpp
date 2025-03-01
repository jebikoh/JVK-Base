#pragma once

#include <vulkan/vulkan.h>

namespace VkUtil {

void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);

}