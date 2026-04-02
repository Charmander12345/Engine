#include "AssetManager.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <filesystem>
#include <fstream>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <exception>
#include <algorithm>
#include "AssetTypes.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#if ENGINE_EDITOR
#include <SDL3/SDL_dialog.h>
#endif

#include "../Renderer/Material.h"
#include "../Core/AudioManager.h"

#if ENGINE_EDITOR
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "../Core/ECS/ECS.h"
#include "../Renderer/EditorTheme.h"
#endif

namespace fs = std::filesystem;

int AssetManager::s_nextAssetID = 1;

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

#if ENGINE_EDITOR
static fs::path getEditorWidgetsRootPath()
{
    return fs::current_path() / "Editor" / "Widgets";
}
#endif

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

static bool readAssetHeaderFromMemory(const std::vector<char>& buffer, AssetType& outType, std::string& outName, bool& outIsJson, size_t& headerEndPos, bool& outIsMsgPack)
{
	outIsJson = false;
	outIsMsgPack = false;
	if (buffer.empty()) return false;

	// Check for JSON start
	size_t i = 0;
	while (i < buffer.size() && std::isspace(static_cast<unsigned char>(buffer[i]))) i++;
	if (i < buffer.size() && buffer[i] == '{')
	{
		outIsJson = true;
		// Parse the whole buffer to get header info
		try {
			json j = json::parse(buffer.begin() + i, buffer.end(), nullptr, false);
			if (j.is_discarded()) return false;
			if (!j.is_object() || !j.contains("type") || !j.contains("name")) return false;
			outType = static_cast<AssetType>(j.at("type").get<int>());
			outName = j.at("name").get<std::string>();
			headerEndPos = 0;
			return true;
		} catch (...) {
			return false;
		}
	}

	// Binary format check (version 2 = JSON body, version 3 = MessagePack body)
	if (buffer.size() < 12) return false;

	const char* ptr = buffer.data();
	uint32_t magic = *reinterpret_cast<const uint32_t*>(ptr); ptr += 4;
	uint32_t version = *reinterpret_cast<const uint32_t*>(ptr); ptr += 4;

	if (magic != 0x41535453 || (version != 2 && version != 3)) return false;
	outIsMsgPack = (version == 3);

	int32_t typeInt = *reinterpret_cast<const int32_t*>(ptr); ptr += 4;
	outType = static_cast<AssetType>(typeInt);

	if (ptr + 4 > buffer.data() + buffer.size()) return false;
	uint32_t nameLen = *reinterpret_cast<const uint32_t*>(ptr); ptr += 4;

	if (ptr + nameLen > buffer.data() + buffer.size()) return false;
	outName.assign(ptr, nameLen);
	ptr += nameLen;

	headerEndPos = ptr - buffer.data();
	return true;
}

static bool readAssetJsonFromMemory(const std::vector<char>& buffer, json& outJson, std::string& errorMessage, bool isJsonFormat, size_t headerEndPos, bool isMsgPack = false)
{
	if (isMsgPack)
	{
		// MessagePack body after binary header (cooked format, version 3)
		if (headerEndPos >= buffer.size()) {
			outJson = json::object();
			return true;
		}
		const auto* start = reinterpret_cast<const uint8_t*>(buffer.data() + headerEndPos);
		const auto* end   = reinterpret_cast<const uint8_t*>(buffer.data() + buffer.size());
		outJson = json::from_msgpack(start, end, true, false);
		if (outJson.is_discarded())
		{
			errorMessage = "Failed to parse MessagePack asset body.";
			return false;
		}
		// MessagePack body is the data directly (no "data" wrapper)
		return true;
	}

	if (isJsonFormat)
	{
		// Find start of JSON
		size_t i = 0;
		while (i < buffer.size() && std::isspace(static_cast<unsigned char>(buffer[i]))) i++;
		if (i >= buffer.size()) {
			 outJson = json::object();
			 return true;
		}
		outJson = json::parse(buffer.begin() + i, buffer.end(), nullptr, false);
	}
	else
	{
		// Binary header v2: headerEndPos is where JSON body starts
		if (headerEndPos >= buffer.size()) {
			 outJson = json::object();
			 return true; 
		}
		const char* start = buffer.data() + headerEndPos;
		const char* end = buffer.data() + buffer.size();
		outJson = json::parse(start, end, nullptr, false);
	}

	if (outJson.is_discarded())
	{
		errorMessage = "Failed to parse asset JSON from memory.";
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
        // Try loading from HPK archive
        if (m_hpkReader && m_hpkReader->isMounted())
        {
            const std::string vpath = m_hpkReader->makeVirtualPath(regPath.string());
            if (!vpath.empty())
            {
                auto data = m_hpkReader->readFile(vpath);
                if (data && data->size() >= 12)
                {
                    const char* ptr = data->data();
                    const char* end = ptr + data->size();

                    uint32_t magic = 0, version = 0;
                    std::memcpy(&magic, ptr, 4);   ptr += 4;
                    std::memcpy(&version, ptr, 4); ptr += 4;
                    if (magic != 0x41525247 || version != 1) return false;

                    uint32_t count = 0;
                    std::memcpy(&count, ptr, 4); ptr += 4;

                    m_registry.reserve(count);
                    for (uint32_t i = 0; i < count; ++i)
                    {
                        if (ptr + 4 > end) return false;
                        uint32_t nameLen = 0;
                        std::memcpy(&nameLen, ptr, 4); ptr += 4;
                        if (ptr + nameLen > end) return false;
                        std::string name(ptr, nameLen); ptr += nameLen;

                        if (ptr + 4 > end) return false;
                        uint32_t pathLen = 0;
                        std::memcpy(&pathLen, ptr, 4); ptr += 4;
                        if (ptr + pathLen > end) return false;
                        std::string path(ptr, pathLen); ptr += pathLen;

                        if (ptr + 4 > end) return false;
                        int32_t typeInt = 0;
                        std::memcpy(&typeInt, ptr, 4); ptr += 4;

                        AssetRegistryEntry e;
                        e.name = std::move(name);
                        e.path = std::move(path);
                        e.type = static_cast<AssetType>(typeInt);
                        registerAssetInRegistry(e);
                    }
                    return true;
                }
            }
        }
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

#if ENGINE_EDITOR
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
#endif

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

    m_registryVersion.fetch_add(1, std::memory_order_relaxed);

    #if ENGINE_EDITOR
    // Persist the updated registry to disk (skipped during batch operations)
    if (!m_suppressRegistrySave)
    {
        auto& diagnostics = DiagnosticsManager::Instance();
        if (diagnostics.isProjectLoaded() && !diagnostics.getProjectInfo().projectPath.empty())
        {
            saveAssetRegistry(diagnostics.getProjectInfo().projectPath);
        }
    }
#endif
}

bool AssetManager::discoverAssetsAndBuildRegistry(const std::string& projectRoot)
{
    auto& log = Logger::Instance();
    log.log(Logger::Category::AssetManagement, "[Registry] discoverAssetsAndBuildRegistry projectRoot='" + projectRoot + "'", Logger::LogLevel::INFO);

    m_registry.clear();
    m_registryByPath.clear();
    m_registryByName.clear();

    // Suppress per-entry registry saves during batch discovery
    m_suppressRegistrySave = true;

    fs::path contentRoot = fs::path(projectRoot) / "Content";
    if (!fs::exists(contentRoot))
    {
        log.log(Logger::Category::AssetManagement, "[Registry] ABORT: Content directory does not exist: " + contentRoot.string(), Logger::LogLevel::WARNING);
        return false;
    }
    log.log(Logger::Category::AssetManagement, "[Registry] scanning contentRoot='" + contentRoot.string() + "'", Logger::LogLevel::INFO);

    int filesScanned = 0;
    int filesSkippedNotRegular = 0;
    int filesSkippedExtension = 0;
    int filesSkippedHeaderType = 0;
    int filesRegistered = 0;

    for (const auto& entry : fs::recursive_directory_iterator(contentRoot))
    {
        if (!entry.is_regular_file())
        {
            ++filesSkippedNotRegular;
            continue;
        }

        ++filesScanned;
        const fs::path p = entry.path();
        const std::string ext = p.extension().string();

        // Standalone script/audio files: register by extension without reading asset header
        if (ext == ".py")
        {
            AssetRegistryEntry e;
            e.name = p.stem().string();
            e.type = AssetType::Script;
            e.path = fs::relative(p, contentRoot).generic_string();
            log.log(Logger::Category::AssetManagement, "[Registry]   + registered (script) name='" + e.name + "' path='" + e.path + "'", Logger::LogLevel::INFO);
            registerAssetInRegistry(e);
            ++filesRegistered;
            continue;
        }
        if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac")
        {
            AssetRegistryEntry e;
            e.name = p.stem().string();
            e.type = AssetType::Audio;
            e.path = fs::relative(p, contentRoot).generic_string();
            log.log(Logger::Category::AssetManagement, "[Registry]   + registered (audio) name='" + e.name + "' path='" + e.path + "'", Logger::LogLevel::INFO);
            registerAssetInRegistry(e);
            ++filesRegistered;
            continue;
        }

        if (ext != ".asset" && ext != ".map")
        {
            ++filesSkippedExtension;
            log.log(Logger::Category::AssetManagement, "[Registry]   skip (extension): " + p.string(), Logger::LogLevel::INFO);
            continue;
        }

        AssetType type = AssetType::Unknown;
        std::string name;

        {
            std::ifstream headerIn(p, std::ios::binary);
            if (!headerIn.is_open())
            {
                ++filesSkippedHeaderType;
                log.log(Logger::Category::AssetManagement, "[Registry]   skip (cannot open): " + p.string(), Logger::LogLevel::WARNING);
                continue;
            }
            bool isJson = false;
            if (!readAssetHeader(headerIn, type, name, isJson))
            {
                ++filesSkippedHeaderType;
                log.log(Logger::Category::AssetManagement, "[Registry]   skip (header type failed): " + p.string(), Logger::LogLevel::WARNING);
                continue;
            }
        }
        if (name.empty())
            name = p.stem().string();

        AssetRegistryEntry e;
        e.name = name;
        e.type = type;

        // store a path relative to Content
        fs::path rel = fs::relative(p, contentRoot);
        e.path = rel.generic_string();

        log.log(Logger::Category::AssetManagement, "[Registry]   + registered name='" + e.name + "' path='" + e.path + "' type=" + std::to_string(static_cast<int>(e.type)), Logger::LogLevel::INFO);
        registerAssetInRegistry(e);
        ++filesRegistered;
    }

    // Re-enable per-entry saves now that batch discovery is done
    m_suppressRegistrySave = false;

    log.log(Logger::Category::AssetManagement, "[Registry] discovery complete: scanned=" + std::to_string(filesScanned)
        + " registered=" + std::to_string(filesRegistered)
        + " skippedExtension=" + std::to_string(filesSkippedExtension)
        + " skippedHeaderType=" + std::to_string(filesSkippedHeaderType)
        + " skippedNotRegular=" + std::to_string(filesSkippedNotRegular)
        + " totalRegistry=" + std::to_string(m_registry.size()),
        Logger::LogLevel::INFO);

    return true;
}

void AssetManager::discoverAssetsAndBuildRegistryAsync(const std::string& projectRoot)
{
    auto& diagnostics = DiagnosticsManager::Instance();
    diagnostics.setAssetRegistryReady(false);

    enqueueJob([this, projectRoot]()
    {
        auto& log = Logger::Instance();
        auto& diagnostics = DiagnosticsManager::Instance();

        log.log(Logger::Category::AssetManagement, "[Registry] async discovery started", Logger::LogLevel::INFO);

        // Build into local containers on the worker thread
        std::vector<AssetRegistryEntry> localRegistry;
        std::unordered_map<std::string, size_t> localByPath;
        std::unordered_map<std::string, size_t> localByName;

        const fs::path contentRoot = fs::path(projectRoot) / "Content";
        if (!fs::exists(contentRoot))
        {
            log.log(Logger::Category::AssetManagement, "[Registry] async ABORT: Content directory does not exist: " + contentRoot.string(), Logger::LogLevel::WARNING);
            diagnostics.setAssetRegistryReady(true);
            return;
        }

        int filesScanned = 0;
        int filesRegistered = 0;

        const auto registerLocal = [&](AssetRegistryEntry&& e)
        {
            const size_t idx = localRegistry.size();
            if (!e.path.empty()) localByPath[e.path] = idx;
            if (!e.name.empty()) localByName[e.name] = idx;
            localRegistry.push_back(std::move(e));
            ++filesRegistered;
        };

        for (const auto& entry : fs::recursive_directory_iterator(contentRoot))
        {
            if (!entry.is_regular_file()) continue;

            ++filesScanned;
            const fs::path p = entry.path();
            const std::string ext = p.extension().string();

            if (ext == ".py")
            {
                AssetRegistryEntry e;
                e.name = p.stem().string();
                e.type = AssetType::Script;
                e.path = fs::relative(p, contentRoot).generic_string();
                registerLocal(std::move(e));
                continue;
            }
            if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac")
            {
                AssetRegistryEntry e;
                e.name = p.stem().string();
                e.type = AssetType::Audio;
                e.path = fs::relative(p, contentRoot).generic_string();
                registerLocal(std::move(e));
                continue;
            }

            if (ext != ".asset" && ext != ".map") continue;

            AssetType type = AssetType::Unknown;
            std::string name;
            {
                std::ifstream headerIn(p, std::ios::binary);
                if (!headerIn.is_open()) continue;
                bool isJson = false;
                if (!readAssetHeader(headerIn, type, name, isJson)) continue;
            }
            if (name.empty()) name = p.stem().string();

            AssetRegistryEntry e;
            e.name = name;
            e.type = type;
            e.path = fs::relative(p, contentRoot).generic_string();
            registerLocal(std::move(e));
        }

        log.log(Logger::Category::AssetManagement, "[Registry] async discovery complete: scanned=" + std::to_string(filesScanned)
            + " registered=" + std::to_string(filesRegistered), Logger::LogLevel::INFO);

        // Swap results into the shared registry under lock
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_registry = std::move(localRegistry);
            m_registryByPath = std::move(localByPath);
            m_registryByName = std::move(localByName);
        }

        // Save registry to disk
#if ENGINE_EDITOR
        saveAssetRegistry(projectRoot);
#endif

        diagnostics.setAssetRegistryReady(true);

        // Validate registry entries against disk to catch externally deleted files
#if ENGINE_EDITOR
        const size_t staleCount = validateRegistry();
        if (staleCount > 0)
        {
            log.log(Logger::Category::AssetManagement, "[Registry] Cleaned " + std::to_string(staleCount) + " stale entries after discovery.", Logger::LogLevel::WARNING);
        }
#endif

        log.log(Logger::Category::AssetManagement, "[Registry] async registry ready. Total assets: " + std::to_string(m_registry.size()), Logger::LogLevel::INFO);
    });
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
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_initialized = true;
    }
    startWorkerPool();
	s_nextAssetID = 1;
    AudioManager::Instance().setAudioResolver([](unsigned int assetId) -> std::optional<AudioManager::AudioBufferData>
    {
        auto asset = AssetManager::Instance().getLoadedAssetByID(assetId);
        if (!asset || asset->getType() != AssetType::Audio)
        {
            return std::nullopt;
        }

        const auto& data = asset->getData();
        if (!data.is_object())
        {
            return std::nullopt;
        }

        AudioManager::AudioBufferData bufferData;
        bufferData.channels = data.value("m_channels", 0);
        bufferData.sampleRate = data.value("m_sampleRate", 0);
        bufferData.format = data.value("m_format", 0);
        bufferData.data = data.value("m_data", std::vector<unsigned char>{});
        return bufferData;
    });
    AudioManager::Instance().setAudioReleaseCallback([](unsigned int assetId)
    {
        AssetManager::Instance().releaseAudioAsset(assetId);
    });
