#include "display_restore.hpp"
#include "display_cache.hpp"
#include "display_initial_state.hpp"
#include "globals.hpp"
#include "utils.hpp"
#include "utils/logging.hpp"
#include <atomic>
#include <map>
#include <memory>
#include <set>

namespace display_restore {

namespace {

struct OriginalMode {
    int width = 0;
    int height = 0;
    UINT32 refresh_num = 0;
    UINT32 refresh_den = 1;
};

struct DisplayRestoreData {
    std::map<std::wstring, OriginalMode> device_to_original; // device name -> original mode
    std::set<std::wstring> devices_changed;                  // devices we modified
};

std::atomic<std::shared_ptr<const DisplayRestoreData>> s_data{std::make_shared<DisplayRestoreData>()};

bool GetCurrentForDevice(const std::wstring &extended_device_id, OriginalMode &out) {
    // Walk display cache for this device
    const auto &cache = display_cache::g_displayCache;
    for (size_t i = 0; i < cache.GetDisplayCount(); ++i) {
        const auto *disp = cache.GetDisplay(i);
        if (disp == nullptr)
            continue;
        if (disp->simple_device_id == extended_device_id) {
            out.width = disp->width;
            out.height = disp->height;
            out.refresh_num = disp->current_refresh_rate.numerator;
            out.refresh_den = disp->current_refresh_rate.denominator == 0 ? 1 : disp->current_refresh_rate.denominator;
            return true;
        }
    }
    return false;
}

bool GetDeviceNameForMonitor(HMONITOR monitor, std::wstring &out_name) {
    MONITORINFOEXW mi;
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(monitor, &mi) == FALSE)
        return false;
    out_name = mi.szDevice;
    return true;
}

bool ApplyModeForDevice(const std::wstring &extended_device_id, const OriginalMode &mode) {
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
    dm.dmPelsWidth = static_cast<DWORD>(mode.width);
    dm.dmPelsHeight = static_cast<DWORD>(mode.height);
    // Round if rational
    double hz =
        mode.refresh_den == 0 ? 0.0 : static_cast<double>(mode.refresh_num) / static_cast<double>(mode.refresh_den);
    if (hz <= 0.0) {
        // Fallback to current registry frequency; leave frequency field unset
        dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
    } else {
        dm.dmDisplayFrequency = static_cast<DWORD>(hz + 0.5);
    }
    // Try with CDS_UPDATEREGISTRY as fallback
    LogInfo("ApplyModeForDevice() - ChangeDisplaySettingsExW: %S", extended_device_id.c_str());
    LONG res = ChangeDisplaySettingsExW(extended_device_id.c_str(), &dm, nullptr, 0, nullptr);
    if (res == DISP_CHANGE_SUCCESSFUL)
        return true;
    res = ChangeDisplaySettingsExW(extended_device_id.c_str(), &dm, nullptr, CDS_UPDATEREGISTRY, nullptr);
    return res == DISP_CHANGE_SUCCESSFUL;
}

} // anonymous namespace

// use ::s_auto_restore_resolution_on_close from globals.cpp via addon.hpp
// include

void MarkOriginalForMonitor(HMONITOR monitor) {
    std::wstring device;
    if (!GetDeviceNameForMonitor(monitor, device))
        return;
    MarkOriginalForDeviceName(device);
}

void MarkOriginalForDeviceName(const std::wstring &device_name) {
    auto current_data = s_data.load();
    if (current_data->device_to_original.find(device_name) != current_data->device_to_original.end())
        return;

    OriginalMode mode{};
    if (GetCurrentForDevice(device_name, mode)) {
        auto new_data = std::make_shared<DisplayRestoreData>(*current_data);
        new_data->device_to_original.emplace(device_name, mode);
        s_data.store(new_data);
    }
}

