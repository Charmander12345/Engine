#pragma once

#include <string>
#include <vector>
#include "../../AssetManager/json.hpp"
#include "../../AssetManager/AssetTypes.h"

using json = nlohmann::json;

/// Describes one child actor slot inside an ActorAsset definition.
/// Each child actor holds its own mesh + material pair, a local transform
/// relative to the parent, and can recursively contain further children
/// to form a hierarchy.
struct ChildActorEntry
{
	std::string actorClass;     // e.g. "StaticMeshActor", "PointLightActor", ...
	std::string name;           // display name for this child

	// Visual data — a StaticMesh-type child holds exactly one mesh + material.
	std::string meshPath;       // content-relative mesh asset path (may be empty)
	std::string materialPath;   // content-relative material asset path (may be empty)

	// Local transform relative to the parent actor
	float position[3]{ 0.0f, 0.0f, 0.0f };
	float rotation[3]{ 0.0f, 0.0f, 0.0f };
	float scale[3]{ 1.0f, 1.0f, 1.0f };

	// Nested children
	std::vector<ChildActorEntry> children;

	json toJson() const;
	static ChildActorEntry fromJson(const json& j);
};

/// In-memory representation of an .actor asset file.
/// An ActorAsset is a reusable actor template (similar to Unreal Blueprints).
/// It stores:
///   - a base actor class name (from ActorRegistry)
///   - a tree of child actors (each with mesh + material + local transform)
///   - an embedded script section (C++ script class name + auto-generated file path)
///
/// The engine automatically creates a C++ script file for every new actor asset
/// and cleans it up when the actor asset is deleted.
struct ActorAssetData
{
	std::string name;                             // display name
	std::string actorClass;                       // registered Actor class (e.g. "StaticMeshActor")
	std::vector<ChildActorEntry> childActors;     // nestable child actor hierarchy
	std::string tag;                              // default tag

	// Root actor visual data (mesh + material for the root itself)
	std::string meshPath;       // content-relative mesh asset path (may be empty)
	std::string materialPath;   // content-relative material asset path (may be empty)

	// ── Embedded script ──────────────────────────────────────────────
	std::string scriptClassName;      // auto-generated C++ class name (e.g. "MyActor_Script")
	std::string scriptHeaderPath;     // content-relative .h path
	std::string scriptCppPath;        // content-relative .cpp path
	bool        scriptEnabled{ true };

	// ── Serialization ────────────────────────────────────────────────
	json toJson() const;
	static ActorAssetData fromJson(const json& j);

	// ── Script file lifecycle ──────────────────────────────────────
	/// Generate the C++ header + source template files for this actor's
	/// embedded script.  Called automatically when a new actor asset is
	/// created.  `contentDir` is the absolute path to the project's
	/// Content folder.  Returns true on success.
	bool generateScriptFiles(const std::string& contentDir) const;

	/// Delete the generated script files from disk.
	/// Called automatically when the actor asset is deleted.
	bool cleanupScriptFiles(const std::string& contentDir) const;
};
