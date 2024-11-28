cmake_minimum_required(VERSION 3.28)
project(JVK_Engine)

set(CMAKE_CXX_STANDARD 20)

find_package(Vulkan REQUIRED)

add_executable(JVK_Engine src/main.cpp
        src/engine.cpp
        src/engine.hpp
        src/jvk.hpp
        src/vk/Swapchain.hpp
        src/vk/Context.hpp
        src/vk/Commands.hpp
        src/vk/Commands.cpp
        src/vk/Queue.hpp
        src/vk/Fence.hpp
        src/vk/Semaphore.hpp
        src/vk/Image.cpp
        src/vk/Image.hpp
        src/DeletionStack.hpp
        src/vk/Allocator.cpp
        src/vk/Allocator.hpp
)

add_subdirectory(include/vkbootstrap)
add_subdirectory(include/vma)
add_subdirectory(include/sdl EXCLUDE_FROM_ALL)
#if(TARGET SDL2::SDL2main)
#    target_link_libraries(JVK_Engine PRIVATE )
#endif()

target_link_libraries(JVK_Engine PRIVATE Vulkan::Vulkan SDL2::SDL2main SDL2::SDL2 GPUOpen::VulkanMemoryAllocator vk-bootstrap::vk-bootstrap)