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
        set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} /PROFILE")
        set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /PROFILE")
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

# ── PhysX 5.x (optional, static library) ────────────────────────────────
option(ENGINE_PHYSX_BACKEND "Build the PhysX backend (requires external/PhysX)" ON)
if(ENGINE_PHYSX_BACKEND AND EXISTS "${CMAKE_SOURCE_DIR}/external/PhysX/physx")
    message(STATUS "PhysX backend enabled – configuring external/PhysX …")

    # Save CMAKE_CONFIGURATION_TYPES before PhysX overrides them.
    set(_engine_saved_config_types "${CMAKE_CONFIGURATION_TYPES}")

    # Required by PhysX build system.
    set(PHYSX_ROOT_DIR "${CMAKE_SOURCE_DIR}/external/PhysX/physx" CACHE PATH "" FORCE)
    if(WIN32)
        set(TARGET_BUILD_PLATFORM "windows" CACHE STRING "" FORCE)
    elseif(APPLE)
        set(TARGET_BUILD_PLATFORM "mac" CACHE STRING "" FORCE)
    elseif(UNIX)
        set(TARGET_BUILD_PLATFORM "linux" CACHE STRING "" FORCE)
    endif()
    set(PX_GENERATE_STATIC_LIBRARIES ON CACHE BOOL "Build PhysX as static libs" FORCE)
    set(PX_BUILDSNIPPETS OFF CACHE BOOL "" FORCE)
    set(PX_BUILDPVDRUNTIME OFF CACHE BOOL "" FORCE)
    set(PX_CMAKE_SUPPRESS_REGENERATION ON CACHE BOOL "" FORCE)
    set(PX_OUTPUT_LIB_DIR "${CMAKE_BINARY_DIR}/physx_lib" CACHE PATH "" FORCE)
    set(PX_OUTPUT_BIN_DIR "${CMAKE_BINARY_DIR}/physx_bin" CACHE PATH "" FORCE)
    set(PX_COPY_EXTERNAL_DLL OFF CACHE BOOL "Disable PhysX external DLL copy" FORCE)
    if(WIN32)
        set(NV_USE_STATIC_WINCRT OFF CACHE BOOL "Use DLL CRT for PhysX" FORCE)
        set(NV_USE_DEBUG_WINCRT ON CACHE BOOL "Use debug CRT for PhysX debug builds" FORCE)
    endif()

    # Ensure CMP0091 is NEW so CMAKE_MSVC_RUNTIME_LIBRARY controls the CRT.
    # PhysX replaces CMAKE_CXX_FLAGS_<CONFIG> without explicit /MD or /MT flags,
    # so without this policy the MSVC default (/MT) is silently used.
    cmake_policy(SET CMP0091 NEW)

    # PhysX public cmake sets PUBLIC_RELEASE=1 which skips internal NVIDIA files,
    # but also triggers a freeglut DLL copy on Windows.  Provide stub files so FILE(COPY) succeeds.
    if(WIN32)
        set(_physx_freeglut_stub "${CMAKE_BINARY_DIR}/_physx_freeglut_stub/win64")
        file(MAKE_DIRECTORY "${_physx_freeglut_stub}")
        file(TOUCH "${_physx_freeglut_stub}/freeglut.dll" "${_physx_freeglut_stub}/freeglutd.dll")
        set(PHYSX_SLN_FREEGLUT_PATH "${CMAKE_BINARY_DIR}/_physx_freeglut_stub" CACHE INTERNAL "" FORCE)
    endif()

    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build PhysX as static" FORCE)
    add_subdirectory(external/PhysX/physx/compiler/public EXCLUDE_FROM_ALL)
    set(BUILD_SHARED_LIBS ON CACHE BOOL "Prefer shared libraries for subprojects" FORCE)

    # Disable warnings-as-errors (/WX) for PhysX targets – our newer MSVC generates
    # conversion warnings inside PhysX that do not affect correctness.
    # Force DLL CRT (/MD, /MDd) on all PhysX targets to match the engine runtime.
    # PhysX replaces CMAKE_CXX_FLAGS_<CONFIG> wholesale, stripping the default CRT flag,
    # so we add it back explicitly (MSVC uses the last flag when duplicates appear).
    foreach(_px_target PhysX PhysXFoundation PhysXCommon PhysXExtensions PhysXCooking
                       PhysXPvdSDK PhysXTask PhysXCharacterKinematic PhysXVehicle2
                       LowLevel LowLevelAABB LowLevelDynamics
                       SceneQuery SimulationController)
        if(TARGET ${_px_target})
            if(MSVC)
                target_compile_options(${_px_target} PRIVATE
                    /WX-
                    $<$<CONFIG:Debug>:/MDd>
                    $<$<CONFIG:Release>:/MD>
                    $<$<CONFIG:RelWithDebInfo>:/MD>
                    $<$<CONFIG:MinSizeRel>:/MD>
                )
                set_property(TARGET ${_px_target} PROPERTY
                             MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
            endif()
        endif()
    endforeach()

    # Restore configuration types that PhysX force-overrode.
    if(_engine_saved_config_types)
        set(CMAKE_CONFIGURATION_TYPES "${_engine_saved_config_types}" CACHE STRING "" FORCE)
    endif()

    set(ENGINE_PHYSX_AVAILABLE TRUE)
    message(STATUS "PhysX targets available: PhysX, PhysXFoundation, PhysXCommon, PhysXExtensions")
else()
    set(ENGINE_PHYSX_AVAILABLE FALSE)
    if(ENGINE_PHYSX_BACKEND)
        message(WARNING "ENGINE_PHYSX_BACKEND=ON but external/PhysX not found – PhysX backend disabled.")
    endif()
endif()

add_subdirectory(src/AssetManager)
add_subdirectory(src/Diagnostics)
add_subdirectory(src/Logger)
add_subdirectory(src/Core)
add_subdirectory(src/Renderer)
add_subdirectory(src/Scripting)
add_subdirectory(src/Landscape)
add_subdirectory(src/Physics)

# ── Tests (GTest) ───────────────────────────────────────────────────────
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
    ${Python3_INCLUDE_DIRS}
)

