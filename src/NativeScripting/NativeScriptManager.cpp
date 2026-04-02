// Prevent wingdi.h from defining the ERROR macro (collides with
// static_cast<Logger::LogLevel>(2)).  Must come before any transitive include
// of <Windows.h>.
#ifndef NOGDI
#define NOGDI
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "NativeScriptManager.h"
#include "NativeScriptRegistry.h"

#include "../Core/ECS/ECS.h"
#include "../Logger/Logger.h"

#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#ifdef ERROR
#undef ERROR
#endif

NativeScriptManager& NativeScriptManager::Instance()
{
static NativeScriptManager instance;
return instance;
}

NativeScriptManager::~NativeScriptManager()
{
shutdownScripts();
unloadGameplayDLL();
}

bool NativeScriptManager::loadGameplayDLL(const std::string& dllPath)
{
if (m_dllHandle)
{
shutdownScripts();
NativeScriptRegistry::Instance().unregisterAll();
unloadGameplayDLL();
}

#ifdef _WIN32
m_dllHandle = LoadLibraryA(dllPath.c_str());
#else
m_dllHandle = dlopen(dllPath.c_str(), RTLD_NOW);
#endif

if (!m_dllHandle)
{
Logger::Instance().log(Logger::Category::Engine,
"NativeScriptManager: failed to load DLL: " + dllPath,
static_cast<Logger::LogLevel>(2));
return false;
}

m_dllPath = dllPath;

auto registered = NativeScriptRegistry::Instance().getRegisteredClassNames();
Logger::Instance().log(Logger::Category::Engine,
"NativeScriptManager: loaded DLL with " + std::to_string(registered.size()) + " class(es): " + dllPath,
Logger::LogLevel::INFO);

return true;
}

void NativeScriptManager::unloadGameplayDLL()
{
if (!m_dllHandle)
{
return;
}

#ifdef _WIN32
FreeLibrary(static_cast<HMODULE>(m_dllHandle));
#else
dlclose(m_dllHandle);
#endif

m_dllHandle = nullptr;
m_dllPath.clear();
}

bool NativeScriptManager::isDLLLoaded() const
{
return m_dllHandle != nullptr;
}

const std::string& NativeScriptManager::getDLLPath() const
{
return m_dllPath;
}

void NativeScriptManager::initializeScripts()
{
auto& ecs = ECS::ECSManager::Instance();

ECS::Schema schema;
schema.require<ECS::LogicComponent>();
auto entities = ecs.getEntitiesMatchingSchema(schema);

for (auto entity : entities)
{
auto* comp = ecs.getComponent<ECS::LogicComponent>(entity);
if (!comp || comp->nativeClassName.empty())
{
continue;
}

if (m_instances.find(entity) != m_instances.end())
{
continue;
}

createInstance(entity, comp->nativeClassName);
}
}

void NativeScriptManager::updateScripts(float deltaTime)
{
m_lastDeltaTime = deltaTime;
for (auto& [entity, script] : m_instances)
{
if (script)
{
script->tick(deltaTime);
}
}
}

float NativeScriptManager::getLastDeltaTime() const
{
return m_lastDeltaTime;
}

void NativeScriptManager::shutdownScripts()
{
for (auto& [entity, script] : m_instances)
{
if (script)
{
script->onDestroy();
delete script;
}
}
m_instances.clear();
}

void NativeScriptManager::createInstance(ECS::Entity entity, const std::string& className)
{
destroyInstance(entity);

INativeScript* script = NativeScriptRegistry::Instance().createInstance(className);
if (!script)
{
Logger::Instance().log(Logger::Category::Engine,
"NativeScriptManager: class not found in registry: " + className,
Logger::LogLevel::WARNING);
return;
}

script->m_entity = entity;
m_instances[entity] = script;

script->onLoaded();
}

void NativeScriptManager::destroyInstance(ECS::Entity entity)
{
auto it = m_instances.find(entity);
if (it == m_instances.end())
{
return;
}

if (it->second)
{
it->second->onDestroy();
delete it->second;
}
m_instances.erase(it);
}

void NativeScriptManager::dispatchBeginOverlap(ECS::Entity self, ECS::Entity other)
{
auto it = m_instances.find(self);
if (it != m_instances.end() && it->second)
{
it->second->onBeginOverlap(other);
}
}

void NativeScriptManager::dispatchEndOverlap(ECS::Entity self, ECS::Entity other)
{
auto it = m_instances.find(self);
if (it != m_instances.end() && it->second)
{
it->second->onEndOverlap(other);
}
}

#if ENGINE_EDITOR
void NativeScriptManager::hotReload()
{
if (!m_dllHandle || m_dllPath.empty())
{
return;
}

shutdownScripts();

NativeScriptRegistry::Instance().unregisterAll();

#ifdef _WIN32
FreeLibrary(static_cast<HMODULE>(m_dllHandle));
#else
dlclose(m_dllHandle);
#endif
m_dllHandle = nullptr;

#ifdef _WIN32
m_dllHandle = LoadLibraryA(m_dllPath.c_str());
#else
m_dllHandle = dlopen(m_dllPath.c_str(), RTLD_NOW);
#endif

if (!m_dllHandle)
{
Logger::Instance().log(Logger::Category::Engine,
"NativeScriptManager: hot-reload failed to reload DLL: " + m_dllPath,
static_cast<Logger::LogLevel>(2));
return;
}

auto registered = NativeScriptRegistry::Instance().getRegisteredClassNames();
Logger::Instance().log(Logger::Category::Engine,
"NativeScriptManager: hot-reloaded " + std::to_string(registered.size()) + " class(es)",
Logger::LogLevel::INFO);

initializeScripts();
}
#endif