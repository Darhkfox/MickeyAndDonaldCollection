// Display enumeration for the monitor picker.
#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct MonitorInfo {
    HMONITOR handle;
    std::wstring device;      // GDI device name, e.g. \\.\DISPLAY2 (stable id stored in settings)
    std::string label;        // "1: DELL U2720Q (2560x1440)"
    RECT rect;                // full monitor area
    RECT work;                // work area (without the taskbar)
    bool primary;
};

std::vector<MonitorInfo> enum_monitors();
// Index of the monitor with the given device name, or of the primary monitor if not found.
int find_monitor(const std::vector<MonitorInfo>& list, const std::wstring& device);
