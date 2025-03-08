#include <engine.hpp>
#include <jvk.hpp>
#include <vk_init.hpp>
#include <vk_pipelines.hpp>
#include <vk_util.hpp>

#include <SDL.h>
#include <SDL_vulkan.h>

#include "VkBootstrap.h"

#include <chrono>
#include <thread>

#define VMA_IMPLEMENTATION
#include "vk_loader.hpp"

#include <vk_mem_alloc.h>

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>

constexpr bool JVK_USE_VALIDATION_LAYERS = true;

JVKEngine *loadedEngine = nullptr;

JVKEngine &JVKEngine::get() {
    return *loadedEngine;
}

void JVKEngine::init() {
    fmt::print("Initializing engine\n");
    assert(loadedEngine == nullptr);
    loadedEngine = this;

    SDL_Init(SDL_INIT_VIDEO);

    SDL_WindowFlags windowFlags = static_cast<SDL_WindowFlags>(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    _window = SDL_CreateWindow(
            "JVK",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            _windowExtent.width,
            _windowExtent.height,
            windowFlags);

    initVulkan();
    initSwapchain();
    initCommands();
    initSyncStructures();
    initDescriptors();
    initPipelines();
    initImgui();
    initDefaultData();

    // CAMERA
    _mainCamera.velocity = glm::vec3(0.0f);
    _mainCamera.position = glm::vec3(30.f, -00.f, -085.f);
    _mainCamera.pitch = 0.0f;
    _mainCamera.yaw = 0.0f;

    // SCENE
    std::string scenePath = "../assets/structure.glb";
    auto sceneFile = loadGLTF(this, scenePath);
    assert(sceneFile.has_value());
    loadedScenes["base_scene"] = *sceneFile;

    _isInitialized = true;
    fmt::print("Engine initialized\n");
}

void JVKEngine::cleanup() {
    if (_isInitialized) {
        vkDeviceWaitIdle(_device);

        loadedScenes.clear();

        // Frame data
        for (int i = 0; i < JVK_NUM_FRAMES; ++i) {
            vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);

            // Frame sync
            vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
            vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
            vkDestroySemaphore(_device, _frames[i]._swapchainSemaphore, nullptr);

            _frames[i]._deletionQueue.flush();

            _frames[i]._frameDescriptors.destroyPools(_device);
        }

        // Textures
        vkDestroySampler(_device, _defaultSamplerLinear, nullptr);
        vkDestroySampler(_device, _defaultSamplerNearest, nullptr);
        destroyImage(_errorCheckerboardImage);
        destroyImage(_blackImage);
        destroyImage(_greyImage);
        destroyImage(_whiteImage);

        // Default data
        _metallicRoughnessMaterial.clearResources(_device);
        destroyBuffer(_matConstants);

        // ImGui
        ImGui_ImplVulkan_Shutdown();
        vkDestroyDescriptorPool(_device, _imguiPool, nullptr);

        // Immediate command pool
        vkDestroyCommandPool(_device, _immCommandPool, nullptr);
        vkDestroyFence(_device, _immFence, nullptr);

        _globalDeletionQueue.flush();

        // Pipelines
        vkDestroyPipelineLayout(_device, _meshPipelineLayout, nullptr);
        vkDestroyPipeline(_device, _meshPipeline, nullptr);

        vkDestroyPipelineLayout(_device, _trianglePipelineLayout, nullptr);
        vkDestroyPipeline(_device, _trianglePipeline, nullptr);

        vkDestroyPipelineLayout(_device, _gradientPipelineLayout, nullptr);
        vkDestroyPipeline(_device, computeEffects[0].pipeline, nullptr);
        vkDestroyPipeline(_device, computeEffects[1].pipeline, nullptr);

        // Descriptors
        _globalDescriptorAllocator.destroyPools(_device);
        vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(_device, _gpuSceneDataDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(_device, _singleImageDescriptorLayout, nullptr);

        // Depth image
        vkDestroyImageView(_device, _depthImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _depthImage.image, _depthImage.allocation);

        // Draw image
        vkDestroyImageView(_device, _drawImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);

        // VMA
        vmaDestroyAllocator(_allocator);

        // Swapchain
        destroySwapchain();

        // API
        vkDestroySurfaceKHR(_instance, _surface, nullptr);
        vkDestroyDevice(_device, nullptr);
        vkb::destroy_debug_utils_messenger(_instance, _debugMessenger);
        vkDestroyInstance(_instance, nullptr);
        SDL_DestroyWindow(_window);
    }

    loadedEngine = nullptr;
}

void JVKEngine::draw() {
    updateScene();
    // Wait and reset render fence
    VK_CHECK(vkWaitForFences(_device, 1, &getCurrentFrame()._renderFence, true, 1000000000));
    getCurrentFrame()._frameDescriptors.clearPools(_device);

    VK_CHECK(vkResetFences(_device, 1, &getCurrentFrame()._renderFence));

    // Request an image from swapchain
    uint32_t swapchainImageIndex;
    VkResult e = vkAcquireNextImageKHR(_device, _swapchain, 1000000000, getCurrentFrame()._swapchainSemaphore, nullptr, &swapchainImageIndex);
    if (e == VK_ERROR_OUT_OF_DATE_KHR) {
        _resizeRequested = true;
        return;
    }

    // Reset the command buffer
    VkCommandBuffer cmd = getCurrentFrame()._mainCommandBuffer;
    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    _drawExtent.width  = std::min(_swapchainExtent.width, _drawImage.imageExtent.width) * renderScale;
    _drawExtent.height = std::min(_swapchainExtent.height, _drawImage.imageExtent.height) * renderScale;

    // Start the command buffer
    VkCommandBufferBeginInfo cmdBeginInfo = VkInit::commandBufferBegin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    // Transition draw image to general
    VkUtil::transitionImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    ComputeEffect &effect = computeEffects[currentComputeEffect];

    // Bind compute pipeline & descriptors
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipelineLayout, 0, 1, &_drawImageDescriptors, 0, nullptr);

    // Push constants for compute
    vkCmdPushConstants(cmd, _gradientPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &effect.data);

    // Draw compute
    vkCmdDispatch(cmd, std::ceil(_drawExtent.width / 16.0f), std::ceil(_drawExtent.height / 16.0f), 1);

    // Transition draw/depth images for render pass
    VkUtil::transitionImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkUtil::transitionImage(cmd, _depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    drawGeometry(cmd);

    // Transition draw image to transfer source
    VkUtil::transitionImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    // Transition swapchain image to transfer destination
    VkUtil::transitionImage(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Copy image to from draw image to swapchain
    VkUtil::copyImageToImage(cmd, _drawImage.image, _swapchainImages[swapchainImageIndex], _drawExtent, _swapchainExtent);

    // Transition swapchain to attachment optimal
    VkUtil::transitionImage(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // Draw UI
    drawImgui(cmd, _swapchainImageViews[swapchainImageIndex]);

    // Transition swapchain for presentation
    VkUtil::transitionImage(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    // End command buffer
    VK_CHECK(vkEndCommandBuffer(cmd));

    // Submit buffer
    // srcStageMask set to COLOR_ATTACHMENT_OUTPUT_BIT to wait for color attachment output (waiting for swapchain image)
    // dstStageMask set to ALL_GRAPHICS_BIT to signal that all graphics stages are done
    VkCommandBufferSubmitInfo cmdInfo = VkInit::commandBufferSubmit(cmd);
    VkSemaphoreSubmitInfo waitInfo    = VkInit::semaphoreSubmit(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, getCurrentFrame()._swapchainSemaphore);
    VkSemaphoreSubmitInfo signalInfo  = VkInit::semaphoreSubmit(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, getCurrentFrame()._renderSemaphore);
    VkSubmitInfo2 submit              = VkInit::submit(&cmdInfo, &signalInfo, &waitInfo);

    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, getCurrentFrame()._renderFence));

    // Present
    VkPresentInfoKHR presentInfo   = {};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext              = nullptr;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &_swapchain;
    presentInfo.pWaitSemaphores    = &getCurrentFrame()._renderSemaphore;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pImageIndices      = &swapchainImageIndex;


    VkResult presentResult = vkQueuePresentKHR(_graphicsQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR) {
        _resizeRequested = true;
    }

    _frameNumber++;
}

void JVKEngine::run() {
    SDL_Event e;
    bool bQuit = false;

    while (!bQuit) {
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) bQuit = true;

            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                SDL_SetRelativeMouseMode(SDL_TRUE);
            }

            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
                SDL_SetRelativeMouseMode(SDL_FALSE);
            }

            if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                    _stopRendering = true;
                }
                if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
                    _stopRendering = false;
                }
            }

            _mainCamera.processSDLEvent(e);
            ImGui_ImplSDL2_ProcessEvent(&e);
        }

        if (_stopRendering) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (_resizeRequested) {
            resizeSwapchain();
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();

        ImGui::NewFrame();

        if (ImGui::Begin("computeEffects")) {

            ImGui::SliderFloat("Render Scale", &renderScale, 0.3f, 1.0f);

            ComputeEffect &selected = computeEffects[currentComputeEffect];

            ImGui::Text("Selected effect: ", selected.name);

            ImGui::SliderInt("Effect Index", &currentComputeEffect, 0, computeEffects.size() - 1);

            ImGui::InputFloat4("data1", reinterpret_cast<float *>(&selected.data.data1));
            ImGui::InputFloat4("data2", reinterpret_cast<float *>(&selected.data.data2));
            ImGui::InputFloat4("data3", reinterpret_cast<float *>(&selected.data.data3));
            ImGui::InputFloat4("data4", reinterpret_cast<float *>(&selected.data.data4));
        }
        ImGui::End();

        ImGui::Render();

        draw();
    }
}

