// ReSharper disable CppNonExplicitConversionOperator
#pragma once
#include "../jvk.hpp"
#include "VkBootstrap.h"

namespace jvk {

struct Context {
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkSurfaceKHR surface;

    Context() {};

    void destroy() const {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyDevice(device, nullptr);
        vkb::destroy_debug_utils_messenger(instance, debug);
        vkDestroyInstance(instance, nullptr);
    }

    operator VkDevice() const { return device; } // NOLINT(*-explicit-constructor)
    operator VkInstance() const { return instance; } // NOLINT(*-explicit-constructor)
    operator VkPhysicalDevice() const { return physicalDevice; } // NOLINT(*-explicit-constructor)
    operator VkSurfaceKHR() const { return surface; } // NOLINT(*-explicit-constructor)
};

}