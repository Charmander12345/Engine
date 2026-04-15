cmake_minimum_required (VERSION 3.12)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Position-independent code for all targets (required on Linux when static
# libraries are linked into shared libraries).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

project ("HorizonEngine" LANGUAGES C CXX)

# Prefer shared libraries for subprojects by default.
set(BUILD_SHARED_LIBS ON CACHE BOOL "Prefer shared libraries for subprojects" FORCE)

# Remove the default debug postfix ("d") from DLL names.
set(CMAKE_DEBUG_POSTFIX "" CACHE STRING "" FORCE)

set(Python3_USE_DEBUG_LIBS OFF CACHE BOOL "" FORCE)

# Prefer the python.org system install over the Windows Store sandbox version.
# The Store version lives in a protected WindowsApps directory whose version
# string changes with every update, breaking the CMake cache silently.
if(WIN32)
    # Collect all standard per-user / system-wide install locations.
    set(_py_hints
        "$ENV{LOCALAPPDATA}/Programs/Python"
        "C:/Python313"
        "C:/Python312"
        "C:/Python311"
        "C:/Python310"
    )
    # If PYTHON_HOME or PYTHONHOME is set, prepend it.
    if(DEFINED ENV{PYTHON_HOME})
        list(PREPEND _py_hints "$ENV{PYTHON_HOME}")
    endif()
    if(DEFINED ENV{PYTHONHOME})
        list(PREPEND _py_hints "$ENV{PYTHONHOME}")
    endif()
    set(Python3_ROOT_DIR "" CACHE PATH "Override: root of the Python 3 install to use")
    if(Python3_ROOT_DIR)
        list(PREPEND _py_hints "${Python3_ROOT_DIR}")
    endif()
    # Suppress the Store stub (python.exe in WindowsApps is a redirect without libs).
    set(Python3_FIND_REGISTRY LAST)
    set(Python3_FIND_VIRTUALENV FIRST)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
if(WIN32 AND Python3_LIBRARY_RELEASE)
    set(Python3_LIBRARY_DEBUG ${Python3_LIBRARY_RELEASE} CACHE FILEPATH "" FORCE)
endif()

# Cross-platform Python library reference.
if(Python3_LIBRARY_RELEASE)
    set(ENGINE_PYTHON_LIB ${Python3_LIBRARY_RELEASE})
elseif(TARGET Python3::Python)
    set(ENGINE_PYTHON_LIB Python3::Python)
else()
    set(ENGINE_PYTHON_LIB ${Python3_LIBRARIES})
endif()

# Ensure consistent MSVC runtime across all targets (prevents CRT/linker unresolved externals)
if (MSVC)
    # Use the DLL runtime (/MD, /MDd)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

    # Force synchronous PDB writes so parallel CL.EXE processes don't conflict.
    add_compile_options(/FS)

    # Prefer 64-bit tools/libraries when generating with Ninja inside Visual Studio.
    # This avoids accidental linkage against x86 Windows SDK libs.
    set(CMAKE_VS_PLATFORM_NAME "x64" CACHE STRING "" FORCE)

    if (CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} /PROFILE /INCREMENTAL:NO")
        set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /PROFILE /INCREMENTAL:NO")
    endif()
endif()

# GCC / Clang common warning flags.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

# OpenGL & threading – needed on all platforms for the renderer.
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# For multi-config generators (Visual Studio), keep per-config output
# directories separate to avoid Debug/Release artifact collisions.
foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    string(TOUPPER ${_cfg} _cfg_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_cfg_upper} "${CMAKE_BINARY_DIR}/${_cfg}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_cfg_upper} "${CMAKE_BINARY_DIR}/${_cfg}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_cfg_upper} "${CMAKE_BINARY_DIR}/${_cfg}")
endforeach()

# Build SDL as a shared library.
set(SDL_SHARED ON CACHE BOOL "Build SDL as shared library" FORCE)
set(SDL_STATIC OFF CACHE BOOL "Build SDL as static library" FORCE)
add_subdirectory(external/SDL3 EXCLUDE_FROM_ALL)
set(ASSIMP_BUILD_SHARED_LIBS OFF CACHE BOOL "Build assimp as shared library" FORCE)
set(ASSIMP_INJECT_DEBUG_POSTFIX ON CACHE BOOL "Use debug postfix for assimp to avoid multi-config artifact collisions" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "Disable assimp tests" FORCE)

