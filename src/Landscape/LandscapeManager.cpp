#include "LandscapeManager.h"

#include <filesystem>
#include <fstream>
#include <vector>
#include <string>

#include "../AssetManager/AssetManager.h"
#include "../AssetManager/AssetTypes.h"
#include "../Core/ECS/ECS.h"
#include "../Core/ECS/Components.h"
#include "../Diagnostics/DiagnosticsManager.h"
#include "../Logger/Logger.h"

namespace fs = std::filesystem;

ECS::Entity LandscapeManager::spawnLandscape(const LandscapeParams& params)
{
    auto& logger = Logger::Instance();
    auto& assetManager = AssetManager::Instance();

    // -----------------------------------------------------------------------
    // 1. Generate flat grid geometry
    //    Vertex layout: x, y, z, u, v  (5 floats per vertex)
    // -----------------------------------------------------------------------
    const int cols = std::max(1, params.subdivisionsX);
    const int rows = std::max(1, params.subdivisionsZ);

    const float halfW = params.width * 0.5f;
    const float halfD = params.depth * 0.5f;

    std::vector<float> vertices;
    vertices.reserve(static_cast<size_t>((cols + 1) * (rows + 1)) * 5);

    for (int r = 0; r <= rows; ++r)
    {
        for (int c = 0; c <= cols; ++c)
        {
            const float x =  -halfW + params.width  * (static_cast<float>(c) / static_cast<float>(cols));
            const float z =  -halfD + params.depth  * (static_cast<float>(r) / static_cast<float>(rows));
            const float u =  static_cast<float>(c) / static_cast<float>(cols);
            const float v =  static_cast<float>(r) / static_cast<float>(rows);
            vertices.push_back(x);
            vertices.push_back(0.0f); // flat Y
            vertices.push_back(z);
            vertices.push_back(u);
            vertices.push_back(v);
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(cols * rows) * 6);

    for (int r = 0; r < rows; ++r)
    {
        for (int c = 0; c < cols; ++c)
        {
            const uint32_t tl = static_cast<uint32_t>( r      * (cols + 1) + c    );
            const uint32_t tr = static_cast<uint32_t>( r      * (cols + 1) + c + 1);
            const uint32_t bl = static_cast<uint32_t>((r + 1) * (cols + 1) + c    );
            const uint32_t br = static_cast<uint32_t>((r + 1) * (cols + 1) + c + 1);
            // Triangle 1
            indices.push_back(tl); indices.push_back(bl); indices.push_back(tr);
            // Triangle 2
            indices.push_back(tr); indices.push_back(bl); indices.push_back(br);
        }
    }

    // -----------------------------------------------------------------------
    // 2. Write as a JSON asset file into Content/Landscape/
    // -----------------------------------------------------------------------
    const std::string relFolder = "Landscape";
    const std::string safeName  = params.name.empty() ? "Landscape" : params.name;
    const std::string relPath   = relFolder + "/" + safeName + ".asset";
    const std::string absPath   = assetManager.getAbsoluteContentPath(relPath);
    if (absPath.empty())
    {
        logger.log(Logger::Category::AssetManagement,
            "LandscapeManager: no active project – cannot save mesh.", Logger::LogLevel::ERROR);
        return 0;
    }

    {
        std::error_code ec;
        fs::create_directories(fs::path(absPath).parent_path(), ec);
    }

    {
        nlohmann::json fileJson;
        fileJson["magic"]   = 0x41535453;
        fileJson["version"] = 2;
        fileJson["type"]    = static_cast<int>(AssetType::Model3D);
        fileJson["name"]    = safeName;

        nlohmann::json data;
        data["m_vertices"] = vertices;
        data["m_indices"]  = indices;
        fileJson["data"]   = std::move(data);

        std::ofstream out(absPath, std::ios::out | std::ios::trunc);
        if (!out.is_open())
        {
            logger.log(Logger::Category::AssetManagement,
                "LandscapeManager: failed to write " + absPath, Logger::LogLevel::ERROR);
            return 0;
        }
        out << fileJson.dump(2);
    }

    logger.log(Logger::Category::AssetManagement,
        "LandscapeManager: wrote mesh to " + absPath, Logger::LogLevel::INFO);

    // -----------------------------------------------------------------------
    // 3. Load the asset into the AssetManager
    // -----------------------------------------------------------------------
    const int assetId = assetManager.loadAsset(absPath, AssetType::Model3D, AssetManager::Sync);
    if (assetId == 0)
    {
        logger.log(Logger::Category::AssetManagement,
            "LandscapeManager: loadAsset failed for " + relPath, Logger::LogLevel::ERROR);
        return 0;
    }

    // -----------------------------------------------------------------------
    // 4. Create ECS entity
    // -----------------------------------------------------------------------
    auto& ecs = ECS::ECSManager::Instance();
    const ECS::Entity entity = ecs.createEntity();

    ECS::TransformComponent transform{};
    ecs.addComponent<ECS::TransformComponent>(entity, transform);

    ECS::NameComponent nameComp;
    nameComp.displayName = safeName;
    ecs.addComponent<ECS::NameComponent>(entity, nameComp);

    ECS::MeshComponent mesh;
    mesh.meshAssetPath = relPath;
    ecs.addComponent<ECS::MeshComponent>(entity, mesh);

    auto* level = DiagnosticsManager::Instance().getActiveLevelSoft();
    if (level)
    {
        level->onEntityAdded(entity);
        level->setIsSaved(false);
    }

    DiagnosticsManager::Instance().setScenePrepared(false);

    logger.log(Logger::Category::Engine,
        "LandscapeManager: spawned entity " + std::to_string(entity) +
        " (" + safeName + ") " +
        std::to_string(cols) + "x" + std::to_string(rows) + " grid, " +
        std::to_string(vertices.size()/5) + " vertices.",
        Logger::LogLevel::INFO);

    return entity;
}
