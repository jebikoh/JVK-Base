#include "engine.hpp"

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <iostream>
#include <ranges>
#include <vk_loader.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

constexpr bool JVK_OVERRIDE_COLORS_WITH_NORMAL_MAP = false;

VkFilter extractFilter(fastgltf::Filter filter) {
    switch (filter) {
        case fastgltf::Filter::Nearest:
        case fastgltf::Filter::NearestMipMapNearest:
        case fastgltf::Filter::NearestMipMapLinear:
            return VK_FILTER_NEAREST;
        case fastgltf::Filter::Linear:
        case fastgltf::Filter::LinearMipMapLinear:
        case fastgltf::Filter::LinearMipMapNearest:
        default:
            return VK_FILTER_LINEAR;
    }
}

VkSamplerMipmapMode extractMipMapMode(fastgltf::Filter filter) {
    switch (filter) {
        case fastgltf::Filter::NearestMipMapNearest:
        case fastgltf::Filter::LinearMipMapNearest:
            return VK_SAMPLER_MIPMAP_MODE_NEAREST;
        case fastgltf::Filter::LinearMipMapLinear:
        case fastgltf::Filter::NearestMipMapLinear:
        default:
            return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
}

void LoadedGLTF::draw(const glm::mat4 &topMatrix, DrawContext &ctx) {
    for (auto &n : topNodes) {
        n->draw(topMatrix, ctx);
    }
}

std::optional<std::shared_ptr<LoadedGLTF>> loadGLTF(JVKEngine *engine, std::filesystem::path filePath) {
    fmt::print("Loading GLTF mesh: {}", filePath.string());

    // SETUP
    std::shared_ptr<LoadedGLTF> scene = std::make_shared<LoadedGLTF>();
    scene->engine                     = engine;
    LoadedGLTF &file                  = *scene.get();

    constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::AllowDouble | fastgltf::Options::LoadGLBBuffers | fastgltf::Options::LoadExternalBuffers;
    fastgltf::Asset gltf;
    fastgltf::Parser parser;

    // LOAD DATA
    auto data = fastgltf::GltfDataBuffer::FromPath(filePath);
    if (!data) {
        fmt::print(stderr, "Failed to load GLTF file");
        return {};
    }

    // DETERMINE TYPE & PARSE
    const auto type = fastgltf::determineGltfFileType(data.get());
    if (type == fastgltf::GltfType::glTF) {
        auto load = parser.loadGltf(data.get(), filePath.parent_path(), gltfOptions);
        if (load) {
            gltf = std::move(load.get());
        } else {
            fmt::print(stderr, "Failed to parse glTF file");
            return {};
        }
    } else if (type == fastgltf::GltfType::GLB) {
        auto load = parser.loadGltfBinary(data.get(), filePath.parent_path(), gltfOptions);
        if (load) {
            gltf = std::move(load.get());
        } else {
            fmt::print(stderr, "Failed to parse glTF binary file");
            return {};
        }
    } else {
        fmt::print(stderr, "Failed to determine glTF type");
        return {};
    }

    // SETUP DESCRIPTOR POOLS
    std::vector<DynamicDescriptorAllocator::PoolSizeRatio> sizes = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3}};
    file.descriptorPool.init(engine->_device, gltf.materials.size(), sizes);

    // LOAD SAMPLERS
    for (fastgltf::Sampler &sampler: gltf.samplers) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType      = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.pNext      = nullptr;
        samplerInfo.maxLod     = VK_LOD_CLAMP_NONE;
        samplerInfo.minLod     = 0;
        samplerInfo.magFilter  = extractFilter(sampler.magFilter.value_or(fastgltf::Filter::Nearest));
        samplerInfo.minFilter  = extractFilter(sampler.minFilter.value_or(fastgltf::Filter::Nearest));
        samplerInfo.mipmapMode = extractMipMapMode(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

        VkSampler nSampler;
        vkCreateSampler(engine->_device, &samplerInfo, nullptr, &nSampler);
        file.samplers.push_back(nSampler);
    }

    // SETUP TEMPORARY ARRAYS
    std::vector<std::shared_ptr<MeshAsset>> meshes;
    std::vector<std::shared_ptr<Node>> nodes;
    std::vector<AllocatedImage> images;
    std::vector<std::shared_ptr<GLTFMaterial>> materials;

    // LOAD TEXTURES
    for (fastgltf::Image &image: gltf.images) {
        images.push_back(engine->_errorCheckerboardImage);
    }

    // LOAD MATERIALS
    file.materialDataBuffer                                          = engine->createBuffer(sizeof(GLTFMetallicRoughness::MaterialConstants) * gltf.materials.size(), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    int dataIndex                                                    = 0;
    GLTFMetallicRoughness::MaterialConstants *sceneMaterialConstants = static_cast<GLTFMetallicRoughness::MaterialConstants *>(file.materialDataBuffer.info.pMappedData);
    for (fastgltf::Material &mat: gltf.materials) {
        std::shared_ptr<GLTFMaterial> newMat = std::make_shared<GLTFMaterial>();
        materials.push_back(newMat);
        file.materials[mat.name.c_str()] = newMat;

        // MATERIAL CONSTANTS
        GLTFMetallicRoughness::MaterialConstants constants;
        constants.colorFactors.x             = mat.pbrData.baseColorFactor[0];
        constants.colorFactors.y             = mat.pbrData.baseColorFactor[1];
        constants.colorFactors.z             = mat.pbrData.baseColorFactor[2];
        constants.colorFactors.a             = mat.pbrData.baseColorFactor[3];
        constants.metallicRoughnessFactors.x = mat.pbrData.metallicFactor;
        constants.metallicRoughnessFactors.y = mat.pbrData.roughnessFactor;
        sceneMaterialConstants[dataIndex]    = constants;

        // MATERIAL PASS
        MaterialPass passType = MaterialPass::MAIN_COLOR;
        if (mat.alphaMode == fastgltf::AlphaMode::Blend) {
            passType = MaterialPass::TRANSPARENT;
        }

        // MATERIAL RESOURCES
        GLTFMetallicRoughness::MaterialResources matResources;
        // Default textures & samplers
        matResources.colorImage               = engine->_whiteImage;
        matResources.colorSampler             = engine->_defaultSamplerLinear;
        matResources.metallicRoughnessImage   = engine->_whiteImage;
        matResources.metallicRoughnessSampler = engine->_defaultSamplerLinear;

        // Uniform Buffer
        matResources.dataBuffer       = file.materialDataBuffer.buffer;
        matResources.dataBufferOffset = dataIndex * sizeof(GLTFMetallicRoughness::MaterialConstants);

        // Textures
        if (mat.pbrData.baseColorTexture.has_value()) {
            size_t img                = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].imageIndex.value();
            size_t sampler            = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].samplerIndex.value();
            matResources.colorImage   = images[img];
            matResources.colorSampler = file.samplers[sampler];
        }

        newMat->data = engine->_metallicRoughnessMaterial.writeMaterial(engine->_device, passType, matResources, file.descriptorPool);
        dataIndex++;
    }

    // LOAD MESHES
    std::vector<uint32_t> indices;
    std::vector<Vertex> vertices;
    for (fastgltf::Mesh &mesh : gltf.meshes) {
        std::shared_ptr<MeshAsset> newMesh = std::make_shared<MeshAsset>();
        meshes.push_back(newMesh);
        file.meshes[mesh.name.c_str()] = newMesh;
        newMesh->name = mesh.name;

        indices.clear();
        vertices.clear();

        for (auto &&p : mesh.primitives) {
            GeoSurface surface;
            surface.startIndex = static_cast<uint32_t>(indices.size());
            surface.count = static_cast<uint32_t>(gltf.accessors[p.indicesAccessor.value()].count);
            size_t initialVertex = vertices.size();

            // INDICES
            {
                fastgltf::Accessor &indexAccessor = gltf.accessors[p.indicesAccessor.value()];
                indices.reserve(indices.size() + indexAccessor.count);
                fastgltf::iterateAccessor<std::uint32_t>(gltf,
                                                         indexAccessor,
                                                         [&](const std::uint32_t idx) {
                                                             indices.push_back(idx + initialVertex);
                                                         });
            }

            // POSITIONS
            {
                auto &positionAccessor = gltf.accessors[p.findAttribute("POSITION")->accessorIndex];
                vertices.resize(vertices.size() + positionAccessor.count);
                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, positionAccessor,
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
                    auto &normalAccessor = gltf.accessors[normals->accessorIndex];
                    fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, normalAccessor,
                                                                  [&](glm::vec3 v, size_t index) {
                                                                      vertices[initialVertex + index].normal = v;
                                                                  });
                }
            }

            // UVs
            {
                auto uv = p.findAttribute("TEXCOORD_0");
                if (uv != p.attributes.end()) {
                    auto &uvAccessor = gltf.accessors[uv->accessorIndex];
                    fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, uvAccessor,
                                                                  [&](glm::vec2 v, size_t index) {
                                                                      vertices[initialVertex + index].uv_x = v.x;
                                                                      vertices[initialVertex + index].uv_y = v.y;
                                                                  });
                }
            }

            // COLORS
            {
                auto colors = p.findAttribute("COLOR_0");
                if (colors != p.attributes.end()) {
                    auto &colorAccessor = gltf.accessors[colors->accessorIndex];
                    fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, colorAccessor,
                                                                  [&](glm::vec4 v, size_t index) {
                                                                      vertices[initialVertex + index].color = v;
                                                                  });
                }
            }

            if (p.materialIndex.has_value()) {
                surface.material = materials[p.materialIndex.value()];
            } else {
                surface.material = materials[0];
            }

            newMesh->surfaces.push_back(surface);
        }

        newMesh->meshBuffers = engine->uploadMesh(indices, vertices);
    }

    // LOAD NODES
    for (fastgltf::Node &node : gltf.nodes) {
        std::shared_ptr<Node> newNode;

        // NODE TYPE
        if (node.meshIndex.has_value()) {
            newNode = std::make_shared<MeshNode>();
            static_cast<MeshNode *>(newNode.get())->mesh = meshes[*node.meshIndex];
        } else {
            newNode = std::make_shared<Node>();
        }

        nodes.push_back(newNode);
        file.nodes[node.name.c_str()];

        // NODE LOCAL TRANSFORM
        std::visit(fastgltf::visitor {
            [&](fastgltf::math::fmat4x4 matrix) {
                memcpy(&newNode->localTransform, matrix.data(), sizeof(matrix));
            },
            [&](fastgltf::TRS transform) {
                glm::vec3 tl(transform.translation[0], transform.translation[1], transform.translation[2]);
                glm::quat rot(transform.rotation[3], transform.rotation[0], transform.rotation[1], transform.rotation[2]);
                glm::vec3 sc(transform.scale[0], transform.scale[1], transform.scale[2]);

                glm::mat4 tm = glm::translate(glm::mat4(1.0f), tl);
                glm::mat4 rm = glm::toMat4(rot);
                glm::mat4 sm = glm::scale(glm::mat4(1.0f), sc);

                newNode->localTransform = tm * rm * sm;
            }
        }, node.transform);
    }

    // BUILD HIERARCHY
    for (int i = 0; i < gltf.nodes.size(); ++i) {
        fastgltf::Node &node = gltf.nodes[i];
        std::shared_ptr<Node> &sceneNode = nodes[i];

        for (auto &c: node.children) {
            sceneNode->children.push_back(nodes[c]);
            nodes[c]->parent = sceneNode;
        }
    }

    // TOP NODES
    for (auto &node : nodes) {
        if (node->parent.lock() == nullptr) {
            file.topNodes.push_back(node);
            node->refreshTransform(glm::mat4{1.0f});
        }
    }

    return scene;
}

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