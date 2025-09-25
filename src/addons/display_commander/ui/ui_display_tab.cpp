#include "ui_display_tab.hpp"
#include "../display_cache.hpp"
#include "../globals.hpp"
#include "../settings/main_tab_settings.hpp"
#include <iomanip>
#include <sstream>
#include <vector>
#include <windows.h>

namespace ui {

// Initialize the display cache for the UI
void InitializeDisplayCache() {
    if (!display_cache::g_displayCache.IsInitialized()) {
        display_cache::g_displayCache.Initialize();
    }
}

// Helper function to get monitor information using the display cache
std::vector<std::string> GetMonitorLabelsFromCache() {
    // Initialize display cache if not already initialized
    InitializeDisplayCache();

    std::vector<std::string> labels;

    auto displays = display_cache::g_displayCache.GetDisplays();

    size_t display_count = displays->size();
    labels.reserve(display_count); // No Auto option needed

    // Add monitor options (0-based indexing)
    for (size_t i = 0; i < display_count; ++i) {
        const auto *display = (*displays)[i].get();
        if (display) {
            std::ostringstream oss;

            // Convert friendly name to string for user-friendly display
            std::string friendly_name(display->friendly_name.begin(), display->friendly_name.end());

            // Get high-precision refresh rate with full precision
            double exact_refresh_rate = display->current_refresh_rate.ToHz();
            std::ostringstream rate_oss;
            rate_oss << std::setprecision(10) << exact_refresh_rate;
            std::string rate_str = rate_oss.str();

            // Remove trailing zeros after decimal point but keep meaningful precision
            size_t decimal_pos = rate_str.find('.');
            if (decimal_pos != std::string::npos) {
                size_t last_nonzero = rate_str.find_last_not_of('0');
                if (last_nonzero == decimal_pos) {
                    // All zeros after decimal, remove decimal point too
                    rate_str = rate_str.substr(0, decimal_pos);
                } else if (last_nonzero > decimal_pos) {
                    // Remove trailing zeros but keep some precision
                    rate_str = rate_str.substr(0, last_nonzero + 1);
                }
            }

            // Format: [DeviceID] Friendly Name - Resolution @ PreciseRefreshRateHz [Raw: num/den]
            std::string device_name(display->device_name.begin(), display->device_name.end());
            oss << "[" << device_name << "] " << friendly_name << " - " << display->GetCurrentResolutionString()
                << " @ " << rate_str << "Hz [Raw: " << display->current_refresh_rate.numerator << "/"
                << display->current_refresh_rate.denominator << "]";
            labels.push_back(oss.str());
        }
    }

    return labels;
}

// Helper function to get full device ID from monitor handle
std::string GetFullDeviceIdFromMonitor(HMONITOR monitor) {
    if (monitor == nullptr) {
        return "No Monitor";
    }

    // Get monitor information
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi)) {
        return "Monitor Info Failed";
    }

    // Get the full device ID using EnumDisplayDevices with EDD_GET_DEVICE_INTERFACE_NAME
    DISPLAY_DEVICEW displayDevice;
    ZeroMemory(&displayDevice, sizeof(displayDevice));
    displayDevice.cb = sizeof(displayDevice);

    DWORD deviceIndex = 0;
    while (EnumDisplayDevicesW(NULL, deviceIndex, &displayDevice, 0)) {
        // Check if this is the device we're looking for
        if (wcscmp(displayDevice.DeviceName, mi.szDevice) == 0) {
            // Found the matching device, now get the full device ID
            DISPLAY_DEVICEW monitorDevice;
            ZeroMemory(&monitorDevice, sizeof(monitorDevice));
            monitorDevice.cb = sizeof(monitorDevice);

            DWORD monitorIndex = 0;
            while (EnumDisplayDevicesW(displayDevice.DeviceName, monitorIndex, &monitorDevice,
                                       EDD_GET_DEVICE_INTERFACE_NAME)) {
                // Return the full device ID (DeviceID contains the full path like DISPLAY\AUS32B4\5&24D3239D&1&UID4353)
                if (wcslen(monitorDevice.DeviceID) > 0) {
                    // Convert wide string to UTF-8 string
                    int size =
                        WideCharToMultiByte(CP_UTF8, 0, monitorDevice.DeviceID, -1, nullptr, 0, nullptr, nullptr);
                    if (size > 0) {
                        std::string result(size - 1, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, monitorDevice.DeviceID, -1, &result[0], size, nullptr, nullptr);
                        return result;
                    }
                }
                monitorIndex++;
            }
            break;
        }
        deviceIndex++;
    }

    // Fallback to simple device name if full device ID not found
    int size = WideCharToMultiByte(CP_UTF8, 0, mi.szDevice, -1, nullptr, 0, nullptr, nullptr);
    if (size > 0) {
        std::string result(size - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, mi.szDevice, -1, &result[0], size, nullptr, nullptr);
        return result;
    }

    return "Conversion Failed";
}

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

        // Get the full device ID for this display
        std::string full_device_id = GetFullDeviceIdFromMonitor(display->monitor_handle);
        if (full_device_id == device_id) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

// Function to get the correct monitor index for target monitor selection
int GetTargetMonitorIndex() {
    // Get the saved game window display device ID
    std::string saved_device_id = settings::g_mainTabSettings.game_window_display_device_id.GetValue();

    // Try to find the monitor by device ID
    int monitor_index = FindMonitorIndexByDeviceId(saved_device_id);

    if (monitor_index >= 0) {
        // Found matching monitor by device ID
        return monitor_index; // Direct 0-based index
    }

    // Fallback to first monitor (index 0) if no match found
    return 0;
}

} // namespace ui
