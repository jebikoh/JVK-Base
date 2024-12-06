#pragma once

#include "../jvk.hpp"

namespace jvk {

class PipelineBuilder {
public:
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages_;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly_;
    VkPipelineRasterizationStateCreateInfo rasterizer_;
    VkPipelineColorBlendAttachmentState colorBlendAttachment_;
    VkPipelineMultisampleStateCreateInfo multisampling_;
    VkPipelineLayout pipelineLayout_;
    VkPipelineDepthStencilStateCreateInfo depthStencil_;
    VkPipelineRenderingCreateInfo renderInfo_;
    VkFormat colorAttachmentFormat_;

    PipelineBuilder() { clear(); }
    void clear();
    VkPipeline buildPipeline(VkDevice device);

    void setShaders(VkShaderModule vertexShader, VkShaderModule fragmentShader);
    void setInputTopology(VkPrimitiveTopology topology);
    void setPolygonMode(VkPolygonMode mode);
    void setCullMode(VkCullModeFlags mode, VkFrontFace frontFace);
    void setMultisamplingNone();
    void disableBlending();
    void setColorAttachmentFormat(VkFormat format);
    void setDepthFormat(VkFormat format);
    void disableDepthTest();
};

namespace create {
VkPipelineLayoutCreateInfo pipelineLayoutInfo() {
    VkPipelineLayoutCreateInfo info{};
    info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.pNext                  = nullptr;
    info.flags                  = 0;
    info.setLayoutCount         = 0;
    info.pSetLayouts            = nullptr;
    info.pushConstantRangeCount = 0;
    info.pPushConstantRanges    = nullptr;
    return info;
}
}// namespace create

}// namespace jvk