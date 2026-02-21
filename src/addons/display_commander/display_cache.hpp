#pragma once

#include <algorithm> // Added for std::max_element
#include <atomic>
#include <cmath>   // Added for std::round
#include <iomanip> // Added for std::setprecision
#include <memory>
#include <optional> // std::nullopt
#include <sstream>  // Added for std::ostringstream
#include <string>
#include <vector>
#include <windows.h>


// Windows DPI awareness headers
#include <shellscalingapi.h>
#pragma comment(lib, "shcore.lib")

namespace display_cache {

// Rational refresh rate structure
struct RationalRefreshRate {
    UINT32 numerator;
    UINT32 denominator;

    RationalRefreshRate() : numerator(0), denominator(1) {}
    RationalRefreshRate(UINT32 num, UINT32 den) : numerator(num), denominator(den) {}

    // Convert to double for display
    double ToHz() const {
        if (denominator == 0)
            return 0.0;
        return static_cast<double>(numerator) / static_cast<double>(denominator);
    }

    // Convert to string representation
    std::string ToString() const {
        if (denominator == 0)
            return "0Hz";

        double hz = ToHz();
        std::ostringstream oss;
        oss << std::setprecision(10) << hz;
        std::string rate_str = oss.str();

        // Remove trailing zeros after decimal point
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

        return rate_str + "Hz";
    }

    // Comparison operators for sorting
    bool operator<(const RationalRefreshRate &other) const { return ToHz() < other.ToHz(); }

    bool operator==(const RationalRefreshRate &other) const {
        return numerator == other.numerator && denominator == other.denominator;
    }
};

// Resolution structure
struct Resolution {
    int width;
    int height;
    std::vector<RationalRefreshRate> refresh_rates;

    Resolution() : width(0), height(0) {}
    Resolution(int w, int h) : width(w), height(h) {}

    // Convert to string representation
    std::string ToString() const {
        std::ostringstream oss;
        oss << width << " x " << height;

        // Calculate and add aspect ratio
        if (height > 0) {
            double aspect_ratio = static_cast<double>(width) / static_cast<double>(height);
            // Round to 2 decimal places and format as "X : 9" where X is the ratio * 9
            double ratio_numerator = aspect_ratio * 9.0;
            if (std::abs(ratio_numerator - std::round(ratio_numerator)) < 0.005) {
                oss << " (" << static_cast<int>(std::round(ratio_numerator)) << ":9)";
            } else {
                oss << " (" << std::fixed << std::setprecision(2) << ratio_numerator << ":9)";
            }
        }

        return oss.str();
    }

    // Comparison operators for sorting
    bool operator<(const Resolution &other) const {
        if (width != other.width)
            return width < other.width;
        return height < other.height;
    }

    bool operator==(const Resolution &other) const { return width == other.width && height == other.height; }
};

// Display information structure
struct DisplayInfo {
    HMONITOR monitor_handle = nullptr;
    std::wstring simple_device_id;
    // TODO add extended_device_id        info.extended_device_id = GetExtendedDeviceIdFromMonitor(display->monitor_handle);
    std::wstring friendly_name;
    std::vector<Resolution> resolutions;

    // Current settings
    int width = 0;
    int height = 0;
    RationalRefreshRate current_refresh_rate;
    int x = 0;
    int y = 0;

    // Monitor properties from MONITORINFOEXW
    bool is_primary = false;
    RECT monitor_rect; // Monitor rectangle in virtual screen coordinates
    RECT work_rect;    // Working area rectangle (excluding taskbar, etc.)

    DisplayInfo() {
        monitor_rect = {0, 0, 0, 0};
        work_rect = {0, 0, 0, 0};
    }

    // Get current resolution as string
    std::string GetCurrentResolutionString() const {
        std::ostringstream oss;
        oss << width << "x" << height;
        return oss.str();
    }

    // Get current refresh rate as string
    std::string GetCurrentRefreshRateString() const { return current_refresh_rate.ToString(); }

