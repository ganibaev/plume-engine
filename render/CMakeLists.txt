# Add source to this project's executable.
target_sources(plume PUBLIC
    render_system.cpp
    render_system.h
    render_descriptors.cpp
    render_descriptors.h
    render_initializers.cpp
    render_initializers.h
    render_shader.cpp
    render_shader.h
    render_core.cpp
    render_core.h
    render_rt_backend_utils.cpp
    render_rt_backend_utils.h
    render_texture_utils.cpp
    render_texture_utils.h
    render_types.h
    render_cfg.h
    render_lights.cpp
    render_lights.h
    render_path_tracing.cpp
    render_path_tracing.h
)

target_include_directories(plume PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/third_party/assimp/include"
    "${CMAKE_BINARY_DIR}/third_party/SDL/include"
)

target_link_libraries(plume vkbootstrap vma glm tinyobjloader imgui stb_image assimp)

target_link_libraries(plume Vulkan::Vulkan SDL3::SDL3)

add_dependencies(plume Shaders)

find_program(GLSL_VALIDATOR glslangValidator HINTS /usr/bin /usr/local/bin $ENV{VULKAN_SDK}/Bin/ $ENV{VULKAN_SDK}/Bin32/)

# Find all shader files under the shaders folder
file(GLOB_RECURSE GLSL_SOURCE_FILES
    "${PROJECT_SOURCE_DIR}/render/shaders/*.frag"
    "${PROJECT_SOURCE_DIR}/render/shaders/*.vert"
    "${PROJECT_SOURCE_DIR}/render/shaders/*.comp"
    "${PROJECT_SOURCE_DIR}/render/shaders/*.rgen"
    "${PROJECT_SOURCE_DIR}/render/shaders/*.rchit"
    "${PROJECT_SOURCE_DIR}/render/shaders/*.rahit"
    "${PROJECT_SOURCE_DIR}/render/shaders/*.rmiss"
)

# Iterate through all shaders
foreach(GLSL ${GLSL_SOURCE_FILES})
  message(STATUS "BUILDING SHADER")
  get_filename_component(FILE_NAME ${GLSL} NAME)
  set(SPIRV "${PROJECT_SOURCE_DIR}/render/shader_binaries/${FILE_NAME}.spv")
  message(STATUS ${GLSL})
  # Compile shader with glslang
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV} --target-env spirv1.4 -g --preamble-text "#extension GL_ARB_shading_language_include : enable"
    DEPENDS ${GLSL})
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

file(GLOB_RECURSE GLSL_HEADER_FILES
    "${PROJECT_SOURCE_DIR}/render/shaders/*.h"
    "${PROJECT_SOURCE_DIR}/render/shaders/*.glsl"
)

add_custom_target(
    Shaders 
    DEPENDS ${SPIRV_BINARY_FILES}
    SOURCES ${GLSL_SOURCE_FILES} ${GLSL_HEADER_FILES}
)

# Make shader sources and headers visible in Visual Studio
set_source_files_properties(${GLSL_SOURCE_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
set_source_files_properties(${GLSL_HEADER_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
