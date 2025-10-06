#pragma once

#include <imgui.h>
#include <dxgi1_6.h>


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

// Helper functions for DXGI string conversion
const char* GetDXGIFormatString(DXGI_FORMAT format);
const char* GetDXGIScalingString(DXGI_SCALING scaling);
const char* GetDXGISwapEffectString(DXGI_SWAP_EFFECT effect);
const char* GetDXGIAlphaModeString(DXGI_ALPHA_MODE mode);
const char* GetDXGIColorSpaceString(DXGI_COLOR_SPACE_TYPE color_space);

} // namespace ui::new_ui
