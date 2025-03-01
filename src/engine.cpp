#include <engine.hpp>
#include <jvk.hpp>

#include <SDL.h>
#include <SDL_vulkan.h>

#include "VkBootstrap.h"

#include <chrono>
#include <thread>

constexpr bool JVK_USE_VALIDATION_LAYERS = true;

JVKEngine *loadedEngine = nullptr;

JVKEngine &JVKEngine::get() {
    return *loadedEngine;
}

void JVKEngine::init() {
    fmt::println("init()");
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
    fmt::println("cleanup()");
    if (_isInitialized) {
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
    fmt::println("draw()");
}

void JVKEngine::run() {
    fmt::println("run()");
    SDL_Event e;
    bool bQuit = false;

    while (!bQuit) {
        fmt::println("run(): loop");
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
    fmt::println("initVulkan()");
    // CREATE INSTANCE
    fmt::println("initVulkan(): CREATE INSTANCE");
    vkb::InstanceBuilder builder;
    vkb::Instance vkbInstance = builder.set_app_name("JVK")
                                        .request_validation_layers(JVK_USE_VALIDATION_LAYERS)
                                        .use_default_debug_messenger()
                                        .require_api_version(1, 3, 0)
                                        .build()
                                        .value();

    _instance       = vkbInstance.instance;
    _debugMessenger = vkbInstance.debug_messenger;

    // CREATE SURFACE
    fmt::println("initVulkan(): CREATE SURFACE");
    SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

    // 1.3 FEATURES
    fmt::println("initVulkan(): 1.3 FEATURES");
    VkPhysicalDeviceVulkan13Features features13;
    features13.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = true;
    features13.synchronization2 = true;
    features13.pNext            = nullptr;

    // 1.2 FEATURES
    fmt::println("initVulkan(): 1.2 FEATURES");
    VkPhysicalDeviceVulkan12Features features12;
    features12.sType               = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing  = true;
    features12.pNext               = nullptr;

    // PHYSICAL DEVICE
    fmt::println("initVulkan(): PHYSICAL DEVICE");
    vkb::PhysicalDeviceSelector physicalDeviceBuilder{vkbInstance};
    vkb::PhysicalDevice vkbPhysicalDevice = physicalDeviceBuilder.set_minimum_version(1, 3)
                                                    .set_required_features_13(features13)
                                                    .set_required_features_12(features12)
                                                    .set_surface(_surface)
                                                    .select()
                                                    .value();

    // DEVICE
    fmt::println("initVulkan(): DEVICE");
    vkb::DeviceBuilder deviceBuilder{vkbPhysicalDevice};
    vkb::Device vkbDevice = deviceBuilder.build().value();
    _device               = vkbDevice.device;
    _chosenGPU            = vkbPhysicalDevice.physical_device;
}

void JVKEngine::initSwapchain() {
    fmt::println("initSwapchain()");
    createSwapchain(_windowExtent.width, _windowExtent.height);
}

void JVKEngine::initCommands() {
    fmt::println("initCommands()");
}

void JVKEngine::initSyncStructures() {
    fmt::println("initSyncStructures()");
}

void JVKEngine::createSwapchain(uint32_t width, uint32_t height) {
    fmt::println("createSwapchain()");
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
    fmt::println("destroySwapchain()");
    vkDestroySwapchainKHR(_device, _swapchain, nullptr);
    for (int i = 0; i < _swapchainImageViews.size(); ++i) {
        vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
    }
}
