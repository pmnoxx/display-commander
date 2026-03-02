#include "ui_display_tab.hpp"
#include "display/display_cache.hpp"
#include "../globals.hpp"
#include <iomanip>
#include <sstream>
#include <vector>
#include <windows.h>

namespace ui {

// Function to find monitor index by device ID
int FindMonitorIndexByDeviceId(const std::string &device_id) {
    if (device_id.empty() || device_id == "No Window" || device_id == "No Monitor" ||
        device_id == "Monitor Info Failed") {
        return -1;
    }

    // First try to match by simple device name (e.g., \\.\DISPLAY1)
    int index = display_cache::g_displayCache.GetDisplayIndexByDeviceName(device_id);
    if (index >= 0) {
        return index;
    }

    // If that fails, try to match by full device ID
    // The full device ID format is like DISPLAY\AUS32B4\5&24D3239D&1&UID4353
    // We need to find the monitor that corresponds to this full device ID
    auto displays_ptr = display_cache::g_displayCache.GetDisplays();
    if (!displays_ptr)
        return -1;

    // Convert the full device ID to wide string for comparison
    std::wstring wdevice_id(device_id.begin(), device_id.end());

    // For each display, try to get its full device ID and compare
    for (size_t i = 0; i < displays_ptr->size(); ++i) {
        const auto &display = (*displays_ptr)[i];
        if (!display)
            continue;

        // Get the extended device ID for this display
        std::string full_device_id = display_cache::g_displayCache.GetExtendedDeviceIdFromMonitor(display->monitor_handle);
        if (full_device_id == device_id) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

} // namespace ui
