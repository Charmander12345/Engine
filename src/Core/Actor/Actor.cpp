#include "Actor.h"
#include "ActorComponent.h"
#include "World.h"
#include "../ECS/ECS.h"

Actor::~Actor()
{
	// Components are destroyed via unique_ptr automatically.
	// ECS entity cleanup is handled by World::destroyActor().
}

// ── Identity ──────────────────────────────────────────────────────────

void Actor::setName(const std::string& name)
{
	m_nameID = NameID::fromString(name);
	if (m_entity != 0)
	{
		auto& ecs = ECS::Manager();
		if (!ecs.hasComponent<ECS::NameComponent>(m_entity))
			ecs.addComponent<ECS::NameComponent>(m_entity);
		if (auto* nc = ecs.getComponent<ECS::NameComponent>(m_entity))
			nc->displayName = name;
	}
}

// ── Transform ─────────────────────────────────────────────────────────

void Actor::getPosition(float outPos[3]) const
{
	if (const auto* tc = ECS::Manager().getComponent<ECS::TransformComponent>(m_entity))
	{
		outPos[0] = tc->position[0];
		outPos[1] = tc->position[1];
		outPos[2] = tc->position[2];
	}
}

void Actor::setPosition(float x, float y, float z)
{
	if (auto* tc = ECS::Manager().getComponent<ECS::TransformComponent>(m_entity))
	{
		tc->position[0] = x;
		tc->position[1] = y;
		tc->position[2] = z;
		ECS::Manager().markTransformDirty(m_entity);
	}
}

void Actor::setPosition(const float pos[3])
{
	setPosition(pos[0], pos[1], pos[2]);
}

void Actor::getRotation(float outRot[3]) const
{
	if (const auto* tc = ECS::Manager().getComponent<ECS::TransformComponent>(m_entity))
	{
		outRot[0] = tc->rotation[0];
		outRot[1] = tc->rotation[1];
		outRot[2] = tc->rotation[2];
	}
}

void Actor::setRotation(float x, float y, float z)
{
	if (auto* tc = ECS::Manager().getComponent<ECS::TransformComponent>(m_entity))
	{
		tc->rotation[0] = x;
		tc->rotation[1] = y;
		tc->rotation[2] = z;
		ECS::Manager().markTransformDirty(m_entity);
	}
}

void Actor::setRotation(const float rot[3])
{
	setRotation(rot[0], rot[1], rot[2]);
}

void Actor::getScale(float outScale[3]) const
{
	if (const auto* tc = ECS::Manager().getComponent<ECS::TransformComponent>(m_entity))
	{
		outScale[0] = tc->scale[0];
		outScale[1] = tc->scale[1];
		outScale[2] = tc->scale[2];
	}
}

void Actor::setScale(float x, float y, float z)
{
	if (auto* tc = ECS::Manager().getComponent<ECS::TransformComponent>(m_entity))
	{
		tc->scale[0] = x;
		tc->scale[1] = y;
		tc->scale[2] = z;
		ECS::Manager().markTransformDirty(m_entity);
	}
}

void Actor::setScale(const float scale[3])
{
	setScale(scale[0], scale[1], scale[2]);
}

void Actor::translate(float dx, float dy, float dz)
{
	if (auto* tc = ECS::Manager().getComponent<ECS::TransformComponent>(m_entity))
	{
		tc->position[0] += dx;
		tc->position[1] += dy;
		tc->position[2] += dz;
		ECS::Manager().markTransformDirty(m_entity);
	}
}

void Actor::rotate(float dx, float dy, float dz)
{
	if (auto* tc = ECS::Manager().getComponent<ECS::TransformComponent>(m_entity))
	{
		tc->rotation[0] += dx;
		tc->rotation[1] += dy;
		tc->rotation[2] += dz;
		ECS::Manager().markTransformDirty(m_entity);
	}
}

// ── Local Transform ───────────────────────────────────────────────────

void Actor::getLocalPosition(float outPos[3]) const
{
	if (const auto* tc = ECS::Manager().getComponent<ECS::TransformComponent>(m_entity))
	{
		outPos[0] = tc->localPosition[0];
		outPos[1] = tc->localPosition[1];
		outPos[2] = tc->localPosition[2];
	}
}

void Actor::setLocalPosition(float x, float y, float z)
{
	if (auto* tc = ECS::Manager().getComponent<ECS::TransformComponent>(m_entity))
	{
		tc->localPosition[0] = x;
		tc->localPosition[1] = y;
		tc->localPosition[2] = z;
		tc->dirty = true;
	}
}

void Actor::getLocalRotation(float outRot[3]) const
{
	if (const auto* tc = ECS::Manager().getComponent<ECS::TransformComponent>(m_entity))
	{
		outRot[0] = tc->localRotation[0];
		outRot[1] = tc->localRotation[1];
		outRot[2] = tc->localRotation[2];
	}
}

