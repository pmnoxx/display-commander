#pragma once

namespace reshade {
namespace api {
struct effect_runtime;
}
}  // namespace reshade

namespace ui::new_ui {

// Initialize experimental tab
void InitExperimentalTab();

// Draw the experimental tab content
void DrawExperimentalTab(reshade::api::effect_runtime* runtime);

// Draw auto-click feature section
void DrawAutoClickFeature();

// Draw mouse coordinates display section
void DrawMouseCoordinatesDisplay();

// Draw backbuffer format override section
void DrawBackbufferFormatOverride();

// Draw buffer resolution upgrade section
void DrawBufferResolutionUpgrade();

// Draw texture format upgrade section
void DrawTextureFormatUpgrade();

// Draw sleep hook controls section
void DrawSleepHookControls();

// Draw rand hook controls section
void DrawRandHookControls();

// Draw time slowdown controls section
void DrawTimeSlowdownControls();

// Draw DLSS indicator controls section
void DrawDlssIndicatorControls();

// Draw D3D9 FLIPEX controls section
void DrawD3D9FlipExControls();

// Draw disable flip chain controls section
void DrawDisableFlipChainControls();

// Draw developer tools section
void DrawDeveloperTools();

// Draw HID suppression controls section
void DrawHIDSuppression();

// Draw debug output hooks section
void DrawDebugOutputHooks();

// Draw anisotropic filtering upgrade section
void DrawAnisotropicFilteringUpgrade();

// Draw DLL blocking controls section
void DrawDLLBlockingControls();

// Draw input test tab
void DrawInputTestTab();

// Draw NVIDIA Profile tab (standalone top-level tab content)
void DrawNvidiaProfileTab(reshade::api::effect_runtime* runtime);

// Cleanup function to stop background threads
void CleanupExperimentalTab();

}  // namespace ui::new_ui
