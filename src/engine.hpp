#pragma once

#include <jvk.hpp>

class JVKEngine {
public:
    bool _isInitialized = false;
    int _frameNumber    = 0;
    bool _stopRendering = false;
    VkExtent2D _windowExtent{1700, 900};

    // VULKAN INSTANCE
    VkInstance _instance;
    VkDebugUtilsMessengerEXT _debugMessenger;
    VkPhysicalDevice _chosenGPU;
    VkDevice _device;
    VkSurfaceKHR _surface;

    // SWAPCHAIN
    VkSwapchainKHR _swapchain;
    VkFormat _swapchainImageFormat;
    std::vector<VkImage> _swapchainImages;
    std::vector<VkImageView> _swapchainImageViews;
    VkExtent2D _swapchainExtent;

    struct SDL_Window *_window = nullptr;

    static JVKEngine &get();

    void init();

    void cleanup();

    void draw();

    void run();

private:
    // INITIALIZATION
    void initVulkan();
    void initSwapchain();
    void initCommands();
    void initSyncStructures();

    // CREATION
    void createSwapchain(uint32_t width, uint32_t height);

    // DESTRUCTION
    void destroySwapchain();
};