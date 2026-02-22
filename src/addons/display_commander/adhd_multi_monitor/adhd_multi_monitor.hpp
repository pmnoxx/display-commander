#pragma once

#include <atomic>
#include <vector>
#include <windows.h>

namespace adhd_multi_monitor {

// Simple ADHD Multi-Monitor Manager - single class implementation
class AdhdMultiMonitorManager {
  public:
    AdhdMultiMonitorManager();
    ~AdhdMultiMonitorManager();

    // Initialize the manager
    bool Initialize();

    // Cleanup resources
    void Shutdown();

    // Update the system (call from main loop)
    void Update();

    // Enable/disable ADHD mode
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return enabled_.load(); }

    // Focus disengagement is always enabled (no UI control needed)
    bool IsFocusDisengage() const { return true; }

    // Check if multiple monitors are available
    bool HasMultipleMonitors() const;

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

    HWND background_hwnd_;
    bool last_game_in_foreground_;

    std::vector<RECT> monitor_rects_;
    RECT game_monitor_rect_;

    bool initialized_;
    bool background_window_created_;

    // Window class name for the background window
    static constexpr const wchar_t *BACKGROUND_WINDOW_CLASS = L"AdhdMultiMonitorBackground";
    static constexpr const wchar_t *BACKGROUND_WINDOW_TITLE = L"ADHD Multi-Monitor Background";
};

// Global instance
extern AdhdMultiMonitorManager g_adhdManager;

} // namespace adhd_multi_monitor
