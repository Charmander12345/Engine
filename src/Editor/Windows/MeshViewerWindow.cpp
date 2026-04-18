#include "MeshViewerWindow.h"

#include "../../Renderer/IRenderObject3D.h"
#include "../../Renderer/RenderResourceManager.h"
#include "../../AssetManager/AssetManager.h"
#include "../../Core/Asset.h"
#include "../../Core/EngineLevel.h"
#include "../../Logger/Logger.h"

#include <cmath>
#include <algorithm>

MeshViewerWindow::MeshViewerWindow() = default;

MeshViewerWindow::~MeshViewerWindow() = default;

bool MeshViewerWindow::initialize(const std::string& assetPath, RenderResourceManager& resourceMgr)
{
    auto& logger = Logger::Instance();

    if (assetPath.empty())
    {
        logger.log(Logger::Category::Rendering, "MeshViewer::initialize: assetPath is empty.", Logger::LogLevel::WARNING);
        return false;
    }

    m_assetPath = assetPath;
    logger.log(Logger::Category::Rendering, "MeshViewer::initialize: starting for '" + assetPath + "'", Logger::LogLevel::INFO);

    auto asset = AssetManager::Instance().getLoadedAssetByPath(assetPath);
    if (!asset)
    {
        logger.log(Logger::Category::Rendering,
            "MeshViewer::initialize: getLoadedAssetByPath returned nullptr for '" + assetPath
            + "'. The asset was not loaded into memory.",
            Logger::LogLevel::WARNING);
        return false;
    }

    logger.log(Logger::Category::Rendering,
        "MeshViewer::initialize: asset loaded — name='" + asset->getName()
        + "' id=" + std::to_string(asset->getId())
        + " type=" + std::to_string(static_cast<int>(asset->getAssetType())),
        Logger::LogLevel::INFO);

    // Create 3D object to read bounding box for initial camera placement.
    std::vector<std::shared_ptr<Texture>> emptyTextures;
    m_meshObject = resourceMgr.getOrCreateObject3D(asset, emptyTextures);

    // Compute a good initial camera position looking at the mesh center.
    if (m_meshObject && m_meshObject->hasLocalBounds())
    {
        const auto bmin = m_meshObject->getLocalBoundsMin();
        const auto bmax = m_meshObject->getLocalBoundsMax();
        const float cx = (bmin.x + bmax.x) * 0.5f;
        const float cy = (bmin.y + bmax.y) * 0.5f;
        const float cz = (bmin.z + bmax.z) * 0.5f;

        const float extX = bmax.x - bmin.x;
        const float extY = bmax.y - bmin.y;
        const float extZ = bmax.z - bmin.z;
        const float maxExt = std::max({ extX, extY, extZ, 0.1f });
        const float dist = maxExt * 2.0f;

        // Place camera at front-right-above the mesh center
        m_initialCamPos = Vec3{ cx + dist * 0.5f, cy + dist * 0.35f, cz + dist * 0.5f };
        // Look toward the center: yaw ~225° (front-left), pitch slightly down
        m_initialCamRot = Vec2{ 225.0f, -20.0f };
    }
    else
    {
        m_initialCamPos = Vec3{ 2.0f, 1.5f, 2.0f };
        m_initialCamRot = Vec2{ 225.0f, -20.0f };
    }

    m_initialized = true;
    return true;
}

int MeshViewerWindow::getVertexCount() const
{
    return m_meshObject ? static_cast<int>(m_meshObject->getVertexCount()) : 0;
}

int MeshViewerWindow::getIndexCount() const
{
    return m_meshObject ? static_cast<int>(m_meshObject->getIndexCount()) : 0;
}

