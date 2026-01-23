#include "AssetManager.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <filesystem>
#include <fstream>
#include <cstdint>
#include "AssetTypes.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>

#include "../Basics/Material.h"

namespace fs = std::filesystem;

namespace
{
    struct ImportDialogContext
    {
        AssetType preferredType{ AssetType::Unknown };
		unsigned int ActionID{ 0 };
    };

    // Map file extension -> asset type (extensible)
    static AssetType DetectAssetTypeFromPath(const fs::path& p)
    {
        std::string ext = p.extension().string();
        for (auto& c : ext)
        {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }

        // Textures
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga")
            return AssetType::Texture;

        // Future: models/audio/shaders/scripts...
        // if (ext == ".obj" || ext == ".fbx" ... ) return AssetType::Model3D;
        // if (ext == ".wav" || ext == ".ogg" ... ) return AssetType::Audio;
        // if (ext == ".glsl" || ext == ".vert" || ext == ".frag") return AssetType::Shader;

        return AssetType::Unknown;
    }

    static void SDLCALL OnImportDialogClosed(void* userdata, const char* const* filelist, int filter)
    {
		Logger::Instance().log(Logger::Category::AssetManagement, "Import dialog closed callback invoked.", Logger::LogLevel::INFO);
        auto* ctx = static_cast<ImportDialogContext*>(userdata);
        if (!ctx)
        {
            Logger::Instance().log(Logger::Category::AssetManagement, "Import dialog context is null!", Logger::LogLevel::ERROR);
            return;
        }

        if (!filelist || !filelist[0])
        {
            Logger::Instance().log(Logger::Category::AssetManagement, "Import dialog cancelled or no file selected.", Logger::LogLevel::INFO);
			DiagnosticsManager::Instance().updateActionProgress(ctx->ActionID, false);
            delete ctx;
            return;
        }

        const std::string selectedPath = filelist[0];
        Logger::Instance().log(Logger::Category::AssetManagement, "Queueing import job for file: " + selectedPath, Logger::LogLevel::INFO);
        AssetManager::Instance().importAssetFromPath(selectedPath, ctx->preferredType, ctx->ActionID);
        delete ctx;
    }

    static std::string sanitizeName(const std::string& name)
    {
        std::string out;
        out.reserve(name.size());
        for (char c : name)
        {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')
            {
                out.push_back(c);
            }
            else
            {
                out.push_back('_');
            }
        }
        if (out.empty()) out = "Imported";
        return out;
    }
}

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

static fs::path getRegistryPath(const std::string& projectRoot)
{
    return fs::path(projectRoot) / "Config" / "AssetRegistry.bin";
}

static bool readAssetHeaderType(const fs::path& absoluteAssetPath, AssetType& outType)
{
    std::ifstream in(absoluteAssetPath, std::ios::binary);
    if (!in.is_open()) return false;

    uint32_t magic = 0;
    uint32_t version = 0;
    if (!in.read(reinterpret_cast<char*>(&magic), sizeof(magic))) return false;
    if (!in.read(reinterpret_cast<char*>(&version), sizeof(version))) return false;
    if (magic != 0x41535453 || version != 2) return false;

    int32_t typeInt = 0;
    if (!in.read(reinterpret_cast<char*>(&typeInt), sizeof(typeInt))) return false;
    outType = static_cast<AssetType>(typeInt);
    return true;
}

static bool readAssetHeaderName(const fs::path& absoluteAssetPath, std::string& outName)
{
    std::ifstream in(absoluteAssetPath, std::ios::binary);
    if (!in.is_open()) return false;

    uint32_t magic = 0;
    uint32_t version = 0;
    if (!in.read(reinterpret_cast<char*>(&magic), sizeof(magic))) return false;
    if (!in.read(reinterpret_cast<char*>(&version), sizeof(version))) return false;
    if (magic != 0x41535453 || version != 2) return false;

    int32_t typeInt = 0;
    if (!in.read(reinterpret_cast<char*>(&typeInt), sizeof(typeInt))) return false;

    std::string name;
    if (!readString(in, name)) return false;
    outName = std::move(name);
    return true;
}

AssetManager& AssetManager::Instance()
{
    static AssetManager instance;
    return instance;
}

