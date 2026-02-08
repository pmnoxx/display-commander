#include "new_ui_main.hpp"
#include "../../globals.hpp"
#include "../../utils/logging.hpp"
#include <reshade.hpp>

namespace ui::new_ui {

// Singleton instance
NewUISystem &NewUISystem::GetInstance() {
    static NewUISystem instance;
    return instance;
}

void NewUISystem::Initialize() {
    if (initialized_) {
        return;
    }

    // Initialize the tab system
    InitializeNewUI();

    // Mark as initialized and enabled
    initialized_ = true;
    enabled_ = true;

    LogInfo("New UI system initialized successfully");
}

void NewUISystem::Draw(reshade::api::effect_runtime* runtime) {
    if (!enabled_ || !initialized_) {
        return;
    }

    g_rendering_ui_section.store("ui:overlay", std::memory_order_release);
    // Draw the new UI system
    DrawNewUI(runtime);
    g_rendering_ui_section.store(nullptr, std::memory_order_release);
}

// Convenience functions
void InitializeNewUISystem() { NewUISystem::GetInstance().Initialize(); }

bool IsNewUIEnabled() { return NewUISystem::GetInstance().IsEnabled(); }

} // namespace ui::new_ui
