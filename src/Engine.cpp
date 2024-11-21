//
// Created by Aneesh on 11/20/2024.
//

#include "Engine.hpp"

#include "vk/Commands.hpp"
#include "vk/Image.hpp"

#include <SDL.h>
#include <SDL_vulkan.h>
#include <VkBootstrap.h>
#include <iostream>
#include <thread>

namespace jvk {

constexpr bool USE_VALIDATION_LAYERS = false;

void Engine::init() {
    initSDL();
    initVulkan();
    initSwapchain();
    initCommands();
    initSyncStructures();
}

void Engine::destroy() {
    // Clean up frame command pools
    for (auto & frame : frames_) {
        frame.commandPool.destroy();
        frame.drawFence.destroy(context_);
        frame.drawSemaphore.destroy(context_);
        frame.swapchainSemaphore.destroy(context_);
    }

    swapchain_.destroy(context_);
    context_.destroy();
    SDL_DestroyWindow(window_);
}

#pragma region Initialization
void Engine::initSDL() {
    SDL_Init(SDL_INIT_VIDEO);

    auto window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);
    window_ = SDL_CreateWindow(
            "Vulkan Engine",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            windowExtent_.width,
            windowExtent_.height,
            window_flags);
}

void Engine::initVulkan() {
    vkb::InstanceBuilder builder;

    auto vkbInstance = builder.set_app_name("JVK")
        .request_validation_layers(USE_VALIDATION_LAYERS)
        .use_default_debug_messenger()
        .require_api_version(1, 3, 0)
        .build();

    if (!vkbInstance) {
        std::cerr << "Failed to create Vulkan instance. Error: " << vkbInstance.error().message() << "\n";
        return;
    }

    vkb::Instance vkbInst = vkbInstance.value();
    context_.instance = vkbInst.instance;
    context_.debug = vkbInst.debug_messenger;

    SDL_Vulkan_CreateSurface(window_, context_.instance, &context_.surface);

    // Features
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = true;
    features13.synchronization2 = true;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType               = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing  = true;

    // Device
    vkb::PhysicalDeviceSelector selector{vkbInst};
    auto vkbPhysicalDevice = selector
                      .set_minimum_version(1, 3)
                      .set_required_features_13(features13)
                      .set_required_features_12(features12)
                      .set_surface(context_.surface)
                      .select()
                      .value();
    context_.physicalDevice = vkbPhysicalDevice.physical_device;

    vkb::DeviceBuilder deviceBuilder{vkbPhysicalDevice};
    vkb::Device vkbDevice = deviceBuilder.build().value();
    context_.device = vkbDevice.device;

    // Queue
    graphicsQueue_.queue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    graphicsQueue_.family = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
}

void Engine::initSwapchain() {
    swapchain_ = Swapchain{context_, windowExtent_.width, windowExtent_.height};
}

void Engine::initCommands() {
    for (auto & frame : frames_) {
        frame.commandPool.init(context_, graphicsQueue_.family, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
        frame.mainCommandBuffer = frame.commandPool.createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    }
}

void Engine::initSyncStructures() {
    for (auto & frame : frames_) {
        frame.swapchainSemaphore.init(context_);
        frame.drawSemaphore.init(context_);
        frame.drawFence.init(context_, VK_FENCE_CREATE_SIGNALED_BIT);
    }
}
void Engine::draw() {
    auto &frame = this->getCurrentFrame();
    frame.drawFence.wait(context_);
    frame.drawFence.reset(context_);

    auto swapchainImageIndex = swapchain_.acquireNextImage(context_, frame.swapchainSemaphore);

    VkCommandBuffer cmd = getCurrentFrame().mainCommandBuffer;
    VK_CHECK(vkResetCommandBuffer(cmd, 0));
    beginCommandBuffer(cmd, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    transitionImage(cmd, swapchain_.images[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkClearColorValue clearValue;
    float flash = std::abs(std::sin(frameNumber_ / 120.f));
    clearValue = { { 0.0f, 0.0f, flash, 1.0f } };

    VkImageSubresourceRange clearRange = imageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);
    vkCmdClearColorImage(cmd, swapchain_.images[swapchainImageIndex], VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);

    transitionImage(cmd, swapchain_.images[swapchainImageIndex], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    endCommandBuffer(cmd);

    auto waitSemaphore = frame.swapchainSemaphore.submitInfo(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR);
    auto signalSemaphore = frame.drawSemaphore.submitInfo(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);

    submitCommandBuffer(graphicsQueue_.queue, cmd, &waitSemaphore, &signalSemaphore, frame.drawFence);

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext              = nullptr;
    presentInfo.pSwapchains        = &swapchain_.swapchain;
    presentInfo.swapchainCount     = 1;
    presentInfo.pWaitSemaphores    = &frame.drawSemaphore.semaphore;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pImageIndices      = &swapchainImageIndex;
    VK_CHECK(vkQueuePresentKHR(graphicsQueue_.queue, &presentInfo));

    frameNumber_++;
}
void Engine::run() {
    SDL_Event e;
    bool quit = false;

    while (!quit) {
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) { quit = true;}
            if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) { stopRendering_ = true; }
                if (e.window.event == SDL_WINDOWEVENT_RESTORED) { stopRendering_ = false; }
            }
        }
        if (stopRendering_) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
        else { draw(); }
    }
}
#pragma endregion

}// namespace jvk