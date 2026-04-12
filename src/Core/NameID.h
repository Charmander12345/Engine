#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

/// Lightweight interned-string identifier – only 4 bytes.
/// Comparisons are O(1) integer comparisons instead of O(n) string comparisons.
/// Inspired by Unreal Engine's FName system.
struct NameID
{
	uint32_t index{ 0 };  // 0 = None/Invalid

	bool operator==(NameID other) const { return index == other.index; }
	bool operator!=(NameID other) const { return index != other.index; }
	bool operator<(NameID other)  const { return index < other.index; }

	/// Returns true if this NameID refers to a valid interned string.
	bool isValid() const { return index != 0; }

	/// Returns true if this is the None/Invalid name.
	bool isNone() const { return index == 0; }

	/// Convenience: get the string representation.
	const std::string& toString() const;

	/// Construct a NameID from a string (interning it if needed).
	static NameID fromString(const std::string& str);

	/// Lookup without inserting – returns invalid NameID if not found.
	static NameID find(const std::string& str);

	/// The "None" / invalid NameID constant.
	static const NameID None;
};

/// Hash support for use as unordered_map key.
namespace std {
template<>
struct hash<NameID> {
	size_t operator()(NameID id) const noexcept { return std::hash<uint32_t>{}(id.index); }
};
}

/// Global name pool – stores interned strings.
/// Thread-safety: currently single-threaded (game thread only).
class NamePool
{
public:
	static NamePool& Instance()
	{
		static NamePool instance;
		return instance;
	}

	/// Find an existing name.  Returns NameID{0} if not found.
	NameID find(const std::string& str) const
	{
		auto it = m_lookup.find(str);
		if (it != m_lookup.end())
			return NameID{ it->second };
		return NameID{ 0 };
	}

	/// Find or insert a name.  Returns the interned NameID.
	NameID findOrAdd(const std::string& str)
	{
		if (str.empty())
			return NameID{ 0 };

		auto it = m_lookup.find(str);
		if (it != m_lookup.end())
			return NameID{ it->second };

		uint32_t newIndex = static_cast<uint32_t>(m_strings.size());
		m_strings.push_back(str);
		m_lookup[str] = newIndex;
		return NameID{ newIndex };
	}

	/// Resolve a NameID back to its string.
	const std::string& resolve(NameID id) const
	{
		if (id.index == 0 || id.index >= m_strings.size())
			return s_empty;
		return m_strings[id.index];
	}

	/// Total number of interned names (excluding the empty slot).
	size_t count() const { return m_strings.size() > 0 ? m_strings.size() - 1 : 0; }

	/// Clear all interned names (generally only for shutdown).
	void clear()
	{
		m_lookup.clear();
		m_strings.clear();
		m_strings.push_back(std::string{});  // slot 0 = empty/None
	}

private:
	NamePool()
	{
		// Slot 0 is reserved for "None" / empty name.
		m_strings.push_back(std::string{});
	}

	NamePool(const NamePool&) = delete;
	NamePool& operator=(const NamePool&) = delete;

	std::unordered_map<std::string, uint32_t> m_lookup;
	std::vector<std::string> m_strings;  // index → string

	static inline const std::string s_empty{};
};

// ── Inline implementations ──────────────────────────────────────────

inline const NameID NameID::None{ 0 };

inline const std::string& NameID::toString() const
{
	return NamePool::Instance().resolve(*this);
}

inline NameID NameID::fromString(const std::string& str)
{
	return NamePool::Instance().findOrAdd(str);
}

inline NameID NameID::find(const std::string& str)
{
	return NamePool::Instance().find(str);
}
