#include "ui_display_tab.hpp"
#include "new_ui/new_ui_main.hpp"

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
        renodx::ui::InitializeDisplayCache();
        cache_initialized = true;
    }

    // Initialize the new UI system directly (no settings2 wrapper)
    renodx::ui::new_ui::InitializeNewUISystem();
    
    
    // Mark as initialized
    ui_settings_initialized = true;
}

