#include "new_ui_main.hpp"
#include "../../addon.hpp"

namespace ui::new_ui {

// Singleton instance
NewUISystem& NewUISystem::GetInstance() {
    static NewUISystem instance;
    return instance;
}

void NewUISystem::Initialize(int debug_mode) {
    if (initialized_) {
        return;
    }

    // Initialize the tab system
    InitializeNewUI(debug_mode);

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
void InitializeNewUISystem(int debug_mode) {
    NewUISystem::GetInstance().Initialize(debug_mode);
}

void DrawNewUISystem() {
    NewUISystem::GetInstance().Draw();
}

bool IsNewUIEnabled() {
    return NewUISystem::GetInstance().IsEnabled();
}

} // namespace ui::new_ui
