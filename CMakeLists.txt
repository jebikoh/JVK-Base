cmake_minimum_required(VERSION 3.28)
project(JVK_Engine)

set(CMAKE_CXX_STANDARD 20)

find_package(Vulkan REQUIRED)

add_executable(JVK_Engine src/main.cpp
        src/engine.cpp
        src/engine.hpp
        src/jvk.hpp
        src/swapchain.hpp
        src/Context.hpp
        src/commands.hpp
        src/commands.cpp
        src/Queue.hpp
        src/Fence.hpp
        src/Semaphore.hpp)

add_subdirectory(include/vkbootstrap)
add_subdirectory(include/vma)
add_subdirectory(include/sdl EXCLUDE_FROM_ALL)
#if(TARGET SDL2::SDL2main)
#    target_link_libraries(JVK_Engine PRIVATE )
#endif()

target_link_libraries(JVK_Engine PRIVATE Vulkan::Vulkan SDL2::SDL2main SDL2::SDL2 GPUOpen::VulkanMemoryAllocator vk-bootstrap::vk-bootstrap)