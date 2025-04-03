#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <iostream>
#include <ranges>
#include <scene.hpp>

#include <rapidobj.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/quaternion.hpp>

#include <stb_image.h>

#include <engine.hpp>
#include <jvk.hpp>

constexpr bool JVK_OVERRIDE_COLORS_WITH_NORMAL_MAP = false;

#ifdef JVK_LOADER_GENERATE_MIPMAPS
constexpr bool JVK_GENERATE_MIPMAPS = true;
#else
constexpr bool JVK_GENERATE_MIPMAPS = false;
#endif

void MeshNode::draw(const glm::mat4 &topMatrix, DrawContext &ctx) {
    glm::mat4 nodeMatrix = topMatrix * worldTransform;

    for (auto &s: mesh->surfaces) {
        RenderObject rObj{};
        rObj.indexCount  = s.count;
        rObj.firstIndex  = s.startIndex;
        rObj.indexBuffer = mesh->meshBuffers.indexBuffer.buffer;
        rObj.material    = s.material;

        rObj.transform           = nodeMatrix;
        rObj.nTransform          = glm::inverseTranspose(nodeMatrix);
        rObj.vertexBufferAddress = mesh->meshBuffers.vertexBufferAddress;

        if (rObj.material->passType == MaterialPass::TRANSPARENT_PASS) {
            ctx.transparentSurfaces.push_back(rObj);
        } else {
            ctx.opaqueSurfaces.push_back(rObj);
        }
    }

    Node::draw(topMatrix, ctx);
}

std::optional<jvk::Image> loadImage(JVKEngine *engine, fastgltf::Asset &asset, fastgltf::Image &image) {
    jvk::Image newImage{};

    // TOP 10 C++ FEATURES I HATE
    std::visit(fastgltf::visitor{
                       [](auto &arg) {},
                       [&](fastgltf::sources::URI &filePath) {
                           assert(filePath.fileByteOffset == 0);// We don't support offsets with stbi.
                           assert(filePath.uri.isLocalPath());  // We're only capable of loading local files.
                           int width, height, nrChannels;

                           const std::string path(filePath.uri.path().begin(), filePath.uri.path().end());// Thanks C++.
                           unsigned char *data = stbi_load(path.c_str(), &width, &height, &nrChannels, 4);

                           if (data) {
                               VkExtent3D imageSize;
                               imageSize.width  = width;
                               imageSize.height = height;
                               imageSize.depth  = 1;

                               newImage = engine->createImage(data, imageSize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, JVK_LOADER_GENERATE_MIPMAPS);
                               stbi_image_free(data);
                           }
                       },
                       [&](fastgltf::sources::Array &vector) {
                           int width, height, nrChannels;
                           unsigned char *data = stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(vector.bytes.data()), static_cast<int>(vector.bytes.size()), &width, &height, &nrChannels, 4);
                           if (data) {
                               VkExtent3D imageSize;
                               imageSize.width  = width;
                               imageSize.height = height;
                               imageSize.depth  = 1;

                               newImage = engine->createImage(data, imageSize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, JVK_LOADER_GENERATE_MIPMAPS);
                               stbi_image_free(data);
                           }
                       },
                       [&](fastgltf::sources::BufferView &view) {
                           auto &bufferView = asset.bufferViews[view.bufferViewIndex];
                           auto &buffer     = asset.buffers[bufferView.bufferIndex];
                           std::visit(fastgltf::visitor{
                                              [](auto &arg) {},
                                              [&](fastgltf::sources::Array &vector) {
                                                  int width, height, nrChannels;
                                                  unsigned char *data = stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(vector.bytes.data() + bufferView.byteOffset),
                                                                                              static_cast<int>(bufferView.byteLength), &width, &height, &nrChannels, 4);
                                                  if (data) {
                                                      VkExtent3D imageSize;
                                                      imageSize.width  = width;
                                                      imageSize.height = height;
                                                      imageSize.depth  = 1;

                                                      newImage = engine->createImage(data, imageSize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, JVK_LOADER_GENERATE_MIPMAPS);
                                                      stbi_image_free(data);
                                                  }
                                              }},
                                      buffer.data);
                       },
               },
               image.data);


    if (newImage.image == VK_NULL_HANDLE) {
        return {};
    }
    return newImage;
}

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

