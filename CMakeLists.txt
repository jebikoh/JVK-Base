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
        src/vk/Descriptors.hpp
        src/vk/Descriptors.cpp
        src/Shaders.hpp
        src/Immediate.hpp
        src/vk/Render.hpp
)

add_subdirectory(include/vkbootstrap)
add_subdirectory(include/vma)
add_subdirectory(include/sdl EXCLUDE_FROM_ALL)
#if(TARGET SDL2::SDL2main)
#    target_link_libraries(JVK_Engine PRIVATE )
#endif()

# ImGUI
add_library(imgui STATIC)
target_include_directories(imgui PUBLIC include/imgui)
target_sources(imgui PRIVATE
        include/imgui/imgui.h
        include/imgui/imgui.cpp
        include/imgui/imgui_demo.cpp
        include/imgui/imgui_draw.cpp
        include/imgui/imgui_widgets.cpp
        include/imgui/imgui_tables.cpp
        include/imgui/imgui_impl_vulkan.cpp
        include/imgui/imgui_impl_sdl2.cpp
)

target_link_libraries(imgui PUBLIC Vulkan::Vulkan SDL2::SDL2)

target_link_libraries(JVK_Engine PRIVATE Vulkan::Vulkan SDL2::SDL2main SDL2::SDL2 GPUOpen::VulkanMemoryAllocator vk-bootstrap::vk-bootstrap imgui)