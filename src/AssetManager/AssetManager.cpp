#include "AssetManager.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <filesystem>
#include <fstream>
#include <cstdint>
#include "AssetTypes.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>

#include "MaterialAsset.h"

namespace fs = std::filesystem;

namespace
{
    struct ImportDialogContext
    {
        AssetType preferredType{ AssetType::Texture };
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

    static void SDLCALL OnImportDialogClosed(void* userdata, const char* const* filelist, int /*filter_index*/)
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
            delete ctx;
            return;
        }

        const std::string selectedPath = filelist[0];
        Logger::Instance().log(Logger::Category::AssetManagement, "Queueing import job for file: " + selectedPath, Logger::LogLevel::INFO);
        AssetManager::Instance().importAssetFromFileAsync(ctx->preferredType, selectedPath);
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

    // Create in-memory default assets immediately. Persisting them (and optionally importing wall.jpg)
    // is handled later when a project is loaded.
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (!m_defaultTriangle2D)
        {
            auto tex = std::make_shared<Texture>();
            tex->setName("DefaultDebugTexture");
            tex->setPath((fs::path("Textures") / "default_debug.asset").generic_string());
            tex->setAssetType(AssetType::Texture);
            tex->setIsSaved(false);
            tex->setWidth(2);
            tex->setHeight(2);
            tex->setChannels(4);
            tex->setData({
                255,   0,   0, 255,   0, 255,   0, 255,
                  0,   0, 255, 255, 255, 255,   0, 255
            });
            m_garbageCollector.registerResource(tex);

            auto mat = std::make_shared<MaterialAsset>();
            mat->setName("DefaultDebugMaterial");
            mat->setPath((fs::path("Materials") / "default_debug.asset").generic_string());
            mat->setAssetType(AssetType::Material);
            mat->setIsSaved(false);
            mat->setTextureAssetPaths({ tex->getPath() });
            m_garbageCollector.registerResource(mat);

            auto quad = std::make_shared<Object2D>();
            quad->setName("DefaultQuad2D");
            quad->setPath("default_quad2d.asset");
            quad->setAssetType(AssetType::Model2D);
            quad->setIsSaved(false);
            quad->setMaterialAssetPath(mat->getPath());
            quad->setLoadedMaterialAsset(mat);
            quad->setVertices({
                // positions             // colors            // texcoords
                -0.5f,  0.5f, 0.0f,      1.0f, 0.0f, 0.0f,     0.0f, 1.0f,
                 0.5f,  0.5f, 0.0f,      0.0f, 1.0f, 0.0f,     1.0f, 1.0f,
                 0.5f, -0.5f, 0.0f,      0.0f, 0.0f, 1.0f,     1.0f, 0.0f,
                -0.5f, -0.5f, 0.0f,      1.0f, 1.0f, 0.0f,     0.0f, 0.0f
            });
            quad->setIndices({ 0, 1, 2, 2, 3, 0 });
            m_garbageCollector.registerResource(quad);

            m_defaultTriangle2D = quad;
        }
    }

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
        return saveAsset(obj);
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
        auto mat = std::make_shared<MaterialAsset>();
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
            quad->setMaterialAssetPath(wallMatRel);
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

void AssetManager::pump()
{
    // currently no main-thread-only work is required.
}

void AssetManager::importAssetFromFileAsync(AssetType type, std::string sourceFilePath)
{
    auto& diagnostics = DiagnosticsManager::Instance();
    diagnostics.setActionInProgress(DiagnosticsManager::ActionType::LoadingAsset, true);

    enqueueJob([this, type, source = std::move(sourceFilePath)]()
    {
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            importAssetFromFile(type, source);
        }

        auto& diagnosticsLocal = DiagnosticsManager::Instance();
        diagnosticsLocal.setActionInProgress(DiagnosticsManager::ActionType::LoadingAsset, false);
    });
}

void AssetManager::saveAllAssetsAsync()
{
    auto& diagnostics = DiagnosticsManager::Instance();
    diagnostics.setActionInProgress(DiagnosticsManager::ActionType::SavingAsset, true);

    enqueueJob([this]()
    {
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            saveAllAssets();
        }

        auto& diagnosticsLocal = DiagnosticsManager::Instance();
        diagnosticsLocal.setActionInProgress(DiagnosticsManager::ActionType::SavingAsset, false);
    });
}

AssetManager::~AssetManager()
{
    stopWorker();
}

