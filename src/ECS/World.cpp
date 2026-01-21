#include "ECS/World.h"

namespace ecs
{
	World::World()
	{
		// Reserve slot 0 as invalid so a default EntityHandle can be safely treated as invalid.
		m_generations.resize(1);
		m_alive.resize(1);
	}

	EntityHandle World::createEntity()
	{
		EntityId id = kInvalidEntityId;
		if (!m_freeList.empty())
		{
			id = m_freeList.back();
			m_freeList.pop_back();
		}
		else
		{
			id = static_cast<EntityId>(m_generations.size());
			m_generations.push_back(0);
			m_alive.push_back(false);
		}

		m_alive[id] = true;
		++m_aliveCount;
		return EntityHandle{ id, m_generations[id] };
	}

	EntityHandle World::createEntityWithId(std::uint64_t guid)
	{
		auto e = createEntity();
		emplaceId(e, IdComponent{ guid });
		return e;
	}

	bool World::destroyEntity(EntityHandle e)
	{
		if (!isAlive(e))
		{
			return false;
		}

		// Remove components first.
		m_transforms.remove(e.id);
		m_names.remove(e.id);
		m_renders.remove(e.id);
		m_groups.remove(e.id);
		m_ids.remove(e.id);

		m_alive[e.id] = false;
		++m_generations[e.id];
		m_freeList.push_back(e.id);
		--m_aliveCount;
		return true;
	}

	TransformComponent& World::emplaceTransform(EntityHandle e, const TransformComponent& t)
	{
		return m_transforms.emplace(e.id, t);
	}

	bool World::removeTransform(EntityHandle e)
	{
		return m_transforms.remove(e.id);
	}

	bool World::hasTransform(EntityHandle e) const
	{
		return m_transforms.has(e.id);
	}

	TransformComponent* World::tryGetTransform(EntityHandle e)
	{
		return m_transforms.tryGet(e.id);
	}

	const TransformComponent* World::tryGetTransform(EntityHandle e) const
	{
		return m_transforms.tryGet(e.id);
	}

	SparseSet<TransformComponent>& World::transforms()
	{
		return m_transforms;
	}

	const SparseSet<TransformComponent>& World::transforms() const
	{
		return m_transforms;
	}

	NameComponent& World::emplaceName(EntityHandle e, const NameComponent& c)
	{
		return m_names.emplace(e.id, c);
	}

	bool World::removeName(EntityHandle e)
	{
		return m_names.remove(e.id);
	}

	bool World::hasName(EntityHandle e) const
	{
		return m_names.has(e.id);
	}

	NameComponent* World::tryGetName(EntityHandle e)
	{
		return m_names.tryGet(e.id);
	}

	const NameComponent* World::tryGetName(EntityHandle e) const
	{
		return m_names.tryGet(e.id);
	}

	SparseSet<NameComponent>& World::names()
	{
		return m_names;
	}

	const SparseSet<NameComponent>& World::names() const
	{
		return m_names;
	}

	RenderComponent& World::emplaceRender(EntityHandle e, const RenderComponent& c)
	{
		return m_renders.emplace(e.id, c);
	}

	bool World::removeRender(EntityHandle e)
	{
		return m_renders.remove(e.id);
	}

	bool World::hasRender(EntityHandle e) const
	{
		return m_renders.has(e.id);
	}

	RenderComponent* World::tryGetRender(EntityHandle e)
	{
		return m_renders.tryGet(e.id);
	}

	const RenderComponent* World::tryGetRender(EntityHandle e) const
	{
		return m_renders.tryGet(e.id);
	}

	SparseSet<RenderComponent>& World::renders()
	{
		return m_renders;
	}

	const SparseSet<RenderComponent>& World::renders() const
	{
		return m_renders;
	}

	GroupComponent& World::emplaceGroup(EntityHandle e, const GroupComponent& c)
	{
		return m_groups.emplace(e.id, c);
	}

	bool World::removeGroup(EntityHandle e)
	{
		return m_groups.remove(e.id);
	}

	bool World::hasGroup(EntityHandle e) const
	{
		return m_groups.has(e.id);
	}

	GroupComponent* World::tryGetGroup(EntityHandle e)
	{
		return m_groups.tryGet(e.id);
	}

	const GroupComponent* World::tryGetGroup(EntityHandle e) const
	{
		return m_groups.tryGet(e.id);
	}

	SparseSet<GroupComponent>& World::groups()
	{
		return m_groups;
	}

	const SparseSet<GroupComponent>& World::groups() const
	{
		return m_groups;
	}

	IdComponent& World::emplaceId(EntityHandle e, const IdComponent& c)
	{
		return m_ids.emplace(e.id, c);
	}

	bool World::removeId(EntityHandle e)
	{
		return m_ids.remove(e.id);
	}

	bool World::hasId(EntityHandle e) const
	{
		return m_ids.has(e.id);
	}

	IdComponent* World::tryGetId(EntityHandle e)
	{
		return m_ids.tryGet(e.id);
	}

	const IdComponent* World::tryGetId(EntityHandle e) const
	{
		return m_ids.tryGet(e.id);
	}

	SparseSet<IdComponent>& World::ids()
	{
		return m_ids;
	}

	const SparseSet<IdComponent>& World::ids() const
	{
		return m_ids;
	}

	bool World::isAlive(EntityHandle e) const
	{
		if (!e.isValid())
		{
			return false;
		}
		if (e.id >= m_generations.size())
		{
			return false;
		}
		if (!m_alive[e.id])
		{
			return false;
		}
		return m_generations[e.id] == e.generation;
	}

	std::size_t World::aliveCount() const
	{
		return m_aliveCount;
	}
}
