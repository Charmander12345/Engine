cmake_minimum_required (VERSION 3.12)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project ("Engine" LANGUAGES C CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_subdirectory(external/glad/cmake)
glad_add_library(glad_gl_core SHARED API gl:core=4.6)

add_subdirectory(external/SDL3 EXCLUDE_FROM_ALL)

add_executable(Engine
    src/main.cpp
)

target_link_libraries(Engine
    PRIVATE
        SDL3::SDL3
        glad_gl_core
)
