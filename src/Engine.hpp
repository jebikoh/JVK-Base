#pragma once
#include "jvk.hpp"
#include "vk/Commands.hpp"
#include "vk/Context.hpp"
#include "vk/Fence.hpp"
#include "vk/Queue.hpp"
#include "vk/Semaphore.hpp"
#include "vk/Swapchain.hpp"
#include "vk/Allocator.hpp"
#include "vk/Image.hpp"
#include "vk/Descriptors.hpp"
#include "Mesh.hpp"

#include "DeletionStack.hpp"
#include "Immediate.hpp"

#include <SDL_vulkan.h>
#include <vk_mem_alloc.h>

namespace jvk {

constexpr unsigned int NUM_FRAMES = 2;

class Engine {
public:
    Context             context_;
    Swapchain           swapchain_;
    DeletionStack       globalDeletionQueue_;
    Allocator           allocator_;
    DescriptorAllocator descriptorAllocator_;

    bool stopRendering_ = false;
    bool isInit_        = false;

    VkExtent2D  windowExtent_{1280, 720};
    SDL_Window* window_;
    bool windowResize_;

    // Frame data
    struct FrameData {
        CommandPool     commandPool;
        VkCommandBuffer mainCommandBuffer;
        // Per-frame sync
        Semaphore       swapchainSemaphore;
        Semaphore       drawSemaphore;
        Fence           drawFence;
        // Per-frame deletion queue
        DeletionStack deletionQueue;
        // Descriptors
        DynamicDescriptorAllocator descriptors;
    };

    size_t    frameNumber_ = 0;
    FrameData frames_[NUM_FRAMES];
    FrameData &getCurrentFrame() { return frames_[frameNumber_ % NUM_FRAMES]; }

    Image drawImage_;
    Image depthImage_;
    VkExtent2D drawImageExtent_;
    VkDescriptorSet drawImageDescriptor_;
    VkDescriptorSetLayout drawImageDescriptorLayout_;

    VkExtent2D drawExtent_;
    float renderScale = 1.0f;

    Queue graphicsQueue_;
    ImmediateBuffer immediateBuffer_;

    struct MeshPipeline {
        VkPipelineLayout layout;
        VkPipeline       pipeline;
    } meshPipeline_;

    std::vector<std::shared_ptr<Mesh>> scene;

    struct GPUSceneData {
        glm::mat4 view;
        glm::mat4 proj;
        glm::mat4 viewProj;
        glm::vec4 ambientColor;
        glm::vec4 sunlightDirection;
        glm::vec4 sunlightColor;
    };
    GPUSceneData sceneData_;
    VkDescriptorSetLayout gpuSceneDataLayout_;

    void init();
    void destroy();
    void draw();
    void run();

    GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);
private:
    void initSDL();
    void initVulkan();
    void initImGUI();
    void initSwapchain();
    void resizeSwapchain();
    void initCommands();
    void initSyncStructures();
    void initDescriptors();

    void initPipelines();
    void initMeshPipeline();

    // Dummy mesh
    void initDummyData();

    // Background clear
    void drawBackground(VkCommandBuffer cmd) const;
    // ImGUI
    void drawUI(VkCommandBuffer cmd, VkImageView targetImageView) const;
    // Geometry
    void drawGeometry(VkCommandBuffer cmd);
};
}// namespace jvk
