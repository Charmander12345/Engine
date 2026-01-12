#include "AssetManager.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <filesystem>
#include <fstream>
#include <cstdint>
#include "AssetTypes.h"

#include "MaterialAsset.h"

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
	m_garbageCollector = GarbageCollector();

    // Create a default 2D triangle asset that is tracked and can be saved via saveAllAssets.
    // It starts as unsaved and will be written on demand.
    {
        auto& logger = Logger::Instance();
        logger.log(Logger::Category::AssetManagement, "Registering built-in default assets...", Logger::LogLevel::INFO);

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
        tri->setIndices({ 0, 1, 2 });

        m_defaultTriangle2D = tri;
        m_garbageCollector.registerResource(m_defaultTriangle2D);
        logger.log(Logger::Category::AssetManagement, "Registered default 2D triangle asset.", Logger::LogLevel::INFO);
    }

    return true;
}

bool AssetManager::saveAllAssets()
{
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
    if (!loadAsset(path))
    {
        return nullptr;
    }

    // Find the loaded asset among currently alive tracked assets.
    for (const auto& alive : m_garbageCollector.getAliveResources())
    {
        if (alive && alive->getPath() == path)
        {
            return alive;
        }
    }

    return nullptr;
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

bool AssetManager::loadAsset(const std::string& path)
{
    auto& logger = Logger::Instance();
    logger.log(Logger::Category::AssetManagement, "Loading asset: " + path, Logger::LogLevel::INFO);

    auto& diagnostics = DiagnosticsManager::Instance();
    bool loaded = diagnostics.isProjectLoaded();
    if (!loaded || diagnostics.getProjectInfo().projectPath.empty())
    {
        logger.log(Logger::Category::AssetManagement, "Asset load failed (no project loaded)", Logger::LogLevel::ERROR);
        return false;
    }

    fs::path contentRoot = fs::path(diagnostics.getProjectInfo().projectPath) / "Content";

    // 'path' is expected to be the relative path inside Content.
    fs::path relPath = fs::path(path);
    if (relPath.is_absolute())
    {
        logger.log(Logger::Category::AssetManagement, "Asset load failed (path must be relative to Content): " + relPath.string(), Logger::LogLevel::ERROR);
        return false;
    }

    fs::path absolutePath = contentRoot / relPath;
    if (!fs::exists(absolutePath))
    {
        logger.log(Logger::Category::AssetManagement, "Asset load failed (missing file): " + absolutePath.string(), Logger::LogLevel::ERROR);
        return false;
    }

    std::ifstream in(absolutePath, std::ios::binary);
    if (!in.is_open())
    {
        logger.log(Logger::Category::AssetManagement, "Failed to open asset file: " + absolutePath.string(), Logger::LogLevel::ERROR);
        return false;
    }

    uint32_t magic = 0;
    uint32_t version = 0;
    if (!in.read(reinterpret_cast<char*>(&magic), sizeof(magic))) return false;
    if (!in.read(reinterpret_cast<char*>(&version), sizeof(version))) return false;
    if (magic != 0x41535453 || version != 2) // 'ASTS'
    {
        logger.log(Logger::Category::AssetManagement, "Invalid asset file format", Logger::LogLevel::ERROR);
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
    // store only relative path inside Content
    obj->setPath(storedRelPath.empty() ? relPath.generic_string() : storedRelPath);
    obj->setAssetType(type);
    obj->setIsSaved(true);

    if (type == AssetType::Texture)
    {
        auto tex = std::static_pointer_cast<Texture>(obj);
        int32_t w = 0, h = 0, c = 0;
        uint32_t dataSize = 0;
        if (!in.read(reinterpret_cast<char*>(&w), sizeof(w))) return false;
        if (!in.read(reinterpret_cast<char*>(&h), sizeof(h))) return false;
        if (!in.read(reinterpret_cast<char*>(&c), sizeof(c))) return false;
        if (!in.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize))) return false;

        std::vector<unsigned char> data;
        data.resize(dataSize);
        if (dataSize > 0)
        {
            if (!in.read(reinterpret_cast<char*>(data.data()), dataSize)) return false;
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
        if (!in.read(reinterpret_cast<char*>(&vertCount), sizeof(vertCount))) return false;
        std::vector<float> verts(vertCount);
        if (vertCount > 0)
        {
            if (!in.read(reinterpret_cast<char*>(verts.data()), vertCount * sizeof(float))) return false;
        }
        obj2d->setVertices(verts);

        uint32_t indexCount = 0;
        if (!in.read(reinterpret_cast<char*>(&indexCount), sizeof(indexCount))) return false;
        std::vector<uint32_t> indices(indexCount);
        if (indexCount > 0)
        {
            if (!in.read(reinterpret_cast<char*>(indices.data()), indexCount * sizeof(uint32_t))) return false;
        }
        obj2d->setIndices(indices);

        // Auto-load material + textures via AssetManager
        if (!materialAssetPath.empty())
        {
            if (auto matAsset = loadMaterialAsset(materialAssetPath))
            {
                auto textures = loadTexturesForMaterial(*matAsset);
                obj2d->setLoadedMaterialAsset(matAsset);
                obj2d->setLoadedTextures(textures);
            }
        }
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

        uint32_t indexCount = 0;
        if (!in.read(reinterpret_cast<char*>(&indexCount), sizeof(indexCount))) return false;
        std::vector<uint32_t> indices(indexCount);
        if (indexCount > 0)
        {
            if (!in.read(reinterpret_cast<char*>(indices.data()), indexCount * sizeof(uint32_t))) return false;
        }
        obj3d->setIndices(indices);

        // Auto-load material + textures via AssetManager
        if (!materialAssetPath.empty())
        {
            if (auto matAsset = loadMaterialAsset(materialAssetPath))
            {
                auto textures = loadTexturesForMaterial(*matAsset);
                obj3d->setLoadedMaterialAsset(matAsset);
                obj3d->setLoadedTextures(textures);
            }
        }
    }

    if (type == AssetType::Level)
    {
        auto level = std::static_pointer_cast<EngineLevel>(obj);
        level->clearLoadedDependencies();

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

            Vec3 pos{}, rot{}, scl{};
            if (!in.read(reinterpret_cast<char*>(&pos), sizeof(pos))) return false;
            if (!in.read(reinterpret_cast<char*>(&rot), sizeof(rot))) return false;
            if (!in.read(reinterpret_cast<char*>(&scl), sizeof(scl))) return false;

            Transform t;
            t.setPosition(pos);
            t.setRotation(rot);
            t.setScale(scl);

            // Load dependency asset into memory (if it exists).
            // We keep a strong reference in the level so the GC won't unload it while the level is active.
            if (!p.empty())
            {
                const std::string depRelPath = p.generic_string();
                loadAsset(depRelPath);

                // Find the loaded asset among currently alive tracked assets.
                std::shared_ptr<EngineObject> depAsset;
                for (const auto& alive : m_garbageCollector.getAliveResources())
                {
                    if (alive && alive->getPath() == depRelPath)
                    {
                        depAsset = alive;
                        break;
                    }
                }

                if (depAsset)
                {
                    level->setLoadedDependency(depRelPath, depAsset);
                }
            }

            EngineObject dep;
            dep.setPath(p.generic_string());
            dep.setTransform(t);

            level->registerObject(dep);
            level->setObjectTransform(dep, t);
        }
    }

    if (type == AssetType::Material)
    {
        auto mat = std::static_pointer_cast<MaterialAsset>(obj);

        uint32_t texRefCount = 0;
        if (!in.read(reinterpret_cast<char*>(&texRefCount), sizeof(texRefCount))) return false;

        std::vector<std::string> refs;
        refs.reserve(texRefCount);

        for (uint32_t i = 0; i < texRefCount; ++i)
        {
            std::string ref;
            if (!readString(in, ref)) return false;
            refs.push_back(std::move(ref));
        }

        mat->setTextureAssetPaths(std::move(refs));
    }

    m_garbageCollector.registerResource(obj);

    logger.log(Logger::Category::AssetManagement, "Asset loaded: " + name + " (" + obj->getPath() + ")", Logger::LogLevel::INFO);
    return true;
}

