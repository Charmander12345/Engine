#pragma once

#include "GameplayAPIExport.h"
#include "INativeScript.h"

#include <string>
#include <unordered_map>
#include <functional>
#include <vector>

/// Global registry that maps class names to factory functions.
/// Populated automatically via REGISTER_NATIVE_SCRIPT macros when
/// the gameplay DLL is loaded.
class GAMEPLAY_API NativeScriptRegistry
{
public:
	using FactoryFunc = std::function<INativeScript*()>;

	static NativeScriptRegistry& Instance();

	void registerClass(const std::string& name, FactoryFunc factory);
	void unregisterAll();

	INativeScript* createInstance(const std::string& className) const;
	bool hasClass(const std::string& className) const;
	std::vector<std::string> getRegisteredClassNames() const;

private:
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251) // private STL member – not exported across DLL boundary
#endif
	std::unordered_map<std::string, FactoryFunc> m_factories;
#ifdef _MSC_VER
#pragma warning(pop)
#endif
};

// ── Registration macro (used in every script .cpp) ──────────────────
// Creates a static object whose constructor auto-registers the class
// in the registry when the DLL is loaded.
#define REGISTER_NATIVE_SCRIPT(ClassName)                                    \
	static struct ClassName##_AutoRegister {                                  \
		ClassName##_AutoRegister() {                                          \
			NativeScriptRegistry::Instance().registerClass(                   \
				#ClassName,                                                   \
				[]() -> INativeScript* { return new ClassName(); });          \
		}                                                                     \
	} g_##ClassName##_autoReg;
