
# Add source to this project's executable.
add_executable(plume
    main.cpp
    vk_engine.cpp
    vk_engine.h
    vk_types.h
    vk_initializers.cpp
    vk_initializers.h
    vk_mesh.cpp
    vk_mesh.h
    vk_textures.cpp
    vk_textures.h)


set_property(TARGET plume PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:plume>")

target_include_directories(plume PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(plume vkbootstrap vma glm tinyobjloader imgui stb_image)

target_link_libraries(plume Vulkan::Vulkan sdl2)

add_dependencies(plume Shaders)
