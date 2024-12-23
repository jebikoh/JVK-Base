#pragma once

#include "../jvk.hpp"

#include <deque>
#include <span>

namespace jvk {
struct DescriptorLayoutBindings {
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

struct DescriptorWriter {
    std::deque<VkDescriptorImageInfo> images;
    std::deque<VkDescriptorBufferInfo> buffers;
    std::vector<VkWriteDescriptorSet> writes;

    void writeImage(int binding, VkImageView image, VkSampler sampler, VkImageLayout layout, VkDescriptorType type);
    void writeBuffer(int binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type);

    void clear();
    void updateSet(VkDevice device, VkDescriptorSet set);
};

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

class DynamicDescriptorAllocator {
public:
    struct PoolSizeRatio {
        VkDescriptorType type;
        float ratio;
    };

    void init(VkDevice device, uint32_t initialSets, std::span<PoolSizeRatio> poolRatios);
    void clearPools(VkDevice device);
    void destroyPools(VkDevice device);

    VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout, void *pNext = nullptr);
private:
    std::vector<PoolSizeRatio> ratios_;
    std::vector<VkDescriptorPool> full_;
    std::vector<VkDescriptorPool> ready_;
    uint32_t setsPerPool_ = 0;

    VkDescriptorPool getPool(VkDevice device);
    VkDescriptorPool createPool(VkDevice device, uint32_t setCount, std::span<PoolSizeRatio> poolRatios);
};

}// namespace jvk
