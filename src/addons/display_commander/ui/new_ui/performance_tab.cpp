#include "performance_tab.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../utils/perf_measurement.hpp"
#include "settings_wrapper.hpp"

#include <reshade_imgui.hpp>

namespace ui::new_ui {

void DrawPerformanceTab() {
    ImGui::Text("Performance Measurements");
    ImGui::Separator();

    if (CheckboxSetting(settings::g_experimentalTabSettings.performance_measurement_enabled,
                        "Performance measurement")) {
        // Auto-saved by CheckboxSetting
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "When enabled, measures CPU time spent in selected internal hot-path functions.\n"
            "When disabled, timing code does not run (no QPC reads, no stat updates).");
    }

    ImGui::SameLine();
    if (ImGui::Button("Reset stats")) {
        perf_measurement::ResetAll();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Reset all performance measurement counters (samples, totals, last).");
    }

    ImGui::Spacing();

    if (CheckboxSetting(settings::g_experimentalTabSettings.performance_suppression_enabled,
                        "Suppress execution (debug)")) {
        // Auto-saved by CheckboxSetting
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "WARNING: Suppression changes behavior and can break features.\n"
            "Use this temporarily to isolate performance hotspots.\n"
            "Suppressed functions early-out, skipping their normal work.");
    }

    ImGui::Spacing();

    if (ImGui::BeginTable("PerfMeasurementsTable", 7,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
                              | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Metric");
        ImGui::TableSetupColumn("Measure");
        ImGui::TableSetupColumn("Avg (us)");
        ImGui::TableSetupColumn("Last (us)");
        ImGui::TableSetupColumn("Max (us)");
        ImGui::TableSetupColumn("Samples");
        ImGui::TableSetupColumn("Suppress");
        ImGui::TableHeadersRow();

        auto row = [](const char* name, perf_measurement::Metric metric, settings::BoolSetting& enabled_setting,
                      const char* measure_checkbox_id, settings::BoolSetting& suppress_setting,
                      const char* suppress_checkbox_id) {
            const perf_measurement::Snapshot s = perf_measurement::GetSnapshot(metric);
            const double avg_us =
                (s.samples > 0) ? (static_cast<double>(s.total_ns) / static_cast<double>(s.samples) / 1000.0) : 0.0;
            const double last_us = static_cast<double>(s.last_ns) / 1000.0;
            const double max_us = static_cast<double>(s.max_ns) / 1000.0;

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(name);

            ImGui::TableSetColumnIndex(1);
            CheckboxSetting(enabled_setting, measure_checkbox_id);  // hidden label, unique ID

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.2f", avg_us);

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.2f", last_us);

            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.2f", max_us);

            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%llu", static_cast<unsigned long long>(s.samples));

            ImGui::TableSetColumnIndex(6);
            const bool suppress_master = settings::g_experimentalTabSettings.performance_suppression_enabled.GetValue();
            if (!suppress_master) {
                ImGui::BeginDisabled(true);
            }
            CheckboxSetting(suppress_setting, suppress_checkbox_id);
            if (!suppress_master) {
                ImGui::EndDisabled();
            }
        };

        row("Performance overlay (draw)", perf_measurement::Metric::Overlay,
            settings::g_experimentalTabSettings.perf_measure_overlay_enabled, "##perf_overlay",
            settings::g_experimentalTabSettings.perf_suppress_overlay, "##suppress_overlay");
        row("  -- Show Volume", perf_measurement::Metric::OverlayShowVolume,
            settings::g_experimentalTabSettings.perf_measure_overlay_show_volume_enabled, "##perf_overlay_show_volume",
            settings::g_experimentalTabSettings.perf_suppress_overlay_show_volume, "##suppress_overlay_show_volume");
        row("  -- Show VRR Status", perf_measurement::Metric::OverlayShowVrrStatus,
            settings::g_experimentalTabSettings.perf_measure_overlay_show_vrr_status_enabled,
            "##perf_overlay_show_vrr_status", settings::g_experimentalTabSettings.perf_suppress_overlay_show_vrr_status,
            "##suppress_overlay_show_vrr_status");
        row("HandlePresentBefore", perf_measurement::Metric::HandlePresentBefore,
            settings::g_experimentalTabSettings.perf_measure_handle_present_before_enabled, "##perf_handle_before",
            settings::g_experimentalTabSettings.perf_suppress_handle_present_before, "##suppress_handle_before");
        row("  -- Device Query", perf_measurement::Metric::HandlePresentBefore_DeviceQuery,
            settings::g_experimentalTabSettings.perf_measure_handle_present_before_device_query_enabled,
            "##perf_handle_before_device_query",
            settings::g_experimentalTabSettings.perf_suppress_handle_present_before_device_query,
            "##suppress_handle_before_device_query");
        row("  -- RecordFrameTime", perf_measurement::Metric::HandlePresentBefore_RecordFrameTime,
            settings::g_experimentalTabSettings.perf_measure_handle_present_before_record_frame_time_enabled,
            "##perf_handle_before_record_frame_time",
            settings::g_experimentalTabSettings.perf_suppress_handle_present_before_record_frame_time,
            "##suppress_handle_before_record_frame_time");
        row("  -- Frame Statistics", perf_measurement::Metric::HandlePresentBefore_FrameStatistics,
            settings::g_experimentalTabSettings.perf_measure_handle_present_before_frame_statistics_enabled,
            "##perf_handle_before_frame_statistics",
            settings::g_experimentalTabSettings.perf_suppress_handle_present_before_frame_statistics,
            "##suppress_handle_before_frame_statistics");
        row("TrackPresentStatistics", perf_measurement::Metric::TrackPresentStatistics,
            settings::g_experimentalTabSettings.perf_measure_track_present_statistics_enabled, "##perf_track_stats",
            settings::g_experimentalTabSettings.perf_suppress_track_present_statistics, "##suppress_track_stats");
        row("OnPresentFlags2", perf_measurement::Metric::OnPresentFlags2,
            settings::g_experimentalTabSettings.perf_measure_on_present_flags2_enabled, "##perf_present_flags2",
            settings::g_experimentalTabSettings.perf_suppress_on_present_flags2, "##suppress_present_flags2");
        row("HandlePresentAfter", perf_measurement::Metric::HandlePresentAfter,
            settings::g_experimentalTabSettings.perf_measure_handle_present_after_enabled, "##perf_handle_after",
            settings::g_experimentalTabSettings.perf_suppress_handle_present_after, "##suppress_handle_after");
        row("FlushCommandQueueFromSwapchain", perf_measurement::Metric::FlushCommandQueueFromSwapchain,
            settings::g_experimentalTabSettings.perf_measure_flush_command_queue_from_swapchain_enabled,
            "##perf_flush_cmdq", settings::g_experimentalTabSettings.perf_suppress_flush_command_queue_from_swapchain,
            "##suppress_flush_cmdq");
        row("EnqueueGPUCompletion", perf_measurement::Metric::EnqueueGPUCompletion,
            settings::g_experimentalTabSettings.perf_measure_enqueue_gpu_completion_enabled,
            "##perf_enqueue_gpu_completion", settings::g_experimentalTabSettings.perf_suppress_enqueue_gpu_completion,
            "##suppress_enqueue_gpu_completion");
        row("GetIndependentFlipState", perf_measurement::Metric::GetIndependentFlipState,
            settings::g_experimentalTabSettings.perf_measure_get_independent_flip_state_enabled,
            "##perf_get_independent_flip_state",
            settings::g_experimentalTabSettings.perf_suppress_get_independent_flip_state,
            "##suppress_get_independent_flip_state");

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Tip: Enable master measurement first, then disable individual metrics to reduce overhead.");
}

}  // namespace ui::new_ui
