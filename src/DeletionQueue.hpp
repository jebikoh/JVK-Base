#pragma once

#include "jvk.hpp"
#include <functional>

struct DeletionQueue {
    std::vector<std::function<void()>> queue;

    void push(std::function<void()> && function) {
        queue.push_back(std::move(function));
    }

    void flush() {
        for (auto &function : queue) { function(); }
        queue.clear();
    }
};