bool AssetManager::saveAllAssets()
{
    std::lock_guard<std::mutex> lock(m_stateMutex);

    auto& diagnostics = DiagnosticsManager::Instance();
    auto& logger = Logger::Instance();

    logger.log(Logger::Category::AssetManagement, "saveAllAssets(): begin", Logger::LogLevel::INFO);

    diagnostics.setActionInProgress(DiagnosticsManager::ActionType::SavingAsset, true);

    // ensure we don't keep stale entries
    m_garbageCollector.collect();

    const auto alive = m_garbageCollector.getAliveResources();

    size_t toSaveCount = 0;
    for (const auto& res : alive)
    {
        if (res && !res->getIsSaved())
        {
            ++toSaveCount;
        }
    }

    logger.log(Logger::Category::AssetManagement, "Saving assets... Pending: " + std::to_string(toSaveCount) + ", Tracked: " + std::to_string(alive.size()), Logger::LogLevel::INFO);

    size_t savedCount = 0;
    for (const auto& res : alive)
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
            logger.log(Logger::Category::AssetManagement, "Failed to auto-save asset: " + res->getName(), Logger::LogLevel::ERROR);
            diagnostics.setActionInProgress(DiagnosticsManager::ActionType::SavingAsset, false);
            return false;
        }

        ++savedCount;
    }

	auto level = diagnostics.getActiveLevel();
	if (level)
	{
		logger.log(Logger::Category::AssetManagement, "Saving active level...", Logger::LogLevel::INFO);
		if (!saveAsset(std::shared_ptr<EngineObject>(level, [](EngineObject*) {})))
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

std::shared_ptr<EngineObject> AssetManager::loadAssetObject(const std::string& path)
{
    // Fast path: already loaded
    {
		Logger::Instance().log(Logger::Category::AssetManagement, "loadAssetObject(): checking for already loaded asset: " + path, Logger::LogLevel::INFO);
        std::lock_guard<std::mutex> lock(m_stateMutex);
        for (const auto& alive : m_garbageCollector.getAliveResources())
        {
            if (alive && fs::path(alive->getPath()).generic_string() == fs::path(path).generic_string())
            {
                return alive;
            }
        }
    }
	Logger::Instance().log(Logger::Category::AssetManagement, "loadAssetObject(): asset not already loaded, proceeding to load: " + path, Logger::LogLevel::INFO);
	auto object = loadAsset(path);
    if (!object)
    {
		Logger::Instance().log(Logger::Category::AssetManagement, "loadAssetObject(): loadAsset() failed for path: " + path, Logger::LogLevel::ERROR);
        return nullptr;
    }
    return object;
}

std::shared_ptr<MaterialAsset> AssetManager::loadMaterialAsset(const std::string& materialAssetPath)
{
    if (materialAssetPath.empty())
    {
        return nullptr;
    }

    auto obj = loadAssetObject(materialAssetPath);
    return std::dynamic_pointer_cast<MaterialAsset>(obj);
}

std::vector<std::shared_ptr<Texture>> AssetManager::loadTexturesForMaterial(const MaterialAsset& material)
{
    std::vector<std::shared_ptr<Texture>> out;

    for (const auto& texPath : material.getTextureAssetPaths())
    {
        if (texPath.empty())
            continue;

        auto obj = loadAssetObject(texPath);
        if (auto tex = std::dynamic_pointer_cast<Texture>(obj))
        {
            out.push_back(std::move(tex));
        }
    }

    return out;
}

std::shared_ptr<EngineObject> AssetManager::loadAsset(const std::string& path)
{
     auto& logger = Logger::Instance();
     logger.log(Logger::Category::AssetManagement, "Loading asset: " + path, Logger::LogLevel::INFO);

     auto logReadFail = [&](const char* what)
     {
         logger.log(Logger::Category::AssetManagement, std::string("Asset load failed while reading ") + what + " (asset: " + path + ")", Logger::LogLevel::ERROR);
     };

     auto& diagnostics = DiagnosticsManager::Instance();
     bool loaded = diagnostics.isProjectLoaded();
     if (!loaded || diagnostics.getProjectInfo().projectPath.empty())
     {
         logger.log(Logger::Category::AssetManagement, "Asset load failed (no project loaded)", Logger::LogLevel::ERROR);
         return nullptr;
     }

    fs::path contentRoot = fs::path(diagnostics.getProjectInfo().projectPath) / "Content";
    logger.log(Logger::Category::AssetManagement, "Content root: " + contentRoot.lexically_normal().string(), Logger::LogLevel::INFO);

    // 'path' is expected to be the relative path inside Content.
    fs::path relPath = fs::path(path);
    if (relPath.is_absolute())
    {
        logger.log(Logger::Category::AssetManagement, "Asset load failed (path must be relative to Content): " + relPath.string(), Logger::LogLevel::ERROR);
        return nullptr;
    }
    logger.log(Logger::Category::AssetManagement, "Relative asset path: " + relPath.generic_string(), Logger::LogLevel::INFO);

    fs::path absolutePath = contentRoot / relPath;
    logger.log(Logger::Category::AssetManagement, "Absolute asset path: " + absolutePath.lexically_normal().string(), Logger::LogLevel::INFO);
    if (!fs::exists(absolutePath))
    {
        logger.log(Logger::Category::AssetManagement, "Asset load failed (missing file): " + absolutePath.string(), Logger::LogLevel::ERROR);
        return nullptr;
    }

    std::ifstream in(absolutePath, std::ios::binary);
    if (!in.is_open())
    {
        logger.log(Logger::Category::AssetManagement, "Failed to open asset file: " + absolutePath.string(), Logger::LogLevel::ERROR);
        return nullptr;
    }

    // Avoid re-loading the same asset while we parse (reduces recursive loops)
    const std::string requestedKey = relPath.generic_string();
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        for (const auto& alive : m_garbageCollector.getAliveResources())
        {
            if (alive && fs::path(alive->getPath()).generic_string() == requestedKey)
            {
                logger.log(Logger::Category::AssetManagement, "Asset already loaded: " + requestedKey, Logger::LogLevel::INFO);
                return alive;
            }
        }
    }

    uint32_t magic = 0;
    uint32_t version = 0;
    if (!in.read(reinterpret_cast<char*>(&magic), sizeof(magic))) { logReadFail("magic"); return nullptr; }
    if (!in.read(reinterpret_cast<char*>(&version), sizeof(version))) { logReadFail("version"); return nullptr; }
    logger.log(Logger::Category::AssetManagement, "Asset header: magic=0x" + std::to_string(magic) + ", version=" + std::to_string(version), Logger::LogLevel::INFO);
    if (magic != 0x41535453 || version != 2) // 'ASTS'
    {
        logger.log(Logger::Category::AssetManagement, "Invalid asset file format (expected magic=0x41535453, version=2)", Logger::LogLevel::ERROR);
        return nullptr;
    }

    int32_t typeInt = 0;
    if (!in.read(reinterpret_cast<char*>(&typeInt), sizeof(typeInt))) { logReadFail("type"); return nullptr; }
    AssetType type = static_cast<AssetType>(typeInt);
    logger.log(Logger::Category::AssetManagement, "Asset type int: " + std::to_string(typeInt), Logger::LogLevel::INFO);

    std::string name;
    std::string storedRelPath;
    std::string materialAssetPath;
    if (!readString(in, name)) { logReadFail("name"); return nullptr; }
    if (!readString(in, storedRelPath)) { logReadFail("storedRelPath"); return nullptr; }
    if (!readString(in, materialAssetPath)) { logReadFail("materialAssetPath"); return nullptr; }

    logger.log(Logger::Category::AssetManagement,
        "Asset meta: name='" + name + "', storedRelPath='" + storedRelPath + "', materialAssetPath='" + materialAssetPath + "'",
        Logger::LogLevel::INFO);

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
        obj = std::make_shared<Texture>();
        break;
    case AssetType::Material:
        obj = std::make_shared<MaterialAsset>();
        break;
    case AssetType::Level:
        obj = std::make_shared<EngineLevel>();
        break;
    default:
        obj = std::make_shared<EngineObject>();
        break;
    }

    obj->setName(name);
    // Use storedRelPath as canonical key if it exists; otherwise use requested path.
    const std::string canonicalKey = fs::path(storedRelPath.empty() ? requestedKey : storedRelPath).generic_string();
    obj->setPath(canonicalKey);
    obj->setAssetType(type);
    obj->setIsSaved(true);

    std::vector<std::string> levelDepPaths;

    if (type == AssetType::Texture)
    {
        auto tex = std::static_pointer_cast<Texture>(obj);
        int32_t w = 0, h = 0, c = 0;
        uint32_t dataSize = 0;
        if (!in.read(reinterpret_cast<char*>(&w), sizeof(w))) { logReadFail("texture width"); return nullptr; }
        if (!in.read(reinterpret_cast<char*>(&h), sizeof(h))) { logReadFail("texture height"); return nullptr; }
        if (!in.read(reinterpret_cast<char*>(&c), sizeof(c))) { logReadFail("texture channels"); return nullptr; }
        if (!in.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize))) { logReadFail("texture dataSize"); return nullptr; }

        logger.log(Logger::Category::AssetManagement,
            "Texture payload: w=" + std::to_string(w) + ", h=" + std::to_string(h) + ", c=" + std::to_string(c) + ", dataSize=" + std::to_string(dataSize),
            Logger::LogLevel::INFO);

        std::vector<unsigned char> data;
        data.resize(dataSize);
        if (dataSize > 0)
        {
            if (!in.read(reinterpret_cast<char*>(data.data()), dataSize)) { logReadFail("texture data blob"); return nullptr; }
        }

        tex->setWidth(w);
        tex->setHeight(h);
        tex->setChannels(c);
        tex->setData(std::move(data));
    }

    if (type == AssetType::Model2D)
    {
        auto obj2d = std::static_pointer_cast<Object2D>(obj);
        obj2d->setMaterialAssetPath(materialAssetPath);

        uint32_t vertCount = 0;
        if (!in.read(reinterpret_cast<char*>(&vertCount), sizeof(vertCount))) { logReadFail("model2d vertCount"); return nullptr; }
        logger.log(Logger::Category::AssetManagement, "Model2D payload: vertCount=" + std::to_string(vertCount), Logger::LogLevel::INFO);
        std::vector<float> verts(vertCount);
        if (vertCount > 0)
        {
            if (!in.read(reinterpret_cast<char*>(verts.data()), vertCount * sizeof(float))) { logReadFail("model2d verts"); return nullptr; }
        }
        obj2d->setVertices(verts);

        uint32_t indexCount = 0;
        if (!in.read(reinterpret_cast<char*>(&indexCount), sizeof(indexCount))) { logReadFail("model2d indexCount"); return nullptr; }
        logger.log(Logger::Category::AssetManagement, "Model2D payload: indexCount=" + std::to_string(indexCount), Logger::LogLevel::INFO);
        std::vector<uint32_t> indices(indexCount);
        if (indexCount > 0)
        {
            if (!in.read(reinterpret_cast<char*>(indices.data()), indexCount * sizeof(uint32_t))) { logReadFail("model2d indices"); return nullptr; }
        }
        obj2d->setIndices(indices);

        // Auto-load material + textures via AssetManager
        if (!materialAssetPath.empty())
        {
            logger.log(Logger::Category::AssetManagement, "Model2D: loading material asset '" + materialAssetPath + "'", Logger::LogLevel::INFO);
            if (auto matAsset = loadMaterialAsset(materialAssetPath))
            {
                logger.log(Logger::Category::AssetManagement, "Model2D: material loaded, loading textures...", Logger::LogLevel::INFO);
                auto textures = loadTexturesForMaterial(*matAsset);
                obj2d->setLoadedMaterialAsset(matAsset);
                obj2d->setLoadedTextures(textures);
            }
            else
            {
                logger.log(Logger::Category::AssetManagement, "Model2D: failed to load material asset '" + materialAssetPath + "'", Logger::LogLevel::WARNING);
            }
        }
    }

    if (type == AssetType::Model3D)
    {
        auto obj3d = std::static_pointer_cast<Object3D>(obj);
        obj3d->setMaterialAssetPath(materialAssetPath);

        uint32_t vertCount = 0;
        if (!in.read(reinterpret_cast<char*>(&vertCount), sizeof(vertCount))) { logReadFail("model3d vertCount"); return nullptr; }
        logger.log(Logger::Category::AssetManagement, "Model3D payload: vertCount=" + std::to_string(vertCount), Logger::LogLevel::INFO);
        std::vector<float> verts(vertCount);
        if (vertCount > 0)
        {
            if (!in.read(reinterpret_cast<char*>(verts.data()), vertCount * sizeof(float))) { logReadFail("model3d verts"); return nullptr; }
        }
        obj3d->setVertices(verts);

        uint32_t indexCount = 0;
        if (!in.read(reinterpret_cast<char*>(&indexCount), sizeof(indexCount))) { logReadFail("model3d indexCount"); return nullptr; }
        logger.log(Logger::Category::AssetManagement, "Model3D payload: indexCount=" + std::to_string(indexCount), Logger::LogLevel::INFO);
        std::vector<uint32_t> indices(indexCount);
        if (indexCount > 0)
        {
            if (!in.read(reinterpret_cast<char*>(indices.data()), indexCount * sizeof(uint32_t))) { logReadFail("model3d indices"); return nullptr; }
        }
        obj3d->setIndices(indices);

        // Auto-load material + textures via AssetManager
        if (!materialAssetPath.empty())
        {
            logger.log(Logger::Category::AssetManagement, "Model3D: loading material asset '" + materialAssetPath + "'", Logger::LogLevel::INFO);
            if (auto matAsset = loadMaterialAsset(materialAssetPath))
            {
                logger.log(Logger::Category::AssetManagement, "Model3D: material loaded, loading textures...", Logger::LogLevel::INFO);
                auto textures = loadTexturesForMaterial(*matAsset);
                obj3d->setLoadedMaterialAsset(matAsset);
                obj3d->setLoadedTextures(textures);
            }
            else
            {
                logger.log(Logger::Category::AssetManagement, "Model3D: failed to load material asset '" + materialAssetPath + "'", Logger::LogLevel::WARNING);
            }
        }
    }

    if (type == AssetType::Level)
    {
        auto level = std::static_pointer_cast<EngineLevel>(obj);
        level->clearLoadedDependencies();

        uint32_t depCount = 0;
        if (!in.read(reinterpret_cast<char*>(&depCount), sizeof(depCount))) { logReadFail("level depCount"); return nullptr; }
        logger.log(Logger::Category::AssetManagement, "Level payload: depCount=" + std::to_string(depCount), Logger::LogLevel::INFO);
        for (uint32_t i = 0; i < depCount; ++i)
        {
            std::string objPath;
            if (!readString(in, objPath)) { logReadFail("level object path"); return nullptr; }

            logger.log(Logger::Category::AssetManagement, "Level dep[" + std::to_string(i) + "]: path='" + objPath + "'", Logger::LogLevel::INFO);

            fs::path p(objPath);
            if (p.is_absolute())
            {
                logger.log("Invalid asset path (should be relative to Content): " + objPath, Logger::LogLevel::ERROR);
                return nullptr;
            }

            Vec3 pos{}, rot{}, scl{};
            if (!in.read(reinterpret_cast<char*>(&pos), sizeof(pos))) { logReadFail("level object position"); return nullptr; }
            if (!in.read(reinterpret_cast<char*>(&rot), sizeof(rot))) { logReadFail("level object rotation"); return nullptr; }
            if (!in.read(reinterpret_cast<char*>(&scl), sizeof(scl))) { logReadFail("level object scale"); return nullptr; }

            Transform t;
            t.setPosition(pos);
            t.setRotation(rot);
            t.setScale(scl);

            if (!p.empty())
            {
                const std::string depRelPath = p.generic_string();
                logger.log(Logger::Category::AssetManagement, "Level: loading object asset '" + depRelPath + "'", Logger::LogLevel::INFO);

                auto depObj = loadAssetObject(depRelPath);
                if (depObj)
                {
                    level->registerObject(depObj);
                    level->setObjectTransform(depRelPath, t);
                    level->setLoadedDependency(depRelPath, depObj);
                    logger.log(Logger::Category::AssetManagement, "Level: object attached '" + depRelPath + "'", Logger::LogLevel::INFO);
                }
                else
                {
                    logger.log(Logger::Category::AssetManagement, "Level: object failed to load '" + depRelPath + "'", Logger::LogLevel::WARNING);
                    // still keep transform so it survives if asset appears later
                    level->setObjectTransform(depRelPath, t);
                }
            }
        }
    }

    if (type == AssetType::Material)
    {
        auto mat = std::static_pointer_cast<MaterialAsset>(obj);

        uint32_t texRefCount = 0;
        if (!in.read(reinterpret_cast<char*>(&texRefCount), sizeof(texRefCount))) { logReadFail("material texRefCount"); return nullptr; }
        logger.log(Logger::Category::AssetManagement, "Material payload: texRefCount=" + std::to_string(texRefCount), Logger::LogLevel::INFO);

        std::vector<std::string> refs;
        refs.reserve(texRefCount);

        for (uint32_t i = 0; i < texRefCount; ++i)
        {
            std::string ref;
            if (!readString(in, ref)) { logReadFail("material texture ref"); return nullptr; }
            logger.log(Logger::Category::AssetManagement, "Material texRef[" + std::to_string(i) + "]: '" + ref + "'", Logger::LogLevel::INFO);
            refs.push_back(std::move(ref));
        }

        mat->setTextureAssetPaths(std::move(refs));
    }

    // Register main object in GC before resolving dependencies
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_garbageCollector.registerResource(obj);
    }

    // Level dependencies are resolved inline during parsing.

    logger.log(Logger::Category::AssetManagement, "Asset loaded: " + name + " (" + obj->getPath() + ")", Logger::LogLevel::INFO);
    return obj;
}