void JVKEngine::initVulkan() {
    // CREATE INSTANCE
    vkb::InstanceBuilder builder;
    auto vkbInstanceResult = builder.set_app_name("JVK")
                                     .request_validation_layers(JVK_USE_VALIDATION_LAYERS)
                                     .use_default_debug_messenger()
                                     .require_api_version(1, 3, 0)
                                     .build();

    if (!vkbInstanceResult) {
        fmt::println("Failed to create Vulkan instance. Error: {}", vkbInstanceResult.error().message());
        abort();
    }

    vkb::Instance vkbInstance = vkbInstanceResult.value();

    _instance       = vkbInstance.instance;
    _debugMessenger = vkbInstance.debug_messenger;

    // CREATE SURFACE
    SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

    // 1.3 FEATURES
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = true;
    features13.synchronization2 = true;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType               = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing  = true;

    // PHYSICAL DEVICE
    vkb::PhysicalDeviceSelector physicalDeviceBuilder{vkbInstance};
    auto vkbPhysicalDeviceResult = physicalDeviceBuilder.set_minimum_version(1, 3)
                                           .set_required_features_13(features13)
                                           .set_required_features_12(features12)
                                           .set_surface(_surface)
                                           .select();

    if (!vkbPhysicalDeviceResult) {
        fmt::println("Failed to select physical device. Error: {}", vkbPhysicalDeviceResult.error().message());
        abort();
    }

    vkb::PhysicalDevice vkbPhysicalDevice = vkbPhysicalDeviceResult.value();

    // DEVICE
    vkb::DeviceBuilder deviceBuilder{vkbPhysicalDevice};
    vkb::Device vkbDevice = deviceBuilder.build().value();
    _device               = vkbDevice.device;
    _chosenGPU            = vkbPhysicalDevice.physical_device;

    // QUEUE
    _graphicsQueue       = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    _graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    // VMA
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice         = _chosenGPU;
    allocatorInfo.device                 = _device;
    allocatorInfo.instance               = _instance;
    allocatorInfo.flags                  = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocatorInfo, &_allocator);
}