#if ENGINE_EDITOR
    ensureEditorWidgetsCreated();
#endif
    return true;
}


void AssetManager::startWorkerPool()
{
    if (m_poolRunning)
        return;

    m_poolStopRequested = false;
    m_poolRunning = true;

    m_poolSize = std::max(2u, std::thread::hardware_concurrency());

    auto& logger = Logger::Instance();
    logger.log(Logger::Category::AssetManagement,
        "Starting asset thread pool with " + std::to_string(m_poolSize) + " threads (hardware_concurrency=" + std::to_string(std::thread::hardware_concurrency()) + ")",
        Logger::LogLevel::INFO);

    m_workerPool.reserve(m_poolSize);
    for (unsigned int i = 0; i < m_poolSize; ++i)
    {
        m_workerPool.emplace_back([this, i]()
        {
            for (;;)
            {
                std::function<void()> job;
                {
                    std::unique_lock<std::mutex> lock(m_jobMutex);
                    m_jobCv.wait(lock, [this]() { return m_poolStopRequested || !m_jobs.empty(); });

                    if (m_poolStopRequested && m_jobs.empty())
                        break;

                    job = std::move(m_jobs.front());
                    m_jobs.pop();
                }

                if (job)
                {
                    job();
                }
            }
        });
    }
}

void AssetManager::stopWorkerPool()
{
    if (!m_poolRunning)
        return;

    {
        std::lock_guard<std::mutex> lock(m_jobMutex);
        m_poolStopRequested = true;
    }
    m_jobCv.notify_all();

    for (auto& t : m_workerPool)
    {
        if (t.joinable())
            t.join();
    }
    m_workerPool.clear();
    m_poolRunning = false;
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
    std::vector<unsigned int> toUnload;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_garbageCollector.collect();

        for (const auto id : m_gcEligibleAssets)
        {
            auto it = m_loadedAssets.find(id);
            if (it == m_loadedAssets.end() || !it->second)
            {
                toUnload.push_back(id);
                continue;
            }

            if (it->second.use_count() > 1)
            {
                continue;
            }

            if (it->second->getType() == AssetType::Audio && AudioManager::Instance().isAssetPlaying(id))
            {
                continue;
            }

            toUnload.push_back(id);
        }
    }

    for (const auto id : toUnload)
    {
        unloadAsset(id);
    }
}

bool AssetManager::unloadAsset(unsigned int assetId)
{
    if (assetId == 0)
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_stateMutex);
    auto it = m_loadedAssets.find(assetId);
    if (it == m_loadedAssets.end())
    {
        return false;
    }
    if (it->second && !it->second->getPath().empty())
    {
        m_loadedAssetsByPath.erase(it->second->getPath());
    }
    m_loadedAssets.erase(it);
    m_gcEligibleAssets.erase(assetId);
    m_garbageCollector.collect();
    return true;
}

bool AssetManager::registerRuntimeResource(const std::shared_ptr<EngineObject>& resource)
{
	std::lock_guard<std::mutex> lock(m_stateMutex);
	if (!m_initialized)
	{
		return false;
	}
	return m_garbageCollector.registerResource(resource);
}

AssetManager::~AssetManager()
{
    stopWorkerPool();
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
	if (object && !object->getPath().empty())
	{
		m_loadedAssetsByPath[object->getPath()] = id;
	}
	s_nextAssetID++;
	return id;
}