    // Get DPI scaling factor for this display
    float GetDpiScaling() const {
        if (!monitor_handle) {
            return 0.0f;
        }

        // Use GetDpiForMonitor API to get effective DPI
        typedef HRESULT(WINAPI * GetDpiForMonitorFunc)(HMONITOR, int, UINT *, UINT *);
        static GetDpiForMonitorFunc GetDpiForMonitor = nullptr;

        if (!GetDpiForMonitor) {
            HMODULE shcore = LoadLibraryA("shcore.dll");
            if (shcore) {
                GetDpiForMonitor = (GetDpiForMonitorFunc)GetProcAddress(shcore, "GetDpiForMonitor");
            }
        }

        if (GetDpiForMonitor) {
            UINT dpi_x = 0, dpi_y = 0;
            HRESULT hr = GetDpiForMonitor(monitor_handle, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y);
            if (SUCCEEDED(hr) && dpi_x > 0) {
                return static_cast<float>(dpi_x) / 96.0f; // 96 is the standard DPI
            }
        }

        /*
        // Fallback: try to get DPI from device context
        HDC hdc = GetDC(nullptr);
        if (hdc) {
            int dpi_x = GetDeviceCaps(hdc, LOGPIXELSX);
            int dpi_y = GetDeviceCaps(hdc, LOGPIXELSY);
            ReleaseDC(nullptr, hdc);
            if (dpi_x > 0) {
                return static_cast<float>(dpi_x) / 96.0f;
            }
        }*/

        return 0.0f; // Unknown scaling
    }

    // Get comprehensive current display info string
    std::string GetCurrentDisplayInfoString() const {
        std::ostringstream oss;
        oss << "Current: " << GetCurrentResolutionString() << " @ " << GetCurrentRefreshRateString();

        // Add DPI scaling information
        float dpi_scale = GetDpiScaling();
        if (dpi_scale > 0.0f) {
            oss << " | Scaling: " << std::fixed << std::setprecision(0) << (dpi_scale * 100.0f) << "%";
        }

        // Debug: Show raw refresh rate values
        oss << " [Raw: " << current_refresh_rate.numerator << "/" << current_refresh_rate.denominator << " = "
            << std::setprecision(10) << current_refresh_rate.ToHz() << "Hz]";

        return oss.str();
    }

    // Find resolution by dimensions
    std::optional<size_t> FindResolutionIndex(int width, int height) const {
        for (size_t i = 0; i < resolutions.size(); ++i) {
            if (resolutions[i].width == width && resolutions[i].height == height) {
                return i;
            }
        }
        return std::nullopt;
    }

    // Find refresh rate index within a resolution
    std::optional<size_t> FindRefreshRateIndex(size_t resolution_index, const RationalRefreshRate &refresh_rate) const {
        if (resolution_index >= resolutions.size())
            return std::nullopt;

        const auto &res = resolutions[resolution_index];
        for (size_t i = 0; i < res.refresh_rates.size(); ++i) {
            if (res.refresh_rates[i] == refresh_rate) {
                return i;
            }
        }
        return std::nullopt;
    }

    // Find the closest supported resolution to current settings
    std::optional<size_t> FindClosestResolutionIndex() const {
        if (resolutions.empty())
            return std::nullopt;

        // Find exact match first
        for (size_t i = 0; i < resolutions.size(); ++i) {
            if (resolutions[i].width == width && resolutions[i].height == height) {
                return i;
            }
        }

        // If no exact match, find closest by area
        size_t closest_index = 0;
        int current_area = width * height;
        int min_diff = std::abs(resolutions[0].width * resolutions[0].height - current_area);

        for (size_t i = 1; i < resolutions.size(); ++i) {
            int area = resolutions[i].width * resolutions[i].height;
            int diff = std::abs(area - current_area);
            if (diff < min_diff) {
                min_diff = diff;
                closest_index = i;
            }
        }

        return closest_index;
    }