void JVKEngine::initSwapchain() {
    createSwapchain(_windowExtent.width, _windowExtent.height);

    // CREATE DRAW IMAGE
    VkExtent3D drawImageExtent = {
            _windowExtent.width,
            _windowExtent.height,
            1};

    _drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;// 16-bit float image
    _drawImage.imageExtent = drawImageExtent;

    VkImageUsageFlags drawImageUsages = {};
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;    // Copy from image
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;    // Copy to image
    drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;         // Allow compute shader to write
    drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;// Graphics pipeline

    VkImageCreateInfo drawImageInfo = VkInit::image(_drawImage.imageFormat, drawImageUsages, drawImageExtent);

    // VMA_MEMORY_USAGE_GPU_ONLY: the texture will never be accessed from the CPU
    // VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT: GPU exclusive memory flag, guarantees that the memory is on the GPU
    VmaAllocationCreateInfo drawImageAllocInfo = {};
    drawImageAllocInfo.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;
    drawImageAllocInfo.requiredFlags           = static_cast<VkMemoryPropertyFlags>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vmaCreateImage(_allocator, &drawImageInfo, &drawImageAllocInfo, &_drawImage.image, &_drawImage.allocation, nullptr);

    VkImageViewCreateInfo imageViewInfo = VkInit::imageView(_drawImage.imageFormat, _drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK(vkCreateImageView(_device, &imageViewInfo, nullptr, &_drawImage.imageView));

    // CREATE DEPTH IMAGE
    _depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
    _depthImage.imageExtent = drawImageExtent;

    VkImageUsageFlags depthImageUsages{};
    depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VkImageCreateInfo depthImageInfo = VkInit::image(_depthImage.imageFormat, depthImageUsages, drawImageExtent);
    vmaCreateImage(_allocator, &depthImageInfo, &drawImageAllocInfo, &_depthImage.image, &_depthImage.allocation, nullptr);

    VkImageViewCreateInfo depthImageViewInfo = VkInit::imageView(_depthImage.imageFormat, _depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);
    VK_CHECK(vkCreateImageView(_device, &depthImageViewInfo, nullptr, &_depthImage.imageView));
}

void JVKEngine::initCommands() {
    // COMMAND POOL
    // Indicate that buffers should be individually resettable
    VkCommandPoolCreateFlags flags          = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPoolCreateInfo commandPoolInfo = VkInit::commandPool(_graphicsQueueFamily, flags);

    // COMMAND BUFFERS
    for (int i = 0; i < JVK_NUM_FRAMES; ++i) {
        VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

        VkCommandBufferAllocateInfo cmdAllocInfo = VkInit::commandBuffer(_frames[i]._commandPool);

        VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));
    }

    // IMMEDIATE BUFFERS
    VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_immCommandPool));
    VkCommandBufferAllocateInfo cmdAllocInfo = VkInit::commandBuffer(_immCommandPool, 1);
    VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_immCommandBuffer));
}

void JVKEngine::initSyncStructures() {
    // FENCE
    // Start signaled to wait on the first frame
    VkFenceCreateInfo fenceCreateInfo = VkInit::fence(VK_FENCE_CREATE_SIGNALED_BIT);

    // SEMAPHORE
    VkSemaphoreCreateInfo semaphoreCreateInfo = VkInit::semaphore();

    for (int i = 0; i < JVK_NUM_FRAMES; ++i) {
        VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));

        VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._swapchainSemaphore));
        VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore));
    }

    // IMMEDIATE SUBMIT FENCE
    VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_immFence));
}

