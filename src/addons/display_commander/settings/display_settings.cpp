#include "display_settings.hpp"
#include "../utils.hpp"

#include <windows.h>

#include <reshade.hpp>

#include <sstream>

namespace display_commander::settings {

// Global instance
std::unique_ptr<DisplaySettings> g_display_settings = nullptr;

// Helper functions for string conversion
std::string WStringToString(const std::wstring &wstr) {
    if (wstr.empty())
        return std::string();

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

std::wstring StringToWString(const std::string &str) {
    if (str.empty())
        return std::wstring();

    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

DisplaySettings::DisplaySettings(display_cache::DisplayCache *cache) : display_cache_(cache) {
    // Initialize with default values
    last_device_id_ = std::make_shared<std::string>("");
    last_width_ = std::make_shared<int>(0);
    last_height_ = std::make_shared<int>(0);
    last_refresh_numerator_ = std::make_shared<uint32_t>(0);
    last_refresh_denominator_ = std::make_shared<uint32_t>(1);
}

DisplaySettings::~DisplaySettings() {
    // Save settings on destruction
    SaveSettings();
}

void DisplaySettings::LoadSettings() {
    LogInfo("DisplaySettings::LoadSettings() - Loading settings from ReShade");

    // Load last device ID using string API
    char device_id_buffer[256] = {0};
    size_t device_id_size = sizeof(device_id_buffer);
    if (reshade::get_config_value(nullptr, "DisplayCommander.DisplaySettings", "last_device_id", device_id_buffer,
                                  &device_id_size)) {
        *last_device_id_ = std::string(device_id_buffer);
        LogInfo("DisplaySettings::LoadSettings() - Loaded last_device_id: %s", last_device_id_->c_str());
    } else {
        LogInfo("DisplaySettings::LoadSettings() - last_device_id not found, using default");
    }

    // Load last resolution
    int loaded_width, loaded_height;
    if (reshade::get_config_value(nullptr, "DisplayCommander.DisplaySettings", "last_width", loaded_width)) {
        *last_width_ = loaded_width;
        LogInfo("DisplaySettings::LoadSettings() - Loaded last_width: %d", loaded_width);
    } else {
        LogInfo("DisplaySettings::LoadSettings() - last_width not found, using default: 0");
    }

    if (reshade::get_config_value(nullptr, "DisplayCommander.DisplaySettings", "last_height", loaded_height)) {
        *last_height_ = loaded_height;
        LogInfo("DisplaySettings::LoadSettings() - Loaded last_height: %d", loaded_height);
    } else {
        LogInfo("DisplaySettings::LoadSettings() - last_height not found, using default: 0");
    }

    // Load last refresh rate
    uint32_t loaded_numerator, loaded_denominator;
    if (reshade::get_config_value(nullptr, "DisplayCommander.DisplaySettings", "last_refresh_numerator",
                                  loaded_numerator)) {
        *last_refresh_numerator_ = loaded_numerator;
        LogInfo("DisplaySettings::LoadSettings() - Loaded last_refresh_numerator: %u", loaded_numerator);
    } else {
        LogInfo("DisplaySettings::LoadSettings() - last_refresh_numerator not found, using default: 0");
    }

    if (reshade::get_config_value(nullptr, "DisplayCommander.DisplaySettings", "last_refresh_denominator",
                                  loaded_denominator)) {
        *last_refresh_denominator_ = loaded_denominator;
        LogInfo("DisplaySettings::LoadSettings() - Loaded last_refresh_denominator: %u", loaded_denominator);
    } else {
        LogInfo("DisplaySettings::LoadSettings() - last_refresh_denominator not found, using default: 1");
    }

    // Validate and fix settings after loading
    ValidateAndFixSettings();
}

void DisplaySettings::SaveSettings() {
    LogInfo("DisplaySettings::SaveSettings() - Saving settings to ReShade");

    // Save last device ID using string API
    reshade::set_config_value(nullptr, "DisplayCommander.DisplaySettings", "last_device_id", last_device_id_->c_str());

    // Save last resolution
    reshade::set_config_value(nullptr, "DisplayCommander.DisplaySettings", "last_width", *last_width_);
    reshade::set_config_value(nullptr, "DisplayCommander.DisplaySettings", "last_height", *last_height_);

    // Save last refresh rate
    reshade::set_config_value(nullptr, "DisplayCommander.DisplaySettings", "last_refresh_numerator",
                              *last_refresh_numerator_);
    reshade::set_config_value(nullptr, "DisplayCommander.DisplaySettings", "last_refresh_denominator",
                              *last_refresh_denominator_);

    LogInfo("DisplaySettings::SaveSettings() - Settings saved successfully");
}

std::string DisplaySettings::GetLastDeviceId() const { return *last_device_id_; }

int DisplaySettings::GetLastWidth() const { return *last_width_; }

int DisplaySettings::GetLastHeight() const { return *last_height_; }

uint32_t DisplaySettings::GetLastRefreshNumerator() const { return *last_refresh_numerator_; }

uint32_t DisplaySettings::GetLastRefreshDenominator() const { return *last_refresh_denominator_; }

void DisplaySettings::SetLastDeviceId(const std::string &device_id) {
    *last_device_id_ = device_id;
    LogInfo("DisplaySettings::SetLastDeviceId() - Set to: %s", device_id.c_str());
}

void DisplaySettings::SetLastResolution(int width, int height) {
    *last_width_ = width;
    *last_height_ = height;
    LogInfo("DisplaySettings::SetLastResolution() - Set to: %dx%d", width, height);
}

void DisplaySettings::SetLastRefreshRate(uint32_t numerator, uint32_t denominator) {
    *last_refresh_numerator_ = numerator;
    *last_refresh_denominator_ = denominator;
    LogInfo("DisplaySettings::SetLastRefreshRate() - Set to: %u/%u", numerator, denominator);
}

bool DisplaySettings::ValidateAndFixSettings() {
    LogInfo("DisplaySettings::ValidateAndFixSettings() - Validating settings");

    if (!display_cache_ || !display_cache_->IsInitialized()) {
        LogError("DisplaySettings::ValidateAndFixSettings() - Display cache not available");
        return false;
    }

    bool needs_fix = false;

    // Check if device ID is valid
    std::string current_device_id = *last_device_id_;
    if (current_device_id.empty()) {
        LogInfo("DisplaySettings::ValidateAndFixSettings() - Device ID is empty, setting to primary display");
        SetToPrimaryDisplay();
        needs_fix = true;
    } else {
        // Check if the device exists in the cache
        bool device_found = false;
        auto displays = display_cache_->GetDisplays();
        if (displays) {
            for (const auto &display : *displays) {
                if (display) {
                    // Convert wstring to string for comparison
                    std::string display_device_name = WStringToString(display->device_name);
                    if (display_device_name == current_device_id) {
                        device_found = true;
                        break;
                    }
                }
            }
        }

        if (!device_found) {
            LogInfo("DisplaySettings::ValidateAndFixSettings() - Device ID '%s' not found, setting to primary display",
                    current_device_id.c_str());
            SetToPrimaryDisplay();
            needs_fix = true;
        }
    }

    // Check if resolution is valid
    int current_width = *last_width_;
    int current_height = *last_height_;
    if (current_width <= 0 || current_height <= 0) {
        LogInfo(
            "DisplaySettings::ValidateAndFixSettings() - Resolution is invalid (%dx%d), setting to current resolution",
            current_width, current_height);
        SetToCurrentResolution();
        needs_fix = true;
    }

    // Check if refresh rate is valid
    uint32_t current_numerator = *last_refresh_numerator_;
    uint32_t current_denominator = *last_refresh_denominator_;
    if (current_denominator == 0 || current_numerator == 0) {
        LogInfo("DisplaySettings::ValidateAndFixSettings() - Refresh rate is invalid (%u/%u), setting to current "
                "refresh rate",
                current_numerator, current_denominator);
        SetToCurrentRefreshRate();
        needs_fix = true;
    }

    if (needs_fix) {
        SaveSettings();
        LogInfo("DisplaySettings::ValidateAndFixSettings() - Settings fixed and saved");
    } else {
        LogInfo("DisplaySettings::ValidateAndFixSettings() - All settings are valid");
    }

    return true;
}

void DisplaySettings::SetToPrimaryDisplay() {
    if (!display_cache_ || !display_cache_->IsInitialized()) {
        LogError("DisplaySettings::SetToPrimaryDisplay() - Display cache not available");
        return;
    }

    auto displays = display_cache_->GetDisplays();
    if (!displays) {
        LogError("DisplaySettings::SetToPrimaryDisplay() - No displays available");
        return;
    }

    // Find primary display
    for (const auto &display : *displays) {
        if (display && display->is_primary) {
            // Convert wstring to string
            std::string device_id = WStringToString(display->device_name);
            *last_device_id_ = device_id;
            LogInfo("DisplaySettings::SetToPrimaryDisplay() - Set to primary display: %s", device_id.c_str());
            return;
        }
    }

    // If no primary display found, use the first display
    if (!displays->empty() && displays->at(0)) {
        // Convert wstring to string
        std::string device_id = WStringToString(displays->at(0)->device_name);
        *last_device_id_ = device_id;
        LogInfo("DisplaySettings::SetToPrimaryDisplay() - No primary display found, using first display: %s",
                device_id.c_str());
    } else {
        LogError("DisplaySettings::SetToPrimaryDisplay() - No displays available");
    }
}

void DisplaySettings::SetToCurrentResolution() {
    if (!display_cache_ || !display_cache_->IsInitialized()) {
        LogError("DisplaySettings::SetToCurrentResolution() - Display cache not available");
        return;
    }

    // Find the display with the current device ID
    std::string current_device_id = *last_device_id_;
    if (current_device_id.empty()) {
        LogError("DisplaySettings::SetToCurrentResolution() - No device ID set");
        return;
    }

    auto displays = display_cache_->GetDisplays();
    if (!displays) {
        LogError("DisplaySettings::SetToCurrentResolution() - No displays available");
        return;
    }

    for (const auto &display : *displays) {
        if (display) {
            // Convert wstring to string for comparison
            std::string display_device_name = WStringToString(display->device_name);
            if (display_device_name == current_device_id) {
                *last_width_ = display->width;
                *last_height_ = display->height;
                LogInfo("DisplaySettings::SetToCurrentResolution() - Set to current resolution: %dx%d", display->width,
                        display->height);
                return;
            }
        }
    }

    LogError("DisplaySettings::SetToCurrentResolution() - Display with ID '%s' not found", current_device_id.c_str());
}

void DisplaySettings::SetToCurrentRefreshRate() {
    if (!display_cache_ || !display_cache_->IsInitialized()) {
        LogError("DisplaySettings::SetToCurrentRefreshRate() - Display cache not available");
        return;
    }

    // Find the display with the current device ID
    std::string current_device_id = *last_device_id_;
    if (current_device_id.empty()) {
        LogError("DisplaySettings::SetToCurrentRefreshRate() - No device ID set");
        return;
    }

    auto displays = display_cache_->GetDisplays();
    if (!displays) {
        LogError("DisplaySettings::SetToCurrentRefreshRate() - No displays available");
        return;
    }

    for (const auto &display : *displays) {
        if (display) {
            // Convert wstring to string for comparison
            std::string display_device_name = WStringToString(display->device_name);
            if (display_device_name == current_device_id) {
                *last_refresh_numerator_ = display->current_refresh_rate.numerator;
                *last_refresh_denominator_ = display->current_refresh_rate.denominator;
                LogInfo("DisplaySettings::SetToCurrentRefreshRate() - Set to current refresh rate: %u/%u",
                        display->current_refresh_rate.numerator, display->current_refresh_rate.denominator);
                return;
            }
        }
    }

    LogError("DisplaySettings::SetToCurrentRefreshRate() - Display with ID '%s' not found", current_device_id.c_str());
}

std::string DisplaySettings::GetDebugInfo() const {
    std::ostringstream oss;
    oss << "DisplaySettings Debug Info:\n";
    oss << "  last_device_id: " << *last_device_id_ << "\n";
    oss << "  last_width: " << *last_width_ << "\n";
    oss << "  last_height: " << *last_height_ << "\n";
    oss << "  last_refresh_numerator: " << *last_refresh_numerator_ << "\n";
    oss << "  last_refresh_denominator: " << *last_refresh_denominator_ << "\n";

    if (*last_refresh_denominator_ != 0) {
        double refresh_hz =
            static_cast<double>(*last_refresh_numerator_) / static_cast<double>(*last_refresh_denominator_);
        oss << "  last_refresh_rate_hz: " << refresh_hz << "\n";
    } else {
        oss << "  last_refresh_rate_hz: invalid (denominator is 0)\n";
    }

    oss << "  display_cache_available: " << (display_cache_ != nullptr ? "yes" : "no") << "\n";
    if (display_cache_) {
        oss << "  display_cache_initialized: " << (display_cache_->IsInitialized() ? "yes" : "no") << "\n";
        if (display_cache_->IsInitialized()) {
            oss << "  display_count: " << display_cache_->GetDisplayCount() << "\n";
        }
    }

    return oss.str();
}

} // namespace display_commander::settings