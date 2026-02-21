cmake_minimum_required (VERSION 3.12)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project ("Engine" LANGUAGES C CXX)

# Prefer shared libraries for subprojects by default.
set(BUILD_SHARED_LIBS ON CACHE BOOL "Prefer shared libraries for subprojects" FORCE)

# Remove the default debug postfix ("d") from DLL names.
set(CMAKE_DEBUG_POSTFIX "" CACHE STRING "" FORCE)

set(Python3_USE_DEBUG_LIBS OFF CACHE BOOL "" FORCE)
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
if (Python3_LIBRARY_RELEASE)
    set(Python3_LIBRARY_DEBUG ${Python3_LIBRARY_RELEASE} CACHE FILEPATH "" FORCE)
endif()

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

# Build SDL as a shared library.
set(SDL_SHARED ON CACHE BOOL "Build SDL as shared library" FORCE)
set(SDL_STATIC OFF CACHE BOOL "Build SDL as static library" FORCE)
add_subdirectory(external/SDL3 EXCLUDE_FROM_ALL)
set(ASSIMP_BUILD_SHARED_LIBS OFF CACHE BOOL "Build assimp as shared library" FORCE)
set(ASSIMP_INJECT_DEBUG_POSTFIX OFF CACHE BOOL "No debug postfix for assimp" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "Disable assimp tests" FORCE)

# Build Assimp and freetype as static libraries so they are embedded in their consumers.
set(ASSIMP_BUILD_SHARED_LIBS OFF CACHE BOOL "Build assimp as shared" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Prefer static for assimp/freetype" FORCE)
add_subdirectory(external/assimp EXCLUDE_FROM_ALL)
add_subdirectory(external/freetype)
set(BUILD_SHARED_LIBS ON CACHE BOOL "Prefer shared libraries for subprojects" FORCE)
set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALSOFT_TESTS OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(external/openal-soft EXCLUDE_FROM_ALL)
add_subdirectory(src/AssetManager)
add_subdirectory(src/Diagnostics)
add_subdirectory(src/Logger)
add_subdirectory(src/Core)
add_subdirectory(src/Renderer)
add_subdirectory(src/Scripting)
add_subdirectory(src/Landscape)

if (NOT TARGET OpenAL::OpenAL)
    message(FATAL_ERROR "OpenAL target not available from external/openal-soft.")
endif()

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
        ${CMAKE_SOURCE_DIR}/src/Core
        ${CMAKE_SOURCE_DIR}/src/Landscape
        ${Python3_INCLUDE_DIRS}
)

target_link_libraries(Engine
    PRIVATE
        SDL3::SDL3
        Renderer
        Logger
        AssetManager
        Diagnostics
        Core
        Scripting
        Landscape
        ${Python3_LIBRARY_RELEASE}
)

if (Python3_LIBRARY_RELEASE)
    get_filename_component(PYTHON_LIB_BASENAME ${Python3_LIBRARY_RELEASE} NAME_WE)
    set(PYTHON_DEBUG_LIB_DIR "${CMAKE_BINARY_DIR}/python")
    set(PYTHON_DEBUG_LIB "${PYTHON_DEBUG_LIB_DIR}/${PYTHON_LIB_BASENAME}_d.lib")
    target_link_directories(Engine PRIVATE "${PYTHON_DEBUG_LIB_DIR}")
    add_custom_command(TARGET Engine PRE_LINK
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PYTHON_DEBUG_LIB_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${Python3_LIBRARY_RELEASE}" "${PYTHON_DEBUG_LIB}"
    )
endif()

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

    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${Python3_RUNTIME_LIBRARY_RELEASE}" "${ENGINE_DEPLOY_DIR}"

    COMMAND_EXPAND_LISTS
)