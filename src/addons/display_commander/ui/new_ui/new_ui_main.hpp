#pragma once

#include "new_ui_tabs.hpp"

namespace ui::new_ui {

// Main entry point for the new UI system
class NewUISystem {
public:
    static NewUISystem& GetInstance();

    // Initialize the new UI system
    void Initialize();

    // Draw the new UI
    void Draw();

    // Check if the new UI system is enabled
    bool IsEnabled() const { return enabled_; }

    // Enable/disable the new UI system
    void SetEnabled(bool enabled) { enabled_ = enabled; }

private:
    NewUISystem() = default;
    ~NewUISystem() = default;
    NewUISystem(const NewUISystem&) = delete;
    NewUISystem& operator=(const NewUISystem&) = delete;

    bool enabled_ = false;
    bool initialized_ = false;
};

// Convenience functions
void InitializeNewUISystem();
bool IsNewUIEnabled();

} // namespace ui::new_ui