bool AssetManager::saveAsset(const std::shared_ptr<EngineObject>& object)
{
    std::lock_guard<std::mutex> lock(m_stateMutex);

    auto& logger = Logger::Instance();
    if (!object)
    {
        logger.log(Logger::Category::AssetManagement, "saveAsset failed: object is null", Logger::LogLevel::ERROR);
        return false;
    }

    const auto& diagnostics = DiagnosticsManager::Instance();
    const auto& projInfo = diagnostics.getProjectInfo();
    if (projInfo.projectPath.empty())
    {
        logger.log(Logger::Category::AssetManagement, "saveAsset failed: no project loaded (projectPath is empty)", Logger::LogLevel::ERROR);
        return false;
    }

    // The object stores only a relative path inside the Content folder.
    fs::path relAssetPath = fs::path(object->getPath());
    if (relAssetPath.is_absolute())
    {
        logger.log(Logger::Category::AssetManagement, "saveAsset failed: object path must be relative to Content: " + relAssetPath.string(), Logger::LogLevel::ERROR);
        return false;
    }

    fs::path contentRoot = fs::path(projInfo.projectPath) / "Content";
    std::error_code ec;
    fs::create_directories(contentRoot, ec);

    fs::path finalAssetPath = contentRoot / relAssetPath;
    fs::create_directories(finalAssetPath.parent_path(), ec);

    logger.log(Logger::Category::AssetManagement, "Saving asset: " + object->getName() + " at " + finalAssetPath.string(), Logger::LogLevel::INFO);

    std::ofstream out(finalAssetPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        logger.log(Logger::Category::AssetManagement, "Failed to open asset file for writing: " + finalAssetPath.string(), Logger::LogLevel::FATAL);
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
    case AssetType::Texture:
    {
        // keep string slot for format compatibility
        writeString(out, materialAssetPath);

        auto tex = std::dynamic_pointer_cast<Texture>(object);
        int32_t w = 0, h = 0, c = 0;
        uint32_t dataSize = 0;
        const unsigned char* dataPtr = nullptr;

        if (tex)
        {
            w = tex->getWidth();
            h = tex->getHeight();
            c = tex->getChannels();
            const auto& data = tex->getData();
            dataSize = static_cast<uint32_t>(data.size());
            dataPtr = data.data();
        }

        out.write(reinterpret_cast<const char*>(&w), sizeof(w));
        out.write(reinterpret_cast<const char*>(&h), sizeof(h));
        out.write(reinterpret_cast<const char*>(&c), sizeof(c));
        out.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
        if (dataSize > 0 && dataPtr)
        {
            out.write(reinterpret_cast<const char*>(dataPtr), dataSize);
        }
        break;
    }
    case AssetType::Material:
    {
        // keep string slot for format compatibility
        writeString(out, materialAssetPath);

        auto mat = std::dynamic_pointer_cast<MaterialAsset>(object);
        uint32_t texRefCount = 0;
        if (mat)
        {
            texRefCount = static_cast<uint32_t>(mat->getTextureAssetPaths().size());
        }

        out.write(reinterpret_cast<const char*>(&texRefCount), sizeof(texRefCount));
        if (mat)
        {
            for (const auto& ref : mat->getTextureAssetPaths())
            {
                writeString(out, ref);
            }
        }
        break;
    }
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

            const auto& indices = obj2d->getIndices();
            uint32_t indexCount = static_cast<uint32_t>(indices.size());
            out.write(reinterpret_cast<const char*>(&indexCount), sizeof(indexCount));
            if (indexCount > 0)
            {
                out.write(reinterpret_cast<const char*>(indices.data()), indexCount * sizeof(uint32_t));
            }
        }
        else
        {
            uint32_t zero = 0;
            out.write(reinterpret_cast<const char*>(&zero), sizeof(zero));
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

            const auto& indices = obj3d->getIndices();
            uint32_t indexCount = static_cast<uint32_t>(indices.size());
            out.write(reinterpret_cast<const char*>(&indexCount), sizeof(indexCount));
            if (indexCount > 0)
            {
                out.write(reinterpret_cast<const char*>(indices.data()), indexCount * sizeof(uint32_t));
            }
        }
        else
        {
            uint32_t zero = 0;
            out.write(reinterpret_cast<const char*>(&zero), sizeof(zero));
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
			const auto& deps = level->getWorldObjects();
            depCount = static_cast<uint32_t>(deps.size());
        }

        out.write(reinterpret_cast<const char*>(&depCount), sizeof(depCount));

        if (level)
        {
			const auto& deps = level->getWorldObjects();
            const auto& transforms = level->getWorldObjectTransforms();

            for (uint32_t i = 0; i < depCount; ++i)
            {
                const auto& dep = deps[i];
                const std::string path = (dep ? dep->getPath() : std::string{});
                writeString(out, path);

                Transform t{};
                if (!path.empty())
                {
                    auto it = transforms.find(path);
                    if (it != transforms.end())
                    {
                        t = it->second;
                    }
                }

                Vec3 p = t.getPosition();
                Vec3 r = t.getRotation();
                Vec3 s = t.getScale();

                out.write(reinterpret_cast<const char*>(&p), sizeof(p));
                out.write(reinterpret_cast<const char*>(&r), sizeof(r));
                out.write(reinterpret_cast<const char*>(&s), sizeof(s));
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
        logger.log(Logger::Category::AssetManagement, "Error writing asset file: " + finalAssetPath.string(), Logger::LogLevel::ERROR);
        return false;
    }

    object->setIsSaved(true);
    return true;
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

void AssetManager::ensureDefaultTriangleAssetSaved()
{
    auto& logger = Logger::Instance();

    // Lazily create defaults (requires a project to be loaded for importing/saving).
    ensureDefaultAssetsCreated();

    std::shared_ptr<EngineObject> tri;
    bool needsSave = false;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        tri = m_defaultTriangle2D;
        needsSave = (tri != nullptr) && !tri->getIsSaved();
    }

    if (!tri)
    {
        logger.log(Logger::Category::AssetManagement, "Default triangle asset missing; cannot initialize level content.", Logger::LogLevel::WARNING);
        return;
    }

    // Also persist dependencies of the default model (material + textures).
    // The object itself should not hold texture pointers; resolve textures via the material asset.
    if (auto obj2d = std::dynamic_pointer_cast<Object2D>(tri))
    {
        const std::string matPath = obj2d->getMaterialAssetPath();
        if (!matPath.empty())
        {
            auto matObj = loadAssetObject(matPath);
            if (auto mat = std::dynamic_pointer_cast<MaterialAsset>(matObj))
            {
                obj2d->setLoadedMaterialAsset(mat);
                if (!mat->getIsSaved())
                {
                    logger.log(Logger::Category::AssetManagement, "Persisting default material asset: " + mat->getPath(), Logger::LogLevel::INFO);
                    saveAsset(mat);
                }

                auto textures = loadTexturesForMaterial(*mat);
                for (const auto& tex : textures)
                {
                    if (tex && !tex->getIsSaved())
                    {
                        logger.log(Logger::Category::AssetManagement, "Persisting default texture asset: " + tex->getPath(), Logger::LogLevel::INFO);
                        saveAsset(tex);
                    }
                }
            }
        }
    }

    if (needsSave)
    {
        if (!saveAsset(tri))
        {
            logger.log(Logger::Category::AssetManagement, "Failed to persist default triangle asset.", Logger::LogLevel::ERROR);
        }
    }
}

std::unique_ptr<EngineLevel> AssetManager::createLevelWithDefaultTriangle(const std::string& levelPath, const std::string& levelName)
{
    auto& logger = Logger::Instance();
    auto& diagnostics = DiagnosticsManager::Instance();

    if (!diagnostics.isProjectLoaded() || diagnostics.getProjectInfo().projectPath.empty())
    {
        logger.log(Logger::Category::AssetManagement, "createLevelWithDefaultTriangle: no project loaded.", Logger::LogLevel::ERROR);
        return nullptr;
    }

    // Ensure default assets exist on disk.
    ensureDefaultAssetsCreated();

    // Load default model so level can reference it.
    auto quadObj = loadAssetObject("default_quad3d.asset");
    auto quad = std::dynamic_pointer_cast<Object3D>(quadObj);
    if (!quad)
    {
        logger.log(Logger::Category::AssetManagement, "createLevelWithDefaultTriangle: failed to load default_quad3d.asset", Logger::LogLevel::ERROR);
        return nullptr;
    }

    auto level = std::make_unique<EngineLevel>();
    level->setName(levelName);
    level->setPath(fs::path(levelPath).generic_string());
    level->setAssetType(AssetType::Level);
    level->setIsSaved(false);

    level->registerObject(quad);
    level->setLoadedDependency(quad->getPath(), quad);

    Transform t{};
    t.setPosition(Vec3{ 0.25f, 0.0f, 0.0f });
    t.setRotation(Vec3{ 20.0f, 50.0f, 70.0f });
    t.setScale(Vec3{ 1.0f, 1.0f, 1.0f });
    level->setObjectTransform(quad->getPath(), t);

    // Save via aliasing shared_ptr (non-owning).
    std::shared_ptr<EngineObject> base;
    std::shared_ptr<EngineObject> alias(base, static_cast<EngineObject*>(level.get()));
    if (!saveAsset(alias))
    {
        logger.log(Logger::Category::AssetManagement, "createLevelWithDefaultTriangle: failed to save level asset: " + level->getPath(), Logger::LogLevel::ERROR);
        return nullptr;
    }

    return level;
}

std::shared_ptr<EngineObject> AssetManager::createAsset(AssetType type, const std::string& path, const std::string& name, const std::string& sourcePath)
{
    auto& logger = Logger::Instance();
    logger.log(Logger::Category::AssetManagement, "Creating asset with name: " + name + " at " + path, Logger::LogLevel::INFO);

    std::shared_ptr<EngineObject> obj;
    std::string normalizedRel;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);

        switch (type)
        {
        case AssetType::Model2D:
            obj = std::make_shared<Object2D>();
            break;
        case AssetType::Model3D:
            obj = std::make_shared<Object3D>();
            break;
        case AssetType::Texture:
            obj = std::make_shared<Texture>();
            break;
        case AssetType::Material:
            obj = std::make_shared<MaterialAsset>();
            break;
        case AssetType::Audio:
        case AssetType::Script:
        case AssetType::Shader:
        case AssetType::Unknown:
        default:
            obj = std::make_shared<EngineObject>();
            break;
        }

        fs::path rel = fs::path(path);
        if (rel.is_absolute())
        {
            rel = rel.filename();
        }

        normalizedRel = normalizeAssetRelPath(type, rel.generic_string());

        obj->setPath(normalizedRel);
        obj->setName(name);
        obj->setAssetType(type);
        obj->setIsSaved(false);

        if (type == AssetType::Texture)
        {
            auto tex = std::dynamic_pointer_cast<Texture>(obj);
            if (tex)
            {
                if (sourcePath.empty())
                {
                    logger.log("Texture asset creation requires a sourcePath to import image data.", Logger::LogLevel::ERROR);
                    return nullptr;
                }

                int w = 0, h = 0, c = 0;
                unsigned char* data = stbi_load(sourcePath.c_str(), &w, &h, &c, 0);
                if (!data)
                {
                    logger.log("Failed to load texture source image: " + sourcePath, Logger::LogLevel::ERROR);
                    return nullptr;
                }

                const size_t total = static_cast<size_t>(w) * static_cast<size_t>(h) * static_cast<size_t>(c);
                std::vector<unsigned char> bytes;
                bytes.assign(data, data + total);
                stbi_image_free(data);

                tex->setWidth(w);
                tex->setHeight(h);
                tex->setChannels(c);
                tex->setData(std::move(bytes));
            }
        }

        if (type == AssetType::Material)
        {
            auto mat = std::dynamic_pointer_cast<MaterialAsset>(obj);
            if (mat)
            {
                std::vector<std::string> refs;
                if (!sourcePath.empty())
                {
                    refs.push_back(sourcePath);
                }
                mat->setTextureAssetPaths(std::move(refs));
            }
        }

        // ALWAYS register in GC when created
        m_garbageCollector.registerResource(obj);
    }

    if (!saveAsset(obj))
    {
        logger.log("Failed to save newly created asset: " + name, Logger::LogLevel::ERROR);
        return nullptr;
    }

    // Update registry and persist it (only if a project is loaded)
    {
        auto& diagnostics = DiagnosticsManager::Instance();
        const auto info = diagnostics.getProjectInfo();
        if (!info.projectPath.empty() && diagnostics.isProjectLoaded())
        {
            AssetRegistryEntry e;
            e.name = name;
            e.path = obj->getPath();
            e.type = type;
            registerAssetInRegistry(e);

            if (!saveAssetRegistry(info.projectPath))
            {
                logger.log("Failed to update asset registry.", Logger::LogLevel::WARNING);
            }
        }
    }

    return obj;
}

