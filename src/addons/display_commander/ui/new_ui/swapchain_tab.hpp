#pragma once

#include <dxgi1_6.h>
#include <imgui.h>
#include <reshade.hpp>

namespace display_commander {
namespace ui {
struct IImGuiWrapper;
}
}  // namespace display_commander

namespace ui::new_ui {

// Initialize swapchain tab
void InitSwapchainTab();

// Auto-apply HDR metadata trigger (called from continuous monitoring)
void AutoApplyTrigger();

// Draw the swapchain tab content (uses ImGui wrapper for ReShade or standalone UI)
void DrawSwapchainTab(display_commander::ui::IImGuiWrapper& imgui, reshade::api::effect_runtime* runtime);

// Draw adapter information section
void DrawAdapterInfo(display_commander::ui::IImGuiWrapper& imgui);

// Draw DXGI composition information
void DrawDxgiCompositionInfo(display_commander::ui::IImGuiWrapper& imgui);

// Draw swapchain wrapper statistics
void DrawSwapchainWrapperStats(display_commander::ui::IImGuiWrapper& imgui);

// Draw swapchain event counters
void DrawSwapchainEventCounters(display_commander::ui::IImGuiWrapper& imgui);

// Draw NGX parameters section
void DrawNGXParameters(display_commander::ui::IImGuiWrapper& imgui);

// Draw DLSS/DLSS-G summary section
void DrawDLSSGSummary(display_commander::ui::IImGuiWrapper& imgui);

// Draw DLSS preset override section
void DrawDLSSPresetOverride(display_commander::ui::IImGuiWrapper& imgui);

// Helper functions for DXGI string conversion
const char* GetDXGIFormatString(DXGI_FORMAT format);
const char* GetDXGIScalingString(DXGI_SCALING scaling);
const char* GetDXGISwapEffectString(DXGI_SWAP_EFFECT effect);
const char* GetDXGIAlphaModeString(DXGI_ALPHA_MODE mode);
const char* GetDXGIColorSpaceString(DXGI_COLOR_SPACE_TYPE color_space);

}  // namespace ui::new_ui
