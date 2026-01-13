add_library(Basics SHARED
    EngineObject.cpp
    EngineObject.h
    "EngineLevel.cpp"
    "EngineLevel.h"
    Object3D.cpp
    Object3D.h
    Object2D.cpp
    Object2D.h
    "Texture.cpp"
    "Texture.h"
    "MaterialAsset.cpp"
    "MaterialAsset.h"
    "MathTypes.h"
)

set_target_properties(Basics PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(Basics
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/AssetManager
        ${CMAKE_SOURCE_DIR}/src/Renderer/OpenGLRenderer
        ${CMAKE_SOURCE_DIR}/src/Renderer/OpenGLRenderer/glad/include
        ${CMAKE_SOURCE_DIR}/src/Renderer
)

if (WIN32)
    set_target_properties(Basics PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
