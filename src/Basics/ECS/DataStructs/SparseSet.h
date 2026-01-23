# pragma once
#include <vector>
#include <cassert>
#include <cstddef>

using Entity = unsigned int;

namespace ECS
{
template<typename T>
struct SparseSet
{
	SparseSet(size_t capacity = 1024)
		: m_sparse(capacity, npos)
	{
	}

	void insert(Entity entity, const T& value = T{})
	{
		ensureSparseSize(entity);

		assert(m_sparse[entity] == npos && "Entity already exists in SparseSet");

		m_sparse[entity] = m_dense.size();
		m_dense.push_back(entity);
		m_data.push_back(value);
	}

	void erase(Entity entity)
	{
		assert(entity < m_sparse.size() && m_sparse[entity] != npos && "Entity does not exist in SparseSet");

		const size_t index = m_sparse[entity];
		const size_t lastIndex = m_dense.size() - 1;
		const Entity lastEntity = m_dense.back();

		// swap-remove
		m_dense[index] = lastEntity;
		m_data[index] = std::move(m_data[lastIndex]);
		m_sparse[lastEntity] = index;

		m_dense.pop_back();
		m_data.pop_back();
		m_sparse[entity] = npos;
	}

	bool contains(Entity entity) const
	{
		return entity < m_sparse.size() && m_sparse[entity] != npos;
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

	const std::vector<Entity>& dense() const { return m_dense; }
	const std::vector<T>& data() const { return m_data; }

private:
	static constexpr size_t npos = static_cast<size_t>(-1);

	void ensureSparseSize(Entity entity)
	{
		if (entity >= m_sparse.size())
			m_sparse.resize(static_cast<size_t>(entity) + 1, npos);
	}

	std::vector<size_t> m_sparse;
	std::vector<Entity> m_dense;
	std::vector<T> m_data;
};
} // namespace ECS