void JVKEngine::createSwapchain(uint32_t width, uint32_t height) {
    vkb::SwapchainBuilder swapchainBuilder{_chosenGPU, _device, _surface};
    _swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

    vkb::Swapchain vkbSwapchain = swapchainBuilder
                                          .set_desired_format(
                                                  VkSurfaceFormatKHR{
                                                          .format     = _swapchainImageFormat,
                                                          .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
                                          .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                                          .set_desired_extent(width, height)
                                          .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                                          .build()
                                          .value();
    _swapchainExtent     = vkbSwapchain.extent;
    _swapchain           = vkbSwapchain.swapchain;
    _swapchainImages     = vkbSwapchain.get_images().value();
    _swapchainImageViews = vkbSwapchain.get_image_views().value();
}

void JVKEngine::destroySwapchain() const {
    vkDestroySwapchainKHR(_device, _swapchain, nullptr);
    for (int i = 0; i < _swapchainImageViews.size(); ++i) {
        vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
    }
}

void JVKEngine::drawBackground(VkCommandBuffer cmd) const {
    VkClearColorValue clearValue;
    float flash = std::abs(std::sin(_frameNumber / 120.0f));
    clearValue  = {{0.0f, 0.0f, flash, 1.0f}};

    VkImageSubresourceRange clearRange = VkInit::imageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);

    vkCmdClearColorImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);
}

void JVKEngine::initDescriptors() {
    // GLOBAL DESCRIPTOR ALLOCATOR
    std::vector<DynamicDescriptorAllocator::PoolSizeRatio> sizes =
    {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 }
    };

    _globalDescriptorAllocator.init(_device, 10, sizes);

    // DRAW IMAGE
    // Layout
    {
        DescriptorLayoutBuilder builder;
        builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        _drawImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
    }

    // Allocate set
    _drawImageDescriptors = _globalDescriptorAllocator.allocate(_device, _drawImageDescriptorLayout);

    // Write to set
    DescriptorWriter writer;
    writer.writeImage(0, _drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    writer.updateSet(_device, _drawImageDescriptors);

    // FRAME DESCRIPTORS
    for (int i = 0; i < JVK_NUM_FRAMES; ++i) {
        std::vector<DynamicDescriptorAllocator::PoolSizeRatio> frameSizes = {
                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
        };

        _frames[i]._frameDescriptors = DynamicDescriptorAllocator();
        _frames[i]._frameDescriptors.init(_device, 1000, frameSizes);
    }

    // GPU SCENE DATA
    {
        DescriptorLayoutBuilder builder;
        builder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        _gpuSceneDataDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    // TEXTURES
    {
        DescriptorLayoutBuilder builder;
        builder.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        _singleImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_FRAGMENT_BIT);
    }
}

void JVKEngine::initPipelines() {
    initBackgroundPipelines();
    initTrianglePipeline();
    initMeshPipeline();
    _metallicRoughnessMaterial.buildPipelines(this);
}

void JVKEngine::initBackgroundPipelines() {
    // PIPELINE LAYOUT
    // Pass an aray of descriptor set layouts, push constants, etc
    VkPushConstantRange pushConstant{};
    pushConstant.offset     = 0;
    pushConstant.size       = sizeof(ComputePushConstants);
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkPipelineLayoutCreateInfo layout{};
    layout.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.pNext                  = nullptr;
    layout.pSetLayouts            = &_drawImageDescriptorLayout;
    layout.setLayoutCount         = 1;
    layout.pPushConstantRanges    = &pushConstant;
    layout.pushConstantRangeCount = 1;
    VK_CHECK(vkCreatePipelineLayout(_device, &layout, nullptr, &_gradientPipelineLayout));

    // PIPELINE STAGES (AND SHADERS)
    VkShaderModule gradientShader;
    if (!VkUtil::loadShaderModule("../shaders/gradient_pc.comp.spv", _device, &gradientShader)) {
        fmt::print("Error when building gradient compute shader \n");
    }

    VkShaderModule skyShader;
    if (!VkUtil::loadShaderModule("../shaders/sky.comp.spv", _device, &skyShader)) {
        fmt::println("Error when building sky compute shader");
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.pNext  = nullptr;
    stageInfo.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = gradientShader;
    stageInfo.pName  = "main";

    // CREATE GRADIENT PIPELINE
    VkComputePipelineCreateInfo computeInfo{};
    computeInfo.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computeInfo.pNext  = nullptr;
    computeInfo.layout = _gradientPipelineLayout;
    computeInfo.stage  = stageInfo;

    ComputeEffect gradient;
    gradient.layout     = _gradientPipelineLayout;
    gradient.name       = "gradient";
    gradient.data       = {};
    gradient.data.data1 = glm::vec4(1, 0, 0, 1);
    gradient.data.data2 = glm::vec4(0, 0, 1, 1);

    VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computeInfo, nullptr, &gradient.pipeline));

    // CREATE SKY PIPELINE
    computeInfo.stage.module = skyShader;

    ComputeEffect sky;
    sky.layout     = _gradientPipelineLayout;
    sky.name       = "sky";
    sky.data       = {};
    sky.data.data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);
    VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computeInfo, nullptr, &sky.pipeline));

    computeEffects.push_back(gradient);
    computeEffects.push_back(sky);

    vkDestroyShaderModule(_device, gradientShader, nullptr);
    vkDestroyShaderModule(_device, skyShader, nullptr);
}

