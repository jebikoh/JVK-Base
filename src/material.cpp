#include <material.hpp>
#include <mesh.hpp>
#include <engine.hpp>
#include <jvk/shaders.hpp>
#include <jvk/pipeline.hpp>

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

    // TODO: look into dynamic state
#ifdef JVK_ENABLE_BACKFACE_CULLING
    pipelineBuilder.setCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
#else
    pipelineBuilder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
#endif

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