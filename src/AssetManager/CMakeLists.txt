add_library(AssetManager SHARED
    AssetManager.cpp
    AssetManager.h
)

set_target_properties(AssetManager PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(AssetManager
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if (WIN32)
    set_target_properties(AssetManager PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
