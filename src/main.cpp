#include <engine.hpp>
#include <SDL.h>

#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnusedParameter"

int main(int argv, char **args) {
    JVKEngine engine;

    engine.init();
    engine.run();
    engine.cleanup();

    return 0;
}
#pragma clang diagnostic pop