int AssetManager::loadAssetAsync(const std::string& path, AssetType type, bool allowGc)
{
	if (path.empty())
	{
		Logger::Instance().log(Logger::Category::AssetManagement, "loadAssetAsync() called with empty path.", Logger::LogLevel::WARNING);
		return 0;
	}

	const std::string normalizedPath = fs::path(path).lexically_normal().string();
	int existingId = 0;
	{
		std::lock_guard<std::mutex> lock(m_stateMutex);
		auto pathIt = m_loadedAssetsByPath.find(path);
		if (pathIt == m_loadedAssetsByPath.end())
		{
			pathIt = m_loadedAssetsByPath.find(normalizedPath);
		}
		if (pathIt != m_loadedAssetsByPath.end())
		{
			existingId = static_cast<int>(pathIt->second);
			if (allowGc)
			{
				m_gcEligibleAssets.insert(pathIt->second);
			}
		}
	}

	int jobId = 0;
	{
		std::lock_guard<std::mutex> lock(m_asyncJobMutex);
		jobId = m_nextAsyncJobId++;
		if (existingId != 0)
		{
			m_finishedAssetJobs[jobId] = existingId;
			return jobId;
		}
		m_runningAssetJobs.insert(jobId);
	}

	enqueueJob([this, normalizedPath, type, allowGc, jobId]()
		{
			const int loadedId = loadAsset(normalizedPath, type, Sync);
			if (allowGc && loadedId != 0)
			{
				markAssetGcEligible(static_cast<unsigned int>(loadedId), true);
			}
			{
				std::lock_guard<std::mutex> lock(m_asyncJobMutex);
				m_runningAssetJobs.erase(jobId);
				m_finishedAssetJobs[jobId] = loadedId;
			}
		});

	return jobId;
}

bool AssetManager::tryConsumeAssetLoadResult(int jobId, int& outAssetId)
{
	std::lock_guard<std::mutex> lock(m_asyncJobMutex);
	auto it = m_finishedAssetJobs.find(jobId);
	if (it == m_finishedAssetJobs.end())
	{
		return false;
	}
	outAssetId = it->second;
	m_finishedAssetJobs.erase(it);
	return true;
}

std::vector<int> AssetManager::getRunningAssetLoadJobs() const
{
	std::lock_guard<std::mutex> lock(m_asyncJobMutex);
	return std::vector<int>(m_runningAssetJobs.begin(), m_runningAssetJobs.end());
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

std::shared_ptr<AssetData> AssetManager::getLoadedAssetByPath(const std::string& path) const
{
	if (path.empty())
		return nullptr;
	std::lock_guard<std::mutex> lock(m_stateMutex);

	auto tryFind = [&](const std::string& key) -> std::shared_ptr<AssetData>
	{
		auto pathIt = m_loadedAssetsByPath.find(key);
		if (pathIt != m_loadedAssetsByPath.end())
		{
			auto assetIt = m_loadedAssets.find(pathIt->second);
			if (assetIt != m_loadedAssets.end())
				return assetIt->second;
		}
		return nullptr;
	};

	if (auto a = tryFind(path)) return a;

	const std::string normalized = fs::path(path).lexically_normal().generic_string();
	if (normalized != path)
	{
		if (auto a = tryFind(normalized)) return a;
	}

	const std::string absPath = getAbsoluteContentPath(path);
	if (!absPath.empty() && absPath != path && absPath != normalized)
	{
		if (auto a = tryFind(absPath)) return a;
	}

	return nullptr;
}

bool AssetManager::isAssetLoaded(const std::string& path) const
{
	if (path.empty())
	{
		return false;
	}

	std::lock_guard<std::mutex> lock(m_stateMutex);
	if (m_loadedAssetsByPath.count(path))
	{
		return true;
	}
	const std::string relPathString = fs::path(path).lexically_normal().generic_string();
	if (m_loadedAssetsByPath.count(relPathString))
	{
		return true;
	}
	const std::string absPath = getAbsoluteContentPath(path);
	if (!absPath.empty() && m_loadedAssetsByPath.count(absPath))
	{
		return true;
	}
	return false;
}

#if ENGINE_EDITOR
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

	auto* level = diagnostics.getActiveLevelSoft();
	if (level)
	{
		logger.log(Logger::Category::AssetManagement, "Saving active level...", Logger::LogLevel::INFO);

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

bool AssetManager::saveActiveLevel()
{
    auto* level = DiagnosticsManager::Instance().getActiveLevelSoft();
    if (!level)
    {
        return false;
    }
    return saveLevelAsset(level).success;
}

size_t AssetManager::getUnsavedAssetCount() const
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    size_t count = 0;
    for (const auto& res : m_loadedAssets)
    {
        if (res.first != 0 && res.second && !res.second->getIsSaved())
        {
            ++count;
        }
    }
    auto& diagnostics = DiagnosticsManager::Instance();
    auto* level = diagnostics.getActiveLevelSoft();
    if (level && !level->getIsSaved())
    {
        ++count;
    }
    return count;
}

std::vector<AssetManager::UnsavedAssetInfo> AssetManager::getUnsavedAssetList() const
{
    std::vector<UnsavedAssetInfo> result;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        for (const auto& res : m_loadedAssets)
        {
            if (res.first != 0 && res.second && !res.second->getIsSaved())
            {
                UnsavedAssetInfo info;
                info.id = res.first;
                info.name = res.second->getName();
                info.path = res.second->getPath();
                info.type = res.second->getAssetType();
                info.isLevel = false;
                result.push_back(std::move(info));
            }
        }
    }
    auto* level = DiagnosticsManager::Instance().getActiveLevelSoft();
    if (level && !level->getIsSaved())
    {
        UnsavedAssetInfo info;
        info.id = 0;
        info.name = level->getName().empty() ? "Active Level" : level->getName();
        info.path = level->getPath();
        info.type = AssetType::Level;
        info.isLevel = true;
        result.push_back(std::move(info));
    }
    return result;
}

void AssetManager::saveAllAssetsAsync(std::function<void(size_t saved, size_t total)> onProgress, std::function<void(bool success)> onFinished)
{
    enqueueJob([this, onProgress = std::move(onProgress), onFinished = std::move(onFinished)]()
    {
        auto& logger = Logger::Instance();
        auto& diagnostics = DiagnosticsManager::Instance();
        diagnostics.setActionInProgress(DiagnosticsManager::ActionType::SavingAsset, true);

        std::vector<std::pair<unsigned int, AssetType>> toSave;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            for (const auto& res : m_loadedAssets)
            {
                if (res.first != 0 && res.second && !res.second->getIsSaved())
                {
                    toSave.push_back({ res.first, res.second->getAssetType() });
                }
            }
        }

        bool hasLevel = false;
        {
            auto* level = diagnostics.getActiveLevelSoft();
            if (level && !level->getIsSaved())
            {
                hasLevel = true;
            }
        }

        const size_t total = toSave.size() + (hasLevel ? 1 : 0);
        size_t saved = 0;
        bool allOk = true;

        for (const auto& [id, type] : toSave)
        {
            Asset asset;
            asset.ID = id;
            asset.type = type;
            if (!saveAsset(asset))
            {
                logger.log(Logger::Category::AssetManagement, "saveAllAssetsAsync: failed asset ID=" + std::to_string(id), Logger::LogLevel::ERROR);
                allOk = false;
            }
            ++saved;
            if (onProgress)
            {
                onProgress(saved, total);
            }
        }

        if (hasLevel)
        {
            auto* level = diagnostics.getActiveLevelSoft();
            if (level)
            {
                if (!saveLevelAsset(level).success)
                {
                    logger.log(Logger::Category::AssetManagement, "saveAllAssetsAsync: failed to save active level", Logger::LogLevel::ERROR);
                    allOk = false;
                }
            }
            ++saved;
            if (onProgress)
            {
                onProgress(saved, total);
            }
        }

        diagnostics.setActionInProgress(DiagnosticsManager::ActionType::SavingAsset, false);
        if (onFinished)
        {
            onFinished(allOk);
        }
    });
}

void AssetManager::saveSelectedAssetsAsync(const std::vector<unsigned int>& selectedIds, bool includeLevel,
    std::function<void(size_t saved, size_t total)> onProgress,
    std::function<void(bool success)> onFinished)
{
    enqueueJob([this, selectedIds, includeLevel, onProgress = std::move(onProgress), onFinished = std::move(onFinished)]()
    {
        auto& logger = Logger::Instance();
        auto& diagnostics = DiagnosticsManager::Instance();
        diagnostics.setActionInProgress(DiagnosticsManager::ActionType::SavingAsset, true);

        std::vector<std::pair<unsigned int, AssetType>> toSave;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            for (unsigned int id : selectedIds)
            {
                if (id == 0) continue; // level is handled separately
                auto it = m_loadedAssets.find(id);
                if (it != m_loadedAssets.end() && it->second)
                {
                    toSave.push_back({ id, it->second->getAssetType() });
                }
            }
        }

        const size_t total = toSave.size() + (includeLevel ? 1 : 0);
        size_t saved = 0;
        bool allOk = true;

        for (const auto& [id, type] : toSave)
        {
            Asset asset;
            asset.ID = id;
            asset.type = type;
            if (!saveAsset(asset))
            {
                logger.log(Logger::Category::AssetManagement, "saveSelectedAssetsAsync: failed asset ID=" + std::to_string(id), Logger::LogLevel::ERROR);
                allOk = false;
            }
            ++saved;
            if (onProgress)
                onProgress(saved, total);
        }

        if (includeLevel)
        {
            auto* level = diagnostics.getActiveLevelSoft();
            if (level)
            {
                if (!saveLevelAsset(level).success)
                {
                    logger.log(Logger::Category::AssetManagement, "saveSelectedAssetsAsync: failed to save active level", Logger::LogLevel::ERROR);
                    allOk = false;
                }
            }
            ++saved;
            if (onProgress)
                onProgress(saved, total);
        }

        diagnostics.setActionInProgress(DiagnosticsManager::ActionType::SavingAsset, false);
        if (onFinished)
            onFinished(allOk);
    });
}
#endif

std::string AssetManager::getAbsoluteContentPath(const std::string& relativeToContent) const
{
    const auto& diagnostics = DiagnosticsManager::Instance();
    if (!diagnostics.isProjectLoaded() || diagnostics.getProjectInfo().projectPath.empty())
    {
        return {};
    }

    fs::path inputPath(relativeToContent);
    if (inputPath.is_absolute())
    {
        return inputPath.lexically_normal().string();
    }

    const fs::path projectRoot(diagnostics.getProjectInfo().projectPath);
    if (!relativeToContent.empty())
    {
        auto it = inputPath.begin();
        if (it != inputPath.end() && *it == "Content")
        {
            return (projectRoot / inputPath).lexically_normal().string();
        }
    }

    return (projectRoot / "Content" / inputPath).lexically_normal().string();
}

