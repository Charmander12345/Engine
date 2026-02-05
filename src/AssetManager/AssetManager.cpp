#include "AssetManager.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <filesystem>
#include <fstream>
#include <cstdint>
#include "AssetTypes.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>

#include "../Renderer/Material.h"

namespace fs = std::filesystem;

int AssetManager::s_nextAssetID = 1;

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

	char first = 0;
	if (!in.get(first)) return false;
	if (first == '{')
	{
		in.unget();
		json j = json::parse(in, nullptr, false);
		if (j.is_discarded()) return false;
		if (!j.is_object() || !j.contains("type")) return false;
		outType = static_cast<AssetType>(j.at("type").get<int>());
		return true;
	}

	in.unget();
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

	char first = 0;
	if (!in.get(first)) return false;
	if (first == '{')
	{
		in.unget();
		json j = json::parse(in, nullptr, false);
		if (j.is_discarded()) return false;
		if (!j.is_object() || !j.contains("name")) return false;
		outName = j.at("name").get<std::string>();
		return true;
	}

	in.unget();
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

static bool readAssetHeader(std::ifstream& in, AssetType& outType, std::string& outName, bool& outIsJson)
{
	outIsJson = false;
	char first = 0;
	if (!in.get(first)) return false;
	if (first == '{')
	{
		in.unget();
		json j = json::parse(in, nullptr, false);
		if (j.is_discarded()) return false;
		if (!j.is_object() || !j.contains("type") || !j.contains("name")) return false;
		outType = static_cast<AssetType>(j.at("type").get<int>());
		outName = j.at("name").get<std::string>();
		outIsJson = true;
		return true;
	}

	in.unget();
	uint32_t magic = 0;
	uint32_t version = 0;
	if (!in.read(reinterpret_cast<char*>(&magic), sizeof(magic))) return false;
	if (!in.read(reinterpret_cast<char*>(&version), sizeof(version))) return false;
	if (magic != 0x41535453 || version != 2) return false;

	int32_t typeInt = 0;
	if (!in.read(reinterpret_cast<char*>(&typeInt), sizeof(typeInt))) return false;
	outType = static_cast<AssetType>(typeInt);

	std::string name;
	if (!readString(in, name)) return false;
	outName = std::move(name);
	return true;
}

