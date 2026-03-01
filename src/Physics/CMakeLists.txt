set(PHYSICS_SOURCES
    IPhysicsBackend.h
    JoltBackend.h
    JoltBackend.cpp
    PhysicsWorld.h
    PhysicsWorld.cpp
)

# Conditionally add PhysX backend sources.
if(ENGINE_PHYSX_AVAILABLE)
    list(APPEND PHYSICS_SOURCES
        PhysXBackend.h
        PhysXBackend.cpp
    )
endif()

add_library(Physics SHARED ${PHYSICS_SOURCES})

set_target_properties(Physics PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
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
        Jolt
)

# Link PhysX when available and expose the preprocessor define.
if(ENGINE_PHYSX_AVAILABLE)
    target_compile_definitions(Physics PUBLIC ENGINE_PHYSX_BACKEND_AVAILABLE=1)
    target_include_directories(Physics PRIVATE
        ${CMAKE_SOURCE_DIR}/external/PhysX/physx/include
    )
    target_link_libraries(Physics
        PRIVATE
            PhysX
            PhysXFoundation
            PhysXCommon
            PhysXExtensions
            PhysXCooking
            PhysXPvdSDK
    )
endif()
