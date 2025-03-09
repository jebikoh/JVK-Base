#include <vk_init.hpp>
#include <vk_util.hpp>

void VkUtil::transitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
    // Creates a pipeline barrier stalls the pipeline until the image is ready
    // 1. All prior writes (srcAccessMask) from any stage (srcStageMask) must happen before the barrier
    // 2. The image layout (oldLayout) is transitioned to the new layout (newLayout)
    // 3. The image is now ready for reads and writes (dstAccessMask) from any stage (dstStageMask)

    VkImageMemoryBarrier2 imageBarrier = {};
    imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    imageBarrier.pNext = nullptr;

    // Stage masks specify when the barrier is executed
    // Access masks specify what type of memory operations are being executed

    // We are stating that this image may be used in any pipeline stage
    // before and after this barrier.
    imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    // We are stating that before the barrier, we are writing to the image
    // and that after the barrier, we might read or write to the image.
    imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

    // Specify the layout transition
    imageBarrier.oldLayout = oldLayout;
    imageBarrier.newLayout = newLayout;

    // Target the correct part of the image
    VkImageAspectFlags aspectMask = (newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    imageBarrier.subresourceRange = VkInit::imageSubresourceRange(aspectMask);
    imageBarrier.image = image;

    // Create the dependency & submit
    VkDependencyInfo depInfo = {};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.pNext = nullptr;
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &imageBarrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);
}

void VkUtil::copyImageToImage(VkCommandBuffer cmd, VkImage src, VkImage dst, VkExtent2D srcSize, VkExtent2D dstSize) {
    // Bit-block Transfer: copying data from one location to another
    // This is slower than `vkCmdCopyImage` but is more flexible
    VkImageBlit2 blitRegion {};
    blitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
    blitRegion.pNext = nullptr;

    blitRegion.srcOffsets[1].x = srcSize.width;
    blitRegion.srcOffsets[1].y = srcSize.height;
    blitRegion.srcOffsets[1].z = 1;

    blitRegion.dstOffsets[1].x = dstSize.width;
    blitRegion.dstOffsets[1].y = dstSize.height;
    blitRegion.dstOffsets[1].z = 1;

    blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.srcSubresource.baseArrayLayer = 0;
    blitRegion.srcSubresource.layerCount = 1;
    blitRegion.srcSubresource.mipLevel = 0;

    blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.dstSubresource.baseArrayLayer = 0;
    blitRegion.dstSubresource.layerCount = 1;
    blitRegion.srcSubresource.mipLevel = 0;

    VkBlitImageInfo2 blitInfo = {};
    blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
    blitInfo.pNext = nullptr;
    blitInfo.srcImage = src;
    blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    blitInfo.dstImage = dst;
    blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    blitInfo.filter = VK_FILTER_LINEAR;
    blitInfo.regionCount = 1;
    blitInfo.pRegions = &blitRegion;

    vkCmdBlitImage2(cmd, &blitInfo);
}

void VkUtil::generateMipmaps(VkCommandBuffer cmd, VkImage image, VkExtent2D imageSize) {
    const int mipLevels = static_cast<int>(std::floor(std::log2(std::max(imageSize.width, imageSize.height)))) + 1;
    for (int mip = 0; mip < mipLevels; ++mip) {
        VkExtent2D halfSize = imageSize;
        halfSize.width /= 2;
        halfSize.height /= 2;

        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.pNext = nullptr;

        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange = VkInit::imageSubresourceRange(aspectMask);
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseMipLevel = mip;
        barrier.image = image;

        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.pNext = nullptr;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(cmd, &dependencyInfo);
        if (mip < mipLevels - 1) {
            VkImageBlit2 blit{};
            blit.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
            blit.pNext = nullptr;

            blit.srcOffsets[1].x = imageSize.width;
            blit.srcOffsets[1].y = imageSize.height;
            blit.srcOffsets[1].z = 1;

            blit.dstOffsets[1].x = halfSize.width;
            blit.dstOffsets[1].y = halfSize.height;
            blit.dstOffsets[1].z = 1;

            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = 1;
            blit.srcSubresource.mipLevel = mip;

            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = 1;
            blit.dstSubresource.mipLevel = mip + 1;

            VkBlitImageInfo2 blitInfo{};
            blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
            blitInfo.pNext = nullptr;
            blitInfo.srcImage = image;
            blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            blitInfo.dstImage = image;
            blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            blitInfo.filter = VK_FILTER_LINEAR;
            blitInfo.regionCount = 1;
            blitInfo.pRegions = &blit;

            vkCmdBlitImage2(cmd, &blitInfo);
            imageSize = halfSize;
        }
    }

    transitionImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
