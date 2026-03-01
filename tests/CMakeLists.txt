# ── Engine GTest infrastructure ─────────────────────────────────────────
# Uses Google Test bundled with assimp (external/assimp/contrib/googletest).

# Force GTest to use the shared CRT (/MD) to match the engine.
set(gtest_force_shared_crt ON CACHE BOOL "Force GTest to use shared CRT" FORCE)

# Build GTest as static, without installing it into the project.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build GTest as static" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "Disable GTest install" FORCE)
set(BUILD_GMOCK  OFF CACHE BOOL "Disable GMock"           FORCE)
add_subdirectory(
    "${CMAKE_SOURCE_DIR}/external/assimp/contrib/googletest"
    "${CMAKE_BINARY_DIR}/googletest"
    EXCLUDE_FROM_ALL
)
set(BUILD_SHARED_LIBS ON CACHE BOOL "Restore shared default" FORCE)

# Force GTest to use the same DLL runtime as the rest of the engine.
if (MSVC)
    set_property(TARGET gtest      PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    set_property(TARGET gtest_main PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# ── EngineTests executable ──────────────────────────────────────────────
add_executable(EngineTests
    RendererAbstractionTests.cpp
)

target_include_directories(EngineTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/Renderer
    ${CMAKE_SOURCE_DIR}/src/Diagnostics
    ${CMAKE_SOURCE_DIR}/src/Core
    ${CMAKE_SOURCE_DIR}/src/AssetManager
    ${CMAKE_SOURCE_DIR}/src/Logger
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(EngineTests PRIVATE
    gtest_main
    Renderer
    Diagnostics
)

set_target_properties(EngineTests PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# ── Deploy EngineTests to a separate directory next to EngineBuild ──────
set(ENGINE_TESTS_DEPLOY_DIR "C:/Users/conno/source/repos/EngineTestsBuild")

set_target_properties(EngineTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY                "${ENGINE_TESTS_DEPLOY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${ENGINE_TESTS_DEPLOY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${ENGINE_TESTS_DEPLOY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${ENGINE_TESTS_DEPLOY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${ENGINE_TESTS_DEPLOY_DIR}"
    PDB_OUTPUT_DIRECTORY                    "${ENGINE_TESTS_DEPLOY_DIR}"
    PDB_OUTPUT_DIRECTORY_DEBUG              "${ENGINE_TESTS_DEPLOY_DIR}"
    PDB_OUTPUT_DIRECTORY_RELEASE            "${ENGINE_TESTS_DEPLOY_DIR}"
    PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO     "${ENGINE_TESTS_DEPLOY_DIR}"
    PDB_OUTPUT_DIRECTORY_MINSIZEREL         "${ENGINE_TESTS_DEPLOY_DIR}"
)

add_custom_command(TARGET EngineTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ENGINE_TESTS_DEPLOY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:EngineTests> "${ENGINE_TESTS_DEPLOY_DIR}"
    COMMAND_EXPAND_LISTS
)

# Register with CTest so `ctest` discovers the tests.
include(GoogleTest)
gtest_discover_tests(EngineTests)
