#include <vk_descriptors.hpp>

void DescriptorLayoutBuilder::addBinding(uint32_t binding, VkDescriptorType type) {
    VkDescriptorSetLayoutBinding newBinding{};
    newBinding.binding         = binding;
    newBinding.descriptorCount = 1;
    newBinding.descriptorType  = type;
    bindings.push_back(newBinding);
}

void DescriptorLayoutBuilder::clear() {
    bindings.clear();
}

VkDescriptorSetLayout DescriptorLayoutBuilder::build(VkDevice device, VkShaderStageFlags shaderStages, void *pNext, VkDescriptorSetLayoutCreateFlags flags) {
    for (auto &b: bindings) {
        b.stageFlags |= shaderStages;
    }

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.pNext        = pNext;
    info.pBindings    = bindings.data();
    info.bindingCount = static_cast<uint32_t>(bindings.size());
    info.flags        = flags;

    VkDescriptorSetLayout set;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &set));

    return set;
}

void DescriptorAllocator::initPool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios) {
    std::vector<VkDescriptorPoolSize> poolSizes;
    for (auto ratio: poolRatios) {
        poolSizes.push_back({.type            = ratio.type,
                             .descriptorCount = uint32_t(ratio.ratio * maxSets)});
    }

    VkDescriptorPoolCreateInfo info{};
    info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.maxSets       = maxSets;
    info.poolSizeCount = (uint32_t) poolSizes.size();
    info.pPoolSizes    = poolSizes.data();

    vkCreateDescriptorPool(device, &info, nullptr, &pool);
}

void DescriptorAllocator::clearDescriptors(VkDevice device) {
    vkResetDescriptorPool(device, pool, 0);
}

void DescriptorAllocator::destroyPool(VkDevice device) {
    vkDestroyDescriptorPool(device, pool, nullptr);
}

VkDescriptorSet DescriptorAllocator::allocate(VkDevice device, VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo info{};
    info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    info.pNext              = nullptr;
    info.descriptorPool     = pool;
    info.descriptorSetCount = 1;
    info.pSetLayouts        = &layout;

    VkDescriptorSet ds;
    VK_CHECK(vkAllocateDescriptorSets(device, &info, &ds));

    return ds;
}

void DynamicDescriptorAllocator::init(VkDevice device, uint32_t initialSets, std::span<PoolSizeRatio> poolRatios) {
    ratios.clear();
    for (auto r: poolRatios) {
        ratios.push_back(r);
    }

    VkDescriptorPool newPool = createPool(device, initialSets, poolRatios);
    setsPerPool              = initialSets * 1.5;

    readyPools.push_back(newPool);
}

VkDescriptorPool DynamicDescriptorAllocator::getPool(VkDevice device) {
    VkDescriptorPool pool;
    if (readyPools.size() != 0) {
        pool = readyPools.back();
        readyPools.pop_back();
    } else {
        pool = createPool(device, setsPerPool, ratios);

        setsPerPool = setsPerPool * 1.5;
        if (setsPerPool > 4092) {
            setsPerPool = 4092;
        }
    }

    return pool;
}

VkDescriptorPool DynamicDescriptorAllocator::createPool(VkDevice device, uint32_t setCount, std::span<PoolSizeRatio> poolRatios) {
    std::vector<VkDescriptorPoolSize> poolSizes;
    for (PoolSizeRatio ratio: poolRatios) {
        poolSizes.push_back(VkDescriptorPoolSize{
                .type            = ratio.type,
                .descriptorCount = uint32_t(ratio.ratio * setCount)});
    }

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags         = 0;
    poolInfo.maxSets       = setCount;
    poolInfo.poolSizeCount = (uint32_t) poolSizes.size();
    poolInfo.pPoolSizes    = poolSizes.data();

    VkDescriptorPool pool;
    vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool);
    return pool;
}

void DynamicDescriptorAllocator::clearPools(VkDevice device) {
    for (auto p: readyPools) {
        vkResetDescriptorPool(device, p, 0);
    }
    for (auto p: fullPools) {
        vkResetDescriptorPool(device, p, 0);
        readyPools.push_back(p);
    }
    fullPools.clear();
}

void DynamicDescriptorAllocator::destroyPools(VkDevice device) {
    for (auto p: readyPools) {
        vkDestroyDescriptorPool(device, p, 0);
    }
    readyPools.clear();
    for (auto p: fullPools) {
        vkDestroyDescriptorPool(device, p, 0);
    }
    fullPools.clear();
}

VkDescriptorSet DynamicDescriptorAllocator::allocate(VkDevice device, VkDescriptorSetLayout layout, void *pNext) {
    VkDescriptorPool pool = getPool(device);

    VkDescriptorSetAllocateInfo info{};
    info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    info.pNext              = pNext;
    info.descriptorPool     = pool;
    info.descriptorSetCount = 1;
    info.pSetLayouts        = &layout;

    VkDescriptorSet ds;
    VkResult result = vkAllocateDescriptorSets(device, &info, &ds);
    if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
        fullPools.push_back(pool);
        pool                = getPool(device);
        info.descriptorPool = pool;

        VK_CHECK(vkAllocateDescriptorSets(device, &info, &ds));
    }

    readyPools.push_back(pool);
    return ds;
}

void DescriptorWriter::writeImage(int binding, VkImageView image, VkSampler sampler, VkImageLayout layout, VkDescriptorType type) {
    VkDescriptorImageInfo &info = images.emplace_back(VkDescriptorImageInfo{
            .sampler     = sampler,
            .imageView   = image,
            .imageLayout = layout});

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding      = binding;
    write.dstSet          = VK_NULL_HANDLE;
    write.descriptorCount = 1;
    write.descriptorType  = type;
    write.pImageInfo      = &info;
    writes.push_back(write);
}

void DescriptorWriter::writeBuffer(int binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type) {
    VkDescriptorBufferInfo &info = buffers.emplace_back(VkDescriptorBufferInfo{
            .buffer = buffer,
            .offset = offset,
            .range  = size});

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding      = binding;
    write.dstSet          = VK_NULL_HANDLE;
    write.descriptorCount = 1;
    write.descriptorType  = type;
    write.pBufferInfo     = &info;
    writes.push_back(write);
}

void DescriptorWriter::clear() {
    images.clear();
    buffers.clear();
    writes.clear();
}

void DescriptorWriter::updateSet(VkDevice device, VkDescriptorSet set) {
    for (VkWriteDescriptorSet  &write : writes) {
        write.dstSet = set;
    }

    vkUpdateDescriptorSets(device, (uint32_t) writes.size(), writes.data(), 0, nullptr);
}
