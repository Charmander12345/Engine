#include "ActorAssetData.h"
#include "../Archive.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

// ── ChildActorEntry serialization ─────────────────────────────────────────

json ChildActorEntry::toJson() const
{
	json j;
	j["actorClass"]   = actorClass;
	j["name"]         = name;
	j["meshPath"]     = meshPath;
	j["materialPath"] = materialPath;
	j["position"]     = { position[0], position[1], position[2] };
	j["rotation"]     = { rotation[0], rotation[1], rotation[2] };
	j["scale"]        = { scale[0],    scale[1],    scale[2] };

	if (!children.empty())
	{
		json arr = json::array();
		for (const auto& child : children)
			arr.push_back(child.toJson());
		j["children"] = arr;
	}
	return j;
}

ChildActorEntry ChildActorEntry::fromJson(const json& j)
{
	ChildActorEntry e;
	if (j.contains("actorClass"))   e.actorClass   = j["actorClass"].get<std::string>();
	if (j.contains("name"))         e.name         = j["name"].get<std::string>();
	if (j.contains("meshPath"))     e.meshPath     = j["meshPath"].get<std::string>();
	if (j.contains("materialPath")) e.materialPath = j["materialPath"].get<std::string>();

	if (j.contains("position") && j["position"].is_array() && j["position"].size() >= 3)
	{
		e.position[0] = j["position"][0].get<float>();
		e.position[1] = j["position"][1].get<float>();
		e.position[2] = j["position"][2].get<float>();
	}
	if (j.contains("rotation") && j["rotation"].is_array() && j["rotation"].size() >= 3)
	{
		e.rotation[0] = j["rotation"][0].get<float>();
		e.rotation[1] = j["rotation"][1].get<float>();
		e.rotation[2] = j["rotation"][2].get<float>();
	}
	if (j.contains("scale") && j["scale"].is_array() && j["scale"].size() >= 3)
	{
		e.scale[0] = j["scale"][0].get<float>();
		e.scale[1] = j["scale"][1].get<float>();
		e.scale[2] = j["scale"][2].get<float>();
	}

	if (j.contains("children") && j["children"].is_array())
	{
		for (const auto& cj : j["children"])
			e.children.push_back(ChildActorEntry::fromJson(cj));
	}
	return e;
}

// ── ActorAssetData serialization ──────────────────────────────────────────

json ActorAssetData::toJson() const
{
	json j;
	j["name"]         = name;
	j["actorClass"]   = actorClass;
	j["tag"]          = tag;
	j["meshPath"]     = meshPath;
	j["materialPath"] = materialPath;

	// Root transform
	j["rootPosition"] = { rootPosition[0], rootPosition[1], rootPosition[2] };
	j["rootRotation"] = { rootRotation[0], rootRotation[1], rootRotation[2] };
	j["rootScale"]    = { rootScale[0],    rootScale[1],    rootScale[2] };

	// Tick settings
	j["canEverTick"]  = canEverTick;
	j["tickGroup"]    = tickGroup;
	j["tickInterval"] = tickInterval;

	// Child actors
	json childArr = json::array();
	for (const auto& child : childActors)
		childArr.push_back(child.toJson());
	j["childActors"] = childArr;

	// Embedded script
	json script;
	script["className"]  = scriptClassName;
	script["headerPath"] = scriptHeaderPath;
	script["cppPath"]    = scriptCppPath;
	script["enabled"]    = scriptEnabled;
	j["script"] = script;

	return j;
}

