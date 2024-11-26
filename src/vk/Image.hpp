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
            Allocator &allocator,
            VkFormat format,
            VkImageUsageFlags usageFlags,
            VkExtent3D extent,
            VkImageAspectFlags aspectFlags)
            ;
};

namespace create {
    VkImageCreateInfo imageCreateInfo(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent);
    VkImageViewCreateInfo imageViewCreateInfo(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags);
}

VkImageSubresourceRange imageSubresourceRange(VkImageAspectFlags aspectMask);

void transitionImage(
        VkCommandBuffer cmd,
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout
        );
}
