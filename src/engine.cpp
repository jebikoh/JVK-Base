//
// Created by Aneesh on 11/20/2024.
//

#include "engine.hpp"

#include <SDL.h>
#include <SDL_vulkan.h>
#include <VkBootstrap.h>
#include <iostream>

namespace jvk {

constexpr bool USE_VALIDATION_LAYERS = false;

void Engine::init() {
    initSDL();
    initVulkan();
    initSwapchain();
}

void Engine::destroy() {
    swapchain_.destroy(instance_);
    instance_.destroy();
    SDL_DestroyWindow(window_);
}

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
    instance_.instance = vkbInst.instance;
    instance_.debug = vkbInst.debug_messenger;

    SDL_Vulkan_CreateSurface(window_, instance_.instance, &instance_.surface);

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
                      .set_surface(instance_.surface)
                      .select()
                      .value();
    instance_.physicalDevice = vkbPhysicalDevice.physical_device;

    vkb::DeviceBuilder deviceBuilder{vkbPhysicalDevice};
    vkb::Device vkbDevice = deviceBuilder.build().value();
    instance_.device = vkbDevice.device;
}

void Engine::initSwapchain() {
    swapchain_ = Swapchain{instance_, windowExtent_.width, windowExtent_.height};
}


}// namespace jvk