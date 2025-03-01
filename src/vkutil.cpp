#include <vkutil.hpp>
#include <vkinit.hpp>

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
