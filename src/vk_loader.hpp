#pragma once
#include "vk_types.hpp"
#include <jvk.hpp>
#include <filesystem>

struct GeoSurface {
    uint32_t startIndex;
    uint32_t count;
};

struct MeshAsset {
    std::string name;

    std::vector<GeoSurface> surfaces;
    GPUMeshBuffers meshBuffers;
};

class JVKEngine;

std::optional<std::vector<std::shared_ptr<MeshAsset>>> loadGltfMeshes(JVKEngine *engine, std::filesystem::path filePath);
