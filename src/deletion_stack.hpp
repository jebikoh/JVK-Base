#pragma once

#include <jvk.hpp>
#include <stack>

struct DeletionStack {
    std::stack<std::function<void()>> deletors;

    void push(std::function<void()> &&function) {
        deletors.push(function);
    }

    void flush() {
        while (!deletors.empty()) {
            deletors.top()();
            deletors.pop();
        }
    }
};