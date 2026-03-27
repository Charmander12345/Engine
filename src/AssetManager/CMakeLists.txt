# ── Common source & include lists ─────────────────────────────────────────
set(ASSET_MANAGER_SOURCES
    AssetManager.cpp
    AssetManager.h
    GarbageCollector.cpp
    GarbageCollector.h
    AssetTypes.h
    AssetCooker.h
    AssetCooker.cpp
    HPKArchive.h
    HPKArchive.cpp
    stb_image.h
    json.hpp
)

set(ASSET_MANAGER_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/Diagnostics
    ${CMAKE_SOURCE_DIR}/src/Logger
    ${CMAKE_SOURCE_DIR}/src/Core
)

set(ASSET_MANAGER_BASE_LIBS
    Diagnostics
    Logger
    Core
    SDL3::SDL3
)

# ── Editor target (ENGINE_EDITOR=1) ──────────────────────────────────────
add_library(AssetManager SHARED ${ASSET_MANAGER_SOURCES})
set_target_properties(AssetManager PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_include_directories(AssetManager PUBLIC ${ASSET_MANAGER_INCLUDE_DIRS})
target_link_libraries(AssetManager PUBLIC ${ASSET_MANAGER_BASE_LIBS} assimp)
target_compile_definitions(AssetManager PUBLIC ENGINE_EDITOR=1)
if (WIN32)
    set_target_properties(AssetManager PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# ── Runtime target (ENGINE_EDITOR=0, no assimp) ─────────────────────────
add_library(AssetManagerRuntime SHARED ${ASSET_MANAGER_SOURCES})
set_target_properties(AssetManagerRuntime PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_include_directories(AssetManagerRuntime PUBLIC ${ASSET_MANAGER_INCLUDE_DIRS})
target_link_libraries(AssetManagerRuntime PUBLIC ${ASSET_MANAGER_BASE_LIBS})
target_compile_definitions(AssetManagerRuntime PUBLIC ENGINE_EDITOR=0)
if (WIN32)
    set_target_properties(AssetManagerRuntime PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
