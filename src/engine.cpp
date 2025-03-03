#include <engine.hpp>
#include <jvk.hpp>
#include <vk_init.hpp>
#include <vk_util.hpp>

#include <SDL.h>
#include <SDL_vulkan.h>

#include "VkBootstrap.h"

#include <chrono>
#include <thread>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

constexpr bool JVK_USE_VALIDATION_LAYERS = true;

JVKEngine *loadedEngine = nullptr;

JVKEngine &JVKEngine::get() {
    return *loadedEngine;
}

void JVKEngine::init() {
    assert(loadedEngine == nullptr);
    loadedEngine = this;

    SDL_Init(SDL_INIT_VIDEO);

    SDL_WindowFlags windowFlags = (SDL_WindowFlags) (SDL_WINDOW_VULKAN);

    _window = SDL_CreateWindow(
            "JVK",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            _windowExtent.width,
            _windowExtent.height,
            windowFlags);

    initVulkan();
    initSwapchain();
    initCommands();
    initSyncStructures();

    _isInitialized = true;
}

void JVKEngine::cleanup() {
    if (_isInitialized) {
        vkDeviceWaitIdle(_device);

        for (int i = 0; i < JVK_NUM_FRAMES; ++i) {
            vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);

            vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
            vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
            vkDestroySemaphore(_device, _frames[i]._swapchainSemaphore, nullptr);

            _frames[i]._deletionQueue.flush();
        }

        _globalDeletionQueue.flush();
        vkDestroyImageView(_device, _drawImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);

        vmaDestroyAllocator(_allocator);

        destroySwapchain();

        vkDestroySurfaceKHR(_instance, _surface, nullptr);
        vkDestroyDevice(_device, nullptr);

        vkb::destroy_debug_utils_messenger(_instance, _debugMessenger);
        vkDestroyInstance(_instance, nullptr);
        SDL_DestroyWindow(_window);
    }

    loadedEngine = nullptr;
}

void JVKEngine::draw() {
    // Wait and reset render fence
    VK_CHECK(vkWaitForFences(_device, 1, &getCurrentFrame()._renderFence, true, 1000000000));
    VK_CHECK(vkResetFences(_device, 1, &getCurrentFrame()._renderFence));

    // Request an image from swapchain
    uint32_t swapchainImageIndex;
    VK_CHECK(vkAcquireNextImageKHR(_device, _swapchain, 1000000000, getCurrentFrame()._swapchainSemaphore, nullptr, &swapchainImageIndex));

    // Reset the command buffer
    VkCommandBuffer cmd = getCurrentFrame()._mainCommandBuffer;
    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    _drawExtent.width = _drawImage.imageExtent.width;
    _drawExtent.height = _drawImage.imageExtent.height;

    // Start the command buffer
    VkCommandBufferBeginInfo cmdBeginInfo = VkInit::commandBufferBegin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    // Transition draw image to general
    VkUtil::transitionImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    // Draw
    drawBackground(cmd);

    // Transition draw image to transfer source
    VkUtil::transitionImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    // Transition swapchain image to transfer destination
    VkUtil::transitionImage(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Copy image to from draw image to swapchain
    VkUtil::copyImageToImage(cmd, _drawImage.image, _swapchainImages[swapchainImageIndex], _drawExtent, _swapchainExtent);

    // Transition swapchain for presentation
    VkUtil::transitionImage(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    // End command buffer
    VK_CHECK(vkEndCommandBuffer(cmd));

    // Submit buffer
    // srcStageMask set to COLOR_ATTACHMENT_OUTPUT_BIT to wait for color attachment output (waiting for swapchain image)
    // dstStageMask set to ALL_GRAPHICS_BIT to signal that all graphics stages are done
    VkCommandBufferSubmitInfo cmdInfo = VkInit::commandBufferSubmit(cmd);
    VkSemaphoreSubmitInfo waitInfo    = VkInit::semaphoreSubmit(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, getCurrentFrame()._swapchainSemaphore);
    VkSemaphoreSubmitInfo signalInfo  = VkInit::semaphoreSubmit(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, getCurrentFrame()._renderSemaphore);
    VkSubmitInfo2 submit              = VkInit::submit(&cmdInfo, &signalInfo, &waitInfo);

    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, getCurrentFrame()._renderFence));

    // Present
    VkPresentInfoKHR presentInfo   = {};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext              = nullptr;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &_swapchain;
    presentInfo.pWaitSemaphores    = &getCurrentFrame()._renderSemaphore;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pImageIndices      = &swapchainImageIndex;
    VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));

    _frameNumber++;
}

void JVKEngine::run() {
    SDL_Event e;
    bool bQuit = false;

    while (!bQuit) {
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) bQuit = true;

            if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                    _stopRendering = true;
                }
                if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
                    _stopRendering = false;
                }
            }
        }

        if (_stopRendering) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        draw();
    }
}

