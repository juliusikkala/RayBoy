cmake_minimum_required(VERSION 3.19)

project(RayBoy LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(FORCE_VENDOR_ALL "Always use vendored versions of libraries" OFF)

find_package(Threads)
if(NOT FORCE_VENDOR_ALL)
    find_package(SDL2)
    find_package(Vulkan)
endif()

# TODO: Clean this up so that Windows build wouldn't require manually fetching
# the library...
if (NOT SDL2_FOUND AND NOT WIN32)
    add_subdirectory(external/SDL)
endif()
if (NOT Vulkan_FOUND)
    add_subdirectory(external/Vulkan-Headers)
    add_subdirectory(external/Vulkan-Loader)
endif()

set(KTX_FEATURE_GL_UPLOAD OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_DOC OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TOOLS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TESTS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_LOADTEST_APPS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
add_subdirectory(external/KTX-Software)

set(shader_src
    src/xor.comp
    src/forward.vert
    src/forward.frag
    src/tonemap.comp
    src/tonemap_msaa.comp
    src/emulator_transform.comp
    src/depth.vert
    src/depth.frag
    src/generate.frag
    src/gather.frag
    # Add more shader sources here
)
set(shader_binary
    xor.comp.h
    forward.vert.h
    forward.frag.h
    tonemap.comp.h
    tonemap_msaa.comp.h
    emulator_transform.comp.h
    depth.vert.h
    depth.frag.h
    generate.frag.h
    gather.frag.h
    # Add more shader binaries here
)

foreach(src binary IN ZIP_LISTS shader_src shader_binary)
    get_filename_component(shader_bin_name ${src} NAME)
    string(REPLACE "\." "_" shader_bin_name ${shader_bin_name})
    add_custom_command(
        OUTPUT ${binary}
        COMMAND glslangValidator --target-env vulkan1.2 --vn ${shader_bin_name}_shader_binary ${CMAKE_CURRENT_SOURCE_DIR}/${src} -o ${binary}
        MAIN_DEPENDENCY ${src}
        DEPENDS ${src}
        IMPLICIT_DEPENDS CXX ${src}
        VERBATIM
    )
endforeach()

if (WIN32)
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_WIN32_KHR)
    set(EXTRA_WIN_SRC WIN32 external/bsd-asprintf.c data/appicon.rc)
elseif(UNIX AND NOT APPLE)
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_XLIB_KHR)
elseif(APPLE)
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_MACOS_MVK)
endif()
set(VOLK_STATIC_DEFINES ${VOLK_STATIC_DEFINES} VOLK_VULKAN_H_PATH="../Vulkan-Headers/include/vulkan/vulkan.h")
set(VOLK_PULL_IN_VULKAN OFF)
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
if (MSVC)
    target_compile_options(SameBoy PRIVATE
        /w /Dssize_t=int64_t
    )
else()
    target_compile_options(SameBoy PRIVATE
        -Wno-missing-braces -Wno-nonnull -Wno-unused-result -Wno-strict-aliasing
        -Wno-multichar -Wno-int-in-bool-context -Wno-format-truncation
        -Wno-implicit-function-declaration -O3
    )
endif()

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
    ${EXTRA_WIN_SRC}
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
    src/error.cc
    src/environment_map.cc
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
    "external/KTX-Software/include"
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_compile_features(rayboy PUBLIC cxx_std_17)
if (Vulkan_FOUND)
    target_include_directories(rayboy PUBLIC "external/Vulkan-Headers/include")
    if (NOT WIN32)
        target_link_libraries(rayboy PUBLIC Vulkan::Vulkan)
    endif()
else()
    target_include_directories(rayboy PUBLIC "external/Vulkan-Headers/include")
    if (NOT WIN32)
        target_link_libraries(rayboy PUBLIC vulkan)
    endif()
endif()
if (SDL2_FOUND)
    target_link_libraries(rayboy PUBLIC SDL2::SDL2)
elseif (WIN32)
    target_link_directories(rayboy PUBLIC "external/SDL/VisualC/x64/Release")
    target_link_libraries(rayboy PUBLIC SDL2 SDL2main)
else()
    target_link_libraries(rayboy PUBLIC SDL2)
endif()
target_link_libraries(rayboy PUBLIC soloud)
target_link_libraries(rayboy PUBLIC Threads::Threads)
target_link_libraries(rayboy PRIVATE volk)
target_link_libraries(rayboy PRIVATE SameBoy)
target_link_libraries(rayboy PUBLIC ktx)

if (WIN32)
    set(NFD_LIB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/external/nativefiledialog/build/lib/Release/x64/nfd.lib)
    add_custom_target(nativefiledialog
        COMMAND msbuild nfd.vcxproj
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/external/nativefiledialog/build/vs2010
    )
    target_link_libraries(rayboy PUBLIC ${NFD_LIB_PATH})
    
    #set_property(TARGET rayboy PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
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

set_property(TARGET rayboy PROPERTY C_STANDARD 11)
set_property(TARGET rayboy PROPERTY CXX_STANDARD 17)
target_link_libraries(rayboy PUBLIC ${GTK_LIBRARIES})
add_dependencies(rayboy nativefiledialog)

target_compile_definitions(rayboy PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