void Actor::setLocalRotation(float x, float y, float z)
{
	if (auto* tc = ECS::Manager().getComponent<ECS::TransformComponent>(m_entity))
	{
		tc->localRotation[0] = x;
		tc->localRotation[1] = y;
		tc->localRotation[2] = z;
		tc->dirty = true;
	}
}

void Actor::getLocalScale(float outScale[3]) const
{
	if (const auto* tc = ECS::Manager().getComponent<ECS::TransformComponent>(m_entity))
	{
		outScale[0] = tc->localScale[0];
		outScale[1] = tc->localScale[1];
		outScale[2] = tc->localScale[2];
	}
}

void Actor::setLocalScale(float x, float y, float z)
{
	if (auto* tc = ECS::Manager().getComponent<ECS::TransformComponent>(m_entity))
	{
		tc->localScale[0] = x;
		tc->localScale[1] = y;
		tc->localScale[2] = z;
		tc->dirty = true;
	}
}

// ── Hierarchy ─────────────────────────────────────────────────────────

void Actor::attachToActor(Actor* parent)
{
	if (!parent || parent == this || parent == m_parent)
		return;

	detachFromParent();

	m_parent = parent;
	parent->m_children.push_back(this);

	// Delegate to ECS parenting
	ECS::Manager().setParent(m_entity, parent->m_entity);
}

void Actor::detachFromParent()
{
	if (!m_parent)
		return;

	auto& siblings = m_parent->m_children;
	for (auto it = siblings.begin(); it != siblings.end(); ++it)
	{
		if (*it == this)
		{
			siblings.erase(it);
			break;
		}
	}
	m_parent = nullptr;

	ECS::Manager().removeParent(m_entity);
}

// ── Destruction ───────────────────────────────────────────────────────

void Actor::destroy()
{
	m_pendingDestroy = true;

	// Fire the destroyed delegate
	if (m_onDestroyed)
		m_onDestroyed(this);
}

// ── Internal lifecycle (called by World) ──────────────────────────────

void Actor::internalInitialize(ECS::Entity entity, World* world)
{
	m_entity = entity;
	m_world = world;

	// Allocate a generation-safe handle for weak references
	m_handle = ActorHandle(ObjectSlotArray::Instance().allocate(this));

	// Ensure the entity has at least a transform
	auto& ecs = ECS::Manager();
	if (!ecs.hasComponent<ECS::TransformComponent>(entity))
		ecs.addComponent<ECS::TransformComponent>(entity);
}

void Actor::internalBeginPlay()
{
	if (m_beginPlayCalled)
		return;
	m_beginPlayCalled = true;

	preInitializeComponents();

	// Initialize all components
	for (auto& comp : m_components)
		comp->beginPlay();

	postInitializeComponents();

	beginPlay();
}

void Actor::internalTick(float deltaTime)
{
	if (m_pendingDestroy || m_deferredSpawn)
		return;

	if (!m_tickSettings.bCanEverTick)
		return;

	// Tick interval throttle
	if (m_tickSettings.tickInterval > 0.0f)
	{
		m_tickAccumulator += deltaTime;
		if (m_tickAccumulator < m_tickSettings.tickInterval)
			return;
		m_tickAccumulator -= m_tickSettings.tickInterval;
	}

	// Tick components first
	for (auto& comp : m_components)
	{
		if (comp->isEnabled())
			comp->tickComponent(deltaTime);
	}

	tick(deltaTime);
}

void Actor::finishSpawning()
{
	if (!m_deferredSpawn)
		return;
	m_deferredSpawn = false;
	internalBeginPlay();
}

void Actor::addTickPrerequisite(Actor* other)
{
	if (!other || other == this)
		return;
	// Avoid duplicates
	for (auto* existing : m_tickPrerequisites)
	{
		if (existing == other)
			return;
	}
	m_tickPrerequisites.push_back(other);
}

void Actor::removeTickPrerequisite(Actor* other)
{
	for (auto it = m_tickPrerequisites.begin(); it != m_tickPrerequisites.end(); ++it)
	{
		if (*it == other)
		{
			m_tickPrerequisites.erase(it);
			return;
		}
	}
}

void Actor::internalEndPlay(EEndPlayReason reason)
{
	endPlay(reason);

	// Shut down components in reverse order
	for (auto it = m_components.rbegin(); it != m_components.rend(); ++it)
		(*it)->endPlay();

	// Release the object handle so weak references become invalid
	if (!m_handle.isNull())
	{
		ObjectSlotArray::Instance().release(m_handle.handle);
		m_handle = ActorHandle{};
	}
}
