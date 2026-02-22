#include "AssetManager.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <filesystem>
#include <fstream>
#include <cstdint>
#include <unordered_set>
#include <cctype>
#include "AssetTypes.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_dialog.h>

#include "../Renderer/Material.h"
#include "../Core/AudioManager.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "../Core/ECS/ECS.h"

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
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".hdr")
            return AssetType::Texture;

        if (ext == ".wav")
            return AssetType::Audio;

        // 3D Models (Assimp-supported)
        if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb" ||
            ext == ".dae" || ext == ".3ds" || ext == ".blend" || ext == ".stl" ||
            ext == ".ply" || ext == ".x3d")
            return AssetType::Model3D;

        // Shaders
        if (ext == ".glsl" || ext == ".vert" || ext == ".frag" || ext == ".geom" || ext == ".comp")
            return AssetType::Shader;

        // Scripts
        if (ext == ".py")
            return AssetType::Script;

        return AssetType::Unknown;
    }


    static std::string normalizeKeyConstantName(const std::string& name)
    {
        std::string result;
        bool lastUnderscore = false;
        for (unsigned char c : name)
        {
            if (std::isalnum(c))
            {
                result.push_back(static_cast<char>(std::toupper(c)));
                lastUnderscore = false;
            }
            else if (!lastUnderscore)
            {
                result.push_back('_');
                lastUnderscore = true;
            }
        }
        while (!result.empty() && result.back() == '_')
        {
            result.pop_back();
        }
        if (result.empty())
        {
            return {};
        }
        return "Key_" + result;
    }

    static void writeKeyConstants(std::ofstream& stubOut, const std::string& indent)
    {
        stubOut << indent << "Keys: Dict[str, int]\n";
        stubOut << indent << "@staticmethod\n";
        stubOut << indent << "def get_key(name: str) -> int: ...\n";

        std::unordered_set<std::string> added;
        for (int scancode = 0; scancode < SDL_SCANCODE_COUNT; ++scancode)
        {
            const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(scancode));
            if (!name || !*name)
            {
                continue;
            }
            const std::string constantName = normalizeKeyConstantName(name);
            if (constantName.empty() || !added.insert(constantName).second)
            {
                continue;
            }
            stubOut << indent << constantName << ": int\n";
        }
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

static fs::path getEditorWidgetsRootPath()
{
    return fs::current_path() / "Editor" / "Widgets";
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

    // Persist the updated registry to disk
    auto& diagnostics = DiagnosticsManager::Instance();
    if (diagnostics.isProjectLoaded() && !diagnostics.getProjectInfo().projectPath.empty())
    {
        saveAssetRegistry(diagnostics.getProjectInfo().projectPath);
    }
}

bool AssetManager::deleteAsset(const std::string& relPath, bool deleteFromDisk)
{
    auto& logger = Logger::Instance();
    auto& diagnostics = DiagnosticsManager::Instance();

    if (relPath.empty())
    {
        return false;
    }

    // Remove from registry
    bool found = false;
    for (auto it = m_registry.begin(); it != m_registry.end(); ++it)
    {
        if (it->path == relPath)
        {
            logger.log(Logger::Category::AssetManagement,
                "Deleting asset from registry: " + relPath, Logger::LogLevel::INFO);
            m_registryByPath.erase(it->path);
            m_registryByName.erase(it->name);
            m_registry.erase(it);
            found = true;
            break;
        }
    }

    if (found)
    {
        // Rebuild index maps since indices shifted
        m_registryByPath.clear();
        m_registryByName.clear();
        for (size_t i = 0; i < m_registry.size(); ++i)
        {
            if (!m_registry[i].path.empty())
                m_registryByPath[m_registry[i].path] = i;
            if (!m_registry[i].name.empty())
                m_registryByName[m_registry[i].name] = i;
        }

        // Persist
        if (diagnostics.isProjectLoaded() && !diagnostics.getProjectInfo().projectPath.empty())
        {
            saveAssetRegistry(diagnostics.getProjectInfo().projectPath);
        }
    }

    // Delete file from disk
    if (deleteFromDisk && diagnostics.isProjectLoaded())
    {
        const fs::path contentDir = fs::path(diagnostics.getProjectInfo().projectPath) / "Content";
        const fs::path absPath = contentDir / fs::path(relPath);
        std::error_code ec;
        if (fs::exists(absPath, ec))
        {
            if (fs::remove(absPath, ec))
            {
                logger.log(Logger::Category::AssetManagement,
                    "Deleted asset file: " + absPath.string(), Logger::LogLevel::INFO);
            }
            else
            {
                logger.log(Logger::Category::AssetManagement,
                    "Failed to delete asset file: " + absPath.string() + " ec=" + ec.message(),
                    Logger::LogLevel::WARNING);
                return false;
            }
        }
    }

    return found;
}

bool AssetManager::discoverAssetsAndBuildRegistry(const std::string& projectRoot)
{
    auto& log = Logger::Instance();
    log.log(Logger::Category::AssetManagement, "[Registry] discoverAssetsAndBuildRegistry projectRoot='" + projectRoot + "'", Logger::LogLevel::INFO);

    m_registry.clear();
    m_registryByPath.clear();
    m_registryByName.clear();

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

        if (!readAssetHeaderType(p, type))
        {
            ++filesSkippedHeaderType;
            log.log(Logger::Category::AssetManagement, "[Registry]   skip (header type failed): " + p.string(), Logger::LogLevel::WARNING);
            continue;
        }
        if (!readAssetHeaderName(p, name))
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

    log.log(Logger::Category::AssetManagement, "[Registry] discovery complete: scanned=" + std::to_string(filesScanned)
        + " registered=" + std::to_string(filesRegistered)
        + " skippedExtension=" + std::to_string(filesSkippedExtension)
        + " skippedHeaderType=" + std::to_string(filesSkippedHeaderType)
        + " skippedNotRegular=" + std::to_string(filesSkippedNotRegular)
        + " totalRegistry=" + std::to_string(m_registry.size()),
        Logger::LogLevel::INFO);

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
    ensureEditorWidgetsCreated();
    return true;
}

