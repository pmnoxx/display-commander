#pragma once

#include <imgui.h>
#include <memory>
#include <string>
#include <vector>
#include "resolution_settings.hpp"


// Forward declaration for Windows types
typedef long long LONGLONG;

namespace display_commander::widgets::resolution_widget {

// Resolution widget class
class ResolutionWidget {
   public:
    ResolutionWidget();
    ~ResolutionWidget() = default;

    // Main draw function - call this from the main tab
    void OnDraw();

    // Initialize the widget (call once at startup)
    void Initialize();

    // Cleanup the widget (call at shutdown)
    void Cleanup();

    // Apply current selection (public for auto-apply on start)
    bool ApplyCurrentSelection();
    
    // Apply a specific resolution (public for programmatic use)
    bool ApplyResolution(int display_index, int width, int height, int refresh_numerator = 0, int refresh_denominator = 0);
    
    // Prepare widget for auto-apply (ensures initialization and settings are loaded)
    void PrepareForAutoApply();

   private:
    // UI state
    int selected_display_index_ = 0;
    int selected_resolution_index_ = 0;
    int selected_refresh_index_ = 0;

    // Cached resolution data for current display
    std::vector<std::string> resolution_labels_;
    std::vector<ResolutionData> resolution_data_;
    std::vector<std::string> refresh_labels_;
    std::vector<ResolutionData> refresh_data_;

    // UI helper functions
    void DrawDisplaySelector();
    void DrawResolutionSelector();
    void DrawRefreshRateSelector();
    void DrawActionButtons();
    void DrawAutoApplyCheckbox();
    void DrawAutoApplyOnStart();
    void DrawOriginalSettingsInfo();
    void DrawAutoRestoreCheckbox();
    void DrawDebugMenu();

    // Data management
    void RefreshDisplayData();
    void RefreshResolutionData();
    void RefreshRefreshRateData();

    // Apply resolution changes
    bool TryApplyResolution(int display_index, const ResolutionData& resolution, const ResolutionData& refresh);

    // Helper functions
    std::string GetDisplayName(int display_index) const;
    int GetActualDisplayIndex() const;
    void UpdateCurrentSelectionFromSettings();
    void UpdateSettingsFromCurrentSelection();

    // UI state management
    bool is_initialized_ = false;
    bool needs_refresh_ = true;
    bool settings_applied_to_ui_ = false;

    // Confirmation dialog state
    bool show_confirmation_ = false;
    LONGLONG confirmation_start_time_ns_ = 0;
    int confirmation_timer_seconds_ = 30;
    ResolutionData pending_resolution_;
    ResolutionData pending_refresh_;
    ResolutionData previous_resolution_;
    ResolutionData previous_refresh_;
    int pending_display_index_ = 0;

    // Confirmation dialog methods
    void DrawConfirmationDialog();
    void RevertResolution();

    // Original settings storage
    struct OriginalSettings {
        int width = 0;
        int height = 0;
        int refresh_numerator = 0;
        int refresh_denominator = 0;
        std::string extended_device_id;
        bool is_primary = false;
        bool captured = false;
    };

    OriginalSettings original_settings_;

    // Original settings management
    void CaptureOriginalSettings();
    std::string FormatOriginalSettingsString() const;
};

// Global widget instance
extern std::unique_ptr<ResolutionWidget> g_resolution_widget;

// Global functions for integration
void InitializeResolutionWidget();
void CleanupResolutionWidget();
void DrawResolutionWidget();

}  // namespace display_commander::widgets::resolution_widget
