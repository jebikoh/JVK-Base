#pragma once

#include "../jvk.hpp"
#include <span>

namespace jvk {

struct DescriptorSetBindings {
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    void addBinding(uint32_t binding, VkDescriptorType type) {
        VkDescriptorSetLayoutBinding newBind{};
        newBind.binding = binding;
        newBind.descriptorCount = 1;
        newBind.descriptorType = type;
        bindings.emplace_back(newBind);
    }

    void clear() { bindings.clear(); }

    VkDescriptorSetLayout build(
            VkDevice device,
            VkShaderStageFlags shaderStages,
            void *pNext = nullptr,
            VkDescriptorSetLayoutCreateFlags flags = 0
            ) {
        for (auto &b: bindings) {
            b.stageFlags |= shaderStages;
        }

        VkDescriptorSetLayoutCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.pNext = pNext;

        info.pBindings    = bindings.data();
        info.bindingCount = (uint32_t) bindings.size();
        info.flags        = flags;

        VkDescriptorSetLayout set;
        VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &set));

        return set;
    }
};

// Improve this later
struct DescriptorAllocator {
    struct PoolSizeRatio {
        VkDescriptorType type;
        float ratio;
    };

    VkDescriptorPool pool;

    void initPool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);
    void clearDescriptors(VkDevice device) const;
    void destroyPool(VkDevice device) const;

    VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout);
};

}