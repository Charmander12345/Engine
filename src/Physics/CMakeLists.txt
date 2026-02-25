add_library(Physics STATIC
    PhysicsWorld.h
    PhysicsWorld.cpp
)

set_target_properties(Physics PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(Physics
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/Core
        ${CMAKE_SOURCE_DIR}/src/Logger
        ${CMAKE_SOURCE_DIR}/src/AssetManager
        ${CMAKE_SOURCE_DIR}/src/Renderer
        ${CMAKE_SOURCE_DIR}/external/SDL3/include
)

target_link_libraries(Physics
    PUBLIC
        Core
        Logger
)
