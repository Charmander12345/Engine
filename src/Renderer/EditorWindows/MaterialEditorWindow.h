#pragma once

#include <string>
#include <memory>
#include "../../Core/MathTypes.h"
#include "../../Core/ECS/ECS.h"

class RenderResourceManager;
class EngineLevel;

/// Editor window that displays a material on a preview mesh (cube).
/// Follows the same pattern as MeshViewerWindow: owns a runtime level
/// that is swapped into the active scene via the tab system.
class MaterialEditorWindow
{
public:
    MaterialEditorWindow();
    ~MaterialEditorWindow();

    /// Load the material asset and prepare the preview.
    bool initialize(const std::string& materialAssetPath);

    const std::string& getAssetPath() const { return m_assetPath; }
    bool isInitialized() const { return m_initialized; }

    // Runtime level (preview cube + light + ground)
    bool createRuntimeLevel();
    void destroyRuntimeLevel();
    std::unique_ptr<EngineLevel> takeRuntimeLevel();
    void giveRuntimeLevel(std::unique_ptr<EngineLevel> level);
    EngineLevel* getRuntimeLevel() const { return m_runtimeLevel.get(); }
    ECS::Entity  getPreviewEntity() const { return m_previewEntity; }

    // Camera
    Vec3 getInitialCameraPosition() const { return m_initialCamPos; }
    Vec2 getInitialCameraRotation() const { return m_initialCamRot; }

private:
    std::string m_assetPath;

    Vec3 m_initialCamPos{};
    Vec2 m_initialCamRot{};

    bool m_initialized{ false };

    std::unique_ptr<EngineLevel> m_runtimeLevel;
    ECS::Entity m_previewEntity{ 0 };
};
