#pragma once

#include <immediate.hpp>
#include <mesh.hpp>

#include <camera.hpp>
#include <deletion_stack.hpp>
#include <jvk.hpp>
#include <stack>
#include <vk_descriptors.hpp>
#include <vk_types.hpp>

#include <jvk/context.hpp>
#include <jvk/swapchain.hpp>
#include <jvk/commands.hpp>
#include <jvk/fence.hpp>
#include <jvk/semaphore.hpp>
#include <jvk/queue.hpp>
#include <jvk/image.hpp>

class JVKEngine;

struct RenderObject {
    uint32_t indexCount;
    uint32_t firstIndex;
    VkBuffer indexBuffer;

    MaterialInstance *material;

    glm::mat4 transform;
    VkDeviceAddress vertexBufferAddress;
};

struct DrawContext {
    std::vector<RenderObject> opaqueSurfaces;
    std::vector<RenderObject> transparentSurfaces;
};

struct MeshNode : public Node {
    std::shared_ptr<MeshAsset> mesh;

    virtual void draw(const glm::mat4 &topMatrix, DrawContext &ctx) override;
};

struct GLTFMetallicRoughness {
    MaterialPipeline opaquePipeline;
    MaterialPipeline transparentPipeline;
    VkDescriptorSetLayout materialDescriptorLayout;

    // To be written to UBO
    struct MaterialConstants {
        glm::vec4 colorFactors;
        glm::vec4 metallicRoughnessFactors;
        glm::vec4 extra[14];
    };

    struct MaterialResources {
        jvk::Image colorImage;
        VkSampler colorSampler;

        jvk::Image metallicRoughnessImage;
        VkSampler metallicRoughnessSampler;

        VkBuffer dataBuffer;
        uint32_t dataBufferOffset;
    };

    DescriptorWriter writer;

    void buildPipelines(JVKEngine *engine);
    void clearResources(VkDevice device) const;

    MaterialInstance writeMaterial(VkDevice device, MaterialPass pass, const MaterialResources &resources, DynamicDescriptorAllocator &descriptorAllocator);
};

struct MeshAsset;
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

    DeletionQueue deletionQueue;
    DynamicDescriptorAllocator frameDescriptors;
};

constexpr unsigned int JVK_NUM_FRAMES = 2;

class JVKEngine {
public:
    bool isInitialized_ = false;
    int frameNumber_    = 0;
    bool stopRendering_ = false;
    VkExtent2D windowExtent_{1700, 900};

    float deltaTime_     = 1;

    jvk::Context context_;
    jvk::Swapchain swapchain_;

    // FRAME DATA
    FrameData frames_[JVK_NUM_FRAMES];
    FrameData &getCurrentFrame() { return frames_[frameNumber_ % JVK_NUM_FRAMES]; }

    // QUEUE
    jvk::Queue graphicsQueue_;

    // MEMORY MANAGEMENT
    VmaAllocator allocator_;

    // DRAW IMAGES
    jvk::Image _drawImage;
    jvk::Image _depthImage;
    VkExtent2D _drawExtent;
    float renderScale = 1.0f;

    // DESCRIPTORS
    DynamicDescriptorAllocator _globalDescriptorAllocator;
    VkDescriptorSet _drawImageDescriptors;
    VkDescriptorSetLayout _drawImageDescriptorLayout;

    // PIPELINES
    std::vector<ComputeEffect> computeEffects;
    int currentComputeEffect{0};
    VkPipeline _gradientPipeline;
    VkPipelineLayout _gradientPipelineLayout;

    // IMMEDIATE COMMANDS
    ImmediateBuffer immBuffer_;

    // IMGUI
    VkDescriptorPool _imguiPool;

    // MESHES
    GPUSceneData sceneData;
    VkDescriptorSetLayout _gpuSceneDataDescriptorLayout;

    // TEXTURES
    jvk::Image whiteImage_;
    jvk::Image blackImage_;
    jvk::Image errorCheckerboardImage_;

    VkSampler _defaultSamplerLinear;
    VkSampler _defaultSamplerNearest;

    VkDescriptorSetLayout _singleImageDescriptorLayout;

    // MATERIALS
    GLTFMetallicRoughness _metallicRoughnessMaterial;
    MaterialInstance _defaultMaterialData;
    AllocatedBuffer _matConstants;

    // SCENE
    DrawContext _mainDrawContext;
    std::unordered_map<std::string, std::shared_ptr<Node>> _loadedNodes;
    std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> loadedScenes;

    // CAMERA
    Camera _mainCamera;

    struct EngineStats {
        float frameTime;
        int triangleCount;
        int drawCallCount;
        int sceneUpdateTime;
        int meshDrawTime;
    } _stats;

    // MSAA
    VkSampleCountFlagBits _maxMsaaSamples = VK_SAMPLE_COUNT_1_BIT;
    VkSampleCountFlagBits _selectedMsaaSamples = VK_SAMPLE_COUNT_4_BIT;

    struct SDL_Window *_window = nullptr;

    static JVKEngine &get();

    void init();

    void cleanup();

    void draw();

    void run();

    GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices) const;

    // IMAGES
    jvk::Image createImage(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false, VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT) const;
    jvk::Image createImage(void *data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false) const;

    // BUFFERS
    AllocatedBuffer createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) const;
    void destroyBuffer(const AllocatedBuffer &buffer) const;

    void updateScene();
private:
    bool _resizeRequested = false;
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