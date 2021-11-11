cmake_minimum_required(VERSION 3.19)

project(RayBoy LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads)
find_package(SDL2)
if (NOT SDL2_FOUND)
    add_subdirectory(external/SDL)
endif()
find_package(Vulkan)
if (NOT Vulkan_FOUND)
    add_subdirectory(external/Vulkan-Headers)
    add_subdirectory(external/Vulkan-Loader)
endif()

set(shader_src
    src/xor.comp
    src/forward.vert
    src/forward.frag
    src/tonemap.comp
    src/tonemap_msaa.comp
    src/emulator_transform.comp
    # Add more shader sources here
)
set(shader_binary
    xor.comp.h
    forward.vert.h
    forward.frag.h
    tonemap.comp.h
    tonemap_msaa.comp.h
    emulator_transform.comp.h
    # Add more shader binaries here
)

foreach(src binary IN ZIP_LISTS shader_src shader_binary)
    get_filename_component(shader_bin_name ${src} NAME)
    string(REPLACE "\." "_" shader_bin_name ${shader_bin_name})
    add_custom_command(
        OUTPUT ${binary}
        COMMAND glslangValidator --target-env vulkan1.2 --vn ${shader_bin_name}_shader_binary ${CMAKE_CURRENT_SOURCE_DIR}/${src} -o ${binary}
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

set(SAMEBOY_CORE_SRC
    external/SameBoy/Core/apu.c
    external/SameBoy/Core/camera.c
    external/SameBoy/Core/display.c
    external/SameBoy/Core/gb.c
    external/SameBoy/Core/joypad.c
    external/SameBoy/Core/mbc.c
    external/SameBoy/Core/memory.c
    external/SameBoy/Core/printer.c
    external/SameBoy/Core/random.c
    external/SameBoy/Core/rumble.c
    external/SameBoy/Core/save_state.c
    external/SameBoy/Core/sgb.c
    external/SameBoy/Core/sm83_cpu.c
    external/SameBoy/Core/symbol_hash.c
    external/SameBoy/Core/timing.c
    external/SameBoy/Core/workboy.c
)
add_library(SameBoy STATIC ${SAMEBOY_CORE_SRC})
target_compile_definitions(SameBoy PUBLIC GB_INTERNAL GB_DISABLE_TIMEKEEPING GB_DISABLE_REWIND GB_DISABLE_CHEATS GB_DISABLE_DEBUGGER GB_VERSION="0.14.7")
target_compile_options(SameBoy PRIVATE
    -Wno-missing-braces -Wno-nonnull -Wno-unused-result -Wno-strict-aliasing
    -Wno-multichar -Wno-int-in-bool-context -Wno-format-truncation
    -Wno-implicit-function-declaration -O3
) 

file(
    GLOB SOLOUD_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/external/soloud/src/core/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/external/soloud/src/backend/sdl2_static/*.cpp
)
add_library(soloud STATIC ${SOLOUD_SRC})
target_compile_definitions(soloud PUBLIC WITH_SDL2_STATIC)
target_include_directories(
    soloud PUBLIC
    "external/soloud/include"
    "external/SDL/include"
)

add_executable(rayboy
    external/vk_mem_alloc.cc
    external/stb_image.cc
    external/tiny_gltf.cc
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/backends/imgui_impl_vulkan.cpp
    external/imgui/backends/imgui_impl_sdl.cpp
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
    src/gpu_pipeline.cc
    src/compute_pipeline.cc
    src/fancy_render_pipeline.cc
    src/plain_render_pipeline.cc
    src/timer.cc
    src/math.cc
    src/mesh.cc
    src/material.cc
    src/texture.cc
    src/model.cc
    src/transformable.cc
    src/animation.cc
    src/ecs.cc
    src/sampler.cc
    src/light.cc
    src/camera.cc
    src/gltf.cc
    src/gpu_buffer.cc
    src/scene.cc
    src/scene_update_render_stage.cc
    src/graphics_pipeline.cc
    src/forward_render_stage.cc
    src/tonemap_render_stage.cc
    src/gui.cc
    src/gui_render_stage.cc
    src/io.cc
    src/options.cc
    src/blit_render_stage.cc
    src/emulator.cc
    src/emulator_render_stage.cc
    src/audio.cc
    src/game.cc
    ${shader_binary}
)
target_include_directories(rayboy PUBLIC
    "external"
    "external/glm"
    "external/VulkanMemoryAllocator/include"
    "external/nativefiledialog/src/include"
    "external/soloud/include"
    "external/SameBoy/Core"
    "external/volk"
    "external/imgui"
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_compile_features(rayboy PUBLIC cxx_std_17)
if (SDL2_FOUND)
    target_link_libraries(rayboy PUBLIC SDL2::SDL2)
else()
    target_link_libraries(rayboy PUBLIC SDL2)
endif()
if (Vulkan_FOUND)
    target_link_libraries(rayboy PUBLIC Vulkan::Vulkan)
else()
    target_include_directories(rayboy PUBLIC "external/Vulkan-Headers/include")
    target_link_libraries(rayboy PUBLIC vulkan)
endif()
target_link_libraries(rayboy PUBLIC soloud)
target_link_libraries(rayboy PUBLIC Threads::Threads)
target_link_libraries(rayboy PRIVATE volk)
target_link_libraries(rayboy PRIVATE SameBoy)

if (WIN32)
    set(NFD_LIB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/external/nativefiledialog/build/lib/Release/x64/nfd.lib)
    add_custom_target(nativefiledialog
        COMMAND make nfd
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/external/nativefiledialog/build/gmake_windows
    )
    target_link_libraries(rayboy PUBLIC ${NFD_LIB_PATH})
elseif(UNIX AND NOT APPLE)
    set(NFD_LIB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/external/nativefiledialog/build/lib/Release/x64/libnfd.a)
    add_custom_target(nativefiledialog
        COMMAND make nfd
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/external/nativefiledialog/build/gmake_linux
    )
    target_link_libraries(rayboy PUBLIC ${NFD_LIB_PATH})
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK REQUIRED gtk+-3.0)
elseif(APPLE)
    set(NFD_LIB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/external/nativefiledialog/build/lib/Release/x64/libnfd.a)
    add_custom_target(nativefiledialog
        COMMAND make nfd
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/external/nativefiledialog/build/gmake_macosx
    )
    target_link_libraries(rayboy PUBLIC ${NFD_LIB_PATH})
endif()

target_link_libraries(rayboy PUBLIC ${GTK_LIBRARIES})
add_dependencies(rayboy nativefiledialog)

target_compile_definitions(rayboy PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
