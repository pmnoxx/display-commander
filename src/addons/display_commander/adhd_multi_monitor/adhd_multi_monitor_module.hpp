#pragma once

// Main header file for the ADHD Multi-Monitor module
// This provides a single include point for all ADHD functionality

#include "adhd_multi_monitor.hpp"
#include "focus_detector.hpp"
#include "adhd_integration.hpp"

namespace adhd_multi_monitor {

// Convenience functions for easy integration
namespace api {

// Initialize the ADHD multi-monitor system
bool Initialize();

// Shutdown the ADHD multi-monitor system
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
bool IsEnabled();

// Check if focus disengagement is enabled
bool IsFocusDisengage();

// Check if multiple monitors are available
bool HasMultipleMonitors();

} // namespace api

} // namespace adhd_multi_monitor
