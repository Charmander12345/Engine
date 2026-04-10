#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <vector>

class Actor;

/// Global registry that maps actor class names to factory functions.
/// Populated automatically via REGISTER_ACTOR macros.
/// Similar to NativeScriptRegistry but for Actor subclasses.
class ActorRegistry
{
public:
	using FactoryFunc = std::function<Actor*()>;

	static ActorRegistry& Instance()
	{
		static ActorRegistry instance;
		return instance;
	}

	/// Register an actor class with its name and factory function.
	void registerClass(const std::string& name, FactoryFunc factory)
	{
		m_factories[name] = std::move(factory);
	}

	/// Remove all registered classes.
	void unregisterAll()
	{
		m_factories.clear();
	}

	/// Create an actor instance by class name.  Returns nullptr if unknown.
	Actor* createActor(const std::string& className) const
	{
		auto it = m_factories.find(className);
		if (it != m_factories.end())
			return it->second();
		return nullptr;
	}

	/// Check if a class name is registered.
	bool hasClass(const std::string& className) const
	{
		return m_factories.find(className) != m_factories.end();
	}

	/// Get all registered class names.
	std::vector<std::string> getRegisteredClassNames() const
	{
		std::vector<std::string> names;
		names.reserve(m_factories.size());
		for (const auto& [name, factory] : m_factories)
			names.push_back(name);
		return names;
	}

private:
	ActorRegistry() = default;
	std::unordered_map<std::string, FactoryFunc> m_factories;
};

// ── Registration macro ──────────────────────────────────────────────
// Use REGISTER_ACTOR(MyActor) in any .cpp file to auto-register.
// The actor can then be spawned via World::spawnActorByClass("MyActor").
#define REGISTER_ACTOR(ClassName)                                          \
	static struct ClassName##_ActorAutoRegister {                           \
		ClassName##_ActorAutoRegister() {                                   \
			ActorRegistry::Instance().registerClass(                        \
				#ClassName,                                                 \
				[]() -> Actor* { return new ClassName(); });                \
		}                                                                   \
	} g_##ClassName##_actorAutoReg;
