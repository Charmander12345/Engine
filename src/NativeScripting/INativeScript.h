#pragma once

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <type_traits>
#include <utility>
#include "GameplayAPIExport.h"

namespace ECS { using Entity = unsigned int; }

/// Simple variant for cross-language script communication (C++ <-> Python).
struct ScriptValue
{
	enum Type { None, Float, Int, Bool, String, Vec3 };
	Type type = None;
	float floatVal = 0.0f;
	int intVal = 0;
	bool boolVal = false;
	std::string stringVal;
	float vec3Val[3]{};

	ScriptValue() = default;
	static ScriptValue makeFloat(float v)              { ScriptValue s; s.type = Float;  s.floatVal = v;    return s; }
	static ScriptValue makeInt(int v)                   { ScriptValue s; s.type = Int;    s.intVal = v;      return s; }
	static ScriptValue makeBool(bool v)                 { ScriptValue s; s.type = Bool;   s.boolVal = v;     return s; }
	static ScriptValue makeString(const std::string& v) { ScriptValue s; s.type = String; s.stringVal = v;   return s; }
	static ScriptValue makeVec3(float x, float y, float z) { ScriptValue s; s.type = Vec3; s.vec3Val[0] = x; s.vec3Val[1] = y; s.vec3Val[2] = z; return s; }
};

/// Base class for all C++ gameplay scripts.
/// The game developer inherits from this and overrides the lifecycle methods.
class INativeScript
{
public:
	virtual ~INativeScript() = default;

	// ── Lifecycle events (overridden by the game developer) ──────────
	virtual void onLoaded()  {}
	virtual void tick(float deltaTime) { (void)deltaTime; }
	virtual void onBeginOverlap(ECS::Entity other) { (void)other; }
	virtual void onEndOverlap(ECS::Entity other)   { (void)other; }
	virtual void onDestroy() {}

	/// Called when Python (or another script) invokes a named function on this C++ script.
	/// The default implementation dispatches to methods registered with SCRIPT_METHOD().
	/// You can still override this for custom dispatch logic.
	virtual ScriptValue onScriptCall(const char* funcName, const std::vector<ScriptValue>& args)
	{
		auto it = m_scriptMethods.find(funcName);
		if (it != m_scriptMethods.end()) return it->second(args);
		return ScriptValue{};
	}

	// ── Engine-set helpers ────────────────────────────────────────────
	ECS::Entity getEntity() const { return m_entity; }

	// ── Script properties (editable from editor / other scripts) ─────
	/// Set a named property value on this script instance.
	void setProperty(const std::string& name, const ScriptValue& value) { m_scriptProperties[name] = value; }
	/// Get a named property value. Returns ScriptValue::None if not found.
	ScriptValue getProperty(const std::string& name) const
	{
		auto it = m_scriptProperties.find(name);
		return it != m_scriptProperties.end() ? it->second : ScriptValue{};
	}
	/// Check if a named property exists.
	bool hasProperty(const std::string& name) const { return m_scriptProperties.find(name) != m_scriptProperties.end(); }
	/// Get all property names.
	std::vector<std::string> getPropertyNames() const
	{
		std::vector<std::string> names;
		names.reserve(m_scriptProperties.size());
		for (const auto& [k, v] : m_scriptProperties) names.push_back(k);
		return names;
	}

	// ── Convenience API (delegates to GameplayAPI for own entity) ────

	// Transform
	GAMEPLAY_API bool getPosition(float outPos[3]) const;
	GAMEPLAY_API bool setPosition(const float pos[3]);
	GAMEPLAY_API bool getRotation(float outRot[3]) const;
	GAMEPLAY_API bool setRotation(const float rot[3]);
	GAMEPLAY_API bool getScale(float outScale[3]) const;
	GAMEPLAY_API bool setScale(const float scale[3]);
	GAMEPLAY_API bool getTransform(float outPos[3], float outRot[3], float outScale[3]) const;
	GAMEPLAY_API bool translate(const float delta[3]);
	GAMEPLAY_API bool rotate(const float delta[3]);

