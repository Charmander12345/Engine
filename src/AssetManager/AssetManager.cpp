#include "AssetManager.h"
#include <filesystem>
#include <fstream>
#include <cstdint>
#include "AssetTypes.h"

namespace fs = std::filesystem;

static void writeString(std::ofstream& out, const std::string& s)
{
    uint32_t len = static_cast<uint32_t>(s.size());
    out.write(reinterpret_cast<const char*>(&len), sizeof(len));
    out.write(s.data(), len);
}

static bool readString(std::ifstream& in, std::string& outStr)
{
    uint32_t len = 0;
    if (!in.read(reinterpret_cast<char*>(&len), sizeof(len))) return false;
    outStr.resize(len);
    if (len > 0 && !in.read(outStr.data(), len)) return false;
    return true;
}

AssetManager& AssetManager::Instance()
{
    static AssetManager instance;
    return instance;
}

bool AssetManager::initialize()
{
	m_garbageCollector = GarbageCollector();

    // Create a default 2D triangle asset that is tracked and can be saved via saveAllAssets.
    // It starts as unsaved and will be written on demand.
    {
        auto& logger = Logger::Instance();

        fs::path contentRoot = "Content";
        fs::path assetPath = contentRoot / "default_triangle2d.asset";

        auto tri = std::make_shared<Object2D>();
        tri->setName("DefaultTriangle2D");
        // store only the relative path inside Content
        tri->setPath("default_triangle2d.asset");
        tri->setAssetType(AssetType::Model2D);
        tri->setIsSaved(false);

        // positions (x,y) + color (r,g,b)
        tri->setVertices({
            0.0f,  0.5f,  1.0f, 0.0f, 0.0f,
           -0.5f, -0.5f,  0.0f, 1.0f, 0.0f,
            0.5f, -0.5f,  0.0f, 0.0f, 1.0f
        });

        m_defaultTriangle2D = tri;
        m_garbageCollector.registerResource(m_defaultTriangle2D);
        logger.log("Registered default 2D triangle asset.", Logger::LogLevel::INFO);
    }

    return true;
}

bool AssetManager::saveAllAssets()
{
    auto& diagnostics = DiagnosticsManager::Instance();
    auto& logger = Logger::Instance();

    diagnostics.setActionInProgress(DiagnosticsManager::ActionType::SavingAsset, true);

    // ensure we don't keep stale entries
    m_garbageCollector.collect();

    const auto& tracked = m_garbageCollector.getTrackedResourcesRef();

    size_t toSaveCount = 0;
    for (const auto& res : tracked)
    {
        if (res && !res->getIsSaved())
        {
            ++toSaveCount;
        }
    }

    logger.log("Saving assets... Pending: " + std::to_string(toSaveCount) + ", Tracked: " + std::to_string(tracked.size()), Logger::LogLevel::INFO);

    size_t savedCount = 0;
    for (const auto& res : tracked)
    {
        if (!res)
        {
            continue;
        }

        if (res->getIsSaved())
        {
            continue;
        }

        if (!saveAsset(res))
        {
            logger.log("Failed to auto-save asset: " + res->getName(), Logger::LogLevel::ERROR);
            diagnostics.setActionInProgress(DiagnosticsManager::ActionType::SavingAsset, false);
            return false;
        }

        ++savedCount;
    }

	auto level = diagnostics.getActiveLevel();
	if (level)
	{
		logger.log("Saving active level...", Logger::LogLevel::INFO);
		if (!saveAsset(std::shared_ptr<EngineObject>(level, [](EngineObject*) {})))
		{
			logger.log("Failed to save active level: " + level->getName(), Logger::LogLevel::ERROR);
			diagnostics.setActionInProgress(DiagnosticsManager::ActionType::SavingAsset, false);
			return false;
		}
	}

    logger.log(
        "All unsaved assets have been saved. Saved: " + std::to_string(savedCount) + "/" + std::to_string(toSaveCount),
        Logger::LogLevel::INFO);

    diagnostics.setActionInProgress(DiagnosticsManager::ActionType::SavingAsset, false);
    return true;
}

