#include <gtest/gtest.h>

#include "NullRenderer/NullRenderer.h"
#include "Renderer/Renderer.h"
#include "Renderer/RendererCapabilities.h"
#include "Diagnostics/DiagnosticsManager.h"

// ---------------------------------------------------------------------------
// NullRenderer basics
// ---------------------------------------------------------------------------

TEST(NullRendererTest, InitializeReturnsTrue)
{
    NullRenderer renderer;
    EXPECT_TRUE(renderer.initialize());
}

TEST(NullRendererTest, NameReturnsNullRenderer)
{
    NullRenderer renderer;
    EXPECT_EQ(renderer.name(), "NullRenderer");
}

TEST(NullRendererTest, WindowReturnsNullptr)
{
    NullRenderer renderer;
    EXPECT_EQ(renderer.window(), nullptr);
}

TEST(NullRendererTest, ShutdownDoesNotCrash)
{
    NullRenderer renderer;
    renderer.initialize();
    EXPECT_NO_THROW(renderer.shutdown());
}

TEST(NullRendererTest, RenderLoopDoesNotCrash)
{
    NullRenderer renderer;
    renderer.initialize();
    EXPECT_NO_THROW(renderer.clear());
    EXPECT_NO_THROW(renderer.render());
    EXPECT_NO_THROW(renderer.present());
}

// ---------------------------------------------------------------------------
// Abstract interface defaults (non-pure methods should return safe defaults)
// ---------------------------------------------------------------------------

TEST(RendererDefaultsTest, CapabilitiesAreDefault)
{
    NullRenderer renderer;
    const auto caps = renderer.getCapabilities();
    EXPECT_FALSE(caps.supportsShadows);
    EXPECT_FALSE(caps.supportsOcclusion);
    EXPECT_FALSE(caps.supportsWireframe);
    EXPECT_FALSE(caps.supportsEntityPicking);
}

TEST(RendererDefaultsTest, TogglesDefaultToFalse)
{
    NullRenderer renderer;
    EXPECT_FALSE(renderer.isShadowsEnabled());
    EXPECT_FALSE(renderer.isVSyncEnabled());
    EXPECT_FALSE(renderer.isWireframeEnabled());
    EXPECT_FALSE(renderer.isOcclusionCullingEnabled());
}

TEST(RendererDefaultsTest, PickingReturnsZero)
{
    NullRenderer renderer;
    EXPECT_EQ(renderer.pickEntityAt(0, 0), 0u);
    EXPECT_EQ(renderer.getSelectedEntity(), 0u);
}

TEST(RendererDefaultsTest, GizmoModeDefaultsToNone)
{
    NullRenderer renderer;
    EXPECT_EQ(renderer.getGizmoMode(), Renderer::GizmoMode::None);
}

TEST(RendererDefaultsTest, MetricsDefaultToZero)
{
    NullRenderer renderer;
    EXPECT_DOUBLE_EQ(renderer.getLastGpuFrameMs(), 0.0);
    EXPECT_DOUBLE_EQ(renderer.getLastCpuRenderWorldMs(), 0.0);
    EXPECT_DOUBLE_EQ(renderer.getLastCpuRenderUiMs(), 0.0);
    EXPECT_EQ(renderer.getLastVisibleCount(), 0u);
}

// ---------------------------------------------------------------------------
// UIManager ownership – every renderer must provide a UIManager
// ---------------------------------------------------------------------------

TEST(NullRendererUITest, UIManagerIsAccessible)
{
    NullRenderer renderer;
    UIManager& ui = renderer.getUIManager();
    EXPECT_EQ(ui.getRenderer(), &renderer);
}

TEST(NullRendererUITest, ConstUIManagerIsAccessible)
{
    NullRenderer renderer;
    const Renderer& cref = renderer;
    const UIManager& ui = cref.getUIManager();
    EXPECT_EQ(ui.getRenderer(), &renderer);
}

// ---------------------------------------------------------------------------
// Camera stubs – should not crash
// ---------------------------------------------------------------------------

TEST(NullRendererCameraTest, CameraOpsDoNotCrash)
{
    NullRenderer renderer;
    EXPECT_NO_THROW(renderer.moveCamera(1.0f, 0.0f, 0.0f));
    EXPECT_NO_THROW(renderer.rotateCamera(10.0f, 5.0f));
    EXPECT_NO_THROW(renderer.setCameraPosition({ 1.0f, 2.0f, 3.0f }));
    EXPECT_NO_THROW(renderer.setCameraRotationDegrees(90.0f, 45.0f));
}

TEST(NullRendererCameraTest, CameraPositionDefaultsToOrigin)
{
    NullRenderer renderer;
    Vec3 pos = renderer.getCameraPosition();
    EXPECT_FLOAT_EQ(pos.x, 0.0f);
    EXPECT_FLOAT_EQ(pos.y, 0.0f);
    EXPECT_FLOAT_EQ(pos.z, 0.0f);
}

TEST(NullRendererCameraTest, ScreenToWorldReturnsFalse)
{
    NullRenderer renderer;
    Vec3 out{};
    EXPECT_FALSE(renderer.screenToWorldPos(100, 100, out));
}

// ---------------------------------------------------------------------------
// DiagnosticsManager RHI helpers
// ---------------------------------------------------------------------------

TEST(DiagnosticsRHITest, RHITypeToStringOpenGL)
{
    EXPECT_EQ(DiagnosticsManager::rhiTypeToString(DiagnosticsManager::RHIType::OpenGL), "OpenGL");
}

TEST(DiagnosticsRHITest, RHITypeToStringVulkan)
{
    EXPECT_EQ(DiagnosticsManager::rhiTypeToString(DiagnosticsManager::RHIType::Vulkan), "Vulkan");
}
