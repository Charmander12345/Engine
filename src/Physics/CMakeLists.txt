set(PHYSICS_SOURCES
    IPhysicsBackend.h
    JoltBackend.h
    JoltBackend.cpp
    PhysicsWorld.h
    PhysicsWorld.cpp
)

add_library(Physics SHARED ${PHYSICS_SOURCES})

set_target_properties(Physics PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
if(WIN32)
    set_target_properties(Physics PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

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
        Diagnostics
        Logger
        Jolt
)
