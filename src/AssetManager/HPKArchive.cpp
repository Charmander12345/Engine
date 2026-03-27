#include "HPKArchive.h"
#include "../Logger/Logger.h"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

// ── Static singleton ────────────────────────────────────────────────────
HPKReader* HPKReader::s_mounted = nullptr;

HPKReader* HPKReader::GetMounted()  { return s_mounted; }
void       HPKReader::SetMounted(HPKReader* r) { s_mounted = r; }

// ── Helpers ─────────────────────────────────────────────────────────────
std::string HPKReader::normalizePath(const std::string& p)
{
    std::string out = p;
    std::replace(out.begin(), out.end(), '\\', '/');
    return out;
}

// ── HPK Reader ──────────────────────────────────────────────────────────
HPKReader::~HPKReader()
{
    unmount();
}

bool HPKReader::mount(const std::string& hpkFilePath)
{
    if (m_mounted) unmount();

    m_file.open(hpkFilePath, std::ios::binary);
    if (!m_file.is_open())
    {
        Logger::Instance().log("HPK: Failed to open archive: " + hpkFilePath, Logger::LogLevel::ERROR);
        return false;
    }

    m_archivePath = hpkFilePath;
    m_baseDir = fs::path(hpkFilePath).parent_path().string();
    if (!m_baseDir.empty() && m_baseDir.back() != '/' && m_baseDir.back() != '\\')
        m_baseDir += '/';

    if (!readTOC())
    {
        Logger::Instance().log("HPK: Failed to read TOC from: " + hpkFilePath, Logger::LogLevel::ERROR);
        m_file.close();
        return false;
    }

    m_mounted = true;
    Logger::Instance().log("HPK: Mounted archive: " + hpkFilePath +
        " (" + std::to_string(m_toc.size()) + " files)", Logger::LogLevel::INFO);
    return true;
}

void HPKReader::unmount()
{
    if (this == s_mounted) s_mounted = nullptr;
    if (m_file.is_open()) m_file.close();
    m_toc.clear();
    m_archivePath.clear();
    m_baseDir.clear();
    m_mounted = false;
}

bool HPKReader::readTOC()
{
    // Read footer (last 16 bytes)
    m_file.seekg(0, std::ios::end);
    const auto fileSize = m_file.tellg();
    if (fileSize < static_cast<std::streamoff>(sizeof(HPKHeader) + sizeof(HPKFooter)))
        return false;

    m_file.seekg(-static_cast<std::streamoff>(sizeof(HPKFooter)), std::ios::end);
    HPKFooter footer{};
    m_file.read(reinterpret_cast<char*>(&footer), sizeof(footer));
    if (!m_file || footer.magic != HPK_MAGIC)
        return false;

    // Read header
    m_file.seekg(0, std::ios::beg);
    HPKHeader header{};
    m_file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!m_file || header.magic != HPK_MAGIC || header.version != HPK_VERSION)
        return false;

    // Read TOC entries
    m_file.seekg(static_cast<std::streamoff>(footer.tocOffset));
    m_toc.clear();
    m_toc.reserve(footer.tocCount);

    for (uint32_t i = 0; i < footer.tocCount; ++i)
    {
        uint16_t pathLen = 0;
        m_file.read(reinterpret_cast<char*>(&pathLen), sizeof(pathLen));
        if (!m_file) return false;

        std::string path(pathLen, '\0');
        m_file.read(path.data(), pathLen);
        if (!m_file) return false;

        FileInfo info{};
        m_file.read(reinterpret_cast<char*>(&info.offset), sizeof(info.offset));
        m_file.read(reinterpret_cast<char*>(&info.size),   sizeof(info.size));
        m_file.read(reinterpret_cast<char*>(&info.flags),  sizeof(info.flags));
        if (!m_file) return false;

        m_toc[normalizePath(path)] = info;
    }

    return true;
}

bool HPKReader::contains(const std::string& virtualPath) const
{
    return m_toc.count(normalizePath(virtualPath)) > 0;
}

std::optional<std::vector<char>> HPKReader::readFile(const std::string& virtualPath) const
{
    const std::string key = normalizePath(virtualPath);
    auto it = m_toc.find(key);
    if (it == m_toc.end())
        return std::nullopt;

    const auto& info = it->second;
    std::vector<char> buffer(info.size);

    {
        std::lock_guard<std::mutex> lock(m_readMutex);
        m_file.clear();
        m_file.seekg(static_cast<std::streamoff>(info.offset));
        m_file.read(buffer.data(), static_cast<std::streamsize>(info.size));
        if (!m_file)
        {
            m_file.clear();
            return std::nullopt;
        }
    }

    return buffer;
}

std::optional<HPKReader::FileInfo> HPKReader::getFileInfo(const std::string& virtualPath) const
{
    const std::string key = normalizePath(virtualPath);
    auto it = m_toc.find(key);
    if (it == m_toc.end())
        return std::nullopt;
    return it->second;
}

