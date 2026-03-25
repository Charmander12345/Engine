# ── Common source & include lists ─────────────────────────────────────────
set(LANDSCAPE_SOURCES
    LandscapeManager.h
    LandscapeManager.cpp
)

set(LANDSCAPE_INCLUDE_DIRS_PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set(LANDSCAPE_INCLUDE_DIRS_PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/AssetManager
    ${CMAKE_SOURCE_DIR}/src/Core
    ${CMAKE_SOURCE_DIR}/src/Diagnostics
    ${CMAKE_SOURCE_DIR}/src/Logger
)

set(LANDSCAPE_BASE_LIBS
    Core
    Diagnostics
    Logger
)

# ── Editor target (ENGINE_EDITOR=1) ──────────────────────────────────────
add_library(Landscape STATIC ${LANDSCAPE_SOURCES})
set_target_properties(Landscape PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_include_directories(Landscape PUBLIC ${LANDSCAPE_INCLUDE_DIRS_PUBLIC} PRIVATE ${LANDSCAPE_INCLUDE_DIRS_PRIVATE})
target_link_libraries(Landscape PUBLIC AssetManager ${LANDSCAPE_BASE_LIBS})
target_compile_definitions(Landscape PUBLIC ENGINE_EDITOR=1)

# ── Runtime target (ENGINE_EDITOR=0) ─────────────────────────────────────
add_library(LandscapeRuntime STATIC ${LANDSCAPE_SOURCES})
set_target_properties(LandscapeRuntime PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_include_directories(LandscapeRuntime PUBLIC ${LANDSCAPE_INCLUDE_DIRS_PUBLIC} PRIVATE ${LANDSCAPE_INCLUDE_DIRS_PRIVATE})
target_link_libraries(LandscapeRuntime PUBLIC AssetManagerRuntime ${LANDSCAPE_BASE_LIBS})
target_compile_definitions(LandscapeRuntime PUBLIC ENGINE_EDITOR=0)