bool AssetManager::loadAssetRegistry(const std::string& projectRoot)
{
    m_registry.clear();
    m_registryByPath.clear();
    m_registryByName.clear();

    const fs::path regPath = getRegistryPath(projectRoot);
    if (!fs::exists(regPath))
    {
        return false;
    }

    std::ifstream in(regPath, std::ios::binary);
    if (!in.is_open())
    {
        return false;
    }

    uint32_t magic = 0;
    uint32_t version = 0;
    if (!in.read(reinterpret_cast<char*>(&magic), sizeof(magic))) return false;
    if (!in.read(reinterpret_cast<char*>(&version), sizeof(version))) return false;

    if (magic != 0x41525247 || version != 1) // 'ARRG'
    {
        return false;
    }

    uint32_t count = 0;
    if (!in.read(reinterpret_cast<char*>(&count), sizeof(count))) return false;

    m_registry.reserve(count);

    for (uint32_t i = 0; i < count; ++i)
    {
        AssetRegistryEntry e;
        int32_t typeInt = 0;
        if (!readString(in, e.name)) return false;
        if (!readString(in, e.path)) return false;
        if (!in.read(reinterpret_cast<char*>(&typeInt), sizeof(typeInt))) return false;
        e.type = static_cast<AssetType>(typeInt);

        registerAssetInRegistry(e);
    }

    return true;
}

bool AssetManager::saveAssetRegistry(const std::string& projectRoot) const
{
    const fs::path regPath = getRegistryPath(projectRoot);

    std::error_code ec;
    fs::create_directories(regPath.parent_path(), ec);

    std::ofstream out(regPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        return false;
    }

    uint32_t magic = 0x41525247; // 'ARRG'
    uint32_t version = 1;
    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));

    uint32_t count = static_cast<uint32_t>(m_registry.size());
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (const auto& e : m_registry)
    {
        writeString(out, e.name);
        writeString(out, e.path);
        int32_t typeInt = static_cast<int32_t>(e.type);
        out.write(reinterpret_cast<const char*>(&typeInt), sizeof(typeInt));
    }

    return out.good();
}

void AssetManager::registerAssetInRegistry(const AssetRegistryEntry& entry)
{
    const size_t idx = m_registry.size();
    m_registry.push_back(entry);

    if (!entry.path.empty())
    {
        m_registryByPath[entry.path] = idx;
    }
    if (!entry.name.empty())
    {
        m_registryByName[entry.name] = idx;
    }
}

bool AssetManager::discoverAssetsAndBuildRegistry(const std::string& projectRoot)
{
    m_registry.clear();
    m_registryByPath.clear();
    m_registryByName.clear();

    fs::path contentRoot = fs::path(projectRoot) / "Content";
    if (!fs::exists(contentRoot))
    {
        return false;
    }

    for (const auto& entry : fs::recursive_directory_iterator(contentRoot))
    {
        if (!entry.is_regular_file())
            continue;

        const fs::path p = entry.path();
        if (p.extension() != ".asset" && p.extension() != ".map")
            continue;

        AssetType type = AssetType::Unknown;
        std::string name;

        if (!readAssetHeaderType(p, type))
            continue;
        if (!readAssetHeaderName(p, name))
            name = p.stem().string();

        AssetRegistryEntry e;
        e.name = name;
        e.type = type;

        // store a path relative to Content
        fs::path rel = fs::relative(p, contentRoot);
        e.path = rel.generic_string();

        registerAssetInRegistry(e);
    }

    return true;
}

bool AssetManager::doesAssetExist(const std::string& pathOrName) const
{
    if (pathOrName.empty())
        return false;

    if (m_registryByPath.find(pathOrName) != m_registryByPath.end())
        return true;

    if (m_registryByName.find(pathOrName) != m_registryByName.end())
        return true;

    return false;
}

const std::vector<AssetRegistryEntry>& AssetManager::getAssetRegistry() const
{
    return m_registry;
}

bool AssetManager::initialize()
{
    startWorker();
	s_nextAssetID = 1;
    return true;
}

