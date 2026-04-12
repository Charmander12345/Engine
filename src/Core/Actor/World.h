#pragma once

#include "Actor.h"
#include "ActorAssetData.h"
#include "ActorRegistry.h"
#include "../ECS/ECS.h"
#include "../NameID.h"
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <functional>

class GameMode;
class GameState;
class PlayerController;
class Pawn;

/// The World manages the lifecycle of all Actors.
/// It acts as a factory (SpawnActor), dispatches ticks, and handles
/// deferred destruction – similar to Unreal Engine's UWorld.
class World
{
public:
	World() = default;
	~World() { destroyAllActors(); }

	// ── Spawning ──────────────────────────────────────────────────
	/// Spawn an actor of type T at the given world position.
	template<typename T>
	T* spawnActor(float x = 0.0f, float y = 0.0f, float z = 0.0f);

	/// Spawn an actor by registered class name (for data-driven workflows).
	Actor* spawnActorByClass(const std::string& className,
		float x = 0.0f, float y = 0.0f, float z = 0.0f);

	/// Spawn an actor from an ActorAssetData definition.
	/// Applies the asset's default transform, components, and script.
	Actor* spawnActorFromAsset(const ActorAssetData& assetData);

	/// Spawn an actor deferred – the actor is created and initialized but
	/// beginPlay() is NOT called.  The caller must call actor->finishSpawning()
	/// after configuring properties.
	template<typename T>
	T* spawnActorDeferred(float x = 0.0f, float y = 0.0f, float z = 0.0f);

	// ── Destruction ───────────────────────────────────────────────
	/// Immediately destroy an actor and its ECS entity.
	void destroyActor(Actor* actor);

	/// Destroy all actors.
	void destroyAllActors();

	// ── Per-frame update ──────────────────────────────────────────
	/// Call once per frame.  Ticks all actors, then processes deferred
	/// destroy requests.  (Legacy – ticks all groups sequentially.)
	void tick(float deltaTime);

	/// Tick only actors in the given group.
	void tickGroup(ETickGroup group, float deltaTime);

	/// Process pending actor destructions (call after all groups ticked).
	void flushDestroys();

	// ── Queries ───────────────────────────────────────────────────
	/// Find the first actor with the given name (or nullptr).
	Actor* findActorByName(const std::string& name) const;

	/// Find the first actor by interned NameID (O(1) per comparison).
	Actor* findActorByNameID(NameID nameID) const;

	/// Find all actors with the given tag.
	std::vector<Actor*> findActorsByTag(const std::string& tag) const;

	/// Find all actors by interned tag NameID.
	std::vector<Actor*> findActorsByTagID(NameID tagID) const;

	/// Find the actor that owns the given ECS entity (or nullptr).
	Actor* findActorByEntity(ECS::Entity entity) const;

	/// Resolve an ActorHandle to an Actor* (nullptr if stale).
	Actor* resolveHandle(ActorHandle handle) const;

	/// Get all actors of type T.
	template<typename T>
	std::vector<T*> getActorsOfClass() const;

	/// Get every living actor.
	const std::vector<std::unique_ptr<Actor>>& getAllActors() const { return m_actors; }

	/// Total number of living actors.
	size_t getActorCount() const { return m_actors.size(); }

	// ── Game Framework ───────────────────────────────────────────────
	/// Set the GameMode type for this world.  Call before beginPlay().
	template<typename T>
	T* setGameMode();

	/// Get the current GameMode (or nullptr).
	GameMode* getGameMode() const { return m_gameMode; }

	/// Get the current GameState (or nullptr).
	GameState* getGameState() const { return m_gameState; }

	/// Initialize the world – spawns GameMode/GameState and calls beginPlay
	/// on all existing actors.  Called once when gameplay starts.
	void beginPlay();

	// ── Overlap dispatch (called by physics integration) ──────────
	void dispatchBeginOverlap(ECS::Entity entityA, ECS::Entity entityB);
	void dispatchEndOverlap(ECS::Entity entityA, ECS::Entity entityB);

	// ── Script attachment callback ──────────────────────────────────
	/// Set an external callback that attaches a native script to an entity.
	/// Used by spawnActorFromAsset to attach embedded scripts without
	/// linking directly against NativeScripting.
	using ScriptAttacher = std::function<void(ECS::Entity, const std::string&)>;
	void setScriptAttacher(ScriptAttacher fn) { m_scriptAttacher = std::move(fn); }

private:
	void processPendingDestroys();
	void tickGroupSorted(ETickGroup group, float deltaTime);

	std::vector<std::unique_ptr<Actor>> m_actors;
	std::unordered_map<ECS::Entity, Actor*> m_entityToActor;
	ScriptAttacher m_scriptAttacher;
	bool m_ticking{ false };
	bool m_hasBegunPlay{ false };

	// Game Framework
	GameMode*  m_gameMode{ nullptr };
	GameState* m_gameState{ nullptr };
};

// ── Template implementations ──────────────────────────────────────────

template<typename T>
T* World::spawnActor(float x, float y, float z)
{
	static_assert(std::is_base_of_v<Actor, T>, "T must derive from Actor");

	ECS::Entity entity = ECS::Manager().createEntity();
	if (entity == 0)
		return nullptr;

	auto actor = std::make_unique<T>();
	T* raw = actor.get();
	raw->internalInitialize(entity, this);
	raw->setPosition(x, y, z);
	raw->internalBeginPlay();

	m_entityToActor[entity] = raw;
	m_actors.push_back(std::move(actor));
	return raw;
}

template<typename T>
T* World::spawnActorDeferred(float x, float y, float z)
{
	static_assert(std::is_base_of_v<Actor, T>, "T must derive from Actor");

	ECS::Entity entity = ECS::Manager().createEntity();
	if (entity == 0)
		return nullptr;

	auto actor = std::make_unique<T>();
	T* raw = actor.get();
	raw->m_deferredSpawn = true;
	raw->internalInitialize(entity, this);
	raw->setPosition(x, y, z);

	m_entityToActor[entity] = raw;
	m_actors.push_back(std::move(actor));
	return raw;
}

template<typename T>
std::vector<T*> World::getActorsOfClass() const
{
	std::vector<T*> result;
	for (auto& actor : m_actors)
	{
		if (T* casted = dynamic_cast<T*>(actor.get()))
			result.push_back(casted);
	}
	return result;
}

template<typename T>
T* World::setGameMode()
{
	static_assert(std::is_base_of_v<GameMode, T>, "T must derive from GameMode");
	return static_cast<T*>(spawnActor<T>(0, 0, 0));
}
