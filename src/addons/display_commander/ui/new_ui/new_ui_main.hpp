#pragma once

#include "new_ui_tabs.hpp"
#include "../imgui_wrapper_base.hpp"
#include <reshade.hpp>

namespace ui::new_ui {

// Main entry point for the new UI system
class NewUISystem {
  public:
    static NewUISystem &GetInstance();

    // Initialize the new UI system
    void Initialize();

    // Draw the new UI (uses gui for all ImGui calls)
    void Draw(reshade::api::effect_runtime* runtime, display_commander::ui::IImGuiWrapper& gui);

    // Check if the new UI system is enabled
    bool IsEnabled() const { return enabled_; }

    // Enable/disable the new UI system
    void SetEnabled(bool enabled) { enabled_ = enabled; }

  private:
    NewUISystem() = default;
    ~NewUISystem() = default;
    NewUISystem(const NewUISystem &) = delete;
    NewUISystem &operator=(const NewUISystem &) = delete;

    bool enabled_ = false;
    bool initialized_ = false;
};

// Convenience functions
void InitializeNewUISystem();
bool IsNewUIEnabled();

} // namespace ui::new_ui
