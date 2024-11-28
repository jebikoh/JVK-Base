#pragma once

#include "jvk.hpp"
#include <functional>
#include <ranges>

struct DeletionStack {
    std::vector<std::function<void()>> queue;

    void push(std::function<void()> && function) {
        queue.push_back(std::move(function));
    }

    void flush() {
        for (auto & it : std::ranges::reverse_view(queue)) {
            it();
        }
        queue.clear();
    }
};