bool AssetManager::loadAsset(const std::string& path)
{
    auto& logger = Logger::Instance();
    logger.log("Loading asset: " + path, Logger::LogLevel::INFO);

    auto& diagnostics = DiagnosticsManager::Instance();
    bool loaded = diagnostics.isProjectLoaded();
    if (!loaded || diagnostics.getProjectInfo().projectPath.empty())
    {
        logger.log("Asset load failed (no project loaded)", Logger::LogLevel::ERROR);
        return false;
    }

    fs::path contentRoot = fs::path(diagnostics.getProjectInfo().projectPath) / "Content";

    // 'path' is expected to be the relative path inside Content.
    fs::path relPath = fs::path(path);
    if (relPath.is_absolute())
    {
        logger.log("Asset load failed (path must be relative to Content): " + relPath.string(), Logger::LogLevel::ERROR);
        return false;
    }

    fs::path absolutePath = contentRoot / relPath;
    if (!fs::exists(absolutePath))
    {
        logger.log("Asset load failed (missing file): " + absolutePath.string(), Logger::LogLevel::ERROR);
        return false;
    }

    std::ifstream in(absolutePath, std::ios::binary);
    if (!in.is_open())
    {
        logger.log("Failed to open asset file: " + absolutePath.string(), Logger::LogLevel::ERROR);
        return false;
    }

    uint32_t magic = 0;
    uint32_t version = 0;
    if (!in.read(reinterpret_cast<char*>(&magic), sizeof(magic))) return false;
    if (!in.read(reinterpret_cast<char*>(&version), sizeof(version))) return false;
    if (magic != 0x41535453 || version != 2) // 'ASTS'
    {
        logger.log("Invalid asset file format", Logger::LogLevel::ERROR);
        return false;
    }

    int32_t typeInt = 0;
    if (!in.read(reinterpret_cast<char*>(&typeInt), sizeof(typeInt))) return false;
    AssetType type = static_cast<AssetType>(typeInt);

    std::string name;
    std::string storedRelPath;
    std::string materialAssetPath;
    if (!readString(in, name)) return false;
    if (!readString(in, storedRelPath)) return false;
    if (!readString(in, materialAssetPath)) return false;

    std::shared_ptr<EngineObject> obj;
    switch (type)
    {
    case AssetType::Model2D:
        obj = std::make_shared<Object2D>();
        break;
    case AssetType::Model3D:
        obj = std::make_shared<Object3D>();
        break;
    case AssetType::Level:
        obj = std::make_shared<EngineLevel>();
        break;
    default:
        obj = std::make_shared<EngineObject>();
        break;
    }

    obj->setName(name);
    // store only relative path inside Content
    obj->setPath(storedRelPath.empty() ? relPath.generic_string() : storedRelPath);
    obj->setAssetType(type);
    obj->setIsSaved(true);

    if (type == AssetType::Model2D)
    {
        auto obj2d = std::static_pointer_cast<Object2D>(obj);
        obj2d->setMaterialAssetPath(materialAssetPath);

        uint32_t vertCount = 0;
        if (!in.read(reinterpret_cast<char*>(&vertCount), sizeof(vertCount))) return false;
        std::vector<float> verts(vertCount);
        if (vertCount > 0)
        {
            if (!in.read(reinterpret_cast<char*>(verts.data()), vertCount * sizeof(float))) return false;
        }
        obj2d->setVertices(verts);
    }

    if (type == AssetType::Model3D)
    {
        auto obj3d = std::static_pointer_cast<Object3D>(obj);
        obj3d->setMaterialAssetPath(materialAssetPath);

        uint32_t vertCount = 0;
        if (!in.read(reinterpret_cast<char*>(&vertCount), sizeof(vertCount))) return false;
        std::vector<float> verts(vertCount);
        if (vertCount > 0)
        {
            if (!in.read(reinterpret_cast<char*>(verts.data()), vertCount * sizeof(float))) return false;
        }
        obj3d->setVertices(verts);
    }

    if (type == AssetType::Level)
    {
        auto level = std::static_pointer_cast<EngineLevel>(obj);

        uint32_t depCount = 0;
        if (!in.read(reinterpret_cast<char*>(&depCount), sizeof(depCount))) return false;
        for (uint32_t i = 0; i < depCount; ++i)
        {
            std::string objPath;
            if (!readString(in, objPath)) return false;

            fs::path p(objPath);
            if (p.is_absolute())
            {
                logger.log("Invalid asset path (should be relative to Content): " + objPath, Logger::LogLevel::ERROR);
                return false;
            }

            EngineObject dep;
            dep.setPath(p.generic_string());
            level->getWorldObjects().push_back(dep);
        }
    }

    m_garbageCollector.registerResource(obj);

    logger.log("Asset loaded: " + name + " (" + obj->getPath() + ")", Logger::LogLevel::INFO);
    return true;
}