bool AssetManager::loadProject(const std::string& projectPath)
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

    auto createAndSetFallbackLevel = [&]()
    {
        const std::string fallbackPath = (fs::path("Levels") / "DefaultLevel.map").generic_string();
        logger.log(Logger::Category::Project, "Creating fallback level: " + fallbackPath, Logger::LogLevel::WARNING);

        auto level = createLevelWithDefaultTriangle(fallbackPath, fs::path(fallbackPath).stem().string());
        if (level)
        {
            diagnostics.setActiveLevel(std::move(level));
            diagnostics.setScenePrepared(false);
            saveProject(info.projectPath);
        }
        else
        {
            logger.log(Logger::Category::Project, "Failed to create fallback level.", Logger::LogLevel::ERROR);
        }
    };

    if (!LevelPath.empty())
    {
		logger.log(Logger::Category::Project, "Loading active level from project file: " + LevelPath, Logger::LogLevel::INFO);
        const std::string relLevelPath = fs::path(LevelPath).generic_string();
        auto loadedObj = loadAssetObject(relLevelPath);
        if (auto loadedLevel = std::dynamic_pointer_cast<EngineLevel>(loadedObj))
        {
			logger.log(Logger::Category::Project, "Active level loaded: " + loadedLevel->getName(), Logger::LogLevel::INFO);
			// Create a runtime level instance but keep resolved world object references.
			auto runtimeLevel = std::make_unique<EngineLevel>();
			runtimeLevel->setName(loadedLevel->getName());
			runtimeLevel->setPath(loadedLevel->getPath());
			runtimeLevel->setAssetType(loadedLevel->getAssetType());
			runtimeLevel->setIsSaved(true);

			// Copy resolved world objects (shared_ptr) and transforms.
			runtimeLevel->getWorldObjects() = loadedLevel->getWorldObjects();
			for (const auto& kv : loadedLevel->getWorldObjectTransforms())
			{
				runtimeLevel->setObjectTransform(kv.first, kv.second);
			}
			logger.log(Logger::Category::Project, "Active level initialized: " + runtimeLevel->getName(), Logger::LogLevel::INFO);
			diagnostics.setActiveLevel(std::move(runtimeLevel));
        }
        else
        {
            logger.log(Logger::Category::AssetManagement, "Failed to load configured level asset: " + relLevelPath, Logger::LogLevel::WARNING);
            createAndSetFallbackLevel();
        }
    }
    else
    {
		logger.log(Logger::Category::Project, "No active level set in project file.", Logger::LogLevel::WARNING);
		createAndSetFallbackLevel();
    }

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

