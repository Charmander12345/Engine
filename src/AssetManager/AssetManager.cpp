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

bool AssetManager::loadAsset(const std::string& path)
{
    auto& logger = Logger::Instance();
    logger.log("Loading asset: " + path, Logger::LogLevel::INFO);

    bool loaded = DiagnosticsManager::Instance().isProjectLoaded();
    fs::path absolutePath = fs::absolute(path);
    if (!fs::exists(absolutePath) || !loaded)
    {
        logger.log("Asset load failed (missing file or no project loaded): " + absolutePath.string(), Logger::LogLevel::ERROR);
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
    std::string relPath;
    std::string materialAssetPath;
    if (!readString(in, name)) return false;
    if (!readString(in, relPath)) return false;
    if (!readString(in, materialAssetPath)) return false;

    std::shared_ptr<EngineObject> obj;
    switch (type)
    {
    case AssetType::Model3D:
        obj = std::make_shared<Object3D>();
        break;
    default:
        obj = std::make_shared<EngineObject>();
        break;
    }

    fs::path contentRoot = fs::current_path() / "content";
    fs::path absAssetPath = contentRoot / relPath;

    obj->setName(name);
    obj->setPath(absAssetPath.string());
    obj->setAssetType(type);

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

    logger.log("Asset loaded: " + name + " (" + relPath + ")", Logger::LogLevel::INFO);
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

    fs::path contentRoot = fs::current_path() / "Content";
    std::error_code ec;
    fs::create_directories(contentRoot, ec);

    fs::path assetPath = fs::path(object->getPath());
    fs::path relPath = fs::relative(assetPath, contentRoot, ec);
    if (ec)
    {
        relPath = assetPath;
    }

    logger.log("Saving asset: " + object->getName() + " at " + assetPath.string(), Logger::LogLevel::INFO);

    std::ofstream out(assetPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        logger.log("Failed to open asset file for writing: " + assetPath.string(), Logger::LogLevel::FATAL);
        return false;
    }

    uint32_t magic = 0x41535453; // 'ASTS'
    uint32_t version = 2;
    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));

    int32_t typeInt = static_cast<int32_t>(object->getAssetType());
    out.write(reinterpret_cast<const char*>(&typeInt), sizeof(typeInt));

    writeString(out, object->getName());
    writeString(out, relPath.string());

    std::string materialAssetPath;
    if (object->getAssetType() == AssetType::Model3D)
    {
        auto obj3d = std::dynamic_pointer_cast<Object3D>(object);
        if (obj3d)
        {
            materialAssetPath = obj3d->getMaterialAssetPath();
        }
    }
    writeString(out, materialAssetPath);

    if (object->getAssetType() == AssetType::Model3D)
    {
        auto obj3d = std::dynamic_pointer_cast<Object3D>(object);
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
    }

    if (!out.good())
    {
        logger.log("Error writing asset file: " + assetPath.string(), Logger::LogLevel::ERROR);
        return false;
    }

    return true;
}

std::shared_ptr<EngineObject> AssetManager::createAsset(AssetType type, const std::string& path, const std::string& name)
{
    auto& logger = Logger::Instance();
    logger.log("Creating asset of type with name: " + name + " at " + path, Logger::LogLevel::INFO);

    std::shared_ptr<EngineObject> obj;
    switch (type)
    {
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

    obj->setPath(path);
    obj->setName(name);
    obj->setAssetType(type);
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
    info.selectedRHI = DiagnosticsManager::RHIType::Unknown;

    std::ifstream in(projectFile);
    if (!in.is_open())
    {
        logger.log("Failed to open project file: " + projectFile.string(), Logger::LogLevel::ERROR);
        diagnostics.setActionInProgress(DiagnosticsManager::ActionType::LoadingProject, false);
        return false;
    }

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
        else if (key == "RHI")
        {
            if (value == "OpenGL") info.selectedRHI = DiagnosticsManager::RHIType::OpenGL;
            else if (value == "DirectX11") info.selectedRHI = DiagnosticsManager::RHIType::DirectX11;
            else if (value == "DirectX12") info.selectedRHI = DiagnosticsManager::RHIType::DirectX12;
            else info.selectedRHI = DiagnosticsManager::RHIType::Unknown;
        }
    }

    diagnostics.setProjectInfo(info);
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

    if (ec)
    {
        logger.log("Failed to create project directories: " + root.string(), Logger::LogLevel::ERROR);
        diagnostics.setActionInProgress(DiagnosticsManager::ActionType::SavingProject, false);
        return false;
    }

    const auto& info = diagnostics.getProjectInfo();
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
    out << "RHI=" << DiagnosticsManager::rhiTypeToString(info.selectedRHI) << "\n";

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

    if (ec)
    {
        logger.log("Failed to create project directory: " + root.string(), Logger::LogLevel::ERROR);
        diagnostics.setActionInProgress(DiagnosticsManager::ActionType::LoadingProject, false);
        return false;
    }

    diagnostics.setProjectInfo(info);
    bool saved = saveProject(root.string());
    diagnostics.setActionInProgress(DiagnosticsManager::ActionType::LoadingProject, false);
    if (saved)
    {
        logger.log("Project created at: " + root.string(), Logger::LogLevel::INFO);
    }
    return saved;
}