bool AssetManager::saveAsset(const std::shared_ptr<EngineObject>& object)
{
    auto& logger = Logger::Instance();
    if (!object)
    {
        logger.log("saveAsset failed: object is null", Logger::LogLevel::ERROR);
        return false;
    }

    const auto& diagnostics = DiagnosticsManager::Instance();
    const auto& projInfo = diagnostics.getProjectInfo();
    if (projInfo.projectPath.empty())
    {
        logger.log("saveAsset failed: no project loaded (projectPath is empty)", Logger::LogLevel::ERROR);
        return false;
    }

    // The object stores only a relative path inside the Content folder.
    fs::path relAssetPath = fs::path(object->getPath());
    if (relAssetPath.is_absolute())
    {
        logger.log("saveAsset failed: object path must be relative to Content: " + relAssetPath.string(), Logger::LogLevel::ERROR);
        return false;
    }

    fs::path contentRoot = fs::path(projInfo.projectPath) / "Content";
    std::error_code ec;
    fs::create_directories(contentRoot, ec);

    fs::path finalAssetPath = contentRoot / relAssetPath;
    fs::create_directories(finalAssetPath.parent_path(), ec);

    logger.log("Saving asset: " + object->getName() + " at " + finalAssetPath.string(), Logger::LogLevel::INFO);

    std::ofstream out(finalAssetPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        logger.log("Failed to open asset file for writing: " + finalAssetPath.string(), Logger::LogLevel::FATAL);
        return false;
    }

    uint32_t magic = 0x41535453; // 'ASTS'
    uint32_t version = 2;
    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));

    int32_t typeInt = static_cast<int32_t>(object->getAssetType());
    out.write(reinterpret_cast<const char*>(&typeInt), sizeof(typeInt));

    writeString(out, object->getName());
    // persist relative path inside Content
    writeString(out, relAssetPath.generic_string());

    std::string materialAssetPath;
    switch (object->getAssetType())
    {
    case AssetType::Model2D:
    {
        auto obj2d = std::dynamic_pointer_cast<Object2D>(object);
        if (obj2d)
        {
            materialAssetPath = obj2d->getMaterialAssetPath();
        }
        writeString(out, materialAssetPath);

        if (obj2d)
        {
            const auto& verts = obj2d->getVertices();
            uint32_t vertCount = static_cast<uint32_t>(verts.size());
            out.write(reinterpret_cast<const char*>(&vertCount), sizeof(vertCount));
            if (vertCount > 0)
            {
                out.write(reinterpret_cast<const char*>(verts.data()), vertCount * sizeof(float));
            }
        }
        else
        {
            uint32_t zero = 0;
            out.write(reinterpret_cast<const char*>(&zero), sizeof(zero));
        }
        break;
    }
    case AssetType::Model3D:
    {
        auto obj3d = std::dynamic_pointer_cast<Object3D>(object);
        if (obj3d)
        {
            materialAssetPath = obj3d->getMaterialAssetPath();
        }
        writeString(out, materialAssetPath);

        if (obj3d)
        {
            const auto& verts = obj3d->getVertices();
            uint32_t vertCount = static_cast<uint32_t>(verts.size());
            out.write(reinterpret_cast<const char*>(&vertCount), sizeof(vertCount));
            if (vertCount > 0)
            {
                out.write(reinterpret_cast<const char*>(verts.data()), vertCount * sizeof(float));
            }
        }
        else
        {
            uint32_t zero = 0;
            out.write(reinterpret_cast<const char*>(&zero), sizeof(zero));
        }
        break;
    }
    case AssetType::Level:
    {
        auto level = std::dynamic_pointer_cast<EngineLevel>(object);
        // keep string slot for format compatibility
        writeString(out, materialAssetPath);

        uint32_t depCount = 0;
        if (level)
        {
            auto& deps = level->getWorldObjects();
            depCount = static_cast<uint32_t>(deps.size());
        }

        out.write(reinterpret_cast<const char*>(&depCount), sizeof(depCount));

        if (level)
        {
            auto& deps = level->getWorldObjects();
            for (uint32_t i = 0; i < depCount; ++i)
            {
                // only store the object path (relative to Content)
                writeString(out, deps[i].getPath());
            }
        }
        break;
    }
    default:
        writeString(out, materialAssetPath);
        break;
    }

    if (!out.good())
    {
        logger.log("Error writing asset file: " + finalAssetPath.string(), Logger::LogLevel::ERROR);
        return false;
    }

    object->setIsSaved(true);
    return true;
}

