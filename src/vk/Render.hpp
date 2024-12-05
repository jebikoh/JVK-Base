#pragma once

#include "../jvk.hpp"

namespace jvk::create {

VkRenderingAttachmentInfo attachmentInfo(
        VkImageView view,
        VkClearValue *clear,
        VkImageLayout layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        ) {
    VkRenderingAttachmentInfo info{};
    info.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    info.pNext       = nullptr;
    info.imageView   = view;
    info.imageLayout = layout;
    info.loadOp      = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    info.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;

    if (clear) { info.clearValue = *clear; }

    return info;
}

VkRenderingInfo renderingInfo(
        VkExtent2D renderExtent,
        VkRenderingAttachmentInfo *colorAttachment,
        VkRenderingAttachmentInfo *depthAttachment
        ) {
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.pNext = nullptr;

    info.renderArea           = VkRect2D{VkOffset2D{0, 0}, renderExtent};
    info.layerCount           = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments    = colorAttachment;
    info.pDepthAttachment     = depthAttachment;
    info.pStencilAttachment   = nullptr;

    return info;
}

}