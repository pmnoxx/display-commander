// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "reshade_overlay_event.hpp"

#include "config/display_commander_config.hpp"
#include "globals.hpp"
#include "settings/main_tab_settings.hpp"
#include "ui/imgui_wrapper_reshade.hpp"
#include "ui/new_ui/main_new_tab.hpp"
#include "ui/new_ui/new_ui_main.hpp"
#include "nvapi/nvapi_actual_refresh_rate_monitor.hpp"
#include "utils/detour_call_tracker.hpp"
#include "utils/timing.hpp"

// Libraries <ReShade> / <imgui>
#include <imgui.h>
#include <reshade.hpp>

// Libraries <standard C++>
#include <array>

namespace reshade_overlay_detail {

constexpr size_t kCursorOutlineSize = 3;
constexpr std::array<std::array<float, 2>, kCursorOutlineSize> kCursorOutline = {{
    {0.5f, 0.5f},
    {17.0f, 8.0f},
    // {(17.0f + 4.0f) * 0.4f, (8.0f + 20.0f) * 0.4f},
    {4.0f, 20.0f},
}};

void DrawCustomCursor(display_commander::ui::IImGuiWrapper& gui_wrapper) {
    const ImVec2 pos = gui_wrapper.GetIO().MousePos;
    const float s = 1.0f;

    display_commander::ui::IImDrawList* draw_list = gui_wrapper.GetForegroundDrawList();
    if (draw_list == nullptr) return;

    const ImU32 col_border = IM_COL32(0, 0, 0, 255);
    const ImU32 col_fill = IM_COL32(255, 255, 255, 255);
    const float thickness = 0.5f;

    // Build screen-space points from coordinate list
    double center_x = 0;
    double center_y = 0;
    ImVec2 pts[kCursorOutlineSize];
    for (size_t i = 0; i < kCursorOutlineSize; ++i) {
        pts[i].x = pos.x + kCursorOutline[i][0] * s;
        pts[i].y = pos.y + kCursorOutline[i][1] * s;
        center_x += pts[i].x;
        center_y += pts[i].y;
    }
    center_x /= kCursorOutlineSize;
    center_y /= kCursorOutlineSize;
    ImVec2 pts2[kCursorOutlineSize];
    for (size_t i = 0; i < kCursorOutlineSize; ++i) {
        pts2[i].x = pts[i].x + (pts[i].x - center_x) * 0.1f;
        pts2[i].y = pts[i].y + (pts[i].y - center_y) * 0.1f;
    }

    // Fill: triangle fan from tip (0) -> (1,2), (2,3), ..., (size-1,1)
    for (size_t i = 1; i < kCursorOutlineSize - 1; ++i) {
        draw_list->AddTriangleFilled(pts[0], pts[i], pts[i + 1], col_fill);
    }
    draw_list->AddTriangleFilled(pts[0], pts[kCursorOutlineSize - 1], pts[1], col_fill);

    // Outline: closed polygon
    for (size_t i = 0; i < kCursorOutlineSize; ++i) {
        const size_t j = (i + 1) % kCursorOutlineSize;
        draw_list->AddLine(pts2[i], pts2[j], col_border, thickness);
    }
}

void OnPerformanceOverlay_DisplayCommanderWindow(reshade::api::effect_runtime* runtime) {
    CALL_GUARD_NO_TS();
    display_commander::ui::ImGuiWrapperReshade overlay_wrapper;
    const float fixed_width = 1600.0f;
    float saved_x = settings::g_mainTabSettings.display_commander_ui_window_x.GetValue();
    float saved_y = settings::g_mainTabSettings.display_commander_ui_window_y.GetValue();
    static float last_saved_x = 0.0f;
    static float last_saved_y = 0.0f;
    if (saved_x > 0.0f || saved_y > 0.0f) {
        if (saved_x != last_saved_x || saved_y != last_saved_y) {
            overlay_wrapper.SetNextWindowPos(ImVec2(saved_x, saved_y), ImGuiCond_Once, ImVec2(0.f, 0.f));
            last_saved_x = saved_x;
            last_saved_y = saved_y;
        }
    }
    overlay_wrapper.SetNextWindowSize(ImVec2(fixed_width, 2160.0f), ImGuiCond_Always);

    bool window_open = true;
    if (overlay_wrapper.Begin("Display Commander", &window_open,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
        if (runtime != nullptr) {
            runtime->block_input_next_frame();
        }
        ImVec2 current_pos = overlay_wrapper.GetWindowPos();
        if (current_pos.x != saved_x || current_pos.y != saved_y) {
            settings::g_mainTabSettings.display_commander_ui_window_x.SetValue(current_pos.x);
            settings::g_mainTabSettings.display_commander_ui_window_y.SetValue(current_pos.y);
            last_saved_x = current_pos.x;
            last_saved_y = current_pos.y;
        }
        ui::new_ui::NewUISystem::GetInstance().Draw(runtime, overlay_wrapper);
    } else {
        settings::g_mainTabSettings.show_display_commander_ui.SetValue(false);
    }
    overlay_wrapper.End();
    if (!window_open) {
        settings::g_mainTabSettings.show_display_commander_ui.SetValue(false);
    }
    DrawCustomCursor(overlay_wrapper);
}

void OnPerformanceOverlay_TestWindow(reshade::api::effect_runtime* runtime, bool show_tooltips) {
    display_commander::ui::ImGuiWrapperReshade overlay_wrapper;
    float vertical_spacing = settings::g_mainTabSettings.overlay_vertical_spacing.GetValue();
    float horizontal_spacing = settings::g_mainTabSettings.overlay_horizontal_spacing.GetValue();
    overlay_wrapper.SetNextWindowPos(ImVec2(10.0f + horizontal_spacing, 10.0f + vertical_spacing), ImGuiCond_Always,
                                     ImVec2(0.f, 0.f));
    float bg_alpha = settings::g_mainTabSettings.overlay_background_alpha.GetValue();
    overlay_wrapper.SetNextWindowBgAlpha(bg_alpha);
    overlay_wrapper.SetNextWindowSize(ImVec2(450, 65), ImGuiCond_FirstUseEver);
    if (overlay_wrapper.Begin("Test Window", nullptr,
                              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize
                                  | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
                                  | ImGuiWindowFlags_AlwaysAutoResize)) {
        ui::new_ui::DrawPerformanceOverlayContent(overlay_wrapper, ui::new_ui::GetGraphicsApiFromRuntime(runtime),
                                                  show_tooltips);
    }
    overlay_wrapper.End();
}

}  // namespace reshade_overlay_detail

void OnPerformanceOverlay(reshade::api::effect_runtime* runtime) {
    CALL_GUARD_NO_TS();
    const bool show_display_commander_ui = settings::g_mainTabSettings.show_display_commander_ui.GetValue();
    const bool show_tooltips = show_display_commander_ui;

    if (show_display_commander_ui) {
        reshade_overlay_detail::OnPerformanceOverlay_DisplayCommanderWindow(runtime);
    }

    const LONGLONG overlay_allowed_after = g_performance_overlay_allowed_after_ns.load(std::memory_order_acquire);
    if (overlay_allowed_after == 0 || utils::get_now_ns() < overlay_allowed_after) {
        return;
    }

    bool show_actual_refresh_rate = settings::g_mainTabSettings.show_actual_refresh_rate.GetValue();
    bool show_refresh_rate_frame_times = settings::g_mainTabSettings.show_refresh_rate_frame_times.GetValue();
    bool show_performance_overlay = settings::g_mainTabSettings.show_performance_overlay.GetValue();
    if (show_performance_overlay && (show_actual_refresh_rate || show_refresh_rate_frame_times)) {
        if (!display_commander::nvapi::IsNvapiActualRefreshRateMonitoringActive()) {
            display_commander::nvapi::StartNvapiActualRefreshRateMonitoring();
        }
    } else {
        if (display_commander::nvapi::IsNvapiActualRefreshRateMonitoringActive()) {
            display_commander::nvapi::StopNvapiActualRefreshRateMonitoring();
        }
    }

    if (!settings::g_mainTabSettings.show_performance_overlay.GetValue()) {
        return;
    }
    reshade_overlay_detail::OnPerformanceOverlay_TestWindow(runtime, show_tooltips);
}

void OnRegisterOverlayDisplayCommander(reshade::api::effect_runtime* runtime) {
    CALL_GUARD_NO_TS();
    if (runtime != nullptr) {
        AddReShadeRuntime(runtime);
    }
    const bool show_display_commander_ui = settings::g_mainTabSettings.show_display_commander_ui.GetValue();
    if (show_display_commander_ui) {
        settings::g_mainTabSettings.show_display_commander_ui.SetValue(false);
    }
    {
        display_commander::ui::ImGuiWrapperReshade gui_wrapper;
        ui::new_ui::NewUISystem::GetInstance().Draw(runtime, gui_wrapper);
    }

    static LONGLONG last_save_time = utils::get_now_ns();
    LONGLONG now = utils::get_now_ns();
    if ((now - last_save_time) >= 5 * utils::SEC_TO_NS) {
        display_commander::config::save_config("periodic save (every 5 seconds)");
        last_save_time = now;
    }
}
