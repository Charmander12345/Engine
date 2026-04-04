#pragma once

#include "GameplayAPIExport.h"
#include "INativeScript.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace ECS { using Entity = unsigned int; }

/// Singleton that loads the gameplay DLL, creates / destroys script
/// instances and dispatches lifecycle events.
class GAMEPLAY_API NativeScriptManager
{
public:
	static NativeScriptManager& Instance();

	// ── DLL management ────────────────────────────────────────────────
	bool loadGameplayDLL(const std::string& dllPath);
	void unloadGameplayDLL();
	bool isDLLLoaded() const;
	const std::string& getDLLPath() const;

	/// Returns C++ script class names discovered from the last loaded DLL.
	/// Available even after the DLL has been unloaded so the editor can
	/// populate dropdowns for entity assignment.
	const std::vector<std::string>& getAvailableClassNames() const;

	// ── Script lifecycle ──────────────────────────────────────────────
	void initializeScripts();
	void updateScripts(float deltaTime);
	void shutdownScripts();
	float getLastDeltaTime() const;

	void createInstance(ECS::Entity entity, const std::string& className);
	void destroyInstance(ECS::Entity entity);

	// ── Overlap dispatch ──────────────────────────────────────────────
	void dispatchBeginOverlap(ECS::Entity self, ECS::Entity other);
	void dispatchEndOverlap(ECS::Entity self, ECS::Entity other);

	// ── Cross-language bridge (C++ <-> Python) ───────────────────────
	using PythonCallHandler = std::function<ScriptValue(ECS::Entity, const char*, const std::vector<ScriptValue>&)>;
	void setPythonCallHandler(PythonCallHandler handler);
	ScriptValue callPythonForEntity(ECS::Entity entity, const char* funcName, const std::vector<ScriptValue>& args) const;
	INativeScript* getInstance(ECS::Entity entity) const;

	/// Unified call: tries C++ onScriptCall first, then Python.
	/// Callers never need to know which language implements the function.
	ScriptValue callFunction(ECS::Entity entity, const char* funcName, const std::vector<ScriptValue>& args) const;

	// ── Hot-Reload (editor only) ──────────────────────────────────────
#if ENGINE_EDITOR
	void hotReload();
#endif

private:
	NativeScriptManager() = default;
	~NativeScriptManager();

	NativeScriptManager(const NativeScriptManager&) = delete;
	NativeScriptManager& operator=(const NativeScriptManager&) = delete;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251) // private STL members – not exported across DLL boundary
#endif
	void* m_dllHandle{ nullptr };
	std::string m_dllPath;
	float m_lastDeltaTime{ 0.0f };
	std::unordered_map<ECS::Entity, INativeScript*> m_instances;
	PythonCallHandler m_pythonCallHandler;
	std::vector<std::string> m_cachedClassNames;
#ifdef _MSC_VER
#pragma warning(pop)
#endif
};