void AssetManager::ensureDefaultAssetsCreated()
{
    auto& diagnostics = DiagnosticsManager::Instance();
    auto& logger = Logger::Instance();
    if (!diagnostics.isProjectLoaded() || diagnostics.getProjectInfo().projectPath.empty())
    {
        // No project context => can't create/save project assets.
		logger.log(Logger::Category::AssetManagement, "Cannot ensure default assets: no project loaded.", Logger::LogLevel::ERROR);
        return;
    }

    
    logger.log(Logger::Category::AssetManagement, "Ensuring default assets exist for project...", Logger::LogLevel::INFO);

    const fs::path contentRoot = fs::path(diagnostics.getProjectInfo().projectPath) / "Content";

    const auto ensureOnDisk = [&](const std::string& relPath, AssetType expectedType, const std::shared_ptr<EngineObject>& obj) -> bool
    {
        const fs::path abs = contentRoot / fs::path(relPath);

        bool existsAndOk = false;
        if (fs::exists(abs))
        {
            AssetType headerType{ AssetType::Unknown };
            existsAndOk = readAssetHeaderType(abs, headerType) && headerType == expectedType;
        }

        if (existsAndOk)
        {
            logger.log(Logger::Category::AssetManagement, "Default asset OK: " + relPath, Logger::LogLevel::INFO);
            return true;
        }

        if (!obj)
        {
            logger.log(Logger::Category::AssetManagement, "Default asset missing/invalid and cannot be created: " + relPath, Logger::LogLevel::ERROR);
            return false;
        }

        logger.log(
            Logger::Category::AssetManagement,
            std::string("Default asset ") + (fs::exists(abs) ? "invalid (type mismatch), overwriting: " : "missing, creating: ") + relPath,
            Logger::LogLevel::WARNING);

        obj->setPath(relPath);
        obj->setAssetType(expectedType);
        obj->setIsSaved(false);
		auto id = registerLoadedAsset(obj);
		Asset asset;
		asset.ID = id;
		asset.type = expectedType;
        return saveAsset(asset);
    };

    // 1) wall texture
    const std::string wallTexRel = (fs::path("Textures") / "wall.asset").generic_string();
    {
        auto tex = std::make_shared<Texture>();
        tex->setName("wall");
        // Default debug texture (2x2 RGBA)
        tex->setWidth(2);
        tex->setHeight(2);
        tex->setChannels(4);
        tex->setData({
            255,   0,   0, 255,   0, 255,   0, 255,
              0,   0, 255, 255, 255, 255,   0, 255
        });
        ensureOnDisk(wallTexRel, AssetType::Texture, tex);
    }

    // 2) wall material
    const std::string wallMatRel = (fs::path("Materials") / "wall.asset").generic_string();
    {
		auto mat = std::make_shared<Material>();
        mat->setName("DefaultDebugMaterial");
        mat->setTextureAssetPaths({ wallTexRel });
        ensureOnDisk(wallMatRel, AssetType::Material, mat);
    }

    // 3) default 3D quad model
    const std::string quad3dRel = "default_quad3d.asset";
    {
        const fs::path abs = contentRoot / fs::path(quad3dRel);
        bool existsAndOk = false;
        if (fs::exists(abs))
        {
            AssetType headerType{ AssetType::Unknown };
            existsAndOk = readAssetHeaderType(abs, headerType) && headerType == AssetType::Model3D;
        }

        if (existsAndOk)
        {
            logger.log(Logger::Category::AssetManagement, "Default asset OK: " + quad3dRel, Logger::LogLevel::INFO);
        }
        else
        {
            auto quad = std::make_shared<Object3D>();
            quad->setName("DefaultQuad3D");
				// Material assets/textures are handled by Material; Object3D only stores a runtime Material instance.
            quad->setVertices({
    -0.5f, -0.5f, -0.5f,  0.0f, 0.0f,
     0.5f, -0.5f, -0.5f,  1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
    -0.5f,  0.5f, -0.5f,  0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 0.0f,

    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
     0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
    -0.5f,  0.5f,  0.5f,  0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,

    -0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
    -0.5f,  0.5f,  0.5f,  1.0f, 0.0f,

     0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
     0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 0.0f,

    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  1.0f, 1.0f,
     0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
     0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,

    -0.5f,  0.5f, -0.5f,  0.0f, 1.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
    -0.5f,  0.5f,  0.5f,  0.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,  0.0f, 1.0f
            });
            quad->setIndices({});

            ensureOnDisk(quad3dRel, AssetType::Model3D, quad);
        }
    }

    // NOTE: Levels are ensured/loaded by loadProject(). This function only creates/saves default asset files.

	logger.log(Logger::Category::AssetManagement, "Default assets ensured.", Logger::LogLevel::INFO);
}

