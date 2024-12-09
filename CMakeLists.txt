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
        src/vk/Shaders.hpp
        src/Immediate.hpp
        src/vk/Render.hpp
        src/vk/Pipeline.hpp
        src/vk/Pipeline.cpp
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

# Shaders
find_program(GLSL_VALIDATOR glslangValidator HINTS /usr/bin /usr/local/bin $ENV{VULKAN_SDK}/Bin/ $ENV{VULKAN_SDK}/Bin32/)

file(GLOB_RECURSE GLSL_SOURCE_FILES
        "${PROJECT_SOURCE_DIR}/shaders/*.frag"
        "${PROJECT_SOURCE_DIR}/shaders/*.vert"
        "${PROJECT_SOURCE_DIR}/shaders/*.comp"
)

foreach(GLSL ${GLSL_SOURCE_FILES})
    message(STATUS "BUILDING SHADER")
    get_filename_component(FILE_NAME ${GLSL} NAME)
    set(SPIRV "${PROJECT_SOURCE_DIR}/shaders/${FILE_NAME}.spv")
    message(STATUS ${GLSL})
    message(STATUS COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV})
    add_custom_command(
            OUTPUT ${SPIRV}
            COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV}
            DEPENDS ${GLSL})
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

add_custom_target(
        Shaders
        DEPENDS ${SPIRV_BINARY_FILES}
)
