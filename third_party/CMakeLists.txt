find_package(Vulkan REQUIRED)

add_library(vkbootstrap STATIC)
add_library(glm INTERFACE)
add_library(vma INTERFACE)

add_library(stb_image INTERFACE)

add_library(tinyobjloader STATIC)

set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL tests")

add_subdirectory(SDL)

target_sources(vkbootstrap PRIVATE 
    vkbootstrap/VkBootstrap.h
    vkbootstrap/VkBootstrap.cpp
    )

target_include_directories(vkbootstrap PUBLIC vkbootstrap)
target_link_libraries(vkbootstrap PUBLIC Vulkan::Vulkan $<$<BOOL:UNIX>:${CMAKE_DL_LIBS}>)

#both vma and glm and header only libs so we only need the include path
target_include_directories(vma INTERFACE vma)
target_include_directories(glm INTERFACE glm)

target_sources(tinyobjloader PRIVATE 
    tinyobjloader/tiny_obj_loader.h
    tinyobjloader/tiny_obj_loader.cc
    )

target_include_directories(tinyobjloader PUBLIC tinyobjloader)

add_library(imgui STATIC)

target_include_directories(imgui PUBLIC imgui)

target_sources(imgui PRIVATE 
    imgui/imgui.h
    imgui/imgui.cpp

    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp

    imgui/imgui_impl_vulkan.cpp
    imgui/imgui_impl_sdl3.cpp
    )

target_link_libraries(imgui PUBLIC Vulkan::Vulkan SDL3::SDL3)

set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "Build assimp tools")
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "Build assimp tests")
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "Build assimp samples")

set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "Build importer")

set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "Build importer")

set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "Build importer")

set(ASSIMP_INSTALL OFF CACHE BOOL "Use as submodule")

set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "No exporters by default")

set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "No importers by default")

add_subdirectory(assimp)

target_include_directories(stb_image INTERFACE stb_image)
