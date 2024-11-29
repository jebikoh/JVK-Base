#include "Descriptors.hpp"

void jvk::DescriptorAllocator::initPool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios) {
    std::vector<VkDescriptorPoolSize> poolSizes;
    for (PoolSizeRatio ratio : poolRatios) {
        poolSizes.push_back(VkDescriptorPoolSize{
                .type = ratio.type,
                .descriptorCount = uint32_t(ratio.ratio * maxSets)
        });
    }

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags         = 0;
    poolInfo.maxSets       = maxSets;
    poolInfo.poolSizeCount = (uint32_t)poolSizes.size();
    poolInfo.pPoolSizes    = poolSizes.data();

    vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool);
}

void jvk::DescriptorAllocator::clearDescriptors(VkDevice device) const {
    vkResetDescriptorPool(device, pool, 0);
}

void jvk::DescriptorAllocator::destroyPool(VkDevice device) const {
    vkDestroyDescriptorPool(device, pool, nullptr);
}

VkDescriptorSet jvk::DescriptorAllocator::allocate(VkDevice device, VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.pNext = nullptr;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet ds;
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &ds));
    return ds;
}
