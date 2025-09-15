#pragma once

#include "adhd_multi_monitor.hpp"
#include "focus_detector.hpp"

namespace adhd_multi_monitor {

// Integration class that manages the ADHD multi-monitor system
class AdhdIntegration {
public:
    AdhdIntegration();
    ~AdhdIntegration();

    // Initialize the integration system
    bool Initialize();

    // Shutdown the integration system
    void Shutdown();

    // Update the system (call from main loop)
    void Update();

    // Set the game window handle
    void SetGameWindow(HWND hwnd);

    // Enable/disable ADHD mode
    void SetEnabled(bool enabled);

    // Set focus disengagement mode
    void SetFocusDisengage(bool disengage);

    // Check if ADHD mode is enabled
    bool IsEnabled() const;

    // Check if focus disengagement is enabled
    bool IsFocusDisengage() const;

    // Check if multiple monitors are available
    bool HasMultipleMonitors() const;

private:
    // Focus change callback
    void OnFocusChanged(bool hasFocus);

    // Member variables
    AdhdMultiMonitorManager* manager_;
    FocusDetector* focus_detector_;
    HWND game_window_;
    bool initialized_;
};

// Global integration instance
extern AdhdIntegration g_adhdIntegration;

} // namespace adhd_multi_monitor
