add_library(Core SHARED
    EngineObject.cpp
    EngineObject.h
    "EngineLevel.cpp"
    "EngineLevel.h"
    "AudioManager.cpp"
    "AudioManager.h"
    
    
    
    
    "../Renderer/Texture.cpp"
    "../Renderer/Texture.h"
    "../Renderer/Material.h"
    "MathTypes.h"
    "ECS/DataStructs/SparseSet.h"
    "ECS/ECS.h"
    "ECS/Components.h"
    "ECS/ECS.cpp" "Asset.cpp"
    "Asset.h" "Core.h")

set_target_properties(Core PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(Core
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/AssetManager
        ${CMAKE_SOURCE_DIR}/src/Renderer
        ${CMAKE_SOURCE_DIR}/external
        ${CMAKE_SOURCE_DIR}/external/SDL3/include
        ${CMAKE_SOURCE_DIR}/ECS
)

target_link_libraries(Core PRIVATE Logger OpenAL::OpenAL SDL3::SDL3)


if (WIN32)
    set_target_properties(Core PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
