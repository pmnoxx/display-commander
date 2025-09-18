#pragma once

#include <imgui.h>
#include <functional>

namespace ui::new_ui {

// Draw the swapchain tab content
void DrawSwapchainTab();

// Draw swapchain information section
void DrawSwapchainInfo();

// Draw adapter information section
void DrawAdapterInfo();

// Draw DXGI composition information
void DrawDxgiCompositionInfo();

// Draw swapchain event counters
void DrawSwapchainEventCounters();

} // namespace ui::new_ui
