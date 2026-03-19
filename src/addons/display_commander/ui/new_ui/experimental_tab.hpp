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

// Draw mouse coordinates display section (experimental tab's own display)
void DrawMouseCoordinatesDisplay(display_commander::ui::IImGuiWrapper& imgui);

// Draw backbuffer format override section
void DrawBackbufferFormatOverride(display_commander::ui::IImGuiWrapper& imgui);

// Draw buffer resolution upgrade section
void DrawBufferResolutionUpgrade(display_commander::ui::IImGuiWrapper& imgui);

// Draw texture format upgrade section
void DrawTextureFormatUpgrade(display_commander::ui::IImGuiWrapper& imgui);

// Draw sleep hook controls section
void DrawSleepHookControls(display_commander::ui::IImGuiWrapper& imgui);

// Draw rand hook controls section
void DrawRandHookControls(display_commander::ui::IImGuiWrapper& imgui);

// Draw time slowdown controls section
void DrawTimeSlowdownControls(display_commander::ui::IImGuiWrapper& imgui);

// Draw DLSS indicator controls section
void DrawDlssIndicatorControls(display_commander::ui::IImGuiWrapper& imgui);

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

// Draw DLL blocking controls section
void DrawDLLBlockingControls(display_commander::ui::IImGuiWrapper& imgui);

// Draw input test tab
void DrawInputTestTab(display_commander::ui::IImGuiWrapper& imgui);

// Draw Runtimes debug sub-tab (all ReShade runtimes: backbuffer size, format, colorspace, etc.)
void DrawRuntimesDebugSubTab(display_commander::ui::IImGuiWrapper& imgui);

// Draw NVIDIA Profile tab (standalone top-level tab content)
void DrawNvidiaProfileTab(reshade::api::effect_runtime* runtime);

// Cleanup function to stop background threads
void CleanupExperimentalTab();

}  // namespace ui::new_ui