void Scene::draw(const glm::mat4 &topMatrix, DrawContext &ctx) {
    for (auto &n: topNodes) {
        n->draw(topMatrix, ctx);
    }
}

void Scene::destroy() {
    VkDevice device = engine->ctx_;

    descriptorPool.destroyPools(device);
    engine->destroyBuffer(materialDataBuffer);

    for (auto &[k, v]: meshes) {
        engine->destroyBuffer(v->meshBuffers.indexBuffer);
        engine->destroyBuffer(v->meshBuffers.vertexBuffer);
    }

    for (auto &image: images) {
        if (image.image == engine->errorCheckerboardImage_.image) continue;
        image.destroy(device, engine->allocator_);
    }

    for (const auto &sampler: samplers) {
        vkDestroySampler(device, sampler, nullptr);
    }
}

std::optional<std::shared_ptr<Scene>> loadGLTF(JVKEngine *engine, const std::filesystem::path &filePath) {
    LOG_INFO("Loading GLTF: {}", filePath.string());

    // SETUP
    std::shared_ptr<Scene> scene = std::make_shared<Scene>();
    scene->engine                = engine;
    Scene &file                  = *scene;

    constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::AllowDouble | fastgltf::Options::LoadExternalBuffers;
    fastgltf::Asset gltf;
    fastgltf::Parser parser;

    // LOAD DATA
    auto data = fastgltf::GltfDataBuffer::FromPath(filePath);
    if (!data) {
        LOG_ERROR("Failed to load GLTF file: {}", filePath.string());
        return {};
    }

    // DETERMINE TYPE & PARSE
    const auto type = fastgltf::determineGltfFileType(data.get());
    if (type == fastgltf::GltfType::glTF) {
        auto load = parser.loadGltf(data.get(), filePath.parent_path(), gltfOptions);
        if (load) {
            gltf = std::move(load.get());
        } else {
            LOG_ERROR("Failed to parse GLTF file: {}", filePath.string());
            return {};
        }
    } else if (type == fastgltf::GltfType::GLB) {
        auto load = parser.loadGltfBinary(data.get(), filePath.parent_path(), gltfOptions);
        if (load) {
            gltf = std::move(load.get());
        } else {
            LOG_ERROR("Failed to parse GLTF binary file: {}", filePath.string());
            return {};
        }
    } else {
        LOG_ERROR("Failed to determine GLTF type: {}", filePath.string());
        return {};
    }

    // SETUP DESCRIPTOR POOLS
    std::vector<jvk::DynamicDescriptorAllocator::PoolSizeRatio> sizes = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3}};
    file.descriptorPool.init(engine->ctx_, gltf.materials.size(), sizes);

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
        vkCreateSampler(engine->ctx_, &samplerInfo, nullptr, &nSampler);
        file.samplers.push_back(nSampler);
    }

    // SETUP TEMPORARY ARRAYS
    std::vector<std::shared_ptr<MeshAsset>> meshes;
    std::vector<std::shared_ptr<Node>> nodes;

    // LOAD TEXTURES
    int textureIndex = 0;
    for (fastgltf::Image &image: gltf.images) {
        std::optional<jvk::Image> img = loadImage(engine, gltf, image);
        std::string imgName;
        if (image.name.empty()) {
            imgName = "texture_" + std::to_string(textureIndex);
        } else {
            imgName = image.name;
        }

        if (img.has_value()) {
            file.images.push_back(*img);
            file.imageMap[imgName] = file.images.size() - 1;
            LOG_INFO("Texture image loaded: {}", imgName);
        } else {
            file.images.push_back(engine->errorCheckerboardImage_);
            LOG_ERROR("GLTF failed to load texture: {}", imgName);
        }
        textureIndex++;
    }

    // LOAD MATERIALS
    file.materialDataBuffer      = engine->createBuffer(sizeof(Material::MaterialConstants) * gltf.materials.size(), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    int dataIndex                = 0;
    auto *sceneMaterialConstants = static_cast<Material::MaterialConstants *>(file.materialDataBuffer.info.pMappedData);
    for (fastgltf::Material &mat: gltf.materials) {
        // MATERIAL CONSTANTS
        Material::MaterialConstants constants{};
        constants.colorFactors.x             = mat.pbrData.baseColorFactor[0];
        constants.colorFactors.y             = mat.pbrData.baseColorFactor[1];
        constants.colorFactors.z             = mat.pbrData.baseColorFactor[2];
        constants.colorFactors.a             = mat.pbrData.baseColorFactor[3];
        constants.metallicRoughnessFactors.x = mat.pbrData.metallicFactor;
        constants.metallicRoughnessFactors.y = mat.pbrData.roughnessFactor;
        sceneMaterialConstants[dataIndex]    = constants;

        // MATERIAL PASS
        MaterialPass passType = MaterialPass::MAIN_COLOR;

#ifdef JVK_USE_GLTF_ALPHA_MODE
        if (mat.alphaMode == fastgltf::AlphaMode::Blend) {
            passType = MaterialPass::TRANSPARENT_PASS;
        }
#endif

        // MATERIAL RESOURCES
        Material::MaterialResources matResources{};
        // Default textures & samplers
        matResources.colorImage               = engine->whiteImage_;
        matResources.colorSampler             = engine->defaultSamplerLinear_;
        matResources.metallicRoughnessImage   = engine->whiteImage_;
        matResources.metallicRoughnessSampler = engine->defaultSamplerLinear_;

        // Uniform Buffer
        matResources.dataBuffer       = file.materialDataBuffer.buffer;
        matResources.dataBufferOffset = dataIndex * sizeof(Material::MaterialConstants);

        // Textures
        if (mat.pbrData.baseColorTexture.has_value()) {
            size_t img                = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].imageIndex.value();
            size_t sampler            = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].samplerIndex.value();
            matResources.colorImage   = file.images[img];
            matResources.colorSampler = file.samplers[sampler];
        }

        MaterialInstance newMat = engine->metallicRoughnessMaterial_.writeMaterial(engine->ctx_, passType, matResources, file.descriptorPool);
        file.materials.push_back(newMat);
        file.materialMap[mat.name.c_str()] = file.materials.size() - 1;

        dataIndex++;
    }

    // LOAD MESHES
    std::vector<uint32_t> indices;
    std::vector<Vertex> vertices;
    for (fastgltf::Mesh &mesh: gltf.meshes) {
        std::shared_ptr<MeshAsset> newMesh = std::make_shared<MeshAsset>();
        meshes.push_back(newMesh);
        file.meshes[mesh.name.c_str()] = newMesh;
        newMesh->name                  = mesh.name;

        indices.clear();
        vertices.clear();

        for (auto &&p: mesh.primitives) {
            Surface surface{};
            surface.startIndex   = static_cast<uint32_t>(indices.size());
            surface.count        = static_cast<uint32_t>(gltf.accessors[p.indicesAccessor.value()].count);
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
                                                                  Vertex newVertex{};
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
                surface.material = &file.materials[p.materialIndex.value()];
            } else {
                surface.material = &file.materials[0];
            }

            newMesh->surfaces.push_back(surface);
        }

        newMesh->meshBuffers = engine->uploadMesh(indices, vertices);
    }

    // LOAD NODES
    for (fastgltf::Node &node: gltf.nodes) {
        std::shared_ptr<Node> newNode;

        // NODE TYPE
        if (node.meshIndex.has_value()) {
            newNode                                       = std::make_shared<MeshNode>();
            dynamic_cast<MeshNode *>(newNode.get())->mesh = meshes[*node.meshIndex];
        } else {
            newNode = std::make_shared<Node>();
        }

        nodes.push_back(newNode);
        file.nodes[node.name.c_str()];

        // NODE LOCAL TRANSFORM
        std::visit(fastgltf::visitor{
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
                           }},
                   node.transform);
    }

    // BUILD HIERARCHY
    for (int i = 0; i < gltf.nodes.size(); ++i) {
        fastgltf::Node &node             = gltf.nodes[i];
        std::shared_ptr<Node> &sceneNode = nodes[i];

        for (auto &c: node.children) {
            sceneNode->children.push_back(nodes[c]);
            nodes[c]->parent = sceneNode;
        }
    }

    // TOP NODES
    for (auto &node: nodes) {
        if (node->parent.lock() == nullptr) {
            file.topNodes.push_back(node);
            node->refreshTransform(glm::mat4{1.0f});
        }
    }

    LOG_INFO("Loaded GLTF file: {}", filePath.string());
    return scene;
}

