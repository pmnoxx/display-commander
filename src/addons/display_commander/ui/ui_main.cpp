#include "ui_display_tab.hpp"
#include "new_ui/new_ui_main.hpp"

// Flag to prevent multiple initializations
static bool ui_settings_initialized = false;

// Initialize all settings sections
void InitializeUISettings(int debug_mode) {
    // Prevent multiple initializations
    if (ui_settings_initialized) {
        return;
    }

    static bool cache_initialized = false;
    if (!cache_initialized) {
        ui::InitializeDisplayCache();
        cache_initialized = true;
    }


    // Mark as initialized
    ui_settings_initialized = true;
}

