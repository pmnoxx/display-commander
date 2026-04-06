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
                      "performance overlay. They are not Intel PresentMon CSV metrics (no ETW display timing).\n"
                      "The |Δlatency| / animation-error-style line is also toggled from the main tab: Performance "
                      "Overlay → GPU & memory → Animation error (NVAPI) (same setting).");

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    bool sim_on = settings::g_mainTabSettings.show_overlay_nvapi_sim_duration.GetValue();
    if (imgui.Checkbox("Overlay: Reflex sim duration (rolling avg)", &sim_on)) {
        settings::g_mainTabSettings.show_overlay_nvapi_sim_duration.SetValue(sim_on);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Rolling average of (simulation_end − simulation_start) from the newest completed frame in "
            "NvAPI_D3D_GetLatency. Reflex latency reporting required.");
    }

    bool sim_to_rs = settings::g_mainTabSettings.show_overlay_nvapi_sim_end_to_rs_start.GetValue();
    if (imgui.Checkbox("Overlay: sim end → render submit start (rolling avg)", &sim_to_rs)) {
        settings::g_mainTabSettings.show_overlay_nvapi_sim_end_to_rs_start.SetValue(sim_to_rs);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Rolling average of (renderSubmitStartTime − simulationEndTime) on the newest completed frame in "
            "NvAPI_D3D_GetLatency. Time from end of simulation to start of render submit. Reflex latency reporting "
            "required.");
    }

    bool rs_phase = settings::g_mainTabSettings.show_overlay_nvapi_rs_submit_duration.GetValue();
    if (imgui.Checkbox("Overlay: render submit start → end (rolling avg)", &rs_phase)) {
        settings::g_mainTabSettings.show_overlay_nvapi_rs_submit_duration.SetValue(rs_phase);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Rolling average of (renderSubmitEndTime − renderSubmitStartTime) on the newest completed frame. Reflex "
            "latency reporting required.");
    }

    bool rs_to_pr = settings::g_mainTabSettings.show_overlay_nvapi_rs_end_to_present_start.GetValue();
    if (imgui.Checkbox("Overlay: render submit end → present start (rolling avg)", &rs_to_pr)) {
        settings::g_mainTabSettings.show_overlay_nvapi_rs_end_to_present_start.SetValue(rs_to_pr);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Rolling average of (presentStartTime − renderSubmitEndTime) on the newest completed frame. Reflex latency "
            "reporting required.");
    }

    bool pr_phase = settings::g_mainTabSettings.show_overlay_nvapi_present_phase_duration.GetValue();
    if (imgui.Checkbox("Overlay: present start → present end (rolling avg)", &pr_phase)) {
        settings::g_mainTabSettings.show_overlay_nvapi_present_phase_duration.SetValue(pr_phase);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Rolling average of (presentEndTime − presentStartTime) on the newest completed frame. Reflex latency "
            "reporting required.");
    }

    bool gpu_on = settings::g_mainTabSettings.show_overlay_nvapi_gpu_active_ms.GetValue();
    if (imgui.Checkbox("Overlay: Reflex GPU active time (rolling avg)", &gpu_on)) {
        settings::g_mainTabSettings.show_overlay_nvapi_gpu_active_ms.SetValue(gpu_on);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Rolling average of gpuActiveRenderTimeUs (driver-reported active GPU time, excluding mid-frame idles) for "
            "the newest frame. Reflex latency reporting required.");
    }

    bool jitter_on = settings::g_mainTabSettings.show_overlay_nvapi_latency_jitter_abs.GetValue();
    if (imgui.Checkbox("Overlay: |Δlatency| vs previous frame (rolling avg)", &jitter_on)) {
        settings::g_mainTabSettings.show_overlay_nvapi_latency_jitter_abs.SetValue(jitter_on);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "When the newest frame_id advances, compares the overlay latency estimate (same formula as the Lat. row: "
            "PC latency + half GPU frame time + optional DLSS-G term) to the previous frame’s estimate; shows a "
            "rolling average of the absolute difference. Not PresentMon MsAnimationError.\n"
            "Same checkbox exists on the main tab under Performance Overlay → GPU & memory → Animation error (NVAPI).");
    }
}

}  // namespace ui::new_ui::debug