# Build Assimp and freetype as static libraries so they are embedded in their consumers.
set(ASSIMP_BUILD_SHARED_LIBS OFF CACHE BOOL "Build assimp as shared" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Prefer static for assimp/freetype" FORCE)
add_subdirectory(external/assimp EXCLUDE_FROM_ALL)
if (MSVC AND TARGET assimp)
    # Force DLL CRT selection per-config for assimp so it matches engine targets
    # when using multi-config generators.
    set_property(TARGET assimp PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    target_compile_options(assimp PRIVATE
        $<$<CONFIG:Debug>:/MDd>
        $<$<CONFIG:Release>:/MD>
        $<$<CONFIG:RelWithDebInfo>:/MD>
        $<$<CONFIG:MinSizeRel>:/MD>)
endif()
add_subdirectory(external/freetype)
set(BUILD_SHARED_LIBS ON CACHE BOOL "Prefer shared libraries for subprojects" FORCE)
set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALSOFT_TESTS OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL OFF CACHE BOOL "" FORCE)

# Avoid Debug/Release static-lib name collisions for OpenAL on multi-config
# generators when outputs are flattened into a single directory.
set(_engine_saved_debug_postfix "${CMAKE_DEBUG_POSTFIX}")
set(CMAKE_DEBUG_POSTFIX "d" CACHE STRING "" FORCE)
add_subdirectory(external/openal-soft EXCLUDE_FROM_ALL)
set(CMAKE_DEBUG_POSTFIX "${_engine_saved_debug_postfix}" CACHE STRING "" FORCE)

# Build Jolt Physics as a static library.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build Jolt as static" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "Use DLL MSVC runtime for Jolt" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "Let parent project control CXX flags" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "Disable LTO for Jolt" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "Disable warnings-as-errors for Jolt" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "Disable Jolt install target" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "Disable FP exceptions in Jolt" FORCE)
set(GENERATE_DEBUG_SYMBOLS OFF CACHE BOOL "Disable extra debug symbols for Jolt" FORCE)
set(JPH_USE_DX12 OFF CACHE BOOL "Disable DX12 compute for Jolt" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "Disable Vulkan compute for Jolt" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "Disable Metal compute for Jolt" FORCE)
set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "Disable CPU compute for Jolt" FORCE)
add_subdirectory(external/jolt/Build EXCLUDE_FROM_ALL)
set(BUILD_SHARED_LIBS ON CACHE BOOL "Prefer shared libraries for subprojects" FORCE)

# ── Physics backend ──────────────────────────────────────────────────────
# The engine currently builds and runs with Jolt only.

add_subdirectory(src/AssetManager)
add_subdirectory(src/Diagnostics)
add_subdirectory(src/Logger)
add_subdirectory(src/Core)
add_subdirectory(src/Renderer)
add_subdirectory(src/Scripting)
add_subdirectory(src/Landscape)
add_subdirectory(src/Physics)
add_subdirectory(src/Editor)

