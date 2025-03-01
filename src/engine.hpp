#pragma once

#include <jvk.hpp>

struct FrameData {
    // FRAME COMMANDS
    VkCommandPool _commandPool;
    VkCommandBuffer _mainCommandBuffer;

    // FRAME SYNC
    // Semaphores:
    //  1. To have render commands wait on swapchain image request
    //  2. Control presentation of rendered image to OS after draw
    // Fences:
    //  1. Wait for draw commands of a submitted cmd buffer to be finished
    VkSemaphore _swapchainSemaphore;
    VkSemaphore _renderSemaphore;
    VkFence _renderFence;
};

constexpr unsigned int JVK_NUM_FRAMES = 2;


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

    // FRAME DATA
    FrameData _frames[JVK_NUM_FRAMES];
    FrameData &getCurrentFrame() { return _frames[_frameNumber % JVK_NUM_FRAMES]; }

    // QUEUE
    VkQueue _graphicsQueue;
    uint32_t _graphicsQueueFamily;

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