std::string AssetManager::getAbsoluteEngineContentPath(const std::string& relativeToContent) const
{
    fs::path inputPath(relativeToContent);
    if (inputPath.is_absolute())
    {
        return inputPath.lexically_normal().string();
    }
    return (fs::current_path() / "Content" / inputPath).lexically_normal().string();
}

bool AssetManager::mountContentArchive(const std::string& hpkPath)
{
    unmountContentArchive();
    m_hpkReader = std::make_unique<HPKReader>();
    if (!m_hpkReader->mount(hpkPath))
    {
        m_hpkReader.reset();
        return false;
    }
    HPKReader::SetMounted(m_hpkReader.get());
    return true;
}

void AssetManager::unmountContentArchive()
{
    if (m_hpkReader)
    {
        m_hpkReader->unmount();
        m_hpkReader.reset();
    }
}

bool AssetManager::isContentArchiveMounted() const
{
    return m_hpkReader && m_hpkReader->isMounted();
}

#if ENGINE_EDITOR
std::string AssetManager::getEditorWidgetPath(const std::string& relativeToEditorWidgets) const
{
    fs::path p = getEditorWidgetsRootPath() / fs::path(relativeToEditorWidgets);
    return p.lexically_normal().string();
}
#endif

int AssetManager::loadAudioFromContentPath(const std::string& relativeToContent, bool allowGc)
{
    if (relativeToContent.empty())
    {
        return 0;
    }

    const std::string absPath = getAbsoluteContentPath(relativeToContent);
    if (absPath.empty())
    {
        Logger::Instance().log(Logger::Category::AssetManagement, "Audio load failed: project not loaded.", Logger::LogLevel::ERROR);
        return 0;
    }

    fs::path relPath = fs::path(relativeToContent).lexically_normal();
    const std::string relPathString = relPath.generic_string();
    const std::string extension = relPath.extension().string();

    if (extension == ".asset")
    {
        return loadAsset(absPath, AssetType::Audio, Sync);
    }

    if (extension != ".wav")
    {
        Logger::Instance().log(Logger::Category::AssetManagement, "Audio load failed: unsupported extension for " + relPathString, Logger::LogLevel::ERROR);
        return 0;
    }

    SDL_AudioSpec spec{};
    Uint8* buffer = nullptr;
    Uint32 length = 0;
    if (!SDL_LoadWAV(absPath.c_str(), &spec, &buffer, &length))
    {
        Logger::Instance().log(Logger::Category::AssetManagement, "Audio load failed: " + absPath, Logger::LogLevel::ERROR);
        return 0;
    }

    auto audio = std::make_shared<AssetData>();
    json audioData = json::object();
    audioData["m_channels"] = static_cast<int>(spec.channels);
    audioData["m_sampleRate"] = static_cast<int>(spec.freq);
    audioData["m_format"] = static_cast<int>(spec.format);
    audioData["m_data"] = std::vector<unsigned char>(buffer, buffer + length);
    SDL_free(buffer);

    audio->setName(relPath.stem().string());
    audio->setPath(relPathString);
    audio->setAssetType(AssetType::Audio);
    audio->setType(AssetType::Audio);
    audio->setData(std::move(audioData));
    audio->setIsSaved(true);

    auto id = registerLoadedAsset(audio);
    if (id != 0)
    {
        audio->setId(id);
    }
    m_garbageCollector.registerResource(audio);
    if (allowGc && id != 0)
    {
        markAssetGcEligible(id, true);
    }
    return static_cast<int>(id);
}

void AssetManager::markAssetGcEligible(unsigned int id, bool eligible)
{
    if (id == 0)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (eligible)
    {
        m_gcEligibleAssets.insert(id);
    }
    else
    {
        m_gcEligibleAssets.erase(id);
    }
}

void AssetManager::releaseAudioAsset(unsigned int assetId)
{
    if (assetId == 0)
    {
        return;
    }

    if (AudioManager::Instance().isAssetPlaying(assetId))
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (m_gcEligibleAssets.find(assetId) == m_gcEligibleAssets.end())
        {
            return;
        }
    }

    unloadAsset(assetId);
}

bool AssetManager::isAudioPlayingContentPath(const std::string& relativeToContent) const
{
    if (relativeToContent.empty())
    {
        return false;
    }

    const std::string absPath = getAbsoluteContentPath(relativeToContent);
    fs::path relPath = fs::path(relativeToContent).lexically_normal();
    const std::string relPathString = relPath.generic_string();

    unsigned int assetId = 0;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        for (const auto& [id, asset] : m_loadedAssets)
        {
            if (!asset || asset->getType() != AssetType::Audio)
            {
                continue;
            }
            const std::string& assetPath = asset->getPath();
            if (assetPath == relPathString || (!absPath.empty() && assetPath == absPath))
            {
                assetId = id;
                break;
            }
        }
    }

    if (assetId == 0)
    {
        return false;
    }

    return AudioManager::Instance().isAssetPlaying(assetId);
}

