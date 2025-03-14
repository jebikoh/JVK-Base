#pragma once

#include <camera.hpp>
#include <material.hpp>
#include <stack>

#include <jvk.hpp>
#include <immediate.hpp>
#include <mesh.hpp>

#include <jvk/commands.hpp>
#include <jvk/context.hpp>
#include <jvk/fence.hpp>
#include <jvk/image.hpp>
#include <jvk/queue.hpp>
#include <jvk/semaphore.hpp>
#include <jvk/swapchain.hpp>
#include <jvk/descriptor.hpp>
#include <jvk/sampler.hpp>
#include <jvk/buffer.hpp>

struct ComputePushConstants {
    glm::vec4 data1;
    glm::vec4 data2;
    glm::vec4 data3;
    glm::vec4 data4;
};

struct ComputeEffect {
    const char *name;
    VkPipeline pipeline;
    VkPipelineLayout layout;
    ComputePushConstants data;
};

struct FrameData {
    // FRAME COMMANDS
    jvk::CommandPool cmdPool;
    jvk::CommandBuffer cmdBuffer;

    // FRAME SYNC
    // Semaphores:
    //  1. To have render commands wait on swapchain image request
    //  2. Control presentation of rendered image to OS after draw
    // Fences:
    //  1. Wait for draw commands of a submitted cmd buffer to be finished
    jvk::Semaphore swapchainSemaphore;
    jvk::Semaphore renderSemaphore;
    jvk::Fence renderFence;

    // GLOBAL FRAME SCENE DATA
    jvk::Buffer sceneDataBuffer;
    VkDescriptorSet sceneDataDescriptorSet;

    jvk::DynamicDescriptorAllocator descriptorAllocator;
};

constexpr unsigned int JVK_NUM_FRAMES = 2;

class JVKEngine {
public:
    bool isInitialized_ = false;
    int frameNumber_    = 0;
    bool stopRendering_ = false;
    VkExtent2D windowExtent_{1700, 900};

    float deltaTime_     = 1;

    jvk::Context ctx_;
    jvk::Swapchain swapchain_;

    // FRAME DATA
    FrameData frames_[JVK_NUM_FRAMES];
    FrameData &getCurrentFrame() { return frames_[frameNumber_ % JVK_NUM_FRAMES]; }

    // QUEUE
    jvk::Queue graphicsQueue_;

    // MEMORY MANAGEMENT
    VmaAllocator allocator_;

    // DRAW IMAGES
    jvk::Image drawImage_;
    jvk::Image depthImage_;
    VkExtent2D drawExtent_;
    float renderScale_ = 1.0f;

    // DESCRIPTORS
    jvk::DynamicDescriptorAllocator globalDescriptorAllocator_;
    VkDescriptorSet drawImageDescriptors_;
    VkDescriptorSetLayout drawImageDescriptorLayout_;

    // PIPELINES
    std::vector<ComputeEffect> computeEffects_;
    int currentComputeEffect_{0};
    VkPipeline computePipeline_;
    VkPipelineLayout computePipelineLayout_;

    // IMMEDIATE COMMANDS
    ImmediateBuffer immBuffer_;

    // IMGUI
    VkDescriptorPool imguiPool_;

    // SCENE DATA
    GPUSceneData sceneData_;
    VkDescriptorSetLayout sceneDataDescriptorLayout_;

    // TEXTURES
    jvk::Image whiteImage_;
    jvk::Image blackImage_;
    jvk::Image errorCheckerboardImage_;

    jvk::Sampler defaultSamplerLinear_;
    jvk::Sampler defaultSamplerNearest_;

    VkDescriptorSetLayout singleImageDescriptorLayout_;

    // MATERIALS
    GLTFMetallicRoughness metallicRoughnessMaterial_;
    MaterialInstance defaultMaterialData_;
    jvk::Buffer matConstants_;

    // SCENE
    DrawContext drawCtx_;
    std::unordered_map<std::string, std::shared_ptr<Node>> loadedNodes_;
    std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> loadedScenes_;

    // CAMERA
    Camera mainCamera_;

    struct EngineStats {
        float frameTime;
        int triangleCount;
        int drawCallCount;
        float sceneUpdateTime;
        float meshDrawTime;
    } stats_;

    // MSAA
    VkSampleCountFlagBits maxMsaaSamples_      = VK_SAMPLE_COUNT_1_BIT;
    VkSampleCountFlagBits selectedMsaaSamples_ = VK_SAMPLE_COUNT_4_BIT;

    struct SDL_Window *window_ = nullptr;

    static JVKEngine &get();

    void init();

    void cleanup();

    void draw();

    void run();

    GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices) const;

    // IMAGES
    jvk::Image createImage(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false, VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT) const;
    jvk::Image createImage(void *data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false) const;
    void destroyImage(const jvk::Image &image) const;

    // BUFFERS
    jvk::Buffer createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) const;
    void destroyBuffer(const jvk::Buffer &buffer) const;

    void updateScene();
private:
    bool resizeRequested_ = false;
    void resizeSwapchain();

    // INITIALIZATION
    void initVulkan();
    void initSwapchain();
    void initDrawImages();
    void initCommands();
    void initSyncStructures();
    void initDescriptors();
    void initPipelines();
    void initImgui();

    // DRAW
    void drawBackground(VkCommandBuffer cmd) const;
    void drawImgui(VkCommandBuffer cmd, VkImageView targetImageView) const;
    void drawGeometry(VkCommandBuffer r);

    // PIPELINES
    void initBackgroundPipelines();

    // MESHES
    void initDefaultData();

    // MSAA
    VkSampleCountFlagBits getMaxUsableSampleCount();
};