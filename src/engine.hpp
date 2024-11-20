#pragma once
#include <SDL_vulkan.h>
#include "jvk.hpp"

namespace jvk {
class Engine {
public:
    VkInstance instance_;
    VkDebugUtilsMessengerEXT debug_;
    VkPhysicalDevice physicalDevice_;
    VkDevice device_;
    VkSurfaceKHR surface_;

    VkExtent2D windowExtent_{800, 600};
    SDL_Window* window_;

    void init();
private:
    void initSDL();
    void initVulkan();
};
}// namespace jvk
