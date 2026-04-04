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
m_cachedClassNames = registered;

if (registered.empty())
{
Logger::Instance().log(Logger::Category::Engine,
"NativeScriptManager: WARNING – DLL loaded but 0 script classes registered.\n"
"  Make sure your .cpp files use REGISTER_NATIVE_SCRIPT(ClassName) after the class definition.\n"
"  Example:\n"
"    class MyScript : public INativeScript { ... };\n"
"    REGISTER_NATIVE_SCRIPT(MyScript)",
Logger::LogLevel::WARNING);
}
else
{
std::string classList;
for (const auto& name : registered)
{
    if (!classList.empty()) classList += ", ";
    classList += name;
}
Logger::Instance().log(Logger::Category::Engine,
"NativeScriptManager: loaded DLL with " + std::to_string(registered.size()) + " class(es): [" + classList + "] from " + dllPath,
Logger::LogLevel::INFO);
}

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

const std::vector<std::string>& NativeScriptManager::getAvailableClassNames() const
{
return m_cachedClassNames;
}

void NativeScriptManager::initializeScripts()
{
auto& ecs = ECS::ECSManager::Instance();

ECS::Schema schema;
schema.require<ECS::LogicComponent>();
auto entities = ecs.getEntitiesMatchingSchema(schema);

int assignedCount = 0;
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

Logger::Instance().log(Logger::Category::Engine,
"NativeScriptManager: creating instance of '" + comp->nativeClassName + "' for entity " + std::to_string(entity),
Logger::LogLevel::INFO);
createInstance(entity, comp->nativeClassName);
++assignedCount;
}

if (assignedCount == 0 && !entities.empty())
{
Logger::Instance().log(Logger::Category::Engine,
"NativeScriptManager: " + std::to_string(entities.size()) + " entity(ies) have a LogicComponent but none have a nativeClassName set.\n"
"  Assign a C++ class via the entity details panel (Logic > C++ Class dropdown).",
Logger::LogLevel::INFO);
}
else
{
Logger::Instance().log(Logger::Category::Engine,
"NativeScriptManager: initialized " + std::to_string(assignedCount) + " C++ script instance(s).",
Logger::LogLevel::INFO);
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

void NativeScriptManager::setPythonCallHandler(PythonCallHandler handler)
{
m_pythonCallHandler = std::move(handler);
}

ScriptValue NativeScriptManager::callPythonForEntity(ECS::Entity entity, const char* funcName, const std::vector<ScriptValue>& args) const
{
if (m_pythonCallHandler)
{
return m_pythonCallHandler(entity, funcName, args);
}
return ScriptValue{};
}

INativeScript* NativeScriptManager::getInstance(ECS::Entity entity) const
{
auto it = m_instances.find(entity);
if (it != m_instances.end())
{
return it->second;
}
return nullptr;
}

ScriptValue NativeScriptManager::callFunction(ECS::Entity entity, const char* funcName, const std::vector<ScriptValue>& args) const
{
if (!funcName) return ScriptValue{};

// 1. Try C++ native script first
auto it = m_instances.find(entity);
if (it != m_instances.end() && it->second)
{
ScriptValue result = it->second->onScriptCall(funcName, args);
if (result.type != ScriptValue::None)
{
return result;
}
}

// 2. Fall back to Python script
if (m_pythonCallHandler)
{
return m_pythonCallHandler(entity, funcName, args);
}

return ScriptValue{};
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