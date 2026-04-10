#include "World.h"
#include "ActorRegistry.h"
#include "BuiltinComponents.h"
#include "../ECS/ECS.h"
#include <algorithm>

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
	actor->internalEndPlay();

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
		(*it)->internalEndPlay();

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

	for (auto& actor : m_actors)
	{
		if (!actor->isPendingDestroy())
			actor->internalTick(deltaTime);
	}

	m_ticking = false;

	processPendingDestroys();
}

Actor* World::findActorByName(const std::string& name) const
{
	for (auto& actor : m_actors)
	{
		if (actor->getName() == name)
			return actor.get();
	}
	return nullptr;
}

std::vector<Actor*> World::findActorsByTag(const std::string& tag) const
{
	std::vector<Actor*> result;
	for (auto& actor : m_actors)
	{
		if (actor->getTag() == tag)
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
			actor->internalEndPlay();

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
