cmake_minimum_required (VERSION 3.12)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project ("Engine" LANGUAGES C CXX)

# Ensure consistent MSVC runtime across all targets (prevents CRT/linker unresolved externals)
if (MSVC)
    # Use the DLL runtime (/MD, /MDd)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

    # Prefer 64-bit tools/libraries when generating with Ninja inside Visual Studio.
    # This avoids accidental linkage against x86 Windows SDK libs.
    set(CMAKE_VS_PLATFORM_NAME "x64" CACHE STRING "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_subdirectory(external/SDL3 EXCLUDE_FROM_ALL)
add_subdirectory(src/AssetManager)
add_subdirectory(src/Diagnostics)
add_subdirectory(src/Basics)
add_subdirectory(src/Renderer)
add_subdirectory(src/Logger)

add_executable(Engine
    src/main.cpp
)

# Engine is a console executable
set_target_properties(Engine PROPERTIES WIN32_EXECUTABLE OFF)

target_include_directories(Engine
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/Renderer
        ${CMAKE_SOURCE_DIR}/src/Logger
        ${CMAKE_SOURCE_DIR}/src/AssetManager
        ${CMAKE_SOURCE_DIR}/src/Diagnostics
        ${CMAKE_SOURCE_DIR}/src/Basics
)

target_link_libraries(Engine
    PRIVATE
        SDL3::SDL3
        Renderer
        Logger
        AssetManager
        Diagnostics
        Basics
)

set(ENGINE_DEPLOY_DIR "C:/Users/conno/source/repos/EngineBuild")

# Always copy from the source tree to avoid stale files in the build directory.
# NOTE: There are two shader directories in the repo; use Renderer/OpenGLRenderer/shaders as the authoritative one.
set(SHADERS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/OpenGLRenderer/shaders")

add_custom_command(TARGET Engine POST_BUILD
    # Ensure deploy directory exists
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ENGINE_DEPLOY_DIR}"

    # Copy Engine exe to deploy directory
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:Engine>" "${ENGINE_DEPLOY_DIR}/"

    # Copy SDL3 (and other runtime) dlls next to exe into deploy directory
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:Engine> "${ENGINE_DEPLOY_DIR}/"

    # Clean shaders output dir first so removed/renamed shaders don't stay around
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${ENGINE_DEPLOY_DIR}/shaders"

    # Copy shaders to deploy directory
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHADERS_SOURCE_DIR}" "${ENGINE_DEPLOY_DIR}/shaders"

    COMMAND_EXPAND_LISTS
)