std::vector<std::string> HPKReader::getFileList() const
{
    std::vector<std::string> files;
    files.reserve(m_toc.size());
    for (const auto& [path, _] : m_toc)
        files.push_back(path);
    return files;
}

std::string HPKReader::makeVirtualPath(const std::string& absolutePath) const
{
    if (m_baseDir.empty()) return {};
    fs::path absP  = fs::path(absolutePath).lexically_normal();
    fs::path baseP = fs::path(m_baseDir).lexically_normal();
    fs::path relP  = absP.lexically_relative(baseP);
    if (!relP.empty())
    {
        std::string result = relP.string();
        if (!(result.size() >= 2 && result[0] == '.' && result[1] == '.'))
        {
            std::replace(result.begin(), result.end(), '\\', '/');
            return result;
        }
    }

    // Fallback: try to extract a virtual path by matching known top-level
    // directories stored in the archive (e.g. "shaders/", "Content/", "Config/").
    std::string norm = absP.string();
    std::replace(norm.begin(), norm.end(), '\\', '/');
    static const char* knownPrefixes[] = { "shaders/", "Content/", "Config/" };
    for (const char* prefix : knownPrefixes)
    {
        std::string needle = std::string("/") + prefix;
        auto pos = norm.find(needle);
        if (pos != std::string::npos)
        {
            std::string candidate = norm.substr(pos + 1); // skip leading '/'
            if (m_toc.count(candidate))
                return candidate;
        }
    }
    return {};
}

// ── HPK Writer ──────────────────────────────────────────────────────────
#if ENGINE_EDITOR

HPKWriter::~HPKWriter()
{
    if (m_open && m_file.is_open())
        m_file.close();
}

bool HPKWriter::begin(const std::string& outputPath)
{
    m_outputPath = outputPath;
    m_entries.clear();
    m_dataOffset = sizeof(HPKHeader);

    std::error_code ec;
    fs::create_directories(fs::path(outputPath).parent_path(), ec);

    m_file.open(outputPath, std::ios::binary | std::ios::trunc);
    if (!m_file.is_open())
        return false;

    HPKHeader header{};
    m_file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    m_open = true;
    return true;
}

bool HPKWriter::addFile(const std::string& virtualPath, const void* data, size_t size)
{
    if (!m_open) return false;

    std::string normalized = virtualPath;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');

    // Align data to 16-byte boundary
    const uint64_t padding = (16 - (m_dataOffset % 16)) % 16;
    if (padding > 0)
    {
        const char zeros[16] = {};
        m_file.write(zeros, static_cast<std::streamsize>(padding));
        m_dataOffset += padding;
    }

    TOCEntry entry;
    entry.path   = normalized;
    entry.offset = m_dataOffset;
    entry.size   = size;
    entry.flags  = 0;

    m_file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    m_dataOffset += size;

    m_entries.push_back(std::move(entry));
    return m_file.good();
}

bool HPKWriter::addFileFromDisk(const std::string& virtualPath, const std::string& diskPath)
{
    std::ifstream in(diskPath, std::ios::binary | std::ios::ate);
    if (!in.is_open()) return false;

    const auto size = in.tellg();
    in.seekg(0, std::ios::beg);

    std::vector<char> buffer(static_cast<size_t>(size));
    in.read(buffer.data(), size);
    if (!in) return false;

    return addFile(virtualPath, buffer.data(), buffer.size());
}

bool HPKWriter::finalize()
{
    if (!m_open) return false;

    // Align before TOC
    const uint64_t padding = (16 - (m_dataOffset % 16)) % 16;
    if (padding > 0)
    {
        const char zeros[16] = {};
        m_file.write(zeros, static_cast<std::streamsize>(padding));
        m_dataOffset += padding;
    }

    const uint64_t tocOffset = m_dataOffset;

    // Write TOC entries
    for (const auto& entry : m_entries)
    {
        const uint16_t pathLen = static_cast<uint16_t>(entry.path.size());
        m_file.write(reinterpret_cast<const char*>(&pathLen), sizeof(pathLen));
        m_file.write(entry.path.data(), pathLen);
        m_file.write(reinterpret_cast<const char*>(&entry.offset), sizeof(entry.offset));
        m_file.write(reinterpret_cast<const char*>(&entry.size),   sizeof(entry.size));
        m_file.write(reinterpret_cast<const char*>(&entry.flags),  sizeof(entry.flags));
    }

    // Write footer
    HPKFooter footer{};
    footer.tocOffset = tocOffset;
    footer.tocCount  = static_cast<uint32_t>(m_entries.size());
    footer.magic     = HPK_MAGIC;
    m_file.write(reinterpret_cast<const char*>(&footer), sizeof(footer));

    m_file.close();
    m_open = false;

    Logger::Instance().log("HPK: Written " + std::to_string(m_entries.size()) +
        " files to " + m_outputPath, Logger::LogLevel::INFO);
    return true;
}

#endif // ENGINE_EDITOR
