add_library(Landscape STATIC
    LandscapeManager.h
    LandscapeManager.cpp
)

set_target_properties(Landscape PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(Landscape
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/AssetManager
        ${CMAKE_SOURCE_DIR}/src/Core
        ${CMAKE_SOURCE_DIR}/src/Diagnostics
        ${CMAKE_SOURCE_DIR}/src/Logger
)

target_link_libraries(Landscape
    PUBLIC
        AssetManager
        Core
        Diagnostics
        Logger
)
