#pragma once
#include "ArchetypeTypes.h"
#include "Chunk.h"
#include <unordered_map>
#include <functional>

namespace Mass
{

// Forward declaration
class MassEntitySubsystem;

// ── MassQuery ────────────────────────────────────────────────────────
/// Describes which fragment types a processor requires (read/write) and
/// which must be absent.  Used to filter matching archetypes.

class MassQuery
{
public:
	MassQuery() = default;

	/// Require fragment type T (entity must have it).
	template<typename T>
	MassQuery& require()
	{
		FragmentRegistry::Instance().registerFragment<T>();
		m_required.set(getFragmentTypeID<T>());
		return *this;
	}

	/// Exclude fragment type T (entity must NOT have it).
	template<typename T>
	MassQuery& exclude()
	{
		FragmentRegistry::Instance().registerFragment<T>();
		m_excluded.set(getFragmentTypeID<T>());
		return *this;
	}

	const ArchetypeSignature& required() const { return m_required; }
	const ArchetypeSignature& excluded() const { return m_excluded; }

private:
	ArchetypeSignature m_required;
	ArchetypeSignature m_excluded;
};

// ── ChunkView ────────────────────────────────────────────────────────
/// Lightweight accessor provided to processors during chunk iteration.
/// Gives typed access to fragment columns without exposing internals.

class ChunkView
{
public:
	ChunkView(Chunk& chunk,
	           const std::unordered_map<FragmentTypeID, uint32_t>& columnMap,
	           size_t entityCount)
		: m_chunk(chunk)
		, m_columnMap(columnMap)
		, m_entityCount(entityCount)
	{}

	/// Number of entities in this chunk.
	size_t count() const { return m_entityCount; }

	/// Get a pointer to the first element of fragment type T.
	/// Returns nullptr if the fragment is not present.
	template<typename T>
	T* getFragmentArray()
	{
		FragmentTypeID tid = getFragmentTypeID<T>();
		auto it = m_columnMap.find(tid);
		if (it == m_columnMap.end()) return nullptr;
		return reinterpret_cast<T*>(m_chunk.columnData(it->second));
	}

	template<typename T>
	const T* getFragmentArray() const
	{
		FragmentTypeID tid = getFragmentTypeID<T>();
		auto it = m_columnMap.find(tid);
		if (it == m_columnMap.end()) return nullptr;
		return reinterpret_cast<const T*>(m_chunk.columnData(it->second));
	}

	/// Get fragment at specific row index.
	template<typename T>
	T* getFragment(size_t row)
	{
		T* arr = getFragmentArray<T>();
		return arr ? &arr[row] : nullptr;
	}

	template<typename T>
	const T* getFragment(size_t row) const
	{
		const T* arr = getFragmentArray<T>();
		return arr ? &arr[row] : nullptr;
	}

private:
	Chunk& m_chunk;
	const std::unordered_map<FragmentTypeID, uint32_t>& m_columnMap;
	size_t m_entityCount;
};

// ── MassProcessor ────────────────────────────────────────────────────
/// Base class for stateless processors that iterate over matching chunks.
/// Subclass and implement configure() + executeChunk().
///
/// Example:
/// ```
/// class MovementProcessor : public Mass::MassProcessor
/// {
/// public:
///     void configure(Mass::MassQuery& query) override
///     {
///         query.require<PositionFragment>().require<VelocityFragment>();
///     }
///     void executeChunk(Mass::ChunkView& view, float dt) override
///     {
///         auto* pos = view.getFragmentArray<PositionFragment>();
///         auto* vel = view.getFragmentArray<VelocityFragment>();
///         for (size_t i = 0; i < view.count(); ++i)
///             pos[i].value += vel[i].value * dt;
///     }
/// };
/// ```

class MassProcessor
{
public:
	virtual ~MassProcessor() = default;

	/// Called once to define which fragments this processor needs.
	virtual void configure(MassQuery& query) = 0;

	/// Called once per matching chunk during tick.
	virtual void executeChunk(ChunkView& view, float deltaTime) = 0;

	/// Drives iteration.  Called by MassEntitySubsystem::tick().
	void execute(MassEntitySubsystem& subsystem, float deltaTime);

	/// Get the query (configured lazily on first execute).
	const MassQuery& getQuery() const { return m_query; }

private:
	MassQuery m_query;
	bool      m_configured{ false };
};

} // namespace Mass
