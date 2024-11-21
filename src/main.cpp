#include "Engine.hpp"
#include <SDL2/SDL.h>
#include <iostream>
#include <vulkan/vulkan.h>

int main(int argc, char *argv[]) {
    jvk::Engine engine;

    engine.init();
    engine.run();
    engine.destroy();

    return 0;
}