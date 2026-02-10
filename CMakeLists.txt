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

    if (CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} /PROFILE")
        set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /PROFILE")
    endif()
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_subdirectory(external/SDL3 EXCLUDE_FROM_ALL)
add_subdirectory(external/freetype)
add_subdirectory(src/AssetManager)
add_subdirectory(src/Diagnostics)
add_subdirectory(src/Core)
add_subdirectory(src/Renderer)
add_subdirectory(src/Logger)

add_executable(Engine
    src/main.cpp
 "src/Core/Asset.h")

# Engine is a console executable
set_target_properties(Engine PROPERTIES WIN32_EXECUTABLE OFF)

target_include_directories(Engine
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/Renderer
        ${CMAKE_SOURCE_DIR}/src/Logger
        ${CMAKE_SOURCE_DIR}/src/AssetManager
        ${CMAKE_SOURCE_DIR}/src/Diagnostics
        ${CMAKE_SOURCE_DIR}/src/Core
)

target_link_libraries(Engine
    PRIVATE
        SDL3::SDL3
        Renderer
        Logger
        AssetManager
        Diagnostics
        Core
)

set(ENGINE_DEPLOY_DIR "C:/Users/conno/source/repos/EngineBuild")

# Build outputs (exe + DLLs) should go directly to the deploy directory.
# This is more reliable than post-build copying, especially with Ninja.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${ENGINE_DEPLOY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${ENGINE_DEPLOY_DIR}")
set(CMAKE_PDB_OUTPUT_DIRECTORY "${ENGINE_DEPLOY_DIR}")
set(CMAKE_PDB_OUTPUT_DIRECTORY_DEBUG "${ENGINE_DEPLOY_DIR}")
set(CMAKE_PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO "${ENGINE_DEPLOY_DIR}")
set(CMAKE_PDB_OUTPUT_DIRECTORY_RELEASE "${ENGINE_DEPLOY_DIR}")
set(CMAKE_PDB_OUTPUT_DIRECTORY_MINSIZEREL "${ENGINE_DEPLOY_DIR}")

set_target_properties(Engine PROPERTIES
    PDB_OUTPUT_DIRECTORY "${ENGINE_DEPLOY_DIR}"
    PDB_OUTPUT_DIRECTORY_DEBUG "${ENGINE_DEPLOY_DIR}"
    PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO "${ENGINE_DEPLOY_DIR}"
    PDB_OUTPUT_DIRECTORY_RELEASE "${ENGINE_DEPLOY_DIR}"
    PDB_OUTPUT_DIRECTORY_MINSIZEREL "${ENGINE_DEPLOY_DIR}"
)

# Always copy from the source tree to avoid stale files in the build directory.
# NOTE: There are two shader directories in the repo; use Renderer/OpenGLRenderer/shaders as the authoritative one.
set(SHADERS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/OpenGLRenderer/shaders")
set(CONTENT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Content")

add_custom_command(TARGET Engine POST_BUILD
    # Ensure deploy directory exists
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ENGINE_DEPLOY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ENGINE_DEPLOY_DIR}/Content/Textures"

    # Copy Engine exe to deploy directory
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Engine>" "${ENGINE_DEPLOY_DIR}/$<TARGET_FILE_NAME:Engine>"

    # Copy all runtime DLLs needed by Engine to deploy directory
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:Engine> "${ENGINE_DEPLOY_DIR}"

    # Clean shaders output dir first so removed/renamed shaders don't stay around
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${ENGINE_DEPLOY_DIR}/shaders"

    # Copy shaders to deploy directory
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHADERS_SOURCE_DIR}" "${ENGINE_DEPLOY_DIR}/shaders"

    COMMAND_EXPAND_LISTS
)