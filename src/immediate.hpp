#pragma once

#include "jvk/init.hpp"
#include <jvk.hpp>
#include <jvk/commands.hpp>
#include <jvk/fence.hpp>

struct ImmediateBuffer {
    jvk::Fence fence;
    jvk::CommandPool pool;
    jvk::CommandBuffer cmd;

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
        VK_CHECK(cmd.reset());

        // Create and start buffer
        VK_CHECK(cmd.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT));

        // Record immediate submit commands
        function(cmd);

        // End buffer
        VK_CHECK(cmd.end());

        // Submit and wait for fence
        VkCommandBufferSubmitInfo cmdInfo = cmd.submitInfo();
        VkSubmitInfo2 submit              = jvk::init::submit(&cmdInfo, nullptr, nullptr);
        VK_CHECK(vkQueueSubmit2(queue, 1, &submit, fence));
        fence.wait();
    }
};
