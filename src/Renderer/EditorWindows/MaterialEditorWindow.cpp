#include "MaterialEditorWindow.h"

#include "../../AssetManager/AssetManager.h"
#include "../../Core/Asset.h"
#include "../../Core/EngineLevel.h"
#include "../../Logger/Logger.h"

MaterialEditorWindow::MaterialEditorWindow() = default;

MaterialEditorWindow::~MaterialEditorWindow() = default;

bool MaterialEditorWindow::initialize(const std::string& materialAssetPath)
{
    auto& logger = Logger::Instance();

    if (materialAssetPath.empty())
    {
        logger.log(Logger::Category::Rendering,
            "MaterialEditor::initialize: materialAssetPath is empty.",
            Logger::LogLevel::WARNING);
        return false;
    }

    m_assetPath = materialAssetPath;

    // Fixed camera for a unit cube at origin – front-right-above
    m_initialCamPos = Vec3{ 2.0f, 1.5f, 2.0f };
    m_initialCamRot = Vec2{ 225.0f, -20.0f };

    m_initialized = true;

    logger.log(Logger::Category::Rendering,
        "MaterialEditor::initialize: ready for '" + materialAssetPath + "'",
        Logger::LogLevel::INFO);
    return true;
}

// ---------------------------------------------------------------------------
// Runtime level for renderWorld integration
// ---------------------------------------------------------------------------
bool MaterialEditorWindow::createRuntimeLevel()
{
    if (m_runtimeLevel)
        return true;

    auto& logger = Logger::Instance();

    m_runtimeLevel = std::make_unique<EngineLevel>();
    m_runtimeLevel->setName("__MaterialEditor__");
    m_runtimeLevel->setAssetType(AssetType::Level);

    json entities = json::array();

    // Preview cube with the material applied
    {
        json entity = json::object();
        json comps = json::object();
        comps["Transform"] = json{
            {"position", json::array({0.0f, 0.0f, 0.0f})},
            {"rotation", json::array({0.0f, 0.0f, 0.0f})},
            {"scale",    json::array({1.0f, 1.0f, 1.0f})}
        };
        comps["Mesh"] = json{ {"meshAssetPath", "default_quad3d.asset"} };
        comps["Material"] = json{ {"materialAssetPath", m_assetPath} };
        comps["Name"] = json{ {"displayName", "MaterialPreview"} };
        entity["components"] = std::move(comps);
        entities.push_back(std::move(entity));
    }

    // Directional light
    {
        json entity = json::object();
        json comps = json::object();
        comps["Transform"] = json{
            {"position", json::array({0.0f, 0.0f, 0.0f})},
            {"rotation", json::array({50.0f, 30.0f, 0.0f})},
            {"scale",    json::array({1.0f, 1.0f, 1.0f})}
        };
        comps["Light"] = json{
            {"type", 1},
            {"color", json::array({0.9f, 0.85f, 0.78f})},
            {"intensity", 0.8f},
            {"range", 100.0f},
            {"spotAngle", 0.0f}
        };
        comps["Name"] = json{ {"displayName", "PreviewLight"} };
        entity["components"] = std::move(comps);
        entities.push_back(std::move(entity));
    }

    // Ground plane
    {
        json entity = json::object();
        json comps = json::object();
        comps["Transform"] = json{
            {"position", json::array({0.0f, -0.5f, 0.0f})},
            {"rotation", json::array({0.0f, 0.0f, 0.0f})},
            {"scale",    json::array({20.0f, 0.01f, 20.0f})}
        };
        comps["Mesh"] = json{ {"meshAssetPath", "default_quad3d.asset"} };
        comps["Material"] = json{ {"materialAssetPath", "Materials/WorldGrid.asset"} };
        comps["Name"] = json{ {"displayName", "PreviewGround"} };
        entity["components"] = std::move(comps);
        entities.push_back(std::move(entity));
    }

    json levelData = json::object();
    levelData["Entities"] = std::move(entities);

    levelData["EditorCamera"] = json{
        {"position", json::array({m_initialCamPos.x, m_initialCamPos.y, m_initialCamPos.z})},
        {"rotation", json::array({m_initialCamRot.x, m_initialCamRot.y})}
    };

    m_runtimeLevel->setLevelData(levelData);
    m_runtimeLevel->setEditorCameraPosition(m_initialCamPos);
    m_runtimeLevel->setEditorCameraRotation(m_initialCamRot);
    m_runtimeLevel->setHasEditorCamera(true);

    logger.log(Logger::Category::Rendering,
        "MaterialEditor: runtime level created for '" + m_assetPath + "'",
        Logger::LogLevel::INFO);
    return true;
}

void MaterialEditorWindow::destroyRuntimeLevel()
{
    m_runtimeLevel.reset();
    m_previewEntity = 0;
}

std::unique_ptr<EngineLevel> MaterialEditorWindow::takeRuntimeLevel()
{
    return std::move(m_runtimeLevel);
}

void MaterialEditorWindow::giveRuntimeLevel(std::unique_ptr<EngineLevel> level)
{
    m_runtimeLevel = std::move(level);
}
