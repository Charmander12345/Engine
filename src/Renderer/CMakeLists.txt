# ── Source file lists ─────────────────────────────────────────────────────
# Common sources are compiled for BOTH editor and runtime builds.
# Editor-only sources are excluded from the runtime library.
set(RENDERER_CORE_COMMON_SOURCES
    Texture.h
    Material.h
    Camera.h
    CameraPath.h
    DDSLoader.h
    DDSLoader.cpp

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
    SplashWindow.h
    RenderResourceManager.h
    RenderResourceManager.cpp

    UIManager.cpp
    UIManager.h
    ViewportUIManager.cpp
    ViewportUIManager.h
    UIWidget.cpp
    UIWidget.h
    ViewportUITheme.h
    EditorUI/EditorWidget.h
    GameplayUI/GameplayWidget.h
    UIWidgets/ButtonWidget.h
    UIWidgets/GridWidget.h
    UIWidgets/StackPanelWidget.h
    UIWidgets/TextWidget.h
    UIWidgets/ColorPickerWidget.cpp
    UIWidgets/ColorPickerWidget.h
    UIWidgets/EntryBarWidget.h
    UIWidgets/SeparatorWidget.h
    UIWidgets/ProgressBarWidget.h
    UIWidgets/SliderWidget.h
    UIWidgets/CheckBoxWidget.h
    UIWidgets/DropDownWidget.h
    UIWidgets/TreeViewWidget.h
    UIWidgets/TabViewWidget.h
    EditorTheme.h
)

# Editor-only sources – not compiled into the runtime build.
set(RENDERER_CORE_EDITOR_SOURCES
    EditorUIBuilder.h
    EditorUIBuilder.cpp
    WidgetDetailSchema.h
    WidgetDetailSchema.cpp
    EditorWindows/MeshViewerWindow.cpp
    EditorWindows/MeshViewerWindow.h
    EditorWindows/MaterialEditorWindow.cpp
    EditorWindows/MaterialEditorWindow.h
    EditorWindows/TextureViewerWindow.cpp
    EditorWindows/TextureViewerWindow.h
    EditorWindows/PopupWindow.cpp
    EditorWindows/PopupWindow.h
    EditorTabs/IEditorTab.h
    EditorTabs/ConsoleTab.h
    EditorTabs/ConsoleTab.cpp
    EditorTabs/ProfilerTab.h
    EditorTabs/ProfilerTab.cpp
    EditorTabs/AudioPreviewTab.h
    EditorTabs/AudioPreviewTab.cpp
    EditorTabs/ParticleEditorTab.h
    EditorTabs/ParticleEditorTab.cpp
    EditorTabs/ShaderViewerTab.h
    EditorTabs/ShaderViewerTab.cpp
    EditorTabs/RenderDebuggerTab.h
    EditorTabs/RenderDebuggerTab.cpp
    EditorTabs/SequencerTab.h
    EditorTabs/SequencerTab.cpp
    EditorTabs/LevelCompositionTab.h
    EditorTabs/LevelCompositionTab.cpp
    EditorTabs/AnimationEditorTab.h
    EditorTabs/AnimationEditorTab.cpp
    EditorTabs/UIDesignerTab.h
    EditorTabs/UIDesignerTab.cpp
    EditorTabs/WidgetEditorTab.h
    EditorTabs/WidgetEditorTab.cpp
    EditorTabs/ContentBrowserPanel.h
    EditorTabs/ContentBrowserPanel.cpp
    EditorTabs/OutlinerPanel.h
    EditorTabs/OutlinerPanel.cpp
    EditorTabs/BuildSystemUI.h
    EditorTabs/BuildSystemUI.cpp
    EditorTabs/EditorDialogs.h
    EditorTabs/EditorDialogs.cpp
)

# Shared properties for both RendererCore variants
set(RENDERER_CORE_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/Core
    ${CMAKE_SOURCE_DIR}/src/AssetManager
    ${CMAKE_SOURCE_DIR}/external/SDL3/include
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_SOURCE_DIR}/src/Landscape
    ${CMAKE_CURRENT_SOURCE_DIR}/OpenGLRenderer/glad/include
)

set(RENDERER_CORE_LINK_LIBS_BASE
    SDL3::SDL3
    Logger
    Core
    ${ENGINE_PYTHON_LIB}
)

# Editor variant links editor AssetManager + Landscape
set(RENDERER_CORE_LINK_LIBS
    ${RENDERER_CORE_LINK_LIBS_BASE}
    AssetManager
    Landscape
)

# Runtime variant links runtime AssetManager + Landscape (no assimp)
set(RENDERER_CORE_LINK_LIBS_RUNTIME
    ${RENDERER_CORE_LINK_LIBS_BASE}
    AssetManagerRuntime
    LandscapeRuntime
)

# ── Editor RendererCore (ENGINE_EDITOR=1) ────────────────────────────────
add_library(RendererCore OBJECT
    ${RENDERER_CORE_COMMON_SOURCES}
    ${RENDERER_CORE_EDITOR_SOURCES}
)
set_target_properties(RendererCore PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(RendererCore PUBLIC ${RENDERER_CORE_INCLUDE_DIRS})
target_link_libraries(RendererCore PUBLIC ${RENDERER_CORE_LINK_LIBS})
target_compile_definitions(RendererCore PUBLIC ENGINE_EDITOR=1)

# ── Runtime RendererCore (ENGINE_EDITOR=0, no editor sources) ────────────
add_library(RendererCoreRuntime OBJECT
    ${RENDERER_CORE_COMMON_SOURCES}
)
set_target_properties(RendererCoreRuntime PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(RendererCoreRuntime PUBLIC ${RENDERER_CORE_INCLUDE_DIRS})
target_link_libraries(RendererCoreRuntime PUBLIC ${RENDERER_CORE_LINK_LIBS_RUNTIME})
target_compile_definitions(RendererCoreRuntime PUBLIC ENGINE_EDITOR=0)

add_subdirectory(OpenGLRenderer)

# Expose PhysX availability so UIManager can show the backend dropdown option.
if(ENGINE_PHYSX_AVAILABLE)
    target_compile_definitions(RendererCore PRIVATE ENGINE_PHYSX_BACKEND_AVAILABLE=1)
    target_compile_definitions(Renderer PRIVATE ENGINE_PHYSX_BACKEND_AVAILABLE=1)
    target_compile_definitions(RendererCoreRuntime PRIVATE ENGINE_PHYSX_BACKEND_AVAILABLE=1)
    target_compile_definitions(RendererRuntime PRIVATE ENGINE_PHYSX_BACKEND_AVAILABLE=1)
endif()
