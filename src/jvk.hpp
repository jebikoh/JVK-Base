#pragma once

#include <vulkan/vulkan.h>

#include <vector>

#include <iostream>
#define VK_CHECK(x)                                                 \
do                                                              \
{                                                               \
VkResult err = x;                                           \
if (err)                                                    \
{                                                           \
std::cout <<"Detected Vulkan error: " << err << std::endl; \
abort();                                                \
}                                                           \
} while (0)