#pragma once

#include <camera.hpp>
#include <material.hpp>
#include <stack>

#include <immediate.hpp>
#include <jvk.hpp>
#include <scene.hpp>

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

std::optional<jvk::Image> loadImage(const JVKEngine *engine, const std::string &path);

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
    jvk::Image depthStencilImage_;
    VkExtent2D drawExtent_;
    float renderScale_ = 1.0f;

    // DESCRIPTORS
    jvk::DynamicDescriptorAllocator globalDescriptorAllocator_;
    VkDescriptorSet drawImageDescriptors_;
    VkDescriptorSetLayout drawImageDescriptorLayout_;

    // COMPUTE
    std::vector<ComputeEffect> computeEffects_;
    int currentComputeEffect_{0};
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
    Material metallicRoughnessMaterial_;
    MaterialInstance defaultMaterialData_;
    jvk::Buffer matConstants_;

    // SCENE
    DrawContext drawCtx_;
    std::unordered_map<std::string, std::shared_ptr<Node>> loadedNodes_;
    std::unordered_map<std::string, std::shared_ptr<Scene>> loadedScenes_;

    // BILLBOARD
    struct BillboardPushConstants {
        glm::vec4 particleCenter;
        glm::vec4 color;
        glm::vec4 scale; // Use X-component
        uint32_t textureIndex;
    };

    jvk::Pipeline billboardPipeline_;

    VkDescriptorSetLayout billboardDescriptorLayout_;
    VkDescriptorSet billboardDescriptorSet_;

    jvk::Image lightbulbImage_;
    jvk::Image sunImage_;

    // MSAA
    VkSampleCountFlagBits maxMsaaSamples_      = VK_SAMPLE_COUNT_1_BIT;
    VkSampleCountFlagBits selectedMsaaSamples_ = VK_SAMPLE_COUNT_4_BIT;

    // CAMERA
    Camera mainCamera_;

    struct EngineStats {
        float frameTime;
        int triangleCount;
        int drawCallCount;
        float sceneUpdateTime;
        float meshDrawTime;
    } stats_;

    struct SDL_Window *window_ = nullptr;

    static JVKEngine &get();

    void init();

    void cleanup();

    void draw();

    void run();

    [[nodiscard]] GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices) const;

    // IMAGES
    [[nodiscard]] jvk::Image createImage(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false, VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT) const;
    [[nodiscard]] jvk::Image createImage(void *data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false) const;

    // BUFFERS
    [[nodiscard]] jvk::Buffer createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) const;
    void destroyBuffer(const jvk::Buffer &buffer) const;

    void updateScene();
private:
    glm::vec4 billboardColor_ = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    bool enableSpotlight_     = false;

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
    void drawImgui(VkCommandBuffer cmd, VkImageView targetImageView) const;
    void drawGeometry(VkCommandBuffer cmd);
    void drawBillboards(VkCommandBuffer cmd);

    // PIPELINES
    void initBackgroundPipelines();
    void initBillboardPipeline();

    // MESHES
    void initDefaultData();

    // MSAA
    [[nodiscard]] VkSampleCountFlagBits getMaxUsableSampleCount() const;
};