int AssetManager::loadAsset(const std::string& path, AssetType type, SyncState syncState)
{
	if (path.empty())
	{
		return 0;
	}

	{
		std::lock_guard<std::mutex> lock(m_stateMutex);
		auto pathIt = m_loadedAssetsByPath.find(path);
		if (pathIt != m_loadedAssetsByPath.end())
		{
			return static_cast<int>(pathIt->second);
		}
	}

	if (syncState == Async)
	{
		enqueueJob([this, path, type]()
			{
				switch (type)
				{
				case AssetType::Model3D:
				case AssetType::PointLight:
					loadObject3DAsset(path);
					break;
				case AssetType::Model2D:
					loadObject2DAsset(path);
					break;
				case AssetType::Texture:
					loadTextureAsset(path);
					break;
			case AssetType::Audio:
				loadAudioAsset(path);
				break;
				case AssetType::Material:
					loadMaterialAsset(path);
					break;
						case AssetType::Level:
								loadLevelAsset(path);
								break;
							case AssetType::Skybox:
								loadSkyboxAsset(path);
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
	case AssetType::PointLight:
		result = loadObject3DAsset(path);
		break;
	case AssetType::Model2D:
		result = loadObject2DAsset(path);
		break;
	case AssetType::Texture:
		result = loadTextureAsset(path);
		break;
	case AssetType::Audio:
		result = loadAudioAsset(path);
		break;
	case AssetType::Material:
		result = loadMaterialAsset(path);
		break;
	case AssetType::Level:
		result = loadLevelAsset(path);
		break;
	case AssetType::Widget:
		result = loadWidgetAsset(path);
		break;
	case AssetType::Skybox:
		result = loadSkyboxAsset(path);
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
		auto pathIt = m_loadedAssetsByPath.find(path);
		if (pathIt != m_loadedAssetsByPath.end())
		{
			return static_cast<int>(pathIt->second);
		}
		const std::string normalized = fs::path(path).lexically_normal().generic_string();
		if (normalized != path)
		{
			pathIt = m_loadedAssetsByPath.find(normalized);
			if (pathIt != m_loadedAssetsByPath.end())
			{
				return static_cast<int>(pathIt->second);
			}
		}
	}

	return 0;
}

#if ENGINE_EDITOR
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
	case AssetType::PointLight:
			result = saveObject3DAsset(assetData);
			break;
        case AssetType::Texture:
			result = saveTextureAsset(assetData);
            break;
        case AssetType::Material:
			result = saveMaterialAsset(assetData);
            break;
	case AssetType::Widget:
		result = saveWidgetAsset(assetData);
		break;
	case AssetType::Audio:
		result = saveAudioAsset(assetData);
		break;
	case AssetType::Skybox:
		result = saveSkyboxAsset(assetData);
		break;
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

#endif

bool AssetManager::loadProject(const std::string& projectPath, SyncState syncState, bool ensureDefaultContent)
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
    bool isPackagedBuild = false;
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
            // No .project file on disk â€” check if this is a packaged build
            // (content.hpk exists). Runtime builds don't need a .project file;
            // all essential info comes from game.ini / compile-time defines.
            const fs::path hpkCandidateNew = root / "Content" / "content.hpk";
            const fs::path hpkCandidateLegacy = root / "content.hpk";
            if (fs::exists(hpkCandidateNew) || fs::exists(hpkCandidateLegacy))
            {
                isPackagedBuild = true;
                logger.log(Logger::Category::Project,
                    "No .project file found, but content.hpk detected â€” treating as packaged build.",
                    Logger::LogLevel::INFO);
            }
            else
            {
                logger.log("No .project file found in: " + root.string(), Logger::LogLevel::ERROR);
                diagnostics.setActionInProgress(DiagnosticsManager::ActionType::LoadingProject, false);
                return false;
            }
        }
    }
    else if (projectFile.extension() != ".project")
    {
        logger.log("Invalid project file: " + projectFile.string(), Logger::LogLevel::ERROR);
        diagnostics.setActionInProgress(DiagnosticsManager::ActionType::LoadingProject, false);
        return false;
    }

    DiagnosticsManager::ProjectInfo info;
    info.projectPath = root.string();
    info.selectedRHI = DiagnosticsManager::RHIType::Unknown;

    if (isPackagedBuild)
    {
        // Derive a project name from the directory
        info.projectName = root.filename().string();
        if (info.projectName.empty())
            info.projectName = "Game";
        logger.log(Logger::Category::Project,
            "Packaged build project info: name=" + info.projectName + " path=" + info.projectPath,
            Logger::LogLevel::INFO);
    }
    else
    {
        info.projectName = projectFile.stem().string();

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
            else if (key == "ScriptingMode")
            {
                info.scriptingMode = DiagnosticsManager::scriptingModeFromString(value);
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
    }

    // Project file fully parsed (or packaged build defaults) - apply it.
    diagnostics.setProjectInfo(info);

    // New project/level context: force re-prepare of renderer resources.
    diagnostics.setScenePrepared(false);

	{
#if ENGINE_EDITOR
		const fs::path scriptsRoot = fs::path(info.projectPath) / "Content" / "Scripts";
		std::error_code scriptsEc;
		fs::create_directories(scriptsRoot, scriptsEc);
		const fs::path stubPath = scriptsRoot / "engine.pyi";
		const fs::path srcStub = fs::current_path() / "Content" / "Scripting" / "engine.pyi";
		fs::copy_file(srcStub, stubPath, fs::copy_options::overwrite_existing, scriptsEc);
#endif
	}

#if ENGINE_EDITOR
	// Ensure default assets if requested (new-project flow may opt out for blank projects).
	if (ensureDefaultContent)
	{
		try
		{
			ensureDefaultAssetsCreated();
		}
		catch (const std::exception& ex)
		{
			logger.log(Logger::Category::Project,
				std::string("Exception while ensuring default assets during project load: ") + ex.what(),
				Logger::LogLevel::ERROR);
			diagnostics.setActionInProgress(DiagnosticsManager::ActionType::LoadingProject, false);
			return false;
		}
		catch (...)
		{
			logger.log(Logger::Category::Project,
				"Unknown exception while ensuring default assets during project load.",
				Logger::LogLevel::ERROR);
			diagnostics.setActionInProgress(DiagnosticsManager::ActionType::LoadingProject, false);
			return false;
		}
	}
#endif

	// Try to mount an HPK content archive (packaged builds).
	// New layout: Content/content.hpk; legacy fallback: content.hpk in root.
	{
		fs::path hpkPath = fs::path(info.projectPath) / "Content" / "content.hpk";
		if (!fs::exists(hpkPath))
			hpkPath = fs::path(info.projectPath) / "content.hpk";
		if (fs::exists(hpkPath))
		{
			if (mountContentArchive(hpkPath.string()))
				logger.log(Logger::Category::Project, "Mounted content archive: " + hpkPath.string(), Logger::LogLevel::INFO);
			else
				logger.log(Logger::Category::Project, "Failed to mount content archive: " + hpkPath.string(), Logger::LogLevel::WARNING);
		}
	}

	// Registry handling.
	if (isPackagedBuild)
	{
		// Packaged runtime: load the pre-built binary registry (from HPK or disk).
		// A full filesystem discovery is pointless because the Content/ directory
		// has been removed and packed into content.hpk.
		logger.log(Logger::Category::Project,
			"Packaged build: loading asset registry from binary (no filesystem discovery).",
			Logger::LogLevel::INFO);
		if (loadAssetRegistry(info.projectPath))
		{
			logger.log(Logger::Category::Project,
				"Asset registry loaded: " + std::to_string(m_registry.size()) + " entries.",
				Logger::LogLevel::INFO);
			DiagnosticsManager::Instance().setAssetRegistryReady(true);
		}
		else
		{
			logger.log(Logger::Category::Project,
				"Failed to load asset registry for packaged build.",
				Logger::LogLevel::WARNING);
			DiagnosticsManager::Instance().setAssetRegistryReady(true);
		}
	}
	else
	{
		// Editor / dev mode: discover assets from the Content/ folder on disk.
		logger.log(Logger::Category::Project,
			"Starting async asset registry build for project: " + info.projectName,
			Logger::LogLevel::INFO);
		discoverAssetsAndBuildRegistryAsync(info.projectPath);
	}

    if (!diagnostics.loadProjectConfig())
    {
        // HPK fallback: read defaults.ini from the mounted archive
        bool loaded = false;
        auto* hpk = HPKReader::GetMounted();
        if (hpk)
        {
            std::filesystem::path cfgPath = std::filesystem::path(info.projectPath) / "Config" / "defaults.ini";
            std::string vpath = hpk->makeVirtualPath(cfgPath.string());
            if (!vpath.empty())
            {
                auto buf = hpk->readFile(vpath);
                if (buf && !buf->empty())
                {
                    loaded = diagnostics.loadProjectConfigFromString(
                        std::string(buf->data(), buf->size()));
                }
            }
        }
        if (!loaded)
            logger.log("Failed to load project config.", Logger::LogLevel::ERROR);
    }

    diagnostics.setActionInProgress(DiagnosticsManager::ActionType::LoadingProject, false);
    logger.log(Logger::Category::Project, "Project loaded: " + info.projectName, Logger::LogLevel::INFO);
    return true;
}

void AssetManager::unloadAllAssets()
{
	auto& logger = Logger::Instance();
	logger.log(Logger::Category::AssetManagement, "Unloading all cached assets.", Logger::LogLevel::INFO);
	{
		std::lock_guard<std::mutex> lock(m_stateMutex);
		m_loadedAssets.clear();
		m_loadedAssetsByPath.clear();
		m_gcEligibleAssets.clear();
		s_nextAssetID = 1;
	}
	m_garbageCollector.clear();
}

#if ENGINE_EDITOR
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
    fs::create_directories(root / "Content" / "Scripts", ec);
    fs::create_directories(root / "Content" / "Scripts");

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
    out << "ScriptingMode=" << DiagnosticsManager::scriptingModeToString(info.scriptingMode) << "\n";
    out << "Level=" << levelPathToSave << "\n";

    diagnostics.setActionInProgress(DiagnosticsManager::ActionType::SavingProject, false);
    logger.log(Logger::Category::Project, "Project saved: " + projectFile.string(), Logger::LogLevel::INFO);
    return true;
}

bool AssetManager::createProject(const std::string& parentDir, const std::string& projectName, const DiagnosticsManager::ProjectInfo& info, SyncState syncState, bool includeDefaultContent)
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

	{
		const fs::path scriptsDir = root / "Content" / "Scripts";
		fs::create_directories(scriptsDir, ec);
		const fs::path stubPath = scriptsDir / "engine.pyi";
		const fs::path srcStub = fs::current_path() / "Content" / "Scripting" / "engine.pyi";
		std::error_code copyEc;
		fs::copy_file(srcStub, stubPath, fs::copy_options::overwrite_existing, copyEc);
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
    defaultlevel->setName("DefaultLevel");
    defaultlevel->setPath(defaultLevelPath);
    defaultlevel->setAssetType(AssetType::Level);
    diagnostics.setActiveLevel(std::move(defaultlevel));

    // ensure defaults.ini exists for the new project
    diagnostics.loadProjectConfig();

    if (!includeDefaultContent)
    {
        if (auto level = diagnostics.getActiveLevel())
        {
            saveLevelAsset(level);
        }
    }

    bool saved = saveProject(root.string());
    diagnostics.setActionInProgress(DiagnosticsManager::ActionType::LoadingProject, false);
    if (saved)
    {
        logger.log("Project created at: " + root.string(), Logger::LogLevel::INFO);
    }
	bool loaded = loadProject(root.string(), syncState, includeDefaultContent);
    if (!loaded)
    {
        logger.log("Failed to load newly created project: " + root.string(), Logger::LogLevel::ERROR);
        return false;
	}
    return loaded;
}
#endif

AssetManager::LoadResult AssetManager::loadTextureAsset(const std::string& path)
{
	LoadResult result;
	auto raw = readAssetFromDisk(path, AssetType::Texture);
	if (!raw.success)
	{
		result.errorMessage = raw.errorMessage;
		return result;
	}
	result.j = raw.data;
	auto id = finalizeAssetLoad(std::move(raw));
	result.success = (id != 0);
	if (!result.success)
	{
		result.errorMessage = "Failed to register texture asset.";
	}
	return result;
}

AssetManager::LoadResult AssetManager::loadAudioAsset(const std::string& path)
{
	LoadResult result;
	auto raw = readAssetFromDisk(path, AssetType::Audio);
	if (!raw.success)
	{
		result.errorMessage = raw.errorMessage;
		return result;
	}
	result.j = raw.data;
	auto id = finalizeAssetLoad(std::move(raw));
	result.success = (id != 0);
	if (!result.success)
	{
		result.errorMessage = "Failed to register audio asset.";
	}
	return result;
}

AssetManager::LoadResult AssetManager::loadWidgetAsset(const std::string& path)
{
	LoadResult result;
	auto raw = readAssetFromDisk(path, AssetType::Widget);

	// If the file was not found and the path is relative, resolve against the
	// project Content directory (the registry stores Content-relative paths but
	// readAssetFromDisk needs a path that std::ifstream can open).
	if (!raw.success && !fs::path(path).is_absolute())
	{
		auto& diagnostics = DiagnosticsManager::Instance();
		if (diagnostics.isProjectLoaded())
		{
			const auto& projectPath = diagnostics.getProjectInfo().projectPath;
			if (!projectPath.empty())
			{
				const std::string resolved =
					(fs::path(projectPath) / "Content" / path).lexically_normal().string();
				raw = readAssetFromDisk(resolved, AssetType::Widget);
				if (raw.success)
				{
					// Keep the original Content-relative path so the cache key
					// matches what callers and the asset registry use.
					raw.path = path;
				}
			}
		}
	}

	if (!raw.success)
	{
		result.errorMessage = raw.errorMessage;
		return result;
	}
	result.j = raw.data;
	auto id = finalizeAssetLoad(std::move(raw));
	result.success = (id != 0);
	if (!result.success)
	{
		result.errorMessage = "Failed to register widget asset.";
	}
	return result;
}

AssetManager::LoadResult AssetManager::loadMaterialAsset(const std::string& path)
{
	LoadResult result;
	auto raw = readAssetFromDisk(path, AssetType::Material);
	if (!raw.success)
	{
		result.errorMessage = raw.errorMessage;
		return result;
	}
	result.j = raw.data;
	auto id = finalizeAssetLoad(std::move(raw));
	result.success = (id != 0);
	if (!result.success)
	{
		result.errorMessage = "Failed to register material asset.";
	}
	return result;
}

