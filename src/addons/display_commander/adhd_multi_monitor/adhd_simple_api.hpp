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

// Enable/disable ADHD mode
void SetEnabled(bool enabled);

// Focus disengagement is always enabled (no API needed)

// Check if ADHD mode is enabled
bool IsEnabled();

// Check if focus disengagement is enabled
bool IsFocusDisengage();

// Check if multiple monitors are available
bool HasMultipleMonitors();

} // namespace api

} // namespace adhd_multi_monitor
