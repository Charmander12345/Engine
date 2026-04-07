#if ENGINE_EDITOR

#include "EditorBridgeImpl.h"

#include "ECS/ECS.h"
#include "EngineLevel.h"
#include "AudioManager.h"
#include "UndoRedoManager.h"
#include "../Logger/Logger.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/UIManager.h"
#include "../Renderer/ViewportUIManager.h"
#include "../AssetManager/AssetManager.h"
#include "../AssetManager/AssetTypes.h"
#include "../Diagnostics/DiagnosticsManager.h"
#include "../Physics/PhysicsWorld.h"
#include "../Scripting/Python/PythonScripting.h"

// ═════════════════════════════════════════════════════════════════════════
//  Construction
// ═════════════════════════════════════════════════════════════════════════

EditorBridgeImpl::EditorBridgeImpl(Renderer* renderer)
    : m_renderer(renderer)
{
}

// ═════════════════════════════════════════════════════════════════════════
//  Renderer / Window
// ═════════════════════════════════════════════════════════════════════════

Renderer* EditorBridgeImpl::getRenderer() { return m_renderer; }

UIManager& EditorBridgeImpl::getUIManager() { return m_renderer->getUIManager(); }

ViewportUIManager* EditorBridgeImpl::getViewportUIManager()
{
    return m_renderer->getViewportUIManagerPtr();
}

SDL_Window* EditorBridgeImpl::getWindow()
{
    return m_renderer ? m_renderer->window() : nullptr;
}

unsigned int EditorBridgeImpl::preloadUITexture(const std::string& path)
{
    return m_renderer ? m_renderer->preloadUITexture(path) : 0;
}

std::shared_ptr<Widget> EditorBridgeImpl::createWidgetFromAsset(const std::shared_ptr<AssetData>& asset)
{
    return m_renderer ? m_renderer->createWidgetFromAsset(asset) : nullptr;
}

// ═════════════════════════════════════════════════════════════════════════
//  Camera
// ═════════════════════════════════════════════════════════════════════════

Vec3 EditorBridgeImpl::getCameraPosition() const
{
    return m_renderer ? m_renderer->getCameraPosition() : Vec3{};
}

Vec2 EditorBridgeImpl::getCameraRotation() const
{
    return m_renderer ? m_renderer->getCameraRotationDegrees() : Vec2{};
}

void EditorBridgeImpl::setCameraPosition(const Vec3& pos)
{
    if (m_renderer) m_renderer->setCameraPosition(pos);
}

void EditorBridgeImpl::setCameraRotation(float yawDeg, float pitchDeg)
{
    if (m_renderer) m_renderer->setCameraRotationDegrees(yawDeg, pitchDeg);
}

void EditorBridgeImpl::moveCamera(float forward, float right, float up)
{
    if (m_renderer) m_renderer->moveCamera(forward, right, up);
}

void EditorBridgeImpl::rotateCamera(float yawDelta, float pitchDelta)
{
    if (m_renderer) m_renderer->rotateCamera(yawDelta, pitchDelta);
}

// ═════════════════════════════════════════════════════════════════════════
//  Entity Operations
// ═════════════════════════════════════════════════════════════════════════

unsigned int EditorBridgeImpl::createEntity(const std::string& name)
{
    auto& ecs = ECS::ECSManager::Instance();
    const ECS::Entity entity = ecs.createEntity();
    ECS::NameComponent nc;
    nc.displayName = name;
    ecs.addComponent<ECS::NameComponent>(entity, nc);

    auto& diag = DiagnosticsManager::Instance();
    if (auto* level = diag.getActiveLevelSoft())
        level->onEntityAdded(entity);

    return static_cast<unsigned int>(entity);
}

void EditorBridgeImpl::removeEntity(unsigned int entity)
{
    auto& ecs = ECS::ECSManager::Instance();
    auto& diag = DiagnosticsManager::Instance();
    const auto e = static_cast<ECS::Entity>(entity);
    if (auto* level = diag.getActiveLevelSoft())
        level->onEntityRemoved(e);
    ecs.removeEntity(e);
}

