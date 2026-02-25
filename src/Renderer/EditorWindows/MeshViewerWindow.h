#pragma once

#include <string>
#include <memory>
#include <vector>
#include "../../Core/MathTypes.h"
#include "../../Core/ECS/ECS.h"

class OpenGLObject3D;
class RenderResourceManager;
class EngineLevel;

// Editor window that displays a single 3D static mesh.
// The normal FPS camera is used — the viewer only computes a good initial
// camera position from the mesh bounding box.
class MeshViewerWindow
{
public:
    MeshViewerWindow();
    ~MeshViewerWindow();

    // Load the mesh asset and compute initial camera position from AABB.
    bool initialize(const std::string& assetPath, RenderResourceManager& resourceMgr);

    const std::string& getAssetPath() const { return m_assetPath; }
    int  getVertexCount() const;
    int  getIndexCount()  const;
    bool isInitialized()  const { return m_initialized; }

    // Runtime level that holds the preview entity (mesh + light + ground).
    bool createRuntimeLevel(const std::string& assetPath);
    void destroyRuntimeLevel();
    std::unique_ptr<EngineLevel> takeRuntimeLevel();
    void giveRuntimeLevel(std::unique_ptr<EngineLevel> level);
    EngineLevel* getRuntimeLevel() const { return m_runtimeLevel.get(); }
    ECS::Entity  getViewerEntity() const { return m_viewerEntity; }

    // Initial camera placement (computed from mesh AABB).
    Vec3 getInitialCameraPosition() const { return m_initialCamPos; }
    Vec2 getInitialCameraRotation() const { return m_initialCamRot; }

private:
    std::string  m_assetPath;

    // Mesh (kept only for vertex/index count queries)
    std::shared_ptr<OpenGLObject3D> m_meshObject;

    // Initial camera derived from mesh AABB
    Vec3 m_initialCamPos{};
    Vec2 m_initialCamRot{};

    bool m_initialized{ false };

    // Runtime level for renderWorld integration
    std::unique_ptr<EngineLevel> m_runtimeLevel;
    ECS::Entity m_viewerEntity{ 0 };
};
