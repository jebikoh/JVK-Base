#pragma once

#include "vk_init.hpp"
#include <jvk.hpp>
#include <jvk/commands.hpp>
#include <jvk/fence.hpp>

struct ImmediateBuffer {
    jvk::Fence fence;
    jvk::CommandPool pool;
    VkCommandBuffer cmd;

    ImmediateBuffer() {};

    VkResult init(VkDevice device, const uint32_t familyIndex, VkCommandPoolCreateFlagBits flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT) {
        VkResult res;
        res = fence.init(device);
        if (res != VK_SUCCESS) { return res; }
        res = pool.init(device, familyIndex, flags);
        if (res != VK_SUCCESS) { return res; }
        return pool.allocateCommandBuffer(&cmd);
    }

    void destroy() {
        pool.destroy();
        fence.destroy();
    }

    void submit(VkQueue queue, std::function<void(VkCommandBuffer cmd)> &&function) const {
        // Reset fence & buffer
        VK_CHECK(fence.reset());
        VK_CHECK(vkResetCommandBuffer(cmd, 0));

        // Create and start buffer
        VkCommandBufferBeginInfo cmdBegin = VkInit::commandBufferBegin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBegin));

        // Record immediate submit commands
        function(cmd);

        // End buffer
        VK_CHECK(vkEndCommandBuffer(cmd));

        // Submit and wait for fence
        VkCommandBufferSubmitInfo cmdInfo = VkInit::commandBufferSubmit(cmd);
        VkSubmitInfo2 submit              = VkInit::submit(&cmdInfo, nullptr, nullptr);
        VK_CHECK(vkQueueSubmit2(queue, 1, &submit, fence));
        fence.wait();
    }
};
