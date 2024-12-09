#pragma once

#include "jvk.hpp"
#include "vk/Buffer.hpp"
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>


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

}