// ---------------------------------------------------------------------------
// Runtime level for renderWorld integration
// ---------------------------------------------------------------------------
bool MeshViewerWindow::createRuntimeLevel(const std::string& assetPath)
{
    if (m_runtimeLevel)
        return true;

    auto& logger = Logger::Instance();
    auto& assetMgr = AssetManager::Instance();

    // --- Read material path from the mesh .asset data ---
    std::string materialPath;
    {
        auto meshAsset = assetMgr.getLoadedAssetByPath(assetPath);
        if (!meshAsset)
        {
            const std::string resolvedPath = assetMgr.getAbsoluteContentPath(assetPath);
            if (!resolvedPath.empty())
                meshAsset = assetMgr.getLoadedAssetByPath(resolvedPath);
        }
        if (meshAsset)
        {
            const auto& data = meshAsset->getData();
            if (data.contains("m_materialAssetPaths") && data["m_materialAssetPaths"].is_array()
                && !data["m_materialAssetPaths"].empty())
            {
                materialPath = data["m_materialAssetPaths"][0].get<std::string>();
                logger.log(Logger::Category::Rendering,
                    "MeshViewer::createRuntimeLevel: found material '" + materialPath + "' in mesh asset",
                    Logger::LogLevel::INFO);
            }
        }
    }

    m_runtimeLevel = std::make_unique<EngineLevel>();
    m_runtimeLevel->setName("__MeshViewer__");
    m_runtimeLevel->setAssetType(AssetType::Level);

    json entities = json::array();

    // Mesh entity
    {
        json meshEntity = json::object();
        json comps = json::object();
        comps["Transform"] = json{
            {"position", json::array({0.0f, 0.0f, 0.0f})},
            {"rotation", json::array({0.0f, 0.0f, 0.0f})},
            {"scale",    json::array({1.0f, 1.0f, 1.0f})}
        };
        comps["Mesh"] = json{ {"meshAssetPath", assetPath} };
        if (!materialPath.empty())
        {
            comps["Material"] = json{ {"materialAssetPath", materialPath} };
        }
        comps["Name"] = json{ {"displayName", "MeshPreview"} };
        meshEntity["components"] = std::move(comps);
        entities.push_back(std::move(meshEntity));
    }

    // Directional light — shines downward from upper-front-right
    // pitch>0 → direction.y = -sin(pitch) < 0 → downward
    {
        json lightEntity = json::object();
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
        lightEntity["components"] = std::move(comps);
        entities.push_back(std::move(lightEntity));
    }

    // Ground plane with WorldGrid material
    {
        json groundEntity = json::object();
        json comps = json::object();
        comps["Transform"] = json{
            {"position", json::array({0.0f, -0.5f, 0.0f})},
            {"rotation", json::array({0.0f, 0.0f, 0.0f})},
            {"scale",    json::array({20.0f, 0.01f, 20.0f})}
        };
        comps["Mesh"] = json{ {"meshAssetPath", "default_quad3d.asset"} };
        comps["Material"] = json{ {"materialAssetPath", "Materials/WorldGrid.asset"} };
        comps["Name"] = json{ {"displayName", "PreviewGround"} };
        groundEntity["components"] = std::move(comps);
        entities.push_back(std::move(groundEntity));
    }

    json levelData = json::object();
    levelData["Entities"] = std::move(entities);

    // Store initial camera in the level so renderWorld restores it on prepare
    levelData["EditorCamera"] = json{
        {"position", json::array({m_initialCamPos.x, m_initialCamPos.y, m_initialCamPos.z})},
        {"rotation", json::array({m_initialCamRot.x, m_initialCamRot.y})}
    };

    m_runtimeLevel->setLevelData(levelData);
    m_runtimeLevel->setEditorCameraPosition(m_initialCamPos);
    m_runtimeLevel->setEditorCameraRotation(m_initialCamRot);
    m_runtimeLevel->setHasEditorCamera(true);

    return true;
}

void MeshViewerWindow::destroyRuntimeLevel()
{
    m_runtimeLevel.reset();
    m_viewerEntity = 0;
}

std::unique_ptr<EngineLevel> MeshViewerWindow::takeRuntimeLevel()
{
    return std::move(m_runtimeLevel);
}

void MeshViewerWindow::giveRuntimeLevel(std::unique_ptr<EngineLevel> level)
{
    m_runtimeLevel = std::move(level);
}