ActorAssetData ActorAssetData::fromJson(const json& j)
{
	ActorAssetData data;

	if (j.contains("name"))         data.name         = j["name"].get<std::string>();
	if (j.contains("actorClass"))   data.actorClass   = j["actorClass"].get<std::string>();
	if (j.contains("tag"))          data.tag           = j["tag"].get<std::string>();
	if (j.contains("meshPath"))     data.meshPath      = j["meshPath"].get<std::string>();
	if (j.contains("materialPath")) data.materialPath  = j["materialPath"].get<std::string>();

	// Root transform
	if (j.contains("rootPosition") && j["rootPosition"].is_array() && j["rootPosition"].size() >= 3)
	{
		data.rootPosition[0] = j["rootPosition"][0].get<float>();
		data.rootPosition[1] = j["rootPosition"][1].get<float>();
		data.rootPosition[2] = j["rootPosition"][2].get<float>();
	}
	if (j.contains("rootRotation") && j["rootRotation"].is_array() && j["rootRotation"].size() >= 3)
	{
		data.rootRotation[0] = j["rootRotation"][0].get<float>();
		data.rootRotation[1] = j["rootRotation"][1].get<float>();
		data.rootRotation[2] = j["rootRotation"][2].get<float>();
	}
	if (j.contains("rootScale") && j["rootScale"].is_array() && j["rootScale"].size() >= 3)
	{
		data.rootScale[0] = j["rootScale"][0].get<float>();
		data.rootScale[1] = j["rootScale"][1].get<float>();
		data.rootScale[2] = j["rootScale"][2].get<float>();
	}

	// Tick settings
	if (j.contains("canEverTick"))  data.canEverTick  = j["canEverTick"].get<bool>();
	if (j.contains("tickGroup"))    data.tickGroup    = j["tickGroup"].get<int>();
	if (j.contains("tickInterval")) data.tickInterval = j["tickInterval"].get<float>();

	// Child actors (new format)
	if (j.contains("childActors") && j["childActors"].is_array())
	{
		for (const auto& cj : j["childActors"])
			data.childActors.push_back(ChildActorEntry::fromJson(cj));
	}

	// Legacy: migrate old "components" array to childActors
	if (data.childActors.empty() && j.contains("components") && j["components"].is_array())
	{
		for (const auto& entry : j["components"])
		{
			ChildActorEntry child;
			if (entry.contains("type"))
			{
				const std::string compType = entry["type"].get<std::string>();
				if (compType == "StaticMesh")
				{
					child.actorClass = "StaticMeshActor";
					if (entry.contains("properties"))
					{
						if (entry["properties"].contains("mesh"))
							child.meshPath = entry["properties"]["mesh"].get<std::string>();
						if (entry["properties"].contains("material"))
							child.materialPath = entry["properties"]["material"].get<std::string>();
					}
				}
				else
				{
					child.actorClass = compType;
				}
			}
			child.name = child.actorClass;
			data.childActors.push_back(std::move(child));
		}
	}

	// Script
	if (j.contains("script") && j["script"].is_object())
	{
		const auto& s = j["script"];
		if (s.contains("className"))  data.scriptClassName  = s["className"].get<std::string>();
		if (s.contains("headerPath")) data.scriptHeaderPath = s["headerPath"].get<std::string>();
		if (s.contains("cppPath"))    data.scriptCppPath    = s["cppPath"].get<std::string>();
		if (s.contains("enabled"))    data.scriptEnabled    = s["enabled"].get<bool>();
	}

	return data;
}

// ── Script file generation / cleanup ──────────────────────────────────────

/// Sanitize a name to be a valid C++ identifier.
static std::string sanitizeClassName(const std::string& input)
{
	std::string result;
	result.reserve(input.size());
	for (char c : input)
	{
		if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
			result += c;
	}
	// Must not start with a digit
	if (!result.empty() && std::isdigit(static_cast<unsigned char>(result[0])))
		result.insert(result.begin(), '_');
	if (result.empty())
		result = "ActorScript";
	return result;
}

