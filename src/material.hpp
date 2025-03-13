#pragma once
#include <jvk.hpp>
#include <jvk/buffer.hpp>

// MATERIALS
enum class MaterialPass : uint8_t {
    MAIN_COLOR,
    TRANSPARENT,
    OTHER
};

struct MaterialPipeline {
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;

    void destroy(const VkDevice device, const bool destroyLayout = false) const {
        if (destroyLayout) {
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        }
        vkDestroyPipeline(device, pipeline, nullptr);
    }
};

struct MaterialInstance {
    MaterialPipeline *pipeline;
    VkDescriptorSet materialSet;
    MaterialPass passType;

    void destroy(const VkDevice device, const bool destroyLayout = false) const {
        pipeline->destroy(device);
    }
};

struct GLTFMaterial {
    MaterialInstance data;
};