void MarkOriginalForDisplayIndex(int display_index) {
    if (display_index < 0)
        return;
    const auto *disp = display_cache::g_displayCache.GetDisplay(static_cast<size_t>(display_index));
    if (disp == nullptr)
        return;
    MarkOriginalForDeviceName(disp->simple_device_id);
}

void MarkDeviceChangedByDisplayIndex(int display_index) {
    if (display_index < 0)
        return;
    const auto *disp = display_cache::g_displayCache.GetDisplay(static_cast<size_t>(display_index));
    if (disp == nullptr)
        return;
    MarkDeviceChangedByDeviceName(disp->simple_device_id);
}

void MarkDeviceChangedByDeviceName(const std::wstring &device_name) {
    auto current_data = s_data.load();
    auto new_data = std::make_shared<DisplayRestoreData>(*current_data);

    // Ensure we have original captured first
    if (new_data->device_to_original.find(device_name) == new_data->device_to_original.end()) {
        OriginalMode mode{};
        if (GetCurrentForDevice(device_name, mode)) {
            new_data->device_to_original.emplace(device_name, mode);
        }
    }
    new_data->devices_changed.insert(device_name);
    s_data.store(new_data);
}

// (ApplyModeForDevice is in anonymous namespace above)

void RestoreAll() {
    auto current_data = s_data.load();

    if (current_data->devices_changed.empty()) {
        LogInfo("RestoreAll: No devices were changed, nothing to restore");
        return;
    }

    LogInfo("RestoreAll: Restoring %zu changed devices", current_data->devices_changed.size());

    for (const auto &device_name : current_data->devices_changed) {
        auto it = current_data->device_to_original.find(device_name);
        if (it == current_data->device_to_original.end()) {
            LogWarn("RestoreAll: No original mode found for device %S, skipping", device_name.c_str());
            continue;
        }

        const auto &original_mode = it->second;
        LogInfo("RestoreAll: Restoring %S to %dx%d @ %u/%u", device_name.c_str(),
                original_mode.width, original_mode.height,
                original_mode.refresh_num, original_mode.refresh_den);

        if (ApplyModeForDevice(device_name, original_mode)) {
            LogInfo("RestoreAll: Successfully restored %S", device_name.c_str());
        } else {
            LogError("RestoreAll: Failed to restore %S", device_name.c_str());
        }
    }
}

void RestoreAllIfEnabled() {
    if (!::s_auto_restore_resolution_on_close)
        return;
    // Only restore if resolution was successfully applied at least once
    if (!::s_resolution_applied_at_least_once.load()) {
        LogInfo("RestoreAllIfEnabled: Skipping restore because resolution was never applied");
        return;
    }
    RestoreAll();
}

void Clear() {
    s_data.store(std::make_shared<DisplayRestoreData>());
    // Also clear the initial display state
    display_initial_state::g_initialDisplayState.Clear();
}

bool HasAnyChanges() {
    auto current_data = s_data.load();
    return ::s_auto_restore_resolution_on_close && !current_data->devices_changed.empty();
}

bool RestoreDisplayByDeviceName(const std::wstring &device_name) {
    auto current_data = s_data.load();

    auto it = current_data->device_to_original.find(device_name);
    if (it == current_data->device_to_original.end()) {
        LogWarn("RestoreDisplayByDeviceName: No original mode found for device %S", device_name.c_str());
        return false;
    }

    const auto &original_mode = it->second;
    LogInfo("RestoreDisplayByDeviceName: Restoring %S to %dx%d @ %u/%u", device_name.c_str(),
            original_mode.width, original_mode.height,
            original_mode.refresh_num, original_mode.refresh_den);

    return ApplyModeForDevice(device_name, original_mode);
}

bool RestoreDisplayByIndex(int display_index) {
    if (display_index < 0)
        return false;
    const auto *disp = display_cache::g_displayCache.GetDisplay(static_cast<size_t>(display_index));
    if (disp == nullptr)
        return false;
    return RestoreDisplayByDeviceName(disp->simple_device_id);
}

} // namespace display_restore
