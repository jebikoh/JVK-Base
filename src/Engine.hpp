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
    };

    size_t    frameNumber_ = 0;
    FrameData frames_[NUM_FRAMES];
    FrameData &getCurrentFrame() { return frames_[frameNumber_ % NUM_FRAMES]; }

    Image drawImage_;
    VkExtent2D drawExtent_;
    VkDescriptorSet drawImageDescriptor_;
    VkDescriptorSetLayout drawImageDescriptorLayout_;

    Queue graphicsQueue_;
    ImmediateBuffer immediateBuffer_;

    struct TrianglePipeline {
        VkPipelineLayout layout;
        VkPipeline       pipeline;
    } trianglePipeline_;

    // Mesh pipeline
    struct MeshPipeline {
        VkPipelineLayout layout;
        VkPipeline       pipeline;
    } meshPipeline_;
    GPUMeshBuffers rectangle;

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
    void initCommands();
    void initSyncStructures();
    void initDescriptors();

    void initPipelines();
    void initTrianglePipeline();
    void initMeshPipeline();

    // Dummy mesh
    void initRectangle();

    // Flashing background
    void drawBackground(VkCommandBuffer cmd) const;
    // ImGUI
    void drawUI(VkCommandBuffer cmd, VkImageView targetImageView) const;
    // Geometry
    void drawGeometry(VkCommandBuffer cmd) const;
};
}// namespace jvk
