#include "ui_display_tab.hpp"
#include "../display_cache.hpp"
#include "../globals.hpp"
#include <windows.h>
#include <vector>
#include <sstream>
#include <iomanip>


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
    labels.reserve(display_count + 1); // +1 for Auto (Current) option

        // Add Auto (Current) as the first option (index 0)
    std::string auto_label = "Auto (Current)";
    HWND hwnd = g_last_swapchain_hwnd.load();
    if (hwnd) {
        HMONITOR current_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        if (current_monitor) {
            // Get current resolution and refresh rate from display cache
            const auto* display = display_cache::g_displayCache.GetDisplayByHandle(current_monitor);
            if (display) {
                int width = display->width;
                int height = display->height;
                double refresh_rate = display->current_refresh_rate.ToHz();

                // Use cached primary monitor flag and device name
                bool is_primary = display->is_primary;
                std::string primary_text = is_primary ? " Primary" : "";
                std::string device_name(display->device_name.begin(), display->device_name.end());

                std::ostringstream rate_oss;
                rate_oss << std::fixed << std::setprecision(3) << refresh_rate;

                auto_label = "Auto (Current) [" + device_name + "] " +
                            std::to_string(width) + "x" + std::to_string(height) +
                            " @ " + rate_oss.str() + "Hz" + primary_text;
            } else {
                auto_label = "Failed to get display from cache";
            }
        }
    }
    labels.push_back(auto_label);

    // Add the regular monitor options (starting from index 1)
    for (size_t i = 0; i < display_count; ++i) {
            const auto* display = (*displays)[i].get();
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





} // namespace ui
