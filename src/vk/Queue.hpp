#pragma once
#include "../../include/sdl/src/video/khronos/vulkan/vulkan_core.h"

namespace jvk {
struct Queue {
    VkQueue queue;
    uint32_t family;

    Queue(VkQueue queue, uint32_t family);
};
}