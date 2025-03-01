#include <jvk.hpp>
#include <engine.hpp>

int main(int argc, char *argv[]) {
    JVKEngine engine;

    engine.init();
    engine.run();
    engine.cleanup();

    return 0;
}