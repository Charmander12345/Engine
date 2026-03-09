add_library(Renderer SHARED
    OpenGLRenderer.cpp
    OpenGLRenderer.h
    OpenGLCamera.cpp
    OpenGLCamera.h
    OpenGLShader.cpp
    OpenGLShader.h
    OpenGLMaterial.cpp
    OpenGLMaterial.h
    OpenGLTexture.cpp
    OpenGLTexture.h
    OpenGLObject2D.cpp
    OpenGLObject2D.h
    OpenGLObject3D.cpp
    OpenGLObject3D.h
    OpenGLTextRenderer.cpp
    OpenGLTextRenderer.h
    OpenGLRenderContext.h
    OpenGLRenderTarget.cpp
    OpenGLRenderTarget.h
    PostProcessStack.cpp
    PostProcessStack.h
    ShaderHotReload.cpp
    ShaderHotReload.h
    OpenGLSplashWindow.cpp
    OpenGLSplashWindow.h
    RendererFactory.cpp
    glad/src/gl.c
)

set_target_properties(Renderer PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

target_include_directories(Renderer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/glad/include
)

target_link_libraries(Renderer
    PUBLIC
        RendererCore
        freetype
)
