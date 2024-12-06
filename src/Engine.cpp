#include "Engine.hpp"

#include "vk/Commands.hpp"
#include "vk/Descriptors.hpp"
#include "vk/Image.hpp"
#include "vk/Render.hpp"
#include "vk/Shaders.hpp"
#include "vk/Pipeline.hpp"

#include <SDL.h>
#include <SDL_vulkan.h>
#include <VkBootstrap.h>

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>

#include <iostream>
#include <thread>

namespace jvk {

constexpr bool USE_VALIDATION_LAYERS = true;

void Engine::init() {
    initSDL();
    initVulkan();
    initSwapchain();
    initCommands();
    initSyncStructures();
    initDescriptors();
    initPipelines();
    initImGUI();

    isInit_ = true;
}

void Engine::destroy() {
    if (!isInit_) { return; }
    vkDeviceWaitIdle(context_.device);

    // Clean up frame command pools
    for (auto & frame : frames_) {
        frame.commandPool.destroy();
        frame.drawFence.destroy(context_);
        frame.drawSemaphore.destroy(context_);
        frame.swapchainSemaphore.destroy(context_);
        frame.deletionQueue.flush();
    }

    globalDeletionQueue_.flush();
    swapchain_.destroy(context_);
    context_.destroy();
    SDL_DestroyWindow(window_);
}

#pragma region Initialization
void Engine::initSDL() {
    SDL_Init(SDL_INIT_VIDEO);

    auto window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);
    window_ = SDL_CreateWindow(
            "Vulkan Engine",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            windowExtent_.width,
            windowExtent_.height,
            window_flags);
}

void Engine::initVulkan() {
    vkb::InstanceBuilder builder;

    auto vkbInstance = builder.set_app_name("JVK")
        .request_validation_layers(USE_VALIDATION_LAYERS)
        .use_default_debug_messenger()
        .require_api_version(1, 3, 0)
        .build();

    if (!vkbInstance) {
        std::cerr << "Failed to create Vulkan instance. Error: " << vkbInstance.error().message() << "\n";
        return;
    }

    vkb::Instance vkbInst = vkbInstance.value();
    context_.instance = vkbInst.instance;
    context_.debug = vkbInst.debug_messenger;

    SDL_Vulkan_CreateSurface(window_, context_.instance, &context_.surface);

    // Features
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = true;
    features13.synchronization2 = true;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType               = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing  = true;

    // Device
    vkb::PhysicalDeviceSelector selector{vkbInst};
    auto vkbPhysicalDeviceResult = selector
                      .set_minimum_version(1, 3)
                      .set_required_features_13(features13)
                      .set_required_features_12(features12)
                      .set_surface(context_.surface)
                      .select();
    if (!vkbPhysicalDeviceResult) {
        std::cerr << "Failed to select physical device. Error: " << vkbPhysicalDeviceResult.error().message() << "\n";
        return;
    }
    const auto& vkbPhysicalDevice = vkbPhysicalDeviceResult.value();

    context_.physicalDevice = vkbPhysicalDevice.physical_device;

    vkb::DeviceBuilder deviceBuilder{vkbPhysicalDevice};
    vkb::Device vkbDevice = deviceBuilder.build().value();
    context_.device = vkbDevice.device;

    // Queue
    graphicsQueue_.queue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    graphicsQueue_.family = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    // Allocator
    allocator_.init(context_);
    globalDeletionQueue_.push([&]() { allocator_.destroy(); });
}

