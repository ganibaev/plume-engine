# Add source to this project's executable.
target_sources(plume PUBLIC
    render_camera.cpp
    render_camera.h
    render_system.cpp
    render_system.h
    render_descriptors.cpp
    render_descriptors.h
    render_initializers.cpp
    render_initializers.h
    render_mesh.cpp
    render_mesh.h
    render_raytracing.cpp
    render_raytracing.h
    render_textures.cpp
    render_textures.h
    render_types.h
    render_cfg.h)

target_include_directories(plume PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/third_party/assimp/include"
    "${CMAKE_BINARY_DIR}/third_party/SDL/include")

target_link_libraries(plume vkbootstrap vma glm tinyobjloader imgui stb_image assimp)

target_link_libraries(plume Vulkan::Vulkan SDL3::SDL3)

add_dependencies(plume Shaders)

find_program(GLSL_VALIDATOR glslangValidator HINTS /usr/bin /usr/local/bin $ENV{VULKAN_SDK}/Bin/ $ENV{VULKAN_SDK}/Bin32/)

## find all the shader files under the shaders folder
file(GLOB_RECURSE GLSL_SOURCE_FILES
    "${PROJECT_SOURCE_DIR}/render/shaders/*.frag"
    "${PROJECT_SOURCE_DIR}/render/shaders/*.vert"
    "${PROJECT_SOURCE_DIR}/render/shaders/*.comp"
    "${PROJECT_SOURCE_DIR}/render/shaders/*.rgen"
    "${PROJECT_SOURCE_DIR}/render/shaders/*.rchit"
    "${PROJECT_SOURCE_DIR}/render/shaders/*.rahit"
    "${PROJECT_SOURCE_DIR}/render/shaders/*.rmiss"
    )

## iterate each shader
foreach(GLSL ${GLSL_SOURCE_FILES})
  message(STATUS "BUILDING SHADER")
  get_filename_component(FILE_NAME ${GLSL} NAME)
  set(SPIRV "${PROJECT_SOURCE_DIR}/render/shader_binaries/${FILE_NAME}.spv")
  message(STATUS ${GLSL})
  ##execute glslang command to compile that specific shader
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV} --target-env spirv1.4 -g
    DEPENDS ${GLSL})
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

add_custom_target(
    Shaders 
    DEPENDS ${SPIRV_BINARY_FILES}
    )