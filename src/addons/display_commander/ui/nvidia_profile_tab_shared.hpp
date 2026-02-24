#pragma once

#include "imgui_wrapper_base.hpp"

namespace display_commander {
namespace ui {

/**
 * Draw the Nvidia Profile (Inspector) tab content.
 * Uses the provided ImGui wrapper so it can run in ReShade overlay or standalone UI.
 * api: graphics API (dx11, dx12, etc.) for display/context only.
 * imgui: backend to use for all UI calls.
 * show_advanced_profile_settings: non-null pointer to bool for "Show advanced profile settings"; read/written by this function.
 */
void DrawNvidiaProfileTab(GraphicsApi api, IImGuiWrapper& imgui, bool* show_advanced_profile_settings);

} // namespace ui
} // namespace display_commander
