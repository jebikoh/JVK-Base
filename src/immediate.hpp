#pragma once

#include "jvk/init.hpp"
#include <jvk.hpp>
#include <jvk/commands.hpp>
#include <jvk/fence.hpp>

struct ImmediateBuffer {
    jvk::Fence fence;
    jvk::CommandPool pool;
    jvk::CommandBuffer cmd;

    ImmediateBuffer() = default;

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
        CHECK_VK(fence.reset());
        CHECK_VK(cmd.reset());

        // Create and start buffer
        CHECK_VK(cmd.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT));

        // Record immediate submit commands
        function(cmd);

        // End buffer
        CHECK_VK(cmd.end());

        // Submit and wait for fence
        VkCommandBufferSubmitInfoKHR cmdInfo = cmd.submitInfo();
        VkSubmitInfo2KHR submit              = jvk::init::submit(&cmdInfo, nullptr, nullptr);
        CHECK_VK(vkQueueSubmit2KHR(queue, 1, &submit, fence));
        fence.wait();
    }
};
