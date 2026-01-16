#pragma once

#include <imgui.h>

#include <array>
#include <atomic>
#include <memory>
#include <string>

namespace display_commander::widgets::resolution_widget {

// Resolution data structure
struct ResolutionData {
    int width = 0;
    int height = 0;
    int refresh_numerator = 0;
    int refresh_denominator = 0;
    bool is_current = false; // true if this represents "current resolution"

    ResolutionData() = default;
    ResolutionData(int w, int h, int num = 0, int denom = 0, bool current = false)
        : width(w), height(h), refresh_numerator(num), refresh_denominator(denom), is_current(current) {}

    bool operator==(const ResolutionData &other) const {
        return width == other.width && height == other.height && refresh_numerator == other.refresh_numerator &&
               refresh_denominator == other.refresh_denominator && is_current == other.is_current;
    }

    bool operator!=(const ResolutionData &other) const { return !(*this == other); }

    std::string ToString() const {
        if (is_current) {
            return "Current Resolution";
        }
        return std::to_string(width) + " x " + std::to_string(height);
    }
};

// Settings for a single display
class DisplayResolutionSettings {
  public:
    DisplayResolutionSettings(const std::string &display_key, int display_index);

    // Load settings from reshade config
    void Load();

    // Save settings to reshade config
    void Save();

    // Get current dirty state
    bool IsDirty() const { return is_dirty_.load(); }

    // Get last saved good state
    const ResolutionData &GetLastSavedState() const { return last_saved_state_; }

    // Get current UI state (may be dirty)
    const ResolutionData &GetCurrentState() const { return current_state_; }

    // Set current UI state (marks as dirty)
    void SetCurrentState(const ResolutionData &data);

    // Save current state as good state (clears dirty flag)
    void SaveCurrentState();

    // Reset to last saved state (clears dirty flag)
    void ResetToLastSaved();

    // Set to current resolution
    void SetToCurrentResolution();

    // Check if current state represents current resolution
    bool IsCurrentResolution() const { return current_state_.is_current; }

  private:
    std::string display_key_;
    int display_index_;

    ResolutionData last_saved_state_;
    ResolutionData current_state_;
    std::atomic<bool> is_dirty_{false};

    void MarkDirty() { is_dirty_.store(true); }
    void ClearDirty() { is_dirty_.store(false); }
};

// Main resolution settings manager for all displays
class ResolutionSettingsManager {
  public:
    static constexpr int MAX_DISPLAYS = 4;

    ResolutionSettingsManager();
    ~ResolutionSettingsManager() = default;

    // Load all settings
    void LoadAll();

    // Save all settings
    void SaveAll();

    // Get settings for specific display (0-3)
    DisplayResolutionSettings &GetDisplaySettings(int display_index);
    const DisplayResolutionSettings &GetDisplaySettings(int display_index) const;

    // Check if any display has dirty state
    bool HasAnyDirty() const;

    // Save all dirty settings
    void SaveAllDirty();

    // Reset all dirty settings to last saved
    void ResetAllDirty();

    // Auto-apply setting
    bool GetAutoApply() const { return auto_apply_.load(); }
    void SetAutoApply(bool enabled);

    // Auto-apply on start settings
    bool GetAutoApplyOnStart() const { return auto_apply_on_start_.load(); }
    void SetAutoApplyOnStart(bool enabled);
    int GetAutoApplyOnStartDelay() const { return auto_apply_on_start_delay_.load(); }
    void SetAutoApplyOnStartDelay(int delay_seconds);

  private:
    std::array<std::unique_ptr<DisplayResolutionSettings>, MAX_DISPLAYS> display_settings_;
    std::atomic<bool> auto_apply_{false};
    std::atomic<bool> auto_apply_on_start_{false};
    std::atomic<int> auto_apply_on_start_delay_{10}; // Default 10 seconds

    void InitializeDisplaySettings();
};

// Global instance
extern std::unique_ptr<ResolutionSettingsManager> g_resolution_settings;

// Initialize the global settings manager
void InitializeResolutionSettings();

// Cleanup the global settings manager
void CleanupResolutionSettings();

} // namespace display_commander::widgets::resolution_widget
