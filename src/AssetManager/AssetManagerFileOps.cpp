// ---------------------------------------------------------------------------
// AssetManagerFileOps.cpp - Split implementation file for AssetManager
// Contains: Delete, move, rename, validate, repair, reference tracking
// ---------------------------------------------------------------------------
#include "AssetManager.h"

#if ENGINE_EDITOR

#include <filesystem>
#include <fstream>
#include <algorithm>

#include "AssetTypes.h"

#include "../Core/ECS/ECS.h"
#include "../Core/Actor/ActorAssetData.h"

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Recursively walk a JSON value and check if any string equals searchVal.
// Returns true if at least one match is found.
// ---------------------------------------------------------------------------
static bool jsonContainsStringValue(const json& node, const std::string& searchVal)
{
	if (node.is_string())
	{
		return node.get<std::string>() == searchVal;
	}
	else if (node.is_array())
	{
		for (const auto& element : node)
		{
			if (jsonContainsStringValue(element, searchVal))
				return true;
		}
	}
	else if (node.is_object())
	{
		for (const auto& [key, value] : node.items())
		{
			if (jsonContainsStringValue(value, searchVal))
				return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
// Recursively collect all string values from a JSON node that look like
// content-relative asset paths (contain ".asset" or ".py").
// ---------------------------------------------------------------------------
static void collectAssetPathStrings(const json& node, std::vector<std::string>& out)
{
	if (node.is_string())
	{
		const std::string& val = node.get_ref<const std::string&>();
		if (val.find(".asset") != std::string::npos || val.find(".py") != std::string::npos)
		{
			out.push_back(val);
		}
	}
	else if (node.is_array())
	{
		for (const auto& element : node)
			collectAssetPathStrings(element, out);
	}
	else if (node.is_object())
	{
		for (const auto& [key, value] : node.items())
			collectAssetPathStrings(value, out);
	}
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

        m_registryVersion.fetch_add(1, std::memory_order_relaxed);
    }

	// Delete file from disk
	if (deleteFromDisk && diagnostics.isProjectLoaded())
	{
		const fs::path contentDir = fs::path(diagnostics.getProjectInfo().projectPath) / "Content";
		const fs::path absPath = contentDir / fs::path(relPath);

		// If this is an ActorAsset, clean up auto-generated script files
		{
			std::error_code readEc;
			if (fs::exists(absPath, readEc))
			{
				std::ifstream in(absPath);
				if (in.is_open())
				{
					json fileJson = json::parse(in, nullptr, false);
					in.close();
					if (!fileJson.is_discarded() && fileJson.contains("type") &&
						fileJson["type"].get<int>() == static_cast<int>(AssetType::ActorAsset) &&
						fileJson.contains("data"))
					{
						ActorAssetData actorData = ActorAssetData::fromJson(fileJson["data"]);
						if (actorData.cleanupScriptFiles(contentDir.string()))
						{
							logger.log(Logger::Category::AssetManagement,
								"Cleaned up script files for actor: " + relPath, Logger::LogLevel::INFO);
						}
					}
				}
			}
		}

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

size_t AssetManager::validateRegistry()
{
    auto& logger = Logger::Instance();
    auto& diagnostics = DiagnosticsManager::Instance();

    if (!diagnostics.isProjectLoaded() || diagnostics.getProjectInfo().projectPath.empty())
    {
        return 0;
    }

    const fs::path contentDir = fs::path(diagnostics.getProjectInfo().projectPath) / "Content";
    if (!fs::exists(contentDir))
    {
        return 0;
    }

    std::vector<std::string> staleEntries;
    for (const auto& entry : m_registry)
    {
        const fs::path absPath = contentDir / fs::path(entry.path);
        std::error_code ec;
        if (!fs::exists(absPath, ec))
        {
            staleEntries.push_back(entry.path);
        }
    }

    if (staleEntries.empty())
    {
        return 0;
    }

    for (const auto& relPath : staleEntries)
    {
        logger.log(Logger::Category::AssetManagement,
            "[Integrity] Stale registry entry (file missing): " + relPath, Logger::LogLevel::WARNING);

        for (auto it = m_registry.begin(); it != m_registry.end(); ++it)
        {
            if (it->path == relPath)
            {
                m_registry.erase(it);
                break;
            }
        }
    }

    // Rebuild index maps
    m_registryByPath.clear();
    m_registryByName.clear();
    for (size_t i = 0; i < m_registry.size(); ++i)
    {
        if (!m_registry[i].path.empty())
            m_registryByPath[m_registry[i].path] = i;
        if (!m_registry[i].name.empty())
            m_registryByName[m_registry[i].name] = i;
    }

    saveAssetRegistry(diagnostics.getProjectInfo().projectPath);
    m_registryVersion.fetch_add(1, std::memory_order_relaxed);

    logger.log(Logger::Category::AssetManagement,
        "[Integrity] Removed " + std::to_string(staleEntries.size()) + " stale registry entries.",
        Logger::LogLevel::WARNING);

    return staleEntries.size();
}

size_t AssetManager::validateEntityReferences(bool showToast)
{
    auto& logger = Logger::Instance();
    auto& ecs = ECS::ECSManager::Instance();
    size_t broken = 0;

    // Helper: check registry + project Content + engine Content on disk
    auto assetExistsOnDisk = [this](const std::string& relPath) -> bool
    {
        if (relPath.empty())
            return false;
        if (doesAssetExist(relPath))
            return true;
        const std::string absProject = getAbsoluteContentPath(relPath);
        if (!absProject.empty() && fs::exists(absProject))
            return true;
        const std::string absEngine = getAbsoluteEngineContentPath(relPath);
        if (!absEngine.empty() && fs::exists(absEngine))
            return true;
        return false;
    };

    // Validate MeshComponent references
    {
        ECS::Schema schema;
        schema.require<ECS::MeshComponent>();
        for (const auto e : ecs.getEntitiesMatchingSchema(schema))
        {
            const auto* mesh = ecs.getComponent<ECS::MeshComponent>(e);
            if (mesh && !mesh->meshAssetPath.empty() && !assetExistsOnDisk(mesh->meshAssetPath))
            {
                logger.log(Logger::Category::AssetManagement,
                    "[Integrity] Entity " + std::to_string(e) + " references missing mesh: " + mesh->meshAssetPath,
                    Logger::LogLevel::WARNING);
                ++broken;
            }
        }
    }

    // Validate MaterialComponent references
    {
        ECS::Schema schema;
        schema.require<ECS::MaterialComponent>();
        for (const auto e : ecs.getEntitiesMatchingSchema(schema))
        {
            const auto* mat = ecs.getComponent<ECS::MaterialComponent>(e);
            if (mat && !mat->materialAssetPath.empty() && !assetExistsOnDisk(mat->materialAssetPath))
            {
                logger.log(Logger::Category::AssetManagement,
                    "[Integrity] Entity " + std::to_string(e) + " references missing material: " + mat->materialAssetPath,
                    Logger::LogLevel::WARNING);
                ++broken;
            }
        }
    }

	// Validate LogicComponent references
	{
		ECS::Schema schema;
		schema.require<ECS::LogicComponent>();
		for (const auto e : ecs.getEntitiesMatchingSchema(schema))
		{
			const auto* sc = ecs.getComponent<ECS::LogicComponent>(e);
			if (sc && !sc->scriptPath.empty() && !assetExistsOnDisk(sc->scriptPath))
			{
				logger.log(Logger::Category::AssetManagement,
					"[Integrity] Entity " + std::to_string(e) + " references missing script: " + sc->scriptPath,
					Logger::LogLevel::WARNING);
				++broken;
			}
		}
	}

    if (broken > 0)
    {
        logger.log(Logger::Category::AssetManagement,
            "[Integrity] Found " + std::to_string(broken) + " broken entity asset reference(s).",
            Logger::LogLevel::WARNING);
    }

    return broken;
}

size_t AssetManager::repairEntityReferences()
{
    auto& logger = Logger::Instance();
    auto& ecs = ECS::ECSManager::Instance();
    size_t repaired = 0;

    const std::string worldGridMaterialPath = "Materials/WorldGrid.asset";

    // Helper: check whether a content-relative asset path actually resolves to a
    // file on disk. We check both the project Content directory and the engine
    // Content directory (for built-in assets like WorldGrid). This is more
    // reliable than a registry-only check because the registry might not cover
    // engine assets and path separators may differ.
    auto assetExistsOnDisk = [this](const std::string& relPath) -> bool
    {
        if (relPath.empty())
            return false;

        // 1. Registry (fast, covers project assets)
        if (doesAssetExist(relPath))
            return true;

        // 2. Project Content directory
        const std::string absProject = getAbsoluteContentPath(relPath);
        if (!absProject.empty() && fs::exists(absProject))
            return true;

        // 3. Engine Content directory (built-in assets next to the executable)
        const std::string absEngine = getAbsoluteEngineContentPath(relPath);
        if (!absEngine.empty() && fs::exists(absEngine))
            return true;

        return false;
    };

    // Repair missing MeshComponent references â€” remove the component entirely
    {
        ECS::Schema schema;
        schema.require<ECS::MeshComponent>();
        const auto entities = ecs.getEntitiesMatchingSchema(schema);
        for (const auto e : entities)
        {
            const auto* mesh = ecs.getComponent<ECS::MeshComponent>(e);
            if (mesh && !mesh->meshAssetPath.empty() && !assetExistsOnDisk(mesh->meshAssetPath))
            {
                logger.log(Logger::Category::AssetManagement,
                    "[Repair] Entity " + std::to_string(e) + ": removing missing mesh '" + mesh->meshAssetPath + "'",
                    Logger::LogLevel::WARNING);
                ecs.removeComponent<ECS::MeshComponent>(e);
                ++repaired;
            }
        }
    }

    // Repair missing MaterialComponent references â€” replace with WorldGrid material
    {
        ECS::Schema schema;
        schema.require<ECS::MaterialComponent>();
        const auto entities = ecs.getEntitiesMatchingSchema(schema);
        for (const auto e : entities)
        {
            const auto* mat = ecs.getComponent<ECS::MaterialComponent>(e);
            if (mat && !mat->materialAssetPath.empty() && !assetExistsOnDisk(mat->materialAssetPath))
            {
                logger.log(Logger::Category::AssetManagement,
                    "[Repair] Entity " + std::to_string(e) + ": replacing missing material '" + mat->materialAssetPath
                    + "' with WorldGrid",
                    Logger::LogLevel::WARNING);
                ECS::MaterialComponent fixed{};
                fixed.materialAssetPath = worldGridMaterialPath;
                ecs.setComponent<ECS::MaterialComponent>(e, fixed);
                ++repaired;
            }
        }
    }

    if (repaired > 0)
    {
        logger.log(Logger::Category::AssetManagement,
            "[Repair] Fixed " + std::to_string(repaired) + " broken entity reference(s).",
            Logger::LogLevel::WARNING);
    }

    return repaired;
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
		scriptSchema.require<ECS::LogicComponent>();
		for (const auto e : ecs.getEntitiesMatchingSchema(scriptSchema))
		{
			if (auto* logic = ecs.getComponent<ECS::LogicComponent>(e))
			{
				if (logic->scriptPath == oldRelPath)
					logic->scriptPath = newRelPath;
			}
		}
	}

	m_registryVersion.fetch_add(1, std::memory_order_relaxed);

	logger.log(Logger::Category::AssetManagement,
		"moveAsset: " + oldRelPath + " â†’ " + newRelPath + " (references updated)",
		Logger::LogLevel::INFO);

	// Scan all .asset files on disk for references to the old path and update them.
	// This catches cross-asset dependencies (e.g. Material â†’ Texture, Level â†’ Entity components).
	updateAssetFileReferences(contentDir, oldRelPath, newRelPath);

	return true;
}

bool AssetManager::renameAsset(const std::string& relPath, const std::string& newName)
{
	auto& logger = Logger::Instance();
	auto& diagnostics = DiagnosticsManager::Instance();

	if (relPath.empty() || newName.empty())
	{
		logger.log(Logger::Category::AssetManagement, "renameAsset: empty relPath or newName.", Logger::LogLevel::ERROR);
		return false;
	}

	if (!diagnostics.isProjectLoaded())
	{
		logger.log(Logger::Category::AssetManagement, "renameAsset: no project loaded.", Logger::LogLevel::ERROR);
		return false;
	}

	// Build new relative path: same parent folder, new filename with same extension
	const fs::path oldRel(relPath);
	const std::string ext = oldRel.extension().string();
	const fs::path parentDir = oldRel.parent_path();
	const std::string newRelPath = (parentDir / (newName + ext)).generic_string();

	if (relPath == newRelPath)
	{
		return true; // no change needed
	}

	// Check the new path doesn't already exist in the registry
	{
		std::lock_guard<std::mutex> lock(m_stateMutex);
		if (m_registryByPath.count(newRelPath))
		{
			logger.log(Logger::Category::AssetManagement, "renameAsset: target path already exists in registry: " + newRelPath, Logger::LogLevel::ERROR);
			return false;
		}
	}

	const fs::path contentDir = fs::path(diagnostics.getProjectInfo().projectPath) / "Content";
	const fs::path srcAbs = contentDir / relPath;
	const fs::path destAbs = contentDir / newRelPath;

	// Check source file exists
	if (!fs::exists(srcAbs))
	{
		logger.log(Logger::Category::AssetManagement, "renameAsset: source file does not exist: " + srcAbs.string(), Logger::LogLevel::ERROR);
		return false;
	}

	// Check destination doesn't already exist on disk
	if (fs::exists(destAbs))
	{
		logger.log(Logger::Category::AssetManagement, "renameAsset: destination file already exists: " + destAbs.string(), Logger::LogLevel::ERROR);
		return false;
	}

	// Rename the .asset file on disk
	std::error_code ec;
	fs::rename(srcAbs, destAbs, ec);
	if (ec)
	{
		logger.log(Logger::Category::AssetManagement, "renameAsset: rename failed: " + ec.message(), Logger::LogLevel::ERROR);
		return false;
	}

	// Also rename the source file if it sits alongside the .asset (e.g. textures, scripts)
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
					const fs::path oldSourceAbs = fs::path(diagnostics.getProjectInfo().projectPath) / oldSourceRel;
					if (fs::exists(oldSourceAbs))
					{
						const fs::path sourceExt = fs::path(oldSourceRel).extension();
						const fs::path newSourceAbs = oldSourceAbs.parent_path() / (newName + sourceExt.string());
						if (oldSourceAbs != newSourceAbs)
						{
							fs::rename(oldSourceAbs, newSourceAbs, ec);
						}
						const std::string newSourceRel = fs::relative(newSourceAbs, fs::path(diagnostics.getProjectInfo().projectPath)).generic_string();
						fileJson["data"]["m_sourcePath"] = newSourceRel;

						std::ofstream out(destAbs, std::ios::out | std::ios::trunc);
						if (out.is_open())
						{
							out << fileJson.dump(4);
						}
					}
				}

						// Update the name field inside the asset file
							if (fileJson.contains("name"))
							{
								fileJson["name"] = newName;
							}

							// ActorAsset: rename embedded script files and update paths
							if (fileJson.contains("type") &&
								fileJson["type"].get<int>() == static_cast<int>(AssetType::ActorAsset) &&
								fileJson.contains("data") && fileJson["data"].is_object())
							{
								auto& data = fileJson["data"];
								data["name"] = newName;

								const std::string newClassName = newName + "_Script";

								// Rename header file
								if (data.contains("scriptHeaderPath"))
								{
									const std::string oldHeader = data["scriptHeaderPath"].get<std::string>();
									const fs::path oldHeaderAbs = contentDir / oldHeader;
									if (fs::exists(oldHeaderAbs))
									{
										const fs::path newHeaderAbs = oldHeaderAbs.parent_path() / (newClassName + ".h");
										std::error_code renameEc;
										fs::rename(oldHeaderAbs, newHeaderAbs, renameEc);
										data["scriptHeaderPath"] = fs::relative(newHeaderAbs, contentDir).generic_string();
									}
								}
								// Rename cpp file
								if (data.contains("scriptCppPath"))
								{
									const std::string oldCpp = data["scriptCppPath"].get<std::string>();
									const fs::path oldCppAbs = contentDir / oldCpp;
									if (fs::exists(oldCppAbs))
									{
										const fs::path newCppAbs = oldCppAbs.parent_path() / (newClassName + ".cpp");
										std::error_code renameEc;
										fs::rename(oldCppAbs, newCppAbs, renameEc);
										data["scriptCppPath"] = fs::relative(newCppAbs, contentDir).generic_string();
									}
								}
								if (data.contains("scriptClassName"))
								{
									data["scriptClassName"] = newClassName;
								}
							}

							// Write updated JSON back to disk
							{
								std::ofstream out(destAbs, std::ios::out | std::ios::trunc);
								if (out.is_open())
								{
									out << fileJson.dump(4);
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
		auto it = m_registryByPath.find(relPath);
		if (it != m_registryByPath.end())
		{
			const size_t idx = it->second;
			const std::string oldName = m_registry[idx].name;
			m_registry[idx].path = newRelPath;
			m_registry[idx].name = newName;
			m_registryByPath.erase(it);
			m_registryByPath[newRelPath] = idx;
			m_registryByName.erase(oldName);
			m_registryByName[newName] = idx;
		}
	}

	// Update loaded asset paths
	for (auto& [id, asset] : m_loadedAssets)
	{
		if (asset && asset->getPath() == relPath)
		{
			asset->setPath(newRelPath);
			asset->setName(newName);
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
				if (mesh->meshAssetPath == relPath)
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
				if (mat->materialAssetPath == relPath)
					mat->materialAssetPath = newRelPath;
			}
		}
	}
	{
		ECS::Schema scriptSchema;
		scriptSchema.require<ECS::LogicComponent>();
		for (const auto e : ecs.getEntitiesMatchingSchema(scriptSchema))
		{
			if (auto* logic = ecs.getComponent<ECS::LogicComponent>(e))
			{
				if (logic->scriptPath == relPath)
					logic->scriptPath = newRelPath;
			}
		}
	}

	// Persist registry
	if (diagnostics.isProjectLoaded() && !diagnostics.getProjectInfo().projectPath.empty())
	{
		saveAssetRegistry(diagnostics.getProjectInfo().projectPath);
	}

	m_registryVersion.fetch_add(1, std::memory_order_relaxed);

	logger.log(Logger::Category::AssetManagement,
		"renameAsset: " + relPath + " â†’ " + newRelPath + " (references updated)",
		Logger::LogLevel::INFO);

	// Scan all .asset files for cross-references
	updateAssetFileReferences(contentDir, relPath, newRelPath);

	return true;
}

std::vector<AssetManager::AssetReference> AssetManager::findReferencesTo(const std::string& relPath) const
{
	std::vector<AssetReference> results;
	if (relPath.empty()) return results;

	auto& diagnostics = DiagnosticsManager::Instance();
	if (!diagnostics.isProjectLoaded()) return results;

	const fs::path contentDir = fs::path(diagnostics.getProjectInfo().projectPath) / "Content";

	// 1) Scan all .asset files on disk
	std::error_code ec;
	for (auto it = fs::recursive_directory_iterator(contentDir, fs::directory_options::skip_permission_denied, ec);
		it != fs::recursive_directory_iterator(); ++it)
	{
		if (it->is_directory()) continue;
		if (it->path().extension() != ".asset") continue;

		const std::string fileRelPath = fs::relative(it->path(), contentDir).generic_string();
		if (fileRelPath == relPath) continue; // skip self

		std::ifstream in(it->path(), std::ios::in | std::ios::binary);
		if (!in.is_open()) continue;

		json fileJson;
		try { fileJson = json::parse(in); }
		catch (...) { continue; }
		in.close();

		if (!fileJson.is_object()) continue;

		bool found = false;
		if (fileJson.contains("data"))
			found |= jsonContainsStringValue(fileJson["data"], relPath);
		if (fileJson.contains("Entities"))
			found |= jsonContainsStringValue(fileJson["Entities"], relPath);

		if (found)
		{
			std::string sourceType = "Asset";
			if (fileJson.contains("type"))
			{
				const std::string t = fileJson["type"].get<std::string>();
				if (t == "Level" || t == "Map") sourceType = "Level";
				else if (t == "Material") sourceType = "Material";
				else if (t == "Widget") sourceType = "Widget";
				else sourceType = t;
			}
			results.push_back({ fileRelPath, sourceType });
		}
	}

	// 2) Scan ECS entities in the active level
	auto& ecs = ECS::ECSManager::Instance();
	{
		ECS::Schema schema;
		schema.require<ECS::MeshComponent>();
		for (const auto e : ecs.getEntitiesMatchingSchema(schema))
		{
			const auto* mesh = ecs.getComponent<ECS::MeshComponent>(e);
			if (mesh && mesh->meshAssetPath == relPath)
			{
				std::string label = "Entity " + std::to_string(e);
				if (const auto* nc = ecs.getComponent<ECS::NameComponent>(e))
				{
					if (!nc->displayName.empty()) label = nc->displayName + " (Entity " + std::to_string(e) + ")";
				}
				results.push_back({ label, "Entity (Mesh)" });
			}
		}
	}
	{
		ECS::Schema schema;
		schema.require<ECS::MaterialComponent>();
		for (const auto e : ecs.getEntitiesMatchingSchema(schema))
		{
			const auto* mat = ecs.getComponent<ECS::MaterialComponent>(e);
			if (mat && mat->materialAssetPath == relPath)
			{
				std::string label = "Entity " + std::to_string(e);
				if (const auto* nc = ecs.getComponent<ECS::NameComponent>(e))
				{
					if (!nc->displayName.empty()) label = nc->displayName + " (Entity " + std::to_string(e) + ")";
				}
				results.push_back({ label, "Entity (Material)" });
			}
		}
	}
	{
		ECS::Schema schema;
		schema.require<ECS::LogicComponent>();
		for (const auto e : ecs.getEntitiesMatchingSchema(schema))
		{
			const auto* sc = ecs.getComponent<ECS::LogicComponent>(e);
			if (sc && sc->scriptPath == relPath)
			{
				std::string label = "Entity " + std::to_string(e);
				if (const auto* nc = ecs.getComponent<ECS::NameComponent>(e))
				{
					if (!nc->displayName.empty()) label = nc->displayName + " (Entity " + std::to_string(e) + ")";
				}
				results.push_back({ label, "Entity (Script)" });
			}
		}
	}

	return results;
}

std::vector<std::string> AssetManager::getAssetDependencies(const std::string& relPath) const
{
	std::vector<std::string> results;
	if (relPath.empty()) return results;

	auto& diagnostics = DiagnosticsManager::Instance();
	if (!diagnostics.isProjectLoaded()) return results;

	const fs::path contentDir = fs::path(diagnostics.getProjectInfo().projectPath) / "Content";
	const fs::path absPath = contentDir / fs::path(relPath);

	std::ifstream in(absPath, std::ios::in | std::ios::binary);
	if (!in.is_open()) return results;

	json fileJson;
	try { fileJson = json::parse(in); }
	catch (...) { return results; }
	in.close();

	if (!fileJson.is_object()) return results;

	// Collect all asset path strings from "data" and "Entities" blocks
	if (fileJson.contains("data"))
		collectAssetPathStrings(fileJson["data"], results);
	if (fileJson.contains("Entities"))
		collectAssetPathStrings(fileJson["Entities"], results);

	// De-duplicate
	std::sort(results.begin(), results.end());
	results.erase(std::unique(results.begin(), results.end()), results.end());

	// Remove self-reference
	results.erase(std::remove(results.begin(), results.end(), relPath), results.end());

	return results;
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
#endif