void AssetManager::startWorker()
{
    if (m_workerRunning)
        return;

    m_workerStopRequested = false;
    m_workerRunning = true;

    m_worker = std::thread([this]()
    {
        auto& logger = Logger::Instance();
        logger.log(Logger::Category::AssetManagement, "Asset worker thread started.", Logger::LogLevel::INFO);

        for (;;)
        {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(m_jobMutex);
                m_jobCv.wait(lock, [this]() { return m_workerStopRequested || !m_jobs.empty(); });

                if (m_workerStopRequested && m_jobs.empty())
                    break;

                job = std::move(m_jobs.front());
                m_jobs.pop();
            }

            if (job)
            {
                job();
            }
        }

        logger.log(Logger::Category::AssetManagement, "Asset worker thread exiting.", Logger::LogLevel::INFO);
    });
}

void AssetManager::stopWorker()
{
    if (!m_workerRunning)
        return;

    {
        std::lock_guard<std::mutex> lock(m_jobMutex);
        m_workerStopRequested = true;
    }
    m_jobCv.notify_all();

    if (m_worker.joinable())
    {
        m_worker.join();
    }

    m_workerRunning = false;
}

void AssetManager::enqueueJob(std::function<void()> job)
{
    {
        std::lock_guard<std::mutex> lock(m_jobMutex);
        m_jobs.push(std::move(job));
    }
    m_jobCv.notify_one();
}

void AssetManager::collectGarbage()
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_garbageCollector.collect();
}

AssetManager::~AssetManager()
{
    stopWorker();
}

unsigned int AssetManager::registerLoadedAsset(const std::shared_ptr<EngineObject>& object)
{
	std::lock_guard<std::mutex> lock(m_stateMutex);
    for (const auto& [id, obj] : m_loadedAssets)
    {
        if (obj == object)
        {
            return 0;
		}
    }
	auto id = s_nextAssetID;
	m_loadedAssets[id] = object;
	s_nextAssetID++;
    return id;
}

std::shared_ptr<EngineObject> AssetManager::getLoadedAssetByID(unsigned int id) const
{
	auto it = m_loadedAssets.find(id);
	if (it != m_loadedAssets.end())
	{
        return nullptr;
	}
    return it->second;
}

bool AssetManager::saveAllAssets(SyncState syncState)
{
    std::lock_guard<std::mutex> lock(m_stateMutex);

    auto& diagnostics = DiagnosticsManager::Instance();
    auto& logger = Logger::Instance();

    logger.log(Logger::Category::AssetManagement, "saveAllAssets(): begin", Logger::LogLevel::INFO);

    diagnostics.setActionInProgress(DiagnosticsManager::ActionType::SavingAsset, true);

    // ensure we don't keep stale entries
    m_garbageCollector.collect();

    //const auto alive = m_garbageCollector.getAliveResources();

    size_t toSaveCount = 0;
    for (const auto& res : m_loadedAssets)
    {
        if (res.first != 0 && !res.second->getIsSaved())
        {
            ++toSaveCount;
        }
    }

    logger.log(Logger::Category::AssetManagement, "Saving assets... Pending: " + std::to_string(toSaveCount) + ", Tracked: " + std::to_string(m_loadedAssets.size()), Logger::LogLevel::INFO);

    size_t savedCount = 0;
    for (const auto& res : m_loadedAssets)
    {
        if (res.second->getIsSaved())
        {
            continue;
        }
		Asset asset;
        asset.ID = res.first;
        asset.type = res.second->getAssetType();
        if (!saveAsset(asset))
        {
            logger.log(Logger::Category::AssetManagement, "Failed to auto-save asset: " + res.second->getName(), Logger::LogLevel::ERROR);
            diagnostics.setActionInProgress(DiagnosticsManager::ActionType::SavingAsset, false);
            return false;
        }

        ++savedCount;
    }

	auto level = diagnostics.getActiveLevel();
	if (level)
	{
		logger.log(Logger::Category::AssetManagement, "Saving active level...", Logger::LogLevel::INFO);
        Asset asset;

		if (!saveLevelAsset(level).success)
		{
			logger.log(Logger::Category::AssetManagement, "Failed to save active level: " + level->getName(), Logger::LogLevel::ERROR);
			diagnostics.setActionInProgress(DiagnosticsManager::ActionType::SavingAsset, false);
			return false;
		}
	}

    logger.log(
        Logger::Category::AssetManagement,
        "All unsaved assets have been saved. Saved: " + std::to_string(savedCount) + "/" + std::to_string(toSaveCount),
        Logger::LogLevel::INFO);

    diagnostics.setActionInProgress(DiagnosticsManager::ActionType::SavingAsset, false);
    logger.log(Logger::Category::AssetManagement, "saveAllAssets(): end", Logger::LogLevel::INFO);
    return true;
}

