#pragma once
#include "fastgltf/types.hpp"
#include "material.hpp"

#include <filesystem>
#include <jvk.hpp>
#include <jvk/descriptor.hpp>

namespace jvk {
struct Image;
}

class JVKEngine;

struct Vertex {
    glm::vec3 position;
    float uv_x;
    glm::vec3 normal;
    float uv_y;
    glm::vec4 color;
};

struct GPUMeshBuffers {
    jvk::Buffer indexBuffer;
    jvk::Buffer vertexBuffer;
    VkDeviceAddress vertexBufferAddress;
};

struct GPUSceneData {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 viewProj;
    glm::vec4 ambientColor;
    glm::vec4 sunlightDirection;
    glm::vec4 sunlightColor;
};

// GLOBAL PUSH CONSTANTS/DESCRIPTOR DATA
struct GPUDrawPushConstants {
    glm::mat4 worldMatrix;
    VkDeviceAddress vertexBuffer;
};

struct GeoSurface {
    uint32_t startIndex;
    uint32_t count;
    std::shared_ptr<GLTFMaterial> material;
};

struct MeshAsset {
    std::string name;

    std::vector<GeoSurface> surfaces;
    GPUMeshBuffers meshBuffers;
};

struct DrawContext;

class IRenderable {
    virtual void draw(const glm::mat4 &topMatrix, DrawContext &ctx) = 0;
};

struct Node : public IRenderable {
    std::weak_ptr<Node> parent;
    std::vector<std::shared_ptr<Node>> children;

    glm::mat4 localTransform;
    glm::mat4 worldTransform;

    void refreshTransform(const glm::mat4 &parentMatrix) {
        worldTransform = parentMatrix * localTransform;
        for (const auto &child : children) {
            child->refreshTransform(worldTransform);
        }
    }

    virtual void draw(const glm::mat4 &topMatrix, DrawContext &ctx) override {
        for (const auto &child : children) {
            child->draw(topMatrix, ctx);
        }
    }
};

struct LoadedGLTF : public IRenderable {
    std::unordered_map<std::string, std::shared_ptr<MeshAsset>> meshes;
    std::unordered_map<std::string, std::shared_ptr<Node>> nodes;
    std::unordered_map<std::string, jvk::Image> images;
    std::unordered_map<std::string, std::shared_ptr<GLTFMaterial>> materials;

    std::vector<std::shared_ptr<Node>> topNodes;
    std::vector<VkSampler> samplers;

    jvk::DynamicDescriptorAllocator descriptorPool;

    jvk::Buffer materialDataBuffer;

    JVKEngine *engine;

    ~LoadedGLTF() { destroy(); };

    virtual void draw(const glm::mat4 &topMatrix, DrawContext &ctx);

private:
    void destroy();
};

std::optional<std::shared_ptr<LoadedGLTF>> loadGLTF(JVKEngine *engine, std::filesystem::path filePath);