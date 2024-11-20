#pragma once
#include <SDL_vulkan.h>
#include "jvk.hpp"
#include "instance.hpp"
#include "swapchain.hpp"

namespace jvk {
class Engine {
public:
    Instance instance_;
    Swapchain swapchain_;

    VkExtent2D windowExtent_{800, 600};
    SDL_Window* window_;

    void init();
    void destroy();
private:
    void initSDL();
    void initVulkan();
    void initSwapchain();
};
}// namespace jvk
