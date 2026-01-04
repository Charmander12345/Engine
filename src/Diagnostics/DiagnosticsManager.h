#pragma once

#include <string>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <filesystem>

class DiagnosticsManager
{
public:
    enum  class RHIType
    {
        Unknown,
        OpenGL,
        Vulkan,
        DirectX11,
        DirectX12
	};

    static DiagnosticsManager& Instance();



    // Set or update a state entry
    void setState(const std::string& key, const std::string& value);

    // Retrieve a state entry; returns std::nullopt if missing
    std::optional<std::string> getState(const std::string& key) const;

    // Clear all stored states
    void clear();

    // Persist/load simple key=value pairs to/from config/config.ini in the engine directory
    bool saveConfig() const;
    bool loadConfig();

private:
    DiagnosticsManager() = default;
    ~DiagnosticsManager() = default;
    DiagnosticsManager(const DiagnosticsManager&) = delete;
    DiagnosticsManager& operator=(const DiagnosticsManager&) = delete;

    std::filesystem::path getConfigPath() const;

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::string> m_states;
};