std::shared_ptr<EngineObject> AssetManager::createAsset(AssetType type, const std::string& path, const std::string& name)
{
    auto& logger = Logger::Instance();
    logger.log("Creating asset of type with name: " + name + " at " + path, Logger::LogLevel::INFO);

    std::shared_ptr<EngineObject> obj;
    switch (type)
    {
    case AssetType::Model2D:
        obj = std::make_shared<Object2D>();
        break;
    case AssetType::Model3D:
        obj = std::make_shared<Object3D>();
        break;
    case AssetType::Texture:
    case AssetType::Audio:
    case AssetType::Script:
    case AssetType::Shader:
    case AssetType::Unknown:
    default:
        obj = std::make_shared<EngineObject>();
        break;
    }

    // Store ONLY the relative path within the Content folder.
    // Caller can pass e.g. "Meshes/cube.asset".
    fs::path rel = fs::path(path);
    if (rel.is_absolute())
    {
        rel = rel.filename();
    }

    obj->setPath(rel.generic_string());
    obj->setName(name);
    obj->setAssetType(type);
    obj->setIsSaved(false);
    return obj;
}

bool AssetManager::loadProject(const std::string& projectPath)
{
    auto& logger = Logger::Instance();
    auto& diagnostics = DiagnosticsManager::Instance();
    diagnostics.setActionInProgress(DiagnosticsManager::ActionType::LoadingProject, true);

    std::error_code ec;
    fs::path root = fs::absolute(projectPath, ec);
    if (ec || !fs::exists(root))
    {
        logger.log("Project path does not exist: " + projectPath, Logger::LogLevel::ERROR);
        diagnostics.setActionInProgress(DiagnosticsManager::ActionType::LoadingProject, false);
        return false;
    }

    fs::path projectFile = root;
    if (fs::is_directory(root))
    {
        bool found = false;
        for (const auto& entry : fs::directory_iterator(root))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".project")
            {
                projectFile = entry.path();
                found = true;
                break;
            }
        }
        if (!found)
        {
            logger.log("No .project file found in: " + root.string(), Logger::LogLevel::ERROR);
            diagnostics.setActionInProgress(DiagnosticsManager::ActionType::LoadingProject, false);
            return false;
        }
    }
    else if (projectFile.extension() != ".project")
    {
        logger.log("Invalid project file: " + projectFile.string(), Logger::LogLevel::ERROR);
        diagnostics.setActionInProgress(DiagnosticsManager::ActionType::LoadingProject, false);
        return false;
    }

    DiagnosticsManager::ProjectInfo info;
    info.projectName = projectFile.stem().string();
    info.projectVersion = "";
    info.engineVersion = "";
    info.projectPath = root.string();
    info.selectedRHI = DiagnosticsManager::RHIType::Unknown;

    std::ifstream in(projectFile);
    if (!in.is_open())
    {
        logger.log("Failed to open project file: " + projectFile.string(), Logger::LogLevel::ERROR);
        diagnostics.setActionInProgress(DiagnosticsManager::ActionType::LoadingProject, false);
        return false;
    }

    std::string LevelPath;
    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] == '#')
            continue;
        auto pos = line.find('=');
        if (pos == std::string::npos)
            continue;
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        if (key == "Name") info.projectName = value;
        else if (key == "Version") info.projectVersion = value;
        else if (key == "EngineVersion") info.engineVersion = value;
        else if (key == "Path") info.projectPath = value;
        else if (key == "RHI")
        {
            if (value == "OpenGL") info.selectedRHI = DiagnosticsManager::RHIType::OpenGL;
            else if (value == "DirectX11") info.selectedRHI = DiagnosticsManager::RHIType::DirectX11;
            else if (value == "DirectX12") info.selectedRHI = DiagnosticsManager::RHIType::DirectX12;
            else info.selectedRHI = DiagnosticsManager::RHIType::Unknown;
        }
        else if (key == "Level")
        {
            LevelPath = value;
		}
    }

    // Ensure path is always set even if missing from file
    if (info.projectPath.empty())
    {
        info.projectPath = root.string();
    }

    diagnostics.setProjectInfo(info);

    if (!LevelPath.empty())
    {
        auto level = std::make_unique<EngineLevel>();
        level->setPath(LevelPath);
        level->setName(fs::path(LevelPath).stem().string());
        diagnostics.setActiveLevel(std::move(level));
    }
    else
    {
        diagnostics.setActiveLevel(nullptr);
        logger.log("No active level set for project.", Logger::LogLevel::WARNING);
    }

    if (!diagnostics.loadProjectConfig())
    {
        logger.log("Failed to load project config.", Logger::LogLevel::ERROR);
    }

    diagnostics.setActionInProgress(DiagnosticsManager::ActionType::LoadingProject, false);
    logger.log("Project loaded: " + info.projectName, Logger::LogLevel::INFO);
    return true;
}