std::string AssetManager::getAbsoluteContentPath(const std::string& relativeToContent) const
{
    const auto& diagnostics = DiagnosticsManager::Instance();
    if (!diagnostics.isProjectLoaded() || diagnostics.getProjectInfo().projectPath.empty())
    {
        return {};
    }

    fs::path p = fs::path(diagnostics.getProjectInfo().projectPath) / "Content" / fs::path(relativeToContent);
    return p.lexically_normal().string();
}

int AssetManager::loadAsset(const std::string& path, AssetType type, SyncState syncState = Sync)
{
	LoadResult result;
    switch (type)
    {
    case AssetType::Model3D:
        result = loadObject3DAsset(path);
        break;
    case AssetType::Model2D:
        result = loadObject2DAsset(path);
		break;
	case AssetType::Texture:
		result = loadTextureAsset(path);
        break;
	case AssetType::Material:
        result = loadMaterialAsset(path);
		break;
	case AssetType::Level:
        result = loadLevelAsset(path);
		break;
    }

    return 0;
}

bool AssetManager::saveAsset(const Asset& asset, SyncState syncState, DiagnosticsManager::Action action)
{
	auto& diagnostics = DiagnosticsManager::Instance();
    if (asset.ID == 0)
    {
        Logger::Instance().log(Logger::Category::AssetManagement, "saveAsset(): dummy save for asset with ID 0.");
        return true;
    }
    auto object = getLoadedAssetByID(asset.ID);
    if (!object)
    {
        Logger::Instance().log(
            Logger::Category::AssetManagement,
            "saveAsset(): Asset with ID " + std::to_string(asset.ID) + " not found among loaded assets.",
            Logger::LogLevel::ERROR);
        return false;
    }
        auto action = diagnostics.registerAction(DiagnosticsManager::ActionType::SavingAsset);
        SaveResult result;
        switch (asset.type)
        {
        case AssetType::Model2D:
            if (syncState == Async)
            {
                enqueueJob([this, asset]()
                    {
						auto object = getLoadedAssetByID(asset.ID);
                        saveObject2DAsset(std::dynamic_pointer_cast<Object2D>(object));
                    });
                return true;
            }
            result = saveObject2DAsset(std::dynamic_pointer_cast<Object2D>(object));
            break;
        case AssetType::Model3D:
            result = saveObject3DAsset(std::dynamic_pointer_cast<Object3D>(object));
            break;
        case AssetType::Texture:
            result = saveTextureAsset(std::dynamic_pointer_cast<Texture>(object));
            break;
        case AssetType::Material:
            result = saveMaterialAsset(std::dynamic_pointer_cast<Material>(object));
            break;
        case AssetType::Audio:
        case AssetType::Script:
        case AssetType::Shader:
        case AssetType::Unknown:
        default:
            Logger::Instance().log(
                Logger::Category::AssetManagement,
                "saveAsset(): Unsupported asset type for asset: " + object->getName(),
                Logger::LogLevel::ERROR);
            return false;
        }
        if (!result.success)
        {
            Logger::Instance().log(
                Logger::Category::AssetManagement,
                "saveAsset(): Failed to save asset: " + object->getName() + ". Error: " + result.errorMessage,
                Logger::LogLevel::ERROR);
            return false;
        }
		diagnostics.updateActionProgress(action.ID, false);
        return result.success;
    }

