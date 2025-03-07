#pragma once

#include <jvk.hpp>
#include <stack>
#include <vk_descriptors.hpp>
#include <vk_types.hpp>


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

struct DeletionQueue {
    std::stack<std::function<void()>> deletors;

    void push(std::function<void()> &&function) {
        deletors.push(function);
    }

    void flush() {
        while (!deletors.empty()) {
            deletors.top()();
            deletors.pop();
        }
    }
};

struct FrameData {
    // FRAME COMMANDS
    VkCommandPool _commandPool;
    VkCommandBuffer _mainCommandBuffer;

    // FRAME SYNC
    // Semaphores:
    //  1. To have render commands wait on swapchain image request
    //  2. Control presentation of rendered image to OS after draw
    // Fences:
    //  1. Wait for draw commands of a submitted cmd buffer to be finished
    VkSemaphore _swapchainSemaphore;
    VkSemaphore _renderSemaphore;
    VkFence _renderFence;

    DeletionQueue _deletionQueue;
};

constexpr unsigned int JVK_NUM_FRAMES = 2;

class JVKEngine {
public:
    bool _isInitialized = false;
    int _frameNumber    = 0;
    bool _stopRendering = false;
    VkExtent2D _windowExtent{1700, 900};

    // VULKAN INSTANCE
    VkInstance _instance;
    VkDebugUtilsMessengerEXT _debugMessenger;
    VkPhysicalDevice _chosenGPU;
    VkDevice _device;
    VkSurfaceKHR _surface;

    // SWAPCHAIN
    VkSwapchainKHR _swapchain;
    VkFormat _swapchainImageFormat;
    std::vector<VkImage> _swapchainImages;
    std::vector<VkImageView> _swapchainImageViews;
    VkExtent2D _swapchainExtent;

    // FRAME DATA
    FrameData _frames[JVK_NUM_FRAMES];
    FrameData &getCurrentFrame() { return _frames[_frameNumber % JVK_NUM_FRAMES]; }

    // QUEUE
    VkQueue _graphicsQueue;
    uint32_t _graphicsQueueFamily;

    // MEMORY MANAGEMENT
    DeletionQueue _globalDeletionQueue;
    VmaAllocator _allocator;

    // DRAW IMAGES
    AllocatedImage _drawImage;
    AllocatedImage _depthImage;
    VkExtent2D _drawExtent;
    float renderScale = 1.0f;

    // DESCRIPTORS
    DescriptorAllocator _globalDescriptorAllocator;
    VkDescriptorSet _drawImageDescriptors;
    VkDescriptorSetLayout _drawImageDescriptorLayout;

    // PIPELINES
    std::vector<ComputeEffect> computeEffects;
    int currentComputeEffect{0};
    VkPipeline _gradientPipeline;
    VkPipelineLayout _gradientPipelineLayout;

    VkPipelineLayout _trianglePipelineLayout;
    VkPipeline _trianglePipeline;

    VkPipelineLayout _meshPipelineLayout;
    VkPipeline _meshPipeline;

    // IMMEDIATE COMMANDS
    VkFence _immFence;
    VkCommandBuffer _immCommandBuffer;
    VkCommandPool _immCommandPool;

    // IMGUI
    VkDescriptorPool _imguiPool;

    // MESHES
    GPUMeshBuffers rectangle;
    std::vector<std::shared_ptr<MeshAsset>> testMeshes;

    struct SDL_Window *_window = nullptr;

    static JVKEngine &get();

    void init();

    void cleanup();

    void draw();

    void run();

    void immediateSubmit(std::function<void(VkCommandBuffer cmd)> && function) const;
    GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices) const;
private:
    bool _resizeRequested = false;
    void resizeSwapchain();

    // INITIALIZATION
    void initVulkan();
    void initSwapchain();
    void initCommands();
    void initSyncStructures();
    void initDescriptors();
    void initPipelines();
    void initImgui();

    // CREATION
    void createSwapchain(uint32_t width, uint32_t height);

    // DESTRUCTION
    void destroySwapchain() const;

    // DRAW
    void drawBackground(VkCommandBuffer cmd) const;
    void drawImgui(VkCommandBuffer cmd, VkImageView targetImageView) const;
    void drawGeometry(VkCommandBuffer cmd) const;

    // PIPELINES
    void initBackgroundPipelines();
    void initTrianglePipeline();
    void initMeshPipeline();

    // BUFFERS
    AllocatedBuffer createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) const;
    void destroyBuffer(const AllocatedBuffer &buffer) const;

    // MESHES
    void initDefaultData();
};