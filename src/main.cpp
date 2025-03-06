#include <jvk.hpp>
#include <engine.hpp>
#include <SDL.h>

int main(int argv, char **args) {
    JVKEngine engine;

    engine.init();
    engine.run();
    engine.cleanup();

    return 0;
}