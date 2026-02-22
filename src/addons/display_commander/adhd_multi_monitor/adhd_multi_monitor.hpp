#pragma once

#include <windows.h>
#include <atomic>
#include <optional>
#include <thread>
#include <vector>

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

    // Enable/disable ADHD mode: (enabled for game display, enabled for other displays)
    void SetEnabled(bool enabled_for_game_display, bool enabled_for_other_displays);
    bool IsEnabledForGameDisplay() const { return enabled_for_game_display_.load(); }
    bool IsEnabledForOtherDisplays() const { return enabled_for_other_displays_.load(); }

    // Focus disengagement is always enabled (no UI control needed)
    bool IsFocusDisengage() const { return true; }

    // Check if multiple monitors are available
    bool HasMultipleMonitors() const;

   private:
    // Background window management
    bool CreateBackgroundWindow();
    void DestroyBackgroundWindow();
    void PositionBackgroundWindow();

    // Monitor enumeration
    void EnumerateMonitors();

    // Window procedure for the background window
    static LRESULT CALLBACK BackgroundWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    // Dedicated thread that runs the message pump for the background window (so the game does not crash if continuous monitoring stops)
    void MessagePumpThreadFunc();

    // Member variables
    std::atomic<bool> enabled_for_other_displays_ = false;
    std::atomic<bool> enabled_for_game_display_ = false;

    // Single window stretching over all displays, inserted after game_hwnd
    HWND background_hwnd_ = nullptr;

    HANDLE pump_stop_event_ = nullptr;
    std::thread message_pump_thread_;

    std::vector<RECT> monitor_rects_;
    RECT game_monitor_rect_;

    bool initialized_;
    bool background_window_created_;

    static constexpr const wchar_t* BACKGROUND_WINDOW_CLASS = L"AdhdMultiMonitorBackground";
    static constexpr const wchar_t* BACKGROUND_WINDOW_TITLE = L"ADHD Multi-Monitor Background";
};

// Global instance
extern AdhdMultiMonitorManager g_adhdManager;

}  // namespace adhd_multi_monitor
