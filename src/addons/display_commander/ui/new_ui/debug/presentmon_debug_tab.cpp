// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "presentmon_debug_tab.hpp"

// Headers <Display Commander>
#include "../../ui_colors.hpp"
#include "../../../settings/main_tab_settings.hpp"

// Libraries <ReShade / ImGui>
#include <imgui.h>

// Libraries <standard C++>
// (none)

namespace ui::new_ui::debug {

void DrawPresentMonDebugTab(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.TextColored(::ui::colors::TEXT_DIMMED,
                      "Build with DEBUG_TABS / bd.ps1 -DebugTabs. These toggles add NVAPI Reflex-derived lines to the "
                      "OSD. They are not Intel PresentMon CSV metrics (no ETW display timing).\n"
                      "The |Δlatency| / animation-error-style line is also toggled from the main tab: Performance "
                      "Overlay → GPU & memory → Animation error (NVAPI) (same setting).");

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    bool sim_on = settings::g_mainTabSettings.show_overlay_nvapi_sim_duration.GetValue();
    if (imgui.Checkbox("Overlay: sim_start -> sim_end (rolling avg)", &sim_on)) {
        settings::g_mainTabSettings.show_overlay_nvapi_sim_duration.SetValue(sim_on);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Newest frame, NvAPI GetLatency. Reflex latency reporting required.");
    }

    bool sim_to_rs = settings::g_mainTabSettings.show_overlay_nvapi_sim_end_to_rs_start.GetValue();
    if (imgui.Checkbox("Overlay: sim_end -> render_submit_start (rolling avg)", &sim_to_rs)) {
        settings::g_mainTabSettings.show_overlay_nvapi_sim_end_to_rs_start.SetValue(sim_to_rs);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Newest frame, NvAPI GetLatency. Reflex latency reporting required.");
    }

    bool rs_phase = settings::g_mainTabSettings.show_overlay_nvapi_rs_submit_duration.GetValue();
    if (imgui.Checkbox("Overlay: render_submit_start -> render_submit_end (rolling avg)", &rs_phase)) {
        settings::g_mainTabSettings.show_overlay_nvapi_rs_submit_duration.SetValue(rs_phase);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Newest frame, NvAPI GetLatency. Reflex latency reporting required.");
    }

    bool rs_s_to_pr_s = settings::g_mainTabSettings.show_overlay_nvapi_rs_start_to_present_start.GetValue();
    if (imgui.Checkbox("Overlay: render_submit_start -> present_start (rolling avg)", &rs_s_to_pr_s)) {
        settings::g_mainTabSettings.show_overlay_nvapi_rs_start_to_present_start.SetValue(rs_s_to_pr_s);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "present_start − render_submit_start (signed ms) when both timestamps non-zero. NvAPI GetLatency.");
    }

    bool rs_to_pr = settings::g_mainTabSettings.show_overlay_nvapi_rs_end_to_present_start.GetValue();
    if (imgui.Checkbox("Overlay: render_submit_end -> present_start (rolling avg)", &rs_to_pr)) {
        settings::g_mainTabSettings.show_overlay_nvapi_rs_end_to_present_start.SetValue(rs_to_pr);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "present_start − render_submit_end (signed ms) when both timestamps non-zero. NvAPI GetLatency.");
    }

    bool pr_phase = settings::g_mainTabSettings.show_overlay_nvapi_present_phase_duration.GetValue();
    if (imgui.Checkbox("Overlay: present_start -> present_end (rolling avg)", &pr_phase)) {
        settings::g_mainTabSettings.show_overlay_nvapi_present_phase_duration.SetValue(pr_phase);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Newest frame, NvAPI GetLatency. Reflex latency reporting required.");
    }

    bool pr_e_to_rs_e = settings::g_mainTabSettings.show_overlay_nvapi_present_end_to_rs_end.GetValue();
    if (imgui.Checkbox("Overlay: present_end -> render_submit_end (rolling avg)", &pr_e_to_rs_e)) {
        settings::g_mainTabSettings.show_overlay_nvapi_present_end_to_rs_end.SetValue(pr_e_to_rs_e);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "render_submit_end − present_end (signed ms) when both timestamps non-zero. NvAPI GetLatency.");
    }

    bool gpu_on = settings::g_mainTabSettings.show_overlay_nvapi_gpu_active_ms.GetValue();
    if (imgui.Checkbox("Overlay: gpu_active_render_time_us (rolling avg)", &gpu_on)) {
        settings::g_mainTabSettings.show_overlay_nvapi_gpu_active_ms.SetValue(gpu_on);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Newest frame, NvAPI GetLatency. Reflex latency reporting required.");
    }

    bool jitter_on = settings::g_mainTabSettings.show_overlay_nvapi_latency_jitter_abs.GetValue();
    if (imgui.Checkbox("Overlay: |delta latency vs prev frame| (rolling avg)", &jitter_on)) {
        settings::g_mainTabSettings.show_overlay_nvapi_latency_jitter_abs.SetValue(jitter_on);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "When frame_id advances: |change| vs previous Lat. row estimate. Main tab: OSD → GPU & "
            "memory → Animation error (NVAPI).");
    }

    bool marker_tid_on = settings::g_mainTabSettings.show_overlay_nvapi_setlatencymarker_threads.GetValue();
    if (imgui.Checkbox("Overlay: SetLatencyMarker detour thread (first 7 marker types)", &marker_tid_on)) {
        settings::g_mainTabSettings.show_overlay_nvapi_setlatencymarker_threads.SetValue(marker_tid_on);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Shows last calling thread ID per NVAPI marker type 0..6 (SIMULATION_START through INPUT_SAMPLE) as "
            "observed in the NvAPI_D3D_SetLatencyMarker hook. Does not require GetLatency.");
    }
}

}  // namespace ui::new_ui::debug
