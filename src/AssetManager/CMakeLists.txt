add_library(AssetManager SHARED
    AssetManager.cpp
    AssetManager.h
    GarbageCollector.cpp
    GarbageCollector.h
    AssetTypes.h
    stb_image.h
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
        ${CMAKE_SOURCE_DIR}/src/Basics
)

target_link_libraries(AssetManager
    PUBLIC
        Diagnostics
        Logger
        Basics
)

if (WIN32)
    set_target_properties(AssetManager PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
