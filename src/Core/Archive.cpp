#include "Archive.h"

// ── std::string (length-prefixed) ───────────────────────────────────

Archive& Archive::operator<<(std::string& v)
{
    uint32_t len = static_cast<uint32_t>(v.size());
    *this << len;
    if (m_error) return *this;

    if (m_isLoading)
    {
        v.resize(len);
    }
    if (len > 0)
    {
        serialize(v.data(), len);
    }
    return *this;
}

// ── std::vector<std::string> ────────────────────────────────────────

Archive& Archive::serializeStringVector(std::vector<std::string>& vec)
{
    uint32_t count = static_cast<uint32_t>(vec.size());
    *this << count;
    if (m_isLoading)
        vec.resize(count);
    for (uint32_t i = 0; i < count; ++i)
        *this << vec[i];
    return *this;
}

// ── Version tag ─────────────────────────────────────────────────────

uint32_t Archive::serializeVersion(uint32_t currentVersion)
{
    uint32_t ver = currentVersion;
    *this << ver;
    return ver;
}
