#pragma once

#include <windows.h>
#include <vector>
#include <atomic>
#include <string>

namespace adhd_multi_monitor {

// Monitor information structure
struct MonitorInfo {
    HMONITOR hMonitor;
    RECT rect;
    bool isPrimary;
    std::wstring deviceName;
};

// ADHD Multi-Monitor Manager class
class AdhdMultiMonitorManager {
public:
    AdhdMultiMonitorManager();
    ~AdhdMultiMonitorManager();

    // Initialize the manager
    bool Initialize();

    // Cleanup resources
    void Shutdown();

    // Enable/disable ADHD mode
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return enabled_.load(); }

    // Set focus disengagement mode
    void SetFocusDisengage(bool disengage);
    bool IsFocusDisengage() const { return focus_disengage_.load(); }

    // Update the background window based on current settings
    void UpdateBackgroundWindow();

    // Handle window focus changes
    void OnWindowFocusChanged(bool hasFocus);

    // Check if multiple monitors are available
    bool HasMultipleMonitors() const;

    // Get the monitor where the game window is located
    HMONITOR GetGameMonitor() const;

private:
    // Background window management
    bool CreateBackgroundWindow();
    void DestroyBackgroundWindow();
    void PositionBackgroundWindow();
    void ShowBackgroundWindow(bool show);

    // Monitor enumeration
    void EnumerateMonitors();
    void UpdateMonitorInfo();

    // Window procedure for the background window
    static LRESULT CALLBACK BackgroundWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    // Member variables
    std::atomic<bool> enabled_;
    std::atomic<bool> focus_disengage_;
    std::atomic<bool> game_has_focus_;

    HWND background_hwnd_;
    HWND game_hwnd_;

    std::vector<MonitorInfo> monitors_;
    MonitorInfo game_monitor_;

    bool initialized_;
    bool background_window_created_;

    // Window class name for the background window
    static constexpr const wchar_t* BACKGROUND_WINDOW_CLASS = L"AdhdMultiMonitorBackground";
    static constexpr const wchar_t* BACKGROUND_WINDOW_TITLE = L"ADHD Multi-Monitor Background";
};

// Global instance
extern AdhdMultiMonitorManager g_adhdManager;

} // namespace adhd_multi_monitor
