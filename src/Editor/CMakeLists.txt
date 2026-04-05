# ── Editor Module ─────────────────────────────────────────────────────────
# Editor-only OBJECT library.  Compiled only when ENGINE_EDITOR=1.
# The editor depends on IEditorBridge (abstract interface in Core) and
# on Renderer/UIManager types — but never on engine-internal singletons
# (ECS, AssetManager, PhysicsWorld, etc.) directly.

set(EDITOR_SOURCES
    EditorApp.cpp
    EditorApp.h
    ProjectSelector.cpp
    ProjectSelector.h
    ${CMAKE_SOURCE_DIR}/src/Core/EditorBridgeImpl.cpp
    ${CMAKE_SOURCE_DIR}/src/Core/EditorBridgeImpl.h
)

# Editor variant (OBJECT lib linked into HorizonEngine)
add_library(Editor OBJECT ${EDITOR_SOURCES})

set_target_properties(Editor PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

target_compile_definitions(Editor PRIVATE ENGINE_EDITOR=1)
target_compile_definitions(Editor PRIVATE
    ENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_include_directories(Editor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/Core
    ${CMAKE_SOURCE_DIR}/src/Renderer
    ${CMAKE_SOURCE_DIR}/src/AssetManager
    ${CMAKE_SOURCE_DIR}/src/Diagnostics
    ${CMAKE_SOURCE_DIR}/src/Logger
    ${CMAKE_SOURCE_DIR}/src/Physics
    ${CMAKE_SOURCE_DIR}/src/Scripting
    ${CMAKE_SOURCE_DIR}/external/SDL3/include
    ${CMAKE_SOURCE_DIR}/external
    ${Python3_INCLUDE_DIRS}
)

target_link_libraries(Editor PUBLIC
    SDL3::SDL3
    Logger
    Core
)

target_compile_definitions(Editor PUBLIC ENGINE_EDITOR=1)