void AssetManager::ensureEditorWidgetsCreated()
{
    auto& logger = Logger::Instance();

    // Ensure Editor/Textures icons exist
    {
        const fs::path texturesDir = fs::current_path() / "Editor" / "Textures";
        std::error_code ec;
        fs::create_directories(texturesDir, ec);

        auto writeTga = [&](const std::string& name, const std::vector<uint8_t>& rgba, int w, int h)
        {
            const fs::path path = texturesDir / name;
            if (fs::exists(path))
            {
                return;
            }
            std::ofstream out(path, std::ios::binary);
            if (!out.is_open())
            {
                return;
            }
            uint8_t header[18] = {};
            header[2] = 2;
            header[12] = static_cast<uint8_t>(w & 0xFF);
            header[13] = static_cast<uint8_t>((w >> 8) & 0xFF);
            header[14] = static_cast<uint8_t>(h & 0xFF);
            header[15] = static_cast<uint8_t>((h >> 8) & 0xFF);
            header[16] = 32;
            header[17] = 0x28;
            out.write(reinterpret_cast<const char*>(header), 18);
            for (int y = 0; y < h; ++y)
            {
                for (int x = 0; x < w; ++x)
                {
                    const int idx = (y * w + x) * 4;
                    const uint8_t bgra[4] = { rgba[idx + 2], rgba[idx + 1], rgba[idx], rgba[idx + 3] };
                    out.write(reinterpret_cast<const char*>(bgra), 4);
                }
            }
        };

        constexpr int sz = 24;
        {
            std::vector<uint8_t> play(sz * sz * 4, 0);
            for (int y = 0; y < sz; ++y)
            {
                const float fy = static_cast<float>(y) / static_cast<float>(sz - 1);
                const float halfH = 0.5f - std::abs(fy - 0.5f);
                const int maxX = static_cast<int>(halfH * 2.0f * static_cast<float>(sz) * 0.85f);
                for (int x = 0; x < sz; ++x)
                {
                    const int idx = (y * sz + x) * 4;
                    if (x >= 4 && x < 4 + maxX)
                    {
                        play[idx] = 220; play[idx + 1] = 220; play[idx + 2] = 220; play[idx + 3] = 255;
                    }
                }
            }
            writeTga("Play.tga", play, sz, sz);
        }
        {
            std::vector<uint8_t> stop(sz * sz * 4, 0);
            for (int y = 0; y < sz; ++y)
            {
                for (int x = 0; x < sz; ++x)
                {
                    const int idx = (y * sz + x) * 4;
                    if (x >= 4 && x < sz - 4 && y >= 4 && y < sz - 4)
                    {
                        stop[idx] = 220; stop[idx + 1] = 100; stop[idx + 2] = 100; stop[idx + 3] = 255;
                    }
                }
            }
            writeTga("Stop.tga", stop, sz, sz);
        }
    }

    const std::string defaultWidgetRel = "TitleBar.asset";
    {
        json widgetJson = json::object();
        widgetJson["m_sizePixels"] = json{ {"x", 0.0f}, {"y", 100.0f} };
        widgetJson["m_positionPixels"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        widgetJson["m_anchor"] = "TopLeft";
        widgetJson["m_fillX"] = true;
        widgetJson["m_zOrder"] = 0;

        json elements = json::array();

        // Full-area dark background
        json bgPanel = json::object();
        bgPanel["id"] = "TitleBar.Background";
        bgPanel["type"] = "Panel";
        bgPanel["from"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        bgPanel["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
        bgPanel["fillX"] = true;
        bgPanel["color"] = json{ {"x", 0.1f}, {"y", 0.1f}, {"z", 0.1f}, {"w", 1.0f} };
        bgPanel["shaderVertex"] = "panel_vertex.glsl";
        bgPanel["shaderFragment"] = "panel_fragment.glsl";
        elements.push_back(bgPanel);

        // Slightly darker strip behind tab row (bottom 50%)
        json tabRowBg = json::object();
        tabRowBg["id"] = "TitleBar.TabRowBg";
        tabRowBg["type"] = "Panel";
        tabRowBg["from"] = json{ {"x", 0.0f}, {"y", 0.5f} };
        tabRowBg["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
        tabRowBg["fillX"] = true;
        tabRowBg["color"] = json{ {"x", 0.08f}, {"y", 0.08f}, {"z", 0.08f}, {"w", 1.0f} };
        tabRowBg["shaderVertex"] = "panel_vertex.glsl";
        tabRowBg["shaderFragment"] = "panel_fragment.glsl";
        elements.push_back(tabRowBg);

        // ---- Row 1: Title row (top portion, y=0..0.5, 50px) ----

        // App title "HorizonEngine" on the left
        json label = json::object();
        label["id"] = "TitleBar.Label";
        label["type"] = "Text";
        label["from"] = json{ {"x", 0.01f}, {"y", 0.0f} };
        label["to"] = json{ {"x", 0.2f}, {"y", 0.5f} };
        label["color"] = json{ {"x", 0.85f}, {"y", 0.85f}, {"z", 0.85f}, {"w", 1.0f} };
        label["text"] = "HorizonEngine";
        label["font"] = "default.ttf";
        label["fontSize"] = 14.0f;
        label["textAlignH"] = "Left";
        label["textAlignV"] = "Center";
        label["sizeToContent"] = true;
        label["shaderVertex"] = "text_vertex.glsl";
        label["shaderFragment"] = "text_fragment.glsl";
        elements.push_back(label);

        // Project name in the center
        json projectLabel = json::object();
        projectLabel["id"] = "TitleBar.ProjectName";
        projectLabel["type"] = "Text";
        projectLabel["from"] = json{ {"x", 0.3f}, {"y", 0.0f} };
        projectLabel["to"] = json{ {"x", 0.7f}, {"y", 0.5f} };
        projectLabel["color"] = json{ {"x", 0.6f}, {"y", 0.6f}, {"z", 0.6f}, {"w", 1.0f} };
        projectLabel["text"] = "Project";
        projectLabel["font"] = "default.ttf";
        projectLabel["fontSize"] = 12.0f;
        projectLabel["textAlignH"] = "Center";
        projectLabel["textAlignV"] = "Center";
        projectLabel["sizeToContent"] = true;
        projectLabel["shaderVertex"] = "text_vertex.glsl";
        projectLabel["shaderFragment"] = "text_fragment.glsl";
        elements.push_back(projectLabel);

        // Window controls (Minimize, Maximize, Close) on the far right
        json buttonStack = json::object();
        buttonStack["id"] = "TitleBar.Buttons";
        buttonStack["type"] = "StackPanel";
        buttonStack["from"] = json{ {"x", 0.88f}, {"y", 0.0f} };
        buttonStack["to"] = json{ {"x", 1.0f}, {"y", 0.5f} };
        buttonStack["orientation"] = "Horizontal";
        buttonStack["padding"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        buttonStack["sizeToContent"] = true;

        json btnMin = json::object();
        btnMin["id"] = "TitleBar.Minimize";
        btnMin["type"] = "Button";
        btnMin["clickEvent"] = "TitleBar.Minimize";
        btnMin["fillX"] = true;
        btnMin["fillY"] = true;
        btnMin["color"] = json{ {"x", 0.1f}, {"y", 0.1f}, {"z", 0.1f}, {"w", 1.0f} };
        btnMin["hoverColor"] = json{ {"x", 0.2f}, {"y", 0.2f}, {"z", 0.2f}, {"w", 1.0f} };
        btnMin["textColor"] = json{ {"x", 1.0f}, {"y", 1.0f}, {"z", 1.0f}, {"w", 1.0f} };
        btnMin["text"] = "_";
        btnMin["font"] = "default.ttf";
        btnMin["fontSize"] = 14.0f;
        btnMin["textAlignH"] = "Center";
        btnMin["textAlignV"] = "Center";
        btnMin["minSize"] = json{ {"x", 46.0f}, {"y", 0.0f} };
        btnMin["padding"] = json{ {"x", 2.0f}, {"y", 2.0f} };
        btnMin["shaderVertex"] = "button_vertex.glsl";
        btnMin["shaderFragment"] = "button_fragment.glsl";

        json btnMax = json::object();
        btnMax["id"] = "TitleBar.Maximize";
        btnMax["type"] = "Button";
        btnMax["clickEvent"] = "TitleBar.Maximize";
        btnMax["fillX"] = true;
        btnMax["fillY"] = true;
        btnMax["color"] = json{ {"x", 0.1f}, {"y", 0.1f}, {"z", 0.1f}, {"w", 1.0f} };
        btnMax["hoverColor"] = json{ {"x", 0.2f}, {"y", 0.2f}, {"z", 0.2f}, {"w", 1.0f} };
        btnMax["textColor"] = json{ {"x", 1.0f}, {"y", 1.0f}, {"z", 1.0f}, {"w", 1.0f} };
        btnMax["text"] = "[ ]";
        btnMax["font"] = "default.ttf";
        btnMax["fontSize"] = 14.0f;
        btnMax["textAlignH"] = "Center";
        btnMax["textAlignV"] = "Center";
        btnMax["minSize"] = json{ {"x", 46.0f}, {"y", 0.0f} };
        btnMax["padding"] = json{ {"x", 2.0f}, {"y", 2.0f} };
        btnMax["shaderVertex"] = "button_vertex.glsl";
        btnMax["shaderFragment"] = "button_fragment.glsl";

        json btnClose = json::object();
        btnClose["id"] = "TitleBar.Close";
        btnClose["type"] = "Button";
        btnClose["clickEvent"] = "TitleBar.Close";
        btnClose["fillX"] = true;
        btnClose["fillY"] = true;
        btnClose["color"] = json{ {"x", 0.1f}, {"y", 0.1f}, {"z", 0.1f}, {"w", 1.0f} };
        btnClose["hoverColor"] = json{ {"x", 0.7f}, {"y", 0.15f}, {"z", 0.15f}, {"w", 1.0f} };
        btnClose["textColor"] = json{ {"x", 1.0f}, {"y", 1.0f}, {"z", 1.0f}, {"w", 1.0f} };
        btnClose["text"] = "X";
        btnClose["font"] = "default.ttf";
        btnClose["fontSize"] = 14.0f;
        btnClose["textAlignH"] = "Center";
        btnClose["textAlignV"] = "Center";
        btnClose["minSize"] = json{ {"x", 46.0f}, {"y", 0.0f} };
        btnClose["padding"] = json{ {"x", 2.0f}, {"y", 2.0f} };
        btnClose["shaderVertex"] = "button_vertex.glsl";
        btnClose["shaderFragment"] = "button_fragment.glsl";

        buttonStack["children"] = json::array({ btnMin, btnMax, btnClose });
        elements.push_back(buttonStack);

        // ---- Row 2: Tab strip (bottom portion, y=0.5..1.0, 50px) ----

        json tabBar = json::object();
        tabBar["id"] = "TitleBar.Tabs";
        tabBar["type"] = "StackPanel";
        tabBar["from"] = json{ {"x", 0.0f}, {"y", 0.5f} };
        tabBar["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
        tabBar["orientation"] = "Horizontal";
        tabBar["fillX"] = true;
        tabBar["fillY"] = true;
        tabBar["padding"] = json{ {"x", 4.0f}, {"y", 0.0f} };
        tabBar["sizeToContent"] = true;

        json tabViewport = json::object();
        tabViewport["id"] = "TitleBar.Tab.Viewport";
        tabViewport["type"] = "Button";
        tabViewport["clickEvent"] = "TitleBar.Tab.Viewport";
        tabViewport["fillY"] = true;
        tabViewport["color"] = json{ {"x", 0.14f}, {"y", 0.14f}, {"z", 0.14f}, {"w", 1.0f} };
        tabViewport["hoverColor"] = json{ {"x", 0.2f}, {"y", 0.2f}, {"z", 0.2f}, {"w", 1.0f} };
        tabViewport["textColor"] = json{ {"x", 0.9f}, {"y", 0.9f}, {"z", 0.9f}, {"w", 1.0f} };
        tabViewport["text"] = "Viewport";
        tabViewport["font"] = "default.ttf";
        tabViewport["fontSize"] = 12.0f;
        tabViewport["textAlignH"] = "Center";
        tabViewport["textAlignV"] = "Center";
        tabViewport["minSize"] = json{ {"x", 90.0f}, {"y", 0.0f} };
        tabViewport["padding"] = json{ {"x", 10.0f}, {"y", 0.0f} };
        tabViewport["shaderVertex"] = "button_vertex.glsl";
        tabViewport["shaderFragment"] = "button_fragment.glsl";

        tabBar["children"] = json::array({ tabViewport });
        elements.push_back(tabBar);

        widgetJson["m_elements"] = elements;

        auto widget = std::make_shared<AssetData>();
        widget->setName("TitleBar");
        widget->setData(std::move(widgetJson));

        const fs::path widgetsRoot = getEditorWidgetsRootPath();
        std::error_code ec;
        fs::create_directories(widgetsRoot, ec);
        const fs::path abs = widgetsRoot / fs::path(defaultWidgetRel);
        bool existsAndOk = false;
        if (fs::exists(abs))
        {
            AssetType headerType{ AssetType::Unknown };
            existsAndOk = readAssetHeaderType(abs, headerType) && headerType == AssetType::Widget;
            if (existsAndOk)
            {
                std::ifstream check(abs, std::ios::in);
                if (check.is_open())
                {
                    const std::string content((std::istreambuf_iterator<char>(check)), std::istreambuf_iterator<char>());
                    if (content.find("HorizonEngine") == std::string::npos
                        || content.find("\"y\": 100.0") == std::string::npos)
                    {
                        existsAndOk = false;
                    }
                }
            }
        }
        if (!existsAndOk)
        {
            std::ofstream out(abs, std::ios::out | std::ios::trunc);
            if (out.is_open())
            {
                json fileJson = json::object();
                fileJson["magic"] = 0x41535453;
                fileJson["version"] = 2;
                fileJson["type"] = static_cast<int>(AssetType::Widget);
                fileJson["name"] = widget->getName();
                fileJson["data"] = widget->getData();
                out << fileJson.dump(4);
                if (!out.good())
                {
                    logger.log(Logger::Category::AssetManagement, "Failed to write editor widget asset.", Logger::LogLevel::ERROR);
                }
            }
            else
            {
                logger.log(Logger::Category::AssetManagement, "Failed to open editor widget asset for writing.", Logger::LogLevel::ERROR);
            }
        }
    }

    const std::string toolbarWidgetRel = "ViewportOverlay.asset";
    {
        json widgetJson = json::object();
        widgetJson["m_sizePixels"] = json{ {"x", 0.0f}, {"y", 34.0f} };
        widgetJson["m_positionPixels"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        widgetJson["m_anchor"] = "TopLeft";
        widgetJson["m_fillX"] = true;
        widgetJson["m_zOrder"] = 0;

        json elements = json::array();

        // Dark toolbar background
        json bg = json::object();
        bg["id"] = "ViewportOverlay.Background";
        bg["type"] = "Panel";
        bg["from"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        bg["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
        bg["fillX"] = true;
        bg["color"] = json{ {"x", 0.12f}, {"y", 0.12f}, {"z", 0.12f}, {"w", 1.0f} };
        bg["shaderVertex"] = "panel_vertex.glsl";
        bg["shaderFragment"] = "panel_fragment.glsl";
        elements.push_back(bg);

        // Center section: PIE controls (spacer + play + spacer)
        json centerStack = json::object();
        centerStack["id"] = "ViewportOverlay.Center";
        centerStack["type"] = "StackPanel";
        centerStack["from"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        centerStack["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
        centerStack["orientation"] = "Horizontal";
        centerStack["fillX"] = true;
        centerStack["fillY"] = true;
        centerStack["padding"] = json{ {"x", 0.0f}, {"y", 0.0f} };

        json spacerLeft = json::object();
        spacerLeft["id"] = "ViewportOverlay.SpacerL";
        spacerLeft["type"] = "Panel";
        spacerLeft["fillX"] = true;
        spacerLeft["fillY"] = true;
        spacerLeft["color"] = json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };

        json btnPIE = json::object();
        btnPIE["id"] = "ViewportOverlay.PIE";
        btnPIE["type"] = "Button";
        btnPIE["clickEvent"] = "ViewportOverlay.PIE";
        btnPIE["color"] = json{ {"x", 0.12f}, {"y", 0.12f}, {"z", 0.12f}, {"w", 1.0f} };
        btnPIE["hoverColor"] = json{ {"x", 0.22f}, {"y", 0.22f}, {"z", 0.22f}, {"w", 1.0f} };
        btnPIE["imagePath"] = "Play.tga";
        btnPIE["minSize"] = json{ {"x", 32.0f}, {"y", 24.0f} };
        btnPIE["sizeToContent"] = true;
        btnPIE["padding"] = json{ {"x", 4.0f}, {"y", 4.0f} };
        btnPIE["shaderVertex"] = "button_vertex.glsl";
        btnPIE["shaderFragment"] = "button_fragment.glsl";

        json spacerRight = json::object();
        spacerRight["id"] = "ViewportOverlay.SpacerR";
        spacerRight["type"] = "Panel";
        spacerRight["fillX"] = true;
        spacerRight["fillY"] = true;
        spacerRight["color"] = json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };

        centerStack["children"] = json::array({ spacerLeft, btnPIE, spacerRight });
        elements.push_back(centerStack);

        // Right section: Settings button
        json rightStack = json::object();
        rightStack["id"] = "ViewportOverlay.Right";
        rightStack["type"] = "StackPanel";
        rightStack["from"] = json{ {"x", 0.88f}, {"y", 0.0f} };
        rightStack["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
        rightStack["orientation"] = "Horizontal";
        rightStack["padding"] = json{ {"x", 2.0f}, {"y", 2.0f} };
        rightStack["sizeToContent"] = true;

        json btnSettings = json::object();
        btnSettings["id"] = "ViewportOverlay.Settings";
        btnSettings["type"] = "Button";
        btnSettings["clickEvent"] = "ViewportOverlay.Settings";
        btnSettings["color"] = json{ {"x", 0.12f}, {"y", 0.12f}, {"z", 0.12f}, {"w", 1.0f} };
        btnSettings["hoverColor"] = json{ {"x", 0.22f}, {"y", 0.22f}, {"z", 0.22f}, {"w", 1.0f} };
        btnSettings["textColor"] = json{ {"x", 0.85f}, {"y", 0.85f}, {"z", 0.85f}, {"w", 1.0f} };
        btnSettings["text"] = "Settings";
        btnSettings["font"] = "default.ttf";
        btnSettings["fontSize"] = 12.0f;
        btnSettings["textAlignH"] = "Center";
        btnSettings["textAlignV"] = "Center";
        btnSettings["minSize"] = json{ {"x", 70.0f}, {"y", 0.0f} };
        btnSettings["padding"] = json{ {"x", 8.0f}, {"y", 2.0f} };
        btnSettings["shaderVertex"] = "button_vertex.glsl";
        btnSettings["shaderFragment"] = "button_fragment.glsl";

        rightStack["children"] = json::array({ btnSettings });
        elements.push_back(rightStack);

        widgetJson["m_elements"] = elements;

        auto widget = std::make_shared<AssetData>();
        widget->setName("ViewportOverlay");
        widget->setData(std::move(widgetJson));

        const fs::path widgetsRoot = getEditorWidgetsRootPath();
        std::error_code ec;
        fs::create_directories(widgetsRoot, ec);
        const fs::path abs = widgetsRoot / fs::path(toolbarWidgetRel);
        bool existsAndOk = false;
        if (fs::exists(abs))
        {
            AssetType headerType{ AssetType::Unknown };
            existsAndOk = readAssetHeaderType(abs, headerType) && headerType == AssetType::Widget;
            if (existsAndOk)
            {
                std::ifstream check(abs, std::ios::in);
                if (check.is_open())
                {
                    const std::string content((std::istreambuf_iterator<char>(check)), std::istreambuf_iterator<char>());
                    if (content.find("ViewportOverlay.Center") == std::string::npos
                        || content.find("ViewportOverlay.Left") != std::string::npos)
                    {
                        existsAndOk = false;
                    }
                }
            }
        }
        if (!existsAndOk)
        {
            std::ofstream out(abs, std::ios::out | std::ios::trunc);
            if (out.is_open())
            {
                json fileJson = json::object();
                fileJson["magic"] = 0x41535453;
                fileJson["version"] = 2;
                fileJson["type"] = static_cast<int>(AssetType::Widget);
                fileJson["name"] = widget->getName();
                fileJson["data"] = widget->getData();
                out << fileJson.dump(4);
            }
        }
    }

    createWorldSettingsWidgetAsset();

    const std::string outlinerWidgetRel = "WorldOutliner.asset";
    {
        json widgetJson = json::object();
        widgetJson["m_sizePixels"] = json{ {"x", 200.0f}, {"y", 0.0f} };
        widgetJson["m_positionPixels"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        widgetJson["m_anchor"] = "TopRight";
        widgetJson["m_fillY"] = true;
        widgetJson["m_zOrder"] = 1;

        json elements = json::array();
        json panel = json::object();
        panel["id"] = "Outliner.Background";
        panel["type"] = "Panel";
        panel["from"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        panel["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
        panel["fillX"] = true;
        panel["fillY"] = true;
        panel["color"] = json{ {"x", 0.07f}, {"y", 0.08f}, {"z", 0.1f}, {"w", 0.88f} };
        panel["shaderVertex"] = "panel_vertex.glsl";
        panel["shaderFragment"] = "panel_fragment.glsl";
        elements.push_back(panel);

        json label = json::object();
        label["id"] = "Outliner.Title";
        label["type"] = "Text";
        label["from"] = json{ {"x", 0.05f}, {"y", 0.02f} };
        label["to"] = json{ {"x", 0.95f}, {"y", 0.1f} };
        label["color"] = json{ {"x", 1.0f}, {"y", 1.0f}, {"z", 1.0f}, {"w", 1.0f} };
        label["text"] = "Outliner";
        label["font"] = "default.ttf";
        label["fontSize"] = 18.0f;
        label["sizeToContent"] = true;
        label["shaderVertex"] = "text_vertex.glsl";
        label["shaderFragment"] = "text_fragment.glsl";
        elements.push_back(label);

        json listPanel = json::object();
        listPanel["id"] = "Outliner.EntityList";
        listPanel["type"] = "StackPanel";
        listPanel["from"] = json{ {"x", 0.05f}, {"y", 0.12f} };
        listPanel["to"] = json{ {"x", 0.95f}, {"y", 0.44f} };
        listPanel["orientation"] = "Vertical";
        listPanel["padding"] = json{ {"x", 2.0f}, {"y", 2.0f} };
        listPanel["fillX"] = true;
        listPanel["sizeToContent"] = false;
        listPanel["scrollable"] = true;
        elements.push_back(listPanel);

        widgetJson["m_elements"] = elements;

        auto widget = std::make_shared<AssetData>();
        widget->setName("WorldOutliner");
        widget->setData(std::move(widgetJson));

        const fs::path widgetsRoot = getEditorWidgetsRootPath();
        std::error_code ec;
        fs::create_directories(widgetsRoot, ec);
        const fs::path abs = widgetsRoot / fs::path(outlinerWidgetRel);
        bool existsAndOk = false;
        if (fs::exists(abs))
        {
            AssetType headerType{ AssetType::Unknown };
            existsAndOk = readAssetHeaderType(abs, headerType) && headerType == AssetType::Widget;
            if (existsAndOk)
            {
                std::ifstream in(abs, std::ios::in | std::ios::binary);
                if (in.is_open())
                {
                    json fileJson = json::parse(in, nullptr, false);
                    bool hasList = false;
                    bool hasNoDetails = true;
                    bool hasFillY = false;
                    if (!fileJson.is_discarded() && fileJson.is_object() && fileJson.contains("data"))
                    {
                        const auto& data = fileJson.at("data");
                        if (data.is_object())
                        {
                            if (data.contains("m_fillY") && data.at("m_fillY").is_boolean() && data.at("m_fillY").get<bool>())
                            {
                                hasFillY = true;
                            }
                            if (data.contains("m_elements"))
                            {
                                const auto& elems = data.at("m_elements");
                                if (elems.is_array())
                                {
                                    for (const auto& elem : elems)
                                    {
                                        if (elem.is_object() && elem.value("id", "") == "Outliner.EntityList")
                                        {
                                            hasList = true;
                                        }
                                        if (elem.is_object() && elem.value("id", "") == "Outliner.Details")
                                        {
                                            hasNoDetails = false;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    existsAndOk = existsAndOk && hasList && hasNoDetails && hasFillY;
                }
            }
        }
        if (!existsAndOk)
        {
            std::ofstream out(abs, std::ios::out | std::ios::trunc);
            if (out.is_open())
            {
                json fileJson = json::object();
                fileJson["magic"] = 0x41535453;
                fileJson["version"] = 2;
                fileJson["type"] = static_cast<int>(AssetType::Widget);
                fileJson["name"] = widget->getName();
                fileJson["data"] = widget->getData();
                out << fileJson.dump(4);
                if (!out.good())
                {
                    logger.log(Logger::Category::AssetManagement, "Failed to write editor widget asset.", Logger::LogLevel::ERROR);
                }
            }
            else
            {
                logger.log(Logger::Category::AssetManagement, "Failed to open editor widget asset for writing.", Logger::LogLevel::ERROR);
            }
        }
    }

    const std::string entityDetailsWidgetRel = "EntityDetails.asset";
    {
        json widgetJson = json::object();
        widgetJson["m_sizePixels"] = json{ {"x", 200.0f}, {"y", 0.0f} };
        widgetJson["m_positionPixels"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        widgetJson["m_anchor"] = "TopRight";
        widgetJson["m_zOrder"] = 2;

        json elements = json::array();
        json panel = json::object();
        panel["id"] = "Details.Background";
        panel["type"] = "Panel";
        panel["from"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        panel["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
        panel["fillX"] = true;
        panel["fillY"] = true;
        panel["color"] = json{ {"x", 0.07f}, {"y", 0.08f}, {"z", 0.1f}, {"w", 0.9f} };
        panel["shaderVertex"] = "panel_vertex.glsl";
        panel["shaderFragment"] = "panel_fragment.glsl";
        elements.push_back(panel);

        json label = json::object();
        label["id"] = "Details.Title";
        label["type"] = "Text";
        label["from"] = json{ {"x", 0.05f}, {"y", 0.0f} };
        label["to"] = json{ {"x", 0.95f}, {"y", 0.1f} };
        label["color"] = json{ {"x", 1.0f}, {"y", 1.0f}, {"z", 1.0f}, {"w", 1.0f} };
        label["text"] = "Details";
        label["font"] = "default.ttf";
        label["fontSize"] = 16.0f;
        label["sizeToContent"] = true;
        label["shaderVertex"] = "text_vertex.glsl";
        label["shaderFragment"] = "text_fragment.glsl";
        elements.push_back(label);

        json contentPanel = json::object();
        contentPanel["id"] = "Details.Content";
        contentPanel["type"] = "StackPanel";
        contentPanel["from"] = json{ {"x", 0.02f}, {"y", 0.12f} };
        contentPanel["to"] = json{ {"x", 0.98f}, {"y", 0.98f} };
        contentPanel["orientation"] = "Vertical";
        contentPanel["padding"] = json{ {"x", 2.0f}, {"y", 2.0f} };
        contentPanel["fillX"] = true;
        contentPanel["sizeToContent"] = false;
        contentPanel["scrollable"] = true;
        contentPanel["color"] = json{ {"x", 0.08f}, {"y", 0.09f}, {"z", 0.12f}, {"w", 0.65f} };
        elements.push_back(contentPanel);

        widgetJson["m_elements"] = elements;

        auto widget = std::make_shared<AssetData>();
        widget->setName("EntityDetails");
        widget->setData(std::move(widgetJson));

        const fs::path widgetsRoot = getEditorWidgetsRootPath();
        std::error_code ec;
        fs::create_directories(widgetsRoot, ec);
        const fs::path abs = widgetsRoot / fs::path(entityDetailsWidgetRel);
        bool existsAndOk = false;
        if (fs::exists(abs))
        {
            AssetType headerType{ AssetType::Unknown };
            existsAndOk = readAssetHeaderType(abs, headerType) && headerType == AssetType::Widget;
            if (existsAndOk)
            {
                std::ifstream in(abs, std::ios::in | std::ios::binary);
                if (in.is_open())
                {
                    json fileJson = json::parse(in, nullptr, false);
                    bool hasContent = false;
                    bool contentScrollable = false;
                    if (!fileJson.is_discarded() && fileJson.is_object() && fileJson.contains("data"))
                    {
                        const auto& data = fileJson.at("data");
                        if (data.is_object() && data.contains("m_elements"))
                        {
                            const auto& elems = data.at("m_elements");
                            if (elems.is_array())
                            {
                                for (const auto& elem : elems)
                                {
                                    if (elem.is_object() && elem.value("id", "") == "Details.Content")
                                    {
                                        hasContent = true;
                                        if (elem.contains("scrollable") && elem.at("scrollable").is_boolean() && elem.at("scrollable").get<bool>())
                                        {
                                            contentScrollable = true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    existsAndOk = existsAndOk && hasContent && contentScrollable;
                }
            }
        }
        if (!existsAndOk)
        {
            std::ofstream out(abs, std::ios::out | std::ios::trunc);
            if (out.is_open())
            {
                json fileJson = json::object();
                fileJson["magic"] = 0x41535453;
                fileJson["version"] = 2;
                fileJson["type"] = static_cast<int>(AssetType::Widget);
                fileJson["name"] = widget->getName();
                fileJson["data"] = widget->getData();
                out << fileJson.dump(4);
            }
        }
    }

    const std::string contentBrowserWidgetRel = "ContentBrowser.asset";
    {
        logger.log(Logger::Category::AssetManagement, "[ContentBrowser] ensureEditorWidgetsCreated: building ContentBrowser widget asset definition", Logger::LogLevel::INFO);

        json widgetJson = json::object();
        widgetJson["m_sizePixels"] = json{ {"x", 0.0f}, {"y", 220.0f} };
        widgetJson["m_positionPixels"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        widgetJson["m_anchor"] = "BottomLeft";
        widgetJson["m_fillX"] = true;
        widgetJson["m_zOrder"] = 2;

        json elements = json::array();
        json panel = json::object();
        panel["id"] = "ContentBrowser.Background";
        panel["type"] = "Panel";
        panel["from"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        panel["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
        panel["fillX"] = true;
        panel["fillY"] = true;
        panel["color"] = json{ {"x", 0.08f}, {"y", 0.09f}, {"z", 0.12f}, {"w", 0.9f} };
        panel["shaderVertex"] = "panel_vertex.glsl";
        panel["shaderFragment"] = "panel_fragment.glsl";
        elements.push_back(panel);

        json label = json::object();
        label["id"] = "ContentBrowser.Title";
        label["type"] = "Text";
        label["from"] = json{ {"x", 0.02f}, {"y", 0.05f} };
        label["to"] = json{ {"x", 0.6f}, {"y", 0.2f} };
        label["color"] = json{ {"x", 1.0f}, {"y", 1.0f}, {"z", 1.0f}, {"w", 1.0f} };
        label["text"] = "Content Browser";
        label["font"] = "default.ttf";
        label["fontSize"] = 18.0f;
        label["sizeToContent"] = true;
        label["shaderVertex"] = "text_vertex.glsl";
        label["shaderFragment"] = "text_fragment.glsl";
        elements.push_back(label);

        // Path bar (breadcrumb) between title row and grid
        json pathBar = json::object();
        pathBar["id"] = "ContentBrowser.PathBar";
        pathBar["type"] = "StackPanel";
        pathBar["from"] = json{ {"x", 0.25f}, {"y", 0.01f} };
        pathBar["to"] = json{ {"x", 0.98f}, {"y", 0.14f} };
        pathBar["orientation"] = "Horizontal";
        pathBar["padding"] = json{ {"x", 2.0f}, {"y", 0.0f} };
        pathBar["color"] = json{ {"x", 0.09f}, {"y", 0.10f}, {"z", 0.13f}, {"w", 0.9f} };
        pathBar["shaderVertex"] = "panel_vertex.glsl";
        pathBar["shaderFragment"] = "panel_fragment.glsl";
        elements.push_back(pathBar);

        json treePanel = json::object();
        treePanel["id"] = "ContentBrowser.Tree";
        treePanel["type"] = "TreeView";
        treePanel["from"] = json{ {"x", 0.0f}, {"y", 0.22f} };
        treePanel["to"] = json{ {"x", 0.22f}, {"y", 0.95f} };
        treePanel["padding"] = json{ {"x", 2.0f}, {"y", 2.0f} };
        treePanel["fillY"] = false;
        treePanel["sizeToContent"] = false;
        treePanel["scrollable"] = true;
        treePanel["color"] = json{ {"x", 0.07f}, {"y", 0.08f}, {"z", 0.10f}, {"w", 0.95f} };
        elements.push_back(treePanel);

        json grid = json::object();
        grid["id"] = "ContentBrowser.Grid";
        grid["type"] = "Grid";
        grid["from"] = json{ {"x", 0.25f}, {"y", 0.16f} };
        grid["to"] = json{ {"x", 0.98f}, {"y", 0.95f} };
        grid["padding"] = json{ {"x", 8.0f}, {"y", 8.0f} };
        grid["scrollable"] = true;
        grid["color"] = json{ {"x", 0.06f}, {"y", 0.07f}, {"z", 0.09f}, {"w", 0.0f} };
        elements.push_back(grid);

        widgetJson["m_elements"] = elements;

        logger.log(Logger::Category::AssetManagement, "[ContentBrowser] widget JSON has " + std::to_string(elements.size()) + " top-level elements (Background, Title, Tree, Grid)", Logger::LogLevel::INFO);

        auto widget = std::make_shared<AssetData>();
        widget->setName("ContentBrowser");
        widget->setData(std::move(widgetJson));

        const fs::path widgetsRoot = getEditorWidgetsRootPath();
        std::error_code ec;
        fs::create_directories(widgetsRoot, ec);
        const fs::path abs = widgetsRoot / fs::path(contentBrowserWidgetRel);
        logger.log(Logger::Category::AssetManagement, "[ContentBrowser] widget asset path: " + abs.string(), Logger::LogLevel::INFO);

        bool existsAndOk = false;
        if (fs::exists(abs))
        {
            AssetType headerType{ AssetType::Unknown };
            if (readAssetHeaderType(abs, headerType) && headerType == AssetType::Widget)
            {
                // Verify the file contains the expected TreeView tree panel
                std::ifstream check(abs, std::ios::in);
                if (check.is_open())
                {
                    const std::string content((std::istreambuf_iterator<char>(check)),
                                              std::istreambuf_iterator<char>());
                    existsAndOk = content.find("ContentBrowser.Tree") != std::string::npos
                               && content.find("TreeView") != std::string::npos;
                }
            }
            logger.log(Logger::Category::AssetManagement, "[ContentBrowser] existing file validated: existsAndOk=" + std::to_string(existsAndOk), Logger::LogLevel::INFO);
        }
        else
        {
            logger.log(Logger::Category::AssetManagement, "[ContentBrowser] widget asset file does not exist yet, will create", Logger::LogLevel::INFO);
        }
        if (!existsAndOk)
        {
            std::ofstream out(abs, std::ios::out | std::ios::trunc);
            if (out.is_open())
            {
                json fileJson = json::object();
                fileJson["magic"] = 0x41535453;
                fileJson["version"] = 2;
                fileJson["type"] = static_cast<int>(AssetType::Widget);
                fileJson["name"] = widget->getName();
                fileJson["data"] = widget->getData();
                out << fileJson.dump(4);
                if (!out.good())
                {
                    logger.log(Logger::Category::AssetManagement, "[ContentBrowser] Failed to write editor widget asset.", Logger::LogLevel::ERROR);
                }
                else
                {
                    logger.log(Logger::Category::AssetManagement, "[ContentBrowser] widget asset written successfully", Logger::LogLevel::INFO);
                }
            }
            else
            {
                logger.log(Logger::Category::AssetManagement, "[ContentBrowser] Failed to open editor widget asset for writing: " + abs.string(), Logger::LogLevel::ERROR);
            }
        }
    }

    const std::string statusBarWidgetRel = "StatusBar.asset";
    {
        json widgetJson = json::object();
        widgetJson["m_sizePixels"] = json{ {"x", 0.0f}, {"y", 32.0f} };
        widgetJson["m_positionPixels"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        widgetJson["m_anchor"] = "BottomLeft";
        widgetJson["m_fillX"] = true;
        widgetJson["m_zOrder"] = 3;

        json elements = json::array();

        json bg = json::object();
        bg["id"] = "StatusBar.Background";
        bg["type"] = "Panel";
        bg["from"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        bg["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
        bg["fillX"] = true;
        bg["fillY"] = true;
        bg["color"] = json{ {"x", 0.1f}, {"y", 0.1f}, {"z", 0.13f}, {"w", 0.95f} };
        bg["shaderVertex"] = "panel_vertex.glsl";
        bg["shaderFragment"] = "panel_fragment.glsl";
        elements.push_back(bg);

        json row = json::object();
        row["id"] = "StatusBar.Row";
        row["type"] = "StackPanel";
        row["from"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        row["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
        row["orientation"] = "Horizontal";
        row["fillX"] = true;
        row["fillY"] = true;
        row["padding"] = json{ {"x", 4.0f}, {"y", 2.0f} };
        row["color"] = json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };

        json undoBtn = json::object();
        undoBtn["id"] = "StatusBar.Undo";
        undoBtn["type"] = "Button";
        undoBtn["text"] = "Undo";
        undoBtn["font"] = "default.ttf";
        undoBtn["fontSize"] = 12.0f;
        undoBtn["textAlignH"] = "Center";
        undoBtn["textAlignV"] = "Center";
        undoBtn["padding"] = json{ {"x", 8.0f}, {"y", 4.0f} };
        undoBtn["minSize"] = json{ {"x", 60.0f}, {"y", 26.0f} };
        undoBtn["color"] = json{ {"x", 0.16f}, {"y", 0.16f}, {"z", 0.2f}, {"w", 0.95f} };
        undoBtn["hoverColor"] = json{ {"x", 0.24f}, {"y", 0.24f}, {"z", 0.3f}, {"w", 0.98f} };
        undoBtn["textColor"] = json{ {"x", 0.7f}, {"y", 0.7f}, {"z", 0.75f}, {"w", 1.0f} };
        undoBtn["shaderVertex"] = "button_vertex.glsl";
        undoBtn["shaderFragment"] = "button_fragment.glsl";
        undoBtn["isHitTestable"] = true;
        undoBtn["clickEvent"] = "StatusBar.Undo";

        json redoBtn = json::object();
        redoBtn["id"] = "StatusBar.Redo";
        redoBtn["type"] = "Button";
        redoBtn["text"] = "Redo";
        redoBtn["font"] = "default.ttf";
        redoBtn["fontSize"] = 12.0f;
        redoBtn["textAlignH"] = "Center";
        redoBtn["textAlignV"] = "Center";
        redoBtn["padding"] = json{ {"x", 8.0f}, {"y", 4.0f} };
        redoBtn["minSize"] = json{ {"x", 60.0f}, {"y", 26.0f} };
        redoBtn["color"] = json{ {"x", 0.16f}, {"y", 0.16f}, {"z", 0.2f}, {"w", 0.95f} };
        redoBtn["hoverColor"] = json{ {"x", 0.24f}, {"y", 0.24f}, {"z", 0.3f}, {"w", 0.98f} };
        redoBtn["textColor"] = json{ {"x", 0.7f}, {"y", 0.7f}, {"z", 0.75f}, {"w", 1.0f} };
        redoBtn["shaderVertex"] = "button_vertex.glsl";
        redoBtn["shaderFragment"] = "button_fragment.glsl";
        redoBtn["isHitTestable"] = true;
        redoBtn["clickEvent"] = "StatusBar.Redo";

        json spacer = json::object();
        spacer["id"] = "StatusBar.Spacer";
        spacer["type"] = "Panel";
        spacer["fillX"] = true;
        spacer["color"] = json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };

        json dirtyLabel = json::object();
        dirtyLabel["id"] = "StatusBar.DirtyLabel";
        dirtyLabel["type"] = "Text";
        dirtyLabel["text"] = "No unsaved changes";
        dirtyLabel["font"] = "default.ttf";
        dirtyLabel["fontSize"] = 12.0f;
        dirtyLabel["textAlignH"] = "Center";
        dirtyLabel["textAlignV"] = "Center";
        dirtyLabel["textColor"] = json{ {"x", 0.6f}, {"y", 0.6f}, {"z", 0.65f}, {"w", 1.0f} };
        dirtyLabel["minSize"] = json{ {"x", 0.0f}, {"y", 26.0f} };
        dirtyLabel["padding"] = json{ {"x", 8.0f}, {"y", 0.0f} };

        json saveBtn = json::object();
        saveBtn["id"] = "StatusBar.Save";
        saveBtn["type"] = "Button";
        saveBtn["text"] = "Save All";
        saveBtn["font"] = "default.ttf";
        saveBtn["fontSize"] = 12.0f;
        saveBtn["textAlignH"] = "Center";
        saveBtn["textAlignV"] = "Center";
        saveBtn["padding"] = json{ {"x", 10.0f}, {"y", 4.0f} };
        saveBtn["minSize"] = json{ {"x", 80.0f}, {"y", 26.0f} };
        saveBtn["color"] = json{ {"x", 0.15f}, {"y", 0.35f}, {"z", 0.15f}, {"w", 0.95f} };
        saveBtn["hoverColor"] = json{ {"x", 0.2f}, {"y", 0.5f}, {"z", 0.2f}, {"w", 0.98f} };
        saveBtn["textColor"] = json{ {"x", 0.95f}, {"y", 0.95f}, {"z", 0.95f}, {"w", 1.0f} };
        saveBtn["shaderVertex"] = "button_vertex.glsl";
        saveBtn["shaderFragment"] = "button_fragment.glsl";
        saveBtn["isHitTestable"] = true;
        saveBtn["clickEvent"] = "StatusBar.Save";

        row["children"] = json::array({ undoBtn, redoBtn, spacer, dirtyLabel, saveBtn });
        elements.push_back(row);

        widgetJson["m_elements"] = elements;

        auto widget = std::make_shared<AssetData>();
        widget->setName("StatusBar");
        widget->setData(std::move(widgetJson));

        const fs::path widgetsRoot = getEditorWidgetsRootPath();
        const fs::path abs = widgetsRoot / fs::path(statusBarWidgetRel);
        bool existsAndOk = false;
        if (fs::exists(abs))
        {
            AssetType headerType{ AssetType::Unknown };
            existsAndOk = readAssetHeaderType(abs, headerType) && headerType == AssetType::Widget;
            if (existsAndOk)
            {
                std::ifstream in(abs, std::ios::in | std::ios::binary);
                if (in.is_open())
                {
                    json fileJson = json::parse(in, nullptr, false);
                    existsAndOk = !fileJson.is_discarded() && fileJson.is_object() && fileJson.contains("data");
                    if (existsAndOk)
                    {
                        const auto& data = fileJson.at("data");
                        existsAndOk = data.is_object() && data.contains("m_elements");
                    }
                }
            }
        }
        if (!existsAndOk)
        {
            std::ofstream out(abs, std::ios::out | std::ios::trunc);
            if (out.is_open())
            {
                json fileJson = json::object();
                fileJson["magic"] = 0x41535453;
                fileJson["version"] = 2;
                fileJson["type"] = static_cast<int>(AssetType::Widget);
                fileJson["name"] = widget->getName();
                fileJson["data"] = widget->getData();
                            out << fileJson.dump(4);
                            }
                        }
                    }

                    // WorldGrid material – stored in engine Content/Materials (not project Content)
                    {
                        const fs::path materialsDir = fs::current_path() / "Content" / "Materials";
                        std::error_code ec;
                        fs::create_directories(materialsDir, ec);
                        const fs::path abs = materialsDir / "WorldGrid.asset";
                        bool existsAndOk = false;
                        if (fs::exists(abs))
                        {
                            AssetType headerType{ AssetType::Unknown };
                            existsAndOk = readAssetHeaderType(abs, headerType) && headerType == AssetType::Material;
                        }
                        if (!existsAndOk)
                        {
                            std::ofstream out(abs, std::ios::out | std::ios::trunc);
                            if (out.is_open())
                            {
                                json matData = json::object();
                                matData["m_shaderFragment"] = "grid_fragment.glsl";

                                json fileJson = json::object();
                                fileJson["magic"] = 0x41535453;
                                fileJson["version"] = 2;
                                fileJson["type"] = static_cast<int>(AssetType::Material);
                                fileJson["name"] = "WorldGrid";
                                fileJson["data"] = matData;
                                out << fileJson.dump(4);
                            }
                        }
                    }
                }

                void AssetManager::createWorldSettingsWidgetAsset()
{
    auto& logger = Logger::Instance();
    const std::string worldSettingsWidgetRel = "WorldSettings.asset";

    json widgetJson = json::object();
    widgetJson["m_sizePixels"] = json{ {"x", 260.0f}, {"y", 0.0f} };
    widgetJson["m_positionPixels"] = json{ {"x", 0.0f}, {"y", 0.0f} };
    widgetJson["m_anchor"] = "TopLeft";
    widgetJson["m_fillY"] = true;
    widgetJson["m_zOrder"] = 1;

    json elements = json::array();

    json panel = json::object();
    panel["id"] = "WorldSettings.Background";
    panel["type"] = "Panel";
    panel["from"] = json{ {"x", 0.0f}, {"y", 0.0f} };
    panel["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
    panel["fillX"] = true;
    panel["fillY"] = true;
    panel["color"] = json{ {"x", 0.09f}, {"y", 0.1f}, {"z", 0.12f}, {"w", 0.95f} };
    panel["shaderVertex"] = "panel_vertex.glsl";
    panel["shaderFragment"] = "panel_fragment.glsl";
    elements.push_back(panel);

    json stack = json::object();
    stack["id"] = "WorldSettings.Stack";
    stack["type"] = "StackPanel";
    stack["from"] = json{ {"x", 0.0f}, {"y", 0.0f} };
    stack["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
    stack["fillX"] = true;
    stack["fillY"] = true;
    stack["sizeToContent"] = true;
    stack["padding"] = json{ {"x", 12.0f}, {"y", 12.0f} };
    stack["orientation"] = "Vertical";
    stack["scrollable"] = true;
    stack["color"] = json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };

    json title = json::object();
    title["id"] = "WorldSettings.Title";
    title["type"] = "Text";
    title["text"] = "World Settings";
    title["font"] = "default.ttf";
    title["fontSize"] = 18.0f;
    title["textAlignH"] = "Left";
    title["textAlignV"] = "Center";
    title["padding"] = json{ {"x", 4.0f}, {"y", 4.0f} };
    title["textColor"] = json{ {"x", 0.95f}, {"y", 0.95f}, {"z", 0.95f}, {"w", 1.0f} };
    title["minSize"] = json{ {"x", 0.0f}, {"y", 26.0f} };

    json clearLabel = json::object();
    clearLabel["id"] = "WorldSettings.ClearColor.Label";
    clearLabel["type"] = "Text";
    clearLabel["text"] = "Clear Color";
    clearLabel["font"] = "default.ttf";
    clearLabel["fontSize"] = 14.0f;
    clearLabel["textAlignH"] = "Left";
    clearLabel["textAlignV"] = "Center";
    clearLabel["padding"] = json{ {"x", 4.0f}, {"y", 4.0f} };
    clearLabel["textColor"] = json{ {"x", 0.85f}, {"y", 0.85f}, {"z", 0.85f}, {"w", 1.0f} };
    clearLabel["minSize"] = json{ {"x", 0.0f}, {"y", 20.0f} };

    json colorPicker = json::object();
    colorPicker["id"] = "WorldSettings.ClearColor";
    colorPicker["type"] = "ColorPicker";
    colorPicker["compact"] = false;
    colorPicker["minSize"] = json{ {"x", 200.0f}, {"y", 80.0f} };

    json separator = json::object();
    separator["id"] = "WorldSettings.Tools.Sep";
    separator["type"] = "Panel";
    separator["color"] = json{ {"x", 0.2f}, {"y", 0.2f}, {"z", 0.22f}, {"w", 1.0f} };
    separator["minSize"] = json{ {"x", 0.0f}, {"y", 1.0f} };
    separator["padding"] = json{ {"x", 0.0f}, {"y", 12.0f} };

    json toolsLabel = json::object();
    toolsLabel["id"] = "WorldSettings.Tools.Label";
    toolsLabel["type"] = "Text";
    toolsLabel["text"] = "Tools";
    toolsLabel["font"] = "default.ttf";
    toolsLabel["fontSize"] = 16.0f;
    toolsLabel["textAlignH"] = "Left";
    toolsLabel["textAlignV"] = "Center";
    toolsLabel["padding"] = json{ {"x", 4.0f}, {"y", 4.0f} };
    toolsLabel["textColor"] = json{ {"x", 0.9f}, {"y", 0.9f}, {"z", 0.92f}, {"w", 1.0f} };
    toolsLabel["minSize"] = json{ {"x", 0.0f}, {"y", 24.0f} };

    json landscapeBtn = json::object();
    landscapeBtn["id"] = "WorldSettings.Tools.Landscape";
    landscapeBtn["type"] = "Button";
    landscapeBtn["text"] = "Landscape Manager...";
    landscapeBtn["font"] = "default.ttf";
    landscapeBtn["fontSize"] = 13.0f;
    landscapeBtn["textAlignH"] = "Center";
    landscapeBtn["textAlignV"] = "Center";
    landscapeBtn["padding"] = json{ {"x", 8.0f}, {"y", 4.0f} };
    landscapeBtn["minSize"] = json{ {"x", 0.0f}, {"y", 28.0f} };
    landscapeBtn["color"] = json{ {"x", 0.15f}, {"y", 0.15f}, {"z", 0.18f}, {"w", 1.0f} };
    landscapeBtn["hoverColor"] = json{ {"x", 0.2f}, {"y", 0.2f}, {"z", 0.24f}, {"w", 1.0f} };
    landscapeBtn["textColor"] = json{ {"x", 0.8f}, {"y", 0.8f}, {"z", 0.85f}, {"w", 1.0f} };
    landscapeBtn["shaderVertex"] = "button_vertex.glsl";
    landscapeBtn["shaderFragment"] = "button_fragment.glsl";
    landscapeBtn["isHitTestable"] = true;
    landscapeBtn["clickEvent"] = "WorldSettings.Tools.Landscape";

    stack["children"] = json::array({ title, clearLabel, colorPicker, separator, toolsLabel, landscapeBtn });

    elements.push_back(stack);
    widgetJson["m_elements"] = elements;

    auto widget = std::make_shared<AssetData>();
    widget->setName("WorldSettings");
    widget->setData(std::move(widgetJson));

    const fs::path widgetsRoot = getEditorWidgetsRootPath();
    std::error_code ec;
    fs::create_directories(widgetsRoot, ec);
    const fs::path abs = widgetsRoot / fs::path(worldSettingsWidgetRel);
    bool existsAndOk = false;
    if (fs::exists(abs))
    {
        AssetType headerType{ AssetType::Unknown };
        existsAndOk = readAssetHeaderType(abs, headerType) && headerType == AssetType::Widget;
        if (existsAndOk)
        {
            std::ifstream in(abs, std::ios::in | std::ios::binary);
            if (in.is_open())
            {
                json fileJson = json::parse(in, nullptr, false);
                bool hasFillY = false;
                if (!fileJson.is_discarded() && fileJson.is_object() && fileJson.contains("data"))
                {
                    const auto& data = fileJson.at("data");
                    if (data.is_object() && data.contains("m_fillY") &&
                        data.at("m_fillY").is_boolean() && data.at("m_fillY").get<bool>())
                    {
                        hasFillY = true;
                    }
                }
                existsAndOk = existsAndOk && hasFillY;
            }
        }
    }
    if (!existsAndOk)
    {
        std::ofstream out(abs, std::ios::out | std::ios::trunc);
        if (out.is_open())
        {
            json fileJson = json::object();
            fileJson["magic"] = 0x41535453;
            fileJson["version"] = 2;
            fileJson["type"] = static_cast<int>(AssetType::Widget);
            fileJson["name"] = widget->getName();
            fileJson["data"] = widget->getData();
            out << fileJson.dump(4);
            if (!out.good())
            {
                logger.log(Logger::Category::AssetManagement, "Failed to write editor widget asset.", Logger::LogLevel::ERROR);
            }
        }
        else
        {
            logger.log(Logger::Category::AssetManagement, "Failed to open editor widget asset for writing.", Logger::LogLevel::ERROR);
        }
    }
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

	// 2b) container2 textures + material (diffuse + specular)
	const std::string container2TexRel = (fs::path("Textures") / "container2.asset").generic_string();
	{
		auto tex = std::make_shared<AssetData>();
		tex->setName("container2");
		json texData = json::object();
		const fs::path container2PngPath = fs::current_path() / "Content" / "Textures" / "container2.png";
		if (fs::exists(container2PngPath))
		{
			texData["m_sourcePath"] = (fs::path("Content") / "Textures" / "container2.png").generic_string();
		}
		else
		{
			int width = 2;
			int height = 2;
			int channels = 4;
			std::vector<unsigned char> pixels{
				200, 150, 100, 255, 180, 130,  80, 255,
				160, 110,  60, 255, 140,  90,  40, 255
			};
			texData["m_width"] = width;
			texData["m_height"] = height;
			texData["m_channels"] = channels;
			texData["m_data"] = std::move(pixels);
		}
		tex->setData(std::move(texData));
		const bool forceOverwrite = fs::exists(container2PngPath);
		ensureOnDisk(container2TexRel, AssetType::Texture, tex, forceOverwrite);
	}

	const std::string container2SpecTexRel = (fs::path("Textures") / "container2_specular.asset").generic_string();
	{
		auto tex = std::make_shared<AssetData>();
		tex->setName("container2_specular");
		json texData = json::object();
		const fs::path container2SpecPath = fs::current_path() / "Content" / "Textures" / "container2_specular.png";
		if (fs::exists(container2SpecPath))
		{
			texData["m_sourcePath"] = (fs::path("Content") / "Textures" / "container2_specular.png").generic_string();
		}
		else
		{
			int width = 2;
			int height = 2;
			int channels = 4;
			std::vector<unsigned char> pixels{
				128, 128, 128, 255, 200, 200, 200, 255,
				200, 200, 200, 255, 128, 128, 128, 255
			};
			texData["m_width"] = width;
			texData["m_height"] = height;
			texData["m_channels"] = channels;
			texData["m_data"] = std::move(pixels);
		}
		tex->setData(std::move(texData));
		const bool forceOverwrite = fs::exists(fs::current_path() / "Content" / "Textures" / "container2_specular.png");
		ensureOnDisk(container2SpecTexRel, AssetType::Texture, tex, forceOverwrite);
	}

	const std::string containerMatRel = (fs::path("Materials") / "container.asset").generic_string();
	{
		auto mat = std::make_shared<AssetData>();
		mat->setName("ContainerMaterial");
		json matData = json::object();
		matData["m_textureAssetPaths"] = std::vector<std::string>{ container2TexRel, container2SpecTexRel };
		matData["m_shininess"] = 64.0f;
		mat->setData(std::move(matData));
		ensureOnDisk(containerMatRel, AssetType::Material, mat);
	}

	const std::string defaultCubeScripts[] =
    {
        (fs::path("Scripts") / "DefaultCube1.py").generic_string(),
        (fs::path("Scripts") / "DefaultCube2.py").generic_string(),
        (fs::path("Scripts") / "DefaultCube3.py").generic_string(),
        (fs::path("Scripts") / "DefaultCube4.py").generic_string(),
        (fs::path("Scripts") / "DefaultCube5.py").generic_string()
    };
    for (const auto& scriptRel : defaultCubeScripts)
    {
        const fs::path absScript = contentRoot / fs::path(scriptRel);
        if (fs::exists(absScript))
        {
            continue;
        }
        std::error_code ec;
        fs::create_directories(absScript.parent_path(), ec);
        std::ofstream out(absScript, std::ios::out | std::ios::trunc);
        if (out.is_open())
        {
            out << "import engine\n\n";
            out << "def onloaded(entity):\n";
            out << "    pass\n\n";
            out << "def tick(entity, dt):\n";
            out << "    pass\n";
            if (!out.good())
            {
                logger.log(Logger::Category::AssetManagement, "Failed to write default cube script.", Logger::LogLevel::ERROR);
            }
        }
        else
        {
            logger.log(Logger::Category::AssetManagement, "Failed to open default cube script for writing.", Logger::LogLevel::ERROR);
        }
    }

    // 3) default 3D quad model

    const std::vector<float> cubeVertices{
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
    const std::string quad3dRel = "default_quad3d.asset";
    const std::string pointLightRel = (fs::path("Lights") / "PointLight.asset").generic_string();
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
            quadData["m_vertices"] = cubeVertices;
            quadData["m_indices"] = std::vector<uint32_t>{};
            quad->setData(std::move(quadData));

            ensureOnDisk(quad3dRel, AssetType::Model3D, quad);
        }
    }

    {
        const fs::path abs = contentRoot / fs::path(pointLightRel);
        bool existsAndOk = false;
        if (fs::exists(abs))
        {
            AssetType headerType{ AssetType::Unknown };
            existsAndOk = readAssetHeaderType(abs, headerType) && headerType == AssetType::PointLight;
        }

        if (!existsAndOk)
        {
            auto lightAsset = std::make_shared<AssetData>();
            lightAsset->setName("PointLight");
            json lightData = json::object();
            lightData["m_vertices"] = cubeVertices;
            lightData["m_indices"] = std::vector<uint32_t>{};
            lightData["m_shaderVertex"] = "vertex.glsl";
            lightData["m_shaderFragment"] = "light_fragment.glsl";
            lightAsset->setData(std::move(lightData));
            ensureOnDisk(pointLightRel, AssetType::PointLight, lightAsset);
        }
    }

    const std::string defaultLevelRel = (fs::path("Levels") / "DefaultLevel.map").generic_string();
    {
        std::error_code ec;
        const fs::path scriptsRoot = contentRoot / "Scripts";
        fs::create_directories(scriptsRoot, ec);
        const fs::path defaultScriptAbs = scriptsRoot / "DefaultCubeScript.py";
        if (!fs::exists(defaultScriptAbs))
        {
            std::ofstream scriptOut(defaultScriptAbs, std::ios::out | std::ios::trunc);
            if (scriptOut.is_open())
            {
                scriptOut << "def onloaded(entity):\n";
                scriptOut << "    print('Default script loaded for', entity)\n\n";
                scriptOut << "def tick(entity, dt):\n";
                scriptOut << "    pass\n";
            }
        }

        const std::string defaultLevelScriptRel = (fs::path("Scripts") / "LevelScript.py").generic_string();
        const fs::path defaultLevelScriptAbs = scriptsRoot / "LevelScript.py";
        if (!fs::exists(defaultLevelScriptAbs))
        {
            std::ofstream scriptOut(defaultLevelScriptAbs, std::ios::out | std::ios::trunc);
            if (scriptOut.is_open())
            {
                scriptOut << "def on_level_loaded():\n";
                scriptOut << "    pass\n\n";
                scriptOut << "def on_level_unloaded():\n";
                scriptOut << "    pass\n";
            }
        }

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
        components["Name"] = json{ {"displayName", "Cube A"} };
        components["Script"] = json{ {"scriptPath", defaultCubeScripts[0]}};
        entity["components"] = components;
        entities.push_back(entity);

        json entity2 = json::object();
        entity2["id"] = 2;

        json components2 = json::object();
        components2["Transform"] = json{
            {"position", json::array({ 2.0f, 0.0f, 0.0f })},
            {"rotation", json::array({ 0.0f, 45.0f, 0.0f })},
            {"scale", json::array({ 1.0f, 1.0f, 1.0f })}
        };
        components2["Mesh"] = json{ {"meshAssetPath", quad3dRel} };
        components2["Material"] = json{ {"materialAssetPath", wallMatRel} };
        components2["Script"] = json{ {"scriptPath", defaultCubeScripts[1]} };
        components2["Name"] = json{ {"displayName", "Cube B"} };
        entity2["components"] = components2;
        entities.push_back(entity2);

        json entity3 = json::object();
        entity3["id"] = 3;

        json components3 = json::object();
        components3["Transform"] = json{
            {"position", json::array({ -2.0f, 0.0f, 0.0f })},
            {"rotation", json::array({ 0.0f, -30.0f, 0.0f })},
            {"scale", json::array({ 1.0f, 1.0f, 1.0f })}
        };
        components3["Mesh"] = json{ {"meshAssetPath", quad3dRel} };
        components3["Material"] = json{ {"materialAssetPath", wallMatRel} };
        components3["Script"] = json{ {"scriptPath", defaultCubeScripts[2]} };
        components3["Name"] = json{ {"displayName", "Cube C"} };
        entity3["components"] = components3;
        entities.push_back(entity3);

        json entity4 = json::object();
        entity4["id"] = 4;

        json components4 = json::object();
        components4["Transform"] = json{
            {"position", json::array({ 0.0f, 0.0f, 2.5f })},
            {"rotation", json::array({ 0.0f, 90.0f, 0.0f })},
            {"scale", json::array({ 1.0f, 1.0f, 1.0f })}
        };
        components4["Mesh"] = json{ {"meshAssetPath", quad3dRel} };
        components4["Material"] = json{ {"materialAssetPath", containerMatRel} };
        components4["Script"] = json{ {"scriptPath", defaultCubeScripts[3]} };
        components4["Name"] = json{ {"displayName", "Cube D"} };
        entity4["components"] = components4;
        entities.push_back(entity4);

        json entity5 = json::object();
        entity5["id"] = 5;

        json components5 = json::object();
        components5["Transform"] = json{
            {"position", json::array({ 0.0f, 0.0f, -2.5f })},
            {"rotation", json::array({ 0.0f, 0.0f, 0.0f })},
            {"scale", json::array({ 1.0f, 1.0f, 1.0f })}
        };
        components5["Mesh"] = json{ {"meshAssetPath", quad3dRel} };
        components5["Material"] = json{ {"materialAssetPath", containerMatRel} };
        components5["Script"] = json{ {"scriptPath", defaultCubeScripts[4]} };
        components5["Name"] = json{ {"displayName", "Cube E"} };
        entity5["components"] = components5;
        entities.push_back(entity5);

        json lightEntity = json::object();
        lightEntity["id"] = 6;

        json lightComponents = json::object();
        lightComponents["Transform"] = json{
            {"position", json::array({ 1.0f, 1.2f, 2.5f })},
            {"rotation", json::array({ 0.0f, 0.0f, 0.0f })},
            {"scale", json::array({ 0.25f, 0.25f, 0.25f })}
        };
        lightComponents["Mesh"] = json{ {"meshAssetPath", pointLightRel} };
        lightComponents["Material"] = json{ {"materialAssetPath", wallMatRel} };
        lightComponents["Name"] = json{ {"displayName", "Point Light"} };
        lightComponents["Light"] = json{
            {"type", static_cast<int>(ECS::LightComponent::LightType::Point)},
            {"color", json::array({ 1.0f, 1.0f, 1.0f })},
            {"intensity", 1.0f},
            {"range", 10.0f},
            {"spotAngle", 30.0f}
        };
        lightEntity["components"] = lightComponents;
        entities.push_back(lightEntity);

        json dirLightEntity = json::object();
        dirLightEntity["id"] = 7;

        json dirLightComponents = json::object();
        dirLightComponents["Transform"] = json{
            {"position", json::array({ 0.0f, 5.0f, 0.0f })},
            {"rotation", json::array({ 50.0f, -30.0f, 0.0f })},
            {"scale", json::array({ 0.15f, 0.15f, 0.15f })}
        };
        dirLightComponents["Mesh"] = json{ {"meshAssetPath", pointLightRel} };
        dirLightComponents["Material"] = json{ {"materialAssetPath", wallMatRel} };
        dirLightComponents["Name"] = json{ {"displayName", "Directional Light"} };
        dirLightComponents["Light"] = json{
            {"type", static_cast<int>(ECS::LightComponent::LightType::Directional)},
            {"color", json::array({ 0.9f, 0.85f, 0.7f })},
            {"intensity", 0.4f},
            {"range", 0.0f},
            {"spotAngle", 0.0f}
        };
        dirLightEntity["components"] = dirLightComponents;
        entities.push_back(dirLightEntity);

        json spotLightEntity = json::object();
        spotLightEntity["id"] = 8;

        json spotLightComponents = json::object();
        spotLightComponents["Transform"] = json{
            {"position", json::array({ 2.0f, 2.5f, 0.0f })},
            {"rotation", json::array({ 60.0f, 0.0f, 0.0f })},
            {"scale", json::array({ 0.15f, 0.15f, 0.15f })}
        };
        spotLightComponents["Mesh"] = json{ {"meshAssetPath", pointLightRel} };
        spotLightComponents["Material"] = json{ {"materialAssetPath", wallMatRel} };
        spotLightComponents["Name"] = json{ {"displayName", "Spot Light"} };
        spotLightComponents["Light"] = json{
            {"type", static_cast<int>(ECS::LightComponent::LightType::Spot)},
            {"color", json::array({ 0.2f, 0.8f, 1.0f })},
            {"intensity", 3.5f},
            {"range", 25.0f},
            {"spotAngle", 25.0f}
        };
        spotLightEntity["components"] = spotLightComponents;
        entities.push_back(spotLightEntity);

        levelJson["Entities"] = entities;

        auto defaultLevel = std::make_unique<EngineLevel>();
        defaultLevel->setName("DefaultLevel");
        defaultLevel->setPath(defaultLevelRel);
        defaultLevel->setAssetType(AssetType::Level);
        defaultLevel->setLevelData(levelJson);
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
            diagnostics.setActiveLevel(std::move(defaultLevel));
        }
        else
        {
            logger.log(Logger::Category::AssetManagement, "Default level OK, loading from disk: " + defaultLevelRel, Logger::LogLevel::INFO);
            auto loadResult = loadLevelAsset(abs.string());
            if (!loadResult.success)
            {
                logger.log(Logger::Category::AssetManagement, "Failed to load existing level from disk, falling back to defaults: " + loadResult.errorMessage, Logger::LogLevel::WARNING);
                diagnostics.setActiveLevel(std::move(defaultLevel));
            }
        }
		diagnostics.setScenePrepared(false);
	}

	// Default Skybox assets — generate from engine-bundled skybox texture sets
	{
		const fs::path engineSkyboxRoot = fs::current_path() / "Content" / "Textures" / "SkyBoxes";
		const std::string skyboxNames[] = { "Sunrise", "Daytime" };
		// Each canonical face name maps to alternative file names (e.g. top/up, bottom/down)
		struct FaceAlias { std::string canonical; std::vector<std::string> names; };
		const FaceAlias faceAliases[] = {
			{ "right",  { "right" } },
			{ "left",   { "left" } },
			{ "top",    { "top", "up" } },
			{ "bottom", { "bottom", "down" } },
			{ "front",  { "front" } },
			{ "back",   { "back" } }
		};
		const std::string faceExts[] = { ".jpg", ".png", ".bmp" };

		for (const auto& skyboxName : skyboxNames)
		{
			const fs::path engineSkyboxDir = engineSkyboxRoot / skyboxName;
			if (!fs::exists(engineSkyboxDir) || !fs::is_directory(engineSkyboxDir))
			{
				continue;
			}

			// Check that at least one face image exists
			bool hasAnyFace = false;
			for (const auto& fa : faceAliases)
			{
				for (const auto& name : fa.names)
				{
					for (const auto& ext : faceExts)
					{
						if (fs::exists(engineSkyboxDir / (name + ext)))
						{
							hasAnyFace = true;
							break;
						}
					}
					if (hasAnyFace) break;
				}
				if (hasAnyFace) break;
			}
			if (!hasAnyFace)
			{
				logger.log(Logger::Category::AssetManagement,
					"Skybox '" + skyboxName + "' folder exists but contains no face images, skipping.",
					Logger::LogLevel::WARNING);
				continue;
			}

			// Copy face images to project Content/Skyboxes/<name>/
			const fs::path projectSkyboxDir = contentRoot / "Skyboxes" / skyboxName;
			std::error_code ec;
			fs::create_directories(projectSkyboxDir, ec);

			json facesJson = json::object();
			for (const auto& fa : faceAliases)
			{
				bool found = false;
				for (const auto& name : fa.names)
				{
					for (const auto& ext : faceExts)
					{
						const fs::path srcFace = engineSkyboxDir / (name + ext);
						if (fs::exists(srcFace))
						{
							// Copy with the original filename so the renderer can find it
							const fs::path destFace = projectSkyboxDir / (name + ext);
							fs::copy_file(srcFace, destFace, fs::copy_options::skip_existing, ec);
							facesJson[fa.canonical] = fs::relative(destFace, contentRoot).generic_string();
							found = true;
							break;
						}
					}
					if (found) break;
				}
			}

			// Create the .asset file
			const std::string skyboxAssetRel = (fs::path("Skyboxes") / (skyboxName + ".asset")).generic_string();
			auto skybox = std::make_shared<AssetData>();
			skybox->setName(skyboxName);
			json skyboxData = json::object();
			skyboxData["faces"] = facesJson;
			skyboxData["folderPath"] = fs::relative(projectSkyboxDir, fs::path(diagnostics.getProjectInfo().projectPath)).generic_string();
			skybox->setData(std::move(skyboxData));
			ensureOnDisk(skyboxAssetRel, AssetType::Skybox, skybox);
		}
	}

	logger.log(Logger::Category::AssetManagement, "Default assets ensured.", Logger::LogLevel::INFO);
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
	auto pathIt = m_loadedAssetsByPath.find(path);
	if (pathIt != m_loadedAssetsByPath.end())
	{
		auto assetIt = m_loadedAssets.find(pathIt->second);
		if (assetIt != m_loadedAssets.end())
			return assetIt->second;
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

std::string AssetManager::getEditorWidgetPath(const std::string& relativeToEditorWidgets) const
{
    fs::path p = getEditorWidgetsRootPath() / fs::path(relativeToEditorWidgets);
    return p.lexically_normal().string();
}

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
            { "All Supported", "png;jpg;jpeg;bmp;tga;hdr;wav;obj;fbx;gltf;glb;dae;3ds;blend;stl;ply;x3d;glsl;vert;frag;geom;comp;py" },
            { "Image Files", "png;jpg;jpeg;bmp;tga;hdr" },
            { "3D Models", "obj;fbx;gltf;glb;dae;3ds;blend;stl;ply;x3d" },
            { "Audio Files", "wav" },
            { "Shaders", "glsl;vert;frag;geom;comp" },
            { "Scripts", "py" },
            { "All Files", "*" }
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

	if (!diagnostics.isProjectLoaded())
	{
		logger.log(Logger::Category::AssetManagement, "Import failed: no project loaded.", Logger::LogLevel::ERROR);
		diagnostics.updateActionProgress(ActionID, false);
		return;
	}

	if (!fs::exists(path))
	{
		logger.log(Logger::Category::AssetManagement, "Import asset failed: file does not exist: " + path, Logger::LogLevel::ERROR);
		diagnostics.updateActionProgress(ActionID, false);
		return;
	}

	const fs::path sourcePath(path);
	const AssetType detectedType = (preferredType != AssetType::Unknown)
		? preferredType
		: DetectAssetTypeFromPath(sourcePath);

	if (detectedType == AssetType::Unknown)
	{
		logger.log(Logger::Category::AssetManagement, "Import failed: unsupported file format: " + sourcePath.extension().string(), Logger::LogLevel::ERROR);
		diagnostics.updateActionProgress(ActionID, false);
		return;
	}

	const std::string assetName = sanitizeName(sourcePath.stem().string());
	const fs::path contentDir = fs::path(diagnostics.getProjectInfo().projectPath) / "Content";
	const fs::path destAssetPath = contentDir / (assetName + ".asset");
	const std::string relPath = fs::relative(destAssetPath, contentDir).generic_string();

	json data = json::object();
	bool success = false;

	switch (detectedType)
	{
	case AssetType::Texture:
	{
		int width = 0, height = 0, channels = 0;
		unsigned char* imgData = stbi_load(path.c_str(), &width, &height, &channels, 4);
		if (!imgData)
		{
			logger.log(Logger::Category::AssetManagement, "Import failed: stb_image could not load: " + path, Logger::LogLevel::ERROR);
			break;
		}
		channels = 4;

		// Copy source file to Content folder
		const fs::path destSourcePath = contentDir / sourcePath.filename();
		std::error_code ec;
		fs::copy_file(sourcePath, destSourcePath, fs::copy_options::overwrite_existing, ec);

		const std::string relSourcePath = fs::relative(destSourcePath, fs::path(diagnostics.getProjectInfo().projectPath)).generic_string();
		data["m_sourcePath"] = relSourcePath;
		data["m_width"] = width;
		data["m_height"] = height;
		data["m_channels"] = channels;

		stbi_image_free(imgData);
		success = true;
		break;
	}

	case AssetType::Audio:
	{
		// Copy source .wav to Content folder
		const fs::path destSourcePath = contentDir / sourcePath.filename();
		std::error_code ec;
		fs::copy_file(sourcePath, destSourcePath, fs::copy_options::overwrite_existing, ec);

		const std::string relSourcePath = fs::relative(destSourcePath, fs::path(diagnostics.getProjectInfo().projectPath)).generic_string();
		data["m_sourcePath"] = relSourcePath;
		data["m_format"] = "wav";
		success = true;
		break;
	}

	case AssetType::Model3D:
	{
		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile(path,
			aiProcess_Triangulate |
			aiProcess_GenNormals |
			aiProcess_FlipUVs |
			aiProcess_JoinIdenticalVertices |
			aiProcess_OptimizeMeshes);

		if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE))
		{
			logger.log(Logger::Category::AssetManagement, "Import failed: Assimp error: " + std::string(importer.GetErrorString()), Logger::LogLevel::ERROR);
			break;
		}

		// Collect all meshes into a single vertex/index buffer (pos3 + uv2 layout)
		std::vector<float> vertices;
		std::vector<uint32_t> indices;
		uint32_t indexOffset = 0;

		for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
		{
			const aiMesh* mesh = scene->mMeshes[m];
			for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
			{
				vertices.push_back(mesh->mVertices[v].x);
				vertices.push_back(mesh->mVertices[v].y);
				vertices.push_back(mesh->mVertices[v].z);

				if (mesh->mTextureCoords[0])
				{
					vertices.push_back(mesh->mTextureCoords[0][v].x);
					vertices.push_back(mesh->mTextureCoords[0][v].y);
				}
				else
				{
					vertices.push_back(0.0f);
					vertices.push_back(0.0f);
				}
			}

			for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
			{
				const aiFace& face = mesh->mFaces[f];
				for (unsigned int i = 0; i < face.mNumIndices; ++i)
				{
					indices.push_back(indexOffset + face.mIndices[i]);
				}
			}

			indexOffset += mesh->mNumVertices;
		}

		data["m_vertices"] = vertices;
		data["m_indices"] = indices;

		logger.log(Logger::Category::AssetManagement,
			"Import 3D model: " + std::to_string(vertices.size() / 5) + " vertices, " + std::to_string(indices.size()) + " indices",
			Logger::LogLevel::INFO);

		success = true;
		break;
	}

	case AssetType::Shader:
	{
		// Copy shader source to Content folder
		const fs::path destSourcePath = contentDir / sourcePath.filename();
		std::error_code ec;
		fs::copy_file(sourcePath, destSourcePath, fs::copy_options::overwrite_existing, ec);

		const std::string relSourcePath = fs::relative(destSourcePath, fs::path(diagnostics.getProjectInfo().projectPath)).generic_string();
		data["m_sourcePath"] = relSourcePath;
		data["m_shaderType"] = sourcePath.extension().string();
		success = true;
		break;
	}

	case AssetType::Script:
	{
		// Copy script to Content folder
		const fs::path destSourcePath = contentDir / sourcePath.filename();
		std::error_code ec;
		fs::copy_file(sourcePath, destSourcePath, fs::copy_options::overwrite_existing, ec);

		const std::string relSourcePath = fs::relative(destSourcePath, fs::path(diagnostics.getProjectInfo().projectPath)).generic_string();
		data["m_sourcePath"] = relSourcePath;
		data["m_scriptPath"] = relSourcePath;
		success = true;
		break;
	}

	default:
		logger.log(Logger::Category::AssetManagement, "Import: unhandled asset type for: " + path, Logger::LogLevel::WARNING);
		break;
	}

	if (!success)
	{
		diagnostics.updateActionProgress(ActionID, false);
		return;
	}

	// Write the .asset file
	std::error_code ec;
	fs::create_directories(destAssetPath.parent_path(), ec);

	std::ofstream out(destAssetPath, std::ios::out | std::ios::trunc);
	if (!out.is_open())
	{
		logger.log(Logger::Category::AssetManagement, "Import failed: could not create .asset file: " + destAssetPath.string(), Logger::LogLevel::ERROR);
		diagnostics.updateActionProgress(ActionID, false);
		return;
	}

	json fileJson = json::object();
	fileJson["magic"] = 0x41535453;
	fileJson["version"] = 2;
	fileJson["type"] = static_cast<int>(detectedType);
	fileJson["name"] = assetName;
	fileJson["data"] = data;

	out << fileJson.dump(4);
	out.close();

	// Register in asset registry
	AssetRegistryEntry regEntry;
	regEntry.name = assetName;
	regEntry.path = relPath;
	regEntry.type = detectedType;
	registerAssetInRegistry(regEntry);

	diagnostics.updateActionProgress(ActionID, false);
	logger.log(Logger::Category::AssetManagement,
		"Import successful: " + assetName + " (" + relPath + ")",
		Logger::LogLevel::INFO);
	diagnostics.enqueueToastNotification("Imported: " + assetName, 3.0f);

	if (m_onImportCompleted)
	{
		m_onImportCompleted();
	}
}

bool AssetManager::moveAsset(const std::string& oldRelPath, const std::string& newRelPath)
{
	auto& logger = Logger::Instance();
	auto& diagnostics = DiagnosticsManager::Instance();

	if (!diagnostics.isProjectLoaded())
	{
		logger.log(Logger::Category::AssetManagement, "moveAsset: no project loaded.", Logger::LogLevel::ERROR);
		return false;
	}

	if (oldRelPath == newRelPath)
	{
		return true;
	}

	const fs::path contentDir = fs::path(diagnostics.getProjectInfo().projectPath) / "Content";
	const fs::path srcAbs = contentDir / oldRelPath;
	const fs::path destAbs = contentDir / newRelPath;

	// Move the file
	std::error_code ec;
	fs::create_directories(destAbs.parent_path(), ec);
	fs::rename(srcAbs, destAbs, ec);
	if (ec)
	{
		logger.log(Logger::Category::AssetManagement, "moveAsset: rename failed: " + ec.message(), Logger::LogLevel::ERROR);
		return false;
	}

	// Also move the source file if it was copied alongside the .asset (e.g. textures, scripts)
	// Read the .asset and check for m_sourcePath
	{
		std::ifstream in(destAbs);
		if (in.is_open())
		{
			try
			{
				json fileJson = json::parse(in);
				in.close();
				if (fileJson.contains("data") && fileJson["data"].is_object() && fileJson["data"].contains("m_sourcePath"))
				{
					const std::string oldSourceRel = fileJson["data"]["m_sourcePath"].get<std::string>();
					// The m_sourcePath is relative to the project root (e.g. "Content/Textures/wall.jpg")
					// Update it to match the new location's folder
					const fs::path oldSourceAbs = fs::path(diagnostics.getProjectInfo().projectPath) / oldSourceRel;
					if (fs::exists(oldSourceAbs))
					{
						const fs::path newSourceAbs = destAbs.parent_path() / fs::path(oldSourceRel).filename();
						if (oldSourceAbs != newSourceAbs)
						{
							fs::rename(oldSourceAbs, newSourceAbs, ec);
						}
						const std::string newSourceRel = fs::relative(newSourceAbs, fs::path(diagnostics.getProjectInfo().projectPath)).generic_string();
						fileJson["data"]["m_sourcePath"] = newSourceRel;

						// Write updated .asset
						std::ofstream out(destAbs, std::ios::out | std::ios::trunc);
						if (out.is_open())
						{
							out << fileJson.dump(4);
						}
					}
				}
			}
			catch (...)
			{
				// Not valid JSON, skip
			}
		}
	}

	// Update registry entry
	{
		std::lock_guard<std::mutex> lock(m_stateMutex);
		auto it = m_registryByPath.find(oldRelPath);
		if (it != m_registryByPath.end())
		{
			const size_t idx = it->second;
			m_registry[idx].path = newRelPath;
			m_registryByPath.erase(it);
			m_registryByPath[newRelPath] = idx;
		}
	}

	// Update loaded asset paths
	for (auto& [id, asset] : m_loadedAssets)
	{
		if (asset && asset->getPath() == oldRelPath)
		{
			asset->setPath(newRelPath);
		}
	}

	// Update ECS component references
	auto& ecs = ECS::ECSManager::Instance();
	{
		ECS::Schema meshSchema;
		meshSchema.require<ECS::MeshComponent>();
		for (const auto e : ecs.getEntitiesMatchingSchema(meshSchema))
		{
			if (auto* mesh = ecs.getComponent<ECS::MeshComponent>(e))
			{
				if (mesh->meshAssetPath == oldRelPath)
					mesh->meshAssetPath = newRelPath;
			}
		}
	}
	{
		ECS::Schema matSchema;
		matSchema.require<ECS::MaterialComponent>();
		for (const auto e : ecs.getEntitiesMatchingSchema(matSchema))
		{
			if (auto* mat = ecs.getComponent<ECS::MaterialComponent>(e))
			{
				if (mat->materialAssetPath == oldRelPath)
					mat->materialAssetPath = newRelPath;
			}
		}
	}
	{
		ECS::Schema scriptSchema;
		scriptSchema.require<ECS::ScriptComponent>();
		for (const auto e : ecs.getEntitiesMatchingSchema(scriptSchema))
		{
			if (auto* script = ecs.getComponent<ECS::ScriptComponent>(e))
			{
				if (script->scriptPath == oldRelPath)
					script->scriptPath = newRelPath;
			}
		}
	}

	logger.log(Logger::Category::AssetManagement,
		"moveAsset: " + oldRelPath + " → " + newRelPath + " (references updated)",
		Logger::LogLevel::INFO);

	// Scan all .asset files on disk for references to the old path and update them.
	// This catches cross-asset dependencies (e.g. Material → Texture, Level → Entity components).
	updateAssetFileReferences(contentDir, oldRelPath, newRelPath);

	return true;
}

// ---------------------------------------------------------------------------
// Recursively walk a JSON value and replace any string that equals oldVal with newVal.
// Returns true if at least one replacement was made.
// ---------------------------------------------------------------------------
static bool replaceJsonStringValues(json& node, const std::string& oldVal, const std::string& newVal)
{
	bool changed = false;
	if (node.is_string())
	{
		if (node.get<std::string>() == oldVal)
		{
			node = newVal;
			changed = true;
		}
	}
	else if (node.is_array())
	{
		for (auto& element : node)
		{
			changed |= replaceJsonStringValues(element, oldVal, newVal);
		}
	}
	else if (node.is_object())
	{
		for (auto& [key, value] : node.items())
		{
			changed |= replaceJsonStringValues(value, oldVal, newVal);
		}
	}
	return changed;
}

void AssetManager::updateAssetFileReferences(const std::filesystem::path& contentDir,
	const std::string& oldRelPath, const std::string& newRelPath)
{
	auto& logger = Logger::Instance();
	std::error_code ec;

	for (auto it = fs::recursive_directory_iterator(contentDir, fs::directory_options::skip_permission_denied, ec);
		it != fs::recursive_directory_iterator(); ++it)
	{
		if (it->is_directory()) continue;
		if (it->path().extension() != ".asset") continue;

		// Skip the moved file itself (already at newRelPath)
		const std::string fileRelPath = fs::relative(it->path(), contentDir).generic_string();
		if (fileRelPath == newRelPath) continue;

		std::ifstream in(it->path(), std::ios::in | std::ios::binary);
		if (!in.is_open()) continue;

		json fileJson;
		try
		{
			fileJson = json::parse(in);
		}
		catch (...)
		{
			continue;
		}
		in.close();

		if (!fileJson.is_object()) continue;

		bool changed = false;

		// Scan "data" block (Material textures, source paths, etc.)
		if (fileJson.contains("data"))
		{
			changed |= replaceJsonStringValues(fileJson["data"], oldRelPath, newRelPath);
		}

		// Scan "Entities" array (Level files with Entity component paths)
		if (fileJson.contains("Entities"))
		{
			changed |= replaceJsonStringValues(fileJson["Entities"], oldRelPath, newRelPath);
		}

		if (changed)
		{
			std::ofstream out(it->path(), std::ios::out | std::ios::trunc);
			if (out.is_open())
			{
				out << fileJson.dump(4);
				logger.log(Logger::Category::AssetManagement,
					"moveAsset: updated references in " + fileRelPath,
					Logger::LogLevel::INFO);
			}
		}
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

    {
        const fs::path scriptsRoot = fs::path(info.projectPath) / "Content" / "Scripts";
        std::error_code scriptsEc;
        fs::create_directories(scriptsRoot, scriptsEc);
        const fs::path stubPath = scriptsRoot / "engine.pyi";
        std::ofstream stubOut(stubPath, std::ios::out | std::ios::trunc);
        if (stubOut.is_open())
        {
			stubOut << "from typing import Callable, Dict, List, Optional, Tuple\n\n";
			stubOut << "Component_Transform: int\nComponent_Mesh: int\nComponent_Material: int\n";
			stubOut << "Component_Light: int\nComponent_Camera: int\nComponent_Physics: int\n";
			stubOut << "Component_Script: int\nComponent_Name: int\n\n";
			stubOut << "Asset_Texture: int\nAsset_Material: int\nAsset_Model2D: int\n";
			stubOut << "Asset_Model3D: int\nAsset_PointLight: int\nAsset_Audio: int\n";
			stubOut << "Asset_Script: int\nAsset_Shader: int\nAsset_Level: int\nAsset_Widget: int\n\n";
			stubOut << "Log_Info: int\nLog_Warning: int\nLog_Error: int\n\n";
			stubOut << "class _entity:\n";
			stubOut << "    @staticmethod\n    def create_entity() -> int: ...\n";
			stubOut << "    @staticmethod\n    def attach_component(entity: int, kind: int) -> bool: ...\n";
			stubOut << "    @staticmethod\n    def detach_component(entity: int, kind: int) -> bool: ...\n";
			stubOut << "    @staticmethod\n    def get_entities(kinds: List[int]) -> List[int]: ...\n";
			stubOut << "    @staticmethod\n    def get_transform(entity: int) -> Optional[Tuple[Tuple[float, float, float], Tuple[float, float, float], Tuple[float, float, float]]]: ...\n";
			stubOut << "    @staticmethod\n    def set_position(entity: int, x: float, y: float, z: float) -> bool: ...\n";
			stubOut << "    @staticmethod\n    def translate(entity: int, dx: float, dy: float, dz: float) -> bool: ...\n";
			stubOut << "    @staticmethod\n    def set_rotation(entity: int, pitch: float, yaw: float, roll: float) -> bool: ...\n";
			stubOut << "    @staticmethod\n    def rotate(entity: int, dp: float, dy: float, dr: float) -> bool: ...\n";
			stubOut << "    @staticmethod\n    def set_scale(entity: int, sx: float, sy: float, sz: float) -> bool: ...\n";
			stubOut << "    @staticmethod\n    def set_mesh(entity: int, path: str) -> bool: ...\n";
			stubOut << "    @staticmethod\n    def get_mesh(entity: int) -> Optional[str]: ...\n\n";
			stubOut << "class _assetmanagement:\n";
			stubOut << "    Asset_Texture: int\n";
			stubOut << "    Asset_Material: int\n";
			stubOut << "    Asset_Model2D: int\n";
			stubOut << "    Asset_Model3D: int\n";
			stubOut << "    Asset_PointLight: int\n";
			stubOut << "    Asset_Audio: int\n";
			stubOut << "    Asset_Script: int\n";
			stubOut << "    Asset_Shader: int\n";
			stubOut << "    Asset_Level: int\n";
			stubOut << "    Asset_Widget: int\n";
			stubOut << "    @staticmethod\n    def is_asset_loaded(path: str) -> bool: ...\n";
			stubOut << "    @staticmethod\n    def load_asset(path: str, type: int, allow_gc: bool = False) -> int: ...\n";
			stubOut << "    @staticmethod\n    def load_asset_async(path: str, type: int, on_loaded: Optional[Callable[[int], None]] = None, allow_gc: bool = False) -> int: ...\n";
			stubOut << "    @staticmethod\n    def save_asset(id: int, type: int, sync: bool = True) -> bool: ...\n";
			stubOut << "    @staticmethod\n    def unload_asset(id: int) -> bool: ...\n\n";
			stubOut << "class _diagnostics:\n";
			stubOut << "    @staticmethod\n    def get_delta_time() -> float: ...\n";
			stubOut << "    @staticmethod\n    def get_state(key: str) -> Optional[str]: ...\n";
			stubOut << "    @staticmethod\n    def set_state(key: str, value: str) -> bool: ...\n\n";
			stubOut << "class _ui:\n";
			stubOut << "    @staticmethod\n    def show_modal_message(message: str, on_closed: Optional[Callable[[], None]] = None) -> bool: ...\n";
			stubOut << "    @staticmethod\n    def close_modal_message() -> bool: ...\n";
			stubOut << "    @staticmethod\n    def show_toast_message(message: str, duration: float = 2.5) -> bool: ...\n\n";
			stubOut << "class _audio:\n";
			stubOut << "    @staticmethod\n    def create_audio(path: str, loop: bool = False, gain: float = 1.0, keep_loaded: bool = False) -> int: ...\n";
			stubOut << "    @staticmethod\n    def create_audio_from_asset(asset_id: int, loop: bool = False, gain: float = 1.0) -> int: ...\n";
			stubOut << "    @staticmethod\n    def play_audio(path: str, loop: bool = False, gain: float = 1.0, keep_loaded: bool = False) -> int: ...\n";
			stubOut << "    @staticmethod\n    def play_audio_handle(handle: int) -> bool: ...\n";
			stubOut << "    @staticmethod\n    def set_audio_volume(handle: int, volume: float) -> bool: ...\n";
			stubOut << "    @staticmethod\n    def get_audio_volume(handle: int) -> float: ...\n";
			stubOut << "    @staticmethod\n    def pause_audio(handle: int) -> bool: ...\n";
			stubOut << "    @staticmethod\n    def pause_audio_handle(handle: int) -> bool: ...\n";
			stubOut << "    @staticmethod\n    def is_audio_playing(handle: int) -> bool: ...\n";
			stubOut << "    @staticmethod\n    def is_audio_playing_path(path: str) -> bool: ...\n";
			stubOut << "    @staticmethod\n    def stop_audio(handle: int) -> bool: ...\n";
			stubOut << "    @staticmethod\n    def stop_audio_handle(handle: int) -> bool: ...\n";
			stubOut << "    @staticmethod\n    def invalidate_audio_handle(handle: int) -> bool: ...\n\n";
			stubOut << "class _input:\n";
			stubOut << "    @staticmethod\n    def set_on_key_pressed(callback: Optional[Callable[[int], None]]) -> bool: ...\n";
			stubOut << "    @staticmethod\n    def set_on_key_released(callback: Optional[Callable[[int], None]]) -> bool: ...\n";
			stubOut << "    @staticmethod\n    def register_key_pressed(key: int, callback: Callable[[int], None]) -> bool: ...\n";
			stubOut << "    @staticmethod\n    def register_key_released(key: int, callback: Callable[[int], None]) -> bool: ...\n";
			stubOut << "    @staticmethod\n    def is_shift_pressed() -> bool: ...\n";
			stubOut << "    @staticmethod\n    def is_ctrl_pressed() -> bool: ...\n";
			stubOut << "    @staticmethod\n    def is_alt_pressed() -> bool: ...\n";
			writeKeyConstants(stubOut, "    ");
			stubOut << "\nclass _camera:\n";
			stubOut << "    @staticmethod\n    def get_camera_position() -> Tuple[float, float, float]: ...\n";
			stubOut << "    @staticmethod\n    def set_camera_position(x: float, y: float, z: float) -> bool: ...\n";
			stubOut << "    @staticmethod\n    def get_camera_rotation() -> Tuple[float, float]: ...\n";
			stubOut << "    @staticmethod\n    def set_camera_rotation(yaw: float, pitch: float) -> bool: ...\n\n";
			stubOut << "class _logging:\n";
			stubOut << "    @staticmethod\n    def log(message: str, level: int = 0) -> bool: ...\n\n";
			stubOut << "entity: _entity\nassetmanagement: _assetmanagement\naudio: _audio\ninput: _input\nui: _ui\ncamera: _camera\ndiagnostics: _diagnostics\nlogging: _logging\n";
        }
    }

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

    {
        const fs::path stubPath = root / "Content" / "Scripts" / "engine.pyi";
        if (!fs::exists(stubPath))
        {
            std::ofstream stubOut(stubPath, std::ios::out | std::ios::trunc);
            if (stubOut.is_open())
            {
				stubOut << "from typing import Callable, Dict, List, Optional, Tuple\n\n";
				stubOut << "Component_Transform: int\nComponent_Mesh: int\nComponent_Material: int\n";
				stubOut << "Component_Light: int\nComponent_Camera: int\nComponent_Physics: int\n";
				stubOut << "Component_Script: int\nComponent_Name: int\n\n";
				stubOut << "Asset_Texture: int\nAsset_Material: int\nAsset_Model2D: int\n";
				stubOut << "Asset_Model3D: int\nAsset_PointLight: int\nAsset_Audio: int\n";
				stubOut << "Asset_Script: int\nAsset_Shader: int\nAsset_Level: int\nAsset_Widget: int\n\n";
				stubOut << "Log_Info: int\nLog_Warning: int\nLog_Error: int\n\n";
				stubOut << "class _entity:\n";
				stubOut << "    @staticmethod\n    def create_entity() -> int: ...\n";
				stubOut << "    @staticmethod\n    def attach_component(entity: int, kind: int) -> bool: ...\n";
				stubOut << "    @staticmethod\n    def detach_component(entity: int, kind: int) -> bool: ...\n";
				stubOut << "    @staticmethod\n    def get_entities(kinds: List[int]) -> List[int]: ...\n";
				stubOut << "    @staticmethod\n    def get_transform(entity: int) -> Optional[Tuple[Tuple[float, float, float], Tuple[float, float, float], Tuple[float, float, float]]]: ...\n";
				stubOut << "    @staticmethod\n    def set_position(entity: int, x: float, y: float, z: float) -> bool: ...\n";
				stubOut << "    @staticmethod\n    def translate(entity: int, dx: float, dy: float, dz: float) -> bool: ...\n";
				stubOut << "    @staticmethod\n    def set_rotation(entity: int, pitch: float, yaw: float, roll: float) -> bool: ...\n";
				stubOut << "    @staticmethod\n    def rotate(entity: int, dp: float, dy: float, dr: float) -> bool: ...\n";
				stubOut << "    @staticmethod\n    def set_scale(entity: int, sx: float, sy: float, sz: float) -> bool: ...\n";
				stubOut << "    @staticmethod\n    def set_mesh(entity: int, path: str) -> bool: ...\n";
				stubOut << "    @staticmethod\n    def get_mesh(entity: int) -> Optional[str]: ...\n\n";
				stubOut << "class _assetmanagement:\n";
				stubOut << "    Asset_Texture: int\n";
				stubOut << "    Asset_Material: int\n";
				stubOut << "    Asset_Model2D: int\n";
				stubOut << "    Asset_Model3D: int\n";
				stubOut << "    Asset_PointLight: int\n";
				stubOut << "    Asset_Audio: int\n";
				stubOut << "    Asset_Script: int\n";
				stubOut << "    Asset_Shader: int\n";
				stubOut << "    Asset_Level: int\n";
				stubOut << "    Asset_Widget: int\n";
				stubOut << "    @staticmethod\n    def is_asset_loaded(path: str) -> bool: ...\n";
				stubOut << "    @staticmethod\n    def load_asset(path: str, type: int, allow_gc: bool = False) -> int: ...\n";
				stubOut << "    @staticmethod\n    def load_asset_async(path: str, type: int, on_loaded: Optional[Callable[[int], None]] = None, allow_gc: bool = False) -> int: ...\n";
				stubOut << "    @staticmethod\n    def save_asset(id: int, type: int, sync: bool = True) -> bool: ...\n";
				stubOut << "    @staticmethod\n    def unload_asset(id: int) -> bool: ...\n\n";
				stubOut << "class _diagnostics:\n";
				stubOut << "    @staticmethod\n    def get_delta_time() -> float: ...\n";
				stubOut << "    @staticmethod\n    def get_state(key: str) -> Optional[str]: ...\n";
				stubOut << "    @staticmethod\n    def set_state(key: str, value: str) -> bool: ...\n\n";
				stubOut << "class _ui:\n";
				stubOut << "    @staticmethod\n    def show_modal_message(message: str, on_closed: Optional[Callable[[], None]] = None) -> bool: ...\n";
				stubOut << "    @staticmethod\n    def close_modal_message() -> bool: ...\n";
				stubOut << "    @staticmethod\n    def show_toast_message(message: str, duration: float = 2.5) -> bool: ...\n\n";
				stubOut << "class _audio:\n";
				stubOut << "    @staticmethod\n    def create_audio(path: str, loop: bool = False, gain: float = 1.0, keep_loaded: bool = False) -> int: ...\n";
				stubOut << "    @staticmethod\n    def create_audio_from_asset(asset_id: int, loop: bool = False, gain: float = 1.0) -> int: ...\n";
				stubOut << "    @staticmethod\n    def play_audio(path: str, loop: bool = False, gain: float = 1.0, keep_loaded: bool = False) -> int: ...\n";
				stubOut << "    @staticmethod\n    def play_audio_handle(handle: int) -> bool: ...\n";
				stubOut << "    @staticmethod\n    def set_audio_volume(handle: int, volume: float) -> bool: ...\n";
				stubOut << "    @staticmethod\n    def get_audio_volume(handle: int) -> float: ...\n";
				stubOut << "    @staticmethod\n    def pause_audio(handle: int) -> bool: ...\n";
				stubOut << "    @staticmethod\n    def pause_audio_handle(handle: int) -> bool: ...\n";
				stubOut << "    @staticmethod\n    def is_audio_playing(handle: int) -> bool: ...\n";
				stubOut << "    @staticmethod\n    def is_audio_playing_path(path: str) -> bool: ...\n";
				stubOut << "    @staticmethod\n    def stop_audio(handle: int) -> bool: ...\n";
				stubOut << "    @staticmethod\n    def stop_audio_handle(handle: int) -> bool: ...\n";
				stubOut << "    @staticmethod\n    def invalidate_audio_handle(handle: int) -> bool: ...\n\n";
				stubOut << "class _input:\n";
				stubOut << "    @staticmethod\n    def set_on_key_pressed(callback: Optional[Callable[[int], None]]) -> bool: ...\n";
				stubOut << "    @staticmethod\n    def set_on_key_released(callback: Optional[Callable[[int], None]]) -> bool: ...\n";
				stubOut << "    @staticmethod\n    def register_key_pressed(key: int, callback: Callable[[int], None]) -> bool: ...\n";
				stubOut << "    @staticmethod\n    def register_key_released(key: int, callback: Callable[[int], None]) -> bool: ...\n";
				stubOut << "    @staticmethod\n    def is_shift_pressed() -> bool: ...\n";
				stubOut << "    @staticmethod\n    def is_ctrl_pressed() -> bool: ...\n";
				stubOut << "    @staticmethod\n    def is_alt_pressed() -> bool: ...\n";
				writeKeyConstants(stubOut, "    ");
				stubOut << "\nclass _camera:\n";
				stubOut << "    @staticmethod\n    def get_camera_position() -> Tuple[float, float, float]: ...\n";
				stubOut << "    @staticmethod\n    def set_camera_position(x: float, y: float, z: float) -> bool: ...\n";
				stubOut << "    @staticmethod\n    def get_camera_rotation() -> Tuple[float, float]: ...\n";
				stubOut << "    @staticmethod\n    def set_camera_rotation(yaw: float, pitch: float) -> bool: ...\n\n";
				stubOut << "class _logging:\n";
				stubOut << "    @staticmethod\n    def log(message: str, level: int = 0) -> bool: ...\n\n";
				stubOut << "entity: _entity\nassetmanagement: _assetmanagement\naudio: _audio\ninput: _input\nui: _ui\ncamera: _camera\ndiagnostics: _diagnostics\nlogging: _logging\n";
            }
        }
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
		result.errorMessage = raw.errorMessage;
		return result;
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

bool AssetManager::saveNewLevelAsset(EngineLevel* level)
{
    return saveLevelAsset(level).success;
}

// ---------------------------------------------------------------------------
// Parallel loading: disk-only read (no shared state), finalize, batch API
// ---------------------------------------------------------------------------

RawAssetData AssetManager::readAssetFromDisk(const std::string& path, AssetType expectedType)
{
    RawAssetData raw;
    raw.path = path;

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
            if (!SDL_LoadWAV(path.c_str(), &spec, &buffer, &length))
            {
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

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
    {
        raw.errorMessage = "Failed to open asset file: " + path;
        return raw;
    }

    AssetType headerType{ AssetType::Unknown };
    std::string name;
    bool isJson = false;
    if (!readAssetHeader(in, headerType, name, isJson))
    {
        raw.errorMessage = "Invalid asset header: " + path;
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

    json j;
    if (!readAssetJson(in, j, raw.errorMessage, isJson))
    {
        return raw;
    }
    in.close();

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
            if (fs::exists(sourcePath))
            {
                int width = 0, height = 0, channels = 0;
                unsigned char* data = stbi_load(sourcePath.string().c_str(), &width, &height, &channels, 4);
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
