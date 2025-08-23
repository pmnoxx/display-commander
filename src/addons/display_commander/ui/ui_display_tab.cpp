#include "ui_display_tab.hpp"
#include "ui_common.hpp"
#include "monitor_settings/monitor_settings.hpp"
#include "../renodx/settings.hpp"
#include "../addon.hpp"
#include "../display_cache.hpp"
#include <windows.h>
#include <vector>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <atomic>

// External variables
extern float s_selected_monitor_index;
extern float s_selected_resolution_index;
extern float s_selected_refresh_rate_index;
extern bool s_initial_auto_selection_done;

namespace renodx::ui {

// Initialize the display cache for the UI
void InitializeDisplayCache() {
    if (!renodx::display_cache::g_displayCache.IsInitialized()) {
        renodx::display_cache::g_displayCache.Initialize();
    }
}

// Helper function to get monitor information using the display cache
std::vector<std::string> GetMonitorLabelsFromCache() {
    std::vector<std::string> labels;
    
    if (!renodx::display_cache::g_displayCache.IsInitialized()) {
        renodx::display_cache::g_displayCache.Initialize();
    }
    
    size_t display_count = renodx::display_cache::g_displayCache.GetDisplayCount();
    labels.reserve(display_count + 1); // +1 for Auto (Current) option
    
    // Add Auto (Current) as the first option (index 0)
    std::string auto_label = "Auto (Current)";
    HWND hwnd = g_last_swapchain_hwnd.load();
    if (hwnd) {
        HMONITOR current_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        if (current_monitor) {
            MONITORINFOEXW mi;
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(current_monitor, &mi)) {
                std::string device_name(mi.szDevice, mi.szDevice + wcslen(mi.szDevice));
                
                // Get current resolution and refresh rate
                DEVMODEW dm;
                dm.dmSize = sizeof(dm);
                if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
                    int width = static_cast<int>(dm.dmPelsWidth);
                    int height = static_cast<int>(dm.dmPelsHeight);
                    int refresh_rate = static_cast<int>(dm.dmDisplayFrequency);
                    
                    // Check if it's primary monitor
                    bool is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
                    std::string primary_text = is_primary ? " Primary" : "";
                    
                    auto_label = "Auto (Current) [" + device_name + "] " + 
                                std::to_string(width) + "x" + std::to_string(height) + 
                                " @ " + std::to_string(refresh_rate) + "Hz" + primary_text;
                } else {
                    // Fallback if we can't get display settings
                    bool is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
                    std::string primary_text = is_primary ? " Primary" : "";
                    auto_label = "Auto (Current) [" + device_name + "]" + primary_text;
                }
            }
        }
    }
    labels.push_back(auto_label);
    
    // Add the regular monitor options (starting from index 1)
    for (size_t i = 0; i < display_count; ++i) {
            const auto* display = renodx::display_cache::g_displayCache.GetDisplay(i);
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
                    << " @ " << rate_str << "Hz [Raw: " 
                    << display->current_refresh_rate.numerator << "/" 
                    << display->current_refresh_rate.denominator << "]";
                labels.push_back(oss.str());
            }
        }
    
    return labels;
}

// Helper function to get max monitor index using the display cache
float GetMaxMonitorIndexFromCache() {
    if (!renodx::display_cache::g_displayCache.IsInitialized()) {
        renodx::display_cache::g_displayCache.Initialize();
    }
    
    size_t display_count = renodx::display_cache::g_displayCache.GetDisplayCount();
    // +1 because we now have Auto (Current) as index 0, so monitors start at index 1
    return static_cast<float>((std::max)(0, static_cast<int>(display_count)));
}

// Helper function to get current display info based on game position using the display cache
std::string GetCurrentDisplayInfoFromCache() {
    HWND hwnd = g_last_swapchain_hwnd.load();
    
    if (!hwnd) {
        return "No game window detected";
    }
    
    // Get window position
    RECT window_rect;
    if (!GetWindowRect(hwnd, &window_rect)) {
        return "Failed to get window position";
    }
    
    // Find which monitor the game is running on
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!monitor) {
        return "Failed to determine monitor";
    }
    
    // Use the display cache to get monitor information
    if (!renodx::display_cache::g_displayCache.IsInitialized()) {
        renodx::display_cache::g_displayCache.Initialize();
    }
    
    const auto* display = renodx::display_cache::g_displayCache.GetDisplayByHandle(monitor);
    if (!display) {
        return "Failed to get display info from cache";
    }
    
    // Use the new comprehensive display info method that shows current vs supported modes
    std::string friendly_name(display->friendly_name.begin(), display->friendly_name.end());
    std::ostringstream oss;
    oss << friendly_name << " - " << display->GetCurrentDisplayInfoString();
    return oss.str();
}

// Legacy functions for backward compatibility (can be removed later)
std::vector<std::string> GetMonitorLabels() {
    return GetMonitorLabelsFromCache();
}

float GetMaxMonitorIndex() {
    return GetMaxMonitorIndexFromCache();
}

std::string GetCurrentDisplayInfo() {
    return GetCurrentDisplayInfoFromCache();
}

// Handle monitor settings UI (extracted from on_draw lambda)
bool HandleMonitorSettingsUI() {
    // Handle display cache refresh logic (every 60 frames)
    renodx::ui::monitor_settings::HandleDisplayCacheRefresh();
    
    // Get current monitor labels (now with precise refresh rates and raw rational values)
    auto monitor_labels = GetMonitorLabelsFromCache();
            if (monitor_labels.empty()) {
                ImGui::Text("No monitors detected");
                return false;
            }
            
        // Handle auto-detection of current display settings
    renodx::ui::monitor_settings::HandleAutoDetection();
    
        // Handle monitor selection UI
    renodx::ui::monitor_settings::HandleMonitorSelection(monitor_labels);
    
    // Handle resolution selection UI
    renodx::ui::monitor_settings::HandleResolutionSelection(static_cast<int>(s_selected_monitor_index));
    
    // Handle refresh rate selection UI
    renodx::ui::monitor_settings::HandleRefreshRateSelection(static_cast<int>(s_selected_monitor_index), static_cast<int>(s_selected_resolution_index));

    // Handle auto-restore resolution checkbox
    renodx::ui::monitor_settings::HandleAutoRestoreResolutionCheckbox();

    // Inline auto-apply checkboxes are now rendered next to their respective combos

    // Handle the DXGI API Apply Button
    renodx::ui::monitor_settings::HandleDXGIAPIApplyButton();
    
    // While a resolution change is pending confirmation, show confirm/revert UI
    renodx::ui::monitor_settings::HandlePendingConfirmationUI();
    
    return false; // No value change
}


} // namespace renodx::ui
