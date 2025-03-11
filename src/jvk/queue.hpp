#pragma once

#include <jvk.hpp>

namespace jvk {

struct Queue {
    VkQueue queue;
    uint32_t family;

    Queue() {};

    operator VkQueue() const { return queue; }
};

}