#include <vulkan/vulkan.h>
#include <SDL2/SDL.h>
#include <iostream>

int main(int argc, char *argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "Error: SDL_Init failed\n";
        return -1;
    }

    SDL_Window* window = SDL_CreateWindow("JVK",
                                          SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED,
                                          800, 600,
                                          SDL_WINDOW_VULKAN);
    if (!window) {
        std::cerr << "Error: SDL_CreateWindow failed\n";
        SDL_Quit();
        return -1;
    }

    bool running = true;
    SDL_Event event;
    while (running) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                default:
                    break;
            }
        }
    }

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}