AssetManager::LoadResult AssetManager::loadObject2DAsset(const std::string& path)
{
	LoadResult result;
	auto raw = readAssetFromDisk(path, AssetType::Model2D);
	if (!raw.success)
	{
		result.errorMessage = raw.errorMessage;
		return result;
	}
	result.j = raw.data;
	auto id = finalizeAssetLoad(std::move(raw));
	result.success = (id != 0);
	if (!result.success)
	{
		result.errorMessage = "Failed to register Object2D asset.";
	}
	return result;
}

AssetManager::LoadResult AssetManager::loadObject3DAsset(const std::string& path)
{
	LoadResult result;
	auto raw = readAssetFromDisk(path, AssetType::Model3D);
	if (!raw.success)
	{
		// HPK fallback for cooked-only meshes (.cooked exists but .asset does not)
		auto* hpk = HPKReader::GetMounted();
		if (hpk)
		{
			fs::path cookedPath = fs::path(path);
			cookedPath.replace_extension(".cooked");
			std::string vpath = hpk->makeVirtualPath(cookedPath.string());
			if (!vpath.empty() && hpk->contains(vpath))
			{
				// Create a minimal stub asset â€“ OpenGLObject3D::prepare() will load the
				// CMSH binary via FindCookedMeshPath / LoadCookedMesh.
				raw.data = json::object();
				raw.name = fs::path(path).stem().string();
				raw.path = path;
				raw.type = AssetType::Model3D;
				raw.success = true;
				raw.errorMessage.clear();
				Logger::Instance().log(Logger::Category::AssetManagement,
					"loadObject3DAsset: using cooked mesh from HPK for '" + path + "'",
					Logger::LogLevel::INFO);
			}
		}
		if (!raw.success)
		{
			result.errorMessage = raw.errorMessage;
			return result;
		}
	}
	result.j = raw.data;
	auto id = finalizeAssetLoad(std::move(raw));
	result.success = (id != 0);
	if (!result.success)
	{
		result.errorMessage = "Failed to register Object3D asset.";
	}
	return result;
}

