//
// Created by Aneesh on 12/5/2024.
//
#include "Pipeline.hpp"
#include "Shaders.hpp"

void jvk::PipelineBuilder::clear() {
    inputAssembly_       = {};
    inputAssembly_.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;

    rasterizer_       = {};
    rasterizer_.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;

    colorBlendAttachment_ = {};

    multisampling_       = {};
    multisampling_.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;

    pipelineLayout_ = {};

    depthStencil_       = {};
    depthStencil_.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    renderInfo_       = {};
    renderInfo_.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;

    shaderStages_.clear();
}

VkPipeline jvk::PipelineBuilder::buildPipeline(VkDevice device) {
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType                             = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.pNext                             = nullptr;
    viewportState.viewportCount                     = 1;
    viewportState.scissorCount                      = 1;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType                               = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.pNext                               = nullptr;
    colorBlending.logicOpEnable                       = VK_FALSE;
    colorBlending.logicOp                             = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount                     = 1;
    colorBlending.pAttachments                        = &colorBlendAttachment_;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType                                = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext               = &renderInfo_;
    pipelineInfo.stageCount          = (uint32_t) shaderStages_.size();
    pipelineInfo.pStages             = shaderStages_.data();
    pipelineInfo.pVertexInputState   = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly_;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer_;
    pipelineInfo.pMultisampleState   = &multisampling_;
    pipelineInfo.pColorBlendState    = &colorBlending;
    pipelineInfo.pDepthStencilState  = &depthStencil_;
    pipelineInfo.layout              = pipelineLayout_;

    VkDynamicState state[]                        = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType                            = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.pDynamicStates                   = &state[0];
    dynamicState.dynamicStateCount                = 2;

    pipelineInfo.pDynamicState = &dynamicState;
    VkPipeline newPipeline;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS) {
        std::cout << "Failed to create graphics pipeline" << std::endl;
        return VK_NULL_HANDLE;
    } else {
        return newPipeline;
    }
}

void jvk::PipelineBuilder::setShaders(VkShaderModule vertexShader, VkShaderModule fragmentShader) {
    shaderStages_.clear();
    shaderStages_.push_back(create::pipelineShaderStage(VK_SHADER_STAGE_VERTEX_BIT, vertexShader, "main"));
    shaderStages_.push_back(create::pipelineShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader, "main"));
}

void jvk::PipelineBuilder::setInputTopology(VkPrimitiveTopology topology) {
    inputAssembly_.topology               = topology;
    inputAssembly_.primitiveRestartEnable = VK_FALSE;
}

void jvk::PipelineBuilder::setPolygonMode(VkPolygonMode mode) {
    rasterizer_.polygonMode = mode;
    rasterizer_.lineWidth   = 1.0f;
}

void jvk::PipelineBuilder::setCullMode(VkCullModeFlags mode, VkFrontFace frontFace) {
    rasterizer_.cullMode  = mode;
    rasterizer_.frontFace = frontFace;
}

void jvk::PipelineBuilder::setMultisamplingNone() {
    multisampling_.sampleShadingEnable   = VK_FALSE;
    multisampling_.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT;
    multisampling_.minSampleShading      = 1.0f;
    multisampling_.pSampleMask           = nullptr;
    multisampling_.alphaToCoverageEnable = VK_FALSE;
    multisampling_.alphaToOneEnable      = VK_FALSE;
}

void jvk::PipelineBuilder::disableBlending() {
    colorBlendAttachment_.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment_.blendEnable    = VK_FALSE;
}

void jvk::PipelineBuilder::setColorAttachmentFormat(VkFormat format) {
    colorAttachmentFormat_              = format;
    renderInfo_.colorAttachmentCount    = 1;
    renderInfo_.pColorAttachmentFormats = &colorAttachmentFormat_;
}

void jvk::PipelineBuilder::setDepthFormat(VkFormat format) {
    renderInfo_.depthAttachmentFormat = format;
}

void jvk::PipelineBuilder::disableDepthTest() {
    depthStencil_.depthTestEnable       = VK_FALSE;
    depthStencil_.depthWriteEnable      = VK_FALSE;
    depthStencil_.depthCompareOp        = VK_COMPARE_OP_NEVER;
    depthStencil_.depthBoundsTestEnable = VK_FALSE;
    depthStencil_.stencilTestEnable     = VK_FALSE;
    depthStencil_.front                 = {};
    depthStencil_.back                  = {};
    depthStencil_.minDepthBounds        = 0.0f;
    depthStencil_.maxDepthBounds        = 1.0f;
}

void jvk::PipelineBuilder::enableDepthTest(const bool depthWriteEnable, VkCompareOp op) {
    depthStencil_.depthTestEnable = VK_TRUE;
    depthStencil_.depthWriteEnable = depthWriteEnable;
    depthStencil_.depthCompareOp = op;
    depthStencil_.depthBoundsTestEnable = VK_FALSE;
    depthStencil_.stencilTestEnable     = VK_FALSE;
    depthStencil_.front                 = {};
    depthStencil_.back                  = {};
    depthStencil_.minDepthBounds        = 0.0f;
    depthStencil_.maxDepthBounds        = 1.0f;

}
