add_library(Renderer SHARED
    Renderer.h
    OpenGLRenderer/OpenGLRenderer.cpp
    OpenGLRenderer/OpenGLRenderer.h
    OpenGLRenderer/glad/src/gl.c
)

set_target_properties(Renderer PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
)

target_include_directories(Renderer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/OpenGLRenderer
        ${CMAKE_CURRENT_SOURCE_DIR}/OpenGLRenderer/glad/include
)

target_link_libraries(Renderer
    PUBLIC
        SDL3::SDL3
        Logger
)

if (WIN32)
    set_target_properties(Renderer PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