	// Transform Parenting
	GAMEPLAY_API bool        setParent(ECS::Entity parent);
	GAMEPLAY_API bool        removeParent();
	GAMEPLAY_API ECS::Entity getParent() const;
	GAMEPLAY_API int         getChildCount() const;
	GAMEPLAY_API int         getChildren(ECS::Entity* outChildren, int maxCount) const;
	GAMEPLAY_API ECS::Entity getRoot() const;
	GAMEPLAY_API bool        isAncestorOf(ECS::Entity descendant) const;
	GAMEPLAY_API bool        getLocalPosition(float outPos[3]) const;
	GAMEPLAY_API bool        setLocalPosition(const float pos[3]);
	GAMEPLAY_API bool        getLocalRotation(float outRot[3]) const;
	GAMEPLAY_API bool        setLocalRotation(const float rot[3]);
	GAMEPLAY_API bool        getLocalScale(float outScale[3]) const;
	GAMEPLAY_API bool        setLocalScale(const float scale[3]);

	// Physics
	GAMEPLAY_API void setVelocity(const float vel[3]);
	GAMEPLAY_API void getVelocity(float outVel[3]) const;
	GAMEPLAY_API void addForce(const float force[3]);
	GAMEPLAY_API void addImpulse(const float impulse[3]);
	GAMEPLAY_API void setAngularVelocity(const float vel[3]);
	GAMEPLAY_API void getAngularVelocity(float outVel[3]) const;

	// Light
	GAMEPLAY_API bool getLightColor(float outColor[3]) const;
	GAMEPLAY_API bool setLightColor(const float color[3]);

	// Particle
	GAMEPLAY_API bool setEmitterProperty(const char* key, float value);
	GAMEPLAY_API bool setEmitterEnabled(bool enabled);
	GAMEPLAY_API bool setEmitterColor(float r, float g, float b, float a);
	GAMEPLAY_API bool setEmitterEndColor(float r, float g, float b, float a);

	// Audio (static – works on any handle, not entity-specific)
	GAMEPLAY_API static unsigned int createAudio(const char* contentPath, bool loop = false, float gain = 1.0f);
	GAMEPLAY_API static unsigned int playAudio(const char* contentPath, bool loop = false, float gain = 1.0f);
	GAMEPLAY_API static bool         playAudioHandle(unsigned int handle);
	GAMEPLAY_API static bool         setAudioVolume(unsigned int handle, float gain);
	GAMEPLAY_API static float        getAudioVolume(unsigned int handle);
	GAMEPLAY_API static bool         pauseAudio(unsigned int handle);
	GAMEPLAY_API static bool         stopAudio(unsigned int handle);
	GAMEPLAY_API static bool         isAudioPlaying(unsigned int handle);
	GAMEPLAY_API static bool         invalidateAudioHandle(unsigned int handle);
	GAMEPLAY_API static bool         setAudioPosition(unsigned int handle, float x, float y, float z);
	GAMEPLAY_API static bool         setAudioSpatial(unsigned int handle, bool is3D, float minDist = 1.0f, float maxDist = 50.0f, float rolloff = 1.0f);

	// Entity management
	GAMEPLAY_API static ECS::Entity findEntityByName(const char* name);
	GAMEPLAY_API static ECS::Entity createEntity();
	GAMEPLAY_API static bool removeEntity(ECS::Entity entity);

	// Entity queries
	GAMEPLAY_API static bool        isEntityValid(ECS::Entity entity);
	GAMEPLAY_API static bool        hasComponent(ECS::Entity entity, int componentKind);
	GAMEPLAY_API static const char* getEntityName(ECS::Entity entity);
	GAMEPLAY_API static bool        setEntityName(ECS::Entity entity, const char* name);
	GAMEPLAY_API static int         getEntityCount();
	GAMEPLAY_API static int         getAllEntities(ECS::Entity* outEntities, int maxCount);
	GAMEPLAY_API static float       distanceBetween(ECS::Entity a, ECS::Entity b);

