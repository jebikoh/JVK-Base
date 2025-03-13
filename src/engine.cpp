#include <engine.hpp>
#include <jvk.hpp>
#include <jvk/util.hpp>
#include <jvk/init.hpp>
#include <jvk/pipeline.hpp>

#include <SDL.h>
#include <SDL_vulkan.h>

#include "VkBootstrap.h"

#include <thread>

#define VMA_IMPLEMENTATION
#include "mesh.hpp"

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

    window_ = SDL_CreateWindow(
            "JVK",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            windowExtent_.width,
            windowExtent_.height,
            windowFlags);

    initVulkan();
    initSwapchain();
    initDrawImages();
    initCommands();
    initSyncStructures();
    initDescriptors();
    initPipelines();
    initImgui();
    initDefaultData();

    // CAMERA
    mainCamera_.velocity = glm::vec3(0.0f);
    mainCamera_.position = glm::vec3(30.f, -00.f, -085.f);
    mainCamera_.pitch    = 0.0f;
    mainCamera_.yaw      = 0.0f;

    // SCENE
    std::string scenePath = "../assets/sponza.glb";
    auto sceneFile        = loadGLTF(this, scenePath);
    assert(sceneFile.has_value());
    loadedScenes_["base_scene"] = *sceneFile;

    isInitialized_ = true;
    fmt::print("Engine initialized\n");
}

void JVKEngine::cleanup() {
    if (isInitialized_) {
        vkDeviceWaitIdle(ctx_.device);

        loadedScenes_.clear();

        // Frame data
        for (int i = 0; i < JVK_NUM_FRAMES; ++i) {
            frames_[i].cmdPool.destroy();

            // Frame sync
            frames_[i].renderFence.destroy();
            frames_[i].renderSemaphore.destroy();
            frames_[i].swapchainSemaphore.destroy();

            frames_[i].sceneDataBuffer.destroy(allocator_);

            frames_[i].descriptorAllocator.destroyPools(ctx_.device);
        }

        // Textures
        defaultSamplerLinear_.destroy();
        defaultSamplerNearest_.destroy();

        errorCheckerboardImage_.destroy(ctx_, allocator_);
        blackImage_.destroy(ctx_, allocator_);
        whiteImage_.destroy(ctx_, allocator_);

        // Default data
        metallicRoughnessMaterial_.clearResources(ctx_.device);
        matConstants_.destroy(allocator_);

        // ImGui
        ImGui_ImplVulkan_Shutdown();
        vkDestroyDescriptorPool(ctx_.device, imguiPool_, nullptr);

        // Immediate command pool
        immBuffer_.destroy();

        // PIPELINES
        vkDestroyPipelineLayout(ctx_.device, computePipelineLayout_, nullptr);
        vkDestroyPipeline(ctx_.device, computeEffects_[0].pipeline, nullptr);
        vkDestroyPipeline(ctx_.device, computeEffects_[1].pipeline, nullptr);

        // Descriptors
        globalDescriptorAllocator_.destroyPools(ctx_.device);
        vkDestroyDescriptorSetLayout(ctx_.device, drawImageDescriptorLayout_, nullptr);
        vkDestroyDescriptorSetLayout(ctx_.device, sceneDataDescriptorLayout_, nullptr);
        vkDestroyDescriptorSetLayout(ctx_.device, singleImageDescriptorLayout_, nullptr);

        // Depth image
        depthImage_.destroy(ctx_, allocator_);

        // Draw image
        drawImage_.destroy(ctx_, allocator_);

        // VMA
        vmaDestroyAllocator(allocator_);

        // Swapchain
        swapchain_.destroy(ctx_);

        // API
        ctx_.destroy();
        SDL_DestroyWindow(window_);
    }

    loadedEngine = nullptr;
}

