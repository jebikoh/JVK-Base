#include "engine.hpp"


#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <vk_loader.hpp>
#include <iostream>

constexpr bool JVK_OVERRIDE_COLORS_WITH_NORMAL_MAP = true;

std::optional<std::vector<std::shared_ptr<MeshAsset>>> loadGltfMeshes(JVKEngine *engine, std::filesystem::path filePath) {
    fmt::print("Loading GLTF mesh: {}", filePath.string());

    auto data = fastgltf::GltfDataBuffer::FromPath(filePath);
    if (!data) {
        fmt::print(stderr, "Failed to load GLTF file");
        return {};
    }

    constexpr auto gltfOptions = fastgltf::Options::LoadExternalBuffers;
    fastgltf::Asset asset;
    fastgltf::Parser parser;

    auto load = parser.loadGltfBinary(data.get(), filePath.parent_path(), gltfOptions);
    if (load) {
        asset = std::move(load.get());
    } else {
        fmt::print(stderr, "Failed to parse glTF file");
        return {};
    }

    std::vector<std::shared_ptr<MeshAsset>> meshes;

    std::vector<uint32_t> indices;
    std::vector<Vertex> vertices;
    for (fastgltf::Mesh &mesh: asset.meshes) {
        MeshAsset newMesh;
        newMesh.name = mesh.name;

        indices.clear();
        vertices.clear();

        for (auto &&p: mesh.primitives) {
            GeoSurface newSurface;
            newSurface.startIndex = static_cast<uint32_t>(indices.size());
            newSurface.count      = static_cast<uint32_t>(asset.accessors[p.indicesAccessor.value()].count);

            size_t initialVertex = vertices.size();
            // Indices
            {
                fastgltf::Accessor &indexAccessor = asset.accessors[p.indicesAccessor.value()];
                indices.reserve(indices.size() + indexAccessor.count);
                fastgltf::iterateAccessor<std::uint32_t>(asset,
                                                         indexAccessor,
                                                         [&](const std::uint32_t idx) {
                                                             indices.push_back(idx + initialVertex);
                                                         });
            }

            // Positions
            {
                auto &positionAccessor = asset.accessors[p.findAttribute("POSITION")->accessorIndex];
                vertices.resize(vertices.size() + positionAccessor.count);
                fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, positionAccessor,
                                                              [&](glm::vec3 v, size_t index) {
                                                                  Vertex newVertex;
                                                                  newVertex.position              = v;
                                                                  newVertex.normal                = {1, 0, 0};
                                                                  newVertex.color                 = glm::vec4{1.f};
                                                                  newVertex.uv_x                  = 0;
                                                                  newVertex.uv_y                  = 0;
                                                                  vertices[initialVertex + index] = newVertex;
                                                              });
            }
            // NORMALS
            {
                auto normals = p.findAttribute("NORMAL");
                if (normals != p.attributes.end()) {
                    auto &normalAccessor = asset.accessors[normals->accessorIndex];
                    fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, normalAccessor,
                                                                  [&](glm::vec3 v, size_t index) {
                                                                      vertices[initialVertex + index].normal = v;
                                                                  });
                }
            }
            // UVs
            {
                auto uv = p.findAttribute("TEXCOORD_0");
                if (uv != p.attributes.end()) {
                    auto &uvAccessor = asset.accessors[uv->accessorIndex];
                    fastgltf::iterateAccessorWithIndex<glm::vec2>(asset, uvAccessor,
                                                                  [&](glm::vec2 v, size_t index) {
                                                                      vertices[initialVertex + index].uv_x = v.x;
                                                                      vertices[initialVertex + index].uv_y = v.y;
                                                                  });
                }
            }
            // Colors
            {
                auto colors = p.findAttribute("COLOR_0");
                if (colors != p.attributes.end()) {
                    auto &colorAccessor = asset.accessors[colors->accessorIndex];
                    fastgltf::iterateAccessorWithIndex<glm::vec4>(asset, colorAccessor,
                                                                  [&](glm::vec4 v, size_t index) {
                                                                      vertices[initialVertex + index].color = v;
                                                                  });
                }
            }
            newMesh.surfaces.push_back(newSurface);
        }

        if (JVK_OVERRIDE_COLORS_WITH_NORMAL_MAP) {
            for (Vertex &v: vertices) {
                v.color = glm::vec4(v.normal, 1.0f);
            }
        }

        newMesh.meshBuffers = engine->uploadMesh(indices, vertices);
        meshes.emplace_back(std::make_shared<MeshAsset>(std::move(newMesh)));
    }

    return meshes;
}