# Common link libraries shared by both targets.
set(ENGINE_COMMON_LIBS
    SDL3::SDL3
    Renderer
    Logger
    AssetManager
    Diagnostics
    Core
    Scripting
    Landscape
    Physics
    ${ENGINE_PYTHON_LIB}
    Threads::Threads
)

# ── Editor target (ENGINE_EDITOR=1) ─────────────────────────────────────
add_executable(HorizonEngine
    src/main.cpp
    )

# Engine is a console executable (WIN32_EXECUTABLE is Windows-only).
if(WIN32)
    set_target_properties(HorizonEngine PROPERTIES WIN32_EXECUTABLE OFF)
endif()

target_include_directories(HorizonEngine PRIVATE ${ENGINE_COMMON_INCLUDES})
target_link_libraries(HorizonEngine PRIVATE ${ENGINE_COMMON_LIBS})
target_compile_definitions(HorizonEngine PRIVATE ENGINE_EDITOR=1
    ENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    CMAKE_GENERATOR="${CMAKE_GENERATOR}"
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
    target_link_libraries(HorizonEngineRuntime PRIVATE ${ENGINE_COMMON_LIBS})
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
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${ENGINE_DEPLOY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${ENGINE_DEPLOY_DIR}")
set(CMAKE_PDB_OUTPUT_DIRECTORY "${ENGINE_DEPLOY_DIR}")
foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    string(TOUPPER ${_cfg} _cfg_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_cfg_upper} "${ENGINE_DEPLOY_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_cfg_upper} "${ENGINE_DEPLOY_DIR}")
    set(CMAKE_PDB_OUTPUT_DIRECTORY_${_cfg_upper} "${ENGINE_DEPLOY_DIR}")
endforeach()

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

    # Copy Engine executable to deploy directory
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:HorizonEngine>" "${ENGINE_DEPLOY_DIR}/$<TARGET_FILE_NAME:HorizonEngine>"

    # Clean shaders output dir first so removed/renamed shaders don't stay around
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${ENGINE_DEPLOY_DIR}/shaders"

    # Copy shaders to deploy directory
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHADERS_SOURCE_DIR}" "${ENGINE_DEPLOY_DIR}/shaders"

    # Copy engine Content directory (textures, skyboxes, etc.) to deploy directory
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CONTENT_SOURCE_DIR}" "${ENGINE_DEPLOY_DIR}/Content"

    COMMAND ${CMAKE_COMMAND} -E copy_directory "${EDITOR_SOURCE_DIR}" "${ENGINE_DEPLOY_DIR}/Editor"

    # Copy static engine.pyi type stub to deploy directory for project distribution
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ENGINE_DEPLOY_DIR}/Content/Scripting"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/src/Scripting/engine.pyi" "${ENGINE_DEPLOY_DIR}/Content/Scripting/engine.pyi"

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

# Copy the runtime executable to a Tools/ subdirectory so it is not directly
# visible next to the editor but Build Game can still locate it.
if(ENGINE_BUILD_RUNTIME)
    add_custom_command(TARGET HorizonEngineRuntime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ENGINE_DEPLOY_DIR}/Tools"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:HorizonEngineRuntime>" "${ENGINE_DEPLOY_DIR}/Tools/$<TARGET_FILE_NAME:HorizonEngineRuntime>"
        COMMAND_EXPAND_LISTS
    )
endif()