std::optional<std::shared_ptr<Scene>> loadOBJ(JVKEngine *engine, const std::filesystem::path &filePath) {
    LOG_INFO("Loading OBJ: {}", filePath.string());

    auto scene    = std::make_shared<Scene>();
    scene->engine = engine;
    Scene &file   = *scene;

    const std::string sPath = filePath.string();
    const size_t lastSlash = sPath.find_last_of("/\\");
    std::string baseDir    = (lastSlash != std::string::npos) ? sPath.substr(0, lastSlash + 1) : "";

    // PARSE DATA
    rapidobj::Result result = rapidobj::ParseFile(filePath);
    if (result.error) {
        LOG_ERROR("{}\n", result.error.code.message());
        return {};
    }

    // TRIANGULATE
    if (const bool success = rapidobj::Triangulate(result); !success) {
        LOG_ERROR("{}\n", result.error.code.message());
        return {};
    }

    // SETUP DESCRIPTOR POOLS
    std::vector<jvk::DynamicDescriptorAllocator::PoolSizeRatio> sizes = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3}};
    file.descriptorPool.init(engine->ctx_, result.materials.size(), sizes);

    std::vector<std::shared_ptr<MeshAsset>> meshes;
    std::vector<std::shared_ptr<Node>> nodes;

    // MATERIALS & TEXTURES
    file.materialDataBuffer      = engine->createBuffer(sizeof(Material::MaterialConstants) * result.materials.size(), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    int dataIndex                = 0;
    int textureIndex = 0;
    auto *sceneMaterialConstants = static_cast<Material::MaterialConstants *>(file.materialDataBuffer.info.pMappedData);

    auto loadTexture = [&](const std::string &texName, jvk::Image &outTex) {
        if (texName.length() > 0) {
            // Check if we already loaded this texture before
            if (file.imageMap.contains(texName)) {
                const size_t img = file.imageMap[texName];
                outTex = file.images[img];
            } else {
                // Otherwise, load it for the first time
                if (const auto img = loadImage(engine, baseDir + texName); img.has_value()) {
                    file.images.push_back(*img);
                    file.imageMap[texName] = file.images.size() - 1;
                    outTex = file.images.back();
                    LOG_INFO("Texture image loaded: {}", texName);
                } else {
                    file.images.push_back(engine->errorCheckerboardImage_);
                    outTex = file.images.back();
                    LOG_ERROR("Failed to load texture: {}", texName);
                }
                textureIndex++;
            }
        }
    };

    for (int i = 0; i < result.materials.size(); ++i) {
        const auto &mat = result.materials[i];

        // MATERIAL CONSTANTS
        Material::MaterialConstants constants{};
        const auto ambient  = mat.ambient.data();
        const auto diffuse  = mat.diffuse.data();
        const auto specular = mat.specular.data();

        constants.ambient.r = ambient[0];
        constants.ambient.g = ambient[1];
        constants.ambient.b = ambient[2];

        constants.diffuse.r = diffuse[0];
        constants.diffuse.g = diffuse[1];
        constants.diffuse.b = diffuse[2];
        constants.diffuse.a = diffuse[3];

        constants.specular.r = specular[0];
        constants.specular.g = specular[1];
        constants.specular.b = specular[2];

        constants.shininess = 32.0f;

        sceneMaterialConstants[dataIndex] = constants;

        // MATERIAL PASS
        constexpr MaterialPass passType = MaterialPass::MAIN_COLOR;

        // MATERIAL RESOURCES
        Material::MaterialResources matResources{};

        // Default texture and samplers
        matResources.colorImage               = engine->whiteImage_;
        matResources.metallicRoughnessImage   = engine->blackImage_;
        matResources.ambientImage             = engine->blackImage_;
        matResources.diffuseImage             = engine->whiteImage_;
        matResources.specularImage            = engine->blackImage_;
        matResources.specularSampler          = engine->defaultSamplerLinear_;
        matResources.diffuseSampler           = engine->defaultSamplerLinear_;
        matResources.ambientSampler           = engine->defaultSamplerLinear_;
        matResources.colorSampler             = engine->defaultSamplerLinear_;
        matResources.metallicRoughnessSampler = engine->defaultSamplerLinear_;

        // UBO
        matResources.dataBuffer = file.materialDataBuffer.buffer;
        matResources.dataBufferOffset = dataIndex * sizeof(Material::MaterialConstants);

        // TEXTURES
        loadTexture(mat.ambient_texname, matResources.ambientImage);
        loadTexture(mat.diffuse_texname, matResources.diffuseImage);
        loadTexture(mat.specular_texname, matResources.specularImage);

        MaterialInstance matInstance = engine->metallicRoughnessMaterial_.writeMaterial(engine->ctx_, passType, matResources, file.descriptorPool);
        file.materials.push_back(matInstance);
        file.materialMap[mat.name.c_str()] = file.materials.size() - 1;

        dataIndex++;
    }

    // LOAD MESH
    std::vector<uint32_t> indices;
    std::vector<Vertex> vertices;

    const auto &objPositions = result.attributes.positions;
    const auto &objNormals   = result.attributes.normals;
    const auto &objUVs = result.attributes.texcoords;

    for (const auto &shape : result.shapes) {
        // Shape == Mesh
        auto newMesh = std::make_shared<MeshAsset>();
        newMesh->name = shape.name;

        indices.clear();
        vertices.clear();

        const auto &mesh = shape.mesh;
        for (const auto &idx : mesh.indices) {
            Vertex vertex{};

            // POSITION
            const int posIdx = idx.position_index;
            vertex.position = {
            objPositions[3 * posIdx],
            objPositions[3 * posIdx + 1],
            objPositions[3 * posIdx + 2]
            };

            // NORMALS
            if (!objNormals.empty() && idx.normal_index >= 0) {
                vertex.normal = {
                objNormals[3 * idx.normal_index],
                objNormals[3 * idx.normal_index + 1],
                objNormals[3 * idx.normal_index + 2]
                };
            } else {
                vertex.normal = {};
            }

            // UVs
            if (!objUVs.empty() && idx.texcoord_index >= 0) {
                vertex.uv_x = objUVs[2 * idx.texcoord_index];
                vertex.uv_y = objUVs[2 * idx.texcoord_index + 1];
            } else {
                vertex.uv_x = 0.0f;
                vertex.uv_y = 0.0f;
            }

            // COLORS
            vertex.color = glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
            vertices.push_back(vertex);
            indices.push_back(static_cast<uint32_t>(vertices.size() - 1));
        }

        Surface surface{};
        surface.startIndex = 0;
        surface.count = static_cast<uint32_t>(mesh.indices.size());

        // A little jank, but we just use the first material_id given and leave it to the user to
        // properly format their OBJs.
        if (mesh.material_ids.size() > 0) {
            const auto &matName = result.materials[mesh.material_ids[0]].name;
            surface.material = &file.materials[file.materialMap[matName]];
        } else {
            surface.material = &file.materials[0];
        }
        newMesh->surfaces.push_back(surface);
        newMesh->meshBuffers = engine->uploadMesh(indices, vertices);

        file.meshes[newMesh->name] = newMesh;

        // Top-level mesh node
        std::shared_ptr<MeshNode> newNode = std::make_shared<MeshNode>();
        newNode->mesh = newMesh;
        newNode->localTransform = glm::mat4(1.0f);
        newNode->worldTransform = glm::mat4(1.0f);
        file.topNodes.push_back(newNode);
    }

    LOG_INFO("Loaded OBJ file: {}", filePath.string());
    return scene;
}
