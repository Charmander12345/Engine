#pragma once

#include "../ECS/ECS.h"
#include "../ECS/Components.h"
#include <string>
#include <vector>
#include <memory>
#include <typeindex>
#include <unordered_map>

class ActorComponent;
class World;

/// Base class for all Actors – the primary game-object abstraction.
/// An Actor wraps one ECS::Entity and owns a set of ActorComponents.
/// Game developers subclass Actor and override lifecycle methods
/// (similar to Unreal Engine's AActor).
class Actor
{
public:
	Actor() = default;
	virtual ~Actor();

	// ── Lifecycle (override in subclasses) ─────────────────────────
	/// Called once after the actor is spawned and all default components
	/// are attached.  Use this to set up initial state.
	virtual void beginPlay() {}

	/// Called every frame with the frame delta time.
	virtual void tick(float deltaTime) { (void)deltaTime; }

	/// Called when this actor is about to be destroyed.
	virtual void endPlay() {}

	/// Called when another actor starts overlapping a sensor on this actor.
	virtual void onBeginOverlap(Actor* other) { (void)other; }

	/// Called when another actor stops overlapping a sensor on this actor.
	virtual void onEndOverlap(Actor* other) { (void)other; }

	// ── Identity ───────────────────────────────────────────────────
	const std::string& getName() const { return m_name; }
	void setName(const std::string& name);

	const std::string& getTag() const { return m_tag; }
	void setTag(const std::string& tag) { m_tag = tag; }

	/// Unique runtime ID (same as the underlying ECS entity).
	ECS::Entity getEntity() const { return m_entity; }

	// ── Transform (convenience – delegates to ECS transform) ──────
	void getPosition(float outPos[3]) const;
	void setPosition(float x, float y, float z);
	void setPosition(const float pos[3]);

	void getRotation(float outRot[3]) const;
	void setRotation(float x, float y, float z);
	void setRotation(const float rot[3]);

	void getScale(float outScale[3]) const;
	void setScale(float x, float y, float z);
	void setScale(const float scale[3]);

	void translate(float dx, float dy, float dz);
	void rotate(float dx, float dy, float dz);

	// ── Local Transform ───────────────────────────────────────────
	void getLocalPosition(float outPos[3]) const;
	void setLocalPosition(float x, float y, float z);

	void getLocalRotation(float outRot[3]) const;
	void setLocalRotation(float x, float y, float z);

	void getLocalScale(float outScale[3]) const;
	void setLocalScale(float x, float y, float z);

	// ── Hierarchy (actor-level parenting) ─────────────────────────
	void attachToActor(Actor* parent);
	void detachFromParent();
	Actor* getParentActor() const { return m_parent; }
	const std::vector<Actor*>& getChildActors() const { return m_children; }

	// ── Component management ──────────────────────────────────────
	/// Add a component of type T.  Returns a non-owning pointer.
	template<typename T, typename... Args>
	T* addComponent(Args&&... args);

	/// Get the first component of type T (or nullptr).
	template<typename T>
	T* getComponent() const;

	/// Check whether a component of type T is attached.
	template<typename T>
	bool hasComponent() const;

	/// Remove the first component of type T.
	template<typename T>
	bool removeComponent();

	/// Get all attached components.
	const std::vector<std::unique_ptr<ActorComponent>>& getComponents() const { return m_components; }

	// ── World access ──────────────────────────────────────────────
	World* getWorld() const { return m_world; }

	/// Is this actor pending destruction?
	bool isPendingDestroy() const { return m_pendingDestroy; }

	/// Marks this actor for destruction at the end of the current frame.
	void destroy();

private:
	friend class World;

	void internalInitialize(ECS::Entity entity, World* world);
	void internalBeginPlay();
	void internalTick(float deltaTime);
	void internalEndPlay();

	ECS::Entity m_entity{ 0 };
	World* m_world{ nullptr };
	std::string m_name;
	std::string m_tag;

	Actor* m_parent{ nullptr };
	std::vector<Actor*> m_children;

	std::vector<std::unique_ptr<ActorComponent>> m_components;
	std::unordered_map<std::type_index, ActorComponent*> m_componentLookup;

	bool m_beginPlayCalled{ false };
	bool m_pendingDestroy{ false };
};

// ── Template implementations ──────────────────────────────────────────

template<typename T, typename... Args>
T* Actor::addComponent(Args&&... args)
{
	static_assert(std::is_base_of_v<ActorComponent, T>, "T must derive from ActorComponent");
	auto comp = std::make_unique<T>(std::forward<Args>(args)...);
	T* raw = comp.get();
	raw->m_owner = this;
	raw->onRegister();
	m_componentLookup[std::type_index(typeid(T))] = raw;
	m_components.push_back(std::move(comp));
	return raw;
}

template<typename T>
T* Actor::getComponent() const
{
	auto it = m_componentLookup.find(std::type_index(typeid(T)));
	if (it != m_componentLookup.end())
		return static_cast<T*>(it->second);
	return nullptr;
}

template<typename T>
bool Actor::hasComponent() const
{
	return m_componentLookup.find(std::type_index(typeid(T))) != m_componentLookup.end();
}

template<typename T>
bool Actor::removeComponent()
{
	auto lookupIt = m_componentLookup.find(std::type_index(typeid(T)));
	if (lookupIt == m_componentLookup.end())
		return false;

	ActorComponent* target = lookupIt->second;
	target->onUnregister();
	m_componentLookup.erase(lookupIt);

	for (auto it = m_components.begin(); it != m_components.end(); ++it)
	{
		if (it->get() == target)
		{
			m_components.erase(it);
			return true;
		}
	}
	return false;
}
