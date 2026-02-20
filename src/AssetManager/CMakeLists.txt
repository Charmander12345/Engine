add_library(AssetManager SHARED
    AssetManager.cpp
    AssetManager.h
    GarbageCollector.cpp
    GarbageCollector.h
    AssetTypes.h
    stb_image.h
    json.hpp
)

set_target_properties(AssetManager PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(AssetManager
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/Diagnostics
        ${CMAKE_SOURCE_DIR}/src/Logger
        ${CMAKE_SOURCE_DIR}/src/Core
)

target_link_libraries(AssetManager
    PUBLIC
        Diagnostics
        Logger
        Core
        SDL3::SDL3
        assimp
)

if (WIN32)
    set_target_properties(AssetManager PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
