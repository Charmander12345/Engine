# ── Common source & include lists ─────────────────────────────────────────
set(SCRIPTING_SOURCES
    "Python/PythonScripting.cpp"
    "Python/PythonScripting.h"
    ScriptingInternal.h
    "Python/MathModule.cpp"
    "Python/PhysicsModule.cpp"
    "Python/EntityModule.cpp"
    "Python/AudioModule.cpp"
    "Python/InputModule.cpp"
    "Python/CameraModule.cpp"
    "Python/ParticleModule.cpp"
    "Python/DiagnosticsModule.cpp"
    "Python/LoggingModule.cpp"
    "Python/UIModule.cpp"
    "Python/GlobalStateModule.cpp"
    "Python/TimerModule.cpp"
    "Python/EditorModule.cpp"
    ScriptHotReload.cpp
    ScriptHotReload.h
    ScriptingGlobalState.h

    # ── C++ NativeScripting (merged into Scripting DLL) ───────────────────
    "${CMAKE_SOURCE_DIR}/src/NativeScripting/GameplayAPIExport.h"
    "${CMAKE_SOURCE_DIR}/src/NativeScripting/INativeScript.h"
    "${CMAKE_SOURCE_DIR}/src/NativeScripting/NativeScriptRegistry.h"
    "${CMAKE_SOURCE_DIR}/src/NativeScripting/NativeScriptRegistry.cpp"
    "${CMAKE_SOURCE_DIR}/src/NativeScripting/NativeScriptManager.h"
    "${CMAKE_SOURCE_DIR}/src/NativeScripting/NativeScriptManager.cpp"
    "${CMAKE_SOURCE_DIR}/src/NativeScripting/GameplayAPI.h"
    "${CMAKE_SOURCE_DIR}/src/NativeScripting/GameplayAPI.cpp"
    "${CMAKE_SOURCE_DIR}/src/NativeScripting/GameplayAPIInternal.h"
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
    ${CMAKE_SOURCE_DIR}/src/NativeScripting
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
target_compile_definitions(Scripting PRIVATE ENGINE_EDITOR=1 GAMEPLAY_DLL_EXPORT=1)
if (WIN32)
    set_target_properties(Scripting PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# ── Runtime target (ENGINE_EDITOR=0) ─────────────────────────────────────
add_library(ScriptingRuntime SHARED ${SCRIPTING_SOURCES})
set_target_properties(ScriptingRuntime PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_include_directories(ScriptingRuntime PUBLIC ${SCRIPTING_INCLUDE_DIRS})
target_link_libraries(ScriptingRuntime PRIVATE ${SCRIPTING_BASE_LIBS} RendererRuntime AssetManagerRuntime)
target_compile_definitions(ScriptingRuntime PRIVATE ENGINE_EDITOR=0 GAMEPLAY_DLL_EXPORT=1)
if (WIN32)
    set_target_properties(ScriptingRuntime PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
