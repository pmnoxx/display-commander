#pragma once

namespace display_commander {
namespace ui {
struct IImGuiWrapper;
}  // namespace ui
}  // namespace display_commander

namespace reshade {
namespace api {
struct effect_runtime;
}
}  // namespace reshade

namespace ui::new_ui {

// Initialize experimental tab
void InitExperimentalTab();

// Draw the experimental tab content (accepts ImGui wrapper for ReShade overlay or standalone UI).
void DrawExperimentalTab(display_commander::ui::IImGuiWrapper& imgui,
                        reshade::api::effect_runtime* runtime);

// Draw disable flip chain controls section
void DrawDisableFlipChainControls();

// Cleanup function to stop background threads
void CleanupExperimentalTab();

}  // namespace ui::new_ui
