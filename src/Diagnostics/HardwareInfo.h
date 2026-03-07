#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct CpuInfo
{
    std::string brand;
    int physicalCores = 0;
    int logicalCores  = 0;
};

struct GpuInfo
{
    std::string renderer;
    std::string vendor;
    std::string driverVersion;
    int64_t vramTotalMB = 0;
    int64_t vramFreeMB  = 0;
};

struct RamInfo
{
    int64_t totalMB     = 0;
    int64_t availableMB = 0;
};

struct MonitorInfo
{
    std::string name;
    int width       = 0;
    int height      = 0;
    int refreshRate = 0;
    float dpiScale  = 1.0f;
    bool primary    = false;
};

struct HardwareInfo
{
    CpuInfo cpu;
    GpuInfo gpu;
    RamInfo ram;
    std::vector<MonitorInfo> monitors;
};

CpuInfo                 queryCpuInfo();
RamInfo                 queryRamInfo();
std::vector<MonitorInfo> queryMonitorInfo();
