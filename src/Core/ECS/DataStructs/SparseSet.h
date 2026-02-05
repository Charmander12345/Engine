# pragma once
#include <array>
#include <cassert>
#include <cstddef>

using Entity = unsigned int;

namespace ECS
{
template<typename T, size_t MaxEntities>
struct SparseSet
{
	SparseSet() { m_sparse.fill(npos); }

	void insert(Entity entity, const T& value = T{})
	{
		assert(entity < MaxEntities && "Entity exceeds SparseSet capacity");
		assert(m_size < MaxEntities && "SparseSet is full");
		assert(m_sparse[entity] == npos && "Entity already exists in SparseSet");

		const size_t index = m_size;
		m_sparse[entity] = index;
		m_dense[index] = entity;
		m_data[index] = value;
		++m_size;
	}

	void clear()
	{
		m_sparse.fill(npos);
		m_size = 0;
	}

	void erase(Entity entity)
	{
		assert(entity < MaxEntities && m_sparse[entity] != npos && "Entity does not exist in SparseSet");
		assert(m_size > 0 && "Cannot erase from empty SparseSet");

		const size_t index = m_sparse[entity];
		const size_t lastIndex = m_size - 1;
		const Entity lastEntity = m_dense[lastIndex];

		// swap-remove
		m_dense[index] = lastEntity;
		m_data[index] = std::move(m_data[lastIndex]);
		m_sparse[lastEntity] = index;
		--m_size;
		m_sparse[entity] = npos;
	}

	bool contains(Entity entity) const
	{
		return entity < MaxEntities && m_sparse[entity] != npos;
	}

	T& get(Entity entity)
	{
		assert(contains(entity) && "Entity does not exist in SparseSet");
		return m_data[m_sparse[entity]];
	}

	const T& get(Entity entity) const
	{
		assert(contains(entity) && "Entity does not exist in SparseSet");
		return m_data[m_sparse[entity]];
	}

	const std::array<Entity, MaxEntities>& dense() const { return m_dense; }
	const std::array<T, MaxEntities>& data() const { return m_data; }

	size_t size() const { return m_size; }
	constexpr size_t capacity() const { return MaxEntities; }

private:
	static constexpr size_t npos = static_cast<size_t>(-1);

	std::array<size_t, MaxEntities> m_sparse{};
	std::array<Entity, MaxEntities> m_dense{};
	std::array<T, MaxEntities> m_data{};
	size_t m_size{ 0 };
};
} // namespace ECS