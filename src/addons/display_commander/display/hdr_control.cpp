#include "hdr_control.hpp"
#include "../display_cache.hpp"
#include "../utils/logging.hpp"

#include <windows.h>
#include <wingdi.h>

#include <vector>

namespace display_commander::display::hdr_control {

namespace {

// After we set HDR state, Windows may return stale data from GetDeviceInfo for a short time.
// Cache the value we just set and return it for the same monitor for a brief window.
constexpr unsigned int kHdrSetCacheMs = 2000;
HMONITOR g_last_set_monitor = nullptr;
bool g_last_set_enabled = false;
ULONGLONG g_last_set_ticks = 0;

// Find the DisplayConfig path index and target adapter/id for the given monitor.
// Returns true if a matching path was found; then path_target_adapter_id and path_target_id are set.
bool FindPathForMonitor(HMONITOR monitor, LUID* path_adapter_id, UINT32* path_target_id) {
    if (!monitor || !path_adapter_id || !path_target_id) {
        return false;
    }

    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi)) {
        return false;
    }

    UINT32 path_count = 0, mode_count = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS) {
        return false;
    }
    if (path_count == 0 || mode_count == 0) {
        return false;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);

    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count, modes.data(), nullptr)
        != ERROR_SUCCESS) {
        return false;
    }

    for (UINT32 i = 0; i < path_count; ++i) {
        const auto& path = paths[i];
        if (!(path.flags & DISPLAYCONFIG_PATH_ACTIVE) || !(path.sourceInfo.statusFlags & DISPLAYCONFIG_SOURCE_IN_USE)) {
            continue;
        }

        DISPLAYCONFIG_SOURCE_DEVICE_NAME getSourceName = {};
        getSourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        getSourceName.header.size = sizeof(DISPLAYCONFIG_SOURCE_DEVICE_NAME);
        getSourceName.header.adapterId = path.sourceInfo.adapterId;
        getSourceName.header.id = path.sourceInfo.id;

        if (DisplayConfigGetDeviceInfo(&getSourceName.header) != ERROR_SUCCESS) {
            continue;
        }

        if (wcscmp(getSourceName.viewGdiDeviceName, mi.szDevice) != 0) {
            continue;
        }

        *path_adapter_id = path.sourceInfo.adapterId;
        *path_target_id = path.targetInfo.id;
        return true;
    }

    return false;
}

}  // namespace

bool GetHdrStateForMonitor(HMONITOR monitor, bool* out_supported, bool* out_enabled) {
    if (!monitor || !out_supported || !out_enabled) {
        return false;
    }

    // Return cached value if we recently set HDR for this monitor (Windows can return stale data).
    ULONGLONG now = GetTickCount64();
    if (g_last_set_monitor == monitor && (now - g_last_set_ticks) < kHdrSetCacheMs) {
        *out_supported = true;  // We only set when supported
        *out_enabled = g_last_set_enabled;
        return true;
    }

    LUID adapter_id = {};
    UINT32 target_id = 0;
    if (!FindPathForMonitor(monitor, &adapter_id, &target_id)) {
        return false;
    }

    DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO getColorInfo = {};
    getColorInfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
    getColorInfo.header.size = sizeof(DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO);
    getColorInfo.header.adapterId = adapter_id;
    getColorInfo.header.id = target_id;

    if (DisplayConfigGetDeviceInfo(&getColorInfo.header) != ERROR_SUCCESS) {
        return false;
    }

    *out_supported = (getColorInfo.advancedColorSupported != 0);
    *out_enabled = (getColorInfo.advancedColorEnabled != 0);
    return true;
}

bool SetHdrForMonitor(HMONITOR monitor, bool enable) {
    if (!monitor) {
        return false;
    }

    bool supported = false, current = false;
    if (!GetHdrStateForMonitor(monitor, &supported, &current)) {
        LogWarn("HDR control: could not get HDR state for monitor");
        return false;
    }
    if (!supported) {
        LogInfo("HDR control: display is not HDR capable, skipping set");
        return false;
    }
    if (current == enable) {
        return true;  // Already in desired state
    }

    LUID adapter_id = {};
    UINT32 target_id = 0;
    if (!FindPathForMonitor(monitor, &adapter_id, &target_id)) {
        return false;
    }

    DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE setHdrState = {};
    setHdrState.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
    setHdrState.header.size = sizeof(DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE);
    setHdrState.header.adapterId = adapter_id;
    setHdrState.header.id = target_id;
    setHdrState.enableAdvancedColor = enable ? TRUE : FALSE;

    LONG result = DisplayConfigSetDeviceInfo(reinterpret_cast<DISPLAYCONFIG_DEVICE_INFO_HEADER*>(&setHdrState));
    if (result != ERROR_SUCCESS) {
        LogWarn("HDR control: DisplayConfigSetDeviceInfo failed with %ld", result);
        return false;
    }

    g_last_set_monitor = monitor;
    g_last_set_enabled = enable;
    g_last_set_ticks = GetTickCount64();

    LogInfo("HDR control: Windows HDR %s for display", enable ? "enabled" : "disabled");
    return true;
}

bool GetHdrStateForDisplayIndex(int display_index, bool* out_supported, bool* out_enabled) {
    if (!out_supported || !out_enabled) {
        return false;
    }

    const auto* display = display_cache::g_displayCache.GetDisplay(display_index);
    if (!display) {
        return false;
    }

    return GetHdrStateForMonitor(display->monitor_handle, out_supported, out_enabled);
}

bool SetHdrForDisplayIndex(int display_index, bool enable) {
    const auto* display = display_cache::g_displayCache.GetDisplay(display_index);
    if (!display) {
        return false;
    }

    return SetHdrForMonitor(display->monitor_handle, enable);
}

}  // namespace display_commander::display::hdr_control
