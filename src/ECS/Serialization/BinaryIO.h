#pragma once

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <type_traits>

namespace ecs::bin
{
	template <typename T>
	inline bool writePod(std::ostream& out, const T& v)
	{
		static_assert(std::is_trivially_copyable_v<T>, "writePod requires trivially copyable types");
		out.write(reinterpret_cast<const char*>(&v), sizeof(T));
		return out.good();
	}

	template <typename T>
	inline bool readPod(std::istream& in, T& v)
	{
		static_assert(std::is_trivially_copyable_v<T>, "readPod requires trivially copyable types");
		in.read(reinterpret_cast<char*>(&v), sizeof(T));
		return in.good();
	}

	inline bool writeString(std::ostream& out, const std::string& s)
	{
		const std::uint32_t len = static_cast<std::uint32_t>(s.size());
		if (!writePod(out, len))
			return false;
		if (len > 0)
		{
			out.write(s.data(), len);
		}
		return out.good();
	}

	inline bool readString(std::istream& in, std::string& s)
	{
		std::uint32_t len = 0;
		if (!readPod(in, len))
			return false;
		s.clear();
		if (len > 0)
		{
			s.resize(len);
			in.read(s.data(), len);
		}
		return in.good();
	}
}