void JVKEngine::initVulkan() {
    // CREATE INSTANCE
    vkb::InstanceBuilder builder;
    auto vkbInstanceResult = builder.set_app_name("JVK")
                                     .request_validation_layers(JVK_USE_VALIDATION_LAYERS)
                                     .use_default_debug_messenger()
                                     .require_api_version(1, 3, 0)
                                     .build();

    if (!vkbInstanceResult) {
        fmt::println("Failed to create Vulkan instance. Error: {}", vkbInstanceResult.error().message());
        abort();
    }

    vkb::Instance vkbInstance = vkbInstanceResult.value();

    _instance       = vkbInstance.instance;
    _debugMessenger = vkbInstance.debug_messenger;

    // CREATE SURFACE
    SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

    // 1.3 FEATURES
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = true;
    features13.synchronization2 = true;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType               = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing  = true;

    // PHYSICAL DEVICE
    vkb::PhysicalDeviceSelector physicalDeviceBuilder{vkbInstance};
    auto vkbPhysicalDeviceResult = physicalDeviceBuilder.set_minimum_version(1, 3)
                                           .set_required_features_13(features13)
                                           .set_required_features_12(features12)
                                           .set_surface(_surface)
                                           .select();

    if (!vkbPhysicalDeviceResult) {
        fmt::println("Failed to select physical device. Error: {}", vkbPhysicalDeviceResult.error().message());
        abort();
    }

    vkb::PhysicalDevice vkbPhysicalDevice = vkbPhysicalDeviceResult.value();

    // DEVICE
    vkb::DeviceBuilder deviceBuilder{vkbPhysicalDevice};
    vkb::Device vkbDevice = deviceBuilder.build().value();
    _device               = vkbDevice.device;
    _chosenGPU            = vkbPhysicalDevice.physical_device;

    // QUEUE
    _graphicsQueue       = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    _graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    // VMA
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice         = _chosenGPU;
    allocatorInfo.device                 = _device;
    allocatorInfo.instance               = _instance;
    allocatorInfo.flags                  = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocatorInfo, &_allocator);
}

void JVKEngine::initSwapchain() {
    createSwapchain(_windowExtent.width, _windowExtent.height);

    // CREATE DRAW IMAGE
    VkExtent3D drawImageExtent = {
            _windowExtent.width,
            _windowExtent.height,
            1};

    _drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;// 16-bit float image
    _drawImage.imageExtent = drawImageExtent;

    VkImageUsageFlags drawImageUsages = {};
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;    // Copy from image
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;    // Copy to image
    drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;         // Allow compute shader to write
    drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;// Graphics pipeline

    VkImageCreateInfo drawImageInfo = VkInit::image(_drawImage.imageFormat, drawImageUsages, drawImageExtent);

    // VMA_MEMORY_USAGE_GPU_ONLY: the texture will never be accessed from the CPU
    // VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT: GPU exclusive memory flag, guarantees that the memory is on the GPU
    VmaAllocationCreateInfo drawImageAllocInfo = {};
    drawImageAllocInfo.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;
    drawImageAllocInfo.requiredFlags           = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vmaCreateImage(_allocator, &drawImageInfo, &drawImageAllocInfo, &_drawImage.image, &_drawImage.allocation, nullptr);

    VkImageViewCreateInfo imageViewInfo = VkInit::imageView(_drawImage.imageFormat, _drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK(vkCreateImageView(_device, &imageViewInfo, nullptr, &_drawImage.imageView));
}

void JVKEngine::initCommands() {
    // COMMAND POOL
    // Indicate that buffers should be individually resettable
    VkCommandPoolCreateFlags flags          = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPoolCreateInfo commandPoolInfo = VkInit::commandPool(_graphicsQueueFamily, flags);

    // COMMAND BUFFERS
    for (int i = 0; i < JVK_NUM_FRAMES; ++i) {
        VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

        VkCommandBufferAllocateInfo cmdAllocInfo = VkInit::commandBuffer(_frames[i]._commandPool);

        VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));
    }
}

void JVKEngine::initSyncStructures() {
    // FENCE
    // Start signaled to wait on the first frame
    VkFenceCreateInfo fenceCreateInfo = VkInit::fence(VK_FENCE_CREATE_SIGNALED_BIT);

    // SEMAPHORE
    VkSemaphoreCreateInfo semaphoreCreateInfo = VkInit::semaphore();

    for (int i = 0; i < JVK_NUM_FRAMES; ++i) {
        VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));

        VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._swapchainSemaphore));
        VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore));
    }
}

void JVKEngine::createSwapchain(uint32_t width, uint32_t height) {
    vkb::SwapchainBuilder swapchainBuilder{_chosenGPU, _device, _surface};
    _swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

    vkb::Swapchain vkbSwapchain = swapchainBuilder
                                          .set_desired_format(
                                                  VkSurfaceFormatKHR{
                                                          .format     = _swapchainImageFormat,
                                                          .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
                                          .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                                          .set_desired_extent(width, height)
                                          .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                                          .build()
                                          .value();
    _swapchainExtent     = vkbSwapchain.extent;
    _swapchain           = vkbSwapchain.swapchain;
    _swapchainImages     = vkbSwapchain.get_images().value();
    _swapchainImageViews = vkbSwapchain.get_image_views().value();
}

void JVKEngine::destroySwapchain() {
    vkDestroySwapchainKHR(_device, _swapchain, nullptr);
    for (int i = 0; i < _swapchainImageViews.size(); ++i) {
        vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
    }
}

void JVKEngine::drawBackground(VkCommandBuffer cmd) {
    VkClearColorValue clearValue;
    float flash = std::abs(std::sin(_frameNumber / 120.0f));
    clearValue = {{0.0f, 0.0f, flash, 1.0f}};

    VkImageSubresourceRange clearRange = VkInit::imageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);

    vkCmdClearColorImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);
}