bool AssetManager::saveAsset(const std::shared_ptr<EngineObject>& object)
{
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
            auto& deps = level->getWorldObjects();
            depCount = static_cast<uint32_t>(deps.size());
        }

        out.write(reinterpret_cast<const char*>(&depCount), sizeof(depCount));

        if (level)
        {
            auto& deps = level->getWorldObjects();
            const auto& transforms = level->getWorldObjectTransforms();

            for (uint32_t i = 0; i < depCount; ++i)
            {
                const auto& dep = deps[i];
                const std::string& path = dep.getPath();
                writeString(out, path);

                Transform t = dep.getTransform();
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

std::shared_ptr<EngineObject> AssetManager::createAsset(AssetType type, const std::string& path, const std::string& name, const std::string& sourcePath)
{
    auto& logger = Logger::Instance();
    logger.log(Logger::Category::AssetManagement, "Creating asset with name: " + name + " at " + path, Logger::LogLevel::INFO);

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

    const std::string normalizedRel = normalizeAssetRelPath(type, rel.generic_string());

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

    diagnostics.setProjectInfo(info);

    // Registry handling: ONLY here.
    // Always rebuild via discovery to ensure it's up-to-date.
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

    out << "Name=" << info.projectName << "\n";
    out << "Version=" << info.projectVersion << "\n";
    out << "EngineVersion=" << info.engineVersion << "\n";
    out << "Path=" << info.projectPath << "\n";
    out << "RHI=" << DiagnosticsManager::rhiTypeToString(info.selectedRHI) << "\n";
	out << "Level=" << (diagnostics.getActiveLevel() ? diagnostics.getActiveLevel()->getPath() : "") << "\n";

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
	bool loaded = loadProject(root.string());
    if (!loaded)
    {
        logger.log("Failed to load newly created project: " + root.string(), Logger::LogLevel::ERROR);
        return false;
	}
    return saved;
}
