#include "NativeScriptRegistry.h"

#include <algorithm>

NativeScriptRegistry& NativeScriptRegistry::Instance()
{
	static NativeScriptRegistry instance;
	return instance;
}

void NativeScriptRegistry::registerClass(const std::string& name, FactoryFunc factory)
{
	m_factories[name] = std::move(factory);
}

void NativeScriptRegistry::unregisterAll()
{
	m_factories.clear();
}

INativeScript* NativeScriptRegistry::createInstance(const std::string& className) const
{
	auto it = m_factories.find(className);
	if (it != m_factories.end())
	{
		return it->second();
	}
	return nullptr;
}

bool NativeScriptRegistry::hasClass(const std::string& className) const
{
	return m_factories.find(className) != m_factories.end();
}

std::vector<std::string> NativeScriptRegistry::getRegisteredClassNames() const
{
	std::vector<std::string> names;
	names.reserve(m_factories.size());
	for (const auto& [name, factory] : m_factories)
	{
		names.push_back(name);
	}
	std::sort(names.begin(), names.end());
	return names;
}
