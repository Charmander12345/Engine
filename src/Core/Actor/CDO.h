#pragma once

#include "../ECS/ECS.h"
#include "../ECS/Components.h"
#include "../Reflection.h"
#include "Actor.h"
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

// ═══════════════════════════════════════════════════════════════════════
// Type-erased component default storage
// ═══════════════════════════════════════════════════════════════════════

/// Base interface for storing one component type's default values.
struct IComponentDefault
{
	virtual ~IComponentDefault() = default;
	virtual std::type_index getTypeIndex() const = 0;
	virtual const void*     getData()      const = 0;
	virtual void*           getMutableData()     = 0;
};

/// Typed implementation — stores an actual copy of the component.
template<typename T>
struct TComponentDefault : IComponentDefault
{
	T defaultValue{};

	TComponentDefault() = default;
	explicit TComponentDefault(const T& val) : defaultValue(val) {}

	std::type_index getTypeIndex() const override { return std::type_index(typeid(T)); }
	const void*     getData()      const override { return &defaultValue; }
	void*           getMutableData()     override { return &defaultValue; }
};

// ═══════════════════════════════════════════════════════════════════════
// ClassDefaultObject — default component state for one Actor class
// ═══════════════════════════════════════════════════════════════════════

/// Class Default Object — stores the default component configuration
/// for a registered Actor class.  Created once per class by constructing
/// a temporary actor instance and capturing all ECS component state
/// after beginPlay().
///
/// Used for:
///   - Delta serialization (only save values that differ from CDO)
///   - Editor override display (highlight non-default property values)
///   - Reset-to-default (restore a property to its CDO value)
class ClassDefaultObject
{
public:
	ClassDefaultObject() = default;
	explicit ClassDefaultObject(std::string className)
		: m_className(std::move(className)) {}

	const std::string& getClassName() const { return m_className; }

	// ── Component default access ─────────────────────────────────

	/// Get the default data for a component type (nullptr if not present).
	template<typename T>
	const T* getDefault() const
	{
		std::type_index ti(typeid(T));
		for (const auto& entry : m_defaults)
		{
			if (entry->getTypeIndex() == ti)
				return static_cast<const T*>(entry->getData());
		}
		return nullptr;
	}

	/// Get mutable default data (e.g. for CDO editing).
	template<typename T>
	T* getMutableDefault()
	{
		std::type_index ti(typeid(T));
		for (auto& entry : m_defaults)
		{
			if (entry->getTypeIndex() == ti)
				return static_cast<T*>(entry->getMutableData());
		}
		return nullptr;
	}

	/// Store a component default from a live ECS component.
	template<typename T>
	void captureDefault(const T& component)
	{
		std::type_index ti(typeid(T));
		for (auto& entry : m_defaults)
		{
			if (entry->getTypeIndex() == ti)
			{
				entry = std::make_unique<TComponentDefault<T>>(component);
				return;
			}
		}
		m_defaults.push_back(std::make_unique<TComponentDefault<T>>(component));
	}

	/// Check if a CDO default exists for a given component type.
	template<typename T>
	bool hasDefault() const
	{
		std::type_index ti(typeid(T));
		for (const auto& entry : m_defaults)
		{
			if (entry->getTypeIndex() == ti)
				return true;
		}
		return false;
	}

	// ── Property override detection ──────────────────────────────

	/// Check if a specific property in a component instance differs
	/// from the CDO default.  Returns true if overridden.
	template<typename T>
	bool isPropertyOverridden(const PropertyInfo& prop, const T& instance) const
	{
		const T* def = getDefault<T>();
		if (!def)
			return true;  // No CDO data → everything counts as overridden
		return !reflectPropertyEquals(prop, def, &instance);
	}

	/// Find all overridden properties for a component instance.
	template<typename T>
	std::vector<const PropertyInfo*> getOverriddenProperties(const T& instance) const
	{
		const T* def = getDefault<T>();
		if (!def)
			return {};
		const ClassInfo* ci = TypeRegistry::Instance().getClassInfo<T>();
		if (!ci)
			return {};
		return reflectDiffProperties(*ci, def, &instance);
	}

	// ── Tick settings ────────────────────────────────────────────

	const TickSettings& getDefaultTickSettings() const { return m_tickSettings; }
	void setDefaultTickSettings(const TickSettings& ts) { m_tickSettings = ts; }

	/// Number of component defaults stored.
	size_t getDefaultCount() const { return m_defaults.size(); }

private:
	std::string m_className;
	TickSettings m_tickSettings;
	std::vector<std::unique_ptr<IComponentDefault>> m_defaults;
};

// ═══════════════════════════════════════════════════════════════════════
// CDORegistry — singleton that manages CDOs for all registered classes
// ═══════════════════════════════════════════════════════════════════════

/// CDORegistry — singleton that manages CDOs for all registered Actor
/// classes.  Call buildAll() once after REGISTER_ACTOR macros have
/// executed (e.g. in World::beginPlay()) to construct every CDO.
class CDORegistry
{
public:
	static CDORegistry& Instance();

	/// Build CDOs for all classes in ActorRegistry.
	/// Temporarily spawns each Actor type to capture default state.
	void buildAll();

	/// Get the CDO for a class name (nullptr if not built).
	const ClassDefaultObject* getCDO(const std::string& className) const;

	/// Get mutable CDO (for editing defaults).
	ClassDefaultObject* getMutableCDO(const std::string& className);

	/// Check if a CDO exists for a class.
	bool hasCDO(const std::string& className) const;

	/// Clear all CDOs (e.g. on shutdown or hot-reload).
	void clear();

	/// Check whether CDOs have been built.
	bool isBuilt() const { return m_built; }

	/// Get all CDOs.
	const std::unordered_map<std::string, ClassDefaultObject>& all() const { return m_cdos; }

private:
	CDORegistry() = default;
	void buildCDO(const std::string& className);

	std::unordered_map<std::string, ClassDefaultObject> m_cdos;
	bool m_built{ false };
};