bool ActorAssetData::generateScriptFiles(const std::string& contentDir) const
{
	if (scriptClassName.empty() || scriptHeaderPath.empty() || scriptCppPath.empty())
		return false;

	const fs::path headerAbs = fs::path(contentDir) / fs::path(scriptHeaderPath);
	const fs::path cppAbs    = fs::path(contentDir) / fs::path(scriptCppPath);

	// Don't overwrite existing files (user may have edited them)
	if (fs::exists(headerAbs) || fs::exists(cppAbs))
		return true;

	std::error_code ec;
	fs::create_directories(headerAbs.parent_path(), ec);
	fs::create_directories(cppAbs.parent_path(), ec);

	const std::string cls = sanitizeClassName(scriptClassName);
	const std::string headerFilename = fs::path(scriptHeaderPath).filename().string();

	// ── Header ───────────────────────────────────────────────────────
	{
		std::ofstream out(headerAbs, std::ios::out | std::ios::trunc);
		if (!out.is_open())
			return false;

		out << "#pragma once\n\n"
			<< "#include \"GameplayAPI.h\"\n\n"
			<< "/// Auto-generated script for actor asset: " << name << "\n"
			<< "/// Modify the lifecycle methods below to add gameplay logic.\n"
			<< "class " << cls << " : public INativeScript\n"
			<< "{\n"
			<< "public:\n"
			<< "\tvoid onLoaded() override\n"
			<< "\t{\n"
			<< "\t\t// Called when the script instance is created\n"
			<< "\t}\n\n"
			<< "\tvoid tick(float deltaTime) override\n"
			<< "\t{\n"
			<< "\t\t(void)deltaTime;\n"
			<< "\t\t// Called every frame\n"
			<< "\t}\n\n"
			<< "\tvoid onBeginOverlap(ECS::Entity other) override\n"
			<< "\t{\n"
			<< "\t\t(void)other;\n"
			<< "\t}\n\n"
			<< "\tvoid onEndOverlap(ECS::Entity other) override\n"
			<< "\t{\n"
			<< "\t\t(void)other;\n"
			<< "\t}\n\n"
			<< "\tvoid onDestroy() override\n"
			<< "\t{\n"
			<< "\t}\n"
			<< "};\n";
		out.close();
	}

	// ── Source ────────────────────────────────────────────────────────
	{
		std::ofstream out(cppAbs, std::ios::out | std::ios::trunc);
		if (!out.is_open())
			return false;

		out << "#include \"" << headerFilename << "\"\n\n"
			<< "// Register this script so the engine can attach it to actors.\n"
			<< "REGISTER_NATIVE_SCRIPT(" << cls << ")\n";
		out.close();
	}

	return true;
}

bool ActorAssetData::cleanupScriptFiles(const std::string& contentDir) const
{
	bool ok = true;
	std::error_code ec;

	if (!scriptHeaderPath.empty())
	{
		const fs::path headerAbs = fs::path(contentDir) / fs::path(scriptHeaderPath);
		if (fs::exists(headerAbs, ec))
			ok &= fs::remove(headerAbs, ec);
	}

	if (!scriptCppPath.empty())
	{
		const fs::path cppAbs = fs::path(contentDir) / fs::path(scriptCppPath);
		if (fs::exists(cppAbs, ec))
			ok &= fs::remove(cppAbs, ec);
	}

	return ok;
}

// ── Bidirectional binary serialization ────────────────────────────────────

static constexpr uint32_t kChildActorEntryVersion = 1;
static constexpr uint32_t kActorAssetDataVersion  = 2;

void ChildActorEntry::serialize(Archive& ar)
{
	ar.serializeVersion(kChildActorEntryVersion);

	ar << actorClass << name;
	ar << meshPath << materialPath;
	ar.serializeFloatArray(position);
	ar.serializeFloatArray(rotation);
	ar.serializeFloatArray(scale);

	uint32_t childCount = static_cast<uint32_t>(children.size());
	ar << childCount;
	if (ar.isLoading())
		children.resize(childCount);
	for (uint32_t i = 0; i < childCount; ++i)
		children[i].serialize(ar);
}

void ActorAssetData::serialize(Archive& ar)
{
	ar.serializeVersion(kActorAssetDataVersion);

	ar << name << actorClass << tag;
	ar << meshPath << materialPath;

	// Child actors
	uint32_t childCount = static_cast<uint32_t>(childActors.size());
	ar << childCount;
	if (ar.isLoading())
		childActors.resize(childCount);
	for (uint32_t i = 0; i < childCount; ++i)
		childActors[i].serialize(ar);

	// Root transform
	ar.serializeFloatArray(rootPosition);
	ar.serializeFloatArray(rootRotation);
	ar.serializeFloatArray(rootScale);

	// Tick settings
	ar << canEverTick << tickGroup << tickInterval;

	// Embedded script
	ar << scriptClassName << scriptHeaderPath << scriptCppPath << scriptEnabled;
}
