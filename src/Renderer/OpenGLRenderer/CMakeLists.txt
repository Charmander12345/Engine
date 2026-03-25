# ── OpenGL backend source files (shared by both library variants) ─────────
set(OPENGL_RENDERER_SOURCES
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
    ParticleSystem.cpp
    ParticleSystem.h
    TextureStreamingManager.cpp
    TextureStreamingManager.h
    OpenGLSplashWindow.cpp
    OpenGLSplashWindow.h
    RendererFactory.cpp
    glad/src/gl.c
)

set(OPENGL_RENDERER_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/glad/include
)

# ── Editor Renderer (ENGINE_EDITOR=1) ────────────────────────────────────
add_library(Renderer SHARED ${OPENGL_RENDERER_SOURCES})

set_target_properties(Renderer PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
)
if(WIN32)
    set_target_properties(Renderer PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

target_include_directories(Renderer PUBLIC ${OPENGL_RENDERER_INCLUDE_DIRS})
target_link_libraries(Renderer PUBLIC RendererCore freetype OpenGL::GL)

# ── Runtime Renderer (ENGINE_EDITOR=0) ───────────────────────────────────
add_library(RendererRuntime SHARED ${OPENGL_RENDERER_SOURCES})

set_target_properties(RendererRuntime PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
)
if(WIN32)
    set_target_properties(RendererRuntime PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

target_include_directories(RendererRuntime PUBLIC ${OPENGL_RENDERER_INCLUDE_DIRS})
target_link_libraries(RendererRuntime PUBLIC RendererCoreRuntime freetype OpenGL::GL)

# On Linux, GLAD loads GL entry points via dlopen/dlsym.
if(UNIX AND NOT APPLE)
    target_link_libraries(Renderer PRIVATE ${CMAKE_DL_LIBS})
    target_link_libraries(RendererRuntime PRIVATE ${CMAKE_DL_LIBS})
endif()
