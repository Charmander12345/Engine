#pragma once

#include "../ECS/ECS.h"
#include "../ECS/Components.h"
#include "../NameID.h"
#include "../ObjectHandle.h"
#include <string>
#include <vector>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <functional>

class ActorComponent;
class World;
class Actor;
class CDORegistry;

/// Tick group determines when an Actor ticks relative to physics.
enum class ETickGroup
{
	PrePhysics,
	DuringPhysics,
	PostPhysics,
	PostUpdateWork
};

/// Per-actor tick settings (stack-allocated).
struct TickSettings
{
	bool       bCanEverTick{ true };
	ETickGroup tickGroup{ ETickGroup::PostPhysics };
	float      tickInterval{ 0.0f };  // 0 = every frame
};

/// Reason why EndPlay was called on an actor.
enum class EEndPlayReason
{
	Destroyed,       // Actor was explicitly destroyed
	LevelUnloaded,   // The level containing this actor was unloaded
	Quit,            // Application is shutting down
	RemovedFromWorld // Actor was removed from the world
};

/// Alias for the Actor weak handle type.
using ActorHandle = TObjectHandle<Actor>;

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
	/// Called before any component's beginPlay() – set up dependencies here.
	virtual void preInitializeComponents() {}

	/// Called after all component beginPlay() calls, before actor beginPlay().
	virtual void postInitializeComponents() {}

	/// Called once after the actor is spawned and all default components
	/// are attached.  Use this to set up initial state.
	virtual void beginPlay() {}

	/// Called every frame with the frame delta time.
	virtual void tick(float deltaTime) { (void)deltaTime; }

	/// Called when this actor is about to be destroyed.
	virtual void endPlay() {}

	/// Called with reason when this actor is about to be destroyed (preferred override).
	virtual void endPlay(EEndPlayReason reason) { (void)reason; endPlay(); }

	/// Called when another actor starts overlapping a sensor on this actor.
	virtual void onBeginOverlap(Actor* other) { (void)other; }

	/// Called when another actor stops overlapping a sensor on this actor.
	virtual void onEndOverlap(Actor* other) { (void)other; }

	// ── Identity ───────────────────────────────────────────────────
	/// Get the actor's display name as a string.
	const std::string& getName() const { return m_nameID.toString(); }
	void setName(const std::string& name);

	/// Get the interned NameID for O(1) comparisons.
	NameID getNameID() const { return m_nameID; }

	const std::string& getTag() const { return m_tagID.toString(); }
	void setTag(const std::string& tag) { m_tagID = NameID::fromString(tag); }

	/// Get the interned tag NameID.
	NameID getTagID() const { return m_tagID; }

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

	// ── Object handle (safe weak reference) ──────────────────────
	/// Get a weak handle to this actor.  Survives actor destruction
	/// detection (handle.isValid() returns false after destroy).
	ActorHandle getHandle() const { return m_handle; }

	// ── Tick dependencies ────────────────────────────────────────
	/// Declare that this actor must tick after 'other' within the same tick group.
	void addTickPrerequisite(Actor* other);

	/// Remove a previously added tick dependency.
	void removeTickPrerequisite(Actor* other);

	/// Get the list of actors that must tick before this one.
	const std::vector<Actor*>& getTickPrerequisites() const { return m_tickPrerequisites; }

	// ── Destruction event ────────────────────────────────────────
	using DestroyedDelegate = std::function<void(Actor*)>;
	/// Register a callback that fires when this actor is destroyed.
	void onDestroyed(DestroyedDelegate callback) { m_onDestroyed = std::move(callback); }

	// ── Tick settings ─────────────────────────────────────────────
	const TickSettings& getTickSettings() const { return m_tickSettings; }
	void setTickGroup(ETickGroup group) { m_tickSettings.tickGroup = group; }
	void setCanEverTick(bool canTick) { m_tickSettings.bCanEverTick = canTick; }
	void setTickInterval(float interval) { m_tickSettings.tickInterval = interval; }

	// ── Deferred spawning ─────────────────────────────────────────
	/// Returns true if this actor was spawned deferred and not yet finalized.
	bool isDeferredSpawn() const { return m_deferredSpawn; }

	/// Finalize a deferred spawn – calls internalBeginPlay().
	void finishSpawning();

	/// Is this actor pending destruction?
	bool isPendingDestroy() const { return m_pendingDestroy; }

	/// Marks this actor for destruction at the end of the current frame.
	void destroy();

private:
	friend class World;
	friend class CDORegistry;

	void internalInitialize(ECS::Entity entity, World* world);
	void internalBeginPlay();
	void internalTick(float deltaTime);
	void internalEndPlay(EEndPlayReason reason = EEndPlayReason::Destroyed);

	ECS::Entity m_entity{ 0 };
	World* m_world{ nullptr };
	NameID m_nameID;
	NameID m_tagID;
	ActorHandle m_handle;

	Actor* m_parent{ nullptr };
	std::vector<Actor*> m_children;

	std::vector<std::unique_ptr<ActorComponent>> m_components;
	std::unordered_map<std::type_index, ActorComponent*> m_componentLookup;

	TickSettings m_tickSettings;
	float m_tickAccumulator{ 0.0f };

	std::vector<Actor*> m_tickPrerequisites;
	DestroyedDelegate m_onDestroyed;

	bool m_beginPlayCalled{ false };
	bool m_pendingDestroy{ false };
	bool m_deferredSpawn{ false };
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

	// If the actor is already playing, late-added components get their beginPlay immediately
	if (m_beginPlayCalled)
		raw->beginPlay();

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
