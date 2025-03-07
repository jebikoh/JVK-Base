#pragma once
#include "fastgltf/types.hpp"
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

std::optional<std::vector<std::shared_ptr<MeshAsset>>> loadGltfMeshes(JVKEngine *engine, std::filesystem::path filePath);
