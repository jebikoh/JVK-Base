#include "Descriptors.hpp"


void jvk::DescriptorWriter::writeImage(int binding, VkImageView image, VkSampler sampler, VkImageLayout layout, VkDescriptorType type) {
    const VkDescriptorImageInfo &info = images.emplace_back(VkDescriptorImageInfo{
            .sampler     = sampler,
            .imageView   = image,
            .imageLayout = layout
    });

    VkWriteDescriptorSet write = {};
    write.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding           = binding;
    write.dstSet               = VK_NULL_HANDLE;
    write.descriptorCount      = 1;
    write.descriptorType       = type;
    write.pImageInfo           = &info;

    writes.push_back(write);
}
void jvk::DescriptorWriter::writeBuffer(int binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type) {
    VkDescriptorBufferInfo &info = buffers.emplace_back(VkDescriptorBufferInfo{
            .buffer = buffer,
            .offset = offset,
            .range  = size});

    VkWriteDescriptorSet write = {};
    write.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding           = binding;
    write.dstSet               = VK_NULL_HANDLE;
    write.descriptorCount      = 1;
    write.descriptorType       = type;
    write.pBufferInfo          = &info;

    writes.push_back(write);
}

void jvk::DescriptorWriter::clear() {
    images.clear();
    buffers.clear();
    writes.clear();
}
void jvk::DescriptorWriter::updateSet(VkDevice device, VkDescriptorSet set) {
    for (VkWriteDescriptorSet & write : writes) {
        write.dstSet = set;
    }
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0 , nullptr);
}

void jvk::DescriptorAllocator::initPool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios) {
    std::vector<VkDescriptorPoolSize> poolSizes;
    for (PoolSizeRatio ratio: poolRatios) {
        poolSizes.push_back(VkDescriptorPoolSize{
                .type            = ratio.type,
                .descriptorCount = uint32_t(ratio.ratio * maxSets)});
    }

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags                      = 0;
    poolInfo.maxSets                    = maxSets;
    poolInfo.poolSizeCount              = (uint32_t) poolSizes.size();
    poolInfo.pPoolSizes                 = poolSizes.data();

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
    allocInfo.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.pNext                       = nullptr;
    allocInfo.descriptorPool              = pool;
    allocInfo.descriptorSetCount          = 1;
    allocInfo.pSetLayouts                 = &layout;

    VkDescriptorSet ds;
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &ds));
    return ds;
}

void jvk::DynamicDescriptorAllocator::init(VkDevice device, uint32_t initialSets, std::span<PoolSizeRatio> poolRatios) {
    ratios_.clear();
    for (auto r: poolRatios) {
        ratios_.push_back(r);
    }

    const VkDescriptorPool pool = createPool(device, initialSets, poolRatios);
    setsPerPool_                = initialSets * 1.5f;
    ready_.push_back(pool);
}

void jvk::DynamicDescriptorAllocator::clearPools(VkDevice device) {
    for (const auto p: ready_) {
        vkResetDescriptorPool(device, p, 0);
    }

    for (const auto p: full_) {
        vkResetDescriptorPool(device, p, 0);
        ready_.push_back(p);
    }
    full_.clear();
}

void jvk::DynamicDescriptorAllocator::destroyPools(VkDevice device) {
    for (const auto p: ready_) {
        vkDestroyDescriptorPool(device, p, nullptr);
    }
    ready_.clear();
    for (const auto p: full_) {
        vkDestroyDescriptorPool(device, p, nullptr);
    }
    full_.clear();
}

VkDescriptorSet jvk::DynamicDescriptorAllocator::allocate(VkDevice device, VkDescriptorSetLayout layout, void *pNext) {
    VkDescriptorPool pool = getPool(device);

    VkDescriptorSetAllocateInfo info = {};
    info.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    info.pNext                       = pNext;
    info.descriptorPool              = pool;
    info.descriptorSetCount          = 1;
    info.pSetLayouts                 = &layout;

    VkDescriptorSet set;
    VkResult result = vkAllocateDescriptorSets(device, &info, &set);
    if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
        full_.push_back(pool);
        pool                = getPool(device);
        info.descriptorPool = pool;
        VK_CHECK(vkAllocateDescriptorSets(device, &info, &set));
    }
    ready_.push_back(pool);
    return set;
}

VkDescriptorPool jvk::DynamicDescriptorAllocator::getPool(VkDevice device) {
    VkDescriptorPool pool;
    if (!ready_.empty()) {
        pool = ready_.back();
        ready_.pop_back();
    } else {
        pool = createPool(device, setsPerPool_, ratios_);
        setsPerPool_ *= 1.5f;
        if (setsPerPool_ > 4092) {
            setsPerPool_ = 4092;
        }
    }

    return pool;
}

VkDescriptorPool jvk::DynamicDescriptorAllocator::createPool(const VkDevice device, const uint32_t setCount, std::span<PoolSizeRatio> poolRatios) {
    std::vector<VkDescriptorPoolSize> poolSizes;
    for (auto [type, ratio]: poolRatios) {
        poolSizes.push_back(VkDescriptorPoolSize{
                .type            = type,
                .descriptorCount = static_cast<uint32_t>(ratio * setCount)});
    }

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags                      = 0;
    poolInfo.maxSets                    = setCount;
    poolInfo.poolSizeCount              = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes                 = poolSizes.data();
    VkDescriptorPool pool;
    vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool);
    return pool;
}