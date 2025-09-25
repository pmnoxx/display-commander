#include "new_ui_main.hpp"
#include "../../utils.hpp"

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

void NewUISystem::Draw() {
    if (!enabled_ || !initialized_) {
        return;
    }

    // Draw the new UI system
    DrawNewUI();
}

// Convenience functions
void InitializeNewUISystem() { NewUISystem::GetInstance().Initialize(); }

bool IsNewUIEnabled() { return NewUISystem::GetInstance().IsEnabled(); }

} // namespace ui::new_ui