AssetManager::LoadResult AssetManager::loadLevelAsset(const std::string& path)
{
	LoadResult result;
	auto& logger = Logger::Instance();

	std::ifstream in(path, std::ios::binary);
	if (!in.is_open())
	{
		// HPK fallback: try reading the level file from the mounted archive
		auto* hpk = HPKReader::GetMounted();
		if (hpk)
		{
			std::string vpath = hpk->makeVirtualPath(path);
			logger.log(Logger::Category::AssetManagement,
				"HPK level fallback: file=" + path + " vpath=" + (vpath.empty() ? "(empty)" : vpath),
				Logger::LogLevel::INFO);
			if (!vpath.empty())
			{
				auto hpkBuf = hpk->readFile(vpath);
				if (hpkBuf && !hpkBuf->empty())
				{
					logger.log(Logger::Category::AssetManagement,
						"HPK level loaded: " + vpath + " (" + std::to_string(hpkBuf->size()) + " bytes)",
						Logger::LogLevel::INFO);

					AssetType headerType{ AssetType::Unknown };
					std::string name;
					bool isJson = false;
					bool isMsgPack = false;
					size_t headerEndPos = 0;
					if (!readAssetHeaderFromMemory(*hpkBuf, headerType, name, isJson, headerEndPos, isMsgPack))
					{
						result.errorMessage = "Invalid level asset header (HPK).";
						return result;
					}
					if (headerType != AssetType::Level)
					{
						result.errorMessage = "Asset type mismatch for level (HPK).";
						return result;
					}
					if (!readAssetJsonFromMemory(*hpkBuf, result.j, result.errorMessage, isJson, headerEndPos, isMsgPack))
					{
						return result;
					}
					if (name.empty())
						name = fs::path(path).stem().string();

					auto level = std::make_unique<EngineLevel>();
					level->setName(name);
					level->setAssetType(headerType);
					level->setIsSaved(true);
					level->setLevelData(result.j);

					auto& diagnostics = DiagnosticsManager::Instance();
					const fs::path contentRoot = fs::path(diagnostics.getProjectInfo().projectPath) / "Content";
					fs::path absLevel = fs::absolute(fs::path(path));
					std::error_code relEc;
					fs::path relPath = fs::relative(absLevel, contentRoot, relEc);
					if (relEc || relPath.empty() || relPath.string().find("..") == 0)
						level->setPath(path);
					else
						level->setPath(relPath.generic_string());

					diagnostics.setActiveLevel(std::move(level));
					diagnostics.setScenePrepared(false);
					result.success = true;
					return result;
				}
			}
		}
		else
		{
			logger.log(Logger::Category::AssetManagement,
				"HPK not mounted when loading level: " + path, Logger::LogLevel::WARNING);
		}
		result.errorMessage = "Failed to open level asset file: " + path;
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
	level->setAssetType(headerType);
	level->setIsSaved(true);
	level->setLevelData(result.j);

	// Store a Content-relative path so saveLevelAsset can reconstruct the
	// absolute path from projectPath / "Content" / relPath.
	auto& diagnostics = DiagnosticsManager::Instance();
	const fs::path contentRoot = fs::path(diagnostics.getProjectInfo().projectPath) / "Content";
	fs::path absLevel = fs::absolute(fs::path(path));
	std::error_code relEc;
	fs::path relPath = fs::relative(absLevel, contentRoot, relEc);
	if (relEc || relPath.empty() || relPath.string().find("..") == 0)
	{
		level->setPath(path);
	}
	else
	{
		level->setPath(relPath.generic_string());
	}

	diagnostics.setActiveLevel(std::move(level));
	diagnostics.setScenePrepared(false);
	result.success = true;
	return result;
}

#if ENGINE_EDITOR
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

AssetManager::SaveResult AssetManager::saveAudioAsset(const std::shared_ptr<AssetData>& audio)
{
	SaveResult result;
	if (!audio)
	{
		result.errorMessage = "Audio asset is null.";
		return result;
	}

	auto& diagnostics = DiagnosticsManager::Instance();
	if (!diagnostics.isProjectLoaded())
	{
		result.errorMessage = "No project loaded for audio save.";
		return result;
	}

	std::string name = audio->getName();
	if (name.empty())
	{
		name = "Audio";
		audio->setName(name);
	}

	std::string relPath = audio->getPath();
	if (relPath.empty())
	{
		relPath = name + ".asset";
		audio->setPath(relPath);
	}

	fs::path rel = fs::path(relPath);
	if (rel.extension().empty())
	{
		rel.replace_extension(".asset");
		relPath = rel.generic_string();
		audio->setPath(relPath);
	}

	const fs::path absPath = fs::path(diagnostics.getProjectInfo().projectPath) / "Content" / fs::path(relPath);
	std::error_code ec;
	fs::create_directories(absPath.parent_path(), ec);

	std::ofstream out(absPath, std::ios::out | std::ios::trunc);
	if (!out.is_open())
	{
		result.errorMessage = "Failed to open audio asset file for writing.";
		return result;
	}

	json data = audio->getData();
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
	fileJson["type"] = static_cast<int>(AssetType::Audio);
	fileJson["name"] = name;
	fileJson["data"] = dataToSave;

	out << fileJson.dump(4);
	if (!out.good())
	{
		result.errorMessage = "Failed to write audio asset data.";
		return result;
	}

	audio->setIsSaved(true);
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
    AssetType objectType = object3D->getAssetType();
    if (objectType != AssetType::PointLight)
    {
        objectType = AssetType::Model3D;
    }
    fileJson["type"] = static_cast<int>(objectType);
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
	return saveLevelAsset(level.get());
}

AssetManager::SaveResult AssetManager::saveLevelAsset(EngineLevel* level)
{
	SaveResult result;
	if (!level)
	{
		result.errorMessage = "Level asset is null.";
		return result;
	}

	// Skip runtime-only levels (e.g. Mesh Viewer preview scenes)
	if (level->getName().rfind("__MeshViewer__", 0) == 0)
	{
		result.success = true;
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
	if (!level->getLevelScriptPath().empty())
	{
		levelJson["Script"] = level->getLevelScriptPath();
	}
	if (level->hasEditorCamera())
	{
		json camJson = json::object();
		const auto& pos = level->getEditorCameraPosition();
		camJson["position"] = json::array({ pos.x, pos.y, pos.z });
		const auto& rot = level->getEditorCameraRotation();
		camJson["rotation"] = json::array({ rot.x, rot.y });
		levelJson["EditorCamera"] = camJson;
	}
	if (!level->getSkyboxPath().empty())
	{
		levelJson["Skybox"] = level->getSkyboxPath();
	}

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

AssetManager::SaveResult AssetManager::saveWidgetAsset(const std::shared_ptr<AssetData>& widget)
{
    SaveResult result;
    if (!widget)
    {
        result.errorMessage = "Widget asset is null.";
        return result;
    }

    auto& diagnostics = DiagnosticsManager::Instance();
    if (!diagnostics.isProjectLoaded())
    {
        result.errorMessage = "No project loaded for widget save.";
        return result;
    }

    std::string name = widget->getName();
    if (name.empty())
    {
        name = "Widget";
        widget->setName(name);
    }

    std::string relPath = widget->getPath();
    if (relPath.empty())
    {
        relPath = name + ".asset";
        widget->setPath(relPath);
    }

    fs::path rel = fs::path(relPath);
    if (rel.extension().empty())
    {
        rel.replace_extension(".asset");
        relPath = rel.generic_string();
        widget->setPath(relPath);
    }

    const fs::path absPath = fs::path(diagnostics.getProjectInfo().projectPath) / "Content" / fs::path(relPath);
    std::error_code ec;
    fs::create_directories(absPath.parent_path(), ec);

    std::ofstream out(absPath, std::ios::out | std::ios::trunc);
    if (!out.is_open())
    {
        result.errorMessage = "Failed to open widget asset file for writing.";
        return result;
    }

    json data = widget->getData();
    if (data.is_null())
    {
        data = json::object();
    }

    json fileJson = json::object();
    fileJson["magic"] = 0x41535453;
    fileJson["version"] = 2;
    fileJson["type"] = static_cast<int>(AssetType::Widget);
    fileJson["name"] = name;
    fileJson["data"] = data;

    out << fileJson.dump(4);
    if (!out.good())
    {
        result.errorMessage = "Failed to write widget asset data.";
        return result;
    }

    widget->setIsSaved(true);
    result.success = true;
    return result;
}

AssetManager::SaveResult AssetManager::saveSkyboxAsset(const std::shared_ptr<AssetData>& skybox)
{
    SaveResult result;
    if (!skybox)
    {
        result.errorMessage = "Skybox asset is null.";
        return result;
    }

    auto& diagnostics = DiagnosticsManager::Instance();
    if (!diagnostics.isProjectLoaded())
    {
        result.errorMessage = "No project loaded for skybox save.";
        return result;
    }

    std::string name = skybox->getName();
    if (name.empty())
    {
        name = "Skybox";
        skybox->setName(name);
    }

    std::string relPath = skybox->getPath();
    if (relPath.empty())
    {
        relPath = "Skyboxes/" + name + ".asset";
        skybox->setPath(relPath);
    }

    fs::path rel = fs::path(relPath);
    if (rel.extension().empty())
    {
        rel.replace_extension(".asset");
        relPath = rel.generic_string();
        skybox->setPath(relPath);
    }

    const fs::path absPath = fs::path(diagnostics.getProjectInfo().projectPath) / "Content" / fs::path(relPath);
    std::error_code ec;
    fs::create_directories(absPath.parent_path(), ec);

    std::ofstream out(absPath, std::ios::out | std::ios::trunc);
    if (!out.is_open())
    {
        result.errorMessage = "Failed to open skybox asset file for writing.";
        return result;
    }

    json data = skybox->getData();
    if (data.is_null())
    {
        data = json::object();
    }

    json fileJson = json::object();
    fileJson["magic"] = 0x41535453;
    fileJson["version"] = 2;
    fileJson["type"] = static_cast<int>(AssetType::Skybox);
    fileJson["name"] = name;
    fileJson["data"] = data;

    out << fileJson.dump(4);
    if (!out.good())
    {
        result.errorMessage = "Failed to write skybox asset data.";
        return result;
    }

    skybox->setIsSaved(true);
    result.success = true;
    return result;
}
#endif

AssetManager::LoadResult AssetManager::loadSkyboxAsset(const std::string& path)
{
    LoadResult result;
    auto raw = readAssetFromDisk(path, AssetType::Skybox);
    if (!raw.success)
    {
        result.errorMessage = raw.errorMessage;
        return result;
    }
    result.j = raw.data;
    auto id = finalizeAssetLoad(std::move(raw));
    result.success = (id != 0);
    if (!result.success)
    {
        result.errorMessage = "Failed to register skybox asset.";
    }
    return result;
}

#if ENGINE_EDITOR
AssetManager::CreateResult AssetManager::createSkyboxAsset(const std::string& path, const std::string& name, const std::string& sourcePath)
{
    CreateResult result;
    auto& diagnostics = DiagnosticsManager::Instance();
    if (!diagnostics.isProjectLoaded())
    {
        result.errorMessage = "No project loaded.";
        return result;
    }

    auto skybox = std::make_shared<AssetData>();
    skybox->setName(name);
    skybox->setAssetType(AssetType::Skybox);
    skybox->setType(AssetType::Skybox);

    json data = json::object();
    data["faces"] = json::object();
    data["faces"]["right"]  = "";
    data["faces"]["left"]   = "";
    data["faces"]["top"]    = "";
    data["faces"]["bottom"] = "";
    data["faces"]["front"]  = "";
    data["faces"]["back"]   = "";

    if (!sourcePath.empty())
    {
        // sourcePath is a directory containing face images
        const fs::path srcDir(sourcePath);
        const fs::path contentDir = fs::path(diagnostics.getProjectInfo().projectPath) / "Content";
        const fs::path skyboxDir = contentDir / "Skyboxes" / name;
        std::error_code ec;
        fs::create_directories(skyboxDir, ec);

        struct FaceAlias { std::string canonical; std::vector<std::string> names; };
        const FaceAlias faceAliases[6] = {
            { "right",  { "right" } },
            { "left",   { "left" } },
            { "top",    { "top", "up" } },
            { "bottom", { "bottom", "down" } },
            { "front",  { "front" } },
            { "back",   { "back" } }
        };
        const std::string extensions[3] = { ".jpg", ".png", ".bmp" };

        for (const auto& fa : faceAliases)
        {
            bool found = false;
            for (const auto& name : fa.names)
            {
                for (const auto& ext : extensions)
                {
                    const fs::path srcFile = srcDir / (name + ext);
                    if (fs::exists(srcFile))
                    {
                        const fs::path destFile = skyboxDir / (name + ext);
                        fs::copy_file(srcFile, destFile, fs::copy_options::overwrite_existing, ec);
                        const std::string relFace = fs::relative(destFile, contentDir).generic_string();
                        data["faces"][fa.canonical] = relFace;
                        found = true;
                        break;
                    }
                }
                if (found) break;
            }
        }

        // Store the folder path relative to Content for the renderer
        data["folderPath"] = fs::relative(skyboxDir, fs::path(diagnostics.getProjectInfo().projectPath)).generic_string();
    }

    skybox->setData(data);
    skybox->setPath(path);
    skybox->setIsSaved(false);

    auto saveResult = saveSkyboxAsset(skybox);
    if (!saveResult.success)
    {
        result.errorMessage = saveResult.errorMessage;
        return result;
    }

    auto id = registerLoadedAsset(skybox);
    if (id != 0)
    {
        skybox->setId(id);
    }
    m_garbageCollector.registerResource(skybox);

    result.object = skybox;
    result.success = true;
    return result;
}
#endif

unsigned char* AssetManager::loadRawImageData(const std::string& path, int& width, int& height, int& channels)
{
    width = 0;
    height = 0;
    channels = 0;

    // Normalize the path to use consistent separators
    std::filesystem::path normalized = std::filesystem::path(path).lexically_normal();
    std::string normalizedStr = normalized.string();

    unsigned char* data = stbi_load(normalizedStr.c_str(), &width, &height, &channels, 0);
    if (!data)
    {
        Logger::Instance().log(Logger::Category::AssetManagement,
            "Failed to load raw image: " + normalizedStr, Logger::LogLevel::ERROR);
    }
    return data;
}

void AssetManager::freeRawImageData(unsigned char* data)
{
    if (data)
    {
        stbi_image_free(data);
    }
}

#if ENGINE_EDITOR
bool AssetManager::saveNewLevelAsset(EngineLevel* level)
{
    return saveLevelAsset(level).success;
}
#endif

// ---------------------------------------------------------------------------
// Parallel loading: disk-only read (no shared state), finalize, batch API
// ---------------------------------------------------------------------------

RawAssetData AssetManager::readAssetFromDisk(const std::string& path, AssetType expectedType)
{
    RawAssetData raw;
    raw.path = path;

    // Helper lambda: load WAV from memory buffer via SDL_IOFromMem
    auto loadWavFromBuffer = [](const std::vector<char>& buf, json& outData) -> bool
    {
        SDL_IOStream* io = SDL_IOFromMem(const_cast<char*>(buf.data()), buf.size());
        if (!io) return false;
        SDL_AudioSpec spec{};
        Uint8* wavBuf = nullptr;
        Uint32 wavLen = 0;
        bool ok = SDL_LoadWAV_IO(io, true, &spec, &wavBuf, &wavLen);
        if (!ok) return false;
        outData = json::object();
        outData["m_channels"]   = static_cast<int>(spec.channels);
        outData["m_sampleRate"] = static_cast<int>(spec.freq);
        outData["m_format"]     = static_cast<int>(spec.format);
        outData["m_data"]       = std::vector<unsigned char>(wavBuf, wavBuf + wavLen);
        SDL_free(wavBuf);
        return true;
    };

    // Special case: raw WAV files loaded directly via SDL
    if (expectedType == AssetType::Audio)
    {
        const fs::path inputPath(path);
        std::string ext = inputPath.extension().string();
        for (auto& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (ext == ".wav")
        {
            SDL_AudioSpec spec{};
            Uint8* buffer = nullptr;
            Uint32 length = 0;
            bool loaded = SDL_LoadWAV(path.c_str(), &spec, &buffer, &length);

            // HPK fallback for raw WAV
            if (!loaded)
            {
                auto* hpk = HPKReader::GetMounted();
                if (hpk)
                {
                    std::string vpath = hpk->makeVirtualPath(path);
                    if (!vpath.empty())
                    {
                        auto hpkBuf = hpk->readFile(vpath);
                        if (hpkBuf)
                        {
                            json audioData;
                            if (loadWavFromBuffer(*hpkBuf, audioData))
                            {
                                raw.data = std::move(audioData);
                                raw.name = inputPath.stem().string();
                                raw.type = AssetType::Audio;
                                raw.success = true;
                                return raw;
                            }
                        }
                    }
                }
                raw.errorMessage = "Failed to load WAV data.";
                return raw;
            }

            json audioData = json::object();
            audioData["m_channels"] = static_cast<int>(spec.channels);
            audioData["m_sampleRate"] = static_cast<int>(spec.freq);
            audioData["m_format"] = static_cast<int>(spec.format);
            audioData["m_data"] = std::vector<unsigned char>(buffer, buffer + length);
            SDL_free(buffer);

            raw.data = std::move(audioData);
            raw.name = inputPath.stem().string();
            raw.type = AssetType::Audio;
            raw.success = true;
            return raw;
        }
    }

    // Try to open the asset file from disk first
    AssetType headerType{ AssetType::Unknown };
    std::string name;
    bool isJson = false;
    json j;

    std::ifstream in(path, std::ios::binary);
    if (in.is_open())
    {
        if (!readAssetHeader(in, headerType, name, isJson))
        {
            raw.errorMessage = "Invalid asset header: " + path;
            return raw;
        }

        if (!readAssetJson(in, j, raw.errorMessage, isJson))
            return raw;
        in.close();
    }
    else
    {
        // HPK fallback: read the entire asset from the mounted archive
        auto* hpk = HPKReader::GetMounted();
        if (!hpk)
        {
            raw.errorMessage = "Failed to open asset file: " + path;
            return raw;
        }
        std::string vpath = hpk->makeVirtualPath(path);
        if (vpath.empty())
        {
            raw.errorMessage = "Failed to open asset file (no virtual path): " + path;
            return raw;
        }
        auto hpkBuf = hpk->readFile(vpath);
        if (!hpkBuf)
        {
            raw.errorMessage = "Failed to read asset from HPK: " + vpath;
            return raw;
        }

        size_t headerEndPos = 0;
        bool isMsgPack = false;
        if (!readAssetHeaderFromMemory(*hpkBuf, headerType, name, isJson, headerEndPos, isMsgPack))
        {
            raw.errorMessage = "Invalid asset header (HPK): " + path;
            return raw;
        }
        if (!readAssetJsonFromMemory(*hpkBuf, j, raw.errorMessage, isJson, headerEndPos, isMsgPack))
            return raw;
    }

    // Type validation (Model3D assets may also be PointLight)
    bool typeOk = false;
    if (expectedType == AssetType::Model3D || expectedType == AssetType::PointLight)
    {
        typeOk = (headerType == AssetType::Model3D || headerType == AssetType::PointLight);
    }
    else
    {
        typeOk = (headerType == expectedType);
    }
    if (!typeOk)
    {
        raw.errorMessage = "Asset type mismatch for: " + path;
        return raw;
    }

    // For textures: decode source image on this thread (CPU-intensive)
    if (headerType == AssetType::Texture && j.is_object() && j.contains("m_sourcePath"))
    {
        const auto& sourceValue = j.at("m_sourcePath");
        if (sourceValue.is_string())
        {
            fs::path sourcePath = sourceValue.get<std::string>();
            if (!sourcePath.is_absolute())
            {
                const auto& projPath = DiagnosticsManager::Instance().getProjectInfo().projectPath;
                fs::path resolved;
                if (!projPath.empty())
                    resolved = fs::path(projPath) / sourcePath;
                if (resolved.empty() || !fs::exists(resolved))
                    resolved = fs::current_path() / sourcePath;
                sourcePath = resolved;
            }

            bool sourceFound = fs::exists(sourcePath);
            std::optional<std::vector<char>> hpkImageBuf;

            // HPK fallback for texture source images
            if (!sourceFound)
            {
                auto* hpk = HPKReader::GetMounted();
                if (hpk)
                {
                    std::string vpath = hpk->makeVirtualPath(sourcePath.string());
                    if (!vpath.empty())
                        hpkImageBuf = hpk->readFile(vpath);
                    if (hpkImageBuf)
                        sourceFound = true;
                }
            }

            if (sourceFound)
            {
                // Check if this is a DDS (compressed) texture
                std::string srcExt = sourcePath.extension().string();
                for (auto& c : srcExt)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                if (srcExt == ".dds" || (j.contains("m_compressed") && j["m_compressed"].get<bool>()))
                {
                    j["m_compressed"] = true;
                    j["m_ddsPath"] = sourcePath.string();
                }
                else
                {
                    int width = 0, height = 0, channels = 0;
                    unsigned char* data = nullptr;
                    if (hpkImageBuf)
                        data = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(hpkImageBuf->data()),
                            static_cast<int>(hpkImageBuf->size()), &width, &height, &channels, 4);
                    else
                        data = stbi_load(sourcePath.string().c_str(), &width, &height, &channels, 4);
                    if (data)
                    {
                        channels = 4;
                        j["m_width"] = width;
                        j["m_height"] = height;
                        j["m_channels"] = channels;
                        j["m_data"] = std::vector<unsigned char>(data, data + (static_cast<size_t>(width) * height * channels));
                        stbi_image_free(data);
                    }
                }
            }
        }
    }

    // For audio .asset files with m_sourcePath: decode WAV source
    if (headerType == AssetType::Audio && j.is_object() && j.contains("m_sourcePath"))
    {
        const auto& sourceValue = j.at("m_sourcePath");
        if (sourceValue.is_string())
        {
            fs::path sourcePath = sourceValue.get<std::string>();
            if (!sourcePath.is_absolute())
            {
                const auto& projPath = DiagnosticsManager::Instance().getProjectInfo().projectPath;
                fs::path resolved;
                if (!projPath.empty())
                    resolved = fs::path(projPath) / sourcePath;
                if (resolved.empty() || !fs::exists(resolved))
                    resolved = fs::current_path() / sourcePath;
                sourcePath = resolved;
            }
            if (fs::exists(sourcePath))
            {
                SDL_AudioSpec spec{};
                Uint8* buffer = nullptr;
                Uint32 length = 0;
                if (SDL_LoadWAV(sourcePath.string().c_str(), &spec, &buffer, &length))
                {
                    j["m_channels"] = static_cast<int>(spec.channels);
                    j["m_sampleRate"] = static_cast<int>(spec.freq);
                    j["m_format"] = static_cast<int>(spec.format);
                    j["m_data"] = std::vector<unsigned char>(buffer, buffer + length);
                    SDL_free(buffer);
                }
            }
            else
            {
                // HPK fallback for audio source files
                auto* hpk = HPKReader::GetMounted();
                if (hpk)
                {
                    std::string vpath = hpk->makeVirtualPath(sourcePath.string());
                    if (!vpath.empty())
                    {
                        auto hpkBuf = hpk->readFile(vpath);
                        if (hpkBuf)
                        {
                            json audioData;
                            if (loadWavFromBuffer(*hpkBuf, audioData))
                            {
                                if (audioData.contains("m_channels"))   j["m_channels"]   = audioData["m_channels"];
                                if (audioData.contains("m_sampleRate")) j["m_sampleRate"] = audioData["m_sampleRate"];
                                if (audioData.contains("m_format"))     j["m_format"]     = audioData["m_format"];
                                if (audioData.contains("m_data"))       j["m_data"]       = audioData["m_data"];
                            }
                        }
                    }
                }
            }
        }
    }

    if (name.empty())
    {
        name = fs::path(path).stem().string();
    }

    raw.data = std::move(j);
    raw.name = std::move(name);
    raw.type = headerType;
    raw.success = true;
    return raw;
}

unsigned int AssetManager::finalizeAssetLoad(RawAssetData&& raw)
{
    if (!raw.success)
    {
        return 0;
    }

    auto asset = std::make_shared<AssetData>();
    asset->setData(std::move(raw.data));
    asset->setName(raw.name);
    asset->setPath(raw.path);
    asset->setAssetType(raw.type);
    asset->setType(raw.type);
    asset->setIsSaved(true);

    auto id = registerLoadedAsset(asset);
    if (id != 0)
    {
        asset->setId(id);
    }
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_garbageCollector.registerResource(asset);
    }
    return id;
}

std::unordered_map<std::string, int> AssetManager::loadBatchParallel(
    const std::vector<std::pair<std::string, AssetType>>& requests)
{
    std::unordered_map<std::string, int> results;
    if (requests.empty())
    {
        return results;
    }

    auto& logger = Logger::Instance();

    // Deduplicate and skip already-loaded assets
    struct PendingLoad
    {
        std::string path;
        AssetType type;
    };
    std::vector<PendingLoad> pending;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        for (const auto& [path, type] : requests)
        {
            if (path.empty())
                continue;
            if (results.count(path))
                continue;

            auto pathIt = m_loadedAssetsByPath.find(path);
            if (pathIt != m_loadedAssetsByPath.end())
            {
                results[path] = static_cast<int>(pathIt->second);
                continue;
            }
            pending.push_back({ path, type });
        }
    }

    if (pending.empty())
    {
        return results;
    }

    const size_t count = pending.size();
    logger.log(Logger::Category::AssetManagement,
        "loadBatchParallel: dispatching " + std::to_string(count) + " jobs to pool (" + std::to_string(m_poolSize) + " threads)",
        Logger::LogLevel::INFO);

    // Shared results vector for the batch; each slot written by exactly one thread
    std::vector<RawAssetData> rawResults(count);
    m_batchPending.store(static_cast<int>(count), std::memory_order_relaxed);

    // Enqueue all jobs into the global pool
    for (size_t i = 0; i < count; ++i)
    {
        enqueueJob([this, i, path = pending[i].path, type = pending[i].type, &rawResults]()
        {
            rawResults[i] = AssetManager::readAssetFromDisk(path, type);

            // Decrement batch counter and notify waiter
            if (m_batchPending.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                std::lock_guard<std::mutex> lk(m_batchMutex);
                m_batchCv.notify_one();
            }
        });
    }

    // Wait for the entire batch to finish
    {
        std::unique_lock<std::mutex> lk(m_batchMutex);
        m_batchCv.wait(lk, [this]() { return m_batchPending.load(std::memory_order_acquire) <= 0; });
    }

    // Finalize sequentially on the calling thread
    for (size_t i = 0; i < count; ++i)
    {
        auto& raw = rawResults[i];
        if (!raw.success)
        {
            if (!raw.errorMessage.empty())
            {
                logger.log(Logger::Category::AssetManagement,
                    "loadBatchParallel: " + raw.errorMessage, Logger::LogLevel::WARNING);
            }
            continue;
        }
        const std::string loadedPath = raw.path;
        unsigned int id = finalizeAssetLoad(std::move(raw));
        if (id != 0)
        {
            results[loadedPath] = static_cast<int>(id);
        }
    }

    logger.log(Logger::Category::AssetManagement,
        "loadBatchParallel: completed. loaded=" + std::to_string(results.size()),
        Logger::LogLevel::INFO);

    return results;
}
