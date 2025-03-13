#pragma once
#include <jvk.hpp>
#include <jvk/buffer.hpp>
#include <jvk/image.hpp>
#include <jvk/descriptor.hpp>

// MATERIALS

/**
 * Determines what pipeline and descriptors to bind
 */
enum class MaterialPass : uint8_t {
    MAIN_COLOR,
    TRANSPARENT,
    OTHER
};

/**
 * Contains the pipeline and layout to bind for a specific material
 */
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

/**
 * A single instance of a material, containing the required pipeline and descriptors
 * to render that material type
 */
struct MaterialInstance {
    MaterialPipeline *pipeline;
    VkDescriptorSet materialSet;
    MaterialPass passType;
};

/**
 * Small wrapper struct over MaterialInstance for readability
 */
struct GLTFMaterial {
    MaterialInstance data;
};

struct JVKEngine;

/**
 * GLTF 2.0 Metallic Roughness Material (Incomplete)
 *
 * Contains pipelines, pipeline layouts, and descriptor set layouts
 * for both material types. Can build and clear resources given an
 * engine pointer.
 *
 * Can instantiate a MaterialInstance class given the required resources.
 */
struct GLTFMetallicRoughness {
    MaterialPipeline opaquePipeline;
    MaterialPipeline transparentPipeline;
    VkDescriptorSetLayout materialDescriptorLayout;

    // To be written to UBO
    struct MaterialConstants {
        glm::vec4 colorFactors;
        glm::vec4 metallicRoughnessFactors;
        glm::vec4 extra[14];
    };

    struct MaterialResources {
        jvk::Image colorImage;
        VkSampler colorSampler;

        jvk::Image metallicRoughnessImage;
        VkSampler metallicRoughnessSampler;

        VkBuffer dataBuffer;
        uint32_t dataBufferOffset;
    };

    jvk::DescriptorWriter writer;

    void buildPipelines(JVKEngine *engine);
    void clearResources(VkDevice device) const;

    MaterialInstance writeMaterial(VkDevice device, MaterialPass pass, const MaterialResources &resources, jvk::DynamicDescriptorAllocator &descriptorAllocator);
};
