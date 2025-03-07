#pragma once
#include <jvk.hpp>

struct AllocatedImage {
    VkImage image;
    VkImageView imageView;
    VmaAllocation allocation;
    VkExtent3D imageExtent;
    VkFormat imageFormat;
};

struct AllocatedBuffer {
    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo info;
};

struct Vertex {
    glm::vec3 position;
    float uv_x;
    glm::vec3 normal;
    float uv_y;
    glm::vec4 color;
};

struct GPUMeshBuffers {
    AllocatedBuffer indexBuffer;
    AllocatedBuffer vertexBuffer;
    VkDeviceAddress vertexBufferAddress;
};

// GLOBAL PUSH CONSTANTS/DESCRIPTOR DATA
struct GPUDrawPushConstants {
    glm::mat4 worldMatrix;
    VkDeviceAddress vertexBuffer;
};

struct GPUSceneData {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 viewProj;
    glm::vec4 ambientColor;
    glm::vec4 sunlightDirection;
    glm::vec4 sunlightColor;
};

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