bool AssetManager::saveProject(const std::string& projectPath)
{
    auto& logger = Logger::Instance();
    auto& diagnostics = DiagnosticsManager::Instance();
    diagnostics.setActionInProgress(DiagnosticsManager::ActionType::SavingProject, true);

    std::error_code ec;
    fs::path root = fs::absolute(projectPath, ec);
    if (ec)
    {
        logger.log("Invalid project path: " + projectPath, Logger::LogLevel::ERROR);
        diagnostics.setActionInProgress(DiagnosticsManager::ActionType::SavingProject, false);
        return false;
    }

    fs::create_directories(root, ec);
    fs::create_directories(root / "Shaders", ec);
    fs::create_directories(root / "Config", ec);
    fs::create_directories(root / "Content", ec);
    fs::create_directories(root / "Content" / "Levels");

    if (ec)
    {
        logger.log("Failed to create project directories: " + root.string(), Logger::LogLevel::ERROR);
        diagnostics.setActionInProgress(DiagnosticsManager::ActionType::SavingProject, false);
        return false;
    }

    auto info = diagnostics.getProjectInfo();
    if (info.projectPath.empty())
    {
        info.projectPath = root.string();
        diagnostics.setProjectInfo(info);
    }

    if (!diagnostics.saveProjectConfig())
    {
        logger.log("Failed to save project config.", Logger::LogLevel::ERROR);
    }

    fs::path projectFile = root / (info.projectName + ".project");
    std::ofstream out(projectFile, std::ios::out | std::ios::trunc);
    if (!out.is_open())
    {
        logger.log("Failed to write project file: " + projectFile.string(), Logger::LogLevel::ERROR);
        diagnostics.setActionInProgress(DiagnosticsManager::ActionType::SavingProject, false);
        return false;
    }

    out << "Name=" << info.projectName << "\n";
    out << "Version=" << info.projectVersion << "\n";
    out << "EngineVersion=" << info.engineVersion << "\n";
    out << "Path=" << info.projectPath << "\n";
    out << "RHI=" << DiagnosticsManager::rhiTypeToString(info.selectedRHI) << "\n";
	out << "Level=" << (diagnostics.getActiveLevel() ? diagnostics.getActiveLevel()->getPath() : "") << "\n";

    diagnostics.setActionInProgress(DiagnosticsManager::ActionType::SavingProject, false);
    logger.log("Project saved: " + projectFile.string(), Logger::LogLevel::INFO);
    return true;
}

bool AssetManager::createProject(const std::string& parentDir, const std::string& projectName, const DiagnosticsManager::ProjectInfo& info)
{
    auto& logger = Logger::Instance();
    auto& diagnostics = DiagnosticsManager::Instance();
    diagnostics.setActionInProgress(DiagnosticsManager::ActionType::LoadingProject, true);

    fs::path root = fs::path(parentDir) / projectName;
    std::error_code ec;
    fs::create_directories(root / "Shaders", ec);
    fs::create_directories(root / "Config", ec);
    fs::create_directories(root / "Content", ec);
    fs::create_directories(root / "Content" / "Levels");

    if (ec)
    {
        logger.log("Failed to create project directory: " + root.string(), Logger::LogLevel::ERROR);
        diagnostics.setActionInProgress(DiagnosticsManager::ActionType::LoadingProject, false);
        return false;
    }

    auto infoWithPath = info;
    infoWithPath.projectPath = fs::absolute(root, ec).string();
    if (ec)
    {
        infoWithPath.projectPath = root.string();
    }

    diagnostics.setProjectInfo(infoWithPath);

    auto defaultlevel = std::make_unique<EngineLevel>();
    defaultlevel->setName("Default");
    defaultlevel->setPath("Levels\\DefaultLevel.map");
    diagnostics.setActiveLevel(std::move(defaultlevel));

    // ensure defaults.ini exists for the new project
    diagnostics.loadProjectConfig();

    bool saved = saveProject(root.string());
    diagnostics.setActionInProgress(DiagnosticsManager::ActionType::LoadingProject, false);
    if (saved)
    {
        logger.log("Project created at: " + root.string(), Logger::LogLevel::INFO);
    }
    return saved;
}
