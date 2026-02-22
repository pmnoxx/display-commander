#pragma once

// Simple API for ADHD Multi-Monitor Mode
// This replaces the complex integration system with a single class approach

namespace adhd_multi_monitor {

// Simple API functions
namespace api {

// Initialize the ADHD multi-monitor system
bool Initialize();

// Shutdown the ADHD multi-monitor system
void Shutdown();

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

} // namespace api

} // namespace adhd_multi_monitor