void JVKEngine::draw() {
    updateScene();
    // Wait and reset render fence
    VK_CHECK(getCurrentFrame().renderFence.wait());
    getCurrentFrame().descriptorAllocator.clearPools(ctx_.device);

    VK_CHECK(getCurrentFrame().renderFence.reset());

    // Request an image from swapchain
    uint32_t swapchainImageIndex;
    VkResult e = swapchain_.acquireNextImage(ctx_, getCurrentFrame().swapchainSemaphore, &swapchainImageIndex);
    if (e == VK_ERROR_OUT_OF_DATE_KHR) {
        resizeRequested_ = true;
        return;
    }

    // Reset the command buffer
    auto cmd = getCurrentFrame().cmdBuffer;
    VK_CHECK(cmd.reset());

    drawExtent_.width  = std::min(swapchain_.extent.width, drawImage_.imageExtent.width) * renderScale_;
    drawExtent_.height = std::min(swapchain_.extent.height, drawImage_.imageExtent.height) * renderScale_;

    // Start the command buffer
    VK_CHECK(cmd.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT));

    // Transition draw image to general
    jvk::transitionImage(cmd, drawImage_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    ComputeEffect &effect = computeEffects_[currentComputeEffect_];

    // Bind compute pipeline & descriptors
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout_, 0, 1, &drawImageDescriptors_, 0, nullptr);

    // Push constants for compute
    vkCmdPushConstants(cmd, computePipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &effect.data);

    // Draw compute
    vkCmdDispatch(cmd, std::ceil(drawExtent_.width / 16.0f), std::ceil(drawExtent_.height / 16.0f), 1);

    // Transition draw/depth images for render pass
    jvk::transitionImage(cmd, drawImage_.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    jvk::transitionImage(cmd, depthImage_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    drawGeometry(cmd);

    // Transition draw image to transfer source
    jvk::transitionImage(cmd, drawImage_.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    // Transition swapchain image to transfer destination
    jvk::transitionImage(cmd, swapchain_.images[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Copy image to from draw image to swapchain
    jvk::copyImageToImage(cmd, drawImage_.image, swapchain_.images[swapchainImageIndex], drawExtent_, swapchain_.extent);

    // Transition swapchain to attachment optimal
    jvk::transitionImage(cmd, swapchain_.images[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // Draw UI
    drawImgui(cmd, swapchain_.imageViews[swapchainImageIndex]);

    // Transition swapchain for presentation
    jvk::transitionImage(cmd, swapchain_.images[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    // End command buffer
    VK_CHECK(cmd.end());

    // Submit buffer
    // srcStageMask set to COLOR_ATTACHMENT_OUTPUT_BIT to wait for color attachment output (waiting for swapchain image)
    // dstStageMask set to ALL_GRAPHICS_BIT to signal that all graphics stages are done
    VkCommandBufferSubmitInfo cmdInfo = cmd.submitInfo();
    VkSemaphoreSubmitInfo waitInfo    = getCurrentFrame().swapchainSemaphore.submitInfo(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    VkSemaphoreSubmitInfo signalInfo  = getCurrentFrame().renderSemaphore.submitInfo(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
    graphicsQueue_.submit(&cmdInfo, &waitInfo, &signalInfo, getCurrentFrame().renderFence);

    // Present
    VkPresentInfoKHR presentInfo   = {};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext              = nullptr;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &swapchain_.swapchain;
    presentInfo.pWaitSemaphores    = &getCurrentFrame().renderSemaphore.semaphore;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pImageIndices      = &swapchainImageIndex;


    VkResult presentResult = vkQueuePresentKHR(graphicsQueue_, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR) {
        resizeRequested_ = true;
    }

    frameNumber_++;
}

void JVKEngine::run() {
    SDL_Event e;
    bool bQuit = false;

    while (!bQuit) {
        auto start = std::chrono::system_clock::now();

        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) bQuit = true;

            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && !ImGui::GetIO().WantCaptureMouse) {
                SDL_SetRelativeMouseMode(SDL_TRUE);
            }

            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
                SDL_SetRelativeMouseMode(SDL_FALSE);
            }

            if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                    stopRendering_ = true;
                }
                if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
                    stopRendering_ = false;
                }
            }

            if (SDL_GetRelativeMouseMode() == SDL_TRUE && !ImGui::GetIO().WantCaptureMouse) {
                mainCamera_.processSDLEvent(e);
            }
            ImGui_ImplSDL2_ProcessEvent(&e);
        }

        if (stopRendering_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (resizeRequested_) {
            resizeSwapchain();
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();

        ImGui::NewFrame();

        ImGui::Begin("Control Panel");

        if (ImGui::BeginTabBar("MainTabs"))
        {
            if (ImGui::BeginTabItem("Stats"))
            {
                ImGui::Text("Frame time %f ms", stats_.frameTime);
                ImGui::Text("Draw time %f ms", stats_.meshDrawTime);
                ImGui::Text("Update time %f ms", stats_.sceneUpdateTime);
                ImGui::Text("Triangles %i", stats_.triangleCount);
                ImGui::Text("Draws %i", stats_.drawCallCount);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Camera"))
            {
                ImGui::SliderFloat("Speed", &mainCamera_.speed, 0.0f, 1000.0f);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Compute Effects"))
            {
                ImGui::SliderFloat("Render Scale", &renderScale_, 0.3f, 1.0f);

                ComputeEffect &selected = computeEffects_[currentComputeEffect_];

                ImGui::Text("Selected effect: %s", selected.name); // Changed to %s for string
                ImGui::SliderInt("Effect Index", &currentComputeEffect_, 0, computeEffects_.size() - 1);

                ImGui::InputFloat4("Input 1", reinterpret_cast<float *>(&selected.data.data1));
                ImGui::InputFloat4("Input 2", reinterpret_cast<float *>(&selected.data.data2));
                ImGui::InputFloat4("Input 3", reinterpret_cast<float *>(&selected.data.data3));
                ImGui::InputFloat4("Input 4", reinterpret_cast<float *>(&selected.data.data4));
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();

        ImGui::Render();

        draw();

        auto end         = std::chrono::system_clock::now();
        auto elapsed     = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        stats_.frameTime = elapsed.count() / 1000.0f;
        deltaTime_       = stats_.frameTime / 1000.0f;
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

    ctx_.instance       = vkbInstance.instance;
    ctx_.debugMessenger = vkbInstance.debug_messenger;

    // CREATE SURFACE
    SDL_Vulkan_CreateSurface(window_, ctx_, &ctx_.surface);

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
                                           .set_surface(ctx_)
                                           .select();

    if (!vkbPhysicalDeviceResult) {
        fmt::println("Failed to select physical device. Error: {}", vkbPhysicalDeviceResult.error().message());
        abort();
    }

    vkb::PhysicalDevice vkbPhysicalDevice = vkbPhysicalDeviceResult.value();

    // DEVICE
    vkb::DeviceBuilder deviceBuilder{vkbPhysicalDevice};
    vkb::Device vkbDevice   = deviceBuilder.build().value();
    ctx_.device         = vkbDevice.device;
    ctx_.physicalDevice = vkbPhysicalDevice.physical_device;

    // QUEUE
    graphicsQueue_.queue  = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    graphicsQueue_.family = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    // VMA
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice         = ctx_.physicalDevice;
    allocatorInfo.device                 = ctx_.device;
    allocatorInfo.instance               = ctx_.instance;
    allocatorInfo.flags                  = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocatorInfo, &allocator_);

    // MSAA
    maxMsaaSamples_ = getMaxUsableSampleCount();
}

void JVKEngine::initSwapchain() {
    swapchain_.init(ctx_, windowExtent_.width, windowExtent_.height);
}

void JVKEngine::initCommands() {
    // COMMAND POOL
    // Indicate that buffers should be individually resettable
    VkCommandPoolCreateFlags flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    // COMMAND BUFFERS
    for (int i = 0; i < JVK_NUM_FRAMES; ++i) {
        VK_CHECK(frames_[i].cmdPool.init(ctx_, graphicsQueue_.family, flags));
        VK_CHECK(frames_[i].cmdPool.allocateCommandBuffer(&frames_[i].cmdBuffer));
    }

    // IMMEDIATE BUFFERS
    VK_CHECK(immBuffer_.init(ctx_, graphicsQueue_.family, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT));
}

void JVKEngine::initSyncStructures() {
    for (int i = 0; i < JVK_NUM_FRAMES; ++i) {
        VK_CHECK(frames_[i].renderFence.init(ctx_, VK_FENCE_CREATE_SIGNALED_BIT));
        VK_CHECK(frames_[i].swapchainSemaphore.init(ctx_));
        VK_CHECK(frames_[i].renderSemaphore.init(ctx_));
    }
}

void JVKEngine::drawBackground(VkCommandBuffer cmd) const {
    VkClearColorValue clearValue;
    float flash = std::abs(std::sin(frameNumber_ / 120.0f));
    clearValue  = {{0.0f, 0.0f, flash, 1.0f}};

    VkImageSubresourceRange clearRange = jvk::init::imageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);

    vkCmdClearColorImage(cmd, drawImage_.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);
}

void JVKEngine::initDescriptors() {
    // GLOBAL DESCRIPTOR ALLOCATOR
    std::vector<jvk::DynamicDescriptorAllocator::PoolSizeRatio> sizes =
            {
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
                    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};

    globalDescriptorAllocator_.init(ctx_.device, 10, sizes);

    // DRAW IMAGE
    // Layout
    {
        jvk::DescriptorLayoutBuilder builder;
        builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        drawImageDescriptorLayout_ = builder.build(ctx_.device, VK_SHADER_STAGE_COMPUTE_BIT);
    }

    // DRAW IMAGE DESCRIPTOR
    {
        // Allocate set
        drawImageDescriptors_ = globalDescriptorAllocator_.allocate(ctx_.device, drawImageDescriptorLayout_);

        // Write to set
        jvk::DescriptorWriter writer;
        writer.writeImage(0, drawImage_.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        writer.updateSet(ctx_.device, drawImageDescriptors_);
    }

    // GPU SCENE DATA
    {
        jvk::DescriptorLayoutBuilder builder;
        builder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        sceneDataDescriptorLayout_ = builder.build(ctx_.device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    // FRAME DESCRIPTORS
    for (int i = 0; i < JVK_NUM_FRAMES; ++i) {
        std::vector<jvk::DynamicDescriptorAllocator::PoolSizeRatio> frameSizes = {
                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
        };

        frames_[i].descriptorAllocator = jvk::DynamicDescriptorAllocator();
        frames_[i].descriptorAllocator.init(ctx_.device, 1000, frameSizes);

        // Allocate scene data descriptor set
        frames_[i].sceneDataDescriptorSet = globalDescriptorAllocator_.allocate(ctx_, sceneDataDescriptorLayout_);

        // Create corresponding buffer
        frames_[i].sceneDataBuffer = createBuffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    }

    // TEXTURES
    {
        jvk::DescriptorLayoutBuilder builder;
        builder.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        singleImageDescriptorLayout_ = builder.build(ctx_.device, VK_SHADER_STAGE_FRAGMENT_BIT);
    }
}

void JVKEngine::initPipelines() {
    initBackgroundPipelines();
    metallicRoughnessMaterial_.buildPipelines(this);
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
    layout.pSetLayouts            = &drawImageDescriptorLayout_;
    layout.setLayoutCount         = 1;
    layout.pPushConstantRanges    = &pushConstant;
    layout.pushConstantRangeCount = 1;
    VK_CHECK(vkCreatePipelineLayout(ctx_.device, &layout, nullptr, &computePipelineLayout_));

    // PIPELINE STAGES (AND SHADERS)
    VkShaderModule gradientShader;
    if (!jvk::loadShaderModule("../shaders/gradient_pc.comp.spv", ctx_.device, &gradientShader)) {
        fmt::print("Error when building gradient compute shader \n");
    }

    VkShaderModule skyShader;
    if (!jvk::loadShaderModule("../shaders/sky.comp.spv", ctx_.device, &skyShader)) {
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
    computeInfo.layout = computePipelineLayout_;
    computeInfo.stage  = stageInfo;

    ComputeEffect gradient;
    gradient.layout     = computePipelineLayout_;
    gradient.name       = "gradient";
    gradient.data       = {};
    gradient.data.data1 = glm::vec4(1, 0, 0, 1);
    gradient.data.data2 = glm::vec4(0, 0, 1, 1);

    VK_CHECK(vkCreateComputePipelines(ctx_.device, VK_NULL_HANDLE, 1, &computeInfo, nullptr, &gradient.pipeline));

    // CREATE SKY PIPELINE
    computeInfo.stage.module = skyShader;

    ComputeEffect sky;
    sky.layout     = computePipelineLayout_;
    sky.name       = "sky";
    sky.data       = {};
    sky.data.data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);
    VK_CHECK(vkCreateComputePipelines(ctx_.device, VK_NULL_HANDLE, 1, &computeInfo, nullptr, &sky.pipeline));

    computeEffects_.push_back(gradient);
    computeEffects_.push_back(sky);

    vkDestroyShaderModule(ctx_.device, gradientShader, nullptr);
    vkDestroyShaderModule(ctx_.device, skyShader, nullptr);
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

    VK_CHECK(vkCreateDescriptorPool(ctx_.device, &poolInfo, nullptr, &imguiPool_));

    ImGui::CreateContext();
    ImGui_ImplSDL2_InitForVulkan(window_);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance            = ctx_;
    initInfo.PhysicalDevice      = ctx_;
    initInfo.Device              = ctx_;
    initInfo.Queue               = graphicsQueue_;
    initInfo.DescriptorPool      = imguiPool_;
    initInfo.MinImageCount       = 3;
    initInfo.ImageCount          = 3;
    initInfo.UseDynamicRendering = true;

    initInfo.PipelineRenderingCreateInfo                         = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    initInfo.PipelineRenderingCreateInfo.colorAttachmentCount    = 1;
    initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchain_.imageFormat;

    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&initInfo);
    ImGui_ImplVulkan_CreateFontsTexture();
}

void JVKEngine::drawImgui(VkCommandBuffer cmd, VkImageView targetImageView) const {
    // Setup color attachment for render pass
    VkRenderingAttachmentInfo colorAttachment = jvk::init::renderingAttachment(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderInfo                = jvk::init::rendering(swapchain_.extent, &colorAttachment, nullptr);

    // Render
    vkCmdBeginRendering(cmd, &renderInfo);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRendering(cmd);
}

void JVKEngine::drawGeometry(VkCommandBuffer cmd) {
    stats_.drawCallCount = 0;
    stats_.triangleCount = 0;
    auto start           = std::chrono::system_clock::now();

    // SORT DRAWS
    std::vector<uint32_t> opaqueDraws;
    opaqueDraws.reserve(drawCtx_.opaqueSurfaces.size());
    for (uint32_t i = 0; i < drawCtx_.opaqueSurfaces.size(); ++i) {
        opaqueDraws.push_back(i);
    }

    std::sort(opaqueDraws.begin(), opaqueDraws.end(), [&](const auto &iA, const auto &iB) {
        const RenderObject &A = drawCtx_.opaqueSurfaces[iA];
        const RenderObject &B = drawCtx_.opaqueSurfaces[iB];

        if (A.material == B.material) {
            return A.indexBuffer < B.indexBuffer;
        }
        return A.material < B.material;
    });

    // SETUP RENDER PASS
    VkRenderingAttachmentInfo colorAttachment = jvk::init::renderingAttachment(drawImage_.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo depthAttachment = jvk::init::depthRenderingAttachment(depthImage_.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderingInfo             = jvk::init::rendering(drawExtent_, &colorAttachment, &depthAttachment);

    // BEGIN RENDER PASS
    vkCmdBeginRendering(cmd, &renderingInfo);

    // UNIFORM BUFFERS & GLOBAL DESCRIPTOR SET
    // Contains global scene data (projection matrices, light, etc)
    jvk::Buffer sceneDataBuffer = getCurrentFrame().sceneDataBuffer;
    GPUSceneData *sceneUniformData     = static_cast<GPUSceneData *>(sceneDataBuffer.allocation->GetMappedData());
    *sceneUniformData                  = sceneData_;

    jvk::DescriptorWriter writer;
    writer.writeBuffer(0, sceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.updateSet(ctx_.device, getCurrentFrame().sceneDataDescriptorSet);

    MaterialPipeline *lastPipeline = nullptr;
    MaterialInstance *lastMaterial = nullptr;
    VkBuffer lastIndexBuffer       = VK_NULL_HANDLE;

    auto draw = [&](const RenderObject &r) {
        if (r.material != lastMaterial) {
            lastMaterial = r.material;

            // Rebind pipeline and global descriptors if material is different
            if (r.material->pipeline != lastPipeline) {
                lastPipeline = r.material->pipeline;

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->pipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->pipelineLayout, 0, 1, &getCurrentFrame().sceneDataDescriptorSet, 0, nullptr);

                VkViewport viewport{};
                viewport.x        = 0;
                viewport.y        = 0;
                viewport.width    = drawExtent_.width;
                viewport.height   = drawExtent_.height;
                viewport.minDepth = 0.0f;
                viewport.maxDepth = 1.0f;
                vkCmdSetViewport(cmd, 0, 1, &viewport);

                // SCISSOR
                VkRect2D scissor{};
                scissor.offset.x      = 0;
                scissor.offset.y      = 0;
                scissor.extent.width  = drawExtent_.width;
                scissor.extent.height = drawExtent_.height;
                vkCmdSetScissor(cmd, 0, 1, &scissor);
            }

            // Bind material descriptor set
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->pipelineLayout, 1, 1, &r.material->materialSet, 0, nullptr);
        }

        // Bind index buffer
        if (r.indexBuffer != lastIndexBuffer) {
            lastIndexBuffer = r.indexBuffer;
            vkCmdBindIndexBuffer(cmd, r.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        }

        // Push constants
        GPUDrawPushConstants pushConstants;
        pushConstants.vertexBuffer = r.vertexBufferAddress;
        pushConstants.worldMatrix  = r.transform;
        vkCmdPushConstants(cmd, r.material->pipeline->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);

        // Draw
        vkCmdDrawIndexed(cmd, r.indexCount, 1, r.firstIndex, 0, 0);

        stats_.drawCallCount++;
        stats_.triangleCount += r.indexCount / 3;
    };

    for (const auto &r: opaqueDraws) {
        draw(drawCtx_.opaqueSurfaces[r]);
    }

    for (const RenderObject &r: drawCtx_.transparentSurfaces) {
        draw(r);
    }

    vkCmdEndRendering(cmd);

    auto end            = std::chrono::system_clock::now();
    auto elapsed        = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    stats_.meshDrawTime = elapsed.count() / 1000.0f;
}

jvk::Buffer JVKEngine::createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) const {
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.pNext = nullptr;
    info.size  = allocSize;
    info.usage = usage;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    jvk::Buffer buffer;
    VK_CHECK(vmaCreateBuffer(allocator_, &info, &allocInfo, &buffer.buffer, &buffer.allocation, &buffer.info));
    return buffer;
}

void JVKEngine::destroyBuffer(const jvk::Buffer &buffer) const {
    buffer.destroy(allocator_);
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
    surface.vertexBufferAddress = vkGetBufferDeviceAddress(ctx_.device, &deviceAddressInfo);

    // Index buffer
    surface.indexBuffer = createBuffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

    // STAGING BUFFER
    jvk::Buffer staging = createBuffer(vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    void *data              = staging.allocation->GetMappedData();

    // COPY DATA TO STAGING BUFFER
    memcpy(data, vertices.data(), vertexBufferSize);
    memcpy(static_cast<char *>(data) + vertexBufferSize, indices.data(), indexBufferSize);

    // COPY TO GPU BUFFER
    immBuffer_.submit(graphicsQueue_, [&](VkCommandBuffer cmd) {
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
    whiteImage_    = createImage((void *) &white, VkExtent3D{1, 1, 1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 1.0f));
    blackImage_    = createImage((void *) &black, VkExtent3D{1, 1, 1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    //checkerboard image
    uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
    std::array<uint32_t, 16 * 16> pixels;
    for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 16; y++) {
            pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
        }
    }
    errorCheckerboardImage_ = createImage(pixels.data(), VkExtent3D{16, 16, 1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    // SAMPLERS
    VK_CHECK(defaultSamplerNearest_.init(ctx_, VK_FILTER_NEAREST, VK_FILTER_NEAREST));
    VK_CHECK(defaultSamplerLinear_.init(ctx_, VK_FILTER_LINEAR, VK_FILTER_LINEAR));

    // MATERIALS
    GLTFMetallicRoughness::MaterialResources matResources;
    matResources.colorImage               = whiteImage_;
    matResources.colorSampler             = defaultSamplerLinear_;
    matResources.metallicRoughnessImage   = whiteImage_;
    matResources.metallicRoughnessSampler = defaultSamplerLinear_;

    matConstants_                                              = createBuffer(sizeof(GLTFMetallicRoughness::MaterialConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    GLTFMetallicRoughness::MaterialConstants *sceneUniformData = static_cast<GLTFMetallicRoughness::MaterialConstants *>(matConstants_.allocation->GetMappedData());
    sceneUniformData->colorFactors                             = glm::vec4{1, 1, 1, 1};
    sceneUniformData->metallicRoughnessFactors                 = glm::vec4{1, 0.5, 0, 0};

    matResources.dataBuffer       = matConstants_.buffer;
    matResources.dataBufferOffset = 0;

    defaultMaterialData_ = metallicRoughnessMaterial_.writeMaterial(ctx_.device, MaterialPass::MAIN_COLOR, matResources, globalDescriptorAllocator_);
}

void JVKEngine::resizeSwapchain() {
    vkDeviceWaitIdle(ctx_);
    swapchain_.destroy(ctx_);

    int w, h;
    SDL_GetWindowSize(window_, &w, &h);
    windowExtent_.width  = w;
    windowExtent_.height = h;

    swapchain_.init(ctx_, windowExtent_.width, windowExtent_.height);
    resizeRequested_ = false;
}

jvk::Image JVKEngine::createImage(const VkExtent3D size, const VkFormat format, const VkImageUsageFlags usage, const bool mipmapped, const VkSampleCountFlagBits sampleCount) const {
    // IMAGE
    jvk::Image image;
    image.imageFormat           = format;
    image.imageExtent           = size;
    VkImageCreateInfo imageInfo = jvk::init::image(format, usage, size, sampleCount);
    if (mipmapped) {
        imageInfo.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;
    }

    // ALLOCATE
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage         = VMA_MEMORY_USAGE_GPU_ONLY;
    allocInfo.requiredFlags = static_cast<VkMemoryPropertyFlags>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vmaCreateImage(allocator_, &imageInfo, &allocInfo, &image.image, &image.allocation, nullptr));

    // DEPTH
    VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
    if (format == VK_FORMAT_D32_SFLOAT) {
        aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    // IMAGE VIEW
    VkImageViewCreateInfo viewInfo       = jvk::init::imageView(format, image.image, aspectFlag);
    viewInfo.subresourceRange.levelCount = imageInfo.mipLevels;
    VK_CHECK(vkCreateImageView(ctx_.device, &viewInfo, nullptr, &image.imageView));

    return image;
}

jvk::Image JVKEngine::createImage(void *data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped) const {
    // STAGING BUFFER
    size_t dataSize              = size.depth * size.width * size.height * 4;
    jvk::Buffer uploadBuffer = createBuffer(dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    memcpy(uploadBuffer.info.pMappedData, data, dataSize);

    // COPY TO IMAGE
    VkImageUsageFlags imgUsages = VK_IMAGE_USAGE_TRANSFER_DST_BIT | usage;
    if (mipmapped) {
        imgUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    jvk::Image image = createImage(size, format, imgUsages, mipmapped);
    immBuffer_.submit(graphicsQueue_.queue, [&](VkCommandBuffer cmd) {
        jvk::transitionImage(cmd, image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

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

        if (mipmapped) {
            jvk::generateMipmaps(cmd, image.image, {image.imageExtent.width, image.imageExtent.height});
        } else {
            jvk::transitionImage(cmd, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    });

    destroyBuffer(uploadBuffer);
    return image;
}

void JVKEngine::updateScene() {
    auto start = std::chrono::system_clock::now();

    mainCamera_.update(deltaTime_);
    glm::mat4 view = mainCamera_.getViewMatrix();
    glm::mat4 proj = glm::perspective(glm::radians(70.f), static_cast<float>(windowExtent_.width) / static_cast<float>(windowExtent_.height), 0.1f, 10000.0f);
    proj[1][1] *= -1;

    drawCtx_.opaqueSurfaces.clear();
    drawCtx_.transparentSurfaces.clear();
    loadedScenes_["base_scene"]->draw(glm::mat4(1.0f), drawCtx_);

    sceneData_.view              = view;
    sceneData_.proj              = proj;
    sceneData_.viewProj          = sceneData_.proj * sceneData_.view;
    sceneData_.ambientColor      = glm::vec4(0.1f);
    sceneData_.sunlightColor     = glm::vec4(1.0f);
    sceneData_.sunlightDirection = glm::vec4(0, 1, 0.5, 1.0f);

    auto end               = std::chrono::system_clock::now();
    auto elapsed           = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    stats_.sceneUpdateTime = elapsed.count() / 1000.0f;
}

VkSampleCountFlagBits JVKEngine::getMaxUsableSampleCount() {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(ctx_, &props);

    VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
    if (counts & VK_SAMPLE_COUNT_64_BIT) return VK_SAMPLE_COUNT_64_BIT;
    if (counts & VK_SAMPLE_COUNT_32_BIT) return VK_SAMPLE_COUNT_32_BIT;
    if (counts & VK_SAMPLE_COUNT_16_BIT) return VK_SAMPLE_COUNT_16_BIT;
    if (counts & VK_SAMPLE_COUNT_8_BIT) return VK_SAMPLE_COUNT_8_BIT;
    if (counts & VK_SAMPLE_COUNT_4_BIT) return VK_SAMPLE_COUNT_4_BIT;
    if (counts & VK_SAMPLE_COUNT_2_BIT) return VK_SAMPLE_COUNT_2_BIT;
    return VK_SAMPLE_COUNT_1_BIT;
}

void JVKEngine::initDrawImages() {
    VkExtent3D drawImageExtent = {
            windowExtent_.width,
            windowExtent_.height,
            1};

    drawImage_.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;// 16-bit float image
    drawImage_.imageExtent = drawImageExtent;

    VkImageUsageFlags drawImageUsages = {};
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;    // Copy from image
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;    // Copy to image
    drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;         // Allow compute shader to write
    drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;// Graphics pipeline

    VkImageCreateInfo drawImageInfo = jvk::init::image(drawImage_.imageFormat, drawImageUsages, drawImageExtent);

    // VMA_MEMORY_USAGE_GPU_ONLY: the texture will never be accessed from the CPU
    // VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT: GPU exclusive memory flag, guarantees that the memory is on the GPU
    VmaAllocationCreateInfo drawImageAllocInfo = {};
    drawImageAllocInfo.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;
    drawImageAllocInfo.requiredFlags           = static_cast<VkMemoryPropertyFlags>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vmaCreateImage(allocator_, &drawImageInfo, &drawImageAllocInfo, &drawImage_.image, &drawImage_.allocation, nullptr);

    VkImageViewCreateInfo imageViewInfo = jvk::init::imageView(drawImage_.imageFormat, drawImage_.image, VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK(vkCreateImageView(ctx_.device, &imageViewInfo, nullptr, &drawImage_.imageView));

    // CREATE DEPTH IMAGE
    depthImage_.imageFormat = VK_FORMAT_D32_SFLOAT;
    depthImage_.imageExtent = drawImageExtent;

    VkImageUsageFlags depthImageUsages{};
    depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VkImageCreateInfo depthImageInfo = jvk::init::image(depthImage_.imageFormat, depthImageUsages, drawImageExtent);
    vmaCreateImage(allocator_, &depthImageInfo, &drawImageAllocInfo, &depthImage_.image, &depthImage_.allocation, nullptr);

    VkImageViewCreateInfo depthImageViewInfo = jvk::init::imageView(depthImage_.imageFormat, depthImage_.image, VK_IMAGE_ASPECT_DEPTH_BIT);
    VK_CHECK(vkCreateImageView(ctx_.device, &depthImageViewInfo, nullptr, &depthImage_.imageView));
}

void JVKEngine::destroyImage(const jvk::Image &image) const {
    image.destroy(ctx_, allocator_);
}

void MeshNode::draw(const glm::mat4 &topMatrix, DrawContext &ctx) {
    glm::mat4 nodeMatrix = topMatrix * worldTransform;

    for (auto &s: mesh->surfaces) {
        RenderObject rObj;
        rObj.indexCount  = s.count;
        rObj.firstIndex  = s.startIndex;
        rObj.indexBuffer = mesh->meshBuffers.indexBuffer.buffer;
        rObj.material    = &s.material->data;

        rObj.transform           = nodeMatrix;
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
    if (!jvk::loadShaderModule("../shaders/mesh.vert.spv", engine->ctx_.device, &vertShader)) {
        fmt::print("Error when building vertex shader module");
    }
    VkShaderModule fragShader;
    if (!jvk::loadShaderModule("../shaders/mesh.frag.spv", engine->ctx_.device, &fragShader)) {
        fmt::print("Error when building fragment shader module");
    }

    // PUSH CONSTANTS
    VkPushConstantRange matrixRange{};
    matrixRange.offset     = 0;
    matrixRange.size       = sizeof(GPUDrawPushConstants);
    matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // DESCRIPTOR LAYOUT
    jvk::DescriptorLayoutBuilder builder;
    builder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    builder.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    builder.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    materialDescriptorLayout = builder.build(engine->ctx_.device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    // _gpuSceneDataDescriptorLayout is used as our global descriptor layout
    VkDescriptorSetLayout layouts[] = {engine->sceneDataDescriptorLayout_, materialDescriptorLayout};

    // PIPELINE LAYOUT
    VkPipelineLayoutCreateInfo layoutInfo = jvk::init::pipelineLayout();
    layoutInfo.setLayoutCount             = 2;
    layoutInfo.pSetLayouts                = layouts;
    layoutInfo.pPushConstantRanges        = &matrixRange;
    layoutInfo.pushConstantRangeCount     = 1;

    VkPipelineLayout layout;
    VK_CHECK(vkCreatePipelineLayout(engine->ctx_.device, &layoutInfo, nullptr, &layout));

    opaquePipeline.pipelineLayout      = layout;
    transparentPipeline.pipelineLayout = layout;

    // PIPELINE
    jvk::PipelineBuilder pipelineBuilder;
    pipelineBuilder.setShaders(vertShader, fragShader);
    pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.setMultiSamplingNone();
    pipelineBuilder.disableBlending();
    pipelineBuilder.enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL);
    pipelineBuilder.setColorAttachmentFormat(engine->drawImage_.imageFormat);
    pipelineBuilder.setDepthAttachmentFormat(engine->depthImage_.imageFormat);
    pipelineBuilder._pipelineLayout = layout;

    opaquePipeline.pipeline = pipelineBuilder.buildPipeline(engine->ctx_.device);

    pipelineBuilder.enableBlendingAdditive();
    pipelineBuilder.enableDepthTest(false, VK_COMPARE_OP_LESS_OR_EQUAL);

    transparentPipeline.pipeline = pipelineBuilder.buildPipeline(engine->ctx_.device);

    vkDestroyShaderModule(engine->ctx_.device, vertShader, nullptr);
    vkDestroyShaderModule(engine->ctx_.device, fragShader, nullptr);
}

MaterialInstance GLTFMetallicRoughness::writeMaterial(const VkDevice device, const MaterialPass pass, const MaterialResources &resources, jvk::DynamicDescriptorAllocator &descriptorAllocator) {
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