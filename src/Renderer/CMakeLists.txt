add_library(RendererCore OBJECT

    Renderer.h
    Shader.h
    RendererCapabilities.h
    IRenderObject2D.h
    IRenderObject3D.h
    ITextRenderer.h
    IShaderProgram.h
    ITexture.h
    IRenderContext.h
    IRenderTarget.h
    RendererFactory.h
    PopupWindow.h
    PopupWindow.cpp
    SplashWindow.h
    RenderResourceManager.h
    RenderResourceManager.cpp

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
set_target_properties(RendererCore PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(RendererCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/Core
        ${CMAKE_SOURCE_DIR}/src/AssetManager
        ${CMAKE_SOURCE_DIR}/external/SDL3/include
        ${CMAKE_SOURCE_DIR}/external
        ${CMAKE_SOURCE_DIR}/src/Landscape
        ${CMAKE_CURRENT_SOURCE_DIR}/OpenGLRenderer/glad/include
)

target_link_libraries(RendererCore
    PUBLIC
        SDL3::SDL3
        Logger
        Core
        AssetManager
        Landscape
        ${Python3_LIBRARY_RELEASE}
)

add_subdirectory(OpenGLRenderer)

# Expose PhysX availability so UIManager can show the backend dropdown option.
if(ENGINE_PHYSX_AVAILABLE)
    target_compile_definitions(RendererCore PRIVATE ENGINE_PHYSX_BACKEND_AVAILABLE=1)
    target_compile_definitions(Renderer PRIVATE ENGINE_PHYSX_BACKEND_AVAILABLE=1)
endif()
