#pragma once

#include "../jvk.hpp"
#include <fstream>

namespace jvk {

inline bool loadShaderModule(const char *path, VkDevice device, VkShaderModule *outShaderModule) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) { return false; }

    size_t fileSize = (size_t) file.tellg();
    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

    file.seekg(0);
    file.read((char *) buffer.data(), fileSize);
    file.close();

    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType                    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.pNext                    = nullptr;
    createInfo.codeSize                 = buffer.size() * sizeof(uint32_t);
    createInfo.pCode                    = buffer.data();

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        return false;
    }
    *outShaderModule = shaderModule;
    return true;
}

namespace create {
inline VkPipelineShaderStageCreateInfo pipelineShaderStage(
        VkShaderStageFlagBits stage,
        VkShaderModule shaderModule,
        const char *entry) {
    VkPipelineShaderStageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.pNext = nullptr;

    info.stage  = stage;
    info.module = shaderModule;
    info.pName  = entry;
    return info;
}
}// namespace create
}// namespace jvk