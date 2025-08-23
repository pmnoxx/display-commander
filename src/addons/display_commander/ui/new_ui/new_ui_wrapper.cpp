#include "new_ui_wrapper.hpp"
#include "new_ui_main.hpp"
#include "../../addon.hpp"

namespace renodx::ui::new_ui {

void AddNewUISettings(std::vector<renodx::utils::settings2::Setting*>& settings) {
    // Add a single custom setting that wraps the entire new UI system
    settings.push_back(new renodx::utils::settings2::Setting{
        .key = "NewUISystem",
        .binding = nullptr,
        .value_type = renodx::utils::settings2::SettingValueType::CUSTOM,
        .default_value = 0.f,
        .label = "New UI System",
        .section = "General",
        .tooltip = "Modern ImGui-based UI system with tabs and improved layout.",
        .on_draw = []() -> bool {
            // Initialize the new UI system if not already done
            static bool initialized = false;
            if (!initialized) {
                InitializeNewUISystem();
                initialized = true;
            }
            
            // Draw the new UI system
            DrawNewUISystem();
            
            return false; // No value changes
        },
        .is_visible = []() { return true; }, // Always visible
    });
}

bool ShouldUseNewUI() {
    // For now, always return true to use the new UI
    // Later this can be made configurable
    return true;
}

} // namespace renodx::ui::new_ui
