#include "ui_display_tab.hpp"


// Flag to prevent multiple initializations
static bool ui_settings_initialized = false;

// Initialize all settings sections
void InitializeUISettings() {
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