void JVKEngine::immediateSubmit(std::function<void(VkCommandBuffer cmd)> &&function) const {
    // Reset fence & buffer
    VK_CHECK(vkResetFences(_device, 1, &_immFence));
    VK_CHECK(vkResetCommandBuffer(_immCommandBuffer, 0));

    // Create and start buffer
    VkCommandBuffer cmd               = _immCommandBuffer;
    VkCommandBufferBeginInfo cmdBegin = VkInit::commandBufferBegin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBegin));

    // Record immediate submit commands
    function(cmd);

    // End buffer
    VK_CHECK(vkEndCommandBuffer(cmd));

    // Submit and wait for fence
    VkCommandBufferSubmitInfo cmdInfo = VkInit::commandBufferSubmit(cmd);
    VkSubmitInfo2 submit              = VkInit::submit(&cmdInfo, nullptr, nullptr);
    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, _immFence));
    VK_CHECK(vkWaitForFences(_device, 1, &_immFence, true, 9999999999));
}

void JVKEngine::initImgui() {
    VkDescriptorPoolSize poolSizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
                                        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
                                        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
                                        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
                                        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
                                        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
                                        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets       = 1000;
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.pPoolSizes    = poolSizes;

    VK_CHECK(vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_imguiPool));

    ImGui::CreateContext();
    ImGui_ImplSDL2_InitForVulkan(_window);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance            = _instance;
    initInfo.PhysicalDevice      = _chosenGPU;
    initInfo.Device              = _device;
    initInfo.Queue               = _graphicsQueue;
    initInfo.DescriptorPool      = _imguiPool;
    initInfo.MinImageCount       = 3;
    initInfo.ImageCount          = 3;
    initInfo.UseDynamicRendering = true;

    initInfo.PipelineRenderingCreateInfo                         = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    initInfo.PipelineRenderingCreateInfo.colorAttachmentCount    = 1;
    initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapchainImageFormat;

    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&initInfo);
    ImGui_ImplVulkan_CreateFontsTexture();
}

void JVKEngine::drawImgui(VkCommandBuffer cmd, VkImageView targetImageView) const {
    // Setup color attachment for render pass
    VkRenderingAttachmentInfo colorAttachment = VkInit::renderingAttachment(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderInfo                = VkInit::rendering(_swapchainExtent, &colorAttachment, nullptr);

    // Render
    vkCmdBeginRendering(cmd, &renderInfo);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRendering(cmd);
}

void JVKEngine::initTrianglePipeline() {
    // LOAD SHADER MODULES
    VkShaderModule triangleFragShader;
    if (!VkUtil::loadShaderModule("../shaders/colored_triangle.frag.spv", _device, &triangleFragShader)) {
        fmt::print("Error when building triangle fragment shader module");
    }

    VkShaderModule triangleVertShader;
    if (!VkUtil::loadShaderModule("../shaders/colored_triangle.vert.spv", _device, &triangleVertShader)) {
        fmt::print("Error when building triangle vertex shader module");
    }

    // CREATE PIPELINE LAYOUT
    VkPipelineLayoutCreateInfo piplineLayoutInfo = VkInit::pipelineLayout();
    VK_CHECK(vkCreatePipelineLayout(_device, &piplineLayoutInfo, nullptr, &_trianglePipelineLayout));

    // CREATE PIPELINE
    VkUtil::PipelineBuilder pipelineBuilder;
    pipelineBuilder._pipelineLayout = _trianglePipelineLayout;
    pipelineBuilder.setShaders(triangleVertShader, triangleFragShader);
    pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.setMultiSamplingNone();
    pipelineBuilder.disableBlending();
    pipelineBuilder.enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL);
    pipelineBuilder.setColorAttachmentFormat(_drawImage.imageFormat);
    pipelineBuilder.setDepthAttachmentFormat(_depthImage.imageFormat);
    _trianglePipeline = pipelineBuilder.buildPipeline(_device);

    // CLEAN UP SHADER MODULES
    vkDestroyShaderModule(_device, triangleVertShader, nullptr);
    vkDestroyShaderModule(_device, triangleFragShader, nullptr);
}

void JVKEngine::initMeshPipeline() {
    // LOAD SHADER MODULES
    VkShaderModule triangleVertShader;
    if (!VkUtil::loadShaderModule("../shaders/colored_triangle_mesh.vert.spv", _device, &triangleVertShader)) {
        fmt::print("Error when building triangle vertex shader module");
    }

    VkShaderModule triangleFragShader;
    if (!VkUtil::loadShaderModule("../shaders/tex_image.frag.spv", _device, &triangleFragShader)) {
        fmt::print("Error when building triangle fragment shader module");
    }

    // PUSH CONSTANTS
    VkPushConstantRange bufferRange{};
    bufferRange.offset     = 0;
    bufferRange.size       = sizeof(GPUDrawPushConstants);
    bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // CREATE PIPELINE LAYOUT
    VkPipelineLayoutCreateInfo info = VkInit::pipelineLayout();
    info.pPushConstantRanges        = &bufferRange;
    info.pushConstantRangeCount     = 1;
    info.pSetLayouts                = &_singleImageDescriptorLayout;
    info.setLayoutCount             = 1;
    VK_CHECK(vkCreatePipelineLayout(_device, &info, nullptr, &_meshPipelineLayout));

    // CREATE PIPELINE
    VkUtil::PipelineBuilder builder;
    builder._pipelineLayout = _meshPipelineLayout;
    builder.setShaders(triangleVertShader, triangleFragShader);
    builder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    builder.setPolygonMode(VK_POLYGON_MODE_FILL);
    builder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    builder.setMultiSamplingNone();
    builder.enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL);
    //    builder.enableBlendingAdditive();
    builder.disableBlending();
    builder.setColorAttachmentFormat(_drawImage.imageFormat);
    builder.setDepthAttachmentFormat(_depthImage.imageFormat);
    _meshPipeline = builder.buildPipeline(_device);

    vkDestroyShaderModule(_device, triangleFragShader, nullptr);
    vkDestroyShaderModule(_device, triangleVertShader, nullptr);
}

