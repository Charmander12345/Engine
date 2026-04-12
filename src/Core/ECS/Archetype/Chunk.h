#pragma once
#include "ArchetypeTypes.h"
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <memory>
#include <vector>

namespace Mass
{

/// Layout of a single fragment column inside a chunk.
struct ColumnLayout
{
	FragmentTypeID typeID{ InvalidFragmentType };
	size_t         elementSize{ 0 };
	size_t         alignment{ 0 };
	size_t         offset{ 0 };     ///< Byte offset from chunk data start
};

/// A Chunk is a contiguous block of ~64 KB storing N entities in SoA layout.
/// Each fragment type occupies a contiguous sub-array (column) within the chunk.
///
/// Memory layout:
///   [Column0: T0₀ T0₁ ... T0ₙ] [padding] [Column1: T1₀ T1₁ ... T1ₙ] ...
///
class Chunk
{
public:
	Chunk() = default;
	~Chunk() { release(); }

	Chunk(const Chunk&) = delete;
	Chunk& operator=(const Chunk&) = delete;

	Chunk(Chunk&& other) noexcept
		: m_data(other.m_data)
		, m_capacity(other.m_capacity)
		, m_count(other.m_count)
		, m_columns(std::move(other.m_columns))
	{
		other.m_data = nullptr;
		other.m_capacity = 0;
		other.m_count = 0;
	}

	Chunk& operator=(Chunk&& other) noexcept
	{
		if (this != &other)
		{
			release();
			m_data     = other.m_data;
			m_capacity = other.m_capacity;
			m_count    = other.m_count;
			m_columns  = std::move(other.m_columns);
			other.m_data = nullptr;
			other.m_capacity = 0;
			other.m_count = 0;
		}
		return *this;
	}

	/// Allocate the chunk for the given column layout.
	/// @param columns  Ordered column descriptors (offset must be pre-computed).
	/// @param capacity Max entities this chunk holds.
	void allocate(const std::vector<ColumnLayout>& columns, size_t capacity)
	{
		release();
		m_columns  = columns;
		m_capacity = capacity;

		// Total byte size: last column offset + last column size * capacity, rounded up
		size_t totalBytes = 0;
		if (!columns.empty())
		{
			const auto& last = columns.back();
			totalBytes = last.offset + last.elementSize * capacity;
		}

		// Align total allocation to cache-line
		totalBytes = alignUp(totalBytes, CacheLineBytes);
		m_data = static_cast<uint8_t*>(std::malloc(totalBytes));
		assert(m_data && "Chunk allocation failed");
		std::memset(m_data, 0, totalBytes);
		m_count = 0;
	}

	/// Release memory.
	void release()
	{
		if (m_data)
		{
			std::free(m_data);
			m_data = nullptr;
		}
		m_capacity = 0;
		m_count    = 0;
	}

	/// Get pointer to column data for the given column index.
	uint8_t* columnData(size_t columnIndex)
	{
		assert(columnIndex < m_columns.size());
		return m_data + m_columns[columnIndex].offset;
	}

	const uint8_t* columnData(size_t columnIndex) const
	{
		assert(columnIndex < m_columns.size());
		return m_data + m_columns[columnIndex].offset;
	}

	/// Get typed pointer to element at row in column.
	template<typename T>
	T* getElement(size_t columnIndex, size_t row)
	{
		assert(row < m_count);
		return reinterpret_cast<T*>(columnData(columnIndex) + row * m_columns[columnIndex].elementSize);
	}

	template<typename T>
	const T* getElement(size_t columnIndex, size_t row) const
	{
		assert(row < m_count);
		return reinterpret_cast<const T*>(columnData(columnIndex) + row * m_columns[columnIndex].elementSize);
	}

	/// Append an entity's default-initialized row.  Returns row index.
	size_t pushBack()
	{
		assert(m_count < m_capacity);
		return m_count++;
	}

	/// Swap-remove row at \p row with the last row.  Returns the entity that was
	/// in the last slot (which now occupies \p row), or UINT32_MAX if the removed
	/// row was already the last.
	void swapRemove(size_t row)
	{
		assert(row < m_count && m_count > 0);
		const size_t last = m_count - 1;
		if (row != last)
		{
			for (size_t c = 0; c < m_columns.size(); ++c)
			{
				uint8_t* dst = columnData(c) + row  * m_columns[c].elementSize;
				uint8_t* src = columnData(c) + last * m_columns[c].elementSize;
				std::memcpy(dst, src, m_columns[c].elementSize);
			}
		}
		--m_count;
	}

	/// Copy row data from another chunk into a new back row in this chunk.
	void copyRowFrom(const Chunk& src, size_t srcRow)
	{
		assert(m_count < m_capacity);
		assert(m_columns.size() == src.m_columns.size());
		const size_t dstRow = m_count++;
		for (size_t c = 0; c < m_columns.size(); ++c)
		{
			uint8_t* dst = columnData(c) + dstRow * m_columns[c].elementSize;
			const uint8_t* srcPtr = src.columnData(c) + srcRow * m_columns[c].elementSize;
			std::memcpy(dst, srcPtr, m_columns[c].elementSize);
		}
	}

	size_t count()    const { return m_count; }
	size_t capacity() const { return m_capacity; }
	bool   isFull()   const { return m_count >= m_capacity; }
	bool   isEmpty()  const { return m_count == 0; }

	const std::vector<ColumnLayout>& columns() const { return m_columns; }

private:
	static size_t alignUp(size_t value, size_t alignment)
	{
		return (value + alignment - 1) & ~(alignment - 1);
	}

	uint8_t*                m_data{ nullptr };
	size_t                  m_capacity{ 0 };
	size_t                  m_count{ 0 };
	std::vector<ColumnLayout> m_columns;
};

/// Compute column layouts and per-chunk entity capacity from fragment type infos.
/// Returns the capacity (max entities per chunk).
inline size_t computeChunkLayout(
	const std::vector<FragmentTypeInfo>& fragments,
	std::vector<ColumnLayout>& outColumns)
{
	outColumns.clear();
	if (fragments.empty()) return 0;

	// Compute total bytes per entity (each fragment size, padded to alignment)
	size_t bytesPerEntity = 0;
	for (const auto& f : fragments)
	{
		size_t aligned = (f.size + f.alignment - 1) & ~(f.alignment - 1);
		bytesPerEntity += aligned;
	}

	if (bytesPerEntity == 0) return 0;

	// Capacity: how many entities fit in ChunkBytes
	size_t capacity = ChunkBytes / bytesPerEntity;
	if (capacity == 0) capacity = 1; // at least one entity

	// Layout columns sequentially, cache-line aligned per column
	size_t offset = 0;
	for (const auto& f : fragments)
	{
		// Align column start to the fragment's alignment (min cache-line for large types)
		size_t colAlign = f.alignment;
		offset = (offset + colAlign - 1) & ~(colAlign - 1);

		ColumnLayout col;
		col.typeID      = f.id;
		col.elementSize = f.size;
		col.alignment   = f.alignment;
		col.offset      = offset;
		outColumns.push_back(col);

		offset += f.size * capacity;
	}

	return capacity;
}

} // namespace Mass