bool AssetManager::saveProject(const std::string& projectPath)
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

bool AssetManager::createProject(const std::string& parentDir, const std::string& projectName, const DiagnosticsManager::ProjectInfo& info)
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

    if (auto* level = diagnostics.getActiveLevel())
    {
        saveAsset(std::shared_ptr<EngineObject>(level, [](EngineObject*) {}));
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

void AssetManager::importAssetWithDialog(SDL_Window* parentWindow, AssetType preferredType)
{
    auto& logger = Logger::Instance();

    const auto& diagnostics = DiagnosticsManager::Instance();
    if (!diagnostics.isProjectLoaded() || diagnostics.getProjectInfo().projectPath.empty())
    {
        logger.log(Logger::Category::AssetManagement, "Import failed: no project loaded.", Logger::LogLevel::ERROR);
        return;
    }

    auto* ctx = new ImportDialogContext();
    ctx->preferredType = preferredType;

    SDL_DialogFileFilter filters[] = {
        { "Images", "png;jpg;jpeg;bmp;tga" },
        { "All files", "*" }
    };

    SDL_ShowOpenFileDialog(
        OnImportDialogClosed,
        ctx,
        parentWindow,
        filters,
        SDL_arraysize(filters),
        nullptr,
        false);

    logger.log(Logger::Category::AssetManagement, "Import dialog opened.", Logger::LogLevel::INFO);
}

// NOTE: File dialogs should be handled by the SDL/UI layer and then call importAssetFromFile(...).

std::shared_ptr<EngineObject> AssetManager::importAssetFromFile(AssetType type, const std::string& sourceFilePath)
{
    auto& logger = Logger::Instance();

    const auto& diagnostics = DiagnosticsManager::Instance();
    if (!diagnostics.isProjectLoaded() || diagnostics.getProjectInfo().projectPath.empty())
    {
        logger.log(Logger::Category::AssetManagement, "Import failed: no project loaded.", Logger::LogLevel::ERROR);
        return nullptr;
    }

    fs::path src(sourceFilePath);
    if (sourceFilePath.empty() || !fs::exists(src))
    {
        logger.log(Logger::Category::AssetManagement, "Import failed: source file missing: " + sourceFilePath, Logger::LogLevel::ERROR);
        return nullptr;
    }

    if (type == AssetType::Unknown)
    {
        type = DetectAssetTypeFromPath(src);
        if (type == AssetType::Unknown)
        {
            logger.log(Logger::Category::AssetManagement,
                "Import failed: couldn't detect asset type from extension: " + src.extension().string() + " (file: " + src.string() + ")",
                Logger::LogLevel::ERROR);
            return nullptr;
        }
    }

    // Dispatch by asset type (extensible)
    switch (type)
    {
    case AssetType::Texture:
    {
        // Destination: Content/Textures/<stem>.asset
        const std::string baseName = sanitizeName(src.stem().string());
        const std::string relAssetPath = (fs::path("Textures") / (baseName + ".asset")).generic_string();

        // If already exists, add a suffix.
        std::string finalRelAssetPath = relAssetPath;
        int suffix = 1;
        while (doesAssetExist(finalRelAssetPath))
        {
            finalRelAssetPath = (fs::path("Textures") / (baseName + "_" + std::to_string(suffix) + ".asset")).generic_string();
            ++suffix;
        }

        logger.log(Logger::Category::AssetManagement, "Importing texture from: " + src.string(), Logger::LogLevel::INFO);
        auto created = createAsset(AssetType::Texture, finalRelAssetPath, baseName, src.string());
        if (created)
        {
            logger.log(Logger::Category::AssetManagement, "Imported texture asset: " + finalRelAssetPath, Logger::LogLevel::INFO);
        }
        return created;
    }
    default:
        logger.log(Logger::Category::AssetManagement, "Import not implemented for this asset type yet.", Logger::LogLevel::WARNING);
        return nullptr;
    }
}
