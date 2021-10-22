cmake_minimum_required(VERSION 3.7...3.21)

if(${CMAKE_VERSION} VERSION_LESS 3.12)
    cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
endif()

project(RayBoy LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(external/SDL)
add_subdirectory(external/Vulkan-Headers)
add_subdirectory(external/Vulkan-Loader)

set(shader_src
    src/xor.comp
    # Add more shader sources here
)
set(shader_binary
    xor.comp.h
    # Add more shader binaries here
)

foreach(src binary IN ZIP_LISTS shader_src shader_binary)
    get_filename_component(shader_bin_name ${shader_binary} NAME_WE)
    add_custom_command(
        OUTPUT ${binary}
        COMMAND glslangValidator --target-env vulkan1.2 --iy --vn ${shader_bin_name}_shader_binary ${CMAKE_CURRENT_SOURCE_DIR}/${src} -o ${binary}
        MAIN_DEPENDENCY ${src}
        IMPLICIT_DEPENDS C ${src}
    )
endforeach()

if (WIN32)
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_WIN32_KHR)
elseif(UNIX AND NOT APPLE)
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_XLIB_KHR)
elseif(APPLE)
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_MACOS_MVK)
endif()
add_subdirectory(external/volk)
add_executable(rayboy
    external/vk_mem_alloc.cc
    external/stb_image.cc
    src/main.cc
    src/context.cc
    src/device.cc
    src/helpers.cc
    src/reaper.cc
    src/vkres.cc
    src/render_stage.cc
    src/render_target.cc
    src/xor_render_stage.cc
    src/render_pipeline.cc
    src/plain_render_pipeline.cc
    src/timer.cc
    src/math.cc
    src/mesh.cc
    src/material.cc
    src/texture.cc
    ${shader_binary}
)
target_include_directories(rayboy PUBLIC
    "external"
    "external/glm"
    "external/Vulkan-Headers/include"
    "external/VulkanMemoryAllocator/include"
    "external/volk"
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_compile_features(rayboy PUBLIC cxx_std_17)
target_link_libraries(rayboy PUBLIC SDL2)
target_link_libraries(rayboy PUBLIC vulkan)
target_link_libraries(rayboy PRIVATE volk)

# https://stackoverflow.com/questions/57478571/why-wont-cmake-build-my-vulkan-spirv-shaders
