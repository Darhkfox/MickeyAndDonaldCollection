#include "monitors.h"
#include <algorithm>
#include <map>

namespace {

std::string narrow(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// GDI device name -> monitor's friendly name (EDID), via the display configuration API.
std::map<std::wstring, std::wstring> friendly_names() {
    std::map<std::wstring, std::wstring> names;
    UINT32 npaths = 0, nmodes = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &npaths, &nmodes) != ERROR_SUCCESS) return names;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(npaths);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(nmodes);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &npaths, paths.data(), &nmodes, modes.data(), nullptr) != ERROR_SUCCESS)
        return names;
    for (UINT32 i = 0; i < npaths; i++) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src = {};
        src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        src.header.size = sizeof(src);
        src.header.adapterId = paths[i].sourceInfo.adapterId;
        src.header.id = paths[i].sourceInfo.id;
        DISPLAYCONFIG_TARGET_DEVICE_NAME tgt = {};
        tgt.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        tgt.header.size = sizeof(tgt);
        tgt.header.adapterId = paths[i].targetInfo.adapterId;
        tgt.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&src.header) == ERROR_SUCCESS &&
            DisplayConfigGetDeviceInfo(&tgt.header) == ERROR_SUCCESS && tgt.monitorFriendlyDeviceName[0])
            names[src.viewGdiDeviceName] = tgt.monitorFriendlyDeviceName;
    }
    return names;
}

BOOL CALLBACK enum_proc(HMONITOR mon, HDC, LPRECT, LPARAM user) {
    auto* list = (std::vector<MonitorInfo>*)user;
    MONITORINFOEXW mi;
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
        MonitorInfo m;
        m.handle = mon;
        m.device = mi.szDevice;
        m.rect = mi.rcMonitor;
        m.work = mi.rcWork;
        m.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        list->push_back(m);
    }
    return TRUE;
}

}  // namespace

std::vector<MonitorInfo> enum_monitors() {
    std::vector<MonitorInfo> list;
    EnumDisplayMonitors(nullptr, nullptr, enum_proc, (LPARAM)&list);
    // Primary first, then left to right.
    std::stable_sort(list.begin(), list.end(), [](const MonitorInfo& a, const MonitorInfo& b) {
        if (a.primary != b.primary) return a.primary;
        return a.rect.left < b.rect.left;
    });
    std::map<std::wstring, std::wstring> names = friendly_names();
    for (size_t i = 0; i < list.size(); i++) {
        MonitorInfo& m = list[i];
        auto it = names.find(m.device);
        std::string name = it != names.end() ? narrow(it->second) : "Display";
        m.label = std::to_string(i + 1) + ": " + name + " (" + std::to_string(m.rect.right - m.rect.left) + "x" +
                  std::to_string(m.rect.bottom - m.rect.top) + ")" + (m.primary ? " - primary" : "");
    }
    return list;
}

int find_monitor(const std::vector<MonitorInfo>& list, const std::wstring& device) {
    for (size_t i = 0; i < list.size(); i++)
        if (list[i].device == device) return (int)i;
    for (size_t i = 0; i < list.size(); i++)
        if (list[i].primary) return (int)i;
    return 0;
}