void EditorBridgeImpl::selectEntity(unsigned int entity)
{
    if (m_renderer)
    {
        m_renderer->getUIManager().selectEntity(entity);
        m_renderer->setSelectedEntity(entity);
    }
}

unsigned int EditorBridgeImpl::getSelectedEntity() const
{
    return m_renderer ? m_renderer->getSelectedEntity() : 0;
}

void EditorBridgeImpl::invalidateEntity(unsigned int entity)
{
    DiagnosticsManager::Instance().invalidateEntity(entity);
}

// ═════════════════════════════════════════════════════════════════════════
//  Asset Management
// ═════════════════════════════════════════════════════════════════════════

int EditorBridgeImpl::loadAsset(const std::string& path, int type)
{
    return AssetManager::Instance().loadAsset(path, static_cast<AssetType>(type), AssetManager::Sync);
}

std::shared_ptr<AssetData> EditorBridgeImpl::getLoadedAssetByID(unsigned int id)
{
    return AssetManager::Instance().getLoadedAssetByID(id);
}

bool EditorBridgeImpl::saveAssets()
{
    // The UIManager handles the save dialog flow
    return true;
}

bool EditorBridgeImpl::importAsset(SDL_Window* window)
{
    AssetManager::Instance().OpenImportDialog(window, AssetType::Unknown, AssetManager::Async);
    return true;
}

bool EditorBridgeImpl::importAssetFromPath(const std::string& filePath)
{
    auto& diag = DiagnosticsManager::Instance();
    auto action = diag.registerAction(DiagnosticsManager::ActionType::ImportingAsset);
    AssetManager::Instance().importAssetFromPath(filePath, AssetType::Unknown, action.ID);
    return true;
}

bool EditorBridgeImpl::deleteAsset(const std::string& path)
{
    return AssetManager::Instance().deleteAsset(path, true);
}

bool EditorBridgeImpl::moveAsset(const std::string& from, const std::string& to)
{
    return AssetManager::Instance().moveAsset(from, to);
}

std::string EditorBridgeImpl::getAbsoluteContentPath(const std::string& rel) const
{
    return AssetManager::Instance().getAbsoluteContentPath(rel);
}

std::string EditorBridgeImpl::getProjectPath() const
{
    return DiagnosticsManager::Instance().getProjectInfo().projectPath;
}

std::string EditorBridgeImpl::getEditorWidgetPath(const std::string& name) const
{
    return AssetManager::Instance().getEditorWidgetPath(name);
}

size_t EditorBridgeImpl::getUnsavedAssetCount() const
{
    return AssetManager::Instance().getUnsavedAssetCount();
}

std::vector<IEditorBridge::AssetReference> EditorBridgeImpl::findReferencesTo(const std::string& assetPath) const
{
    const auto refs = AssetManager::Instance().findReferencesTo(assetPath);
    std::vector<AssetReference> result;
    result.reserve(refs.size());
    for (const auto& r : refs)
        result.push_back({ r.sourcePath, r.sourceType });
    return result;
}

std::vector<std::string> EditorBridgeImpl::getAssetDependencies(const std::string& assetPath) const
{
    return AssetManager::Instance().getAssetDependencies(assetPath);
}

void EditorBridgeImpl::setOnImportCompleted(std::function<void()> callback)
{
    AssetManager::Instance().setOnImportCompleted(std::move(callback));
}

// ═════════════════════════════════════════════════════════════════════════
//  Level Management
// ═════════════════════════════════════════════════════════════════════════

bool EditorBridgeImpl::loadLevel(const std::string& relPath, std::string& outError)
{
    const std::string absPath = AssetManager::Instance().getAbsoluteContentPath(relPath);
    if (absPath.empty())
    {
        outError = "Could not resolve path: " + relPath;
        return false;
    }
    auto result = AssetManager::Instance().loadLevelAsset(absPath);
    if (!result.success)
    {
        outError = result.errorMessage;
        return false;
    }
    return true;
}

