# ── Common source & include lists ─────────────────────────────────────────
set(SCRIPTING_SOURCES
    PythonScripting.cpp
    PythonScripting.h
    ScriptingInternal.h
    MathModule.cpp
    PhysicsModule.cpp
    EntityModule.cpp
    AudioModule.cpp
    InputModule.cpp
    CameraModule.cpp
    ParticleModule.cpp
    DiagnosticsModule.cpp
    LoggingModule.cpp
    UIModule.cpp
    EditorModule.cpp
    ScriptHotReload.cpp
    ScriptHotReload.h
)

set(SCRIPTING_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Python3_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src/Core
    ${CMAKE_SOURCE_DIR}/src/Renderer
    ${CMAKE_SOURCE_DIR}/src/AssetManager
    ${CMAKE_SOURCE_DIR}/src/Diagnostics
    ${CMAKE_SOURCE_DIR}/external/SDL3/include
    ${CMAKE_SOURCE_DIR}/src/Physics
)

set(SCRIPTING_BASE_LIBS
    SDL3::SDL3
    Logger
    Diagnostics
    Core
    Physics
    ${ENGINE_PYTHON_LIB}
)

# ── Editor target (ENGINE_EDITOR=1) ──────────────────────────────────────
add_library(Scripting SHARED ${SCRIPTING_SOURCES})
set_target_properties(Scripting PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_include_directories(Scripting PUBLIC ${SCRIPTING_INCLUDE_DIRS})
target_link_libraries(Scripting PRIVATE ${SCRIPTING_BASE_LIBS} Renderer AssetManager)
target_compile_definitions(Scripting PRIVATE ENGINE_EDITOR=1)
if (WIN32)
    set_target_properties(Scripting PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# ── Runtime target (ENGINE_EDITOR=0) ─────────────────────────────────────
add_library(ScriptingRuntime SHARED ${SCRIPTING_SOURCES})
set_target_properties(ScriptingRuntime PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_include_directories(ScriptingRuntime PUBLIC ${SCRIPTING_INCLUDE_DIRS})
target_link_libraries(ScriptingRuntime PRIVATE ${SCRIPTING_BASE_LIBS} RendererRuntime AssetManagerRuntime)
target_compile_definitions(ScriptingRuntime PRIVATE ENGINE_EDITOR=0)
if (WIN32)
    set_target_properties(ScriptingRuntime PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
