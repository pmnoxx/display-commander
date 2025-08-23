#pragma once

#include <deps/imgui/imgui.h>
#include <functional>

namespace renodx::ui::new_ui {

// Draw the swapchain tab content
void DrawSwapchainTab();

// Draw swapchain information section
void DrawSwapchainInfo();

// Draw adapter information section  
void DrawAdapterInfo();

// Draw DXGI composition information
void DrawDxgiCompositionInfo();

} // namespace renodx::ui::new_ui