bool EditorBridgeImpl::saveActiveLevel()
{
    return AssetManager::Instance().saveActiveLevel();
}

std::string EditorBridgeImpl::getActiveLevelName() const
{
    auto* level = DiagnosticsManager::Instance().getActiveLevelSoft();
    return level ? level->getName() : "";
}

void EditorBridgeImpl::captureEditorCameraToLevel()
{
    if (!m_renderer) return;
    auto* level = DiagnosticsManager::Instance().getActiveLevelSoft();
    if (!level) return;
    level->setEditorCameraPosition(m_renderer->getCameraPosition());
    level->setEditorCameraRotation(m_renderer->getCameraRotationDegrees());
    level->setHasEditorCamera(true);
}

void EditorBridgeImpl::restoreEditorCameraFromLevel()
{
    if (!m_renderer) return;
    auto* level = DiagnosticsManager::Instance().getActiveLevelSoft();
    if (!level || !level->hasEditorCamera()) return;
    m_renderer->setCameraPosition(level->getEditorCameraPosition());
    const auto& rot = level->getEditorCameraRotation();
    m_renderer->setCameraRotationDegrees(rot.x, rot.y);
}

std::string EditorBridgeImpl::getLevelSkyboxPath() const
{
    auto* level = DiagnosticsManager::Instance().getActiveLevelSoft();
    return level ? level->getSkyboxPath() : "";
}

void EditorBridgeImpl::setLevelSkyboxPath(const std::string& path)
{
    auto* level = DiagnosticsManager::Instance().getActiveLevelSoft();
    if (level) level->setSkyboxPath(path);
}

void EditorBridgeImpl::setScenePrepared(bool prepared)
{
    DiagnosticsManager::Instance().setScenePrepared(prepared);
}

// ═════════════════════════════════════════════════════════════════════════
//  PIE
// ═════════════════════════════════════════════════════════════════════════

bool EditorBridgeImpl::isPIEActive() const
{
    return DiagnosticsManager::Instance().isPIEActive();
}

void EditorBridgeImpl::setPIEActive(bool active)
{
    DiagnosticsManager::Instance().setPIEActive(active);
}

void EditorBridgeImpl::initializePhysicsForPIE()
{
    auto& diag = DiagnosticsManager::Instance();
    if (auto v = diag.getState("PhysicsBackend"))
    {
        if (*v != "Jolt")
        {
            Logger::Instance().log(Logger::Category::Engine,
                "PIE physics backend request is not Jolt. Falling back to Jolt.",
                Logger::LogLevel::WARNING);
            diag.setState("PhysicsBackend", "Jolt");
            diag.saveConfig();
        }
    }
    else
    {
        diag.setState("PhysicsBackend", "Jolt");
        diag.saveConfig();
    }

    PhysicsWorld::Instance().initialize(PhysicsWorld::Backend::Jolt);

    auto toFloat = [&](const std::string& key, float fallback) -> float {
        if (auto v = diag.getState(key)) {
            try { return std::stof(*v); } catch (...) {}
        }
        return fallback;
    };
    float gx = toFloat("PhysicsGravityX", 0.0f);
    float gy = toFloat("PhysicsGravityY", -9.81f);
    float gz = toFloat("PhysicsGravityZ", 0.0f);
    PhysicsWorld::Instance().setGravity(gx, gy, gz);
    PhysicsWorld::Instance().setFixedTimestep(toFloat("PhysicsFixedTimestep", 1.0f / 60.0f));
    PhysicsWorld::Instance().setSleepThreshold(toFloat("PhysicsSleepThreshold", 0.05f));
}

void EditorBridgeImpl::shutdownPhysics()
{
    PhysicsWorld::Instance().shutdown();
}

void EditorBridgeImpl::snapshotEcsState()
{
    auto* level = DiagnosticsManager::Instance().getActiveLevelSoft();
    if (level) level->snapshotEcsState();
}

