#pragma once

// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
// Headers <Display Commander>
#include "display_settings.hpp"
#include "display/display_cache.hpp"
#include "dxgi/vram_info.hpp"
#include "globals.hpp"
#include "hooks/nvidia/ngx_hooks.hpp"
#include "hooks/present_traffic_tracking.hpp"
#include "hooks/vulkan/nvlowlatencyvk_hooks.hpp"
#include "hooks/windows_hooks/api_hooks.hpp"
#include "settings/advanced_tab_settings.hpp"
#include "settings/experimental_tab_settings.hpp"
#include "settings/main_tab_settings.hpp"
#include "settings/streamline_tab_settings.hpp"
#include "settings/swapchain_tab_settings.hpp"
#include "nvapi/nvapi_actual_refresh_rate_monitor.hpp"
#include "swapchain_events.hpp"
#include "ui/forkawesome.h"
#include "ui/new_ui/settings_wrapper.hpp"
#include "ui/ui_colors.hpp"
#include "utils.hpp"
#include "utils/detour_call_tracker.hpp"
#include "utils/general_utils.hpp"
#include "utils/logging.hpp"
#include "hooks/windows_hooks/window_proc_hooks.hpp"

// Libraries <ReShade / ImGui>
#include <imgui.h>
#include <reshade_imgui.hpp>

// Libraries <C++>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// Libraries <Windows.h>
#include <Windows.h>

// Libraries <Windows>
#include <d3d9types.h>
#include <psapi.h>

namespace ui::new_ui {

void DrawDisplaySettings_DisplayAndTarget(display_commander::ui::IImGuiWrapper& imgui,
                                         reshade::api::effect_runtime* runtime);
void DrawDisplaySettings_WindowModeAndApply(display_commander::ui::IImGuiWrapper& imgui);
void DrawDisplaySettings_FpsLimiter(display_commander::ui::IImGuiWrapper& imgui);
void DrawDisplaySettings_VSyncAndTearing(display_commander::ui::IImGuiWrapper& imgui);
void DrawAdhdMultiMonitorControls(display_commander::ui::IImGuiWrapper& imgui);

/** Horizontal offset from row start to the control after a leading checkbox (ImGui checkbox square + SameLine gap). */
inline float GetMainTabCheckboxColumnGutter(display_commander::ui::IImGuiWrapper& imgui) {
    const ImGuiStyle& st = imgui.GetStyle();
    const float frame_h = imgui.GetTextLineHeight() + st.FramePadding.y * 2.f;
    return frame_h + st.ItemInnerSpacing.x;
}

/** Dummy + SameLine so combos/sliders align with the FPS Limit / Background FPS Limit label column.
 *  When `compensate_for_parent_indent` is true, subtract one ImGui indent level. */
inline void PushFpsLimiterSliderColumnAlign(display_commander::ui::IImGuiWrapper& imgui, float checkbox_column_gutter,
                                           bool compensate_for_parent_indent = false) {
    float g = checkbox_column_gutter;
    if (compensate_for_parent_indent) {
        const float ind = imgui.GetStyle().IndentSpacing;
        g = (g > ind) ? (g - ind) : 0.0f;
    }
    if (g > 0.0f) {
        imgui.Dummy(ImVec2(g, imgui.GetTextLineHeight()));
        imgui.SameLine(0.0f, 0.0f);
    }
}

}  // namespace ui::new_ui

