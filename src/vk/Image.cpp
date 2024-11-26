//
// Created by Aneesh on 11/20/2024.
//

#include "Image.hpp"

VkImageSubresourceRange jvk::imageSubresourceRange(VkImageAspectFlags aspectMask) {
    VkImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask = aspectMask;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    return subresourceRange;
}

void jvk::transitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkImageMemoryBarrier2 imBarrier{};
    imBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    imBarrier.pNext = nullptr;

    imBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    imBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;

    imBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    imBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;

    imBarrier.oldLayout = oldLayout;
    imBarrier.newLayout = newLayout;

    VkImageAspectFlags aspectMask = (newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    imBarrier.subresourceRange = imageSubresourceRange(aspectMask);
    imBarrier.image            = image;

    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.pNext = nullptr;

    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &imBarrier;

    vkCmdPipelineBarrier2(cmd, &depInfo);
}

VkImageCreateInfo jvk::create::imageCreateInfo(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent) {
    VkImageCreateInfo info = {};
    info.sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.pNext       = nullptr;
    info.imageType   = VK_IMAGE_TYPE_2D;
    info.format      = format;
    info.extent      = extent;
    info.mipLevels   = 1;
    info.arrayLayers = 1;
    info.samples     = VK_SAMPLE_COUNT_1_BIT;
    info.tiling      = VK_IMAGE_TILING_OPTIMAL;
    info.usage       = usageFlags;
    return info;
}

VkImageViewCreateInfo jvk::create::imageViewCreateInfo(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo info = {};
    info.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.pNext    = nullptr;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.image    = image;
    info.format   = format;
    info.subresourceRange.baseMipLevel   = 0;
    info.subresourceRange.levelCount     = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount     = 1;
    info.subresourceRange.aspectMask     = aspectFlags;
    return info;
}

void jvk::Image::init(jvk::Allocator &allocator, VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent, VkImageAspectFlags aspectFlags) {
    format_ = format;
    extent_ = extent;

    auto imageCreateInfo = create::imageCreateInfo(format_, usageFlags, extent_);
}