#pragma once

#include "instance.hpp"
#include "jvk.hpp"
#include <VkBootstrap.h>

namespace jvk {

struct Swapchain {
    VkSwapchainKHR swapchain;
    VkFormat imageFormat;

    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    VkExtent2D extent;

    Swapchain(Instance &instance,
              uint32_t width,
              uint32_t height,
              VkFormat format = VK_FORMAT_B8G8R8A8_UNORM,
              VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
              VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR,
              VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_TRANSFER_DST_BIT
              ) {
        vkb::SwapchainBuilder builder{instance.physicalDevice, instance.device, instance.surface};

        imageFormat = format;

        VkSurfaceFormatKHR surfaceFormat{};
        surfaceFormat.format = imageFormat;
        surfaceFormat.colorSpace = colorSpace;

        vkb::Swapchain vkbSwapchain = builder
        .set_desired_format(surfaceFormat)
        .set_desired_present_mode(presentMode)
        .set_desired_extent(width, height)
        .add_image_usage_flags(imageUsageFlags)
        .build()
        .value();

        extent = vkbSwapchain.extent;
        swapchain = vkbSwapchain.swapchain;
        images = vkbSwapchain.get_images().value();
        imageViews = vkbSwapchain.get_image_views().value();
    }

    void destroy(Instance &instance) {
        vkDestroySwapchainKHR(instance.device, swapchain, nullptr);
        for (auto & imageView : imageViews) vkDestroyImageView(instance.device, imageView, nullptr);
    }
};

}