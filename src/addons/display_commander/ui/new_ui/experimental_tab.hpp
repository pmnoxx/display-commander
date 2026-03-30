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

// Draw time slowdown controls section
void DrawTimeSlowdownControls(display_commander::ui::IImGuiWrapper& imgui);

// Draw D3D9 FLIPEX controls section
void DrawD3D9FlipExControls(display_commander::ui::IImGuiWrapper& imgui);

// Draw disable flip chain controls section
void DrawDisableFlipChainControls();

// Draw developer tools section
void DrawDeveloperTools(display_commander::ui::IImGuiWrapper& imgui);

// Draw debug output hooks section
void DrawDebugOutputHooks(display_commander::ui::IImGuiWrapper& imgui);

// Draw anisotropic filtering upgrade section
void DrawAnisotropicFilteringUpgrade(display_commander::ui::IImGuiWrapper& imgui);

// Cleanup function to stop background threads
void CleanupExperimentalTab();

}  // namespace ui::new_ui
