#pragma once
#include "jvk.hpp"
#include <functional>

#include "vk/Commands.hpp"
#include "vk/Fence.hpp"

namespace jvk {

struct ImmediateBuffer {
    Fence fence;
    VkCommandBuffer cmd;
    CommandPool pool;

    void init(VkDevice device, const uint32_t familyIndex) {
        pool.init(device, familyIndex, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
        cmd = pool.createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        fence.init(device, VK_FENCE_CREATE_SIGNALED_BIT);
    }

    void destroy(VkDevice device) const {
        pool.destroy();
        fence.destroy(device);
    }

    void submit(VkDevice device, VkQueue &queue, std::function<void(VkCommandBuffer cmd)> && function) {
        fence.reset(device);
        VK_CHECK(vkResetCommandBuffer(cmd, 0));

        beginCommandBuffer(cmd, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        function(cmd);
        endCommandBuffer(cmd);

        submitCommandBuffer(queue, cmd, nullptr, nullptr, fence);
        fence.wait(device);
    }
};

}