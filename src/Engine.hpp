#pragma once
#include "jvk.hpp"
#include "vk/Commands.hpp"
#include "vk/Context.hpp"
#include "vk/Fence.hpp"
#include "vk/Queue.hpp"
#include "vk/Semaphore.hpp"
#include "vk/Swapchain.hpp"

#include <SDL_vulkan.h>

namespace jvk {

constexpr unsigned int NUM_FRAMES = 2;

class Engine {
public:
    Context context_;
    Swapchain swapchain_;
    bool stopRendering_ = false;

    VkExtent2D windowExtent_{800, 600};
    SDL_Window* window_;

    // Frame data
    struct FrameData {
        CommandPool commandPool;
        VkCommandBuffer mainCommandBuffer;
        // Per-frame sync
        Semaphore swapchainSemaphore, drawSemaphore;
        Fence drawFence;
    };

    size_t frameNumber_ = 0;
    FrameData frames_[NUM_FRAMES];
    FrameData &getCurrentFrame() { return frames_[frameNumber_ % NUM_FRAMES]; }

    Queue graphicsQueue_;

    void init();
    void destroy();
    void draw();
    void run();
private:
    void initSDL();
    void initVulkan();
    void initSwapchain();
    void initCommands();
    void initSyncStructures();
};
}// namespace jvk