    // Find the closest supported refresh rate within a resolution to current refresh rate
    std::optional<size_t> FindClosestRefreshRateIndex(size_t resolution_index) const {
        if (resolution_index >= resolutions.size())
            return std::nullopt;

        const auto &res = resolutions[resolution_index];
        if (res.refresh_rates.empty())
            return std::nullopt;

        // Find exact match first
        for (size_t i = 0; i < res.refresh_rates.size(); ++i) {
            if (res.refresh_rates[i] == current_refresh_rate) {
                return i;
            }
        }

        // If no exact match, find closest by frequency
        size_t closest_index = 0;
        double current_hz = current_refresh_rate.ToHz();
        double min_diff = std::abs(res.refresh_rates[0].ToHz() - current_hz);

        for (size_t i = 1; i < res.refresh_rates.size(); ++i) {
            double hz = res.refresh_rates[i].ToHz();
            double diff = std::abs(hz - current_hz);
            if (diff < min_diff) {
                min_diff = diff;
                closest_index = i;
            }
        }

        return closest_index;
    }

    // Get resolution labels for UI
    std::vector<std::string> GetResolutionLabels() const {
        std::vector<std::string> labels;
        labels.reserve(resolutions.size() + 1);

        // Option 0: Current Resolution
        labels.push_back(std::string("Current Resolution (") + GetCurrentResolutionString() + ")");

        for (const auto &res : resolutions) {
            labels.push_back(res.ToString());
        }

        return labels;
    }

    // Get refresh rate labels for a specific resolution
    std::vector<std::string> GetRefreshRateLabels(size_t resolution_index) const {
        // Map UI index to underlying resolution index
        size_t effective_index = resolution_index;
        if (resolution_index == 0) {
            auto idx = FindResolutionIndex(width, height);
            if (!idx.has_value())
                return {};
            effective_index = idx.value();
        } else {
            // Shift down by one since 0 is "Current Resolution"
            if ((resolution_index - 1) >= resolutions.size())
                return {};
            effective_index = resolution_index - 1;
        }

        const auto &res = resolutions[effective_index];
        std::vector<std::string> labels;
        // +2 for option 0 (Current) and option 1 (Max supported)
        labels.reserve(res.refresh_rates.size() + 2);

        // Add option 0: "Current Refresh Rate"
        labels.push_back(std::string("Current Refresh Rate (") + current_refresh_rate.ToString() + ")");

        // Add option 1: "Max supported refresh rate"
        if (!res.refresh_rates.empty()) {
            // Find the maximum refresh rate
            auto max_rate = std::max_element(res.refresh_rates.begin(), res.refresh_rates.end());
            if (max_rate != res.refresh_rates.end()) {
                labels.push_back("Max supported refresh rate (" + max_rate->ToString() + ")");
            } else {
                labels.push_back("Max supported refresh rate");
            }
        }

        // Add all available refresh rates
        for (const auto &rate : res.refresh_rates) {
            labels.push_back(rate.ToString());
        }

        return labels;
    }
};

// Display information structure for UI consumption
struct DisplayInfoForUI {
    std::string extended_device_id;  // Unique device identifier (e.g., "\\?\DISPLAY#AUS32B4#5&24d3239d&1&UID4353#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}")
    std::string simple_device_id;    // Simple device identifier for display (e.g., "DISPLAY1", "DISPLAY2")
    std::string friendly_name;       // Human-readable name
    std::string current_resolution;  // Current resolution string
    std::string current_refresh_rate; // Current refresh rate string
    std::string display_label;       // Formatted label for UI display
    bool is_primary;                 // Whether this is the primary display
    HMONITOR monitor_handle;         // For matching with current window
    int display_index;               // Current index in the display array (for backward compatibility)