static std::string normalizeAssetRelPath(AssetType type, std::string relPath)
{
    fs::path p(relPath);

    // Force folder conventions for certain asset types
    if (type == AssetType::Material)
    {
        // Always store materials under "Materials/"
        if (p.has_parent_path())
        {
            // if user already wrote Materials/, keep it
            auto first = *p.begin();
            if (first != "Materials")
            {
                p = fs::path("Materials") / p.filename();
            }
        }
        else
        {
            p = fs::path("Materials") / p;
        }

        if (p.extension() != ".asset")
        {
            p.replace_extension(".asset");
        }
    }

    return p.generic_string();
}

int AssetManager::createAsset(AssetType type, const std::string& path, const std::string& name, const std::string& sourcePath, SyncState syncState)
{
    auto& logger = Logger::Instance();
    logger.log(Logger::Category::AssetManagement, "Creating asset with name: " + name + " at " + path, Logger::LogLevel::INFO);

    return -1;
}

bool AssetManager::OpenImportDialog(SDL_Window* parentWindow /* = nullptr */, AssetType forcedType /* = AssetType::Unknown */, SyncState syncState /* = Sync */)
{
    auto& logger = Logger::Instance();
    auto& diagnostics = DiagnosticsManager::Instance();
	auto action = diagnostics.registerAction(DiagnosticsManager::ActionType::ImportingAsset);
    if (parentWindow)
    {
        ImportDialogContext* ctx = new ImportDialogContext();
        ctx->preferredType = forcedType;
		ctx->ActionID = action.ID;

        // SDL3: Filter als SDL_DialogFileFilter-Array
        SDL_DialogFileFilter filters[] = {
            { "All Files", "*" },
            { "Image Files", "png;jpg;jpeg;bmp;tga" }
        };

        logger.log(Logger::Category::AssetManagement, "Opening import asset dialog with SDL...", Logger::LogLevel::INFO);

        // Korrekte SDL3-Funktion verwenden
        SDL_ShowOpenFileDialog(
            OnImportDialogClosed, // Callback (bereits im File definiert)
            ctx,                  // Userdata
            parentWindow,         // Window
            filters,              // Filter-Array
            SDL_arraysize(filters), // Anzahl Filter
            nullptr,              // Default Location
            false                 // allow_many: nur eine Datei
        );
        return true;
    }
    logger.log(Logger::Category::AssetManagement, "Opening import asset dialog...", Logger::LogLevel::INFO);
    return false;
}

void AssetManager::importAssetFromPath(std::string path, AssetType preferredType, unsigned int ActionID)
{
    auto& logger = Logger::Instance();
    auto& diagnostics = DiagnosticsManager::Instance();
    logger.log(Logger::Category::AssetManagement, "Importing asset from path: " + path, Logger::LogLevel::INFO);
    if (!fs::exists(path))
    {
        logger.log(Logger::Category::AssetManagement, "Import asset failed: file does not exist: " + path, Logger::LogLevel::ERROR);
		diagnostics.updateActionProgress(ActionID, false);
		return;
    }
    std::ifstream inFile;
	inFile.open(path);
    if (!inFile.is_open())
    {
        logger.log(Logger::Category::AssetManagement, "Import asset failed: could not open file: " + path, Logger::LogLevel::ERROR);
        diagnostics.updateActionProgress(ActionID, false);
        return;
    }
}

