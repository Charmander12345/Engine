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
	enum Type { None, Float, Int, Bool, String };
	Type type = None;
	float floatVal = 0.0f;
	int intVal = 0;
	bool boolVal = false;
	std::string stringVal;

	ScriptValue() = default;
	static ScriptValue makeFloat(float v)              { ScriptValue s; s.type = Float;  s.floatVal = v;    return s; }
	static ScriptValue makeInt(int v)                   { ScriptValue s; s.type = Int;    s.intVal = v;      return s; }
	static ScriptValue makeBool(bool v)                 { ScriptValue s; s.type = Bool;   s.boolVal = v;     return s; }
	static ScriptValue makeString(const std::string& v) { ScriptValue s; s.type = String; s.stringVal = v;   return s; }
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

	/// Call a Python function defined in this entity's Python script.
	/// Returns ScriptValue::None if the entity has no Python script or the function is missing.
	GAMEPLAY_API ScriptValue callPythonFunction(const char* funcName, const std::vector<ScriptValue>& args = {}) const;

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

	// Cross-entity transform access
	GAMEPLAY_API static bool getPositionOf(ECS::Entity entity, float outPos[3]);
	GAMEPLAY_API static bool getRotationOf(ECS::Entity entity, float outRot[3]);
	GAMEPLAY_API static bool getScaleOf(ECS::Entity entity, float outScale[3]);
	GAMEPLAY_API static bool setPositionOf(ECS::Entity entity, const float pos[3]);
	GAMEPLAY_API static bool setRotationOf(ECS::Entity entity, const float rot[3]);
	GAMEPLAY_API static bool setScaleOf(ECS::Entity entity, const float scale[3]);

	// Cross-entity physics access
	GAMEPLAY_API static void getVelocityOf(ECS::Entity entity, float outVel[3]);
	GAMEPLAY_API static void setVelocityOf(ECS::Entity entity, const float vel[3]);

	// Cross-script communication
	GAMEPLAY_API static ScriptValue callScriptFunction(ECS::Entity entity, const char* funcName, const std::vector<ScriptValue>& args = {});
	GAMEPLAY_API static ScriptValue callPythonFunctionOn(ECS::Entity entity, const char* funcName, const std::vector<ScriptValue>& args = {});

	/// Unified call: automatically routes to C++ (onScriptCall) or Python.
	/// Callers never need to know which language implements the function.
	GAMEPLAY_API static ScriptValue callFunction(ECS::Entity entity, const char* funcName, const std::vector<ScriptValue>& args = {});

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
	GAMEPLAY_API static void logInfo(const char* msg);
	GAMEPLAY_API static void logWarning(const char* msg);
	GAMEPLAY_API static void logError(const char* msg);

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

	// ── Argument extraction / conversion helpers ────────────────────

	template<typename T>
	static T extractArg(const ScriptValue& v)
	{
		if constexpr (std::is_same_v<T, float>)            return v.floatVal;
		else if constexpr (std::is_same_v<T, double>)      return static_cast<double>(v.floatVal);
		else if constexpr (std::is_same_v<T, int>)          return v.intVal;
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
