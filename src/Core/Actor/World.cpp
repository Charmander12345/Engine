#include "World.h"
#include "ActorRegistry.h"
#include "CDO.h"
#include "BuiltinComponents.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/GameState.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "../ECS/ECS.h"
#include <algorithm>
#include <unordered_set>
#include <queue>

Actor* World::spawnActorByClass(const std::string& className, float x, float y, float z)
{
	Actor* actor = ActorRegistry::Instance().createActor(className);
	if (!actor)
		return nullptr;

	ECS::Entity entity = ECS::Manager().createEntity();
	if (entity == 0)
	{
		delete actor;
		return nullptr;
	}

	actor->internalInitialize(entity, this);
	actor->setPosition(x, y, z);
	actor->internalBeginPlay();

	m_entityToActor[entity] = actor;
	m_actors.push_back(std::unique_ptr<Actor>(actor));
	return actor;
}

Actor* World::spawnActorFromAsset(const ActorAssetData& assetData)
{
	// Determine which actor class to spawn
	const std::string& className = assetData.actorClass;
	Actor* actor = nullptr;

	if (!className.empty() && className != "Actor" && ActorRegistry::Instance().hasClass(className))
		actor = ActorRegistry::Instance().createActor(className);
	else
		actor = new Actor();

	if (!actor)
		return nullptr;

	ECS::Entity entity = ECS::Manager().createEntity();
	if (entity == 0)
	{
		delete actor;
		return nullptr;
	}

	actor->internalInitialize(entity, this);

	// Set name and tag
	if (!assetData.name.empty())
		actor->setName(assetData.name);
	if (!assetData.tag.empty())
		actor->setTag(assetData.tag);

	actor->internalBeginPlay();

	// Set root mesh + material (after beginPlay so StaticMeshComponent exists)
	if (!assetData.meshPath.empty())
	{
		auto* meshComp = actor->getComponent<StaticMeshComponent>();
		if (meshComp)
		{
			meshComp->setMesh(assetData.meshPath);
			if (!assetData.materialPath.empty())
				meshComp->setMaterial(assetData.materialPath);
		}
	}

	m_entityToActor[entity] = actor;
	m_actors.push_back(std::unique_ptr<Actor>(actor));

	// Recursively spawn child actors
	auto spawnChildren = [&](auto& self, Actor* parent, const std::vector<ChildActorEntry>& children) -> void
	{
		for (const auto& child : children)
		{
			Actor* childActor = nullptr;
			if (!child.actorClass.empty() && child.actorClass != "Actor" && ActorRegistry::Instance().hasClass(child.actorClass))
				childActor = ActorRegistry::Instance().createActor(child.actorClass);
			else
				childActor = new Actor();

			if (!childActor)
				continue;

			ECS::Entity childEntity = ECS::Manager().createEntity();
			if (childEntity == 0)
			{
				delete childActor;
				continue;
			}

			childActor->internalInitialize(childEntity, this);
			if (!child.name.empty())
				childActor->setName(child.name);

			childActor->internalBeginPlay();

			// Set child mesh + material (after beginPlay)
			if (!child.meshPath.empty())
			{
				auto* meshComp = childActor->getComponent<StaticMeshComponent>();
				if (meshComp)
				{
					meshComp->setMesh(child.meshPath);
					if (!child.materialPath.empty())
						meshComp->setMaterial(child.materialPath);
				}
			}

			// Set local transform
			childActor->setLocalPosition(child.position[0], child.position[1], child.position[2]);
			childActor->setLocalRotation(child.rotation[0], child.rotation[1], child.rotation[2]);
			childActor->setLocalScale(child.scale[0], child.scale[1], child.scale[2]);

			// Attach to parent
			childActor->attachToActor(parent);

			m_entityToActor[childEntity] = childActor;
			m_actors.push_back(std::unique_ptr<Actor>(childActor));

			// Recurse into nested children
			if (!child.children.empty())
				self(self, childActor, child.children);
		}
	};

	spawnChildren(spawnChildren, actor, assetData.childActors);

	// Attach embedded script if enabled
	if (assetData.scriptEnabled && !assetData.scriptClassName.empty() && m_scriptAttacher)
	{
		m_scriptAttacher(entity, assetData.scriptClassName);
	}

	return actor;
}

void World::destroyActor(Actor* actor)
{
	if (!actor)
		return;

	// Detach children first
	auto childrenCopy = actor->getChildActors();
	for (auto* child : childrenCopy)
		child->detachFromParent();

	actor->detachFromParent();
	actor->internalEndPlay(EEndPlayReason::Destroyed);

	// Remove from entity lookup
	m_entityToActor.erase(actor->getEntity());

	// Remove the ECS entity
	ECS::Manager().removeEntity(actor->getEntity());

	// Remove from actor list
	for (auto it = m_actors.begin(); it != m_actors.end(); ++it)
	{
		if (it->get() == actor)
		{
			m_actors.erase(it);
			break;
		}
	}
}

void World::destroyAllActors()
{
	// EndPlay in reverse order
	for (auto it = m_actors.rbegin(); it != m_actors.rend(); ++it)
		(*it)->internalEndPlay(EEndPlayReason::LevelUnloaded);

	// Clean up ECS entities
	for (auto& actor : m_actors)
	{
		ECS::Manager().removeEntity(actor->getEntity());
	}

	m_actors.clear();
	m_entityToActor.clear();
}

void World::tick(float deltaTime)
{
	m_ticking = true;

	tickGroupSorted(ETickGroup::PrePhysics, deltaTime);
	tickGroupSorted(ETickGroup::DuringPhysics, deltaTime);
	tickGroupSorted(ETickGroup::PostPhysics, deltaTime);
	tickGroupSorted(ETickGroup::PostUpdateWork, deltaTime);

	m_ticking = false;

	processPendingDestroys();
}

