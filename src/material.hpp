#pragma once
#include <jvk.hpp>
#include <jvk/descriptor.hpp>
#include <jvk/image.hpp>
#include <jvk/sampler.hpp>
#include <jvk/pipeline.hpp>

// MATERIALS

/**
 * Determines what pipeline and descriptors to bind
 */
enum class MaterialPass : uint8_t {
    MAIN_COLOR,
    TRANSPARENT_PASS,
    OTHER
};

/**
 * A single instance of a material, containing the required pipeline and descriptors
 * to render that material type
 */
struct MaterialInstance {
    jvk::Pipeline *pipeline;
    VkDescriptorSet materialSet;
    MaterialPass passType;
};

class JVKEngine;

/**
 * GLTF 2.0 Metallic Roughness Material (Incomplete)
 *
 * Contains pipelines, pipeline layouts, and descriptor set layouts
 * for both material types. Can build and clear resources given an
 * engine pointer.
 *
 * Can instantiate a MaterialInstance class given the required resources.
 */
struct Material {
    jvk::Pipeline opaquePipeline;
    jvk::Pipeline transparentPipeline;
    VkDescriptorSetLayout materialDescriptorLayout = VK_NULL_HANDLE;

    // To be written to UBO
    struct alignas(256) MaterialConstants {
        // PBR
        glm::vec4 colorFactors;
        glm::vec4 metallicRoughnessFactors;
        // BLINN-PHONG
        glm::vec4 ambient;
        glm::vec4 diffuse;
        glm::vec3 specular;
        float shininess;
    };

    struct MaterialResources {
        jvk::Image colorImage;
        VkSampler colorSampler;

        jvk::Image metallicRoughnessImage;
        VkSampler metallicRoughnessSampler;

        jvk::Image ambientImage;
        VkSampler ambientSampler;

        jvk::Image diffuseImage;
        jvk::Sampler diffuseSampler;

        jvk::Image specularImage;
        VkSampler specularSampler;

        VkBuffer dataBuffer;
        uint32_t dataBufferOffset;
    };

    jvk::DescriptorWriter writer;

    void buildPipelines(JVKEngine *engine);
    void clearResources(VkDevice device) const;

    MaterialInstance writeMaterial(VkDevice device, MaterialPass pass, const MaterialResources &resources, jvk::DynamicDescriptorAllocator &descriptorAllocator);
};