	// Cross-script communication
	/// Unified call: automatically routes to C++ (onScriptCall) or Python.
	/// Callers never need to know which language implements the function.
	GAMEPLAY_API static ScriptValue callFunction(ECS::Entity entity, const char* funcName, const std::vector<ScriptValue>& args = {});

	/// Call a function on this entity's own scripts (C++ or Python).
	/// Convenience overload so scripts can use callFunction("func") instead of callFunction(getEntity(), "func").
	ScriptValue callFunction(const char* funcName, const std::vector<ScriptValue>& args = {})
	{
		return callFunction(m_entity, funcName, args);
	}

	// Global state
	GAMEPLAY_API static bool        setGlobalNumber(const char* name, double value);
	GAMEPLAY_API static bool        setGlobalString(const char* name, const char* value);
	GAMEPLAY_API static bool        setGlobalBool(const char* name, bool value);
	GAMEPLAY_API static double      getGlobalNumber(const char* name);
	GAMEPLAY_API static const char* getGlobalString(const char* name);
	GAMEPLAY_API static bool        getGlobalBool(const char* name);
	GAMEPLAY_API static bool        removeGlobal(const char* name);
	GAMEPLAY_API static void        clearGlobals();

	// Logging
	GAMEPLAY_API static void log(const char* msg, int level = 0);

	// Time
	GAMEPLAY_API static float getDeltaTime();
	GAMEPLAY_API static float getTotalTime();

protected:
	// ── Script method registration ──────────────────────────────────
	/// Register a member function so it can be called by name from Python.
	/// Use the SCRIPT_METHOD(methodName) macro for convenience.
	template<typename T, typename Ret, typename... Args>
	void registerMethod(const char* name, Ret(T::*method)(Args...))
	{
		T* self = static_cast<T*>(this);
		m_scriptMethods[name] = [self, method](const std::vector<ScriptValue>& a) -> ScriptValue {
			return invokeMethod(self, method, a);
		};
	}

	/// Const-method overload.
	template<typename T, typename Ret, typename... Args>
	void registerMethod(const char* name, Ret(T::*method)(Args...) const)
	{
		T* self = static_cast<T*>(this);
		m_scriptMethods[name] = [self, method](const std::vector<ScriptValue>& a) -> ScriptValue {
			return invokeMethod(self, method, a);
		};
	}

private:
	friend class NativeScriptManager;
	ECS::Entity m_entity{ 0 };
	std::unordered_map<std::string, std::function<ScriptValue(const std::vector<ScriptValue>&)>> m_scriptMethods;
	std::unordered_map<std::string, ScriptValue> m_scriptProperties;

	// ── Argument extraction / conversion helpers ────────────────────

	template<typename T>
	static T extractArg(const ScriptValue& v)
	{
		if constexpr (std::is_same_v<T, float>)            return v.type == ScriptValue::Int ? static_cast<float>(v.intVal) : v.floatVal;
		else if constexpr (std::is_same_v<T, double>)      return v.type == ScriptValue::Int ? static_cast<double>(v.intVal) : static_cast<double>(v.floatVal);
		else if constexpr (std::is_same_v<T, int>)          return v.type == ScriptValue::Float ? static_cast<int>(v.floatVal) : v.intVal;
		else if constexpr (std::is_same_v<T, bool>)         return v.boolVal;
		else if constexpr (std::is_same_v<T, std::string>)  return v.stringVal;
		else if constexpr (std::is_same_v<T, const std::string&>) return v.stringVal;
		else if constexpr (std::is_same_v<T, ScriptValue>)  return v;
		else if constexpr (std::is_same_v<T, const ScriptValue&>) return v;
		else static_assert(sizeof(T) == 0, "Unsupported argument type for script method");
	}

	template<typename T>
	static ScriptValue toScriptValue(const T& v)
	{
		if constexpr (std::is_same_v<T, float>)            return ScriptValue::makeFloat(v);
		else if constexpr (std::is_same_v<T, double>)      return ScriptValue::makeFloat(static_cast<float>(v));
		else if constexpr (std::is_same_v<T, int>)          return ScriptValue::makeInt(v);
		else if constexpr (std::is_same_v<T, bool>)         return ScriptValue::makeBool(v);
		else if constexpr (std::is_same_v<T, std::string>)  return ScriptValue::makeString(v);
		else if constexpr (std::is_same_v<T, ScriptValue>)  return v;
		else static_assert(sizeof(T) == 0, "Unsupported return type for script method");
	}

