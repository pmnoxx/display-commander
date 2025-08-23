#pragma once

// Main entry point for the new UI system
#include "new_ui_main.hpp"

// Tab management system
#include "new_ui_tabs.hpp"

// Individual tab implementations
#include "swapchain_tab.hpp"
#include "window_info_tab.hpp"
#include "device_info_tab.hpp"

// Wrapper for integration with existing settings
#include "new_ui_wrapper.hpp"

// Convenience namespace for easy access
namespace new_ui = renodx::ui::new_ui;