void JVKEngine::drawGeometry(VkCommandBuffer cmd) {
    // SETUP RENDER PASS
    VkRenderingAttachmentInfo colorAttachment = VkInit::renderingAttachment(_drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo depthAttachment = VkInit::depthRenderingAttachment(_depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderingInfo             = VkInit::rendering(_drawExtent, &colorAttachment, &depthAttachment);

    // BEGIN RENDER PASS
    vkCmdBeginRendering(cmd, &renderingInfo);
    // BIND PIPELINE
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipeline);

    // VIEWPORT
    VkViewport viewport{};
    viewport.x        = 0;
    viewport.y        = 0;
    viewport.width    = _drawExtent.width;
    viewport.height   = _drawExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    // SCISSOR
    VkRect2D scissor{};
    scissor.offset.x      = 0;
    scissor.offset.y      = 0;
    scissor.extent.width  = _drawExtent.width;
    scissor.extent.height = _drawExtent.height;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // UNIFORM BUFFERS & GLOBAL DESCRIPTOR SET
    // Contains global scene data (projection matrices, light, etc)
    AllocatedBuffer gpuSceneDataBuffer = createBuffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    GPUSceneData *sceneUniformData     = static_cast<GPUSceneData *>(gpuSceneDataBuffer.allocation->GetMappedData());
    *sceneUniformData                  = sceneData;

    VkDescriptorSet globalDescriptor = getCurrentFrame()._frameDescriptors.allocate(_device, _gpuSceneDataDescriptorLayout);
    DescriptorWriter writer;
    writer.writeBuffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.updateSet(_device, globalDescriptor);

    getCurrentFrame()._deletionQueue.push([=, this]() {
        destroyBuffer(gpuSceneDataBuffer);
    });

    auto draw = [&](const RenderObject &r) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->pipeline);

        // Bind global descriptor and material descriptor
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->pipelineLayout, 0, 1, &globalDescriptor, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->pipelineLayout, 1, 1, &r.material->materialSet,0, nullptr);

        // Bind index buffer
        vkCmdBindIndexBuffer(cmd, r.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        // Push constants
        GPUDrawPushConstants pushConstants;
        pushConstants.vertexBuffer = r.vertexBufferAddress;
        pushConstants.worldMatrix = r.transform;
        vkCmdPushConstants(cmd, r.material->pipeline->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);

        // Draw
        vkCmdDrawIndexed(cmd, r.indexCount, 1, r.firstIndex, 0, 0);
    };

    for (const RenderObject &r : _mainDrawContext.opaqueSurfaces) {
        draw(r);
    }

    for (const RenderObject &r : _mainDrawContext.transparentSurfaces) {
        draw(r);
    }

    vkCmdEndRendering(cmd);
}

AllocatedBuffer JVKEngine::createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) const {
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.pNext = nullptr;
    info.size  = allocSize;
    info.usage = usage;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    AllocatedBuffer buffer;
    VK_CHECK(vmaCreateBuffer(_allocator, &info, &allocInfo, &buffer.buffer, &buffer.allocation, &buffer.info));
    return buffer;
}

void JVKEngine::destroyBuffer(const AllocatedBuffer &buffer) const {
    vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}

