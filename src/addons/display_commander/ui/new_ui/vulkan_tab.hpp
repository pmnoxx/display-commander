#pragma once

namespace display_commander {
namespace ui {
struct IImGuiWrapper;
}  // namespace ui
}  // namespace display_commander

namespace ui::new_ui {

// Initialize Vulkan (experimental) tab (reserved for future hook init)
void InitVulkanTab();

// Draw the Vulkan (experimental) tab content: controls and debug info for Vulkan Reflex / frame pacing.
// Accepts ImGui wrapper for ReShade overlay or independent UI.
void DrawVulkanTab(display_commander::ui::IImGuiWrapper& imgui);

}  // namespace ui::new_ui
