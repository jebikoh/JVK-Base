#pragma once
#include "fastgltf/types.hpp"
#include "vk_descriptors.hpp"
#include "vk_types.hpp"

#include <filesystem>
#include <jvk.hpp>

struct GLTFMaterial {
    MaterialInstance data;
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

class JVKEngine;

struct LoadedGLTF : public IRenderable {
    std::unordered_map<std::string, std::shared_ptr<MeshAsset>> meshes;
    std::unordered_map<std::string, std::shared_ptr<Node>> nodes;
    std::unordered_map<std::string, AllocatedImage> images;
    std::unordered_map<std::string, std::shared_ptr<GLTFMaterial>> materials;

    std::vector<std::shared_ptr<Node>> topNodes;
    std::vector<VkSampler> samplers;

    DynamicDescriptorAllocator descriptorPool;

    AllocatedBuffer materialDataBuffer;

    JVKEngine *engine;

    ~LoadedGLTF() { destroy(); };

    virtual void draw(const glm::mat4 &topMatrix, DrawContext &ctx);

private:
    void destroy() {};
};

std::optional<std::shared_ptr<LoadedGLTF>> loadGLTF(JVKEngine *engine, std::filesystem::path filePath);
std::optional<std::vector<std::shared_ptr<MeshAsset>>> loadGltfMeshes(JVKEngine *engine, std::filesystem::path filePath);