GPUMeshBuffers JVKEngine::uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices) const {
    const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
    const size_t indexBufferSize  = indices.size() * sizeof(uint32_t);

    GPUMeshBuffers surface;

    // CREATE BUFFERS
    // Vertex buffer
    surface.vertexBuffer = createBuffer(vertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

    // Vertex buffer address
    VkBufferDeviceAddressInfo deviceAddressInfo{};
    deviceAddressInfo.sType     = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    deviceAddressInfo.buffer    = surface.vertexBuffer.buffer;
    surface.vertexBufferAddress = vkGetBufferDeviceAddress(_device, &deviceAddressInfo);

    // Index buffer
    surface.indexBuffer = createBuffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

    // STAGING BUFFER
    AllocatedBuffer staging = createBuffer(vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    void *data              = staging.allocation->GetMappedData();

    // COPY DATA TO STAGING BUFFER
    memcpy(data, vertices.data(), vertexBufferSize);
    memcpy(static_cast<char *>(data) + vertexBufferSize, indices.data(), indexBufferSize);

    // COPY TO GPU BUFFER
    immediateSubmit([&](VkCommandBuffer cmd) {
        // COPY VERTEX DATA
        VkBufferCopy vertexCopy{0};
        vertexCopy.dstOffset = 0;
        vertexCopy.srcOffset = 0;
        vertexCopy.size      = vertexBufferSize;
        vkCmdCopyBuffer(cmd, staging.buffer, surface.vertexBuffer.buffer, 1, &vertexCopy);

        // COPY INDEX DATA
        VkBufferCopy indexCopy{0};
        indexCopy.dstOffset = 0;
        indexCopy.srcOffset = vertexBufferSize;
        indexCopy.size      = indexBufferSize;
        vkCmdCopyBuffer(cmd, staging.buffer, surface.indexBuffer.buffer, 1, &indexCopy);
    });

    // DESTROY STAGING BUFFER
    destroyBuffer(staging);
    return surface;
}

void JVKEngine::initDefaultData() {
    // TEXTURES
    // 1 pixel default textures
    uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
    _whiteImage    = createImage((void *) &white, VkExtent3D{1, 1, 1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    uint32_t grey = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1));
    _greyImage    = createImage((void *) &grey, VkExtent3D{1, 1, 1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 1.0f));
    _blackImage    = createImage((void *) &black, VkExtent3D{1, 1, 1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    //checkerboard image
    uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
    std::array<uint32_t, 16 * 16> pixels;
    for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 16; y++) {
            pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
        }
    }
    _errorCheckerboardImage = createImage(pixels.data(), VkExtent3D{16, 16, 1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    // SAMPLERS
    VkSamplerCreateInfo sampler{};
    sampler.sType     = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_NEAREST;
    sampler.minFilter = VK_FILTER_NEAREST;
    vkCreateSampler(_device, &sampler, nullptr, &_defaultSamplerNearest);

    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    vkCreateSampler(_device, &sampler, nullptr, &_defaultSamplerLinear);

    // MATERIALS
    GLTFMetallicRoughness::MaterialResources matResources;
    matResources.colorImage               = _whiteImage;
    matResources.colorSampler             = _defaultSamplerLinear;
    matResources.metallicRoughnessImage   = _whiteImage;
    matResources.metallicRoughnessSampler = _defaultSamplerLinear;

    _matConstants = createBuffer(sizeof(GLTFMetallicRoughness::MaterialConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    GLTFMetallicRoughness::MaterialConstants *sceneUniformData = static_cast<GLTFMetallicRoughness::MaterialConstants *>(_matConstants.allocation->GetMappedData());
    sceneUniformData->colorFactors = glm::vec4{1, 1, 1, 1};
    sceneUniformData->metallicRoughnessFactors = glm::vec4{1, 0.5, 0, 0};

    matResources.dataBuffer = _matConstants.buffer;
    matResources.dataBufferOffset = 0;

    _defaultMaterialData = _metallicRoughnessMaterial.writeMaterial(_device, MaterialPass::MAIN_COLOR, matResources,  _globalDescriptorAllocator);
}

void JVKEngine::resizeSwapchain() {
    vkDeviceWaitIdle(_device);
    destroySwapchain();

    int w, h;
    SDL_GetWindowSize(_window, &w, &h);
    _windowExtent.width  = w;
    _windowExtent.height = h;

    createSwapchain(_windowExtent.width, _windowExtent.height);
    _resizeRequested = false;
}

AllocatedImage  JVKEngine::createImage(const VkExtent3D size, const VkFormat format, const VkImageUsageFlags usage, const bool mipmapped) const {
    // IMAGE
    AllocatedImage image;
    image.imageFormat           = format;
    image.imageExtent           = size;
    VkImageCreateInfo imageInfo = VkInit::image(format, usage, size);
    if (mipmapped) {
        imageInfo.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;
    }

    // ALLOCATE
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage         = VMA_MEMORY_USAGE_GPU_ONLY;
    allocInfo.requiredFlags = static_cast<VkMemoryPropertyFlags>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vmaCreateImage(_allocator, &imageInfo, &allocInfo, &image.image, &image.allocation, nullptr));

    // DEPTH
    VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
    if (format == VK_FORMAT_D32_SFLOAT) {
        aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    // IMAGE VIEW
    VkImageViewCreateInfo viewInfo       = VkInit::imageView(format, image.image, aspectFlag);
    viewInfo.subresourceRange.levelCount = imageInfo.mipLevels;
    VK_CHECK(vkCreateImageView(_device, &viewInfo, nullptr, &image.imageView));

    return image;
}

AllocatedImage JVKEngine::createImage(void *data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped) const {
    // STAGING BUFFER
    size_t dataSize              = size.depth * size.width * size.height * 4;
    AllocatedBuffer uploadBuffer = createBuffer(dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    memcpy(uploadBuffer.info.pMappedData, data, dataSize);

    // COPY TO IMAGE
    AllocatedImage image = createImage(size, format, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT, mipmapped);
    immediateSubmit([&](VkCommandBuffer cmd) {
        VkUtil::transitionImage(cmd, image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset      = 0;
        copyRegion.bufferRowLength   = 0;
        copyRegion.bufferImageHeight = 0;

        copyRegion.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel       = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount     = 1;
        copyRegion.imageExtent                     = size;

        vkCmdCopyBufferToImage(cmd, uploadBuffer.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
        VkUtil::transitionImage(cmd, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });

    destroyBuffer(uploadBuffer);
    return image;
}
void JVKEngine::destroyImage(const AllocatedImage &img) const {
    vkDestroyImageView(_device, img.imageView, nullptr);
    vmaDestroyImage(_allocator, img.image, img.allocation);
}

void JVKEngine::updateScene() {
    _mainCamera.update();
    glm::mat4 view = _mainCamera.getViewMatrix();
    glm::mat4 proj = glm::perspective(glm::radians(70.f), static_cast<float>(_windowExtent.width) / static_cast<float>(_windowExtent.height), 0.1f, 10000.0f);
    proj[1][1] *= -1;

    _mainDrawContext.opaqueSurfaces.clear();
    _mainDrawContext.transparentSurfaces.clear();
    loadedScenes["base_scene"]->draw(glm::mat4(1.0f), _mainDrawContext);

    sceneData.view = view;
    sceneData.proj = proj;
    sceneData.viewProj = sceneData.proj * sceneData.view;
    sceneData.ambientColor = glm::vec4(0.1f);
    sceneData.sunlightColor = glm::vec4(1.0f);
    sceneData.sunlightDirection = glm::vec4(0,1,0.5,1.0f);
}

void MeshNode::draw(const glm::mat4 &topMatrix, DrawContext &ctx) {
    glm::mat4 nodeMatrix = topMatrix * worldTransform;

    for (auto & s : mesh->surfaces) {
        RenderObject rObj;
        rObj.indexCount = s.count;
        rObj.firstIndex = s.startIndex;
        rObj.indexBuffer = mesh->meshBuffers.indexBuffer.buffer;
        rObj.material = &s.material->data;

        rObj.transform = nodeMatrix;
        rObj.vertexBufferAddress = mesh->meshBuffers.vertexBufferAddress;

        if (rObj.material->passType == MaterialPass::TRANSPARENT) {
            ctx.transparentSurfaces.push_back(rObj);
        } else {
            ctx.opaqueSurfaces.push_back(rObj);
        }
    }

    Node::draw(topMatrix, ctx);
}

void GLTFMetallicRoughness::buildPipelines(JVKEngine *engine) {
    // LOAD SHADERS
    VkShaderModule vertShader;
    if (!VkUtil::loadShaderModule("../shaders/mesh.vert.spv", engine->_device, &vertShader)) {
        fmt::print("Error when building vertex shader module");
    }
    VkShaderModule fragShader;
    if (!VkUtil::loadShaderModule("../shaders/mesh.frag.spv", engine->_device, &fragShader)) {
        fmt::print("Error when building fragment shader module");
    }

    // PUSH CONSTANTS
    VkPushConstantRange matrixRange{};
    matrixRange.offset     = 0;
    matrixRange.size       = sizeof(GPUDrawPushConstants);
    matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // DESCRIPTOR LAYOUT
    DescriptorLayoutBuilder builder;
    builder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    builder.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    builder.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    materialDescriptorLayout = builder.build(engine->_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    // _gpuSceneDataDescriptorLayout is used as our global descriptor layout
    VkDescriptorSetLayout layouts[] = {engine->_gpuSceneDataDescriptorLayout, materialDescriptorLayout};

    // PIPELINE LAYOUT
    VkPipelineLayoutCreateInfo layoutInfo = VkInit::pipelineLayout();
    layoutInfo.setLayoutCount             = 2;
    layoutInfo.pSetLayouts                = layouts;
    layoutInfo.pPushConstantRanges        = &matrixRange;
    layoutInfo.pushConstantRangeCount     = 1;

    VkPipelineLayout layout;
    VK_CHECK(vkCreatePipelineLayout(engine->_device, &layoutInfo, nullptr, &layout));

    opaquePipeline.pipelineLayout      = layout;
    transparentPipeline.pipelineLayout = layout;

    // PIPELINE
    VkUtil::PipelineBuilder pipelineBuilder;
    pipelineBuilder.setShaders(vertShader, fragShader);
    pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.setMultiSamplingNone();
    pipelineBuilder.disableBlending();
    pipelineBuilder.enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL);
    pipelineBuilder.setColorAttachmentFormat(engine->_drawImage.imageFormat);
    pipelineBuilder.setDepthAttachmentFormat(engine->_depthImage.imageFormat);
    pipelineBuilder._pipelineLayout = layout;

    opaquePipeline.pipeline = pipelineBuilder.buildPipeline(engine->_device);

    pipelineBuilder.enableBlendingAdditive();
    pipelineBuilder.enableDepthTest(false, VK_COMPARE_OP_LESS_OR_EQUAL);

    transparentPipeline.pipeline = pipelineBuilder.buildPipeline(engine->_device);

    vkDestroyShaderModule(engine->_device, vertShader, nullptr);
    vkDestroyShaderModule(engine->_device, fragShader, nullptr);
}

MaterialInstance GLTFMetallicRoughness::writeMaterial(const VkDevice device, const MaterialPass pass, const MaterialResources &resources, DynamicDescriptorAllocator &descriptorAllocator) {
    MaterialInstance matData;
    matData.passType = pass;
    if (pass == MaterialPass::TRANSPARENT) {
        matData.pipeline = &transparentPipeline;
    } else {
        matData.pipeline = &opaquePipeline;
    }
    matData.materialSet = descriptorAllocator.allocate(device, materialDescriptorLayout);

    writer.clear();
    writer.writeBuffer(0, resources.dataBuffer, sizeof(MaterialConstants), resources.dataBufferOffset, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.writeImage(1, resources.colorImage.imageView, resources.colorSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.writeImage(2, resources.metallicRoughnessImage.imageView, resources.metallicRoughnessSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.updateSet(device, matData.materialSet);

    return matData;
}

void GLTFMetallicRoughness::clearResources(const VkDevice device) const {
    vkDestroyDescriptorSetLayout(device, materialDescriptorLayout, nullptr);
    opaquePipeline.destroy(device, true);
    transparentPipeline.destroy(device);
}