void EditorBridgeImpl::restoreEcsSnapshot()
{
    auto* level = DiagnosticsManager::Instance().getActiveLevelSoft();
    if (level) level->restoreEcsSnapshot();
}

unsigned int EditorBridgeImpl::findActiveCameraEntity() const
{
    ECS::Schema camSchema;
    camSchema.require<ECS::CameraComponent>().require<ECS::TransformComponent>();
    auto& ecs = ECS::ECSManager::Instance();
    const auto camEntities = ecs.getEntitiesMatchingSchema(camSchema);

    unsigned int activeCamEntity = 0;
    for (const auto e : camEntities)
    {
        const auto* cam = ecs.getComponent<ECS::CameraComponent>(e);
        if (cam && cam->isActive)
        {
            activeCamEntity = static_cast<unsigned int>(e);
            break;
        }
    }
    if (activeCamEntity == 0 && !camEntities.empty())
        activeCamEntity = static_cast<unsigned int>(camEntities.front());

    return activeCamEntity;
}

// ═════════════════════════════════════════════════════════════════════════
//  Physics
// ═════════════════════════════════════════════════════════════════════════

IEditorBridge::RaycastResult EditorBridgeImpl::raycastDown(float ox, float oy, float oz, float maxDist)
{
    auto hit = PhysicsWorld::Instance().raycast(ox, oy, oz, 0.0f, -1.0f, 0.0f, maxDist);
    return { hit.hit, hit.point[1] };
}

// ═════════════════════════════════════════════════════════════════════════
//  Diagnostics
// ═════════════════════════════════════════════════════════════════════════

void EditorBridgeImpl::setState(const std::string& key, const std::string& value)
{
    DiagnosticsManager::Instance().setState(key, value);
}

std::optional<std::string> EditorBridgeImpl::getState(const std::string& key) const
{
    return DiagnosticsManager::Instance().getState(key);
}

void EditorBridgeImpl::requestShutdown()
{
    DiagnosticsManager::Instance().requestShutdown();
}

bool EditorBridgeImpl::isShutdownRequested() const
{
    return DiagnosticsManager::Instance().isShutdownRequested();
}

bool EditorBridgeImpl::isProjectLoaded() const
{
    return DiagnosticsManager::Instance().isProjectLoaded();
}

// ═════════════════════════════════════════════════════════════════════════
//  Scripting
// ═════════════════════════════════════════════════════════════════════════

void EditorBridgeImpl::reloadScripts()
{
    Scripting::ReloadScripts();
}

void EditorBridgeImpl::loadEditorPlugins(const std::string& projectPath)
{
    Scripting::LoadEditorPlugins(projectPath);
}

// ═════════════════════════════════════════════════════════════════════════
//  Undo/Redo
// ═════════════════════════════════════════════════════════════════════════

void EditorBridgeImpl::pushUndoCommand(UndoCommand cmd)
{
    UndoRedoManager::Command c;
    c.description = std::move(cmd.description);
    c.execute     = std::move(cmd.execute);
    c.undo        = std::move(cmd.undo);
    UndoRedoManager::Instance().pushCommand(std::move(c));
}

bool EditorBridgeImpl::canUndo() const { return UndoRedoManager::Instance().canUndo(); }
bool EditorBridgeImpl::canRedo() const { return UndoRedoManager::Instance().canRedo(); }
void EditorBridgeImpl::undo()          { UndoRedoManager::Instance().undo(); }
void EditorBridgeImpl::redo()          { UndoRedoManager::Instance().redo(); }
void EditorBridgeImpl::clearUndoHistory() { UndoRedoManager::Instance().clear(); }

void EditorBridgeImpl::setOnUndoRedoChanged(std::function<void()> callback)
{
    UndoRedoManager::Instance().setOnChanged(std::move(callback));
}

// ═════════════════════════════════════════════════════════════════════════
//  Audio
// ═════════════════════════════════════════════════════════════════════════

void EditorBridgeImpl::stopAllAudio()
{
    AudioManager::Instance().stopAll();
}

#endif // ENGINE_EDITOR
