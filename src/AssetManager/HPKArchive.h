#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <fstream>
#include <optional>

// ── HPK Format Constants (shared between editor packer and runtime reader) ──
static constexpr uint32_t HPK_MAGIC   = 0x48504B31; // 'HPK1'
static constexpr uint32_t HPK_VERSION = 1;

struct HPKHeader
{
    uint32_t magic{ HPK_MAGIC };
    uint32_t version{ HPK_VERSION };
    uint32_t flags{ 0 };
    uint32_t reserved[5]{ 0, 0, 0, 0, 0 };
};
static_assert(sizeof(HPKHeader) == 32, "HPKHeader must be 32 bytes");

struct HPKFooter
{
    uint64_t tocOffset{ 0 };
    uint32_t tocCount{ 0 };
    uint32_t magic{ HPK_MAGIC };
};
static_assert(sizeof(HPKFooter) == 16, "HPKFooter must be 16 bytes");

// ── HPK Reader (runtime + editor) ──────────────────────────────────────
class HPKReader
{
public:
    HPKReader() = default;
    ~HPKReader();
    HPKReader(const HPKReader&) = delete;
    HPKReader& operator=(const HPKReader&) = delete;

    bool mount(const std::string& hpkFilePath);
    void unmount();
    bool isMounted() const { return m_mounted; }

    bool contains(const std::string& virtualPath) const;
    std::optional<std::vector<char>> readFile(const std::string& virtualPath) const;

    struct FileInfo
    {
        uint64_t offset{ 0 };
        uint64_t size{ 0 };
        uint32_t flags{ 0 };
    };

    std::optional<FileInfo> getFileInfo(const std::string& virtualPath) const;
    std::vector<std::string> getFileList() const;
    size_t getFileCount() const { return m_toc.size(); }
    const std::string& getArchivePath() const { return m_archivePath; }
    const std::string& getBaseDir() const { return m_baseDir; }

    // Derive the HPK virtual path from an absolute filesystem path.
    // Returns empty string if the path is not under the HPK base directory.
    std::string makeVirtualPath(const std::string& absolutePath) const;

    // Global singleton access for cross-module HPK reads (set by AssetManager).
    static HPKReader* GetMounted();
    static void       SetMounted(HPKReader* reader);

private:
    bool readTOC();
    static std::string normalizePath(const std::string& p);

    std::string m_archivePath;
    std::string m_baseDir; // parent directory of the .hpk file
    mutable std::ifstream m_file;
    mutable std::mutex m_readMutex;
    std::unordered_map<std::string, FileInfo> m_toc;
    bool m_mounted{ false };

    static HPKReader* s_mounted;
};

#if ENGINE_EDITOR

// ── HPK Writer (editor only) ───────────────────────────────────────────
class HPKWriter
{
public:
    HPKWriter() = default;
    ~HPKWriter();

    bool begin(const std::string& outputPath);
    bool addFile(const std::string& virtualPath, const void* data, size_t size);
    bool addFileFromDisk(const std::string& virtualPath, const std::string& diskPath);
    bool finalize();

    size_t   getFileCount()     const { return m_entries.size(); }
    uint64_t getTotalDataSize() const { return m_dataOffset - sizeof(HPKHeader); }

private:
    struct TOCEntry
    {
        std::string path;
        uint64_t    offset{ 0 };
        uint64_t    size{ 0 };
        uint32_t    flags{ 0 };
    };

    std::string m_outputPath;
    std::ofstream m_file;
    std::vector<TOCEntry> m_entries;
    uint64_t m_dataOffset{ sizeof(HPKHeader) };
    bool m_open{ false };
};

#endif // ENGINE_EDITOR
