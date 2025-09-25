#pragma once

#include "../display_cache.hpp"

#include <memory>
#include <string>

namespace display_commander::settings {

// Forward declaration
class DisplaySettings;

// Global instance
extern std::unique_ptr<DisplaySettings> g_display_settings;

class DisplaySettings {
  public:
    // Settings - using shared_ptr for thread safety
    std::shared_ptr<std::string> last_device_id_;
    std::shared_ptr<int> last_width_;
    std::shared_ptr<int> last_height_;
    std::shared_ptr<uint32_t> last_refresh_numerator_;
    std::shared_ptr<uint32_t> last_refresh_denominator_;

    // Display cache reference
    display_cache::DisplayCache *display_cache_;

    // Constructor with DisplayCache
    explicit DisplaySettings(display_cache::DisplayCache *cache);
    ~DisplaySettings();

    // Load settings from ReShade
    void LoadSettings();

    // Save settings to ReShade
    void SaveSettings();

    // Get current settings
    std::string GetLastDeviceId() const;
    int GetLastWidth() const;
    int GetLastHeight() const;
    uint32_t GetLastRefreshNumerator() const;
    uint32_t GetLastRefreshDenominator() const;

    // Set settings
    void SetLastDeviceId(const std::string &device_id);
    void SetLastResolution(int width, int height);
    void SetLastRefreshRate(uint32_t numerator, uint32_t denominator);

    // Validation and fallback methods
    bool ValidateAndFixSettings();
    void SetToPrimaryDisplay();
    void SetToCurrentResolution();
    void SetToCurrentRefreshRate();

    // Debug information
    std::string GetDebugInfo() const;
};

} // namespace display_commander::settings