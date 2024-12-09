#pragma once

#include "jvk.hpp"
#include "vk/Buffer.hpp"
#include <filesystem>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <optional>


namespace jvk {

struct Vertex {
    glm::vec3 position;
    float uv_x;
    glm::vec3 normal;
    float uv_y;
    glm::vec4 color;
};

struct GPUMeshBuffers {
    Buffer indexBuffer;
    Buffer vertexBuffer;
    VkDeviceAddress vertexBufferAddress;

    void destroy(VmaAllocator allocator) const {
        indexBuffer.destroy(allocator);
        vertexBuffer.destroy(allocator);
    }
};

struct GPUDrawPushConstants {
    glm::mat4 worldMatrix;
    VkDeviceAddress vertexBufferAddress;
};

struct Surface {
    uint32_t startIndex;
    uint32_t count;
};

struct Mesh {
    std::string name;
    std::vector<Surface> surfaces;
    GPUMeshBuffers gpuBuffers;

    void destroy(VmaAllocator allocator) const {
        gpuBuffers.destroy(allocator);
    }
};

class Engine;

std::optional<std::vector<std::shared_ptr<Mesh>>> loadMeshes(Engine *engine, std::filesystem::path filePath);

}