bool AssetManager::loadProject(const std::string& projectPath, SyncState syncState)
{
    auto& logger = Logger::Instance();
    auto& diagnostics = DiagnosticsManager::Instance();
    diagnostics.setActionInProgress(DiagnosticsManager::ActionType::LoadingProject, true);

    logger.log(Logger::Category::Project, "Loading project: " + projectPath, Logger::LogLevel::INFO);

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

    // Project file fully parsed - apply it.
    diagnostics.setProjectInfo(info);

    // New project/level context: force re-prepare of renderer resources.
    diagnostics.setScenePrepared(false);

    // Ensure default assets exist, but do not create/override the active level here.
    ensureDefaultAssetsCreated();

    // Registry handling: build at the very end, so it includes any ensured default assets.
	logger.log(Logger::Category::Project, "Building asset registry for project: " + info.projectName, Logger::LogLevel::INFO);
    {
        const bool registryLoaded = loadAssetRegistry(info.projectPath);
        if (!registryLoaded)
        {
            logger.log(Logger::Category::AssetManagement, "Asset registry missing/invalid; will rebuild.", Logger::LogLevel::INFO);
        }

        if (!discoverAssetsAndBuildRegistry(info.projectPath))
        {
            logger.log(Logger::Category::AssetManagement, "Asset discovery failed.", Logger::LogLevel::ERROR);
        }

        if (!saveAssetRegistry(info.projectPath))
        {
            logger.log(Logger::Category::AssetManagement, "Failed to save asset registry.", Logger::LogLevel::WARNING);
        }
    }
	logger.log(Logger::Category::AssetManagement, "Asset registry ready. Total assets: " + std::to_string(m_registry.size()), Logger::LogLevel::INFO);

    if (!diagnostics.loadProjectConfig())
    {
        logger.log("Failed to load project config.", Logger::LogLevel::ERROR);
    }

    diagnostics.setActionInProgress(DiagnosticsManager::ActionType::LoadingProject, false);
    logger.log(Logger::Category::Project, "Project loaded: " + info.projectName, Logger::LogLevel::INFO);
    return true;
}

bool AssetManager::saveProject(const std::string& projectPath, SyncState syncState)
{
    auto& logger = Logger::Instance();
    auto& diagnostics = DiagnosticsManager::Instance();
    diagnostics.setActionInProgress(DiagnosticsManager::ActionType::SavingProject, true);

    logger.log(Logger::Category::Project, "Saving project: " + projectPath, Logger::LogLevel::INFO);

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

    std::string levelPathToSave;
    if (diagnostics.getActiveLevel())
    {
        levelPathToSave = fs::path(diagnostics.getActiveLevel()->getPath()).generic_string();
    }

    out << "Name=" << info.projectName << "\n";
    out << "Version=" << info.projectVersion << "\n";
    out << "EngineVersion=" << info.engineVersion << "\n";
    out << "Path=" << info.projectPath << "\n";
    out << "RHI=" << DiagnosticsManager::rhiTypeToString(info.selectedRHI) << "\n";
    out << "Level=" << levelPathToSave << "\n";

    diagnostics.setActionInProgress(DiagnosticsManager::ActionType::SavingProject, false);
    logger.log(Logger::Category::Project, "Project saved: " + projectFile.string(), Logger::LogLevel::INFO);
    return true;
}

bool AssetManager::createProject(const std::string& parentDir, const std::string& projectName, const DiagnosticsManager::ProjectInfo& info, SyncState syncState)
{
    auto& logger = Logger::Instance();
    auto& diagnostics = DiagnosticsManager::Instance();
    diagnostics.setActionInProgress(DiagnosticsManager::ActionType::LoadingProject, true);

    logger.log(Logger::Category::Project, "Creating project: " + projectName + " in " + parentDir, Logger::LogLevel::INFO);

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

    const std::string defaultLevelPath = (fs::path("Levels") / "DefaultLevel.map").generic_string();
    auto defaultlevel = std::make_unique<EngineLevel>();
    diagnostics.setActiveLevel(std::move(defaultlevel));

    // ensure defaults.ini exists for the new project
    diagnostics.loadProjectConfig();

    if (auto level = diagnostics.getActiveLevel())
    {
        saveLevelAsset(level);
    }

    bool saved = saveProject(root.string());
    diagnostics.setActionInProgress(DiagnosticsManager::ActionType::LoadingProject, false);
    if (saved)
    {
        logger.log("Project created at: " + root.string(), Logger::LogLevel::INFO);
    }
	bool loaded = loadProject(root.string());
    if (!loaded)
    {
        logger.log("Failed to load newly created project: " + root.string(), Logger::LogLevel::ERROR);
        return false;
	}
    return loaded;
}

AssetManager::LoadResult AssetManager::loadTextureAsset(const std::string& path)
{
    return LoadResult();
}
