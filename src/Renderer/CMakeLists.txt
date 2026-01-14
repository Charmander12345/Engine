add_library(Renderer SHARED

    Renderer.h
    Shader.h
    Material.h
    OpenGLRenderer/OpenGLRenderer.cpp
    OpenGLRenderer/OpenGLRenderer.h
    OpenGLRenderer/OpenGLShader.cpp
    OpenGLRenderer/OpenGLShader.h
    OpenGLRenderer/OpenGLMaterial.cpp
    OpenGLRenderer/OpenGLMaterial.h
    OpenGLRenderer/OpenGLTexture.cpp
    OpenGLRenderer/OpenGLTexture.h
    OpenGLRenderer/glad/src/gl.c
    RenderResourceManager.cpp
    OpenGLRenderer/OpenGLObject2D.cpp
    OpenGLRenderer/OpenGLObject2D.h
)
set_target_properties(Renderer PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
)

target_include_directories(Renderer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/OpenGLRenderer
        ${CMAKE_CURRENT_SOURCE_DIR}/OpenGLRenderer/glad/include
        ${CMAKE_SOURCE_DIR}/src/Basics
        ${CMAKE_SOURCE_DIR}/src/AssetManager
)

target_link_libraries(Renderer
    PUBLIC
        SDL3::SDL3
        Logger
        Basics
        AssetManager
)

if (WIN32)
    set_target_properties(Renderer PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
