cmake_minimum_required(VERSION 3.28)
project(JVK_Engine)

set(CMAKE_CXX_STANDARD 20)

find_package(Vulkan REQUIRED)

option(JVK_ENABLE_PERF_FLAGS "Enable performance flags" OFF)
option(JVK_USE_GLTF_ALPHA_MODE "Enable transparent pipeline" OFF)

add_executable(JVK_Engine src/main.cpp
        src/jvk.hpp
        src/engine.hpp
        src/engine.cpp
        src/jvk/init.hpp
        src/material.hpp
        src/jvk/descriptor.hpp
        src/jvk/descriptor.cpp
        src/jvk/pipeline.cpp
        src/jvk/pipeline.hpp
        src/mesh.hpp
        src/mesh.cpp
        src/camera.hpp
        src/camera.cpp
        src/jvk/commands.hpp
        src/jvk/context.hpp
        src/jvk/swapchain.hpp
        src/jvk/semaphore.hpp
        src/jvk/fence.hpp
        src/jvk/queue.hpp
        src/immediate.hpp
        src/jvk/image.hpp
        src/jvk/sampler.hpp
        src/jvk/util.hpp
        src/jvk/util.cpp
        src/jvk/buffer.hpp
)

if (JVK_ENABLE_PERF_FLAGS)
    if (MSVC)
        message(STATUS "Using MSVC compiler")
        add_compile_options(/O2 /fp:fast)
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(STATUS "Using GNU or Clang compiler")
        add_compile_options(-O3 -ffast-math)
    else ()
        message(WARNING "Unknown compiler. No optimization flags added.")
    endif ()
endif ()

if (JVK_USE_GLTF_ALPHA_MODE)
    add_compile_definitions(-DJVK_USE_GLTF_ALPHA_MODE)
endif ()


add_subdirectory(include/vkbootstrap)
add_subdirectory(include/vma)
add_subdirectory(include/sdl EXCLUDE_FROM_ALL)

# GLM
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE include/glm)

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

# FMT
include(FetchContent)

FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt
        GIT_TAG        e69e5f977d458f2650bb346dadf2ad30c5320281) # 10.2.1
FetchContent_MakeAvailable(fmt)

# FastGLTF
add_subdirectory(include/fastgltf)

target_include_directories(JVK_Engine
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include/stb
)

target_link_libraries(imgui PUBLIC Vulkan::Vulkan SDL2::SDL2)

target_link_libraries(JVK_Engine PRIVATE Vulkan::Vulkan SDL2::SDL2main SDL2::SDL2 GPUOpen::VulkanMemoryAllocator vk-bootstrap::vk-bootstrap imgui glm fastgltf::fastgltf fmt::fmt)



# Shaders
find_program(GLSL_VALIDATOR glslangValidator HINTS /usr/bin /usr/local/bin $ENV{VULKAN_SDK}/Bin/ $ENV{VULKAN_SDK}/Bin32/)

set(GLSL_SOURCE_FILES
        "${PROJECT_SOURCE_DIR}/shaders/gradient_pc.comp"
        "${PROJECT_SOURCE_DIR}/shaders/sky.comp"
        "${PROJECT_SOURCE_DIR}/shaders/mesh.frag"
        "${PROJECT_SOURCE_DIR}/shaders/mesh.vert"
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
