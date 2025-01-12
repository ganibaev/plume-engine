cmake_minimum_required (VERSION 3.18)

project ("plume"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(Vulkan REQUIRED)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory(engine)

add_subdirectory(render)

add_subdirectory(third_party)

set_property(TARGET plume PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:plume>")