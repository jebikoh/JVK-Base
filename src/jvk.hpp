#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <span>
#include <array>
#include <functional>
#include <deque>

#include <volk.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vk_mem_alloc.h>

#include <fmt/core.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <logger.hpp>

inline void checkVulkanError(const VkResult result, char const *const func, const char *const file, int const line) {
    if (result) {
        LOG_FATAL("Detected Vulkan error at {}:{} '{}': {}", file, line, func, string_VkResult(result));
    }
}

#define CHECK_VK(err) checkVulkanError((err), #err, __FILE__, __LINE__)

constexpr uint64_t JVK_TIMEOUT = 1000000000;