# ── Tests (GTest)
option(ENGINE_BUILD_TESTS "Build engine GTest suite" ON)
if(ENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if (NOT TARGET OpenAL::OpenAL)
    message(FATAL_ERROR "OpenAL target not available from external/openal-soft.")
endif()

# ── Editor-Separation (Phase 12): ENGINE_EDITOR preprocessor define ──
# ENGINE_EDITOR=1 → full editor build (default)
# ENGINE_EDITOR=0 → runtime-only build (no editor UI, no editor setup)
option(ENGINE_BUILD_RUNTIME "Build a runtime-only target without editor" ON)

# Common include directories shared by both targets.
set(ENGINE_COMMON_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/Renderer
    ${CMAKE_SOURCE_DIR}/src/Logger
    ${CMAKE_SOURCE_DIR}/src/AssetManager
    ${CMAKE_SOURCE_DIR}/src/Diagnostics
    ${CMAKE_SOURCE_DIR}/src/Core
    ${CMAKE_SOURCE_DIR}/src/Landscape
    ${CMAKE_SOURCE_DIR}/src/Physics
    ${CMAKE_SOURCE_DIR}/CrashHandler
    ${Python3_INCLUDE_DIRS}
)

# Common link libraries shared by both targets (minus Renderer – differs per target).
set(ENGINE_COMMON_LIBS_BASE
    SDL3::SDL3
    Logger
    Diagnostics
    Core
    Physics
    ${ENGINE_PYTHON_LIB}
    Threads::Threads
)

# Editor-specific libraries (link to editor AssetManager, Scripting, Landscape)
set(ENGINE_EDITOR_LIBS
    ${ENGINE_COMMON_LIBS_BASE}
    AssetManager
    Scripting
    Landscape
)

# Runtime-specific libraries (link to runtime variants – no assimp)
set(ENGINE_RUNTIME_LIBS
    ${ENGINE_COMMON_LIBS_BASE}
    AssetManagerRuntime
    ScriptingRuntime
    LandscapeRuntime
)

# ── Editor target (ENGINE_EDITOR=1) ─────────────────────────────────────
add_executable(HorizonEngine
    src/main.cpp
    src/BuildPipeline.cpp
    )

# Engine is a console executable (WIN32_EXECUTABLE is Windows-only).
if(WIN32)
    set_target_properties(HorizonEngine PROPERTIES WIN32_EXECUTABLE OFF)
endif()

target_include_directories(HorizonEngine PRIVATE ${ENGINE_COMMON_INCLUDES} ${CMAKE_SOURCE_DIR}/src/Editor)
target_link_libraries(HorizonEngine PRIVATE ${ENGINE_EDITOR_LIBS} Renderer Editor)
target_compile_definitions(HorizonEngine PRIVATE ENGINE_EDITOR=1
    ENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

# ── Runtime target (ENGINE_EDITOR=0) ────────────────────────────────────
if(ENGINE_BUILD_RUNTIME)
    add_executable(HorizonEngineRuntime
        src/main.cpp
    )
    if(WIN32)
        set_target_properties(HorizonEngineRuntime PROPERTIES WIN32_EXECUTABLE OFF)
    endif()
    target_include_directories(HorizonEngineRuntime PRIVATE ${ENGINE_COMMON_INCLUDES})
    target_link_libraries(HorizonEngineRuntime PRIVATE ${ENGINE_RUNTIME_LIBS} RendererRuntime)
    target_compile_definitions(HorizonEngineRuntime PRIVATE ENGINE_EDITOR=0)

    # When building a standalone game, bake the game configuration into the binary
    # so that the data cannot simply be swapped out.
    if(DEFINED GAME_START_LEVEL AND NOT "${GAME_START_LEVEL}" STREQUAL "")
        target_compile_definitions(HorizonEngineRuntime PRIVATE
            GAME_START_LEVEL="${GAME_START_LEVEL}"
        )
    endif()
    if(DEFINED GAME_WINDOW_TITLE AND NOT "${GAME_WINDOW_TITLE}" STREQUAL "")
        target_compile_definitions(HorizonEngineRuntime PRIVATE
            GAME_WINDOW_TITLE_BAKED="${GAME_WINDOW_TITLE}"
        )
    endif()

    # ── Build-profile compile definitions ────────────────────────────────
    # ENGINE_BUILD_PROFILE = "Debug" | "Development" | "Shipping" (default)
    # Each profile enables a unique define that controls which dev features
    # are compiled into the runtime binary.
    #   ENGINE_BUILD_SHIPPING    – pure game, no dev/debug features
    #   ENGINE_BUILD_DEVELOPMENT – metrics overlay (F10), basic diagnostics
    #   ENGINE_BUILD_DEBUG       – all dev tools, debug shortcuts, validation
    if(NOT DEFINED ENGINE_BUILD_PROFILE OR "${ENGINE_BUILD_PROFILE}" STREQUAL "")
        set(ENGINE_BUILD_PROFILE "Shipping")
    endif()
    if(ENGINE_BUILD_PROFILE STREQUAL "Debug")
        target_compile_definitions(HorizonEngineRuntime PRIVATE ENGINE_BUILD_DEBUG=1)
    elseif(ENGINE_BUILD_PROFILE STREQUAL "Development")
        target_compile_definitions(HorizonEngineRuntime PRIVATE ENGINE_BUILD_DEVELOPMENT=1)
    else()
        target_compile_definitions(HorizonEngineRuntime PRIVATE ENGINE_BUILD_SHIPPING=1)
    endif()

    # ── Delay-load engine DLLs for clean output layout ───────────────────
    # The game build places the exe in root and all DLLs in Engine/.
    # /DELAYLOAD defers DLL loading until first use so that SetDllDirectory()
    # in main() can redirect the loader to Engine/ before any DLL is needed.
    if(MSVC)
        set(_delay_targets SDL3::SDL3 Logger Diagnostics Core Physics
            AssetManagerRuntime ScriptingRuntime RendererRuntime)
        foreach(_t ${_delay_targets})
            if(TARGET ${_t})
                target_link_options(HorizonEngineRuntime PRIVATE
                    "/DELAYLOAD:$<TARGET_FILE_NAME:${_t}>")
            endif()
        endforeach()
        # NOTE: Python DELAYLOAD intentionally omitted – HorizonEngineRuntime
        # has no direct python imports; Python is loaded by ScriptingRuntime.dll.
        target_link_libraries(HorizonEngineRuntime PRIVATE delayimp)
    endif()
endif()

if(WIN32 AND Python3_LIBRARY_RELEASE)
    get_filename_component(PYTHON_LIB_BASENAME ${Python3_LIBRARY_RELEASE} NAME_WE)
    set(PYTHON_DEBUG_LIB_DIR "${CMAKE_BINARY_DIR}/python")
    set(PYTHON_DEBUG_LIB "${PYTHON_DEBUG_LIB_DIR}/${PYTHON_LIB_BASENAME}_d.lib")
    target_link_directories(HorizonEngine PRIVATE "${PYTHON_DEBUG_LIB_DIR}")
    add_custom_command(TARGET HorizonEngine PRE_LINK
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PYTHON_DEBUG_LIB_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${Python3_LIBRARY_RELEASE}" "${PYTHON_DEBUG_LIB}"
    )
    if(ENGINE_BUILD_RUNTIME)
        target_link_directories(HorizonEngineRuntime PRIVATE "${PYTHON_DEBUG_LIB_DIR}")
        add_custom_command(TARGET HorizonEngineRuntime PRE_LINK
            COMMAND ${CMAKE_COMMAND} -E make_directory "${PYTHON_DEBUG_LIB_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${Python3_LIBRARY_RELEASE}" "${PYTHON_DEBUG_LIB}"
        )
    endif()
endif()

if(NOT DEFINED ENGINE_DEPLOY_DIR)
    if(WIN32)
        set(ENGINE_DEPLOY_DIR "$ENV{USERPROFILE}/source/repos/EngineBuild")
    else()
        set(ENGINE_DEPLOY_DIR "${CMAKE_BINARY_DIR}/deploy")
    endif()
endif()

# Build outputs (exe + DLLs) should go directly to the deploy directory.
# This is more reliable than post-build copying, especially with Ninja.
# For multi-config generators, flatten per-config subdirectories.
# NOTE: This only affects targets created AFTER this point.  All engine
# targets are created earlier via add_subdirectory / add_executable, so
# they inherit the per-config output dirs from lines 61-72.  For the
# editor, POST_BUILD commands copy everything to ENGINE_DEPLOY_DIR.
# For game builds, BuildPipeline.cpp handles deployment and filters out
# editor-only DLLs (AssetManager.dll, Scripting.dll, Renderer.dll).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${ENGINE_DEPLOY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${ENGINE_DEPLOY_DIR}")
set(CMAKE_PDB_OUTPUT_DIRECTORY "${ENGINE_DEPLOY_DIR}")
foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    string(TOUPPER ${_cfg} _cfg_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_cfg_upper} "${ENGINE_DEPLOY_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_cfg_upper} "${ENGINE_DEPLOY_DIR}")
    set(CMAKE_PDB_OUTPUT_DIRECTORY_${_cfg_upper} "${ENGINE_DEPLOY_DIR}")
endforeach()

# Ensure the executable is built directly into the deploy directory so that
# SDL_GetBasePath() returns a path where Tools/ (headers, libs) already exist.
# Without this, VS launches from the CMake build tree and PIE builds fail
# because the generated GameScripts CMakeLists cannot find the headers/libs.
set_target_properties(HorizonEngine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${ENGINE_DEPLOY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${ENGINE_DEPLOY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${ENGINE_DEPLOY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${ENGINE_DEPLOY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${ENGINE_DEPLOY_DIR}"
)
if(ENGINE_BUILD_RUNTIME)
    set_target_properties(HorizonEngineRuntime PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${ENGINE_DEPLOY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${ENGINE_DEPLOY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${ENGINE_DEPLOY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${ENGINE_DEPLOY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${ENGINE_DEPLOY_DIR}"
    )
endif()

if(MSVC)
    set_target_properties(HorizonEngine PROPERTIES
        PDB_OUTPUT_DIRECTORY "${ENGINE_DEPLOY_DIR}"
        PDB_OUTPUT_DIRECTORY_DEBUG "${ENGINE_DEPLOY_DIR}"
        PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO "${ENGINE_DEPLOY_DIR}"
        PDB_OUTPUT_DIRECTORY_RELEASE "${ENGINE_DEPLOY_DIR}"
        PDB_OUTPUT_DIRECTORY_MINSIZEREL "${ENGINE_DEPLOY_DIR}"
    )
    if(ENGINE_BUILD_RUNTIME)
        set_target_properties(HorizonEngineRuntime PROPERTIES
            PDB_OUTPUT_DIRECTORY "${ENGINE_DEPLOY_DIR}"
            PDB_OUTPUT_DIRECTORY_DEBUG "${ENGINE_DEPLOY_DIR}"
            PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO "${ENGINE_DEPLOY_DIR}"
            PDB_OUTPUT_DIRECTORY_RELEASE "${ENGINE_DEPLOY_DIR}"
            PDB_OUTPUT_DIRECTORY_MINSIZEREL "${ENGINE_DEPLOY_DIR}"
        )
    endif()
endif()

# Always copy from the source tree to avoid stale files in the build directory.
# NOTE: There are two shader directories in the repo; use Renderer/OpenGLRenderer/shaders as the authoritative one.
set(SHADERS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/OpenGLRenderer/shaders")
set(CONTENT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Content")
set(EDITOR_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Editor")

add_custom_command(TARGET HorizonEngine POST_BUILD
    # Ensure deploy directory exists
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ENGINE_DEPLOY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ENGINE_DEPLOY_DIR}/Content/Textures"

    # Copy Engine executable to deploy directory (with retry to survive Defender locks)
    COMMAND ${CMAKE_COMMAND} -DSRC=$<TARGET_FILE:HorizonEngine> -DDST=${ENGINE_DEPLOY_DIR}/$<TARGET_FILE_NAME:HorizonEngine> -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/retry_copy.cmake

    # Clean shaders output dir first so removed/renamed shaders don't stay around
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${ENGINE_DEPLOY_DIR}/shaders"

    # Copy shaders to deploy directory
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHADERS_SOURCE_DIR}" "${ENGINE_DEPLOY_DIR}/shaders"

    # Copy engine Content directory (textures, skyboxes, etc.) to deploy directory
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CONTENT_SOURCE_DIR}" "${ENGINE_DEPLOY_DIR}/Content"

    COMMAND ${CMAKE_COMMAND} -E copy_directory "${EDITOR_SOURCE_DIR}" "${ENGINE_DEPLOY_DIR}/Editor"

    # Copy static engine.pyi type stub to deploy directory for project distribution
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ENGINE_DEPLOY_DIR}/Content/Scripting"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/src/Scripting/Python/engine.pyi" "${ENGINE_DEPLOY_DIR}/Content/Scripting/engine.pyi"

    # Copy tools/ bootstrap scripts to deploy directory so the editor can
    # locate and execute them at runtime (next to the exe, no ENGINE_SOURCE_DIR needed).
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ENGINE_DEPLOY_DIR}/tools"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootstrap.ps1" "${ENGINE_DEPLOY_DIR}/tools/bootstrap.ps1"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootstrap.sh"  "${ENGINE_DEPLOY_DIR}/tools/bootstrap.sh"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/tools/README.md"     "${ENGINE_DEPLOY_DIR}/tools/README.md"

    # Deploy a minimal bundled CMake (cmake.exe + Modules + Templates) so the
    # editor can build C++ game scripts even if CMake is not installed system-wide.
    # Clean first to remove stale files from previous bootstrap downloads.
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${ENGINE_DEPLOY_DIR}/Tools/cmake"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ENGINE_DEPLOY_DIR}/Tools/cmake/bin"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_COMMAND}" "${ENGINE_DEPLOY_DIR}/Tools/cmake/bin/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_ROOT}/Modules"   "${ENGINE_DEPLOY_DIR}/Tools/cmake/share/cmake-${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}/Modules"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_ROOT}/Templates" "${ENGINE_DEPLOY_DIR}/Tools/cmake/share/cmake-${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}/Templates"

    COMMAND_EXPAND_LISTS
)

# Windows-specific: copy runtime DLLs alongside the executable.
if(WIN32)
    add_custom_command(TARGET HorizonEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:HorizonEngine> "${ENGINE_DEPLOY_DIR}"
        COMMAND_EXPAND_LISTS
    )
    if(Python3_RUNTIME_LIBRARY_RELEASE)
        add_custom_command(TARGET HorizonEngine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${Python3_RUNTIME_LIBRARY_RELEASE}" "${ENGINE_DEPLOY_DIR}"
        )
    endif()
endif()

# ── Deploy import libraries (.lib) to Tools/lib/ ────────────────────────
# GameScripts (PIE) and lightweight game builds link against pre-built
# engine libraries, so we copy the .lib files next to the editor.
set(_ENGINE_LIB_DEPLOY_DIR "${ENGINE_DEPLOY_DIR}/Tools/lib")
add_custom_command(TARGET HorizonEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_ENGINE_LIB_DEPLOY_DIR}"
    # Shared library import libs (editor variants)
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_LINKER_FILE:Scripting>"      "${_ENGINE_LIB_DEPLOY_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_LINKER_FILE:Renderer>"       "${_ENGINE_LIB_DEPLOY_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_LINKER_FILE:AssetManager>"   "${_ENGINE_LIB_DEPLOY_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_LINKER_FILE:Core>"           "${_ENGINE_LIB_DEPLOY_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_LINKER_FILE:Logger>"         "${_ENGINE_LIB_DEPLOY_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_LINKER_FILE:Diagnostics>"    "${_ENGINE_LIB_DEPLOY_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_LINKER_FILE:Physics>"        "${_ENGINE_LIB_DEPLOY_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_LINKER_FILE:SDL3::SDL3>"     "${_ENGINE_LIB_DEPLOY_DIR}/"
    COMMAND_EXPAND_LISTS
)
if(ENGINE_BUILD_RUNTIME)
    # Ensure runtime shared libs are built when the editor is built so that
    # POST_BUILD deployment below can find the .lib/.dll files.
    add_dependencies(HorizonEngine ScriptingRuntime RendererRuntime AssetManagerRuntime LandscapeRuntime)

    add_custom_command(TARGET HorizonEngine POST_BUILD
        # Runtime-variant import libs (for game builds)
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_LINKER_FILE:ScriptingRuntime>"    "${_ENGINE_LIB_DEPLOY_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_LINKER_FILE:RendererRuntime>"     "${_ENGINE_LIB_DEPLOY_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_LINKER_FILE:AssetManagerRuntime>" "${_ENGINE_LIB_DEPLOY_DIR}/"
        # Runtime-variant DLLs (lightweight game build copies these to game output)
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:ScriptingRuntime>"    "${_ENGINE_LIB_DEPLOY_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:RendererRuntime>"     "${_ENGINE_LIB_DEPLOY_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:AssetManagerRuntime>" "${_ENGINE_LIB_DEPLOY_DIR}/"
        # Static libraries used by runtime target
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:LandscapeRuntime>"  "${_ENGINE_LIB_DEPLOY_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Landscape>"         "${_ENGINE_LIB_DEPLOY_DIR}/"
        COMMAND_EXPAND_LISTS
    )
endif()

# ── Deploy NativeScripting headers to Tools/include/ ────────────────────
# GameScripts projects need these headers to compile against the engine API.
set(_ENGINE_INCLUDE_DEPLOY_DIR "${ENGINE_DEPLOY_DIR}/Tools/include/NativeScripting")
add_custom_command(TARGET HorizonEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_ENGINE_INCLUDE_DEPLOY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/src/NativeScripting/GameplayAPIExport.h"  "${_ENGINE_INCLUDE_DEPLOY_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/src/NativeScripting/INativeScript.h"      "${_ENGINE_INCLUDE_DEPLOY_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/src/NativeScripting/NativeScriptRegistry.h" "${_ENGINE_INCLUDE_DEPLOY_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/src/NativeScripting/GameplayAPI.h"        "${_ENGINE_INCLUDE_DEPLOY_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/src/NativeScripting/NativeScriptManager.h" "${_ENGINE_INCLUDE_DEPLOY_DIR}/"
    COMMAND_EXPAND_LISTS
)

# ── Deploy SDL3 headers to Tools/include/ ────────────────────────────────
# Lightweight game builds need SDL3 headers to compile main.cpp.
add_custom_command(TARGET HorizonEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/external/SDL3/include/SDL3" "${ENGINE_DEPLOY_DIR}/Tools/include/SDL3"
    COMMAND_EXPAND_LISTS
)

# Copy the runtime executable to a Tools/ subdirectory so it is not directly
# visible next to the editor but Build Game can still locate it.
if(ENGINE_BUILD_RUNTIME)
    add_custom_command(TARGET HorizonEngineRuntime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ENGINE_DEPLOY_DIR}/Tools"
        COMMAND ${CMAKE_COMMAND} -DSRC=$<TARGET_FILE:HorizonEngineRuntime> -DDST=${ENGINE_DEPLOY_DIR}/Tools/$<TARGET_FILE_NAME:HorizonEngineRuntime> -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/retry_copy.cmake
        COMMAND_EXPAND_LISTS
    )
endif()

# ── Generate EngineConfig.cmake for lightweight game builds ─────────────
# This file is written at configure time and deployed next to the editor.
# BuildPipeline.cpp and EditorApp.cpp can read it to know library names,
# Python paths, etc. without running a full cmake configure on the engine tree.
set(_ENGINE_CONFIG_CONTENT "# Auto-generated by HorizonEngine CMake – do not edit\n")
string(APPEND _ENGINE_CONFIG_CONTENT "set(ENGINE_SOURCE_DIR \"${CMAKE_SOURCE_DIR}\")\n")
string(APPEND _ENGINE_CONFIG_CONTENT "set(ENGINE_DEPLOY_DIR \"${ENGINE_DEPLOY_DIR}\")\n")
string(APPEND _ENGINE_CONFIG_CONTENT "set(Python3_INCLUDE_DIRS \"${Python3_INCLUDE_DIRS}\")\n")
if(Python3_LIBRARY_RELEASE)
    string(APPEND _ENGINE_CONFIG_CONTENT "set(ENGINE_PYTHON_LIB \"${Python3_LIBRARY_RELEASE}\")\n")
else()
    string(APPEND _ENGINE_CONFIG_CONTENT "set(ENGINE_PYTHON_LIB \"${Python3_LIBRARIES}\")\n")
endif()
if(Python3_RUNTIME_LIBRARY_RELEASE)
    string(APPEND _ENGINE_CONFIG_CONTENT "set(Python3_RUNTIME_LIBRARY_RELEASE \"${Python3_RUNTIME_LIBRARY_RELEASE}\")\n")
endif()
file(WRITE "${CMAKE_BINARY_DIR}/EngineConfig.cmake" "${_ENGINE_CONFIG_CONTENT}")
add_custom_command(TARGET HorizonEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ENGINE_DEPLOY_DIR}/Tools"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/EngineConfig.cmake" "${ENGINE_DEPLOY_DIR}/Tools/EngineConfig.cmake"
)

# ── CrashHandler (out-of-process crash reporter) ────────────────────────
add_subdirectory(CrashHandler)