void World::tickGroup(ETickGroup group, float deltaTime)
{
	m_ticking = true;
	tickGroupSorted(group, deltaTime);
}

void World::tickGroupSorted(ETickGroup group, float deltaTime)
{
	// Gather actors in this tick group
	std::vector<Actor*> groupActors;
	for (auto& actor : m_actors)
	{
		if (!actor->isPendingDestroy() && actor->getTickSettings().tickGroup == group)
			groupActors.push_back(actor.get());
	}

	if (groupActors.empty())
		return;

	// Check if any actor in this group has tick prerequisites
	bool hasDependencies = false;
	for (auto* actor : groupActors)
	{
		if (!actor->getTickPrerequisites().empty())
		{
			hasDependencies = true;
			break;
		}
	}

	if (!hasDependencies)
	{
		// Fast path: no dependencies, tick in array order
		for (auto* actor : groupActors)
			actor->internalTick(deltaTime);
		return;
	}

	// Topological sort via Kahn's algorithm
	std::unordered_set<Actor*> groupSet(groupActors.begin(), groupActors.end());
	std::unordered_map<Actor*, int> inDegree;
	std::unordered_map<Actor*, std::vector<Actor*>> dependents;  // prerequisite → who depends on it

	for (auto* actor : groupActors)
		inDegree[actor] = 0;

	for (auto* actor : groupActors)
	{
		for (auto* prereq : actor->getTickPrerequisites())
		{
			if (groupSet.count(prereq))
			{
				dependents[prereq].push_back(actor);
				inDegree[actor]++;
			}
		}
	}

	std::queue<Actor*> ready;
	for (auto* actor : groupActors)
	{
		if (inDegree[actor] == 0)
			ready.push(actor);
	}

	std::vector<Actor*> sorted;
	sorted.reserve(groupActors.size());

	while (!ready.empty())
	{
		Actor* current = ready.front();
		ready.pop();
		sorted.push_back(current);

		for (auto* dep : dependents[current])
		{
			if (--inDegree[dep] == 0)
				ready.push(dep);
		}
	}

	// If cycle detected, remaining actors tick in array order
	if (sorted.size() < groupActors.size())
	{
		for (auto* actor : groupActors)
		{
			if (inDegree[actor] > 0)
				sorted.push_back(actor);
		}
	}

	for (auto* actor : sorted)
		actor->internalTick(deltaTime);
}

void World::flushDestroys()
{
	m_ticking = false;
	processPendingDestroys();
}

Actor* World::findActorByName(const std::string& name) const
{
	NameID id = NameID::find(name);
	if (id.isNone())
		return nullptr;
	return findActorByNameID(id);
}

Actor* World::findActorByNameID(NameID nameID) const
{
	for (auto& actor : m_actors)
	{
		if (actor->getNameID() == nameID)
			return actor.get();
	}
	return nullptr;
}

std::vector<Actor*> World::findActorsByTag(const std::string& tag) const
{
	NameID id = NameID::find(tag);
	if (id.isNone())
		return {};
	return findActorsByTagID(id);
}

std::vector<Actor*> World::findActorsByTagID(NameID tagID) const
{
	std::vector<Actor*> result;
	for (auto& actor : m_actors)
	{
		if (actor->getTagID() == tagID)
			result.push_back(actor.get());
	}
	return result;
}

Actor* World::findActorByEntity(ECS::Entity entity) const
{
	auto it = m_entityToActor.find(entity);
	if (it != m_entityToActor.end())
		return it->second;
	return nullptr;
}

void World::dispatchBeginOverlap(ECS::Entity entityA, ECS::Entity entityB)
{
	Actor* actorA = findActorByEntity(entityA);
	Actor* actorB = findActorByEntity(entityB);
	if (actorA && actorB)
	{
		actorA->onBeginOverlap(actorB);
		actorB->onBeginOverlap(actorA);
	}
}

void World::dispatchEndOverlap(ECS::Entity entityA, ECS::Entity entityB)
{
	Actor* actorA = findActorByEntity(entityA);
	Actor* actorB = findActorByEntity(entityB);
	if (actorA && actorB)
	{
		actorA->onEndOverlap(actorB);
		actorB->onEndOverlap(actorA);
	}
}

Actor* World::resolveHandle(ActorHandle handle) const
{
	return handle.get();
}

void World::beginPlay()
{
	if (m_hasBegunPlay)
		return;
	m_hasBegunPlay = true;

	// Build CDOs for all registered Actor classes (once)
	if (!CDORegistry::Instance().isBuilt())
		CDORegistry::Instance().buildAll();

	// Set up GameState if a GameMode was configured
	if (m_gameMode)
	{
		if (!m_gameState)
			m_gameState = spawnActor<GameState>(0, 0, 0);
		m_gameMode->m_gameState = m_gameState;
		m_gameState->m_hasBegunPlay = true;
		m_gameMode->onStartPlay();
	}
}

void World::processPendingDestroys()
{
	for (auto it = m_actors.begin(); it != m_actors.end(); )
	{
		if ((*it)->isPendingDestroy())
		{
			Actor* actor = it->get();
			auto childrenCopy = actor->getChildActors();
			for (auto* child : childrenCopy)
				child->detachFromParent();
			actor->detachFromParent();
			actor->internalEndPlay(EEndPlayReason::Destroyed);

			m_entityToActor.erase(actor->getEntity());
			ECS::Manager().removeEntity(actor->getEntity());
			it = m_actors.erase(it);
		}
		else
		{
			++it;
		}
	}
}
