cmake_minimum_required (VERSION 3.12)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project ("Engine" LANGUAGES C CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_subdirectory(external/SDL3 EXCLUDE_FROM_ALL)
add_subdirectory(src/Renderer)
add_subdirectory(src/Logger)
add_subdirectory(src/AssetManager)
add_subdirectory(src/Diagnostics)

add_executable(Engine
    src/main.cpp
    src/AssetManager/AssetManager.h
)

target_link_libraries(Engine
    PRIVATE
        SDL3::SDL3
        Renderer
        Logger
        AssetManager
        Diagnostics
)

add_custom_command(TARGET Engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/OpenGLRenderer/shaders
            $<TARGET_FILE_DIR:Engine>/shaders
)