    DisplayInfoForUI() : is_primary(false), monitor_handle(nullptr), display_index(-1) {}
};

// Main display cache class
class DisplayCache {
  private:
    std::atomic<std::shared_ptr<std::vector<std::unique_ptr<DisplayInfo>>>> displays;
    std::atomic<bool> is_initialized;

  public:
    DisplayCache() : displays(std::make_shared<std::vector<std::unique_ptr<DisplayInfo>>>()), is_initialized(false) {}

    // Initialize the cache by enumerating all displays
    bool Initialize();

    // Refresh the cache (re-enumerate displays)
    bool Refresh();

    // Get number of displays
    size_t GetDisplayCount() const;

    // Get all displays
    std::shared_ptr<std::vector<std::unique_ptr<DisplayInfo>>> GetDisplays() const;

    // Get display by index
    const DisplayInfo *GetDisplay(size_t index) const;

    // Get display by monitor handle
    const DisplayInfo *GetDisplayByHandle(HMONITOR monitor) const;

    // Get display by device name
    const DisplayInfo *GetDisplayByDeviceName(const std::wstring &device_name) const;

    // Get display index by device name (returns -1 if not found)
    int GetDisplayIndexByDeviceName(const std::string &device_name) const;

    // Get extended device ID from monitor handle
    std::string GetExtendedDeviceIdFromMonitor(HMONITOR monitor) const;

    // Get device ID of the display to the left (to_the_left true) or right (to_the_left false) of the current one.
    // Displays are ordered by monitor_rect.left. Returns empty string if no adjacent display.
    std::string GetAdjacentDisplayDeviceId(const std::string& current_device_id, bool to_the_left) const;

    // Get resolution labels for a specific display
    std::vector<std::string> GetResolutionLabels(size_t display_index) const;

    // Get refresh rate labels for a specific display and resolution
    std::vector<std::string> GetRefreshRateLabels(size_t display_index, size_t resolution_index) const;

    // Get monitor labels for all displays (formatted for UI display)
    std::vector<std::string> GetMonitorLabels() const;

    // Get display information for UI consumption (device ID based)
    std::vector<DisplayInfoForUI> GetDisplayInfoForUI() const;

    // Get display info by device ID (returns nullptr if not found)
    const DisplayInfoForUI* GetDisplayInfoByDeviceId(const std::string& device_id) const;

    // Get current resolution for a display
    bool GetCurrentResolution(size_t display_index, int &width, int &height) const;

    // Get current refresh rate for a display
    bool GetCurrentRefreshRate(size_t display_index, RationalRefreshRate &refresh_rate) const;

    // Get rational refresh rate for a specific display, resolution, and refresh rate index
    bool GetRationalRefreshRate(size_t display_index, size_t resolution_index, size_t refresh_rate_index,
                                RationalRefreshRate &refresh_rate) const;

    // Get current display info (current settings, not supported modes)
    bool GetCurrentDisplayInfo(size_t display_index, int &width, int &height, RationalRefreshRate &refresh_rate) const;

    // Get supported modes info (what the display can do)
    bool GetSupportedModes(size_t display_index, std::vector<Resolution> &resolutions) const;

    // Get maximum refresh rate across all monitors
    double GetMaxRefreshRateAcrossAllMonitors() const;

    // Check if cache is initialized
    bool IsInitialized() const { return is_initialized.load(std::memory_order_acquire); }

    // Clear the cache
    void Clear() {
        displays.store(std::make_shared<std::vector<std::unique_ptr<DisplayInfo>>>(), std::memory_order_release);
        is_initialized.store(false, std::memory_order_release);
    }

    // Swap internal data from another cache (used for atomic-like updates)
    void SwapFrom(DisplayCache &&other);

    // Copy a display snapshot by index; returns false if out of range
    bool CopyDisplay(size_t index, DisplayInfo &out) const;

    // Print vSyncFreqDivider information for debugging
    void PrintVSyncFreqDivider() const;
};

// Global instance
extern DisplayCache g_displayCache;

} // namespace display_cache
