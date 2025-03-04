#pragma once

#include <jvk.hpp>

namespace VkUtil {

bool loadShaderModule(const char *filePath, VkDevice device, VkShaderModule *outShaderModule);

}