static bool readAssetJson(std::ifstream& in, json& outJson, std::string& errorMessage, bool isJsonFormat)
{
	if (isJsonFormat)
	{
		in.clear();
		in.seekg(0, std::ios::beg);
	}

	std::string jsonText((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	if (jsonText.empty())
	{
		outJson = json::object();
		return true;
	}

	outJson = json::parse(jsonText, nullptr, false);
	if (outJson.is_discarded())
	{
		errorMessage = "Failed to parse asset JSON.";
		return false;
	}
	if (outJson.is_object() && outJson.contains("data"))
	{
		outJson = outJson.at("data");
	}
	return true;
}

std::vector<std::shared_ptr<AssetData>> AssetManager::getAssetsByType(AssetType type) const
{
	std::vector<std::shared_ptr<AssetData>> results;
	std::lock_guard<std::mutex> lock(m_stateMutex);
	results.reserve(m_loadedAssets.size());
	for (const auto& [id, asset] : m_loadedAssets)
	{
		if (asset && asset->getAssetType() == type)
		{
			results.push_back(asset);
		}
	}
	return results;
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

    const auto ensureOnDisk = [&](const std::string& relPath, AssetType expectedType, const std::shared_ptr<AssetData>& obj, bool forceOverwrite = false) -> bool
    {
        const fs::path abs = contentRoot / fs::path(relPath);

        bool existsAndOk = false;
        if (fs::exists(abs))
        {
            AssetType headerType{ AssetType::Unknown };
            existsAndOk = readAssetHeaderType(abs, headerType) && headerType == expectedType;
        }

        if (forceOverwrite && existsAndOk)
        {
            existsAndOk = false;
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
        obj->setType(expectedType);
        obj->setIsSaved(false);
		auto id = registerLoadedAsset(obj);
        if (id != 0)
        {
            obj->setId(id);
        }
		Asset asset;
		asset.ID = id;
		asset.type = expectedType;
        return saveAsset(asset);
    };

    // 1) wall texture
    const std::string wallTexRel = (fs::path("Textures") / "wall.asset").generic_string();
    {
        auto tex = std::make_shared<AssetData>();
        tex->setName("wall");
        json texData = json::object();
        const fs::path wallPngPath = fs::current_path() / "Content" / "Textures" / "wall.jpg";
        if (fs::exists(wallPngPath))
        {
            texData["m_sourcePath"] = (fs::path("Content") / "Textures" / "wall.jpg").generic_string();
        }
        else
        {
            int width = 2;
            int height = 2;
            int channels = 4;
            std::vector<unsigned char> pixels{
                255,   0,   0, 255,   0, 255,   0, 255,
                  0,   0, 255, 255, 255, 255,   0, 255
            };
            texData["m_width"] = width;
            texData["m_height"] = height;
            texData["m_channels"] = channels;
            texData["m_data"] = std::move(pixels);
        }
        tex->setData(std::move(texData));
        const bool forceOverwrite = fs::exists(wallPngPath);
        ensureOnDisk(wallTexRel, AssetType::Texture, tex, forceOverwrite);
    }

    // 2) wall material
    const std::string wallMatRel = (fs::path("Materials") / "wall.asset").generic_string();
    {
		auto mat = std::make_shared<AssetData>();
        mat->setName("DefaultDebugMaterial");
        json matData = json::object();
        matData["m_textureAssetPaths"] = std::vector<std::string>{ wallTexRel };
        mat->setData(std::move(matData));
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
            auto quad = std::make_shared<AssetData>();
            quad->setName("DefaultQuad3D");
				// Material assets/textures are handled by Material; Object3D only stores a runtime Material instance.
            json quadData = json::object();
            quadData["m_vertices"] = std::vector<float>{
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
            };
            quadData["m_indices"] = std::vector<uint32_t>{};
            quad->setData(std::move(quadData));

            ensureOnDisk(quad3dRel, AssetType::Model3D, quad);
        }
    }

    const std::string defaultLevelRel = (fs::path("Levels") / "DefaultLevel.map").generic_string();
    {
        json levelJson = json::object();
        levelJson["Objects"] = json::array();
        levelJson["Groups"] = json::array();

        json entities = json::array();
        json entity = json::object();
        entity["id"] = 1;

        json components = json::object();
        components["Transform"] = json{
            {"position", json::array({ 0.0f, 0.0f, 0.0f })},
            {"rotation", json::array({ 0.0f, 0.0f, 0.0f })},
            {"scale", json::array({ 1.0f, 1.0f, 1.0f })}
        };
        components["Mesh"] = json{ {"meshAssetPath", quad3dRel} };
        components["Material"] = json{ {"materialAssetPath", wallMatRel} };
        entity["components"] = components;
        entities.push_back(entity);

        levelJson["Entities"] = entities;

        auto defaultLevel = std::make_unique<EngineLevel>();
        defaultLevel->setName("DefaultLevel");
        defaultLevel->setPath(defaultLevelRel);
        defaultLevel->setAssetType(AssetType::Level);
        defaultLevel->setLevelData(levelJson);
        defaultLevel->prepareEcs();

        const fs::path abs = contentRoot / fs::path(defaultLevelRel);
        bool existsAndOk = false;
        if (fs::exists(abs))
        {
            AssetType headerType{ AssetType::Unknown };
            existsAndOk = readAssetHeaderType(abs, headerType) && headerType == AssetType::Level;
        }
        if (!existsAndOk)
        {
            logger.log(Logger::Category::AssetManagement, "Default level missing/invalid, creating: " + defaultLevelRel, Logger::LogLevel::WARNING);
            if (!saveLevelAsset(defaultLevel).success)
            {
                logger.log(Logger::Category::AssetManagement, "Failed to save default level asset.", Logger::LogLevel::ERROR);
            }
        }
        else
        {
            logger.log(Logger::Category::AssetManagement, "Default level OK: " + defaultLevelRel, Logger::LogLevel::INFO);
        }

        diagnostics.setActiveLevel(std::move(defaultLevel));
        diagnostics.setScenePrepared(false);
    }

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

unsigned int AssetManager::registerLoadedAsset(const std::shared_ptr<AssetData>& object)
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

std::shared_ptr<AssetData> AssetManager::getLoadedAssetByID(unsigned int id) const
{
	auto it = m_loadedAssets.find(id);
	if (it == m_loadedAssets.end())
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

int AssetManager::loadAsset(const std::string& path, AssetType type, SyncState syncState)
{
    if (path.empty())
	{
		return 0;
	}

	{
		std::lock_guard<std::mutex> lock(m_stateMutex);
		for (const auto& [id, obj] : m_loadedAssets)
		{
			if (obj && obj->getPath() == path)
			{
				return static_cast<int>(id);
			}
		}
	}

	if (syncState == Async)
	{
		enqueueJob([this, path, type]()
			{
				switch (type)
				{
				case AssetType::Model3D:
					loadObject3DAsset(path);
					break;
				case AssetType::Model2D:
					loadObject2DAsset(path);
					break;
				case AssetType::Texture:
					loadTextureAsset(path);
					break;
				case AssetType::Material:
					loadMaterialAsset(path);
					break;
				case AssetType::Level:
					loadLevelAsset(path);
					break;
				default:
					break;
				}
			});
		return 0;
	}

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
	default:
		return 0;
	}

	if (!result.success)
	{
		return 0;
	}
	{
		std::lock_guard<std::mutex> lock(m_stateMutex);
		for (const auto& [id, obj] : m_loadedAssets)
		{
			if (obj && obj->getPath() == path)
			{
				return static_cast<int>(id);
			}
		}
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
	std::shared_ptr<AssetData> assetData;
	{
		std::lock_guard<std::mutex> lock(m_stateMutex);
		auto it = m_loadedAssets.find(asset.ID);
		if (it != m_loadedAssets.end())
		{
			assetData = it->second;
		}
	}
	if (!assetData)
    {
        Logger::Instance().log(
            Logger::Category::AssetManagement,
            "saveAsset(): Asset with ID " + std::to_string(asset.ID) + " not found among loaded assets.",
            Logger::LogLevel::ERROR);
        return false;
    }
        auto newAction = diagnostics.registerAction(DiagnosticsManager::ActionType::SavingAsset);
        SaveResult result;
        switch (asset.type)
        {
        case AssetType::Model2D:
            if (syncState == Async)
            {
                enqueueJob([this, asset, newAction]()
                    {
						std::shared_ptr<AssetData> queuedAssetData;
						{
							std::lock_guard<std::mutex> lock(m_stateMutex);
							auto it = m_loadedAssets.find(asset.ID);
							if (it != m_loadedAssets.end())
							{
								queuedAssetData = it->second;
							}
						}
						saveObject2DAsset(queuedAssetData);
						DiagnosticsManager::Instance().updateActionProgress(newAction.ID, false);
                    });
                return true;
            }
			result = saveObject2DAsset(assetData);
            break;
        case AssetType::Model3D:
			result = saveObject3DAsset(assetData);
            break;
        case AssetType::Texture:
			result = saveTextureAsset(assetData);
            break;
        case AssetType::Material:
			result = saveMaterialAsset(assetData);
            break;
        case AssetType::Audio:
        case AssetType::Script:
        case AssetType::Shader:
        case AssetType::Unknown:
        default:
            Logger::Instance().log(
                Logger::Category::AssetManagement,
				"saveAsset(): Unsupported asset type for asset: " + assetData->getName(),
                Logger::LogLevel::ERROR);
            return false;
        }
        if (!result.success)
        {
            Logger::Instance().log(
                Logger::Category::AssetManagement,
				"saveAsset(): Failed to save asset: " + assetData->getName() + ". Error: " + result.errorMessage,
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
	LoadResult result;
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open())
	{
		result.errorMessage = "Failed to open texture asset file.";
		return result;
	}

    AssetType headerType{ AssetType::Unknown };
    std::string name;
    bool isJson = false;
    if (!readAssetHeader(in, headerType, name, isJson))
	{
		result.errorMessage = "Invalid texture asset header.";
		return result;
	}

	if (headerType != AssetType::Texture)
	{
		result.errorMessage = "Asset type mismatch for texture.";
		return result;
	}

    if (!readAssetJson(in, result.j, result.errorMessage, isJson))
	{
		return result;
	}

	auto texture = std::make_shared<AssetData>();
	if (result.j.is_object())
	{
		if (result.j.contains("m_sourcePath"))
		{
			const auto& sourceValue = result.j.at("m_sourcePath");
			if (sourceValue.is_string())
			{
				fs::path sourcePath = sourceValue.get<std::string>();
				if (!sourcePath.is_absolute())
				{
					sourcePath = fs::current_path() / sourcePath;
				}
				if (fs::exists(sourcePath))
				{
					int width = 0;
					int height = 0;
					int channels = 0;
					unsigned char* data = stbi_load(sourcePath.string().c_str(), &width, &height, &channels, 4);
					if (data)
					{
						channels = 4;
						result.j["m_width"] = width;
						result.j["m_height"] = height;
						result.j["m_channels"] = channels;
						result.j["m_data"] = std::vector<unsigned char>(data, data + (width * height * channels));
						stbi_image_free(data);
					}
				}
			}
		}
		texture->setData(result.j);
	}

	if (name.empty())
	{
		name = fs::path(path).stem().string();
	}

	texture->setName(name);
	texture->setPath(path);
	texture->setAssetType(headerType);
	texture->setType(headerType);
	texture->setIsSaved(true);

	auto id = registerLoadedAsset(texture);
	if (id != 0)
	{
		texture->setId(id);
	}
	m_garbageCollector.registerResource(texture);

	result.success = true;
	return result;
}

AssetManager::LoadResult AssetManager::loadMaterialAsset(const std::string& path)
{
	LoadResult result;
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open())
	{
		result.errorMessage = "Failed to open material asset file.";
		return result;
	}

    AssetType headerType{ AssetType::Unknown };
    std::string name;
    bool isJson = false;
    if (!readAssetHeader(in, headerType, name, isJson))
	{
		result.errorMessage = "Invalid material asset header.";
		return result;
	}

	if (headerType != AssetType::Material)
	{
		result.errorMessage = "Asset type mismatch for material.";
		return result;
	}

    if (!readAssetJson(in, result.j, result.errorMessage, isJson))
	{
		return result;
	}

	auto material = std::make_shared<AssetData>();
	if (result.j.is_object())
	{
		material->setData(result.j);
	}

	if (name.empty())
	{
		name = fs::path(path).stem().string();
	}

	material->setName(name);
	material->setPath(path);
	material->setAssetType(headerType);
	material->setType(headerType);
	material->setIsSaved(true);

	auto id = registerLoadedAsset(material);
	if (id != 0)
	{
		material->setId(id);
	}
	m_garbageCollector.registerResource(material);

	result.success = true;
	return result;
}

AssetManager::LoadResult AssetManager::loadObject2DAsset(const std::string& path)
{
	LoadResult result;
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open())
	{
		result.errorMessage = "Failed to open Object2D asset file.";
		return result;
	}

    AssetType headerType{ AssetType::Unknown };
    std::string name;
    bool isJson = false;
    if (!readAssetHeader(in, headerType, name, isJson))
	{
		result.errorMessage = "Invalid Object2D asset header.";
		return result;
	}

	if (headerType != AssetType::Model2D)
	{
		result.errorMessage = "Asset type mismatch for Object2D.";
		return result;
	}

    if (!readAssetJson(in, result.j, result.errorMessage, isJson))
	{
		return result;
	}

	auto object2D = std::make_shared<AssetData>();
	if (result.j.is_object())
	{
		object2D->setData(result.j);
	}

	if (name.empty())
	{
		name = fs::path(path).stem().string();
	}

	object2D->setName(name);
	object2D->setPath(path);
	object2D->setAssetType(headerType);
	object2D->setType(headerType);
	object2D->setIsSaved(true);

	auto id = registerLoadedAsset(object2D);
	if (id != 0)
	{
		object2D->setId(id);
	}
	m_garbageCollector.registerResource(object2D);

	result.success = true;
	return result;
}

AssetManager::LoadResult AssetManager::loadObject3DAsset(const std::string& path)
{
	LoadResult result;
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open())
	{
		result.errorMessage = "Failed to open Object3D asset file.";
		return result;
	}

    AssetType headerType{ AssetType::Unknown };
    std::string name;
    bool isJson = false;
    if (!readAssetHeader(in, headerType, name, isJson))
	{
		result.errorMessage = "Invalid Object3D asset header.";
		return result;
	}

	if (headerType != AssetType::Model3D)
	{
		result.errorMessage = "Asset type mismatch for Object3D.";
		return result;
	}

    if (!readAssetJson(in, result.j, result.errorMessage, isJson))
	{
		return result;
	}

	auto object3D = std::make_shared<AssetData>();
	if (result.j.is_object())
	{
		object3D->setData(result.j);
	}

	if (name.empty())
	{
		name = fs::path(path).stem().string();
	}

	object3D->setName(name);
	object3D->setPath(path);
	object3D->setAssetType(headerType);
	object3D->setType(headerType);
	object3D->setIsSaved(true);

	auto id = registerLoadedAsset(object3D);
	if (id != 0)
	{
		object3D->setId(id);
	}
	m_garbageCollector.registerResource(object3D);

	result.success = true;
	return result;
}

AssetManager::LoadResult AssetManager::loadLevelAsset(const std::string& path)
{
	LoadResult result;
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open())
	{
		result.errorMessage = "Failed to open level asset file.";
		return result;
	}

    AssetType headerType{ AssetType::Unknown };
    std::string name;
    bool isJson = false;
    if (!readAssetHeader(in, headerType, name, isJson))
	{
		result.errorMessage = "Invalid level asset header.";
		return result;
	}

	if (headerType != AssetType::Level)
	{
		result.errorMessage = "Asset type mismatch for level.";
		return result;
	}

    if (!readAssetJson(in, result.j, result.errorMessage, isJson))
	{
		return result;
	}

	if (name.empty())
	{
		name = fs::path(path).stem().string();
	}

	auto level = std::make_unique<EngineLevel>();
	level->setName(name);
	level->setPath(path);
	level->setAssetType(headerType);
	level->setIsSaved(true);
	level->setLevelData(result.j);

	auto& diagnostics = DiagnosticsManager::Instance();
	diagnostics.setActiveLevel(std::move(level));
	diagnostics.setScenePrepared(false);
	result.success = true;
	return result;
}

AssetManager::SaveResult AssetManager::saveTextureAsset(const std::shared_ptr<AssetData>& texture)
{
    SaveResult result;
    if (!texture)
    {
        result.errorMessage = "Texture asset is null.";
        return result;
    }

    auto& diagnostics = DiagnosticsManager::Instance();
    if (!diagnostics.isProjectLoaded())
    {
        result.errorMessage = "No project loaded for texture save.";
        return result;
    }

    std::string name = texture->getName();
    if (name.empty())
    {
        name = "Texture";
        texture->setName(name);
    }

    std::string relPath = texture->getPath();
    if (relPath.empty())
    {
        relPath = name + ".asset";
        texture->setPath(relPath);
    }

    fs::path rel = fs::path(relPath);
    if (rel.extension().empty())
    {
        rel.replace_extension(".asset");
        relPath = rel.generic_string();
        texture->setPath(relPath);
    }

    const fs::path absPath = fs::path(diagnostics.getProjectInfo().projectPath) / "Content" / fs::path(relPath);
    std::error_code ec;
    fs::create_directories(absPath.parent_path(), ec);

    std::ofstream out(absPath, std::ios::out | std::ios::trunc);
    if (!out.is_open())
    {
        result.errorMessage = "Failed to open texture asset file for writing.";
        return result;
    }

    json data = texture->getData();
    if (data.is_null())
    {
        data = json::object();
    }

    json dataToSave = data;
    if (dataToSave.is_object() && dataToSave.contains("m_sourcePath"))
    {
        dataToSave.erase("m_data");
    }

    json fileJson = json::object();
    fileJson["magic"] = 0x41535453;
    fileJson["version"] = 2;
    fileJson["type"] = static_cast<int>(AssetType::Texture);
    fileJson["name"] = name;
    fileJson["data"] = dataToSave;

    out << fileJson.dump(4);
    if (!out.good())
    {
        result.errorMessage = "Failed to write texture asset data.";
        return result;
    }

    texture->setIsSaved(true);
    result.success = true;
    return result;
}

AssetManager::SaveResult AssetManager::saveMaterialAsset(const std::shared_ptr<AssetData>& material)
{
    SaveResult result;
    if (!material)
    {
        result.errorMessage = "Material asset is null.";
        return result;
    }

    auto& diagnostics = DiagnosticsManager::Instance();
    if (!diagnostics.isProjectLoaded())
    {
        result.errorMessage = "No project loaded for material save.";
        return result;
    }

    std::string name = material->getName();
    if (name.empty())
    {
        name = "Material";
        material->setName(name);
    }

    std::string relPath = material->getPath();
    if (relPath.empty())
    {
        relPath = name + ".asset";
    }
    relPath = normalizeAssetRelPath(AssetType::Material, relPath);
    material->setPath(relPath);

    const fs::path absPath = fs::path(diagnostics.getProjectInfo().projectPath) / "Content" / fs::path(relPath);
    std::error_code ec;
    fs::create_directories(absPath.parent_path(), ec);

    std::ofstream out(absPath, std::ios::out | std::ios::trunc);
    if (!out.is_open())
    {
        result.errorMessage = "Failed to open material asset file for writing.";
        return result;
    }

    json data = material->getData();
    if (data.is_null())
    {
        data = json::object();
    }

    json fileJson = json::object();
    fileJson["magic"] = 0x41535453;
    fileJson["version"] = 2;
    fileJson["type"] = static_cast<int>(AssetType::Material);
    fileJson["name"] = name;
    fileJson["data"] = data;

    out << fileJson.dump(4);
    if (!out.good())
    {
        result.errorMessage = "Failed to write material asset data.";
        return result;
    }

    material->setIsSaved(true);
    result.success = true;
    return result;
}

AssetManager::SaveResult AssetManager::saveObject2DAsset(const std::shared_ptr<AssetData>& object2D)
{
    SaveResult result;
    if (!object2D)
    {
        result.errorMessage = "Object2D asset is null.";
        return result;
    }

    auto& diagnostics = DiagnosticsManager::Instance();
    if (!diagnostics.isProjectLoaded())
    {
        result.errorMessage = "No project loaded for Object2D save.";
        return result;
    }

    std::string name = object2D->getName();
    if (name.empty())
    {
        name = "Object2D";
        object2D->setName(name);
    }

    std::string relPath = object2D->getPath();
    if (relPath.empty())
    {
        relPath = name + ".asset";
        object2D->setPath(relPath);
    }

    fs::path rel = fs::path(relPath);
    if (rel.extension().empty())
    {
        rel.replace_extension(".asset");
        relPath = rel.generic_string();
        object2D->setPath(relPath);
    }

    const fs::path absPath = fs::path(diagnostics.getProjectInfo().projectPath) / "Content" / fs::path(relPath);
    std::error_code ec;
    fs::create_directories(absPath.parent_path(), ec);

    std::ofstream out(absPath, std::ios::out | std::ios::trunc);
    if (!out.is_open())
    {
        result.errorMessage = "Failed to open Object2D asset file for writing.";
        return result;
    }

    json data = object2D->getData();
    if (data.is_null())
    {
        data = json::object();
    }

    json fileJson = json::object();
    fileJson["magic"] = 0x41535453;
    fileJson["version"] = 2;
    fileJson["type"] = static_cast<int>(AssetType::Model2D);
    fileJson["name"] = name;
    fileJson["data"] = data;

    out << fileJson.dump(4);
    if (!out.good())
    {
        result.errorMessage = "Failed to write Object2D asset data.";
        return result;
    }

    object2D->setIsSaved(true);
    result.success = true;
    return result;
}

AssetManager::SaveResult AssetManager::saveObject3DAsset(const std::shared_ptr<AssetData>& object3D)
{
    SaveResult result;
    if (!object3D)
    {
        result.errorMessage = "Object3D asset is null.";
        return result;
    }

    auto& diagnostics = DiagnosticsManager::Instance();
    if (!diagnostics.isProjectLoaded())
    {
        result.errorMessage = "No project loaded for Object3D save.";
        return result;
    }

    std::string name = object3D->getName();
    if (name.empty())
    {
        name = "Object3D";
        object3D->setName(name);
    }

    std::string relPath = object3D->getPath();
    if (relPath.empty())
    {
        relPath = name + ".asset";
        object3D->setPath(relPath);
    }

    fs::path rel = fs::path(relPath);
    if (rel.extension().empty())
    {
        rel.replace_extension(".asset");
        relPath = rel.generic_string();
        object3D->setPath(relPath);
    }

    const fs::path absPath = fs::path(diagnostics.getProjectInfo().projectPath) / "Content" / fs::path(relPath);
    std::error_code ec;
    fs::create_directories(absPath.parent_path(), ec);

    std::ofstream out(absPath, std::ios::out | std::ios::trunc);
    if (!out.is_open())
    {
        result.errorMessage = "Failed to open Object3D asset file for writing.";
        return result;
    }

    json data = object3D->getData();
    if (data.is_null())
    {
        data = json::object();
    }

    json fileJson = json::object();
    fileJson["magic"] = 0x41535453;
    fileJson["version"] = 2;
    fileJson["type"] = static_cast<int>(AssetType::Model3D);
    fileJson["name"] = name;
    fileJson["data"] = data;

    out << fileJson.dump(4);
    if (!out.good())
    {
        result.errorMessage = "Failed to write Object3D asset data.";
        return result;
    }

    object3D->setIsSaved(true);
    result.success = true;
    return result;
}

AssetManager::SaveResult AssetManager::saveLevelAsset(const std::unique_ptr<EngineLevel>& level)
{
	SaveResult result;
	if (!level)
	{
		result.errorMessage = "Level asset is null.";
		return result;
	}

	auto& diagnostics = DiagnosticsManager::Instance();
	if (!diagnostics.isProjectLoaded())
	{
		result.errorMessage = "No project loaded for level save.";
		return result;
	}

	std::string name = level->getName();
	if (name.empty())
	{
		name = "DefaultLevel";
		level->setName(name);
	}

	std::string relPath = level->getPath();
	if (relPath.empty())
	{
		relPath = (fs::path("Levels") / (name + ".map")).generic_string();
		level->setPath(relPath);
	}

	const fs::path absPath = fs::path(diagnostics.getProjectInfo().projectPath) / "Content" / fs::path(relPath);
	std::error_code ec;
	fs::create_directories(absPath.parent_path(), ec);

    std::ofstream out(absPath, std::ios::out | std::ios::trunc);
	if (!out.is_open())
	{
		result.errorMessage = "Failed to open level asset file for writing.";
		return result;
	}

    json levelJson;
	std::vector<std::string> objectPaths;
	for (const auto& objInstance : level->getWorldObjects())
	{
		if (objInstance && objInstance->object)
		{
			objectPaths.push_back(objInstance->object->getPath());
		}
	}

	std::vector<json> groupsJson;
	for (const auto& grp : level->getGroups())
	{
		json g;
		g["id"] = grp.id;
		std::vector<std::string> groupObjectPaths;
		for (const auto& obj : grp.objects)
		{
			if (obj)
			{
				groupObjectPaths.push_back(obj->getPath());
			}
		}
		g["objects"] = groupObjectPaths;
		g["isInstanced"] = grp.isInstanced;
		groupsJson.push_back(g);
	}

	levelJson["Objects"] = objectPaths;
	levelJson["Groups"] = groupsJson;
    levelJson["Entities"] = level->serializeEcsEntities();

    json fileJson = json::object();
    fileJson["magic"] = 0x41535453;
    fileJson["version"] = 2;
    fileJson["type"] = static_cast<int>(AssetType::Level);
    fileJson["name"] = name;
    fileJson["data"] = levelJson;

    out << fileJson.dump(4);
    if (!out.good())
    {
        result.errorMessage = "Failed to write level asset data.";
        return result;
    }

	level->setIsSaved(true);
	result.success = true;
	return result;
}
