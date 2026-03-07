#include "HardwareInfo.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <intrin.h>
#endif

#include <algorithm>
#include <string>

// ─── CPU ─────────────────────────────────────────────────────────────────────

CpuInfo queryCpuInfo()
{
    CpuInfo info;

#ifdef _WIN32
    // Brand string via CPUID (leaves 0x80000002..0x80000004)
    int regs[4] = {};
    __cpuid(regs, static_cast<int>(0x80000000u));
    const unsigned maxExt = static_cast<unsigned>(regs[0]);

    if (maxExt >= 0x80000004u)
    {
        char brand[49] = {};
        __cpuid(reinterpret_cast<int*>(brand +  0), static_cast<int>(0x80000002u));
        __cpuid(reinterpret_cast<int*>(brand + 16), static_cast<int>(0x80000003u));
        __cpuid(reinterpret_cast<int*>(brand + 32), static_cast<int>(0x80000004u));
        brand[48] = '\0';
        info.brand = brand;
        auto start = info.brand.find_first_not_of(' ');
        if (start != std::string::npos && start > 0)
            info.brand = info.brand.substr(start);
    }

    // Logical cores
    SYSTEM_INFO si = {};
    GetSystemInfo(&si);
    info.logicalCores = static_cast<int>(si.dwNumberOfProcessors);

    // Physical cores
    DWORD len = 0;
    GetLogicalProcessorInformation(nullptr, &len);
    if (len > 0)
    {
        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buf(len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
        if (GetLogicalProcessorInformation(buf.data(), &len))
        {
            int cores = 0;
            for (const auto& entry : buf)
            {
                if (entry.Relationship == RelationProcessorCore)
                    ++cores;
            }
            info.physicalCores = cores;
        }
    }
#endif

    return info;
}

// ─── RAM ─────────────────────────────────────────────────────────────────────

RamInfo queryRamInfo()
{
    RamInfo info;

#ifdef _WIN32
    MEMORYSTATUSEX ms = {};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms))
    {
        info.totalMB     = static_cast<int64_t>(ms.ullTotalPhys     / (1024ULL * 1024ULL));
        info.availableMB = static_cast<int64_t>(ms.ullAvailPhys     / (1024ULL * 1024ULL));
    }
#endif

    return info;
}

// ─── Monitors ────────────────────────────────────────────────────────────────

#ifdef _WIN32
static std::string wideToUtf8(const wchar_t* wide)
{
    if (!wide || !wide[0]) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), len, nullptr, nullptr);
    return out;
}
#endif

std::vector<MonitorInfo> queryMonitorInfo()
{
    std::vector<MonitorInfo> monitors;

#ifdef _WIN32
    DISPLAY_DEVICEW dd = {};
    dd.cb = sizeof(dd);

    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i)
    {
        if (!(dd.StateFlags & DISPLAY_DEVICE_ACTIVE))
            continue;

        DEVMODEW dm = {};
        dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettingsW(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm))
            continue;

        MonitorInfo mi;
        mi.name        = wideToUtf8(dd.DeviceString);
        mi.width       = static_cast<int>(dm.dmPelsWidth);
        mi.height      = static_cast<int>(dm.dmPelsHeight);
        mi.refreshRate = static_cast<int>(dm.dmDisplayFrequency);
        mi.primary     = (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;

        // DPI scale via device context
        HDC hdc = CreateDCW(dd.DeviceName, nullptr, nullptr, nullptr);
        if (hdc)
        {
            int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
            mi.dpiScale = static_cast<float>(dpiX) / 96.0f;
            DeleteDC(hdc);
        }

        monitors.push_back(std::move(mi));
    }

    // Sort: primary monitor first
    std::stable_sort(monitors.begin(), monitors.end(),
        [](const MonitorInfo& a, const MonitorInfo& b) { return a.primary && !b.primary; });
#endif

    return monitors;
}
