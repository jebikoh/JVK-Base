#pragma once

#include <jvk.hpp>

// VULKAN DESCRIPTORS
// VkDescriptorSetLayout -> VkDescriptorPool (Allocate) -> VkDescriptorSet
// VkDescriptorSet holds bindings (pointers) to various resources on GPU.
//
// Vulkan Pipelines have multiple slots for different descriptor sets.
// The specification guarantees at least 4 sets

// To create a descriptor layout, we need an array of bindings
struct DescriptorLayoutBuilder {
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    void addBinding(uint32_t binding, VkDescriptorType type);
    void clear();
    VkDescriptorSetLayout build(VkDevice device, VkShaderStageFlags shaderStages, void *pNext = nullptr, VkDescriptorSetLayoutCreateFlags flags = 0);
};
