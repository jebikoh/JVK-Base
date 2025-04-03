#define VOLK_IMPLEMENTATION
#include <jvk.hpp>
#include <jvk/init.hpp>
#include <jvk/pipeline.hpp>
#include <jvk/util.hpp>

#include <engine.hpp>
#include <scene.hpp>

#include <thread>

#include <SDL.h>
#include <SDL_vulkan.h>

#include <VkBootstrap.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>

constexpr bool JVK_USE_VALIDATION_LAYERS = true;

JVKEngine *loadedEngine = nullptr;

std::optional<jvk::Image> loadImage(const JVKEngine *engine, const std::string &path) {
    jvk::Image newImage{};

    int width, height, nrChannels;
    if (unsigned char *data = stbi_load(path.c_str(), &width, &height, &nrChannels, 4)) {
        VkExtent3D imageSize;
        imageSize.width  = width;
        imageSize.height = height;
        imageSize.depth  = 1;

        newImage = engine->createImage(data, imageSize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, JVK_LOADER_GENERATE_MIPMAPS);
        stbi_image_free(data);
    }

    if (newImage.image == VK_NULL_HANDLE) {
        return {};
    }
    return newImage;
}
JVKEngine &JVKEngine::get() {
    return *loadedEngine;
}

void JVKEngine::init() {
    LOG_INFO("Initializing engine");
    assert(loadedEngine == nullptr);
    loadedEngine = this;

    SDL_Init(SDL_INIT_VIDEO);

    auto windowFlags = static_cast<SDL_WindowFlags>(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    window_ = SDL_CreateWindow(
            "JVK",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            static_cast<int>(windowExtent_.width),
            static_cast<int>(windowExtent_.height),
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
    mainCamera_.position = glm::vec3(1.0f, 1.0f, 1.0f);
    mainCamera_.pitch    = 0.0f;
    mainCamera_.yaw      = 0.0f;

    // SCENE
//     const std::string scenePath = "../assets/duck.glb";
//     const auto sceneFile        = loadGLTF(this, scenePath);

    const std::string scenePath = "../assets/backpack/backpack.obj";
    const auto sceneFile        = loadOBJ(this, scenePath);

//    const std::string scenePath = "../assets/sponza/sponza.obj";
//    const auto sceneFile        = loadOBJ(this, scenePath);

    assert(sceneFile.has_value());
    loadedScenes_["base_scene"] = *sceneFile;

    isInitialized_ = true;
    LOG_INFO("Engine initialized");
}

void JVKEngine::cleanup() {
    if (isInitialized_) {
        LOG_INFO("Terminating engine");
        vkDeviceWaitIdle(ctx_.device);

        LOG_INFO("Destroying scene resources");
        loadedScenes_.clear();

        LOG_INFO("Destroying engine resources");
        // Frame data
        for (auto &frame: frames_) {
            frame.cmdPool.destroy();

            // Frame sync
            frame.renderFence.destroy();
            frame.renderSemaphore.destroy();
            frame.swapchainSemaphore.destroy();

            frame.sceneDataBuffer.destroy(allocator_);

            frame.descriptorAllocator.destroyPools(ctx_.device);
        }

        // Textures
        defaultSamplerLinear_.destroy();
        defaultSamplerNearest_.destroy();

        errorCheckerboardImage_.destroy(ctx_, allocator_);
        blackImage_.destroy(ctx_, allocator_);
        whiteImage_.destroy(ctx_, allocator_);

        lightbulbImage_.destroy(ctx_, allocator_);
        sunImage_.destroy(ctx_, allocator_);

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
        billboardPipeline_.destroy(ctx_, true);

        // Descriptors
        globalDescriptorAllocator_.destroyPools(ctx_.device);
        vkDestroyDescriptorSetLayout(ctx_.device, drawImageDescriptorLayout_, nullptr);
        vkDestroyDescriptorSetLayout(ctx_.device, sceneDataDescriptorLayout_, nullptr);
        vkDestroyDescriptorSetLayout(ctx_.device, singleImageDescriptorLayout_, nullptr);
        vkDestroyDescriptorSetLayout(ctx_.device, billboardDescriptorLayout_, nullptr);

        // Depth image
        depthStencilImage_.destroy(ctx_, allocator_);

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

    LOG_INFO("Engine terminated");
    loadedEngine = nullptr;
}

void JVKEngine::draw() {
    updateScene();
    // Wait and reset render fence
    CHECK_VK(getCurrentFrame().renderFence.wait());
    getCurrentFrame().descriptorAllocator.clearPools(ctx_.device);

    CHECK_VK(getCurrentFrame().renderFence.reset());

    // Request an image from swapchain
    uint32_t swapchainImageIndex;
    VkResult e = swapchain_.acquireNextImage(ctx_, getCurrentFrame().swapchainSemaphore, &swapchainImageIndex);
    if (e == VK_ERROR_OUT_OF_DATE_KHR) {
        resizeRequested_ = true;
        return;
    }

    // Reset the command buffer
    auto cmd = getCurrentFrame().cmdBuffer;
    CHECK_VK(cmd.reset());

    drawExtent_.width  = static_cast<uint32_t>(static_cast<float>(std::min(swapchain_.extent.width, drawImage_.imageExtent.width)) * renderScale_);
    drawExtent_.height = static_cast<uint32_t>(static_cast<float>(std::min(swapchain_.extent.height, drawImage_.imageExtent.height)) * renderScale_);

    // Start the command buffer
    CHECK_VK(cmd.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT));

    // Transition draw image to general
    jvk::transitionImage(cmd, drawImage_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    ComputeEffect &effect = computeEffects_[currentComputeEffect_];

    // Bind compute pipeline & descriptors
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout_, 0, 1, &drawImageDescriptors_, 0, nullptr);

    // Push constants for compute
    vkCmdPushConstants(cmd, computePipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &effect.data);

    // Draw compute
    vkCmdDispatch(cmd, std::ceil(static_cast<float>(drawExtent_.width) / 16.0f), std::ceil(static_cast<float>(drawExtent_.height) / 16.0f), 1);

    // Transition draw/depth images for render pass
    jvk::transitionImage(cmd, drawImage_.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    jvk::transitionImage(cmd, depthStencilImage_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

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
    CHECK_VK(cmd.end());

    // Submit buffer
    // srcStageMask set to COLOR_ATTACHMENT_OUTPUT_BIT to wait for color attachment output (waiting for swapchain image)
    // dstStageMask set to ALL_GRAPHICS_BIT to signal that all graphics stages are done
    VkCommandBufferSubmitInfoKHR cmdInfo = cmd.submitInfo();
    VkSemaphoreSubmitInfoKHR waitInfo    = getCurrentFrame().swapchainSemaphore.submitInfo(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR);
    VkSemaphoreSubmitInfoKHR signalInfo  = getCurrentFrame().renderSemaphore.submitInfo(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT_KHR);
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

        if (ImGui::BeginTabBar("MainTabs")) {
            if (ImGui::BeginTabItem("Stats")) {
                ImGui::Text("Frame time %f ms", stats_.frameTime);
                ImGui::Text("Draw time %f ms", stats_.meshDrawTime);
                ImGui::Text("Update time %f ms", stats_.sceneUpdateTime);
                ImGui::Text("Triangles %i", stats_.triangleCount);
                ImGui::Text("Draws %i", stats_.drawCallCount);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Camera")) {
                ImGui::SliderFloat("Speed", &mainCamera_.speed, 0.0f, 1000.0f);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Compute Effects")) {
                ImGui::SliderFloat("Render Scale", &renderScale_, 0.3f, 1.0f);

                ComputeEffect &selected = computeEffects_[currentComputeEffect_];

                ImGui::Text("Selected effect: %s", selected.name);// Changed to %s for string
                ImGui::SliderInt("Effect Index", &currentComputeEffect_, 0, static_cast<int>(computeEffects_.size()) - 1);

                ImGui::InputFloat4("Input 1", reinterpret_cast<float *>(&selected.data.data1));
                ImGui::InputFloat4("Input 2", reinterpret_cast<float *>(&selected.data.data2));
                ImGui::InputFloat4("Input 3", reinterpret_cast<float *>(&selected.data.data3));
                ImGui::InputFloat4("Input 4", reinterpret_cast<float *>(&selected.data.data4));
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Lights")) {
                ImGui::Checkbox("Enable Spotlight", &enableSpotlight_);
                ImGui::ColorEdit3("Icon Color", &billboardColor_.x);

                ImGui::Text("Sun");
                ImGui::DragFloat3("Position##Sun", &sceneData_.dirLight.position.x);
                ImGui::DragFloat3("Direction##Sun", &sceneData_.dirLight.direction.x);
                ImGui::ColorEdit3("Diffuse##Sun", &sceneData_.dirLight.diffuse.x);
                ImGui::ColorEdit3("Ambient##Sun", &sceneData_.dirLight.ambient.x);
                ImGui::ColorEdit3("Specular##Sun", &sceneData_.dirLight.specular.x);

                for (int i = 0; i < 2; ++i) {
                    ImGui::Text("Light %i", i);
                    ImGui::DragFloat3(("Position##Light" + std::to_string(i)).c_str(), &sceneData_.pointLights[i].position.x);
                    ImGui::ColorEdit3(("Diffuse##Light" + std::to_string(i)).c_str(), &sceneData_.pointLights[i].diffuse.x);
                    ImGui::ColorEdit3(("Ambient##Light" + std::to_string(i)).c_str(), &sceneData_.pointLights[i].ambient.x);
                    ImGui::ColorEdit3(("Specular##Light" + std::to_string(i)).c_str(), &sceneData_.pointLights[i].specular.x);
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();

        ImGui::Render();

        draw();

        auto end         = std::chrono::system_clock::now();
        auto elapsed     = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        stats_.frameTime = static_cast<float>(elapsed.count()) / 1000.0f;
        deltaTime_       = stats_.frameTime / 1000.0f;
    }
}

void JVKEngine::initVulkan() {
    LOG_INFO("Initializing Vulkan");

    // VOLK
    auto volkResult = volkInitialize();
    if (volkResult != VK_SUCCESS) {
        LOG_ERROR("Error initializing volk library");
    }


    // CREATE INSTANCE
    vkb::InstanceBuilder builder;
    auto vkbInstanceResult = builder.set_app_name("JVK")
                                     .request_validation_layers(JVK_USE_VALIDATION_LAYERS)
                                     .use_default_debug_messenger()
                                     .require_api_version(1, 3, 0)
                                     .build();

    if (!vkbInstanceResult) {
        LOG_ERROR("Failed to create Vulkan instance: {}", vkbInstanceResult.error().message());
    }

    vkb::Instance vkbInstance = vkbInstanceResult.value();

    ctx_.instance       = vkbInstance.instance;
    ctx_.debugMessenger = vkbInstance.debug_messenger;

    volkLoadInstance(ctx_.instance);

    // CREATE SURFACE
    SDL_Vulkan_CreateSurface(window_, ctx_, &ctx_.surface);

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType               = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing  = true;

    // PHYSICAL DEVICE
    vkb::PhysicalDeviceSelector physicalDeviceBuilder{vkbInstance};
    auto vkbPhysicalDeviceResult = physicalDeviceBuilder.set_minimum_version(1, 2)
                                           .add_required_extension(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)
                                           .add_required_extension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)
                                           .add_required_extension(VK_KHR_COPY_COMMANDS_2_EXTENSION_NAME)
                                           .set_required_features_12(features12)
                                           .set_surface(ctx_)
                                           .select();

    if (!vkbPhysicalDeviceResult) {
        LOG_ERROR("Failed to select physical device: {}", vkbPhysicalDeviceResult.error().message());
    }

    const vkb::PhysicalDevice &vkbPhysicalDevice = vkbPhysicalDeviceResult.value();

    // DEVICE
    vkb::DeviceBuilder deviceBuilder{vkbPhysicalDevice};

    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRendering{};
    dynamicRendering.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
    dynamicRendering.dynamicRendering = VK_TRUE;

    VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2{};
    synchronization2.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    synchronization2.synchronization2 = VK_TRUE;
    synchronization2.pNext            = &dynamicRendering;

    deviceBuilder.add_pNext(&synchronization2);
    vkb::Device vkbDevice = deviceBuilder.build().value();
    ctx_.device           = vkbDevice.device;
    ctx_.physicalDevice   = vkbPhysicalDevice.physical_device;

    // We only use one device, so this is fine
    volkLoadDevice(ctx_.device);

    // QUEUE
    graphicsQueue_.queue  = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    graphicsQueue_.family = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    // VMA
    VmaVulkanFunctions vulkanFunctions    = {};
    vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr   = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice         = ctx_.physicalDevice;
    allocatorInfo.device                 = ctx_.device;
    allocatorInfo.instance               = ctx_.instance;
    allocatorInfo.flags                  = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorInfo.pVulkanFunctions       = &vulkanFunctions;

    vmaCreateAllocator(&allocatorInfo, &allocator_);

    // MSAA
    // maxMsaaSamples_ = getMaxUsableSampleCount();
    LOG_INFO("Initialized Vulkan");
}

void JVKEngine::initSwapchain() {
    LOG_INFO("Initializing swapchain");
    swapchain_.init(ctx_, windowExtent_.width, windowExtent_.height);
    LOG_INFO("Initialized swapchain");
}

void JVKEngine::initCommands() {
    LOG_INFO("Initializing command buffers");
    // COMMAND POOL
    // Indicate that buffers should be individually resettable
    VkCommandPoolCreateFlags flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    // COMMAND BUFFERS
    for (auto &frame: frames_) {
        CHECK_VK(frame.cmdPool.init(ctx_, graphicsQueue_.family, flags));
        CHECK_VK(frame.cmdPool.allocateCommandBuffer(&frame.cmdBuffer));
    }

    // IMMEDIATE BUFFERS
    CHECK_VK(immBuffer_.init(ctx_, graphicsQueue_.family, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT));
    LOG_INFO("Initialized command buffers");
}

void JVKEngine::initSyncStructures() {
    LOG_INFO("Initializing synchronization structures");
    for (auto &frame: frames_) {
        CHECK_VK(frame.renderFence.init(ctx_, VK_FENCE_CREATE_SIGNALED_BIT));
        CHECK_VK(frame.swapchainSemaphore.init(ctx_));
        CHECK_VK(frame.renderSemaphore.init(ctx_));
    }
    LOG_INFO("Initialized synchronization structures");
}

void JVKEngine::initDescriptors() {
    LOG_INFO("Initializing descriptors");
    // GLOBAL DESCRIPTOR ALLOCATOR
    std::vector<jvk::DynamicDescriptorAllocator::PoolSizeRatio> sizes =
            {
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
                    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4}};

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

    // BILLBOARD
    {
        jvk::DescriptorLayoutBuilder builder;
        builder.addBinding(0, 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        //        builder.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        //        builder.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        billboardDescriptorLayout_ = builder.build(ctx_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        billboardDescriptorSet_    = globalDescriptorAllocator_.allocate(ctx_, billboardDescriptorLayout_);
    }

    // FRAME DESCRIPTORS
    for (auto &frame: frames_) {
        std::vector<jvk::DynamicDescriptorAllocator::PoolSizeRatio> frameSizes = {
                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
        };

        frame.descriptorAllocator = jvk::DynamicDescriptorAllocator();
        frame.descriptorAllocator.init(ctx_.device, 1000, frameSizes);

        // Allocate scene data descriptor set
        frame.sceneDataDescriptorSet = globalDescriptorAllocator_.allocate(ctx_, sceneDataDescriptorLayout_);

        // Create corresponding buffer
        frame.sceneDataBuffer = createBuffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    }

    // TEXTURES
    {
        jvk::DescriptorLayoutBuilder builder;
        builder.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        singleImageDescriptorLayout_ = builder.build(ctx_.device, VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    LOG_INFO("Initialized descriptors");
}

void JVKEngine::initPipelines() {
    LOG_INFO("Initializing pipelines");
    initBackgroundPipelines();
    initBillboardPipeline();
    metallicRoughnessMaterial_.buildPipelines(this);
    LOG_INFO("Initialized pipelines");
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
    CHECK_VK(vkCreatePipelineLayout(ctx_.device, &layout, nullptr, &computePipelineLayout_));

    // PIPELINE STAGES (AND SHADERS)
    VkShaderModule gradientShader;
    if (!jvk::loadShaderModule("../shaders/gradient_pc.comp.spv", ctx_.device, &gradientShader)) {
        LOG_FATAL("Failed to load gradient_pc.comp.spv");
    }

    VkShaderModule skyShader;
    if (!jvk::loadShaderModule("../shaders/sky.comp.spv", ctx_.device, &skyShader)) {
        LOG_FATAL("Failed to load sky.comp.spv");
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

    ComputeEffect gradient{};
    gradient.layout     = computePipelineLayout_;
    gradient.name       = "gradient";
    gradient.data       = {};
    gradient.data.data1 = glm::vec4(0.243, 0.243, 0.247, 1);
    gradient.data.data2 = glm::vec4(0.243, 0.243, 0.247, 1);

    CHECK_VK(vkCreateComputePipelines(ctx_.device, VK_NULL_HANDLE, 1, &computeInfo, nullptr, &gradient.pipeline));

    // CREATE SKY PIPELINE
    computeInfo.stage.module = skyShader;

    ComputeEffect sky{};
    sky.layout     = computePipelineLayout_;
    sky.name       = "sky";
    sky.data       = {};
    sky.data.data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);
    CHECK_VK(vkCreateComputePipelines(ctx_.device, VK_NULL_HANDLE, 1, &computeInfo, nullptr, &sky.pipeline));

    computeEffects_.push_back(gradient);
    computeEffects_.push_back(sky);

    vkDestroyShaderModule(ctx_.device, gradientShader, nullptr);
    vkDestroyShaderModule(ctx_.device, skyShader, nullptr);
}

void JVKEngine::initBillboardPipeline() {
    // LOAD SHADERS
    VkShaderModule vertShader;
    if (!jvk::loadShaderModule("../shaders/billboard.vert.spv", ctx_, &vertShader)) {
        LOG_FATAL("Error when building vertex shader module");
    }
    VkShaderModule fragShader;
    if (!jvk::loadShaderModule("../shaders/billboard.frag.spv", ctx_, &fragShader)) {
        LOG_FATAL("Error when building fragment shader module");
    }

    // PUSH CONSTANTS
    VkPushConstantRange pushConstant{};
    pushConstant.offset     = 0;
    pushConstant.size       = sizeof(BillboardPushConstants);
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // DESCRIPTOR LAYOUTS
    const VkDescriptorSetLayout layouts[] = {sceneDataDescriptorLayout_, billboardDescriptorLayout_};

    // PIPELINE LAYOUT
    VkPipelineLayoutCreateInfo layoutInfo = jvk::init::pipelineLayout();
    layoutInfo.setLayoutCount             = 2;
    layoutInfo.pSetLayouts                = layouts;
    layoutInfo.pushConstantRangeCount     = 1;
    layoutInfo.pPushConstantRanges        = &pushConstant;
    CHECK_VK(vkCreatePipelineLayout(ctx_.device, &layoutInfo, nullptr, &billboardPipeline_.pipelineLayout));

    // BUILD PIPELINE
    jvk::PipelineBuilder builder;
    builder.setShaders(vertShader, fragShader);
    builder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
    builder.setPolygonMode(VK_POLYGON_MODE_FILL);
    builder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    builder.setMultiSamplingNone();
    builder.disableBlending();
    builder.enableBlendingAlphaBlend();
    builder.enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL);
    builder.disableStencilTest();
    builder.setColorAttachmentFormat(drawImage_.imageFormat);
    builder.setDepthAttachmentFormat(depthStencilImage_.imageFormat);
    builder._pipelineLayout = billboardPipeline_.pipelineLayout;

    billboardPipeline_.pipeline = builder.buildPipeline(ctx_);

    // DESTROY SHADERS
    vkDestroyShaderModule(ctx_, vertShader, nullptr);
    vkDestroyShaderModule(ctx_, fragShader, nullptr);
}

void JVKEngine::initImgui() {
    LOG_INFO("Initializing UI");
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

    CHECK_VK(vkCreateDescriptorPool(ctx_.device, &poolInfo, nullptr, &imguiPool_));

    ImGui::CreateContext();
    ImGui_ImplSDL2_InitForVulkan(window_);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance            = ctx_.instance;
    initInfo.PhysicalDevice      = ctx_.physicalDevice;
    initInfo.Device              = ctx_.device;
    initInfo.Queue               = graphicsQueue_.queue;
    initInfo.DescriptorPool      = imguiPool_;
    initInfo.MinImageCount       = 3;
    initInfo.ImageCount          = 3;
    initInfo.UseDynamicRendering = true;

    initInfo.PipelineRenderingCreateInfo                         = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    initInfo.PipelineRenderingCreateInfo.colorAttachmentCount    = 1;
    initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchain_.imageFormat;

    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&initInfo);
    ImGui_ImplVulkan_CreateFontsTexture();
    LOG_INFO("Initialized UI");
}

void JVKEngine::drawImgui(VkCommandBuffer cmd, VkImageView targetImageView) const {
    // Setup color attachment for render pass
    VkRenderingAttachmentInfo colorAttachment = jvk::init::renderingAttachment(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderInfo                = jvk::init::rendering(swapchain_.extent, &colorAttachment, nullptr);

    // Render
    vkCmdBeginRenderingKHR(cmd, &renderInfo);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRenderingKHR(cmd);
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

    VkClearValue clearValue{};
    clearValue.depthStencil.depth   = 1.0f;
    clearValue.depthStencil.stencil = 0;

    VkRenderingAttachmentInfo depthAttachment = jvk::init::depthRenderingAttachment(depthStencilImage_.imageView, &clearValue, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderingInfo             = jvk::init::rendering(drawExtent_, &colorAttachment, &depthAttachment);

    // BEGIN RENDER PASS
    vkCmdBeginRenderingKHR(cmd, &renderingInfo);

    // UNIFORM BUFFERS & GLOBAL DESCRIPTOR SET
    // Contains global scene data (projection matrices, light, etc)
    jvk::Buffer sceneDataBuffer = getCurrentFrame().sceneDataBuffer;
    auto *sceneUniformData      = static_cast<GPUSceneData *>(sceneDataBuffer.allocation->GetMappedData());
    *sceneUniformData           = sceneData_;

    jvk::DescriptorWriter writer;
    writer.writeBuffer(0, sceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.updateSet(ctx_.device, getCurrentFrame().sceneDataDescriptorSet);

    jvk::Pipeline *lastPipeline    = nullptr;
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
                viewport.width    = static_cast<float>(drawExtent_.width);
                viewport.height   = static_cast<float>(drawExtent_.height);
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
        GPUDrawPushConstants pushConstants{};
        pushConstants.vertexBuffer = r.vertexBufferAddress;
        pushConstants.worldMatrix  = r.transform;
        pushConstants.nWorldMatrix = r.nTransform;
        vkCmdPushConstants(cmd, r.material->pipeline->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);

        // Draw
        vkCmdDrawIndexed(cmd, r.indexCount, 1, r.firstIndex, 0, 0);

        stats_.drawCallCount++;
        stats_.triangleCount += static_cast<int>(r.indexCount) / 3;
    };

    for (const auto &r: opaqueDraws) {
        draw(drawCtx_.opaqueSurfaces[r]);
    }

    for (const RenderObject &r: drawCtx_.transparentSurfaces) {
        draw(r);
    }

    drawBillboards(cmd);

    vkCmdEndRenderingKHR(cmd);

    auto end            = std::chrono::system_clock::now();
    auto elapsed        = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    stats_.meshDrawTime = static_cast<float>(elapsed.count()) / 1000.0f;
}

// For now, this draws the error checkerboard texture to the first point light's position
void JVKEngine::drawBillboards(VkCommandBuffer cmd) {
    // Uniform buffer & descriptors are already updated & written by this point

    // PIPELINE & DESCRIPTORS
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, billboardPipeline_.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, billboardPipeline_.pipelineLayout, 0, 1, &getCurrentFrame().sceneDataDescriptorSet, 0, nullptr);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, billboardPipeline_.pipelineLayout, 1, 1, &billboardDescriptorSet_, 0, nullptr);

    // VIEWPORT
    VkViewport viewport{};
    viewport.x        = 0;
    viewport.y        = 0;
    viewport.width    = static_cast<float>(drawExtent_.width);
    viewport.height   = static_cast<float>(drawExtent_.height);
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

    // POINT LIGHTS
    BillboardPushConstants pushConstants{};
    pushConstants.color        = billboardColor_;
    pushConstants.scale        = glm::vec4(0.25f);
    pushConstants.textureIndex = 0;

    for (auto &light: sceneData_.pointLights) {
        pushConstants.particleCenter = light.position;
        vkCmdPushConstants(cmd, billboardPipeline_.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(BillboardPushConstants), &pushConstants);
        vkCmdDraw(cmd, 4, 1, 0, 0);
    }

    // SUN
    pushConstants.particleCenter = sceneData_.dirLight.position;
    pushConstants.textureIndex   = 1;
    vkCmdPushConstants(cmd, billboardPipeline_.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(BillboardPushConstants), &pushConstants);
    vkCmdDraw(cmd, 4, 1, 0, 0);
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

    jvk::Buffer buffer{};
    CHECK_VK(vmaCreateBuffer(allocator_, &info, &allocInfo, &buffer.buffer, &buffer.allocation, &buffer.info));
    return buffer;
}

void JVKEngine::destroyBuffer(const jvk::Buffer &buffer) const {
    buffer.destroy(allocator_);
}

GPUMeshBuffers JVKEngine::uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices) const {
    const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
    const size_t indexBufferSize  = indices.size() * sizeof(uint32_t);

    GPUMeshBuffers surface{};

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
    void *data          = staging.allocation->GetMappedData();

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
    LOG_INFO("Initializing default data");
    // TEXTURES
    // 1 pixel default textures
    LOG_INFO("Initializing default textures");
    uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
    whiteImage_    = createImage((void *) &white, VkExtent3D{1, 1, 1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 1.0f));
    blackImage_    = createImage((void *) &black, VkExtent3D{1, 1, 1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    //checkerboard image
    uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
    std::array<uint32_t, 16 * 16> pixels{};
    for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 16; y++) {
            pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
        }
    }
    errorCheckerboardImage_ = createImage(pixels.data(), VkExtent3D{16, 16, 1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    // BILLBOARDS
    if (const auto img = loadImage(this, "../assets/billboard/lightbulb.png"); img.has_value()) {
        lightbulbImage_ = img.value();
        LOG_INFO("Lightbulb icon loaded");
    } else {
        LOG_ERROR("Failed to load lightbulb icon");
    }

    if (const auto img = loadImage(this, "../assets/billboard/sun.png"); img.has_value()) {
        sunImage_ = img.value();
        LOG_INFO("Sun icon loaded");
    } else {
        LOG_ERROR("Failed to load sun icon");
    }

    // SAMPLERS
    CHECK_VK(defaultSamplerNearest_.init(ctx_, VK_FILTER_NEAREST, VK_FILTER_NEAREST));
    CHECK_VK(defaultSamplerLinear_.init(ctx_, VK_FILTER_LINEAR, VK_FILTER_LINEAR));

    // Write billboard images to descriptor set
    VkDescriptorImageInfo imageInfos[2];
    imageInfos[0].sampler     = defaultSamplerLinear_;
    imageInfos[0].imageView   = lightbulbImage_.imageView;
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    imageInfos[1].sampler     = defaultSamplerLinear_;
    imageInfos[1].imageView   = sunImage_.imageView;
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    jvk::DescriptorWriter writer;
    writer.writeImages(0, imageInfos, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    //    writer.writeImage(0, lightbulbImage_.imageView, defaultSamplerLinear_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    //    writer.writeImage(1, sunImage_.imageView, defaultSamplerLinear_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.updateSet(ctx_, billboardDescriptorSet_);

    // MATERIALS
    Material::MaterialResources matResources{};
    matResources.colorImage               = whiteImage_;
    matResources.colorSampler             = defaultSamplerLinear_;
    matResources.metallicRoughnessImage   = whiteImage_;
    matResources.metallicRoughnessSampler = defaultSamplerLinear_;
    matResources.ambientImage             = whiteImage_;
    matResources.ambientSampler           = defaultSamplerLinear_;
    matResources.diffuseImage             = whiteImage_;
    matResources.diffuseSampler           = defaultSamplerLinear_;
    matResources.specularImage            = whiteImage_;
    matResources.specularSampler          = defaultSamplerLinear_;

    matConstants_                              = createBuffer(sizeof(Material::MaterialConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    auto *sceneUniformData                     = static_cast<Material::MaterialConstants *>(matConstants_.allocation->GetMappedData());
    sceneUniformData->colorFactors             = glm::vec4{1, 1, 1, 1};
    sceneUniformData->metallicRoughnessFactors = glm::vec4{1, 0.5, 0, 0};
    sceneUniformData->ambient                  = glm::vec4{0, 0, 0, 1};
    sceneUniformData->diffuse                  = glm::vec4{1, 1, 1, 1};
    sceneUniformData->specular                 = glm::vec3{0, 0, 0};
    sceneUniformData->shininess                = 32.0f;

    matResources.dataBuffer       = matConstants_.buffer;
    matResources.dataBufferOffset = 0;

    defaultMaterialData_ = metallicRoughnessMaterial_.writeMaterial(ctx_.device, MaterialPass::MAIN_COLOR, matResources, globalDescriptorAllocator_);

    // DIRECTIONAL LIGHT
    sceneData_.dirLight.position  = glm::vec4(10.0f, 10.0f, 10.0f, 1.0f);
    sceneData_.dirLight.direction = glm::vec4(glm::vec3(-1.0f, -1.0f, -1.0f), 0.0f);
    sceneData_.dirLight.ambient   = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f);
    sceneData_.dirLight.diffuse   = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
    sceneData_.dirLight.specular  = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

    // 2 POINT LIGHTS
    sceneData_.pointLights[0].position  = glm::vec4(2.0f, -2.0f, 0.0f, 1.0f);
    sceneData_.pointLights[0].ambient   = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f);
    sceneData_.pointLights[0].diffuse   = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
    sceneData_.pointLights[0].specular  = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    sceneData_.pointLights[0].constant  = 1.0f;
    sceneData_.pointLights[0].linear    = 0.09f;
    sceneData_.pointLights[0].quadratic = 0.032f;

    sceneData_.pointLights[1].position  = glm::vec4(-2.0f, -2.0f, 0.0f, 1.0f);
    sceneData_.pointLights[1].ambient   = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f);
    sceneData_.pointLights[1].diffuse   = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
    sceneData_.pointLights[1].specular  = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    sceneData_.pointLights[1].constant  = 1.0f;
    sceneData_.pointLights[1].linear    = 0.09f;
    sceneData_.pointLights[1].quadratic = 0.032f;

    // SPOTLIGHT
    sceneData_.spotLight.position    = mainCamera_.position;
    sceneData_.spotLight.direction   = mainCamera_.getFront();
    sceneData_.spotLight.ambient     = glm::vec3(0.0f);
    sceneData_.spotLight.diffuse     = glm::vec3(1.0f);
    sceneData_.spotLight.specular    = glm::vec3(1.0f);
    sceneData_.spotLight.constant    = 1.0f;
    sceneData_.spotLight.linear      = 0.09f;
    sceneData_.spotLight.quadratic   = 0.032f;
    sceneData_.spotLight.cutOff      = glm::cos(glm::radians(12.5f));
    sceneData_.spotLight.outerCutOff = glm::cos(glm::radians(15.0f));
    LOG_INFO("Initialized default data");
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
    jvk::Image image{};
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
    CHECK_VK(vmaCreateImage(allocator_, &imageInfo, &allocInfo, &image.image, &image.allocation, nullptr));

    // DEPTH STENCIL
    VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
    if (jvk::formatHasDepth(image.imageFormat)) {
        aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (format > VK_FORMAT_D16_UNORM_S8_UINT) {
            aspectFlag |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    }

    // IMAGE VIEW
    VkImageViewCreateInfo viewInfo       = jvk::init::imageView(format, image.image, aspectFlag);
    viewInfo.subresourceRange.levelCount = imageInfo.mipLevels;
    CHECK_VK(vkCreateImageView(ctx_.device, &viewInfo, nullptr, &image.imageView));

    return image;
}

jvk::Image JVKEngine::createImage(void *data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped) const {
    // STAGING BUFFER
    size_t dataSize          = size.depth * size.width * size.height * 4;
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

    sceneData_.view      = view;
    sceneData_.proj      = proj;
    sceneData_.viewProj  = sceneData_.proj * sceneData_.view;
    sceneData_.cameraPos = glm::vec4(mainCamera_.position, 1.0f);

    sceneData_.spotLight.position  = mainCamera_.position;
    sceneData_.spotLight.direction = mainCamera_.getFront();

    sceneData_.enableUserSpotLight = enableSpotlight_ ? 1 : 0;

    auto end               = std::chrono::system_clock::now();
    auto elapsed           = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    stats_.sceneUpdateTime = static_cast<float>(elapsed.count()) / 1000.0f;
}

VkSampleCountFlagBits JVKEngine::getMaxUsableSampleCount() const {
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
    LOG_INFO("Initializing draw images");
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
    CHECK_VK(vkCreateImageView(ctx_.device, &imageViewInfo, nullptr, &drawImage_.imageView));

    // DEPTH/STENCIL IMAGE
    jvk::getSupportedDepthStencilFormat(ctx_, &depthStencilImage_.imageFormat);
    depthStencilImage_.imageExtent = drawImageExtent;

    VkImageUsageFlags depthImageUsages{};
    depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VkImageCreateInfo depthImageInfo = jvk::init::image(depthStencilImage_.imageFormat, depthImageUsages, drawImageExtent);
    vmaCreateImage(allocator_, &depthImageInfo, &drawImageAllocInfo, &depthStencilImage_.image, &depthStencilImage_.allocation, nullptr);

    VkImageViewCreateInfo depthImageViewInfo = jvk::init::imageView(depthStencilImage_.imageFormat, depthStencilImage_.image, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
    CHECK_VK(vkCreateImageView(ctx_.device, &depthImageViewInfo, nullptr, &depthStencilImage_.imageView));
    LOG_INFO("Initialized draw images");
}
