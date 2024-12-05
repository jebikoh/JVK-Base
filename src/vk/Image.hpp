#pragma once

#include "Allocator.hpp"
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>
namespace jvk {

class Image {
public:
    VkImage       image_;
    VkImageView   view_;
    VmaAllocation allocation_;
    VkExtent3D    extent_;
    VkFormat      format_;

    void init(
            VkDevice device,
            Allocator &allocator,
            VkFormat format,
            VkImageUsageFlags usageFlags,
            VkExtent3D extent
            );

    void destroy(VkDevice device, Allocator &allocator) const;
};

namespace create {
    VkImageCreateInfo imageInfo(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent);
    VkImageViewCreateInfo imageViewInfo(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags);
}

VkImageSubresourceRange imageSubresourceRange(VkImageAspectFlags aspectMask);

void transitionImage(
        VkCommandBuffer cmd,
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout
        );

void copyImage(VkCommandBuffer cmd, VkImage src, VkImage dst, VkExtent2D srcSize, VkExtent2D dstSize);

//void copyImage(VkCommandBuffer cmd, Image &src, Image &dst);
}

