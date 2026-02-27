add_library(Renderer SHARED

    Renderer.h
    Shader.h
    PopupWindow.h
    PopupWindow.cpp
    SplashWindow.h
    SplashWindow.cpp

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
    OpenGLRenderer/OpenGLObject3D.cpp
    OpenGLRenderer/OpenGLObject3D.h
    OpenGLRenderer/OpenGLCamera.cpp
    OpenGLRenderer/OpenGLCamera.h
    OpenGLRenderer/OpenGLTextRenderer.cpp
    OpenGLRenderer/OpenGLTextRenderer.h
    UIManager.cpp
    UIManager.h
    UIWidget.cpp
    UIWidget.h
    UIWidgets/ButtonWidget.cpp
    UIWidgets/ButtonWidget.h
    UIWidgets/GridWidget.cpp
    UIWidgets/GridWidget.h
    UIWidgets/StackPanelWidget.cpp
    UIWidgets/StackPanelWidget.h
    UIWidgets/TextWidget.cpp
    UIWidgets/TextWidget.h
    UIWidgets/ColorPickerWidget.cpp
    UIWidgets/ColorPickerWidget.h
    UIWidgets/EntryBarWidget.h
    UIWidgets/SeparatorWidget.cpp
    UIWidgets/SeparatorWidget.h
    UIWidgets/ProgressBarWidget.cpp
    UIWidgets/ProgressBarWidget.h
    UIWidgets/SliderWidget.cpp
    UIWidgets/SliderWidget.h
    UIWidgets/CheckBoxWidget.cpp
    UIWidgets/CheckBoxWidget.h
    UIWidgets/DropDownWidget.cpp
    UIWidgets/DropDownWidget.h
    UIWidgets/TreeViewWidget.cpp
    UIWidgets/TreeViewWidget.h
    UIWidgets/TabViewWidget.cpp
    UIWidgets/TabViewWidget.h
    EditorWindows/MeshViewerWindow.cpp
    EditorWindows/MeshViewerWindow.h
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
        ${CMAKE_SOURCE_DIR}/src/Core
        ${CMAKE_SOURCE_DIR}/src/AssetManager
        ${CMAKE_SOURCE_DIR}/external/SDL3/include
        ${CMAKE_SOURCE_DIR}/src/Landscape
)

target_link_libraries(Renderer
    PUBLIC
        SDL3::SDL3
        freetype
        Logger
        Core
        AssetManager
        Landscape
        ${Python3_LIBRARY_RELEASE}
)

# Expose PhysX availability so UIManager can show the backend dropdown option.
if(ENGINE_PHYSX_AVAILABLE)
    target_compile_definitions(Renderer PRIVATE ENGINE_PHYSX_BACKEND_AVAILABLE=1)
endif()

if (WIN32)
    set_target_properties(Renderer PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
