#pragma once
#include "jvk.hpp"
#include <VkBootstrap.h>

namespace jvk {

struct Instance {
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkSurfaceKHR surface;

    void destroy() const {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyDevice(device, nullptr);
        vkb::destroy_debug_utils_messenger(instance, debug);
        vkDestroyInstance(instance, nullptr);
    }
};

}