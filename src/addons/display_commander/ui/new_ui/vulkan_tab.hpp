#pragma once

namespace reshade {
namespace api {
struct effect_runtime;
}
}  // namespace reshade

namespace ui::new_ui {

// Initialize Vulkan (experimental) tab (reserved for future hook init)
void InitVulkanTab();

// Draw the Vulkan (experimental) tab content: controls and debug info for Vulkan Reflex / frame pacing
void DrawVulkanTab(reshade::api::effect_runtime* runtime);

}  // namespace ui::new_ui