void Engine::initImGUI() {
    VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
                                         { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
                                         { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
                                         { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
                                         { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
                                         { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
                                         { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
                                         { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
                                         { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
                                         { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
                                         { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    VkDescriptorPool imguiPool;
    VK_CHECK(vkCreateDescriptorPool(context_, &pool_info, nullptr, &imguiPool));

    ImGui::CreateContext();

    ImGui_ImplSDL2_InitForVulkan(window_);

    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.Instance                  = context_;
    initInfo.PhysicalDevice            = context_;
    initInfo.Device                    = context_;
    initInfo.Queue                     = graphicsQueue_.queue;
    initInfo.DescriptorPool            = imguiPool;
    initInfo.MinImageCount             = 3;
    initInfo.ImageCount                = 3;
    initInfo.UseDynamicRendering       = true;

    initInfo.PipelineRenderingCreateInfo                         = {};
    initInfo.PipelineRenderingCreateInfo.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    initInfo.PipelineRenderingCreateInfo.colorAttachmentCount    = 1;
    initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchain_.imageFormat;

    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&initInfo);
    ImGui_ImplVulkan_CreateFontsTexture();

    globalDeletionQueue_.push([=]() {
        ImGui_ImplVulkan_Shutdown();
        vkDestroyDescriptorPool(context_, imguiPool, nullptr);
    });
}

void Engine::initSwapchain() {
    swapchain_ = Swapchain{context_, windowExtent_.width, windowExtent_.height};

    VkExtent3D drawImageExtent = {
            windowExtent_.width,
            windowExtent_.height,
            1
    };

    VkImageUsageFlags drawImageUsages{};
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    drawImage_.init(context_, allocator_, VK_FORMAT_R16G16B16A16_SFLOAT, drawImageUsages, drawImageExtent);
    globalDeletionQueue_.push([=, this]() { drawImage_.destroy(context_, allocator_); });
}

void Engine::initCommands() {
    for (auto & frame : frames_) {
        frame.commandPool.init(context_, graphicsQueue_.family, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
        frame.mainCommandBuffer = frame.commandPool.createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    }

    immediateBuffer_.init(context_, graphicsQueue_.family);
    globalDeletionQueue_.push([=, this]() {
        immediateBuffer_.destroy(context_);
    });
}

void Engine::initSyncStructures() {
    for (auto & frame : frames_) {
        frame.swapchainSemaphore.init(context_);
        frame.drawSemaphore.init(context_);
        frame.drawFence.init(context_, VK_FENCE_CREATE_SIGNALED_BIT);
    }
}

void Engine::initDescriptors() {
    std::vector<DescriptorAllocator::PoolSizeRatio> sizes =
            {
                    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}
            };

    descriptorAllocator_.initPool(context_, 10, sizes);

    // Compute layout
    {
        DescriptorSetBindings bindings;
        bindings.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        drawImageDescriptorLayout_ = bindings.build(context_, VK_SHADER_STAGE_COMPUTE_BIT);

        drawImageDescriptor_ = descriptorAllocator_.allocate(context_, drawImageDescriptorLayout_);

        VkDescriptorImageInfo imgInfo{};
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imgInfo.imageView   = drawImage_.view_;

        VkWriteDescriptorSet drawImageWrite = {};
        drawImageWrite.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        drawImageWrite.pNext                = nullptr;
        drawImageWrite.dstBinding           = 0;
        drawImageWrite.dstSet               = drawImageDescriptor_;
        drawImageWrite.descriptorCount      = 1;
        drawImageWrite.descriptorType       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        drawImageWrite.pImageInfo           = &imgInfo;

        vkUpdateDescriptorSets(context_, 1, &drawImageWrite, 0, nullptr);

        globalDeletionQueue_.push([&]() {
            descriptorAllocator_.destroyPool(context_);
            vkDestroyDescriptorSetLayout(context_, drawImageDescriptorLayout_, nullptr);
        });
    }
}

void Engine::draw() {
    auto &frame = this->getCurrentFrame();
    frame.drawFence.wait(context_);
    frame.deletionQueue.flush();
    frame.drawFence.reset(context_);

    auto swapchainImageIndex = swapchain_.acquireNextImage(context_, frame.swapchainSemaphore);

    VkCommandBuffer cmd = getCurrentFrame().mainCommandBuffer;
    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    drawExtent_.width  = drawImage_.extent_.width;
    drawExtent_.height = drawImage_.extent_.height;

    beginCommandBuffer(cmd, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    transitionImage(cmd, drawImage_.image_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    drawBackground(cmd);
    transitionImage(cmd, drawImage_.image_, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    transitionImage(cmd, swapchain_.images[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyImage(cmd, drawImage_.image_, swapchain_.images[swapchainImageIndex], drawExtent_, swapchain_.extent);
    transitionImage(cmd, swapchain_.images[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    drawUI(cmd, swapchain_.imageViews[swapchainImageIndex]);
    transitionImage(cmd, swapchain_.images[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    endCommandBuffer(cmd);

    auto waitSemaphore = frame.swapchainSemaphore.submitInfo(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR);
    auto signalSemaphore = frame.drawSemaphore.submitInfo(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);

    submitCommandBuffer(graphicsQueue_.queue, cmd, &waitSemaphore, &signalSemaphore, frame.drawFence);

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext              = nullptr;
    presentInfo.pSwapchains        = &swapchain_.swapchain;
    presentInfo.swapchainCount     = 1;
    presentInfo.pWaitSemaphores    = &frame.drawSemaphore.semaphore;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pImageIndices      = &swapchainImageIndex;
    VK_CHECK(vkQueuePresentKHR(graphicsQueue_.queue, &presentInfo));

    frameNumber_++;
}

void Engine::run() {
    SDL_Event e;
    bool quit = false;

    while (!quit) {
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) { quit = true;}
            if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) { stopRendering_ = true; }
                if (e.window.event == SDL_WINDOWEVENT_RESTORED) { stopRendering_ = false; }
            }

            ImGui_ImplSDL2_ProcessEvent(&e);
        }
        if (stopRendering_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::ShowDemoWindow();
        ImGui::Render();

        draw();
    }
}

void Engine::drawBackground(VkCommandBuffer cmd) const {
    VkClearColorValue clearValue;
    float flash = std::abs(std::sin(frameNumber_ / 120.f));
    clearValue = { { 0.0f, 0.0f, flash, 1.0f } };

    VkImageSubresourceRange clearRange = imageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);
    vkCmdClearColorImage(cmd, drawImage_.image_, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);

}

void Engine::drawUI(VkCommandBuffer cmd, VkImageView targetImageView) const {
    auto colorAttachment = create::attachmentInfo(targetImageView, nullptr);
    VkRenderingInfo renderInfo = create::renderingInfo(swapchain_.extent, &colorAttachment, nullptr);

    vkCmdBeginRendering(cmd, &renderInfo);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRendering(cmd);
}

void Engine::initPipelines() {
    initTrianglePipeline();
}

void Engine::initTrianglePipeline() {
    VkShaderModule fragShader;
    VkShaderModule vertShader;

    if (!loadShaderModule("../shaders/colored_triangle.vert.spv", context_, &vertShader)) {
        std::cerr << "Failed to load vertex shader" << std::endl;
    }
    if (!loadShaderModule("../shaders/colored_triangle.frag.spv", context_, &fragShader)) {
        std::cerr << "Failed to load fragment shader" << std::endl;
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = create::pipelineLayoutInfo();
    VK_CHECK(vkCreatePipelineLayout(context_, &pipelineLayoutInfo, nullptr, &trianglePipeline_.layout));

    PipelineBuilder builder;
    builder.pipelineLayout_ = trianglePipeline_.layout;
    builder.setShaders(vertShader, fragShader);
    builder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    builder.setPolygonMode(VK_POLYGON_MODE_FILL);
    builder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    builder.setMultisamplingNone();
    builder.disableBlending();
    builder.disableDepthTest();

    builder.setColorAttachmentFormat(drawImage_.format_);
    builder.setDepthFormat(VK_FORMAT_UNDEFINED);

    trianglePipeline_.pipeline = builder.buildPipeline(context_);

    vkDestroyShaderModule(context_, fragShader, nullptr);
    vkDestroyShaderModule(context_, vertShader, nullptr);

    globalDeletionQueue_.push([=, this]() {
        vkDestroyPipelineLayout(context_, trianglePipeline_.layout, nullptr);
        vkDestroyPipeline(context_, trianglePipeline_.pipeline, nullptr);
    });
}

#pragma endregion

}// namespace jvk