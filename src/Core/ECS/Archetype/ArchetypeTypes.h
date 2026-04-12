#pragma once
#include <cstddef>
#include <cstdint>
#include <bitset>
#include <vector>
#include <functional>
#include <string>

namespace Mass
{

// ── Constants ────────────────────────────────────────────────────────
static constexpr size_t MaxFragmentTypes = 64;
static constexpr size_t ChunkBytes       = 64 * 1024;  // 64 KB per chunk
static constexpr size_t CacheLineBytes   = 64;

// ── Fragment Type Registration ───────────────────────────────────────
using FragmentTypeID = uint16_t;
static constexpr FragmentTypeID InvalidFragmentType = UINT16_MAX;

namespace Detail
{
	inline FragmentTypeID nextFragmentID()
	{
		static FragmentTypeID s_next = 0;
		return s_next++;
	}
}

/// Compile-time unique ID per fragment struct.
template<typename T>
inline FragmentTypeID getFragmentTypeID()
{
	static const FragmentTypeID id = Detail::nextFragmentID();
	return id;
}

/// Runtime info about a registered fragment type.
struct FragmentTypeInfo
{
	FragmentTypeID id{ InvalidFragmentType };
	size_t         size{ 0 };
	size_t         alignment{ 0 };
	std::string    name;
};

// ── Fragment Registry ────────────────────────────────────────────────
class FragmentRegistry
{
public:
	static FragmentRegistry& Instance()
	{
		static FragmentRegistry s_instance;
		return s_instance;
	}

	template<typename T>
	const FragmentTypeInfo& registerFragment(const char* debugName = "")
	{
		FragmentTypeID id = getFragmentTypeID<T>();
		if (id >= m_infos.size())
			m_infos.resize(static_cast<size_t>(id) + 1);
		auto& info = m_infos[id];
		if (info.id == InvalidFragmentType)
		{
			info.id        = id;
			info.size      = sizeof(T);
			info.alignment = alignof(T);
			info.name      = debugName;
		}
		return info;
	}

	const FragmentTypeInfo& getInfo(FragmentTypeID id) const
	{
		return m_infos[id];
	}

	size_t count() const { return m_infos.size(); }

private:
	FragmentRegistry() = default;
	std::vector<FragmentTypeInfo> m_infos;
};

/// Helper macro: registers a fragment and exposes its ID.
#define MASS_FRAGMENT(T) \
	namespace MassFragmentReg_##T { \
		static const auto& s_info = ::Mass::FragmentRegistry::Instance().registerFragment<T>(#T); \
	}

// ── Archetype Signature (sorted bitset of fragment type IDs) ─────────
using ArchetypeSignature = std::bitset<MaxFragmentTypes>;

// ── MassEntity Handle ────────────────────────────────────────────────
struct MassEntity
{
	uint32_t index{ 0 };
	uint32_t generation{ 0 };

	bool operator==(MassEntity other) const { return index == other.index && generation == other.generation; }
	bool operator!=(MassEntity other) const { return !(*this == other); }
	bool isValid() const { return generation != 0; }
};

static constexpr MassEntity InvalidMassEntity{ 0, 0 };

// ── Entity Slot (global array entry) ─────────────────────────────────
struct MassEntitySlot
{
	uint32_t generation{ 0 };   ///< Incremented on reuse; 0 = never allocated
	uint32_t archetypeIndex{ 0 }; ///< Index into MassEntitySubsystem archetype list
	uint32_t chunkIndex{ 0 };   ///< Index of chunk within archetype
	uint32_t rowIndex{ 0 };     ///< Row within chunk
	bool     alive{ false };
};

// ── Deferred Command Buffer ──────────────────────────────────────────
/// Structural changes are deferred to avoid iterator invalidation during
/// processor execution.  Flush after all processors for the current phase.
class MassCommandBuffer
{
public:
	using DeferredFn = std::function<void()>;

	void enqueue(DeferredFn&& fn) { m_commands.push_back(std::move(fn)); }

	void flush()
	{
		for (auto& cmd : m_commands)
			cmd();
		m_commands.clear();
	}

	bool empty() const { return m_commands.empty(); }
	size_t size() const { return m_commands.size(); }

private:
	std::vector<DeferredFn> m_commands;
};

} // namespace Mass
