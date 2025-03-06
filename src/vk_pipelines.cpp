#include <fstream>
#include <vk_init.hpp>
#include <vk_pipelines.hpp>

bool VkUtil::loadShaderModule(const char *filePath, VkDevice device, VkShaderModule *outShaderModule) {
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    size_t fileSize = (size_t) file.tellg();
    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

    file.seekg(0);
    file.read((char *) buffer.data(), fileSize);
    file.close();

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.pNext    = nullptr;
    createInfo.codeSize = buffer.size() * sizeof(uint32_t);
    createInfo.pCode    = buffer.data();

    VkShaderModule shader;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shader) != VK_SUCCESS) {
        return false;
    }

    *outShaderModule = shader;
    return true;
}

void VkUtil::PipelineBuilder::clear() {
    _inputAssembly        = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    _rasterizer           = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    _colorBlendAttachment = {};
    _multisampling        = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    _pipelineLayout       = {};
    _depthStencil         = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    _renderingInfo        = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    _shaderStages.clear();
}

void VkUtil::PipelineBuilder::setShaders(VkShaderModule vertexShader, VkShaderModule fragmentShader) {
    _shaderStages.clear();
    _shaderStages.push_back(VkInit::pipelineShaderStage(VK_SHADER_STAGE_VERTEX_BIT, vertexShader));
    _shaderStages.push_back(VkInit::pipelineShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader));
}

void VkUtil::PipelineBuilder::setInputTopology(VkPrimitiveTopology topology) {
    _inputAssembly.topology               = topology;
    _inputAssembly.primitiveRestartEnable = VK_FALSE;
}

void VkUtil::PipelineBuilder::setPolygonMode(VkPolygonMode mode) {
    _rasterizer.polygonMode = mode;
    _rasterizer.lineWidth   = 1.0f;
}

void VkUtil::PipelineBuilder::setCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace) {
    _rasterizer.cullMode  = cullMode;
    _rasterizer.frontFace = frontFace;
}

void VkUtil::PipelineBuilder::setMultiSamplingNone() {
    _multisampling.sampleShadingEnable   = VK_FALSE;
    _multisampling.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT;
    _multisampling.minSampleShading      = 1.0f;
    _multisampling.pSampleMask           = nullptr;
    _multisampling.alphaToCoverageEnable = VK_FALSE;
    _multisampling.alphaToOneEnable      = VK_FALSE;
}

void VkUtil::PipelineBuilder::disableBlending() {
    _colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    _colorBlendAttachment.blendEnable    = VK_FALSE;
}

void VkUtil::PipelineBuilder::setColorAttachmentFormat(VkFormat format) {
    _colorAttachmentFormat                 = format;
    _renderingInfo.colorAttachmentCount    = 1;
    _renderingInfo.pColorAttachmentFormats = &_colorAttachmentFormat;
}

void VkUtil::PipelineBuilder::setDepthAttachmentFormat(VkFormat format) {
    _renderingInfo.depthAttachmentFormat = format;
}

VkPipeline VkUtil::PipelineBuilder::buildPipeline(const VkDevice device) const {
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.pNext         = nullptr;
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.pNext           = nullptr;
    colorBlending.logicOpEnable   = VK_FALSE;
    colorBlending.logicOp         = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments    = &_colorBlendAttachment;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext               = &_renderingInfo;
    pipelineInfo.stageCount          = static_cast<uint32_t>(_shaderStages.size());
    pipelineInfo.pStages             = _shaderStages.data();
    pipelineInfo.pVertexInputState   = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &_inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &_rasterizer;
    pipelineInfo.pMultisampleState   = &_multisampling;
    pipelineInfo.pColorBlendState    = &colorBlending;
    pipelineInfo.pDepthStencilState  = &_depthStencil;
    pipelineInfo.layout              = _pipelineLayout;

    // DYNAMIC STATE
    VkDynamicState state[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicInfo{};
    dynamicInfo.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicInfo.pDynamicStates    = &state[0];
    dynamicInfo.dynamicStateCount = 2;

    pipelineInfo.pDynamicState = &dynamicInfo;

    VkPipeline newPipeline;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS) {
        fmt::println("Failed to create pipeline");
        return VK_NULL_HANDLE;
    } else {
        return newPipeline;
    }
}
