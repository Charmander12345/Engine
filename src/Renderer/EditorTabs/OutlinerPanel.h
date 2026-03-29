#pragma once
#if ENGINE_EDITOR

#include <string>
#include <memory>
#include <optional>
#include <cstdint>

#include "../../Core/ECS/ECS.h"

class UIManager;
class Renderer;
class EditorWidget;
class EngineLevel;
enum class AssetType;

/// Manages the World Outliner tree-view and Entity Details property panel.
/// Extracted from UIManager to reduce file size and isolate outliner logic.
class OutlinerPanel
{
public:
    OutlinerPanel(UIManager* uiManager, Renderer* renderer);

    void setRenderer(Renderer* renderer) { m_renderer = renderer; }

    // --- Core methods (extracted from UIManager) ---
    void populateWidget(const std::shared_ptr<EditorWidget>& widget);
    void populateDetails(unsigned int entity);
    void refresh();
    void navigateByArrow(int direction);
    void selectEntity(unsigned int entity);

    // --- Entity clipboard ---
    void copySelectedEntity();
    bool pasteEntity();
    bool duplicateEntity();
    bool hasClipboard() const { return m_entityClipboard.valid; }

    // --- Asset-to-entity application ---
    void applyAssetToEntity(AssetType type, const std::string& assetPath, unsigned int entity);

    // --- Accessors ---
    unsigned int getSelectedEntity() const { return m_selectedEntity; }
    void         setSelectedEntity(unsigned int e) { m_selectedEntity = e; }

    EngineLevel* getLevel() const { return m_level; }
    void         setLevel(EngineLevel* level);

    uint64_t getLastEcsComponentVersion() const { return m_lastEcsComponentVersion; }
    void     setLastEcsComponentVersion(uint64_t v) { m_lastEcsComponentVersion = v; }
    uint64_t getLastRegistryVersion() const { return m_lastRegistryVersion; }
    void     setLastRegistryVersion(uint64_t v) { m_lastRegistryVersion = v; }

private:
    UIManager* m_uiManager{ nullptr };
    Renderer*  m_renderer{ nullptr };

    EngineLevel*  m_level{ nullptr };
    unsigned int  m_selectedEntity{ 0 };

    struct EntityClipboard
    {
        bool valid{ false };
        std::optional<ECS::TransformComponent>       transform;
        std::optional<ECS::MeshComponent>            mesh;
        std::optional<ECS::MaterialComponent>        material;
        std::optional<ECS::LightComponent>           light;
        std::optional<ECS::CameraComponent>          camera;
        std::optional<ECS::PhysicsComponent>         physics;
        std::optional<ECS::ScriptComponent>          script;
        std::optional<ECS::NameComponent>            name;
        std::optional<ECS::CollisionComponent>       collision;
        std::optional<ECS::HeightFieldComponent>     heightField;
        std::optional<ECS::LodComponent>             lod;
        std::optional<ECS::AnimationComponent>       animation;
        std::optional<ECS::ParticleEmitterComponent> particleEmitter;
    };
    EntityClipboard m_entityClipboard;

    uint64_t m_lastEcsComponentVersion{ 0 };
    uint64_t m_lastRegistryVersion{ 0 };
};

#endif // ENGINE_EDITOR