	// Invoke with unpacked arguments (non-void return)
	template<typename T, typename Ret, typename... Args, std::size_t... I>
	static ScriptValue invokeUnpacked(T* self, Ret(T::*method)(Args...), const std::vector<ScriptValue>& a, std::index_sequence<I...>)
	{
		return toScriptValue<Ret>((self->*method)(extractArg<std::decay_t<Args>>(I < a.size() ? a[I] : ScriptValue{})...));
	}

	// Invoke with unpacked arguments (void return)
	template<typename T, typename... Args, std::size_t... I>
	static ScriptValue invokeUnpacked(T* self, void(T::*method)(Args...), const std::vector<ScriptValue>& a, std::index_sequence<I...>)
	{
		(self->*method)(extractArg<std::decay_t<Args>>(I < a.size() ? a[I] : ScriptValue{})...);
		return ScriptValue::makeBool(true);
	}

	// Invoke with unpacked arguments (non-void return, const)
	template<typename T, typename Ret, typename... Args, std::size_t... I>
	static ScriptValue invokeUnpacked(T* self, Ret(T::*method)(Args...) const, const std::vector<ScriptValue>& a, std::index_sequence<I...>)
	{
		return toScriptValue<Ret>((self->*method)(extractArg<std::decay_t<Args>>(I < a.size() ? a[I] : ScriptValue{})...));
	}

	// Invoke with unpacked arguments (void return, const)
	template<typename T, typename... Args, std::size_t... I>
	static ScriptValue invokeUnpacked(T* self, void(T::*method)(Args...) const, const std::vector<ScriptValue>& a, std::index_sequence<I...>)
	{
		(self->*method)(extractArg<std::decay_t<Args>>(I < a.size() ? a[I] : ScriptValue{})...);
		return ScriptValue::makeBool(true);
	}

	// Entry point: deduce Args count and dispatch
	template<typename T, typename Ret, typename... Args>
	static ScriptValue invokeMethod(T* self, Ret(T::*method)(Args...), const std::vector<ScriptValue>& a)
	{
		return invokeUnpacked(self, method, a, std::make_index_sequence<sizeof...(Args)>{});
	}

	template<typename T, typename Ret, typename... Args>
	static ScriptValue invokeMethod(T* self, Ret(T::*method)(Args...) const, const std::vector<ScriptValue>& a)
	{
		return invokeUnpacked(self, method, a, std::make_index_sequence<sizeof...(Args)>{});
	}
};

/// Convenience macro: registers a member function of the current class as a script-callable method.
/// Usage in onLoaded():  SCRIPT_METHOD(myFunction);
#define SCRIPT_METHOD(method) \
	this->registerMethod(#method, &std::remove_pointer_t<decltype(this)>::method)

/// Convenience macro: registers a named property with a default value.
/// Usage in onLoaded():  SCRIPT_PROPERTY(speed, 5.0f);
/// The property is accessible from other scripts via getProperty/setProperty
/// and will be exposed in the editor details panel.
#define SCRIPT_PROPERTY(name, defaultValue) \
	do { if (!this->hasProperty(#name)) this->setProperty(#name, ScriptValue::makeFloat(static_cast<float>(defaultValue))); } while(0)

#define SCRIPT_PROPERTY_INT(name, defaultValue) \
	do { if (!this->hasProperty(#name)) this->setProperty(#name, ScriptValue::makeInt(defaultValue)); } while(0)

#define SCRIPT_PROPERTY_BOOL(name, defaultValue) \
	do { if (!this->hasProperty(#name)) this->setProperty(#name, ScriptValue::makeBool(defaultValue)); } while(0)

#define SCRIPT_PROPERTY_STRING(name, defaultValue) \
	do { if (!this->hasProperty(#name)) this->setProperty(#name, ScriptValue::makeString(defaultValue)); } while(0)
