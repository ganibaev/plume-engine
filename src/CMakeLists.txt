# Add source to this project's executable.
add_executable(plume
    main.cpp
    vk_camera.cpp
    vk_camera.h
    vk_engine.cpp
    vk_engine.h
    vk_descriptors.cpp
    vk_descriptors.h
    vk_initializers.cpp
    vk_initializers.h
    vk_mesh.cpp
    vk_mesh.h
    vk_raytracing.cpp
    vk_raytracing.h
    vk_nrc.h
    vk_textures.cpp
    vk_textures.h
    vk_types.h
    vk_cfg.h)

set_property(TARGET plume PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:plume>")

get_target_property(TCNN_INCLUDE_DIRECTORIES tiny-cuda-nn INCLUDE_DIRECTORIES)

target_include_directories(plume PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/third_party/assimp/include"
    "${CMAKE_BINARY_DIR}/third_party/SDL/include"
    "${CMAKE_BINARY_DIR}/src/tcnn_wrapper"
    ${TCNN_INCLUDE_DIRECTORIES})

add_subdirectory(tcnn_wrapper)

target_compile_definitions(plume PUBLIC -DTCNN_MIN_GPU_ARCH=0)

set_property(TARGET tcnn_wrapper PROPERTY CUDA_RESOLVE_DEVICE_SYMBOLS ON)

target_link_libraries(plume vkbootstrap vma glm tinyobjloader imgui stb_image assimp tcnn_wrapper)

target_link_libraries(plume Vulkan::Vulkan SDL3::SDL3)

add_dependencies(plume Shaders)
