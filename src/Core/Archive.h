#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

/// Bidirectional archive – the same serialize() call works for both
/// saving and loading.  Concrete subclasses write to / read from files
/// or memory buffers.
///
/// Usage:
///   void serialize(Archive& ar)
///   {
///       ar << myFloat << myInt << myString;
///   }
///
/// When ar.isLoading() the operator<< reads INTO the variable;
/// when ar.isSaving() it writes FROM the variable.

class Archive
{
public:
    virtual ~Archive() = default;

    bool isLoading() const { return m_isLoading; }
    bool isSaving()  const { return !m_isLoading; }

    /// Low-level byte transfer – direction depends on isLoading().
    virtual void serialize(void* data, size_t size) = 0;

    /// True if the last operation failed or the stream is in an error state.
    virtual bool hasError() const { return m_error; }

    // ── Convenience operator<< for common types ─────────────────────
    //
    // Only fundamental types are listed here to avoid duplicates with
    // fixed-width typedefs (e.g. uint32_t == unsigned int on MSVC).
    // The fixed-width types resolve to one of these automatically.

    Archive& operator<<(bool& v)            { serialize(&v, sizeof(v)); return *this; }
    Archive& operator<<(char& v)            { serialize(&v, sizeof(v)); return *this; }
    Archive& operator<<(signed char& v)     { serialize(&v, sizeof(v)); return *this; }
    Archive& operator<<(unsigned char& v)   { serialize(&v, sizeof(v)); return *this; }
    Archive& operator<<(short& v)           { serialize(&v, sizeof(v)); return *this; }
    Archive& operator<<(unsigned short& v)  { serialize(&v, sizeof(v)); return *this; }
    Archive& operator<<(int& v)             { serialize(&v, sizeof(v)); return *this; }
    Archive& operator<<(unsigned int& v)    { serialize(&v, sizeof(v)); return *this; }
    Archive& operator<<(long& v)            { serialize(&v, sizeof(v)); return *this; }
    Archive& operator<<(unsigned long& v)   { serialize(&v, sizeof(v)); return *this; }
    Archive& operator<<(long long& v)       { serialize(&v, sizeof(v)); return *this; }
    Archive& operator<<(unsigned long long& v){ serialize(&v, sizeof(v)); return *this; }
    Archive& operator<<(float& v)           { serialize(&v, sizeof(v)); return *this; }
    Archive& operator<<(double& v)          { serialize(&v, sizeof(v)); return *this; }

    /// Length-prefixed string (uint32 length + raw chars, no null terminator).
    Archive& operator<<(std::string& v);

    // ── Fixed-size array helpers ────────────────────────────────────

    /// Serialize a C-style float array of known length.
    template<size_t N>
    Archive& serializeFloatArray(float (&arr)[N])
    {
        serialize(arr, sizeof(float) * N);
        return *this;
    }

    /// Serialize a std::vector of trivially-copyable elements.
    /// Format: uint32 count + raw bytes.
    template<typename T>
    Archive& serializeVector(std::vector<T>& vec)
    {
        static_assert(std::is_trivially_copyable_v<T>,
            "serializeVector only works with trivially-copyable types. "
            "For complex types, iterate and serialize each element.");
        uint32_t count = static_cast<uint32_t>(vec.size());
        *this << count;
        if (isLoading())
            vec.resize(count);
        if (count > 0)
            serialize(vec.data(), sizeof(T) * count);
        return *this;
    }

    /// Serialize a std::vector of std::string.
    Archive& serializeStringVector(std::vector<std::string>& vec);

    // ── Versioning helper ───────────────────────────────────────────

    /// Write/read a format version tag.  On load, returns the stored
    /// version so the caller can branch for backwards compatibility.
    uint32_t serializeVersion(uint32_t currentVersion);

protected:
    bool m_isLoading{ false };
    bool m_error{ false };
};

// ═══════════════════════════════════════════════════════════════════════
// FileArchiveWriter – binary file output
// ═══════════════════════════════════════════════════════════════════════

class FileArchiveWriter : public Archive
{
public:
    explicit FileArchiveWriter(const std::string& path)
        : m_stream(path, std::ios::binary | std::ios::out | std::ios::trunc)
    {
        m_isLoading = false;
        if (!m_stream.is_open()) m_error = true;
    }

    void serialize(void* data, size_t size) override
    {
        if (m_error) return;
        m_stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        if (!m_stream.good()) m_error = true;
    }

    bool hasError() const override { return m_error || !m_stream.good(); }
    void close() { m_stream.close(); }

private:
    std::ofstream m_stream;
};

// ═══════════════════════════════════════════════════════════════════════
// FileArchiveReader – binary file input
// ═══════════════════════════════════════════════════════════════════════

class FileArchiveReader : public Archive
{
public:
    explicit FileArchiveReader(const std::string& path)
        : m_stream(path, std::ios::binary | std::ios::in)
    {
        m_isLoading = true;
        if (!m_stream.is_open()) m_error = true;
    }

    void serialize(void* data, size_t size) override
    {
        if (m_error) return;
        m_stream.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
        if (!m_stream.good() && !m_stream.eof()) m_error = true;
    }

    bool hasError() const override { return m_error; }
    bool atEnd() const { return m_stream.eof(); }
    void close() { m_stream.close(); }

private:
    std::ifstream m_stream;
};

// ═══════════════════════════════════════════════════════════════════════
// MemoryArchive – reads/writes to an in-memory byte buffer.
// Ideal for undo/redo snapshots and network packets.
// ═══════════════════════════════════════════════════════════════════════

class MemoryArchive : public Archive
{
public:
    /// Construct a writer (starts with an empty buffer).
    MemoryArchive()
    {
        m_isLoading = false;
    }

    /// Construct a reader from existing data.
    explicit MemoryArchive(const std::vector<uint8_t>& data)
        : m_buffer(data)
    {
        m_isLoading = true;
    }

    /// Construct a reader by moving data in.
    explicit MemoryArchive(std::vector<uint8_t>&& data)
        : m_buffer(std::move(data))
    {
        m_isLoading = true;
    }

    void serialize(void* data, size_t size) override
    {
        if (m_error) return;
        if (m_isLoading)
        {
            if (m_readPos + size > m_buffer.size())
            {
                m_error = true;
                return;
            }
            std::memcpy(data, m_buffer.data() + m_readPos, size);
            m_readPos += size;
        }
        else
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            m_buffer.insert(m_buffer.end(), bytes, bytes + size);
        }
    }

    /// Access the underlying buffer (e.g. to store for undo).
    const std::vector<uint8_t>& getBuffer() const { return m_buffer; }
    std::vector<uint8_t>        takeBuffer()      { return std::move(m_buffer); }

    /// Reset read position to the beginning (for re-reading).
    void resetRead() { m_readPos = 0; m_error = false; }

    /// Reset to empty writer state.
    void resetWrite()
    {
        m_buffer.clear();
        m_readPos = 0;
        m_isLoading = false;
        m_error = false;
    }

    size_t size() const { return m_buffer.size(); }

private:
    std::vector<uint8_t> m_buffer;
    size_t m_readPos{ 0 };
};
