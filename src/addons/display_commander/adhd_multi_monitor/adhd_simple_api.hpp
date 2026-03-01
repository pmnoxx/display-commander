#pragma once

#include <cstdint>

// Simple API for ADHD Multi-Monitor Mode
// This replaces the complex integration system with a single class approach

namespace adhd_multi_monitor {

// Debug info for the ADHD background window (for tooltips / diagnostics)
struct BackgroundWindowDebugInfo {
    void* hwnd = nullptr;   // HWND as void* to avoid windows.h in this header
    bool not_null = false;
    int left = 0, top = 0, width = 0, height = 0;
    bool is_visible = false;
};

// Simple API functions
namespace api {

// Initialize the ADHD multi-monitor system
bool Initialize();

// Update the system (call from main loop)
void Update();

// Enable/disable ADHD mode: (enabled for game display, enabled for other displays)
void SetEnabled(bool enabled_for_game_display, bool enabled_for_other_displays);
bool IsEnabledForGameDisplay();
bool IsEnabledForOtherDisplays();

// Focus disengagement is always enabled (no API needed)

// Check if focus disengagement is enabled
bool IsFocusDisengage();

// Check if multiple monitors are available
bool HasMultipleMonitors();

// Fill debug info for the ADHD background window (hwnd, position, size, visibility). Safe from any thread.
void GetBackgroundWindowDebugInfo(BackgroundWindowDebugInfo* out);

}  // namespace api

}  // namespace adhd_multi_monitor
