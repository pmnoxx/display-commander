#include "main_new_tab.hpp"
#include "../../addon.hpp"
#include "../../adhd_multi_monitor/adhd_simple_api.hpp"
#include "../../audio/audio_management.hpp"
#include "../../dlss/dlss_indicator_manager.hpp"
#include "../../dxgi/vram_info.hpp"
#include "../../globals.hpp"
#include "../../hooks/api_hooks.hpp"
#include "../../hooks/loadlibrary_hooks.hpp"
#include "../../hooks/ngx_hooks.hpp"
#include "../../hooks/nvapi_hooks.hpp"
#include "../../hooks/window_proc_hooks.hpp"
#include "../../hooks/windows_hooks/windows_message_hooks.hpp"
#include "../../input_remapping/input_remapping.hpp"
#include "../../latency/reflex_provider.hpp"
#include "../../latent_sync/latent_sync_limiter.hpp"
#include "../../latent_sync/refresh_rate_monitor_integration.hpp"
#include "../../nvapi/nvapi_actual_refresh_rate_monitor.hpp"
#include "../../nvapi/reflex_manager.hpp"
#include "../../performance_types.hpp"
#include "../../presentmon/presentmon_manager.hpp"
#include "../../res/forkawesome.h"
#include "../../res/ui_colors.hpp"
#include "../../settings/advanced_tab_settings.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../settings/streamline_tab_settings.hpp"
#include "../../settings/swapchain_tab_settings.hpp"
#include "../../swapchain_events.hpp"
#include "../../utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/overlay_window_detector.hpp"
#include "../../utils/perf_measurement.hpp"
#include "../../utils/platform_api_detector.hpp"
#include "../../utils/version_check.hpp"
#include "../../widgets/resolution_widget/resolution_widget.hpp"
#include "imgui.h"
#include "new_ui_tabs.hpp"
#include "settings_wrapper.hpp"
#include "utils/timing.hpp"
#include "version.hpp"

#include <d3d9.h>
#include <d3d9types.h>
#include <dxgi.h>
#include <minwindef.h>
#include <shellapi.h>
#include <sysinfoapi.h>
#include <reshade_imgui.hpp>

// Define IID for IDirect3DDevice9 if not already defined
#ifndef IID_IDirect3DDevice9
EXTERN_C const GUID IID_IDirect3DDevice9 = {
    0xd0223b96, 0xbf7a, 0x43fd, {0x92, 0xbd, 0xa4, 0x3b, 0x8d, 0x82, 0x9a, 0x7b}};
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>

// Minimum CPU cores that can be selected (excludes 1-5)
static constexpr int MIN_CPU_CORES_SELECTABLE = 6;

namespace ui::new_ui {

namespace {

// Flag to indicate a restart is required after changing VSync/tearing options
std::atomic<bool> s_restart_needed_vsync_tearing{false};

// Helper function to check if injected Reflex is active
bool DidNativeReflexSleepRecently(uint64_t now_ns) {
    auto last_injected_call = g_nvapi_last_sleep_timestamp_ns.load();
    return last_injected_call > 0 && (now_ns - last_injected_call) < utils::SEC_TO_NS;  // 1s in nanoseconds
}
}  // anonymous namespace

void DrawFrameTimeGraph() {
    // Get frame time data from the performance ring buffer
    const uint32_t count = ::g_perf_ring.GetCount();

    if (count == 0) {
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "No frame time data available yet...");
        return;
    }

    // Collect frame times for the graph (last 300 samples for smooth display)
    static std::vector<float> frame_times;
    frame_times.clear();
    const uint32_t samples_to_collect = min(count, 300u);
    frame_times.reserve(samples_to_collect);

    for (uint32_t i = 0; i < samples_to_collect; ++i) {
        const ::PerfSample& sample = ::g_perf_ring.GetSample(i);
        if (sample.dt > 0.0f) {
            frame_times.push_back(sample.dt);  // Convert FPS to frame time in ms
        }
    }

    if (frame_times.empty()) {
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "No valid frame time data available...");
        return;
    }

    // Calculate statistics for the graph
    float min_frame_time = *std::ranges::min_element(frame_times);
    float max_frame_time = *std::ranges::max_element(frame_times);
    float avg_frame_time = 0.0f;
    for (float ft : frame_times) {
        avg_frame_time += ft;
    }
    avg_frame_time /= static_cast<float>(frame_times.size());

    // Calculate average FPS from average frame time
    float avg_fps = (avg_frame_time > 0.0f) ? (1.0f / avg_frame_time) : 0.0f;

    // Display statistics
    ImGui::Text("Min: %.2f ms | Max: %.2f ms | Avg: %.2f ms | FPS(avg): %.1f", min_frame_time, max_frame_time,
                avg_frame_time, avg_fps);

    // Create overlay text with current frame time
    std::string overlay_text = "Frame Time: " + std::to_string(frame_times.back()).substr(0, 4) + " ms";

    // Add sim-to-display latency if GPU measurement is enabled and we have valid data
    if (settings::g_mainTabSettings.gpu_measurement_enabled.GetValue() != 0
        && ::g_sim_to_display_latency_ns.load() > 0) {
        double sim_to_display_ms = (1.0 * ::g_sim_to_display_latency_ns.load() / utils::NS_TO_MS);
        overlay_text += " | Sim-to-Display Lat: " + std::to_string(sim_to_display_ms).substr(0, 4) + " ms";

        // Add GPU late time (how much later GPU finishes compared to Present)
        double gpu_late_ms = (1.0 * ::g_gpu_late_time_ns.load() / utils::NS_TO_MS);
        overlay_text += " | GPU Late: " + std::to_string(gpu_late_ms).substr(0, 4) + " ms";
    }

    // Set graph size and scale
    ImVec2 graph_size = ImVec2(-1.0f, 200.0f);  // Full width, 200px height
    float scale_min = 0.0f;                     // Always start from 0ms
    float scale_max = avg_frame_time * 4.f;     // Add some padding

    // Draw the frame time graph
    ImGui::PlotLines("Frame Time (ms)", frame_times.data(), static_cast<int>(frame_times.size()),
                     0,  // values_offset
                     overlay_text.c_str(), scale_min, scale_max, graph_size);

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Frame time graph showing recent frame times in milliseconds.\n"
            "Lower values = higher FPS, smoother gameplay.\n"
            "Spikes indicate frame drops or stuttering.");
    }

    // Frame Time Mode Selector
    ImGui::Spacing();
    ImGui::Text("Frame Time Mode:");
    ImGui::SameLine();

    int current_mode = static_cast<int>(settings::g_mainTabSettings.frame_time_mode.GetValue());
    const char* mode_items[] = {"Present-to-Present", "Frame Begin-to-Frame Begin", "Display Timing (GPU Completion)"};

    if (ImGui::Combo("##frame_time_mode", &current_mode, mode_items, 3)) {
        settings::g_mainTabSettings.frame_time_mode.SetValue(current_mode);
        LogInfo("Frame time mode changed to: %s", mode_items[current_mode]);
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Select which timing events to record for the frame time graph:\n"
            "- Present-to-Present: Records time between Present calls\n"
            "- Frame Begin-to-Frame Begin: Records time between frame begin events\n"
            "- Display Timing: Records when frames are actually displayed (based on GPU completion)\n"
            "  Note: Display Timing requires GPU measurement to be enabled");
    }
}

// Cached data for frame timeline; updated at most once per second to reduce flicker.
struct CachedTimelinePhase {
    const char* label;
    double start_ms;
    double end_ms;
    ImVec4 color;
};
static std::vector<CachedTimelinePhase> s_timeline_phases;
static double s_timeline_t_min = 0.0;
static double s_timeline_t_max = 1.0;
static double s_timeline_time_range = 1.0;
static LONGLONG s_timeline_last_update_ns = 0;

// Updates timeline cache from g_frame_data (last completed frame). All phase times are computed
// relative to sim_start_ns. Refreshes at most once per second.
static void UpdateFrameTimelineCache() {
    const uint64_t last_completed_frame_id = (g_global_frame_id.load() > 0) ? (g_global_frame_id.load() - 1) : 0;
    if (last_completed_frame_id == 0) {
        s_timeline_phases.clear();
        return;
    }
    const size_t slot = static_cast<size_t>(last_completed_frame_id % kFrameDataBufferSize);
    const FrameData& fd = g_frame_data[slot];
    if (fd.frame_id.load() != last_completed_frame_id || fd.sim_start_ns.load() <= 0
        || fd.present_end_time_ns.load() <= 0) {
        s_timeline_phases.clear();
        return;
    }

    const LONGLONG now_ns = utils::get_now_ns();
    const bool should_update =
        (s_timeline_phases.empty()) || (now_ns - s_timeline_last_update_ns >= static_cast<LONGLONG>(utils::SEC_TO_NS));
    if (!should_update) {
        return;
    }
    s_timeline_last_update_ns = now_ns;

    const LONGLONG base_ns = fd.sim_start_ns.load();
    const double to_ms = 1.0 / static_cast<double>(utils::NS_TO_MS);

    // All times relative to sim_start (base_ns) in milliseconds
    const double sim_start_ms = 0.0;
    const double sim_end_ms = (fd.submit_start_time_ns.load() > base_ns)
                                  ? (static_cast<double>(fd.submit_start_time_ns.load() - base_ns) * to_ms)
                                  : sim_start_ms;
    const double render_end_ms = (fd.render_submit_end_time_ns.load() > base_ns)
                                     ? (static_cast<double>(fd.render_submit_end_time_ns.load() - base_ns) * to_ms)
                                     : sim_end_ms;
    const double present_start_ms = (fd.present_start_time_ns.load() > base_ns)
                                        ? (static_cast<double>(fd.present_start_time_ns.load() - base_ns) * to_ms)
                                        : render_end_ms;
    const double present_end_ms = (fd.present_end_time_ns.load() > base_ns)
                                      ? (static_cast<double>(fd.present_end_time_ns.load() - base_ns) * to_ms)
                                      : present_start_ms;
    const double sleep_pre_start_ms =
        (fd.sleep_pre_present_start_time_ns.load() > base_ns)
            ? (static_cast<double>(fd.sleep_pre_present_start_time_ns.load() - base_ns) * to_ms)
            : render_end_ms;
    const double sleep_pre_end_ms =
        (fd.sleep_pre_present_end_time_ns.load() > base_ns)
            ? (static_cast<double>(fd.sleep_pre_present_end_time_ns.load() - base_ns) * to_ms)
            : present_start_ms;
    const double sleep_post_start_ms =
        (fd.sleep_post_present_start_time_ns.load() > base_ns)
            ? (static_cast<double>(fd.sleep_post_present_start_time_ns.load() - base_ns) * to_ms)
            : present_end_ms;
    const double sleep_post_end_ms =
        (fd.sleep_post_present_end_time_ns.load() > base_ns)
            ? (static_cast<double>(fd.sleep_post_present_end_time_ns.load() - base_ns) * to_ms)
            : present_end_ms;
    const bool has_gpu =
        (settings::g_mainTabSettings.gpu_measurement_enabled.GetValue() != 0 && fd.gpu_completion_time_ns.load() > 0);
    const double gpu_end_ms = has_gpu && fd.gpu_completion_time_ns.load() > base_ns
                                  ? (static_cast<double>(fd.gpu_completion_time_ns.load() - base_ns) * to_ms)
                                  : present_end_ms;

    const ImVec4 col_sim(0.2f, 0.75f, 0.35f, 1.0f);
    const ImVec4 col_render(0.35f, 0.55f, 1.0f, 1.0f);
    const ImVec4 col_reshade(0.75f, 0.4f, 1.0f, 1.0f);
    const ImVec4 col_sleep(0.5f, 0.5f, 0.55f, 1.0f);
    const ImVec4 col_present(1.0f, 0.55f, 0.2f, 1.0f);
    const ImVec4 col_gpu(0.95f, 0.35f, 0.35f, 1.0f);

    s_timeline_phases.clear();
    if (sim_end_ms > sim_start_ms) {
        s_timeline_phases.push_back({"Simulation", sim_start_ms, sim_end_ms, col_sim});
    }
    if (render_end_ms > sim_end_ms) {
        s_timeline_phases.push_back({"Render Submit", sim_end_ms, render_end_ms, col_render});
    }
    // ReShade: render_end up to sleep-pre start (or present_start if no sleep-pre)
    const double reshade_end_ms =
        (fd.sleep_pre_present_start_time_ns.load() > 0) ? sleep_pre_start_ms : present_start_ms;
    if (reshade_end_ms > render_end_ms) {
        s_timeline_phases.push_back({"ReShade", render_end_ms, reshade_end_ms, col_reshade});
    }
    if (sleep_pre_end_ms > sleep_pre_start_ms) {
        s_timeline_phases.push_back({"FPS Sleep (before)", sleep_pre_start_ms, sleep_pre_end_ms, col_sleep});
    }
    if (present_end_ms > present_start_ms) {
        s_timeline_phases.push_back({"Present", present_start_ms, present_end_ms, col_present});
    }
    if (sleep_post_end_ms > sleep_post_start_ms) {
        s_timeline_phases.push_back({"FPS Sleep (after)", sleep_post_start_ms, sleep_post_end_ms, col_sleep});
    }
    if (has_gpu && gpu_end_ms > present_start_ms) {
        s_timeline_phases.push_back({"GPU", present_start_ms, gpu_end_ms, col_gpu});
    }

    const double frame_ms = (sleep_post_end_ms > present_end_ms) ? sleep_post_end_ms : present_end_ms;
    s_timeline_t_min = 0.0;
    s_timeline_t_max = frame_ms;
    for (const auto& p : s_timeline_phases) {
        if (p.end_ms > s_timeline_t_max) {
            s_timeline_t_max = p.end_ms;
        }
    }
    if (s_timeline_t_max <= s_timeline_t_min) {
        s_timeline_t_max = s_timeline_t_min + 1.0;
    }
    s_timeline_time_range = s_timeline_t_max - s_timeline_t_min;
}

// Draw a single-frame timeline: one horizontal bar per phase, each on its own row.
// Uses start/end times (relative to frame start) so bars show when each phase began and ended.
// Data is cached and refreshed at most once per second to avoid flicker.
void DrawFrameTimelineBar() {
    UpdateFrameTimelineCache();
    if (s_timeline_phases.empty()) {
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "Frame timeline: no frame time data yet.");
        return;
    }

    const std::vector<CachedTimelinePhase>& phases = s_timeline_phases;
    const double t_min = s_timeline_t_min;
    const double t_max = s_timeline_t_max;
    const double time_range = s_timeline_time_range;

    ImGui::Text("Frame timeline (start to end, relative to sim start, updates every 1 s)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Each row = one phase. Bar shows when it started and ended (0 = sim start). "
            "Times from last completed frame (g_frame_data).");
    }
    ImGui::Spacing();

    const float row_height = 18.0f;
    const float bar_rounding = 2.0f;
    const float label_width = 150.0f;

    if (!ImGui::BeginTable("##FrameTimeline", 2, ImGuiTableFlags_None, ImVec2(-1.0f, 0.0f))) {
        return;
    }
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, label_width);
    ImGui::TableSetupColumn("Bar", ImGuiTableColumnFlags_WidthStretch);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if (draw_list == nullptr) {
        ImGui::EndTable();
        return;
    }

    for (const auto& p : phases) {
        const double duration = p.end_ms - p.start_ms;
        if (duration <= 0.0) {
            continue;
        }

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(p.label);

        ImGui::TableNextColumn();
        const ImVec2 bar_pos = ImGui::GetCursorScreenPos();
        const float bar_width = ImGui::GetContentRegionAvail().x;
        const ImVec2 bar_size(bar_width, row_height);

        // Bar in time range: start_ms -> end_ms maps to bar_pos.x -> bar_pos.x + bar_width
        const double frac_start = (p.start_ms - t_min) / time_range;
        const double frac_end = (p.end_ms - t_min) / time_range;
        float x0 = bar_pos.x + static_cast<float>(frac_start * static_cast<double>(bar_width));
        float x1 = bar_pos.x + static_cast<float>(frac_end * static_cast<double>(bar_width));
        if (x1 - x0 < 1.0f) {
            x1 = x0 + 1.0f;
        }
        if (x1 > bar_pos.x + bar_width) {
            x1 = bar_pos.x + bar_width;
        }
        if (x0 < bar_pos.x) {
            x0 = bar_pos.x;
        }

        draw_list->AddRectFilled(ImVec2(bar_pos.x, bar_pos.y), ImVec2(bar_pos.x + bar_width, bar_pos.y + bar_size.y),
                                 ImGui::GetColorU32(ImGuiCol_FrameBg), bar_rounding);
        draw_list->AddRectFilled(ImVec2(x0, bar_pos.y), ImVec2(x1, bar_pos.y + bar_size.y),
                                 ImGui::ColorConvertFloat4ToU32(p.color), bar_rounding);

        ImGui::Dummy(bar_size);

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s: %.2f ms - %.2f ms (%.2f ms)", p.label, p.start_ms, p.end_ms, duration);
        }
    }

    // Time axis row: empty label column, then "0 ms" and "t_max ms" in bar column
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("");
    ImGui::TableNextColumn();
    const float axis_bar_width = ImGui::GetContentRegionAvail().x;
    const float axis_cell_x = ImGui::GetCursorPosX();
    ImGui::TextColored(ui::colors::TEXT_DIMMED, "0 ms");
    ImGui::SameLine(axis_cell_x + axis_bar_width - 50.0f);
    ImGui::TextColored(ui::colors::TEXT_DIMMED, "%.1f ms", t_max);

    ImGui::EndTable();
}

// Compact frame timeline bar for performance overlay (smaller rows, fixed width).
void DrawFrameTimelineBarOverlay(bool show_tooltips) {
    UpdateFrameTimelineCache();
    if (s_timeline_phases.empty()) {
        return;
    }
    const std::vector<CachedTimelinePhase>& phases = s_timeline_phases;
    const double t_min = s_timeline_t_min;
    const double t_max = s_timeline_t_max;
    const double time_range = s_timeline_time_range;

    const float row_height = 10.0f;
    const float bar_rounding = 1.0f;
    const float label_width = 88.0f;
    const float graph_scale = settings::g_mainTabSettings.overlay_graph_scale.GetValue();
    const float total_width = 280.0f * graph_scale;

    if (!ImGui::BeginTable("##FrameTimelineOverlay", 2, ImGuiTableFlags_None, ImVec2(total_width, 0.0f))) {
        return;
    }
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, label_width);
    ImGui::TableSetupColumn("Bar", ImGuiTableColumnFlags_WidthStretch);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if (draw_list == nullptr) {
        ImGui::EndTable();
        return;
    }

    for (const auto& p : phases) {
        const double duration = p.end_ms - p.start_ms;
        if (duration <= 0.0) {
            continue;
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(p.label);
        ImGui::TableNextColumn();
        const ImVec2 bar_pos = ImGui::GetCursorScreenPos();
        const float bar_width = ImGui::GetContentRegionAvail().x;
        const ImVec2 bar_size(bar_width, row_height);

        const double frac_start = (p.start_ms - t_min) / time_range;
        const double frac_end = (p.end_ms - t_min) / time_range;
        float x0 = bar_pos.x + static_cast<float>(frac_start * static_cast<double>(bar_width));
        float x1 = bar_pos.x + static_cast<float>(frac_end * static_cast<double>(bar_width));
        if (x1 - x0 < 1.0f) {
            x1 = x0 + 1.0f;
        }
        if (x1 > bar_pos.x + bar_width) {
            x1 = bar_pos.x + bar_width;
        }
        if (x0 < bar_pos.x) {
            x0 = bar_pos.x;
        }
        draw_list->AddRectFilled(ImVec2(bar_pos.x, bar_pos.y), ImVec2(bar_pos.x + bar_width, bar_pos.y + bar_size.y),
                                 ImGui::GetColorU32(ImGuiCol_FrameBg), bar_rounding);
        draw_list->AddRectFilled(ImVec2(x0, bar_pos.y), ImVec2(x1, bar_pos.y + bar_size.y),
                                 ImGui::ColorConvertFloat4ToU32(p.color), bar_rounding);
        ImGui::Dummy(bar_size);
        if (show_tooltips && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s: %.2f - %.2f ms", p.label, p.start_ms, p.end_ms);
        }
    }
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("");
    ImGui::TableNextColumn();
    const float axis_bar_width = ImGui::GetContentRegionAvail().x;
    const float axis_cell_x = ImGui::GetCursorPosX();
    ImGui::TextColored(ui::colors::TEXT_DIMMED, "0");
    ImGui::SameLine(axis_cell_x + axis_bar_width - 28.0f);
    ImGui::TextColored(ui::colors::TEXT_DIMMED, "%.0f ms", t_max);
    ImGui::EndTable();
}

// Draw DLSS information (same format as performance overlay). Caller must pass pre-fetched summary.
void DrawDLSSInfo(const DLSSGSummary& dlssg_summary) {
    const bool any_dlss_active =
        dlssg_summary.dlss_active || dlssg_summary.dlss_g_active || dlssg_summary.ray_reconstruction_active;

    // FG Mode
    if (any_dlss_active
        && (dlssg_summary.fg_mode == "2x" || dlssg_summary.fg_mode == "3x" || dlssg_summary.fg_mode == "4x")) {
        ImGui::Text("FG: %s", dlssg_summary.fg_mode.c_str());
    } else {
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "FG: OFF");
    }

    // DLSS Internal Resolution (same format as performance overlay: internal -> output -> backbuffer)
    if (any_dlss_active && dlssg_summary.internal_resolution != "N/A") {
        std::string res_text = dlssg_summary.internal_resolution;
        const int bb_w = g_game_render_width.load();
        const int bb_h = g_game_render_height.load();
        if (bb_w > 0 && bb_h > 0) {
            res_text += " -> " + std::to_string(bb_w) + "x" + std::to_string(bb_h);
        }
        ImGui::Text("DLSS Res: %s", res_text.c_str());
    } else {
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "DLSS Res: N/A");
    }

    // DLSS Status
    if (any_dlss_active) {
        std::string status_text = "DLSS: On";
        if (dlssg_summary.ray_reconstruction_active) {
            status_text += " (RR)";
        } else if (dlssg_summary.dlss_g_active) {
            status_text += " (DLSS-G)";
        }
        ImGui::TextColored(ui::colors::TEXT_SUCCESS, "%s", status_text.c_str());
    } else {
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "DLSS: Off");
    }

    // DLSS Quality Preset
    if (any_dlss_active && dlssg_summary.quality_preset != "N/A") {
        ImGui::Text("DLSS Quality: %s", dlssg_summary.quality_preset.c_str());
    } else {
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "DLSS Quality: N/A");
    }

    // DLSS Render Preset
    if (any_dlss_active) {
        DLSSModelProfile model_profile = GetDLSSModelProfile();
        if (model_profile.is_valid) {
            std::string current_quality = dlssg_summary.quality_preset;
            int render_preset_value = 0;

            // Use Ray Reconstruction presets if RR is active, otherwise use Super Resolution presets
            if (dlssg_summary.ray_reconstruction_active) {
                if (current_quality == "Quality") {
                    render_preset_value = model_profile.rr_quality_preset;
                } else if (current_quality == "Balanced") {
                    render_preset_value = model_profile.rr_balanced_preset;
                } else if (current_quality == "Performance") {
                    render_preset_value = model_profile.rr_performance_preset;
                } else if (current_quality == "Ultra Performance") {
                    render_preset_value = model_profile.rr_ultra_performance_preset;
                } else if (current_quality == "Ultra Quality") {
                    render_preset_value = model_profile.rr_ultra_quality_preset;
                } else {
                    render_preset_value = model_profile.rr_quality_preset;
                }
            } else {
                if (current_quality == "Quality") {
                    render_preset_value = model_profile.sr_quality_preset;
                } else if (current_quality == "Balanced") {
                    render_preset_value = model_profile.sr_balanced_preset;
                } else if (current_quality == "Performance") {
                    render_preset_value = model_profile.sr_performance_preset;
                } else if (current_quality == "Ultra Performance") {
                    render_preset_value = model_profile.sr_ultra_performance_preset;
                } else if (current_quality == "Ultra Quality") {
                    render_preset_value = model_profile.sr_ultra_quality_preset;
                } else if (current_quality == "DLAA") {
                    render_preset_value = model_profile.sr_dlaa_preset;
                } else {
                    render_preset_value = model_profile.sr_quality_preset;
                }
            }

            std::string render_preset_letter = ConvertRenderPresetToLetter(render_preset_value);
            ImGui::Text("DLSS Render: %s", render_preset_letter.c_str());
        } else {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "DLSS Render: N/A");
        }
    } else {
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "DLSS Render: N/A");
    }

    // DLSS Render Preset override (same settings as Swapchain tab)
    if (any_dlss_active) {
        bool preset_override_enabled = settings::g_swapchainTabSettings.dlss_preset_override_enabled.GetValue();
        if (ImGui::Checkbox("Enable DLSS Preset Override##MainTab", &preset_override_enabled)) {
            settings::g_swapchainTabSettings.dlss_preset_override_enabled.SetValue(preset_override_enabled);
            ResetNGXPresetInitialization();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Override DLSS presets at runtime (Game Default / DLSS Default / Preset A, B, C, etc.). Same as "
                "Swapchain tab.");
        }

        if (settings::g_swapchainTabSettings.dlss_preset_override_enabled.GetValue()) {
            std::vector<std::string> preset_options =
                dlssg_summary.ray_reconstruction_active ? GetDLSSPresetOptions(dlssg_summary.supported_dlss_rr_presets)
                                                        : GetDLSSPresetOptions(dlssg_summary.supported_dlss_presets);
            std::vector<const char*> preset_cstrs;
            preset_cstrs.reserve(preset_options.size());
            for (const auto& option : preset_options) {
                preset_cstrs.push_back(option.c_str());
            }

            std::string current_value = dlssg_summary.ray_reconstruction_active
                                            ? settings::g_swapchainTabSettings.dlss_rr_preset_override.GetValue()
                                            : settings::g_swapchainTabSettings.dlss_sr_preset_override.GetValue();
            int current_selection = 0;
            for (size_t i = 0; i < preset_options.size(); ++i) {
                if (current_value == preset_options[i]) {
                    current_selection = static_cast<int>(i);
                    break;
                }
            }

            const char* combo_label =
                dlssg_summary.ray_reconstruction_active ? "RR Preset##MainTab" : "SR Preset##MainTab";
            if (ImGui::Combo(combo_label, &current_selection, preset_cstrs.data(),
                             static_cast<int>(preset_cstrs.size()))) {
                const std::string& new_value = preset_options[current_selection];
                if (dlssg_summary.ray_reconstruction_active) {
                    settings::g_swapchainTabSettings.dlss_rr_preset_override.SetValue(new_value);
                } else {
                    settings::g_swapchainTabSettings.dlss_sr_preset_override.SetValue(new_value);
                }
                ResetNGXPresetInitialization();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Preset: Game Default = no override, DLSS Default = 0, Preset A/B/C... = 1/2/3...");
            }
        }
    }

    // DLSS indicator (registry: NVIDIA NGXCore ShowDlssIndicator — same as Special-K / .reg toggle)
    if (any_dlss_active) {
        bool reg_enabled = dlss::DlssIndicatorManager::IsDlssIndicatorEnabled();
        ImGui::TextColored(reg_enabled ? ui::colors::TEXT_SUCCESS : ui::colors::TEXT_DIMMED, "DLSS indicator: %s",
                           reg_enabled ? "On" : "Off");
        if (ImGui::Checkbox("Enable DLSS indicator through Registry##MainTab", &reg_enabled)) {
            LogInfo("DLSS Indicator: %s", reg_enabled ? "enabled" : "disabled");
            if (!dlss::DlssIndicatorManager::SetDlssIndicatorEnabled(reg_enabled)) {
                LogInfo("DLSS Indicator: Apply to registry failed (run as admin or use .reg in Experimental tab).");
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Show DLSS on-screen indicator (resolution/version) in games. Writes NVIDIA registry; may require "
                "restart. Admin needed if apply fails.");
        }

        // DLSS-FG indicator text level (DLSSG_IndicatorText): 0=off, 1=minimal, 2=detailed
        const char* dlssg_indicator_items[] = {"Off", "Minimal", "Detailed"};
        int dlssg_indicator_current = static_cast<int>(dlss::DlssIndicatorManager::GetDlssgIndicatorTextLevel());
        if (dlssg_indicator_current < 0 || dlssg_indicator_current > 2) {
            dlssg_indicator_current = 0;
        }
        if (ImGui::Combo("DLSS-FG indicator text##MainTab", &dlssg_indicator_current, dlssg_indicator_items,
                         static_cast<int>(sizeof(dlssg_indicator_items) / sizeof(dlssg_indicator_items[0])))) {
            DWORD level = static_cast<DWORD>(dlssg_indicator_current);
            if (!dlss::DlssIndicatorManager::SetDlssgIndicatorTextLevel(level)) {
                LogInfo("DLSSG_IndicatorText: Apply to registry failed (run as admin).");
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "DLSS-FG on-screen indicator text level (registry DLSSG_IndicatorText). Off / Minimal / Detailed. "
                "May require restart. Admin needed if apply fails.");
        }
    }

    // DLSS.Feature.Create.Flags (own field)
    if (any_dlss_active) {
        int create_flags_val = 0;
        bool has_create_flags = ::g_ngx_parameters.get_as_int("DLSS.Feature.Create.Flags", create_flags_val);
        std::string create_flags_list;
        if (has_create_flags) {
            static const struct {
                unsigned int mask;
                const char* name;
            } k_dlss_feature_bits[] = {
                {1u << 0, "IsHDR"},         {1u << 1, "MVLowRes"},       {1u << 2, "MVJittered"},
                {1u << 3, "DepthInverted"}, {1u << 4, "Reserved_0"},     {1u << 5, "DoSharpening"},
                {1u << 6, "AutoExposure"},  {1u << 7, "AlphaUpscaling"}, {1u << 31, "IsInvalid"},
            };
            unsigned int uflags = static_cast<unsigned int>(create_flags_val);
            unsigned int known_mask = 0;
            for (const auto& b : k_dlss_feature_bits) {
                known_mask |= b.mask;
                if ((uflags & b.mask) != 0u) {
                    if (!create_flags_list.empty()) create_flags_list += ", ";
                    create_flags_list += b.name;
                }
            }
            unsigned int unknown_bits = uflags & ~known_mask;
            if (unknown_bits != 0) {
                if (!create_flags_list.empty()) create_flags_list += ", ";
                create_flags_list += "+0x";
                std::ostringstream oss;
                oss << std::hex << unknown_bits;
                create_flags_list += oss.str();
                create_flags_list += " (other)";
            }
            if (create_flags_list.empty()) create_flags_list = "None";
        }
        if (has_create_flags) {
            ImGui::Text("Create.Flags: %d (%s)", create_flags_val, create_flags_list.c_str());
        } else {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Create.Flags: N/A");
        }
    } else {
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "Create.Flags: N/A");
    }
    std::string ae_current = settings::g_swapchainTabSettings.dlss_forced_auto_exposure.GetValue();

    static auto original_auto_exposure_setting = ae_current;
    // Auto Exposure (info + override combo, same as Special-K). Shown only when Create.Flags has
    // the AutoExposure bit and we have a resolved state (ae_idx != 0, i.e. not N/A).
    bool show_auto_exposure = false;
    if (any_dlss_active) {
        int create_flags_ae = 0;
        const bool has_create_flags_ae = ::g_ngx_parameters.get_as_int("DLSS.Feature.Create.Flags", create_flags_ae);
        constexpr unsigned int k_auto_exposure_bit = 1u << 6;
        const bool flags_have_auto_exposure =
            has_create_flags_ae && ((static_cast<unsigned int>(create_flags_ae) & k_auto_exposure_bit) != 0u);
        int ae_idx = 0;  // 0 = N/A, 1 = Off, 2 = On (game state)
        if (dlssg_summary.auto_exposure == "Off") {
            ae_idx = 1;
        } else if (dlssg_summary.auto_exposure == "On") {
            ae_idx = 2;
        }
        show_auto_exposure = (ae_idx != 0 || original_auto_exposure_setting != ae_current) && flags_have_auto_exposure;
    }
    if (show_auto_exposure) {
        ImGui::Text("Auto Exposure: %s", dlssg_summary.auto_exposure.c_str());
        const char* ae_items[] = {"Game Default", "Force Off", "Force On"};
        int ae_idx = 0;
        if (ae_current == "Force Off") {
            ae_idx = 1;
        } else if (ae_current == "Force On") {
            ae_idx = 2;
        }
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("Force On").x + (ImGui::GetStyle().FramePadding.x * 2.0f) + 20.0f);
        if (ImGui::Combo("Auto Exposure Override##DLSS", &ae_idx, ae_items, 3)) {
            settings::g_swapchainTabSettings.dlss_forced_auto_exposure.SetValue(ae_items[ae_idx]);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Override DLSS auto-exposure. Takes effect when DLSS feature is (re)created.\n"
                "See Create.Flags field for current DLSS.Feature.Create.Flags value and decoded bits.");
        }
        if (original_auto_exposure_setting != ae_current) {
            ImGui::TextColored(ui::colors::TEXT_WARNING, "Restart required for change to take effect.");
        }
    } else {
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "Auto Exposure: N/A");
    }

    // DLSS DLL Versions
    ImGui::Spacing();
    if (dlssg_summary.dlss_dll_version != "N/A") {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "DLSS DLL: %s", dlssg_summary.dlss_dll_version.c_str());
        if (dlssg_summary.supported_dlss_presets != "N/A") {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), " [%s]", dlssg_summary.supported_dlss_presets.c_str());
        }
    } else {
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "DLSS DLL: N/A");
    }

    if (dlssg_summary.dlssg_dll_version != "N/A") {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "DLSS-G DLL: %s", dlssg_summary.dlssg_dll_version.c_str());
    } else {
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "DLSS-G DLL: N/A");
    }

    if (dlssg_summary.dlssd_dll_version != "N/A" && dlssg_summary.dlssd_dll_version != "Not loaded") {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "DLSS-D DLL: %s", dlssg_summary.dlssd_dll_version.c_str());
    } else {
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "DLSS-D DLL: N/A");
    }
    if (settings::g_streamlineTabSettings.dlss_override_enabled.GetValue()) {
        std::string not_applied;
        if (settings::g_streamlineTabSettings.dlss_override_dlss.GetValue() && !dlssg_summary.dlss_override_applied) {
            if (!not_applied.empty()) not_applied += ", ";
            not_applied += "nvngx_dlss.dll";
        }
        if (settings::g_streamlineTabSettings.dlss_override_dlss_rr.GetValue()
            && !dlssg_summary.dlssd_override_applied) {
            if (!not_applied.empty()) not_applied += ", ";
            not_applied += "nvngx_dlssd.dll";
        }
        if (settings::g_streamlineTabSettings.dlss_override_dlss_fg.GetValue()
            && !dlssg_summary.dlssg_override_applied) {
            if (!not_applied.empty()) not_applied += ", ";
            not_applied += "nvngx_dlssg.dll";
        }
        if (!not_applied.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                               ICON_FK_WARNING
                               " Override not applied for: %s. Restart game with override enabled before launch.",
                               not_applied.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "The game loaded these DLLs before our hooks were active. Enable override and restart the game to "
                    "use override versions.");
            }
        }
    }
}

// Draw native frame time graph (for frames shown to display via native swapchain Present)
void DrawNativeFrameTimeGraph() {
    // Check if limit real frames is enabled
    if (!settings::g_mainTabSettings.limit_real_frames.GetValue()) {
        ImGui::TextColored(ui::colors::TEXT_DIMMED,
                           "Native frame time graph requires limit real frames to be enabled.");
        return;
    }

    // Get native frame time data from the ring buffer
    const uint32_t count = ::g_native_frame_time_ring.GetCount();

    if (count == 0) {
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "No native frame time data available yet...");
        return;
    }

    // Collect frame times for the graph (last 300 samples for smooth display)
    static std::vector<float> frame_times;
    frame_times.clear();
    const uint32_t samples_to_collect = min(count, 300u);
    frame_times.reserve(samples_to_collect);

    for (uint32_t i = 0; i < samples_to_collect; ++i) {
        const ::PerfSample& sample = ::g_native_frame_time_ring.GetSample(i);
        if (sample.dt > 0.0f) {
            frame_times.push_back(1000.0 * sample.dt);  // Convert to frame time in ms
        }
    }

    if (frame_times.empty()) {
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "No valid native frame time data available...");
        return;
    }

    // Calculate statistics for the graph
    float min_frame_time = *std::ranges::min_element(frame_times);
    float max_frame_time = *std::ranges::max_element(frame_times);
    float avg_frame_time = 0.0f;
    for (float ft : frame_times) {
        avg_frame_time += ft;
    }
    avg_frame_time /= static_cast<float>(frame_times.size());

    // Calculate average FPS from average frame time
    float avg_fps = (avg_frame_time > 0.0f) ? (1000.0f / avg_frame_time) : 0.0f;

    // Display statistics
    ImGui::Text("Min: %.2f ms | Max: %.2f ms | Avg: %.2f ms | FPS(avg): %.1f", min_frame_time, max_frame_time,
                avg_frame_time, avg_fps);

    // Create overlay text with current frame time
    std::string overlay_text = "Native Frame Time: " + std::to_string(frame_times.back()).substr(0, 4) + " ms";

    // Set graph size and scale
    ImVec2 graph_size = ImVec2(-1.0f, 200.0f);  // Full width, 200px height
    float scale_min = 0.0f;                     // Always start from 0ms
    float scale_max = avg_frame_time * 4.f;     // Add some padding

    // Draw the native frame time graph
    ImGui::PlotLines("Native Frame Time (ms)", frame_times.data(), static_cast<int>(frame_times.size()),
                     0,  // values_offset
                     overlay_text.c_str(), scale_min, scale_max, graph_size);

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Native frame time graph showing frames actually shown to display via native swapchain Present.\n"
            "This tracks frames when limit real frames is enabled.\n"
            "Lower values = higher FPS, smoother gameplay.\n"
            "Spikes indicate frame drops or stuttering.");
    }
}

// Draw refresh rate frame times graph (actual refresh rate from NVAPI Adaptive Sync)
void DrawRefreshRateFrameTimesGraph(bool show_tooltips) {
    // Use actual refresh rate samples (NVAPI) - lock-free iteration
    static std::vector<float> frame_times;
    frame_times.clear();
    frame_times.reserve(256);  // Reserve for max samples

    display_commander::nvapi::ForEachNvapiActualRefreshRateSample([&](double rate) {
        if (rate > 0.0) {
            // Convert Hz to frame time in milliseconds
            frame_times.push_back(static_cast<float>(1000.0 / rate));
        }
    });

    if (frame_times.empty()) {
        if (display_commander::nvapi::IsNvapiActualRefreshRateMonitoringActive()
            && display_commander::nvapi::IsNvapiGetAdaptiveSyncDataFailingRepeatedly()) {
            ImGui::TextColored(ui::colors::TEXT_WARNING,
                               "NvAPI_DISP_GetAdaptiveSyncData failing repeatedly — no refresh rate data.");
        }
        return;  // Don't show anything if no valid data (monitor not active or no samples yet)
    }

    // PlotLines draws index 0 on the left: reverse so newest (present) is on the left, oldest (past) on the right
    std::reverse(frame_times.begin(), frame_times.end());

    // Calculate statistics for the graph
    float min_frame_time = *std::ranges::min_element(frame_times);
    float max_frame_time = *std::ranges::max_element(frame_times);
    float avg_frame_time = 0.0f;
    for (float ft : frame_times) {
        avg_frame_time += ft;
    }
    avg_frame_time /= static_cast<float>(frame_times.size());

    // Calculate standard deviation
    float variance = 0.0f;
    for (float ft : frame_times) {
        float diff = ft - avg_frame_time;
        variance += diff * diff;
    }
    variance /= static_cast<float>(frame_times.size());
    float std_deviation = std::sqrt(variance);

    // Fixed width for overlay (compact) - apply user scale
    float graph_scale = settings::g_mainTabSettings.overlay_graph_scale.GetValue();
    ImVec2 graph_size = ImVec2(300.0f * graph_scale, 60.0f * graph_scale);  // Scaled width and height
    float scale_min = 0.0f;
    float max_scale = settings::g_mainTabSettings.overlay_graph_max_scale.GetValue();
    float scale_max = avg_frame_time * max_scale;  // User-configurable max scale multiplier

    // Create overlay text with current refresh rate frame time
    //.. std::string overlay_text = "Refresh Frame Time: " + std::to_string(frame_times.back()).substr(0, 4) + " ms";
    // overlay_text += " | Avg: " + std::to_string(avg_frame_time).substr(0, 4) + " ms";

    // Draw chart background with transparency
    float chart_alpha = settings::g_mainTabSettings.overlay_chart_alpha.GetValue();
    ImVec4 bg_color = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
    bg_color.w *= chart_alpha;  // Apply transparency to background

    // Push style color so PlotLines uses the transparent background
    ImGui::PushStyleColor(ImGuiCol_FrameBg, bg_color);

    // Draw compact refresh rate frame time graph (line stays fully opaque)
    ImGui::PlotLines("##RefreshRateFrameTime", frame_times.data(), static_cast<int>(frame_times.size()),
                     0,        // values_offset
                     nullptr,  // overlay_text - no text for compact version
                     scale_min, scale_max, graph_size);

    // Restore original style color
    ImGui::PopStyleColor();

    // Display refresh rate time statistics if enabled
    bool show_refresh_rate_frame_time_stats = settings::g_mainTabSettings.show_refresh_rate_frame_time_stats.GetValue();
    if (show_refresh_rate_frame_time_stats) {
        ImGui::Text("Avg: %.2f ms | Dev: %.2f ms | Min: %.2f ms | Max: %.2f ms", avg_frame_time, std_deviation,
                    min_frame_time, max_frame_time);
    }

    if (ImGui::IsItemHovered() && show_tooltips) {
        ImGui::SetTooltip(
            "Actual refresh rate frame time graph (NvAPI_DISP_GetAdaptiveSyncData) in milliseconds.\n"
            "Lower values = higher refresh rate.\n"
            "Spikes indicate refresh rate variations (VRR, power management, etc.).");
    }
}

// Compact overlay version with fixed width
void DrawFrameTimeGraphOverlay(bool show_tooltips) {
    if (perf_measurement::IsSuppressionEnabled()
        && perf_measurement::IsMetricSuppressed(perf_measurement::Metric::Overlay)) {
        return;
    }

    perf_measurement::ScopedTimer perf_timer(perf_measurement::Metric::Overlay);

    // Get frame time data from the performance ring buffer
    const uint32_t count = ::g_perf_ring.GetCount();

    if (count == 0) {
        return;  // Don't show anything if no data
    }
    const uint32_t samples_to_display = min(count, 256u);

    // Collect frame times for the graph (last 256 samples for compact display)
    static std::vector<float> frame_times;
    frame_times.clear();
    frame_times.reserve(samples_to_display);

    for (uint32_t i = 0; i < samples_to_display; ++i) {
        const ::PerfSample& sample = ::g_perf_ring.GetSample(i);
        frame_times.push_back(1000.0 * sample.dt);  // Convert FPS to frame time in ms
    }

    if (frame_times.empty()) {
        return;  // Don't show anything if no valid data
    }

    // Calculate statistics for the graph
    float min_frame_time = *std::ranges::min_element(frame_times);
    float max_frame_time = *std::ranges::max_element(frame_times);
    float avg_frame_time = 0.0f;
    for (float ft : frame_times) {
        avg_frame_time += ft;
    }
    avg_frame_time /= static_cast<float>(frame_times.size());

    // Calculate standard deviation
    float variance = 0.0f;
    for (float ft : frame_times) {
        float diff = ft - avg_frame_time;
        variance += diff * diff;
    }
    variance /= static_cast<float>(frame_times.size());
    float std_deviation = std::sqrt(variance);

    // Fixed width for overlay (compact) - apply user scale
    float graph_scale = settings::g_mainTabSettings.overlay_graph_scale.GetValue();
    ImVec2 graph_size = ImVec2(300.0f * graph_scale, 60.0f * graph_scale);  // Scaled width and height
    float scale_min = 0.0f;
    float max_scale = settings::g_mainTabSettings.overlay_graph_max_scale.GetValue();
    float scale_max = avg_frame_time * max_scale;  // User-configurable max scale multiplier

    // Draw chart background with transparency
    float chart_alpha = settings::g_mainTabSettings.overlay_chart_alpha.GetValue();
    ImVec4 bg_color = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
    bg_color.w *= chart_alpha;  // Apply transparency to background

    // Push style color so PlotLines uses the transparent background
    ImGui::PushStyleColor(ImGuiCol_FrameBg, bg_color);

    // Draw compact frame time graph (line stays fully opaque)
    ImGui::PlotLines("##FrameTime", frame_times.data(), static_cast<int>(frame_times.size()),
                     0,        // values_offset
                     nullptr,  // overlay_text - no text for compact version
                     scale_min, scale_max, graph_size);

    // Restore original style color
    ImGui::PopStyleColor();

    // Display frame time statistics if enabled
    bool show_frame_time_stats = settings::g_mainTabSettings.show_frame_time_stats.GetValue();
    if (show_frame_time_stats) {
        ImGui::Text("Avg: %.2f ms | Dev: %.2f ms | Min: %.2f ms | Max: %.2f ms", avg_frame_time, std_deviation,
                    min_frame_time, max_frame_time);
    }

    if (ImGui::IsItemHovered() && show_tooltips) {
        ImGui::SetTooltip("Frame time graph (last 256 frames)\nAvg: %.2f ms | Max: %.2f ms", avg_frame_time,
                          max_frame_time);
    }
}

// Compact overlay version for native frame times (frames shown to display via native swapchain Present)
void DrawNativeFrameTimeGraphOverlay(bool show_tooltips) {
    if (perf_measurement::IsSuppressionEnabled()
        && perf_measurement::IsMetricSuppressed(perf_measurement::Metric::Overlay)) {
        return;
    }

    perf_measurement::ScopedTimer perf_timer(perf_measurement::Metric::Overlay);

    // Get native frame time data from the ring buffer
    const uint32_t count = ::g_native_frame_time_ring.GetCount();

    if (count == 0) {
        return;  // Don't show anything if no data
    }
    const uint32_t samples_to_display = min(count, 256u);

    // Collect frame times for the graph (last 256 samples for compact display)
    static std::vector<float> frame_times;
    frame_times.clear();
    frame_times.reserve(samples_to_display);

    for (uint32_t i = 0; i < samples_to_display; ++i) {
        const ::PerfSample& sample = ::g_native_frame_time_ring.GetSample(i);
        if (sample.dt > 0.0f) {
            frame_times.push_back(1000.0 * sample.dt);  // Convert to frame time in ms
        }
    }

    if (frame_times.empty()) {
        return;  // Don't show anything if no valid data
    }

    // Calculate statistics for the graph
    float max_frame_time = *std::ranges::max_element(frame_times);
    float avg_frame_time = 0.0f;
    for (float ft : frame_times) {
        avg_frame_time += ft;
    }
    avg_frame_time /= static_cast<float>(frame_times.size());

    // Fixed width for overlay (compact) - apply user scale
    float graph_scale = settings::g_mainTabSettings.overlay_graph_scale.GetValue();
    ImVec2 graph_size = ImVec2(300.0f * graph_scale, 60.0f * graph_scale);  // Scaled width and height
    float scale_min = 0.0f;
    float max_scale = settings::g_mainTabSettings.overlay_graph_max_scale.GetValue();
    float scale_max = avg_frame_time * max_scale;  // User-configurable max scale multiplier

    // Draw chart background with transparency
    float chart_alpha = settings::g_mainTabSettings.overlay_chart_alpha.GetValue();
    ImVec4 bg_color = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
    bg_color.w *= chart_alpha;  // Apply transparency to background

    // Push style color so PlotLines uses the transparent background
    ImGui::PushStyleColor(ImGuiCol_FrameBg, bg_color);

    // Draw compact native frame time graph (line stays fully opaque)
    ImGui::PlotLines("##NativeFrameTime", frame_times.data(), static_cast<int>(frame_times.size()),
                     0,        // values_offset
                     nullptr,  // overlay_text - no text for compact version
                     scale_min, scale_max, graph_size);

    // Restore original style color
    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered() && show_tooltips) {
        ImGui::SetTooltip("Native frame time graph (last 256 frames)\nAvg: %.2f ms | Max: %.2f ms", avg_frame_time,
                          max_frame_time);
    }
}

void InitMainNewTab() {
    static bool settings_loaded_once = false;
    if (!settings_loaded_once) {
        // Settings already loaded at startup
        settings::g_mainTabSettings.LoadSettings();
        s_window_mode = static_cast<WindowMode>(settings::g_mainTabSettings.window_mode.GetValue());
        s_aspect_index = static_cast<AspectRatioType>(settings::g_mainTabSettings.aspect_index.GetValue());
        s_window_alignment = static_cast<WindowAlignment>(settings::g_mainTabSettings.alignment.GetValue());
        // FPS limits are now automatically synced via FloatSettingRef
        // Audio mute settings are automatically synced via BoolSettingRef
        // Background mute settings are automatically synced via BoolSettingRef
        // VSync & Tearing - all automatically synced via BoolSettingRef

        // Apply loaded mute state immediately if manual mute is enabled
        // (BoolSettingRef already synced s_audio_mute, but we need to apply it to the audio system)
        if (settings::g_mainTabSettings.audio_mute.GetValue()) {
            if (::SetMuteForCurrentProcess(true)) {
                ::g_muted_applied.store(true);
                LogInfo("Audio mute state loaded and applied from settings");
            } else {
                LogWarn("Failed to apply loaded mute state");
            }
        }

        // Update input blocking system with loaded settings
        // Input blocking is now handled by Windows message hooks

        // Apply ADHD Multi-Monitor Mode settings
        adhd_multi_monitor::api::SetEnabled(settings::g_mainTabSettings.adhd_multi_monitor_enabled.GetValue());

        settings_loaded_once = true;

        // FPS limiter mode
        s_fps_limiter_mode.store(static_cast<FpsLimiterMode>(settings::g_mainTabSettings.fps_limiter_mode.GetValue()));
        // Scanline offset and VBlank Sync Divisor are now automatically synced via IntSettingRef

        // Initialize resolution widget
        display_commander::widgets::resolution_widget::InitializeResolutionWidget();

        // Sync log level from settings
        // Note: The setting already updates g_min_log_level via SetValue() when loaded,
        // so we don't need to manually update it here. The setting wrapper handles the
        // index-to-enum conversion automatically.
    }
}

void DrawAdvancedSettings() {
    // Advanced Settings Control
    {
        bool advanced_settings = settings::g_mainTabSettings.advanced_settings_enabled.GetValue();
        if (ImGui::Checkbox(ICON_FK_FILE_CODE " Show All Tabs", &advanced_settings)) {
            settings::g_mainTabSettings.advanced_settings_enabled.SetValue(advanced_settings);
            LogInfo("Advanced settings %s", advanced_settings ? "enabled" : "disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Enable advanced settings to show advanced tabs (Advanced, Debug, HID Input, etc.).\n"
                "When disabled, advanced tabs will be hidden to simplify the interface.");
        }
    }

    ImGui::Spacing();

    // Logging Level Control
    // Note: ComboSettingEnumRefWrapper already updates g_min_log_level via SetValue(),
    // so we don't need to manually update it here. Just log the change.
    if (ComboSettingEnumRefWrapper(settings::g_mainTabSettings.log_level, "Logging Level")) {
        // Always log the level change (using LogCurrentLogLevel which uses LogError)
        LogCurrentLogLevel();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Controls the minimum log level to display:\n\n"
            "- Error Only: Only error messages\n"
            "- Warning: Errors and warnings\n"
            "- Info: Errors, warnings, and info messages\n"
            "- Debug (Everything): All log messages (default)");
    }

    ImGui::Spacing();

    // Individual Tab Visibility Settings
    ImGui::Text("Show Individual Tabs:");
    ImGui::Indent();

    if (ui::new_ui::g_tab_manager.HasTab("advanced")) {
        if (CheckboxSetting(settings::g_mainTabSettings.show_advanced_tab, "Show Advanced Tab")) {
            LogInfo("Show Advanced tab %s",
                    settings::g_mainTabSettings.show_advanced_tab.GetValue() ? "enabled" : "disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows the Advanced tab even when 'Show All Tabs' is disabled.");
        }
    }

    if (ui::new_ui::g_tab_manager.HasTab("window_info")) {
        if (CheckboxSetting(settings::g_mainTabSettings.show_window_info_tab, "Show Window Info Tab")) {
            LogInfo("Show Window Info tab %s",
                    settings::g_mainTabSettings.show_window_info_tab.GetValue() ? "enabled" : "disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows the Window Info tab even when 'Show All Tabs' is disabled.");
        }
    }

    if (ui::new_ui::g_tab_manager.HasTab("swapchain")) {
        if (CheckboxSetting(settings::g_mainTabSettings.show_swapchain_tab, "Show Swapchain Tab")) {
            LogInfo("Show Swapchain tab %s",
                    settings::g_mainTabSettings.show_swapchain_tab.GetValue() ? "enabled" : "disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows the Swapchain tab even when 'Show All Tabs' is disabled.");
        }
    }

    if (ui::new_ui::g_tab_manager.HasTab("controller")) {
        if (CheckboxSetting(settings::g_mainTabSettings.show_controller_tab, "Show Controller Tab")) {
            LogInfo("Show Controller tab %s",
                    settings::g_mainTabSettings.show_controller_tab.GetValue() ? "enabled" : "disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Shows the Controller tab (XInput monitoring and remapping) even when 'Show All Tabs' is disabled.");
        }
    }

    if (ui::new_ui::g_tab_manager.HasTab("streamline")) {
        if (CheckboxSetting(settings::g_mainTabSettings.show_streamline_tab, "Show Streamline Tab")) {
            LogInfo("Show Streamline tab %s",
                    settings::g_mainTabSettings.show_streamline_tab.GetValue() ? "enabled" : "disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows the Streamline tab even when 'Show All Tabs' is disabled.");
        }
    }

    if (ui::new_ui::g_tab_manager.HasTab("experimental")) {
        if (CheckboxSetting(settings::g_mainTabSettings.show_experimental_tab, "Show Debug Tab")) {
            LogInfo("Show Debug tab %s",
                    settings::g_mainTabSettings.show_experimental_tab.GetValue() ? "enabled" : "disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows the Debug tab even when 'Show All Tabs' is disabled.");
        }
    }

    ImGui::Unindent();

    ImGui::Spacing();
}

void DrawMainNewTab(reshade::api::effect_runtime* runtime) {
    // Load saved settings once and sync legacy globals
    g_rendering_ui_section.store("ui:tab:main_new:entry", std::memory_order_release);

    // Config save failure warning at the top
    g_rendering_ui_section.store("ui:tab:main_new:warnings:config", std::memory_order_release);
    auto config_save_failure_path = g_config_save_failure_path.load();
    if (config_save_failure_path != nullptr && !config_save_failure_path->empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ui::colors::TEXT_ERROR, ICON_FK_WARNING " Error: Failed to save config to %s",
                           config_save_failure_path->c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("The configuration file could not be saved. Check file permissions and disk space.");
        }
        ImGui::Spacing();
    }

    g_rendering_ui_section.store("ui:tab:main_new:warnings:load_from_dll", std::memory_order_release);
    // LoadFromDllMain warning
    int32_t load_from_dll_main_value = 0;
    if (reshade::get_config_value(nullptr, "ADDON", "LoadFromDllMain", load_from_dll_main_value)
        && load_from_dll_main_value == 1) {
        ImGui::Spacing();
        ImGui::TextColored(ui::colors::TEXT_WARNING,
                           ICON_FK_WARNING " WARNING: LoadFromDllMain is set to 1 in ReShade configuration");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "LoadFromDllMain=1 can cause compatibility issues with some games and addons. "
                "Consider disabling it in the Advanced tab or ReShade.ini if you experience problems.");
        }
        ImGui::Spacing();
    }

    g_rendering_ui_section.store("ui:tab:main_new:warnings:multi_version", std::memory_order_release);
    // Multiple Display Commander versions warning
    auto other_dc_version = g_other_dc_version_detected.load();
    if (other_dc_version != nullptr && !other_dc_version->empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ui::colors::TEXT_ERROR,
                           ICON_FK_WARNING " ERROR: Multiple Display Commander versions detected!");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Another Display Commander instance (v%s) is loaded in this process. "
                "This may cause conflicts and unexpected behavior. Please ensure only one version is loaded.",
                other_dc_version->c_str());
        }
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::TEXT_ERROR, "Other version: v%s", other_dc_version->c_str());
        ImGui::Spacing();
    }

    g_rendering_ui_section.store("ui:tab:main_new:warnings:multi_swapchain", std::memory_order_release);
    // Multiple swapchains (ReShade runtimes) warning
    const size_t runtime_count = GetReShadeRuntimeCount();
    if (runtime_count > 1) {
        ImGui::Spacing();
        ImGui::TextColored(ui::colors::TEXT_WARNING,
                           ICON_FK_WARNING " WARNING: Multiple swapchains detected (%zu ReShade runtimes)",
                           runtime_count);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "More than one swapchain/runtime is active. Some features may target only the first runtime. "
                "This can happen with multi-window or multi-context games.");
        }
        ImGui::Spacing();
    }

    g_rendering_ui_section.store("ui:tab:main_new:version_build", std::memory_order_release);
    // Version and build information at the top
    // if (ImGui::CollapsingHeader("Display Commander", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextColored(ui::colors::TEXT_DEFAULT, "Version: %s | Build: %s %s", DISPLAY_COMMANDER_VERSION_STRING,
                           DISPLAY_COMMANDER_BUILD_DATE, DISPLAY_COMMANDER_BUILD_TIME);

        // Version check and update UI
        {
            using namespace display_commander::utils::version_check;
            auto& state = GetVersionCheckState();

            // Check for updates on first load (only once)
            static bool initial_check_done = false;
            if (!initial_check_done && !state.checking.load()) {
                CheckForUpdates();
                initial_check_done = true;
            }

            ImGui::SameLine();
            ImGui::Spacing();
            ImGui::SameLine();

            VersionComparison status = state.status.load();
            std::string* latest_version_ptr = state.latest_version.load();
            std::string* error_ptr = state.error_message.load();

            if (status == VersionComparison::Checking) {
                ImGui::TextColored(ui::colors::TEXT_DIMMED, ICON_FK_REFRESH " Checking for updates...");
            } else if (status == VersionComparison::UpdateAvailable && latest_version_ptr != nullptr) {
                ImGui::TextColored(ui::colors::TEXT_WARNING, ICON_FK_WARNING " Update available: v%s",
                                   latest_version_ptr->c_str());
                ImGui::SameLine();

// Determine if we're 64-bit or 32-bit (simplified check - you may want to improve this)
#ifdef _WIN64
                bool is_64bit = true;
#else
                bool is_64bit = false;
#endif

                std::string* download_url = is_64bit ? state.download_url_64.load() : state.download_url_32.load();
                if (download_url != nullptr && !download_url->empty()) {
                    if (ImGui::Button("Download")) {
                        // Run download in background thread
                        std::thread download_thread([is_64bit]() {
                            if (DownloadUpdate(is_64bit)) {
                                LogInfo("Update downloaded successfully");
                            } else {
                                LogError("Failed to download update");
                            }
                        });
                        download_thread.detach();
                    }
                    if (ImGui::IsItemHovered()) {
                        auto download_dir = GetDownloadDirectory();
                        std::string download_path_str = download_dir.string();
                        ImGui::SetTooltip(
                            "Download will be saved to:\n%s\nFilename: zzz_display_commander_BUILD.addon%s",
                            download_path_str.c_str(), is_64bit ? "64" : "32");
                    }
                }
            } else if (status == VersionComparison::UpToDate) {
                ImGui::TextColored(ui::colors::TEXT_SUCCESS, ICON_FK_OK " Up to date");
            } else if (status == VersionComparison::CheckFailed && error_ptr != nullptr) {
                ImGui::TextColored(ui::colors::TEXT_ERROR, ICON_FK_WARNING " Check failed: %s", error_ptr->c_str());
            }

            // Manual check button
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FK_REFRESH)) {
                if (!state.checking.load()) {
                    CheckForUpdates();
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Check for updates");
            }
        }

        // Display current graphics API with feature level/version
        int api_value = g_last_reshade_device_api.load();
        if (api_value != 0) {
            reshade::api::device_api api = static_cast<reshade::api::device_api>(api_value);
            uint32_t api_version = g_last_api_version.load();
            ImGui::SameLine();

            if (api == reshade::api::device_api::d3d9 && s_d3d9e_upgrade_successful.load()) {
                api_version = 0x9100;  // due to reshade's bug.
            }

            // Display API with version/feature level and bitness
#ifdef _WIN64
            const char* bitness_label = "64-bit";
#else
            const char* bitness_label = "32-bit";
#endif
            if (api_version != 0) {
                std::string api_string = GetDeviceApiVersionString(api, api_version);
                ImGui::TextColored(ui::colors::TEXT_LABEL, "| %s: %s", bitness_label, api_string.c_str());
            } else {
                ImGui::TextColored(ui::colors::TEXT_LABEL, "| %s: %s", bitness_label, GetDeviceApiString(api));
            }
        }

        // Display detected platform APIs (Steam, Epic, GOG, etc.)
        {
            using namespace display_commander::utils;
            static std::vector<PlatformAPI> cached_detected_apis;
            static DWORD last_check_time = 0;
            DWORD current_time = GetTickCount();

            // Update cached list every 2 seconds to avoid performance impact
            if (current_time - last_check_time > 2000) {
                cached_detected_apis = GetDetectedPlatformAPIs();
                last_check_time = current_time;
            }

            if (!cached_detected_apis.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(ui::colors::TEXT_LABEL, "| Platform: ");
                ImGui::SameLine();

                // Display all detected platforms, comma-separated
                for (size_t i = 0; i < cached_detected_apis.size(); ++i) {
                    const char* api_name = GetPlatformAPIName(cached_detected_apis[i]);
                    ImGui::TextColored(ui::colors::TEXT_HIGHLIGHT, "%s", api_name);
                    if (i < cached_detected_apis.size() - 1) {
                        ImGui::SameLine();
                        ImGui::TextColored(ui::colors::TEXT_DIMMED, ", ");
                        ImGui::SameLine();
                    }
                }
            }
        }

        // Ko-fi support button
        ImGui::Spacing();
        ImGui::TextColored(ui::colors::TEXT_LABEL, "Support the project:");
        ui::colors::PushIconColor(ui::colors::ICON_SPECIAL);
        if (ImGui::Button(ICON_FK_PLUS " Buy me a coffee on Ko-fi")) {
            ShellExecuteA(nullptr, "open", "https://ko-fi.com/pmnox", nullptr, nullptr, SW_SHOW);
        }
        ui::colors::PopIconColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Support Display Commander development with a coffee!");
        }
    }

    /*
if (enabled_experimental_features) {
    // Gamma Control Warning
    uint32_t gamma_control_calls = g_dxgi_output_event_counters[DXGI_OUTPUT_EVENT_SETGAMMACONTROL].load();
    if (gamma_control_calls > 0) {
        ImGui::Spacing();
        ImGui::TextColored(ui::colors::TEXT_WARNING,
                           ICON_FK_WARNING
                           " WARNING: Game is using gamma control (SetGammaControl called %u times)",
                           gamma_control_calls);
        LogInfo("TODO: Implement supressing SetGammaControl calls feature.");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "The game is actively modifying display gamma settings. This may affect color accuracy and HDR "
                "functionality. Check the Swapchain tab for more details.");
        }
        ImGui::Spacing();
    }

    // NVIDIA Ansel/Camera SDK Warning (check for all possible DLL names)
    bool ansel_loaded = display_commanderhooks::IsModuleLoaded(L"NvAnselSDK.dll")
                        || display_commanderhooks::IsModuleLoaded(L"AnselSDK64.dll")
                        || display_commanderhooks::IsModuleLoaded(L"NvCameraSDK64.dll")
                        || display_commanderhooks::IsModuleLoaded(L"NvCameraAPI64.dll")
                        || display_commanderhooks::IsModuleLoaded(L"GFExperienceCore.dll");

    // Also check full paths in case modules are stored with full paths instead of just filenames
    if (!ansel_loaded) {
        auto loaded_modules = display_commanderhooks::GetLoadedModules();
        for (const auto& module : loaded_modules) {
            std::wstring module_name_lower = module.moduleName;
            std::wstring module_path_lower = module.fullPath;
            std::transform(module_name_lower.begin(), module_name_lower.end(), module_name_lower.begin(),
                           ::towlower);
            std::transform(module_path_lower.begin(), module_path_lower.end(), module_path_lower.begin(),
                           ::towlower);

            if (module_name_lower.find(L"anselsdk64.dll") != std::wstring::npos
                || module_name_lower.find(L"nvanselsdk.dll") != std::wstring::npos
                || module_name_lower.find(L"nvcamerasdk64.dll") != std::wstring::npos
                || module_name_lower.find(L"nvcameraapi64.dll") != std::wstring::npos
                || module_name_lower.find(L"gfexperiencecore.dll") != std::wstring::npos
                || module_path_lower.find(L"anselsdk64.dll") != std::wstring::npos
                || module_path_lower.find(L"nvanselsdk.dll") != std::wstring::npos
                || module_path_lower.find(L"nvcamerasdk64.dll") != std::wstring::npos
                || module_path_lower.find(L"nvcameraapi64.dll") != std::wstring::npos
                || module_path_lower.find(L"gfexperiencecore.dll") != std::wstring::npos) {
                ansel_loaded = true;
                break;
            }
        }
    }

    // Debug logging for Ansel detection
    static bool debug_logged = false;
    if (!debug_logged) {
        LogInfo(
            "Ansel detection check: NvAnselSDK.dll=%s, AnselSDK64.dll=%s, NvCameraSDK64.dll=%s, "
            "NvCameraAPI64.dll=%s, GFExperienceCore.dll=%s",
            display_commanderhooks::IsModuleLoaded(L"NvAnselSDK.dll") ? "YES" : "NO",
            display_commanderhooks::IsModuleLoaded(L"AnselSDK64.dll") ? "YES" : "NO",
            display_commanderhooks::IsModuleLoaded(L"NvCameraSDK64.dll") ? "YES" : "NO",
            display_commanderhooks::IsModuleLoaded(L"NvCameraAPI64.dll") ? "YES" : "NO",
            display_commanderhooks::IsModuleLoaded(L"GFExperienceCore.dll") ? "YES" : "NO");

        LogInfo("Fallback Ansel detection result: %s", ansel_loaded ? "YES" : "NO");

        // Also log all loaded modules for debugging
        auto loaded_modules = display_commanderhooks::GetLoadedModules();
        LogInfo("Total loaded modules: %zu", loaded_modules.size());
        for (const auto& module : loaded_modules) {
            // Check if module name contains Ansel, Camera, or GFExperience (case insensitive)
            std::wstring module_name_lower = module.moduleName;
            std::transform(module_name_lower.begin(), module_name_lower.end(), module_name_lower.begin(),
                           ::towlower);

            if (module_name_lower.find(L"ansel") != std::wstring::npos
                || module_name_lower.find(L"camera") != std::wstring::npos
                || module_name_lower.find(L"gfexperience") != std::wstring::npos) {
                LogInfo("Found Ansel/Camera related module: %S (path: %S)", module.moduleName.c_str(),
                        module.fullPath.c_str());
            }
        }
        debug_logged = true;
    }

    if (ansel_loaded) {
        ImGui::Spacing();
        bool skip_ansel = settings::g_mainTabSettings.skip_ansel_loading.GetValue();
        if (skip_ansel) {
            ImGui::TextColored(ui::colors::TEXT_WARNING, ICON_FK_WARNING
                               " WARNING: NVIDIA Ansel/Camera SDK is loaded (Skip enabled but already loaded)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "NVIDIA Ansel/Camera SDK was already loaded before the skip setting could take effect. Restart "
                    "the game to apply the skip setting.");
            }
        } else {
            ImGui::TextColored(ui::colors::TEXT_WARNING,
                               ICON_FK_WARNING " WARNING: NVIDIA Ansel/Camera SDK is loaded");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "NVIDIA Ansel/Camera SDK is loaded (NvAnselSDK.dll, AnselSDK64.dll, NvCameraSDK64.dll, "
                    "NvCameraAPI64.dll, or GFExperienceCore.dll). This may interfere with display settings and HDR "
                    "functionality. Enable 'Skip Loading Ansel Libraries' to prevent this.");
            }
        }
        ImGui::Spacing();
    }

    // Ansel Control Section
    if (ansel_loaded || settings::g_mainTabSettings.skip_ansel_loading.GetValue()) {
        bool skip_ansel = settings::g_mainTabSettings.skip_ansel_loading.GetValue();
        if (ImGui::Checkbox("Skip Loading Ansel Libraries", &skip_ansel)) {
            settings::g_mainTabSettings.skip_ansel_loading.SetValue(skip_ansel);
            LogInfo("Skip Ansel loading %s", skip_ansel ? "enabled" : "disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Prevents Ansel-related DLLs from being loaded by the game. This can help avoid conflicts with "
                "display settings and HDR functionality. Requires restart to take effect.");
        }

        if (skip_ansel) {
            ImGui::SameLine();
            if (ansel_loaded) {
                ImGui::TextColored(ui::colors::TEXT_WARNING, ICON_FK_WARNING " Already Loaded");
            } else {
                ImGui::TextColored(ui::colors::TEXT_SUCCESS, ICON_FK_OK " Active");
            }
        }

        ImGui::Spacing();
    }

    ImGui::Spacing();
}*/
    // Display Settings Section
    g_rendering_ui_section.store("ui:tab:main_new:display_settings", std::memory_order_release);
    if (ImGui::CollapsingHeader("Display Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        DrawDisplaySettings(runtime);
        if (enabled_experimental_features) {
            // Misc (Streamline DLSS-G)
            g_rendering_ui_section.store("ui:tab:main_new:misc", std::memory_order_release);
            if (ImGui::CollapsingHeader("Misc", ImGuiTreeNodeFlags_None)) {
                ImGui::Indent();
                if (CheckboxSetting(settings::g_mainTabSettings.force_fg_auto, "Force FG Auto (Streamline)")) {
                    LogInfo("Force FG Auto %s",
                            settings::g_mainTabSettings.force_fg_auto.GetValue() ? "enabled" : "disabled");
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Override slDLSSGSetOptions to force DLSS-G mode to Auto. Applies to Streamline (sl.dlss_g) "
                        "integrations only. When enabled, games that set Off or On will have their choice overridden "
                        "to "
                        "Auto.");
                }
                ImGui::Unindent();
            }
        }
        ImGui::Unindent();
    }

    ImGui::Spacing();

    // Brightness and AutoHDR (Display Commander ReShade effects)
    g_rendering_ui_section.store("ui:tab:main_new:brightness_autohdr", std::memory_order_release);
    if (ImGui::CollapsingHeader("Brightness and AutoHDR", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent();
        if (SliderFloatSettingRef(settings::g_mainTabSettings.brightness_percent, "Brightness (%)", "%.0f")) {
            // Value is applied in OnReShadePresent each frame
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Adjust brightness via Display Commander's ReShade effect (0-200%%, 100%% = neutral).\n"
                "Requires DisplayCommander_Control.fx to be in ReShade's Shaders folder and effect reload (e.g. "
                "Ctrl+Shift+F5) or game restart.");
        }
        if (ComboSettingRefWrapper(settings::g_mainTabSettings.brightness_colorspace, "Color Space")) {
            // Value is applied in OnReShadePresent each frame
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Auto = use backbuffer as-is. sRGB = linearize, multiply, encode. Linear = assume linear, multiply.");
        }
        if (CheckboxSetting(settings::g_mainTabSettings.auto_hdr, "AutoHDR")) {
            // Value is applied in OnReShadePresent each frame
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Run DisplayCommander Perceptual Boost effect for HDR-style enhancement. Requires Generic RenoDX to "
                "upgrade buffers from SDR to HDR.");
        }
        if (settings::g_mainTabSettings.auto_hdr.GetValue()) {
            if (SliderFloatSettingRef(settings::g_mainTabSettings.auto_hdr_strength, "Auto HDR strength", "%.2f")) {
                // Value is applied in OnReShadePresent each frame
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Profile 3 effect strength (0.0 = no effect, 1.0 = full effect, up to 2.0).");
            }
        }
        // Misc subsection: Gamma, Contrast, Saturation (less prominent)
        ui::colors::PushNestedHeaderColors();
        if (ImGui::CollapsingHeader("Misc", ImGuiTreeNodeFlags_None)) {
            ImGui::Indent();
            if (SliderFloatSettingRef(settings::g_mainTabSettings.gamma_value, "Gamma", "%.2f")) {
                // Value is applied in OnReShadePresent each frame
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Gamma correction (0.5–2.0, 1.0 = neutral). Applied in DisplayCommander_Control.fx with "
                    "Brightness.");
            }
            if (SliderFloatSettingRef(settings::g_mainTabSettings.contrast_value, "Contrast", "%.2f")) {
                // Value is applied in OnReShadePresent each frame
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Contrast (0.0–2.0, 1.0 = neutral). Applied in DisplayCommander_Control.fx with Brightness.");
            }
            if (SliderFloatSettingRef(settings::g_mainTabSettings.saturation_value, "Saturation", "%.2f")) {
                // Value is applied in OnReShadePresent each frame
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Saturation (0.0 = grayscale, 1.0 = neutral, up to 2.0). Applied in DisplayCommander_Control.fx "
                    "with "
                    "Brightness.");
            }
            ImGui::Unindent();
        }
        ui::colors::PopNestedHeaderColors();
        ImGui::Unindent();
    }

    ImGui::Spacing();

    // Monitor/Display Resolution Settings Section
    g_rendering_ui_section.store("ui:tab:main_new:resolution", std::memory_order_release);
    if (ImGui::CollapsingHeader("Resolution Control", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent();
        display_commander::widgets::resolution_widget::DrawResolutionWidget();
        ImGui::Unindent();
    }

    ImGui::Spacing();

    // Texture Filtering / Sampler State Section
    g_rendering_ui_section.store("ui:tab:main_new:texture_filtering", std::memory_order_release);
    if (ImGui::CollapsingHeader("Texture Filtering", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent();

        // Display call count
        uint32_t d3d11_count = g_d3d_sampler_event_counters[D3D_SAMPLER_EVENT_CREATE_SAMPLER_STATE_D3D11].load();
        uint32_t d3d12_count = g_d3d_sampler_event_counters[D3D_SAMPLER_EVENT_CREATE_SAMPLER_D3D12].load();
        uint32_t total_count = d3d11_count + d3d12_count;

        ImGui::Text("CreateSampler Calls: %u", total_count);
        if (d3d11_count > 0) {
            ImGui::SameLine();
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "(D3D11: %u)", d3d11_count);
        }
        if (d3d12_count > 0) {
            ImGui::SameLine();
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "(D3D12: %u)", d3d12_count);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Total number of CreateSamplerState (D3D11) and CreateSampler (D3D12) calls intercepted.");
        }

        // Show statistics if we have any calls
        if (total_count > 0) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Filter Mode Statistics
            ImGui::TextColored(ui::colors::TEXT_LABEL, "Filter Modes (Original Game Requests):");
            ImGui::Indent();

            uint32_t point_count = g_sampler_filter_mode_counters[SAMPLER_FILTER_POINT].load();
            uint32_t linear_count = g_sampler_filter_mode_counters[SAMPLER_FILTER_LINEAR].load();
            uint32_t aniso_count = g_sampler_filter_mode_counters[SAMPLER_FILTER_ANISOTROPIC].load();
            uint32_t comp_point_count = g_sampler_filter_mode_counters[SAMPLER_FILTER_COMPARISON_POINT].load();
            uint32_t comp_linear_count = g_sampler_filter_mode_counters[SAMPLER_FILTER_COMPARISON_LINEAR].load();
            uint32_t comp_aniso_count = g_sampler_filter_mode_counters[SAMPLER_FILTER_COMPARISON_ANISOTROPIC].load();
            uint32_t other_count = g_sampler_filter_mode_counters[SAMPLER_FILTER_OTHER].load();

            if (point_count > 0) {
                ImGui::Text("  Point: %u", point_count);
            }
            if (linear_count > 0) {
                ImGui::Text("  Linear: %u", linear_count);
            }
            if (aniso_count > 0) {
                ImGui::Text("  Anisotropic: %u", aniso_count);
            }
            if (comp_point_count > 0) {
                ImGui::Text("  Comparison Point: %u", comp_point_count);
            }
            if (comp_linear_count > 0) {
                ImGui::Text("  Comparison Linear: %u", comp_linear_count);
            }
            if (comp_aniso_count > 0) {
                ImGui::Text("  Comparison Anisotropic: %u", comp_aniso_count);
            }
            if (other_count > 0) {
                ImGui::Text("  Other: %u", other_count);
            }

            ImGui::Unindent();
            ImGui::Spacing();

            // Address Mode Statistics
            ImGui::TextColored(ui::colors::TEXT_LABEL, "Address Modes (U Coordinate):");
            ImGui::Indent();

            uint32_t wrap_count = g_sampler_address_mode_counters[SAMPLER_ADDRESS_WRAP].load();
            uint32_t mirror_count = g_sampler_address_mode_counters[SAMPLER_ADDRESS_MIRROR].load();
            uint32_t clamp_count = g_sampler_address_mode_counters[SAMPLER_ADDRESS_CLAMP].load();
            uint32_t border_count = g_sampler_address_mode_counters[SAMPLER_ADDRESS_BORDER].load();
            uint32_t mirror_once_count = g_sampler_address_mode_counters[SAMPLER_ADDRESS_MIRROR_ONCE].load();

            if (wrap_count > 0) {
                ImGui::Text("  Wrap: %u", wrap_count);
            }
            if (mirror_count > 0) {
                ImGui::Text("  Mirror: %u", mirror_count);
            }
            if (clamp_count > 0) {
                ImGui::Text("  Clamp: %u", clamp_count);
            }
            if (border_count > 0) {
                ImGui::Text("  Border: %u", border_count);
            }
            if (mirror_once_count > 0) {
                ImGui::Text("  Mirror Once: %u", mirror_once_count);
            }

            ImGui::Unindent();
            ImGui::Spacing();

            // Anisotropy Level Statistics (only for anisotropic filters)
            uint32_t total_aniso_samplers = 0;
            for (int i = 0; i < MAX_ANISOTROPY_LEVELS; ++i) {
                total_aniso_samplers += g_sampler_anisotropy_level_counters[i].load();
            }

            if (total_aniso_samplers > 0) {
                ImGui::TextColored(ui::colors::TEXT_LABEL, "Anisotropic Filtering Levels (Original Game Requests):");
                ImGui::Indent();

                // Show only levels that have been used
                for (int i = 0; i < MAX_ANISOTROPY_LEVELS; ++i) {
                    uint32_t count = g_sampler_anisotropy_level_counters[i].load();
                    if (count > 0) {
                        int level = i + 1;  // Convert from 0-based index to 1-based level
                        ImGui::Text("  %dx: %u", level, count);
                    }
                }

                ImGui::Unindent();
                ImGui::Spacing();
            }

            ImGui::Separator();
        }

        ImGui::Spacing();

        // Max Anisotropy Override
        // Only affects existing anisotropic filters
        int max_aniso = settings::g_mainTabSettings.max_anisotropy.GetValue();
        if (ImGui::SliderInt("Anisotropic Level", &max_aniso, 0, 16, max_aniso == 0 ? "Game Default" : "%dx")) {
            settings::g_mainTabSettings.max_anisotropy.SetValue(max_aniso);
            LogInfo("Max anisotropy set to %d", max_aniso);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Override maximum anisotropic filtering level (1-16) for existing anisotropic filters.\n"
                "Set to 0 (Game default) to preserve the game's original AF settings.\n"
                "Only affects samplers that already use anisotropic filtering.");
        }

        // Reset button for Anisotropic Level
        if (max_aniso != 0) {
            ImGui::SameLine();
            if (ImGui::Button("Game Default##Anisotropic Level")) {
                settings::g_mainTabSettings.max_anisotropy.SetValue(0);
                LogInfo("Max anisotropy reset to game default");
            }
        }

        ImGui::Spacing();

        // Mipmap LOD Bias
        float lod_bias = settings::g_mainTabSettings.force_mipmap_lod_bias.GetValue();
        if (ImGui::SliderFloat("Mipmap LOD Bias", &lod_bias, -5.0f, 5.0f, lod_bias == 0.0f ? "Game Default" : "%.2f")) {
            settings::g_mainTabSettings.force_mipmap_lod_bias.SetValue(lod_bias);
            LogInfo("Mipmap LOD bias set to %.2f", lod_bias);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Use a small (i.e. -0.6'ish) negative LOD bias to sharpen DLSS and FSR games");
        }

        // Reset button for LOD bias
        if (lod_bias != 0.0f) {
            ImGui::SameLine();
            if (ImGui::Button("Game Default##Mipmap LOD Bias")) {
                settings::g_mainTabSettings.force_mipmap_lod_bias.SetValue(0.0f);
                LogInfo("Mipmap LOD bias reset to game default");
            }
        }

        ImGui::Spacing();
        ImGui::TextColored(ui::colors::TEXT_WARNING,
                           ICON_FK_WARNING " Game restart may be required for changes to take full effect.");

        ImGui::Unindent();
    }

    ImGui::Spacing();

    // Audio Settings Section
    g_rendering_ui_section.store("ui:tab:main_new:audio", std::memory_order_release);
    if (ImGui::CollapsingHeader("Audio Control", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent();
        DrawAudioSettings();
        ImGui::Unindent();
    }

    ImGui::Spacing();

    g_rendering_ui_section.store("ui:tab:main_new:input", std::memory_order_release);
    // Input Blocking Section
    if (ImGui::CollapsingHeader("Input Control", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent();

        // Create 3 columns with fixed width
        ImGui::Columns(3, "InputBlockingColumns", true);

        // First line: Headers
        ImGui::Text("Suppress Keyboard");
        ImGui::NextColumn();
        ImGui::Text("Suppress Mouse");
        ImGui::NextColumn();
        ImGui::Text("Suppress Gamepad");
        ImGui::NextColumn();

        // Second line: Selectors
        if (ui::new_ui::ComboSettingEnumRefWrapper(settings::g_mainTabSettings.keyboard_input_blocking, "##Keyboard")) {
            // Restore cursor clipping when input blocking is disabled
            if (settings::g_mainTabSettings.keyboard_input_blocking.GetValue()
                == static_cast<int>(InputBlockingMode::kDisabled)) {
                // display_commanderhooks::RestoreClipCursor();
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Controls keyboard input blocking behavior.");
        }

        ImGui::NextColumn();

        if (ui::new_ui::ComboSettingEnumRefWrapper(settings::g_mainTabSettings.mouse_input_blocking, "##Mouse")) {
            // Restore cursor clipping when input blocking is disabled
            if (settings::g_mainTabSettings.mouse_input_blocking.GetValue()
                == static_cast<int>(InputBlockingMode::kDisabled)) {
                // display_commanderhooks::RestoreClipCursor();
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Controls mouse input blocking behavior.");
        }

        ImGui::NextColumn();

        ui::new_ui::ComboSettingEnumRefWrapper(settings::g_mainTabSettings.gamepad_input_blocking, "##Gamepad");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Controls gamepad input blocking behavior.");
        }

        ImGui::Columns(1);  // Reset to single column

        ImGui::Spacing();

        // Clip Cursor checkbox
        bool clip_cursor = settings::g_mainTabSettings.clip_cursor_enabled.GetValue();
        if (ImGui::Checkbox("Clip Cursor", &clip_cursor)) {
            settings::g_mainTabSettings.clip_cursor_enabled.SetValue(clip_cursor);
            // If disabling, unlock cursor immediately
            if (!clip_cursor) {
                display_commanderhooks::ClipCursor_Direct(nullptr);
            } else {
                // If enabling and game is in foreground, clip immediately
                if (!g_app_in_background.load()) {
                    display_commanderhooks::ClipCursorToGameWindow();
                }
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Limits mouse movement to the game window when the game is in foreground.\n"
                "Unlocks cursor when game is in background.\n\n"
                "This fixes games which don't lock the mouse cursor, preventing focus switches\n"
                "on multimonitor setups when moving the mouse and clicking.");
        }

        ImGui::Spacing();

        // Enable Gamepad Remapping (same value as Controller tab)
        {
            auto& remapper = display_commander::input_remapping::InputRemapper::get_instance();
            bool remapping_enabled = remapper.is_remapping_enabled();
            if (ImGui::Checkbox("Enable XBOX-style Gamepad Remapping", &remapping_enabled)) {
                remapper.set_remapping_enabled(remapping_enabled);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "When enabled, XINPUT gamepad buttons can be remapped to keyboard keys, other gamepad buttons, "
                    "or actions (e.g. volume, screenshot). Supports chords (e.g. Home + D-Pad for volume) and hold "
                    "mode.\n\n"
                    "This checkbox is the same setting as in the Controller tab. For full setup (remapping list, "
                    "input method, \"Block Gamepad Input When Home Pressed\", default chords), open the Controller "
                    "tab.");
            }
            if (remapping_enabled) {
                ImGui::Spacing();
                // Home button behavior for Display Commander UI
                bool require_solo_press = settings::g_mainTabSettings.guide_button_solo_ui_toggle_only.GetValue();
                if (ImGui::Checkbox("Require Home-only press to toggle Display Commander UI", &require_solo_press)) {
                    settings::g_mainTabSettings.guide_button_solo_ui_toggle_only.SetValue(require_solo_press);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "When enabled, tapping the Home button will open/close Display Commander UI only if no other\n"
                        "gamepad buttons were pressed between Home down and Home up.\n\n"
                        "Example:\n"
                        "- Press Home, do nothing else, release Home -> Toggle Display Commander UI\n"
                        "- Press Home + any other button (e.g. volume chords) -> Do NOT toggle Display Commander UI");
                }
            }
        }

        ImGui::Unindent();
    }

    ImGui::Spacing();

    g_rendering_ui_section.store("ui:tab:main_new:window_control", std::memory_order_release);
    if (ImGui::CollapsingHeader("Window Control", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent();

        // Continue Rendering in Background
        if (CheckboxSetting(settings::g_advancedTabSettings.continue_rendering, "Continue Rendering in Background")) {
            bool new_value = settings::g_advancedTabSettings.continue_rendering.GetValue();
            s_continue_rendering.store(new_value);
            LogInfo("Continue rendering in background %s", new_value ? "enabled" : "disabled");

            // Install or uninstall window proc hooks based on the setting
            HWND game_window = display_commanderhooks::GetGameWindow();
            if (new_value) {
                // Enable: Install hooks if we have a valid window
                if (game_window != nullptr && IsWindow(game_window)) {
                    if (display_commanderhooks::InstallWindowProcHooks(game_window)) {
                        LogInfo("Window procedure hooks installed after enabling continue rendering");
                    } else {
                        LogWarn("Failed to install window procedure hooks after enabling continue rendering");
                    }
                } else {
                    LogInfo("Window procedure hooks will be installed when a valid window is available");
                }
            } else {
                // Disable: Uninstall hooks
                display_commanderhooks::UninstallWindowProcHooks();
                LogInfo("Window procedure hooks uninstalled after disabling continue rendering");
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Prevent games from pausing or reducing performance when alt-tabbed. Blocks window focus "
                "messages to keep games running in background.");
        }

        ImGui::Spacing();

        // Screensaver Mode
        if (ComboSettingEnumRefWrapper(settings::g_mainTabSettings.screensaver_mode, "Screensaver Mode")) {
            LogInfo("Screensaver mode changed to %d", settings::g_mainTabSettings.screensaver_mode.GetValue());
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Controls screensaver behavior while the game is running:\n\n"
                "- Default (no change): Preserves original game behavior\n"
                "- Disable when Focused: Disables screensaver when game window is focused\n"
                "- Disable: Always disables screensaver while game is running\n\n"
                "Note: This feature requires the screensaver implementation to be active.");
        }

        ImGui::Unindent();
    }

    ImGui::Spacing();

    // CPU Control Section
    g_rendering_ui_section.store("ui:tab:main_new:cpu", std::memory_order_release);
    if (ImGui::CollapsingHeader("CPU Control", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent();

        // Get CPU core count for display
        SYSTEM_INFO sys_info = {};
        GetSystemInfo(&sys_info);
        DWORD max_cores = sys_info.dwNumberOfProcessors;

        // Update maximum if needed
        settings::UpdateCpuCoresMaximum();

        int cpu_cores_value = settings::g_mainTabSettings.cpu_cores.GetValue();
        int max_cores_int = static_cast<int>(max_cores);

        // Clamp invalid values (1 to MIN_CPU_CORES_SELECTABLE-1) to MIN_CPU_CORES_SELECTABLE or 0
        if (cpu_cores_value > 0 && cpu_cores_value < MIN_CPU_CORES_SELECTABLE) {
            cpu_cores_value = MIN_CPU_CORES_SELECTABLE;
            settings::g_mainTabSettings.cpu_cores.SetValue(cpu_cores_value);
            s_cpu_cores.store(cpu_cores_value);
        }

        // Use actual CPU cores value in slider, but we'll handle invalid values (1-5) by clamping
        // Slider range: 0, then MIN_CPU_CORES_SELECTABLE to max_cores_int
        int slider_min = 0;
        int slider_max = max_cores_int;

        // Create label for slider
        std::string slider_label = "CPU Cores";
        if (cpu_cores_value == 0) {
            slider_label += " (Default - No Change)";
        } else if (cpu_cores_value == max_cores_int) {
            slider_label += " (All Cores)";
        } else {
            slider_label += " (" + std::to_string(cpu_cores_value) + " Core" + (cpu_cores_value > 1 ? "s" : "") + ")";
        }

        // Format string for slider display
        const char* format_str = (cpu_cores_value == 0) ? "Default" : "%d";

        // Use a temporary variable for the slider to handle invalid values
        int slider_temp_value = cpu_cores_value;

        if (ImGui::SliderInt(slider_label.c_str(), &slider_temp_value, slider_min, slider_max, format_str)) {
            // Clamp invalid values (1 to MIN_CPU_CORES_SELECTABLE-1) to MIN_CPU_CORES_SELECTABLE
            int new_cpu_cores_value = slider_temp_value;
            if (new_cpu_cores_value > 0 && new_cpu_cores_value < MIN_CPU_CORES_SELECTABLE) {
                new_cpu_cores_value = MIN_CPU_CORES_SELECTABLE;
            }

            settings::g_mainTabSettings.cpu_cores.SetValue(new_cpu_cores_value);
            s_cpu_cores.store(new_cpu_cores_value);
            LogInfo("CPU cores set to %d (0 = default/no change)", new_cpu_cores_value);
            cpu_cores_value = new_cpu_cores_value;  // Update for display below
        }

        // Show tooltip for slider
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Controls CPU core affinity for the game process:\n\n"
                "- 0 (Default): No change to process affinity\n"
                "- %d-%d: Limit game to use specified number of CPU cores\n\n"
                "Note: Changes take effect immediately. Game restart may be required for full effect.",
                MIN_CPU_CORES_SELECTABLE, max_cores_int);
        }

        // Show actual CPU cores value next to slider for clarity
        if (cpu_cores_value > 0) {
            ImGui::SameLine();
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "= %d core%s", cpu_cores_value, cpu_cores_value > 1 ? "s" : "");
        }

        // Display current status
        ImGui::Spacing();
        if (cpu_cores_value == 0) {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, ICON_FK_FILE " No CPU affinity change (using default)");
        } else {
            ImGui::TextColored(ui::colors::TEXT_SUCCESS, ICON_FK_OK " CPU affinity set to %d core%s", cpu_cores_value,
                               cpu_cores_value > 1 ? "s" : "");
        }

        ImGui::Unindent();
    }

    ImGui::Spacing();

    // Window Controls Section
    DrawWindowControls();

    ImGui::Spacing();

    // Overlay Windows Detection Section
    g_rendering_ui_section.store("ui:tab:main_new:overlay_windows", std::memory_order_release);
    if (ImGui::CollapsingHeader("Overlay Windows", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent();

        HWND game_window = display_commanderhooks::GetGameWindow();
        if (game_window != nullptr && IsWindow(game_window) != FALSE) {
            static DWORD last_check_time = 0;
            DWORD current_time = GetTickCount();

            // Update overlay list every 500ms to avoid performance impact
            static std::vector<display_commander::utils::OverlayWindowInfo> overlay_list;
            if (current_time - last_check_time > 500) {
                overlay_list = display_commander::utils::DetectOverlayWindows(game_window);
                last_check_time = current_time;
            }

            if (overlay_list.empty()) {
                ImGui::TextColored(ui::colors::TEXT_DIMMED, "No overlay windows detected");
            } else {
                ImGui::Text("Detected %zu overlay window(s):", overlay_list.size());
                ImGui::Spacing();

                // Create a table-like display
                if (ImGui::BeginTable("OverlayWindows", 6,
                                      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                    ImGui::TableSetupColumn("Window Title", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Process", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Z-Order", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableSetupColumn("Overlap Area", ImGuiTableColumnFlags_WidthFixed, 170.0f);
                    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 200.0f);
                    ImGui::TableHeadersRow();

                    for (const auto& overlay : overlay_list) {
                        ImGui::TableNextRow();

                        // Window Title
                        ImGui::TableSetColumnIndex(0);
                        std::string title_utf8 =
                            overlay.window_title.empty()
                                ? "(No Title)"
                                : std::string(overlay.window_title.begin(), overlay.window_title.end());
                        ImGui::TextUnformatted(title_utf8.c_str());

                        // Process Name
                        ImGui::TableSetColumnIndex(1);
                        std::string process_utf8 =
                            overlay.process_name.empty()
                                ? "(Unknown)"
                                : std::string(overlay.process_name.begin(), overlay.process_name.end());
                        ImGui::TextUnformatted(process_utf8.c_str());

                        // PID
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%lu", overlay.process_id);

                        // Z-Order (Above/Below)
                        ImGui::TableSetColumnIndex(3);
                        if (overlay.is_above_game) {
                            ui::colors::PushIconColor(ui::colors::ICON_WARNING);
                            ImGui::Text(ICON_FK_WARNING " Above");
                            ui::colors::PopIconColor();
                        } else {
                            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Below");
                        }

                        // Overlapping Area
                        ImGui::TableSetColumnIndex(4);
                        if (overlay.overlaps_game) {
                            ImGui::Text("%ld px (%.1f%%)", overlay.overlapping_area_pixels,
                                        overlay.overlapping_area_percent);
                        } else {
                            ImGui::TextColored(ui::colors::TEXT_DIMMED, "No overlap");
                        }

                        // Status
                        ImGui::TableSetColumnIndex(5);
                        if (overlay.overlaps_game) {
                            ui::colors::PushIconColor(ui::colors::ICON_WARNING);
                            ImGui::Text(ICON_FK_WARNING " Overlapping");
                            ui::colors::PopIconColor();
                        } else if (overlay.is_visible) {
                            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Visible");
                        } else {
                            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Hidden");
                        }
                    }

                    ImGui::EndTable();
                }
            }
        } else {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Game window not detected");
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Shows all visible windows that overlap with the game window.\n"
                "Windows can be above or below the game in Z-order.\n"
                "Overlapping windows may cause performance issues.");
        }

        ImGui::Unindent();
    }

    ImGui::Spacing();

    g_rendering_ui_section.store("ui:tab:main_new:important_info", std::memory_order_release);
    if (ImGui::CollapsingHeader("Important Info", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        DrawImportantInfo();
        ImGui::Unindent();
    }
    g_rendering_ui_section.store("ui:tab:main_new:advanced_settings", std::memory_order_release);
    if (ImGui::CollapsingHeader("Advanced Settings", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent();
        DrawAdvancedSettings();
        ImGui::Unindent();
    }
}

void DrawQuickFpsLimitChanger() {
    // Quick-set buttons based on current monitor refresh rate
    {
        auto window_state = ::g_window_state.load();
        double refresh_hz = window_state->current_monitor_refresh_rate.ToHz();
        int y = static_cast<int>(std::round(refresh_hz));
        if (y <= 0) {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Quick fps limit changer not working: TODO FIXME");
            return;
        }
        bool first = true;
        const float selected_epsilon = 0.0001f;
        // Add No Limit button at the beginning
        {
            bool selected = (std::fabs(settings::g_mainTabSettings.fps_limit.GetValue() - 0.0f) <= selected_epsilon);
            if (selected) {
                ui::colors::PushSelectedButtonColors();
            }
            if (ImGui::Button("No Limit")) {
                settings::g_mainTabSettings.fps_limit.SetValue(0.0f);
            }
            if (selected) {
                ui::colors::PopSelectedButtonColors();
            }
        }
        first = false;
        for (int x = 1; x <= 15; ++x) {
            if (y % x == 0) {
                int candidate_rounded = y / x;
                float candidate_precise = refresh_hz / x;
                if (candidate_rounded >= 30) {
                    if (!first) ImGui::SameLine();
                    first = false;
                    std::string label = std::to_string(candidate_rounded);
                    {
                        bool selected = (std::fabs(settings::g_mainTabSettings.fps_limit.GetValue() - candidate_precise)
                                         <= selected_epsilon);
                        if (selected) {
                            ui::colors::PushSelectedButtonColors();
                        }
                        if (ImGui::Button(label.c_str())) {
                            float target_fps = candidate_precise;
                            settings::g_mainTabSettings.fps_limit.SetValue(target_fps);
                        }
                        if (selected) {
                            ui::colors::PopSelectedButtonColors();
                        }
                        // Add tooltip showing the precise calculation
                        if (ImGui::IsItemHovered()) {
                            std::ostringstream tooltip_oss;
                            tooltip_oss.setf(std::ios::fixed);
                            tooltip_oss << std::setprecision(3);
                            tooltip_oss << "FPS = " << refresh_hz << " ÷ " << x << " = " << candidate_precise
                                        << " FPS\n\n";
                            tooltip_oss << "Creates a smooth frame rate that divides evenly\n";
                            tooltip_oss << "into the monitor's refresh rate.";
                            ImGui::SetTooltip("%s", tooltip_oss.str().c_str());
                        }
                    }
                }
            }
        }
        // Add Gsync Cap button at the end
        if (!first) {
            ImGui::SameLine();
        }

        {
            // Gsync formula: refresh_hz - (refresh_hz * refresh_hz / 3600)
            double gsync_target = refresh_hz - (refresh_hz * refresh_hz / 3600.0);
            float precise_target = static_cast<float>(gsync_target);
            if (precise_target < 1.0f) precise_target = 1.0f;
            bool selected =
                (std::fabs(settings::g_mainTabSettings.fps_limit.GetValue() - precise_target) <= selected_epsilon);

            if (selected) {
                ui::colors::PushSelectedButtonColors();
            }
            if (ImGui::Button("VRR Cap")) {
                double precise_target = gsync_target;  // do not round on apply
                float target_fps = static_cast<float>(precise_target < 1.0 ? 1.0 : precise_target);
                settings::g_mainTabSettings.fps_limit.SetValue(target_fps);
            }
            if (selected) {
                ui::colors::PopSelectedButtonColors();
            }
            // Add tooltip explaining the Gsync formula
            if (ImGui::IsItemHovered()) {
                std::ostringstream tooltip_oss;
                tooltip_oss.setf(std::ios::fixed);
                tooltip_oss << std::setprecision(3);
                tooltip_oss << "Gsync Cap: FPS = " << refresh_hz << " - (" << refresh_hz << "² / 3600)\n";
                tooltip_oss << "= " << refresh_hz << " - " << (refresh_hz * refresh_hz / 3600.0) << " = "
                            << gsync_target << " FPS\n\n";
                tooltip_oss << "Creates a ~0.3ms frame time buffer to optimize latency\n";
                tooltip_oss << "and prevent tearing, similar to NVIDIA Reflex Low Latency Mode.";
                ImGui::SetTooltip("%s", tooltip_oss.str().c_str());
            }
        }
    }
}

void DrawDisplaySettings_DisplayAndTarget() {
    {
        // Target Display list and selection (needed for refresh rate fallback on same line as Game Render Resolution)
        auto display_info = display_cache::g_displayCache.GetDisplayInfoForUI();
        std::string current_device_id = settings::g_mainTabSettings.selected_extended_display_device_id.GetValue();
        int selected_index = 0;
        for (size_t i = 0; i < display_info.size(); ++i) {
            if (display_info[i].extended_device_id == current_device_id) {
                selected_index = static_cast<int>(i);
                break;
            }
        }

        // Render resolution and refresh rate on the same line: "Render resolution: XXX Refresh rate: XXX"
        int game_render_w = g_game_render_width.load();
        int game_render_h = g_game_render_height.load();
        if (game_render_w > 0 && game_render_h > 0) {
            ImGui::TextColored(ui::colors::TEXT_LABEL, "Render resolution:");
            ImGui::SameLine();
            ImGui::Text("%dx%d", game_render_w, game_render_h);

            // Get bit depth from swapchain format (optional, in parens)
            auto desc_ptr = g_last_swapchain_desc.load();
            if (desc_ptr != nullptr) {
                const char* bit_depth_str = nullptr;
                switch (desc_ptr->back_buffer.texture.format) {
                    case reshade::api::format::r8g8b8a8_unorm:
                    case reshade::api::format::b8g8r8a8_unorm:     bit_depth_str = "8-bit"; break;
                    case reshade::api::format::r10g10b10a2_unorm:  bit_depth_str = "10-bit"; break;
                    case reshade::api::format::r16g16b16a16_float: bit_depth_str = "16-bit"; break;
                    default:                                       break;
                }

                if (bit_depth_str != nullptr) {
                    ImGui::SameLine();
                    ImGui::TextColored(ui::colors::TEXT_DIMMED, " (%s)", bit_depth_str);
                }
            }

            // Refresh rate on same line: "Refresh rate: XXX" (actual NVAPI smoothed alpha 0.02, else selected display)
            static double s_smoothed_actual_hz = 0.0;
            constexpr double k_alpha = 0.02;
            double raw_actual_hz = display_commander::nvapi::GetNvapiActualRefreshRateHz();
            double refresh_hz = 0.0;
            if (raw_actual_hz > 0.0) {
                s_smoothed_actual_hz = k_alpha * raw_actual_hz + (1.0 - k_alpha) * s_smoothed_actual_hz;
                refresh_hz = s_smoothed_actual_hz;
            } else if (selected_index >= 0 && selected_index < static_cast<int>(display_info.size())
                       && !display_info[selected_index].current_refresh_rate.empty()) {
                std::string rate_str = display_info[selected_index].current_refresh_rate;
                try {
                    double parsed = std::stod(rate_str);
                    if (parsed >= 1.0 && parsed <= 500.0) {
                        refresh_hz = parsed;
                    }
                } catch (...) {
                }
            }
            ImGui::SameLine();
            if (refresh_hz > 0.0) {
                ImGui::TextColored(ui::colors::TEXT_LABEL, "Refresh rate:");
                ImGui::SameLine();
                ImGui::Text("%.1f Hz", refresh_hz);
            } else {
                ImGui::TextColored(ui::colors::TEXT_LABEL, "Refresh rate:");
                ImGui::SameLine();
                ImGui::TextColored(ui::colors::TEXT_DIMMED, "—");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Render resolution: the resolution the game requested (before any modifications). "
                    "Matches Special K's render_x/render_y.\n"
                    "Refresh rate: actual (NVAPI) when available, else selected display's configured rate.");
            }

            // VRAM and RAM usage on one line under Render resolution
            uint64_t vram_used = 0;
            uint64_t vram_total = 0;
            if (display_commander::dxgi::GetVramInfo(&vram_used, &vram_total) && vram_total > 0) {
                const uint64_t used_mib = vram_used / (1024ULL * 1024ULL);
                const uint64_t total_mib = vram_total / (1024ULL * 1024ULL);
                ImGui::TextColored(ui::colors::TEXT_LABEL, "VRAM:");
                ImGui::SameLine();
                ImGui::Text("%llu / %llu MiB", static_cast<unsigned long long>(used_mib),
                            static_cast<unsigned long long>(total_mib));
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("GPU video memory used / budget (DXGI adapter memory budget).");
                }
            } else {
                ImGui::TextColored(ui::colors::TEXT_LABEL, "VRAM:");
                ImGui::SameLine();
                ImGui::TextColored(ui::colors::TEXT_DIMMED, "N/A");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("VRAM unavailable (DXGI adapter or budget query failed).");
                }
            }

            // RAM (system memory) on the same line
            ImGui::SameLine();
            MEMORYSTATUSEX mem_status = {};
            mem_status.dwLength = sizeof(mem_status);
            if (GlobalMemoryStatusEx(&mem_status) != 0 && mem_status.ullTotalPhys > 0) {
                const uint64_t ram_used = mem_status.ullTotalPhys - mem_status.ullAvailPhys;
                const uint64_t ram_used_mib = ram_used / (1024ULL * 1024ULL);
                const uint64_t ram_total_mib = mem_status.ullTotalPhys / (1024ULL * 1024ULL);
                ImGui::TextColored(ui::colors::TEXT_LABEL, "RAM:");
                ImGui::SameLine();
                ImGui::Text("%llu / %llu MiB", static_cast<unsigned long long>(ram_used_mib),
                            static_cast<unsigned long long>(ram_total_mib));
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("System physical memory in use / total (GlobalMemoryStatusEx).");
                }
            } else {
                ImGui::TextColored(ui::colors::TEXT_LABEL, "RAM:");
                ImGui::SameLine();
                ImGui::TextColored(ui::colors::TEXT_DIMMED, "N/A");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("System memory info unavailable.");
                }
            }

            ImGui::Spacing();
        }

        // Target Display dropdown
        std::vector<const char*> monitor_c_labels;
        monitor_c_labels.reserve(display_info.size());
        for (const auto& info : display_info) {
            monitor_c_labels.push_back(info.display_label.c_str());
        }

        if (ImGui::Combo("Target Display", &selected_index, monitor_c_labels.data(),
                         static_cast<int>(monitor_c_labels.size()))) {
            if (selected_index >= 0 && selected_index < static_cast<int>(display_info.size())) {
                // Store the device ID
                std::string new_device_id = display_info[selected_index].extended_device_id;
                settings::g_mainTabSettings.selected_extended_display_device_id.SetValue(new_device_id);

                LogInfo("Target monitor changed to device ID: %s", new_device_id.c_str());
            }
        }
        if (ImGui::IsItemHovered()) {
            // Get the saved game window display device ID for tooltip
            std::string saved_device_id = settings::g_mainTabSettings.game_window_display_device_id.GetValue();
            std::string tooltip_text =
                "Choose which monitor to apply size/pos to. The monitor corresponding to the "
                "game window is automatically selected.";
            if (!saved_device_id.empty() && saved_device_id != "No Window" && saved_device_id != "No Monitor"
                && saved_device_id != "Monitor Info Failed") {
                tooltip_text += "\n\nGame window is on: " + saved_device_id;
            }
            ImGui::SetTooltip("%s", tooltip_text.c_str());
        }
    }
}

void DrawDisplaySettings_WindowModeAndApply() {
    // Window Mode dropdown (with persistent setting)
    if (ComboSettingEnumRefWrapper(settings::g_mainTabSettings.window_mode, "Window Mode")) {
        WindowMode old_mode = s_window_mode.load();
        s_window_mode = static_cast<WindowMode>(settings::g_mainTabSettings.window_mode.GetValue());

        // Don't apply changes immediately - let the normal window management system handle it
        // This prevents crashes when changing modes during gameplay

        std::ostringstream oss;
        oss << "Window mode changed from " << static_cast<int>(old_mode) << " to "
            << settings::g_mainTabSettings.window_mode.GetValue();
        LogInfo(oss.str().c_str());
    }

    // Aspect Ratio dropdown (only shown in Aspect Ratio mode)
    if (s_window_mode.load() == WindowMode::kAspectRatio) {
        if (ComboSettingWrapper(settings::g_mainTabSettings.aspect_index, "Aspect Ratio")) {
            s_aspect_index = static_cast<AspectRatioType>(settings::g_mainTabSettings.aspect_index.GetValue());
            LogInfo("Aspect ratio changed");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Choose the aspect ratio for window resizing.");
        }
    }
    if (s_window_mode.load() == WindowMode::kAspectRatio) {
        // Width dropdown for aspect ratio mode
        if (ComboSettingRefWrapper(settings::g_mainTabSettings.window_aspect_width, "Window Width")) {
            s_aspect_width.store(settings::g_mainTabSettings.window_aspect_width.GetValue());
            LogInfo("Window width for aspect mode setting changed to: %d", s_aspect_width.load());
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Choose the width for the aspect ratio window. 'Display Width' uses the current monitor width.");
        }
    }

    // Window Alignment dropdown (only shown in Aspect Ratio mode)
    if (s_window_mode.load() == WindowMode::kAspectRatio) {
        if (ComboSettingWrapper(settings::g_mainTabSettings.alignment, "Alignment")) {
            s_window_alignment = static_cast<WindowAlignment>(settings::g_mainTabSettings.alignment.GetValue());
            LogInfo("Window alignment changed");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Choose how to align the window when repositioning is needed. 0=Center, 1=Top Left, "
                "2=Top Right, 3=Bottom Left, 4=Bottom Right.");
        }
    }
    // Background Black Curtain checkbox (only shown in Aspect Ratio mode)
    if (s_window_mode.load() == WindowMode::kAspectRatio) {
        if (CheckboxSetting(settings::g_mainTabSettings.background_feature, "Background Black Curtain")) {
            LogInfo("Background black curtain setting changed");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Creates a black background behind the game window when it doesn't cover the full screen.");
        }
        ImGui::SameLine();
    }

    // ADHD Multi-Monitor Mode controls
    DrawAdhdMultiMonitorControls(s_window_mode.load() == WindowMode::kAspectRatio);

    // Apply Changes button
    ui::colors::PushIconColor(ui::colors::ICON_SUCCESS);
    if (ImGui::Button(ICON_FK_OK " Apply Changes")) {
        // Force immediate application of window changes
        ::g_init_apply_generation.fetch_add(1);
        LogInfo("Apply Changes button clicked - forcing immediate window update");
        std::ostringstream oss;
        // All global settings on this tab are handled by the settings wrapper
        oss << "Apply Changes button clicked - forcing immediate window update";
        LogInfo(oss.str().c_str());
    }
    ui::colors::PopIconColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Apply the current window size and position settings immediately.");
    }
}

void DrawDisplaySettings_FpsLimiterMode() {
    ImGui::Spacing();

    // FPS Limiter Mode
    {
        const char* items[] = {"Default", "NVIDIA Reflex (DX11/DX12 only, Vulkan not supported)", "Disabled",
                               "Sync to Display Refresh Rate (fraction of monitor refresh rate) Non-VRR"};

        int current_item = settings::g_mainTabSettings.fps_limiter_mode.GetValue();
        int prev_item = current_item;
        if (ImGui::Combo("FPS Limiter Mode", &current_item, items, 4)) {
            settings::g_mainTabSettings.fps_limiter_mode.SetValue(current_item);
            s_fps_limiter_mode.store(static_cast<FpsLimiterMode>(current_item));
            FpsLimiterMode mode = s_fps_limiter_mode.load();
            if (mode == FpsLimiterMode::kDisabled) {
                LogInfo("FPS Limiter: Disabled (no limiting)");
            } else if (mode == FpsLimiterMode::kReflex) {
                LogInfo("FPS Limiter: Reflex");
                s_reflex_auto_configure.store(true);
                settings::g_advancedTabSettings.reflex_auto_configure.SetValue(true);
                g_reflex_settings_outdated.store(true);
            } else if (mode == FpsLimiterMode::kOnPresentSync) {
                LogInfo("FPS Limiter: OnPresent Frame Synchronizer");
            } else if (mode == FpsLimiterMode::kLatentSync) {
                LogInfo("FPS Limiter: VBlank Scanline Sync for VSYNC-OFF or without VRR");
            }

            if (mode == FpsLimiterMode::kReflex && prev_item != static_cast<int>(FpsLimiterMode::kReflex)) {
                // reset the reflex auto configure setting
                settings::g_advancedTabSettings.reflex_auto_configure.SetValue(false);
                s_reflex_auto_configure.store(false);
                g_reflex_settings_outdated.store(true);
            }
        }

        // Custom rendering for grayed out option
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Choose limiter:\n"
                "Default - recommend.\n"
                "Reflex Mode - uses reflex library to limit FPS\n"
                "Disabled - no FPS limiting\n"
                "Sync to Display Refresh Rate (fraction of monitor refresh rate) Non-VRR - synchronizes frame display "
                "time "
                "to the monitor refresh rate.");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(src: %s)", GetChosenFpsLimiterSiteName());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Which path is currently applying the FPS limiter this frame.\n"
                "Priority: reflex_marker > dxgi_swapchain > dxgi_factory_wrapper > reshade_addon_event.");
        }
        if (current_item == static_cast<int>(FpsLimiterMode::kOnPresentSync)) {
            // Check if we're running on D3D9 and show warning
            int current_api = g_last_reshade_device_api.load();
            if (current_api == static_cast<int>(reshade::api::device_api::d3d9)) {
                ImGui::TextColored(ui::colors::TEXT_WARNING,
                                   ICON_FK_WARNING " Warning: Reflex does not work with Direct3D 9");
            } else {
                if (ImGui::IsItemHovered()) {
                    std::string tooltip =
                        "Enable NVIDIA Reflex alongside OnPresentSync FPS limiter. Reflex will run at +0.5%% FPS "
                        "limit "
                        "for better latency reduction.";
                    auto last_params = ::g_last_reflex_params_set_by_addon.load();
                    if (last_params) {
                        float fps = (last_params->minimumIntervalUs > 0)
                                        ? (1000000.0f / static_cast<float>(last_params->minimumIntervalUs))
                                        : 0.0f;
                        tooltip += "\n\nLast Reflex settings we set via API:";
                        tooltip += "\n  Low Latency: ";
                        tooltip += (last_params->bLowLatencyMode != 0) ? "On" : "Off";
                        tooltip += ", Boost: ";
                        tooltip += (last_params->bLowLatencyBoost != 0) ? "On" : "Off";
                        tooltip += ", Use Markers: ";
                        tooltip += (last_params->bUseMarkersToOptimize != 0) ? "On" : "Off";
                        tooltip += "\n  FPS limit: ";
                        if (fps > 0.0f) {
                            std::ostringstream oss;
                            oss << std::fixed << std::setprecision(1) << fps;
                            tooltip += oss.str();
                        } else {
                            tooltip += "none";
                        }
                    }
                    ImGui::SetTooltip("%s", tooltip.c_str());
                }

                // Reflex mode selector for OnPresent: Low latency (default), Low+boost, Off, Game Defaults
                ImGui::Spacing();
                if (ComboSettingEnumRefWrapper(settings::g_mainTabSettings.onpresent_reflex_mode, "Reflex")) {
                    // Setting is automatically saved via ComboSettingEnumRefWrapper
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "NVIDIA Reflex setting when using OnPresent FPS limiter.\n\n"
                        "Low latency: Enables Reflex Low Latency Mode (default).\n"
                        "Low Latency + boost: Enables both Low Latency and Boost for maximum latency reduction.\n"
                        "Off: Disables both Low Latency and Boost.\n"
                        "Game Defaults: Do not override; use the game's own Reflex settings.");
                }

                // Low Latency Ratio Selector (Experimental WIP placeholder)
                ImGui::Spacing();
                // ImGui::TextColored(ui::colors::TEXT_HIGHLIGHT, "Low Latency Ratio:");
                // ImGui::SameLine();
                auto display_input_ratio = !(::IsNativeFramePacingInSync()
                                             && settings::g_mainTabSettings.native_pacing_sim_start_only.GetValue());

                if (display_input_ratio) {
                    if (ComboSettingWrapper(settings::g_mainTabSettings.onpresent_sync_low_latency_ratio,
                                            "Display / Input Ratio")) {
                        // Setting is automatically saved via ComboSettingWrapper
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Controls the balance between display latency and input latency.\n\n"
                            "Available in 12.5%% steps:\n"
                            "100%% Display / 0%% Input: Prioritizes consistent frame timing (better frame timing at "
                            "cost "
                            "of latency)\n"
                            "87.5%% Display / 12.5%% Input: Slight input latency reduction\n"
                            "75%% Display / 25%% Input: Moderate input latency reduction\n"
                            "62.5%% Display / 37.5%% Input: Balanced with slight input preference\n"
                            "50%% Display / 50%% Input: Balanced approach\n"
                            "37.5%% Display / 62.5%% Input: Balanced with slight display preference\n"
                            "25%% Display / 75%% Input: Prioritizes input responsiveness\n"
                            "12.5%% Display / 87.5%% Input: Strong input preference\n"
                            "0%% Display / 100%% Input: Maximum input responsiveness (lower latency)\n\n"
                            "Note: This is an experimental feature.");
                    }

                    // Debug Info Button
                    ImGui::SameLine();
                    static bool show_delay_bias_debug = false;
                    if (ImGui::SmallButton("[Debug]")) {
                        show_delay_bias_debug = !show_delay_bias_debug;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Show delay_bias debug information");
                    }

                    // Debug Info Window
                    if (show_delay_bias_debug) {
                        ImGui::Begin("Delay Bias Debug Info", &show_delay_bias_debug,
                                     ImGuiWindowFlags_AlwaysAutoResize);

                        // Get current values
                        int ratio_index = settings::g_mainTabSettings.onpresent_sync_low_latency_ratio.GetValue();
                        float delay_bias = g_onpresent_sync_delay_bias.load();
                        LONGLONG frame_time_ns = g_onpresent_sync_frame_time_ns.load();
                        LONGLONG last_frame_end_ns = g_onpresent_sync_last_frame_end_ns.load();
                        LONGLONG frame_start_ns = g_onpresent_sync_frame_start_ns.load();
                        LONGLONG pre_sleep_ns = g_onpresent_sync_pre_sleep_ns.load();
                        LONGLONG post_sleep_ns = g_onpresent_sync_post_sleep_ns.load();
                        LONGLONG late_ns = late_amount_ns.load();

                        // Display ratio index and delay_bias
                        ImGui::TextColored(ui::colors::TEXT_HIGHLIGHT, "Ratio Settings:");
                        ImGui::Text("Ratio Index: %d", ratio_index);
                        float display_pct = (1.0f - delay_bias) * 100.0f;
                        float input_pct = delay_bias * 100.0f;
                        ImGui::Text("Delay Bias: %.3f (%.1f%% Display / %.1f%% Input)", delay_bias, display_pct,
                                    input_pct);

                        ImGui::Spacing();
                        ImGui::TextColored(ui::colors::TEXT_HIGHLIGHT, "Frame Timing:");
                        if (frame_time_ns > 0) {
                            float frame_time_ms = frame_time_ns / 1'000'000.0f;
                            float target_fps = 1000.0f / frame_time_ms;
                            ImGui::Text("Frame Time: %.3f ms (%.1f FPS)", frame_time_ms, target_fps);
                        } else {
                            ImGui::TextColored(ui::colors::TEXT_WARNING, "Frame Time: Not set (FPS limiter disabled?)");
                        }

                        ImGui::Spacing();
                        ImGui::TextColored(ui::colors::TEXT_HIGHLIGHT, "Sleep Times:");
                        if (pre_sleep_ns > 0) {
                            ImGui::Text("Pre-Sleep: %.3f ms", pre_sleep_ns / 1'000'000.0f);
                        } else {
                            ImGui::Text("Pre-Sleep: 0 ms");
                        }
                        if (post_sleep_ns > 0) {
                            ImGui::Text("Post-Sleep: %.3f ms", post_sleep_ns / 1'000'000.0f);
                        } else {
                            ImGui::Text("Post-Sleep: 0 ms");
                        }
                        if (late_ns != 0) {
                            ImGui::TextColored(ui::colors::TEXT_WARNING, "Late Amount: %.3f ms",
                                               late_ns / 1'000'000.0f);
                        } else {
                            ImGui::Text("Late Amount: 0 ms");
                        }

                        ImGui::Spacing();
                        ImGui::TextColored(ui::colors::TEXT_HIGHLIGHT, "Frame Timing (Raw):");
                        if (last_frame_end_ns > 0) {
                            LONGLONG now_ns = utils::get_now_ns();
                            LONGLONG time_since_last_frame_ns = now_ns - last_frame_end_ns;
                            ImGui::Text("Last Frame End: %lld ns (%.3f ms ago)", last_frame_end_ns,
                                        time_since_last_frame_ns / 1'000'000.0f);
                        } else {
                            ImGui::Text("Last Frame End: Not set (first frame?)");
                        }
                        if (frame_start_ns > 0) {
                            LONGLONG now_ns = utils::get_now_ns();
                            LONGLONG time_since_start_ns = now_ns - frame_start_ns;
                            ImGui::Text("Frame Start: %lld ns (%.3f ms ago)", frame_start_ns,
                                        time_since_start_ns / 1'000'000.0f);
                        } else {
                            ImGui::Text("Frame Start: Not set");
                        }

                        ImGui::End();
                    }
                }
            }
        }

        if (current_item == static_cast<int>(FpsLimiterMode::kReflex)) {
            // Check if we're running on D3D9 and show warning
            int current_api = g_last_reshade_device_api.load();
            if (current_api == static_cast<int>(reshade::api::device_api::d3d9)) {
                ImGui::TextColored(ui::colors::TEXT_WARNING,
                                   ICON_FK_WARNING " Warning: Reflex does not work with Direct3D 9");
            } else {
                uint64_t now_ns = utils::get_now_ns();

                // Show Native Reflex status only when streamline is used
                if (g_swapchain_wrapper_present_called.load(std::memory_order_acquire)) {
                    if (IsNativeReflexActive(now_ns)) {
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                                           ICON_FK_OK " Native Reflex: ACTIVE Limit Real Frames: ON");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("The game has native Reflex support and is actively using it. ");
                        }
                        double native_ns = static_cast<double>(g_sleep_reflex_native_ns_smooth.load());
                        double calls_per_second = native_ns <= 0 ? -1 : 1000000000.0 / native_ns;
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                                           "Native Reflex: %.2f times/sec (%.1f ms interval)", calls_per_second,
                                           native_ns / 1000000.0);
                        if (ImGui::IsItemHovered()) {
                            double raw_ns = static_cast<double>(g_sleep_reflex_native_ns.load());
                            ImGui::SetTooltip("Smoothed interval using rolling average. Raw: %.1f ms",
                                              raw_ns / 1000000.0);
                        }
                        //   if (!DidNativeReflexSleepRecently(now_ns)) {
                        //     ImGui::TextColored(
                        //         ImVec4(1.0f, 0.6f, 0.0f, 1.0f), ICON_FK_WARNING
                        //      " Warning: Native Reflex is not sleeping recently - may indicate issues! (FIXME)");
                        // }
                    } else {
                        bool limit_real = settings::g_mainTabSettings.limit_real_frames.GetValue();
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                                           ICON_FK_OK " Injected Reflex: ACTIVE Limit Real Frames: %s",
                                           limit_real ? "ON" : "OFF");
                        double injected_ns = static_cast<double>(g_sleep_reflex_injected_ns_smooth.load());
                        double calls_per_second = injected_ns <= 0 ? -1 : 1000000000.0 / injected_ns;
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                                           "Injected Reflex: %.2f times/sec (%.1f ms interval)", calls_per_second,
                                           injected_ns / 1000000.0);
                        if (ImGui::IsItemHovered()) {
                            double raw_ns = static_cast<double>(g_sleep_reflex_injected_ns.load());
                            ImGui::SetTooltip("Smoothed interval using rolling average. Raw: %.1f ms",
                                              raw_ns / 1000000.0);
                        }

                        // Warn if both native and injected reflex are running simultaneously
                        if (DidNativeReflexSleepRecently(now_ns)) {
                            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), ICON_FK_WARNING
                                               " Warning: Both native and injected Reflex are active - this may cause "
                                               "conflicts! (FIXME)");
                        }
                    }
                }

                // Reflex mode selector for Reflex FPS limiter (same options as OnPresent)
                ImGui::Spacing();
                if (ComboSettingEnumRefWrapper(settings::g_mainTabSettings.reflex_limiter_reflex_mode, "Reflex")) {
                    g_reflex_settings_outdated.store(true);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "NVIDIA Reflex setting when using Reflex FPS limiter.\n\n"
                        "Low latency: Enables Reflex Low Latency Mode (default).\n"
                        "Low Latency + boost: Enables both Low Latency and Boost for maximum latency reduction.\n"
                        "Off: Disables both Low Latency and Boost.\n"
                        "Game Defaults: Do not override; use the game's own Reflex settings.");
                }
                ImGui::SameLine();
                bool pcl_stats = settings::g_mainTabSettings.pcl_stats_enabled.GetValue();
                if (ImGui::Checkbox("PCL stats", &pcl_stats)) {
                    settings::g_mainTabSettings.pcl_stats_enabled.SetValue(pcl_stats);
                    // Reinstall window proc hooks if PCL stats is enabled
                    HWND game_window = display_commanderhooks::GetGameWindow();
                    if (game_window != nullptr && pcl_stats) {
                        display_commanderhooks::InstallWindowProcHooks(game_window);
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Enables PCLStats ETW reporting for latency measurement.\nRequires window proc hooks to be "
                        "installed.\nWorks with Reflex and OnPresent sync modes.");
                }
            }
            if (IsNativeReflexActive() || settings::g_advancedTabSettings.reflex_supress_native.GetValue()) {
                ImGui::SameLine();
                if (CheckboxSetting(settings::g_advancedTabSettings.reflex_supress_native,
                                    ICON_FK_WARNING " Suppress Native Reflex")) {
                    g_reflex_settings_outdated.store(true);
                    LogInfo("Suppress Native Reflex %s",
                            settings::g_advancedTabSettings.reflex_supress_native.GetValue() ? "enabled" : "disabled");
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Override the game's native Reflex implementation with the addon's injected version.");
                }
            }
        }

        if (current_item == static_cast<int>(FpsLimiterMode::kReflex)) {
            // Suppress Reflex Sleep checkbox
            ImGui::Spacing();
            if (CheckboxSetting(settings::g_mainTabSettings.suppress_reflex_sleep, "Suppress Reflex Sleep")) {
                g_reflex_settings_outdated.store(true);
                LogInfo("Suppress Reflex Sleep %s",
                        settings::g_mainTabSettings.suppress_reflex_sleep.GetValue() ? "enabled" : "disabled");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Suppresses both native Reflex sleep calls (from the game) and injected Reflex sleep calls.\n"
                    "This prevents Reflex from sleeping the CPU, which may help with certain compatibility issues.");
            }
        }

        // PCL stats checkbox for OnPresentSync mode (when Reflex is not enabled)
        if (current_item == static_cast<int>(FpsLimiterMode::kOnPresentSync)) {
            ImGui::Spacing();
            bool pcl_stats = settings::g_mainTabSettings.pcl_stats_enabled.GetValue();
            if (ImGui::Checkbox("PCL stats", &pcl_stats)) {
                settings::g_mainTabSettings.pcl_stats_enabled.SetValue(pcl_stats);
                // Reinstall window proc hooks if PCL stats is enabled
                HWND game_window = display_commanderhooks::GetGameWindow();
                if (game_window != nullptr && pcl_stats) {
                    display_commanderhooks::InstallWindowProcHooks(game_window);
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Enables PCLStats ETW reporting for latency measurement.\nRequires window proc hooks to be "
                    "installed.\nNote: PCL stats markers are only emitted when Reflex is enabled.");
            }
        }

        // Experimental FG native fps limiter (only visible if OnPresentSync mode is selected and in sync)
        if (current_item == static_cast<int>(FpsLimiterMode::kOnPresentSync)) {
            if (::IsNativeFramePacingInSync()) {
                if (CheckboxSetting(settings::g_mainTabSettings.experimental_fg_native_fps_limiter,
                                    "Use Reflex Latency Markers as fps limiter")) {
                    LogInfo("Experimental FG native fps limiter %s",
                            settings::g_mainTabSettings.experimental_fg_native_fps_limiter.GetValue() ? "enabled"
                                                                                                      : "disabled");
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "When enabled with Frame Generation (DLSS-G) active, limits native (real) frame rate.\n"
                        "Experimental; may improve frame pacing with FG.");
                }
                if (CheckboxSetting(settings::g_mainTabSettings.native_pacing_sim_start_only, "Native frame pacing")) {
                    LogInfo(
                        "Native pacing sim start only %s",
                        settings::g_mainTabSettings.native_pacing_sim_start_only.GetValue() ? "enabled" : "disabled");
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "When enabled, native frame pacing uses SIMULATION_START instead of PRESENT_END.\n"
                        "Matches Special-K behavior (pacing on simulation thread rather than render thread).");
                }
                // Schedule present start N frame times after simulation start (default on, improves native frame
                // pacing)
                if (CheckboxSetting(settings::g_mainTabSettings.delay_present_start_after_sim_enabled,
                                    "Schedule present start N frame times after simulation start")) {
                    LogInfo("Schedule present start after Sim Start %s",
                            settings::g_mainTabSettings.delay_present_start_after_sim_enabled.GetValue() ? "enabled"
                                                                                                         : "disabled");
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "When enabled, PRESENT_START is scheduled for (SIMULATION_START + N frame times).\n"
                        "Improves frame pacing when using native frame pacing. Use the slider to set N (0 = no delay, "
                        "1 = one frame, 0.5 = half frame, etc.).");
                }
                ImGui::SameLine();
                if (SliderFloatSetting(settings::g_mainTabSettings.delay_present_start_frames, "Delay (frames)",
                                       "%.2f")) {
                    // Setting is automatically saved by SliderFloatSetting
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Frames to delay PRESENT_START after SIMULATION_START (0–2). 0 = no delay.");
                }
            }
        }

        // Experimental Safe Mode fps limiter (only visible if OnPresentSync mode is selected)
        if (enabled_experimental_features) {
            if (current_item == static_cast<int>(FpsLimiterMode::kOnPresentSync)) {
                if (CheckboxSetting(settings::g_mainTabSettings.experimental_safe_mode_fps_limiter,
                                    "Experimental Safe Mode fps limiter")) {
                    LogInfo("Experimental Safe Mode fps limiter %s",
                            settings::g_mainTabSettings.experimental_safe_mode_fps_limiter.GetValue() ? "enabled"
                                                                                                      : "disabled");
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Uses a safer FPS limiting path with reduced risk of stutter or instability.\n"
                        "Experimental; may have slightly higher latency than the default limiter.");
                }
            }
        }

        // Limit Real Frames indicator (only visible if OnPresentSync mode is selected)
        if (current_item == static_cast<int>(FpsLimiterMode::kOnPresentSync)) {
            if (g_swapchain_wrapper_present_called.load(std::memory_order_acquire)) {
                bool limit_real = settings::g_mainTabSettings.limit_real_frames.GetValue();
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Limit Real Frames: %s", limit_real ? "ON" : "OFF");
            }
        }
    }

    // Latent Sync Mode (only visible if Latent Sync limiter is selected)
    if (s_fps_limiter_mode.load() == FpsLimiterMode::kLatentSync) {
        // Scanline Offset (only visible if scanline mode is selected)
        int current_offset = settings::g_mainTabSettings.scanline_offset.GetValue();
        int temp_offset = current_offset;
        if (ImGui::SliderInt("Scanline Offset", &temp_offset, -1000, 1000, "%d")) {
            settings::g_mainTabSettings.scanline_offset.SetValue(temp_offset);
            s_scanline_offset.store(temp_offset);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Scanline offset for latent sync (-1000 to 1000). This defines the offset from the "
                "threshold where frame pacing is active.");
        }

        // VBlank Sync Divisor (only visible if latent sync mode is selected)
        int current_divisor = settings::g_mainTabSettings.vblank_sync_divisor.GetValue();
        int temp_divisor = current_divisor;
        if (ImGui::SliderInt("VBlank Sync Divisor (controls FPS limit as fraction of monitor refresh rate)",
                             &temp_divisor, 0, 8, "%d")) {
            settings::g_mainTabSettings.vblank_sync_divisor.SetValue(temp_divisor);
            s_vblank_sync_divisor.store(temp_divisor);
        }
        if (ImGui::IsItemHovered()) {
            // Calculate effective refresh rate based on monitor info
            auto window_state = ::g_window_state.load();
            double refresh_hz = 60.0;  // default fallback
            if (window_state) {
                refresh_hz = window_state->current_monitor_refresh_rate.ToHz();
            }

            std::ostringstream tooltip_oss;
            tooltip_oss << "VBlank Sync Divisor (0-8). Controls frame pacing similar to VSync divisors:\n\n";
            tooltip_oss << "  0 -> No additional wait (Off)\n";
            for (int div = 1; div <= 8; ++div) {
                int effective_fps = static_cast<int>(std::round(refresh_hz / div));
                tooltip_oss << "  " << div << " -> " << effective_fps << " FPS";
                if (div == 1)
                    tooltip_oss << " (Full Refresh)";
                else if (div == 2)
                    tooltip_oss << " (Half Refresh)";
                else
                    tooltip_oss << " (1/" << div << " Refresh)";
                tooltip_oss << "\n";
            }
            tooltip_oss << "\n0 = Disabled, higher values reduce effective frame rate for smoother frame pacing.";
            ImGui::SetTooltip("%s", tooltip_oss.str().c_str());
        }

        // VBlank Monitor Status (only visible if latent sync is enabled and FPS limit > 0)
        if (s_fps_limiter_mode.load() == FpsLimiterMode::kLatentSync) {
            if (dxgi::latent_sync::g_latentSyncManager) {
                auto& latent = dxgi::latent_sync::g_latentSyncManager->GetLatentLimiter();
                if (latent.IsVBlankMonitoringActive()) {
                    ImGui::Spacing();
                    ImGui::TextColored(ui::colors::STATUS_ACTIVE, "✁EVBlank Monitor: ACTIVE");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "VBlank monitoring thread is running and collecting scanline data for frame pacing.");
                    }

                    ImGui::TextColored(ui::colors::STATUS_INACTIVE, "  refresh time: %.3fms",
                                       1.0 * dxgi::fps_limiter::ns_per_refresh.load() / utils::NS_TO_MS);
                    ImGui::SameLine();
                    ImGui::TextColored(ui::colors::STATUS_INACTIVE, "  total_height: %llu",
                                       dxgi::fps_limiter::g_latent_sync_total_height.load());
                    ImGui::SameLine();
                    ImGui::TextColored(ui::colors::STATUS_INACTIVE, "  active_height: %llu",
                                       dxgi::fps_limiter::g_latent_sync_active_height.load());
                } else {
                    ImGui::Spacing();
                    ImGui::TextColored(ui::colors::STATUS_STARTING, ICON_FK_WARNING " VBlank Monitor: STARTING...");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "VBlank monitoring is enabled but the monitoring thread is still starting up.");
                    }
                }
            }
        }
    }
}

void DrawDisplaySettings_FpsAndBackground() {
    if (enabled_experimental_features) {
        if (g_swapchain_wrapper_present_called.load(std::memory_order_acquire)) {
            ImGui::Spacing();
            bool limit_real = settings::g_mainTabSettings.limit_real_frames.GetValue();
            if (ImGui::Checkbox("Limit Real Frames", &limit_real)) {
                settings::g_mainTabSettings.limit_real_frames.SetValue(limit_real);
                LogInfo(limit_real ? "Limit Real Frames enabled" : "Limit Real Frames disabled");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Limit real frames when using DLSS Frame Generation.\n"
                    "When enabled, the FPS limiter limits the game's internal framerate (real frames)\n"
                    "instead of generated frames. This helps maintain proper frame timing with Frame Gen enabled.");
            }
        }
    } else {
        if (settings::g_mainTabSettings.limit_real_frames.GetValue()) {
            settings::g_mainTabSettings.limit_real_frames.SetValue(false);
        }
    }
    ImGui::Spacing();

    // FPS Limit slider (persisted)

    bool fps_limit_enabled = s_fps_limiter_mode.load() != FpsLimiterMode::kDisabled
                                 && s_fps_limiter_mode.load() != FpsLimiterMode::kLatentSync
                             || settings::g_advancedTabSettings.reflex_enable.GetValue();

    {
        if (!fps_limit_enabled) {
            ImGui::BeginDisabled();
        }

        float current_value = settings::g_mainTabSettings.fps_limit.GetValue();
        const char* fmt = (current_value > 0.0f) ? "%.3f FPS" : "No Limit";
        if (SliderFloatSettingRef(settings::g_mainTabSettings.fps_limit, "FPS Limit", fmt)) {
        }

        auto cur_limit = settings::g_mainTabSettings.fps_limit.GetValue();
        if (cur_limit > 0.0f && cur_limit < 10.0f) {
            settings::g_mainTabSettings.fps_limit.SetValue(0.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Set FPS limit for the game (0 = no limit). Now uses the new Custom FPS Limiter system.");
        }

        if (!fps_limit_enabled) {
            ImGui::EndDisabled();
        }
    }

    // No Render in Background checkbox
    {
        bool no_render_in_bg = settings::g_mainTabSettings.no_render_in_background.GetValue();
        if (ImGui::Checkbox("No Render in Background", &no_render_in_bg)) {
            settings::g_mainTabSettings.no_render_in_background.SetValue(no_render_in_bg);
            // The setting is automatically synced via BoolSettingRef
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Skip rendering draw calls when the game window is not in the foreground. This can save "
                "GPU power and reduce background processing.");
        }
        ImGui::SameLine();
        bool no_present_in_bg = settings::g_mainTabSettings.no_present_in_background.GetValue();
        if (ImGui::Checkbox("No Present in Background", &no_present_in_bg)) {
            settings::g_mainTabSettings.no_present_in_background.SetValue(no_present_in_bg);
            // The setting is automatically synced via BoolSettingRef
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Skip ReShade's on_present processing when the game window is not in the foreground. "
                "This can save GPU power and reduce background processing.");
        }
    }
}

// Context for VSync & Tearing swapchain debug tooltip (filled by PresentModeLine, consumed by SwapchainTooltip).
struct VSyncTearingTooltipContext {
    const reshade::api::swapchain_desc* desc = nullptr;
    DxgiBypassMode flip_state = DxgiBypassMode::kUnset;
    std::string flip_state_str;
    std::string present_mode_name;
};

static void DrawDisplaySettings_VSyncAndTearing_FpsSliders() {
    bool fps_limit_enabled = s_fps_limiter_mode.load() != FpsLimiterMode::kDisabled
                                 && s_fps_limiter_mode.load() != FpsLimiterMode::kLatentSync
                             || settings::g_advancedTabSettings.reflex_enable.GetValue();
    {
        if (!fps_limit_enabled) {
            ImGui::BeginDisabled();
        }
        DrawQuickFpsLimitChanger();
        if (!fps_limit_enabled) {
            ImGui::EndDisabled();
        }
    }
    ImGui::Spacing();
    {
        if (!fps_limit_enabled) {
            ImGui::BeginDisabled();
        }
        float current_bg = settings::g_mainTabSettings.fps_limit_background.GetValue();
        const char* fmt_bg = (current_bg > 0.0f) ? "%.0f FPS" : "No Limit";
        if (SliderFloatSettingRef(settings::g_mainTabSettings.fps_limit_background, "Background FPS Limit", fmt_bg)) {
        }
        if (!fps_limit_enabled) {
            ImGui::EndDisabled();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "FPS cap when the game window is not in the foreground. Now uses the new Custom FPS Limiter "
                "system.");
        }
    }
}

static void DrawDisplaySettings_VSyncAndTearing_Checkboxes() {
    if (g_reshade_event_counters[RESHADE_EVENT_CREATE_SWAPCHAIN_CAPTURE].load() > 0) {
        bool vs_on = settings::g_mainTabSettings.force_vsync_on.GetValue();
        if (ImGui::Checkbox("Force VSync ON", &vs_on)) {
            s_restart_needed_vsync_tearing.store(true);
            if (vs_on) {
                settings::g_mainTabSettings.force_vsync_off.SetValue(false);
            }
            settings::g_mainTabSettings.force_vsync_on.SetValue(vs_on);
            LogInfo(vs_on ? "Force VSync ON enabled" : "Force VSync ON disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Forces sync interval = 1 (requires restart).");
        }
        ImGui::SameLine();

        bool vs_off = settings::g_mainTabSettings.force_vsync_off.GetValue();
        if (ImGui::Checkbox("Force VSync OFF", &vs_off)) {
            s_restart_needed_vsync_tearing.store(true);
            if (vs_off) {
                settings::g_mainTabSettings.force_vsync_on.SetValue(false);
            }
            settings::g_mainTabSettings.force_vsync_off.SetValue(vs_off);
            LogInfo(vs_off ? "Force VSync OFF enabled" : "Force VSync OFF disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Forces sync interval = 0 (requires restart).");
        }
        ImGui::SameLine();

        bool prevent_t = settings::g_mainTabSettings.prevent_tearing.GetValue();
        if (ImGui::Checkbox("Prevent Tearing", &prevent_t)) {
            settings::g_mainTabSettings.prevent_tearing.SetValue(prevent_t);
            LogInfo(prevent_t ? "Prevent Tearing enabled (tearing flags will be cleared)" : "Prevent Tearing disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Prevents tearing by clearing DXGI tearing flags and preferring sync.");
        }
    } else {
        ImGui::TextColored(ui::colors::TEXT_WARNING,
                           "VSYNC ON/OFF Prevent Tearing options unavailable due to reshade bug!");
    }

    auto desc_ptr = g_last_swapchain_desc.load();
    if (desc_ptr && desc_ptr->back_buffer_count < 3
        || settings::g_mainTabSettings.increase_backbuffer_count_to_3.GetValue()) {
        ImGui::SameLine();
        bool increase_backbuffer = settings::g_mainTabSettings.increase_backbuffer_count_to_3.GetValue();
        if (ImGui::Checkbox("Increase Backbuffer Count to 3", &increase_backbuffer)) {
            settings::g_mainTabSettings.increase_backbuffer_count_to_3.SetValue(increase_backbuffer);
            s_restart_needed_vsync_tearing.store(true);
            LogInfo(increase_backbuffer ? "Increase Backbuffer Count to 3 enabled"
                                        : "Increase Backbuffer Count to 3 disabled");
        }
        if (ImGui::IsItemHovered()) {
            std::ostringstream tooltip;
            tooltip << "Increases backbuffer count from " << desc_ptr->back_buffer_count
                    << " to 3 (requires restart).\n"
                    << "Current backbuffer count: " << desc_ptr->back_buffer_count;
            ImGui::SetTooltip("%s", tooltip.str().c_str());
        }
    }

    int current_api = g_last_reshade_device_api.load();
    bool is_d3d9 = current_api == static_cast<int>(reshade::api::device_api::d3d9);
    bool is_dxgi = g_last_reshade_device_api.load() == static_cast<int>(reshade::api::device_api::d3d10)
                   || current_api == static_cast<int>(reshade::api::device_api::d3d11)
                   || current_api == static_cast<int>(reshade::api::device_api::d3d12);
    bool enable_flip = settings::g_advancedTabSettings.enable_flip_chain.GetValue();
    bool is_flip = g_last_swapchain_desc.load()
                   && (g_last_swapchain_desc.load()->present_mode == DXGI_SWAP_EFFECT_FLIP_DISCARD
                       || g_last_swapchain_desc.load()->present_mode == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL);
    static bool has_been_enabled = false;
    has_been_enabled |= is_dxgi && (enable_flip || !is_flip);

    if (has_been_enabled) {
        ImGui::SameLine();
        if (ImGui::Checkbox("Enable Flip Chain (requires restart)", &enable_flip)) {
            settings::g_advancedTabSettings.enable_flip_chain.SetValue(enable_flip);
            s_enable_flip_chain.store(enable_flip);
            s_restart_needed_vsync_tearing.store(true);
            LogInfo(enable_flip ? "Enable Flip Chain enabled" : "Enable Flip Chain disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Forces games to use flip model swap chains (FLIP_DISCARD) for better performance.\n"
                "This setting requires a game restart to take effect.\n"
                "Only works with DirectX 10/11/12 (DXGI) games.");
        }
    }

    if (is_d3d9) {
        ImGui::SameLine();
        bool enable_d9ex_with_flip = settings::g_experimentalTabSettings.d3d9_flipex_enabled.GetValue();
        if (ImGui::Checkbox("Enable Flip State (requires restart)", &enable_d9ex_with_flip)) {
            settings::g_experimentalTabSettings.d3d9_flipex_enabled.SetValue(enable_d9ex_with_flip);
            LogInfo(enable_d9ex_with_flip ? "Enable D9EX with Flip Model enabled"
                                          : "Enable D9EX with Flip Model disabled");
        }
    }

    if (s_restart_needed_vsync_tearing.load()) {
        ImGui::Spacing();
        ImGui::TextColored(ui::colors::TEXT_ERROR, "Game restart required to apply VSync/tearing changes.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

static void DrawDisplaySettings_VSyncAndTearing_PresentMonETWSubsection() {
    presentmon::PresentMonFlipState pm_flip_state;
    presentmon::PresentMonDebugInfo pm_debug_info;
    bool has_pm_flip_state = presentmon::g_presentMonManager.GetFlipState(pm_flip_state);
    presentmon::g_presentMonManager.GetDebugInfo(pm_debug_info);

    ImGui::TextColored(ui::colors::TEXT_LABEL, "PresentMon Flip State:");
    if (has_pm_flip_state) {
        const char* pm_flip_str = DxgiBypassModeToString(pm_flip_state.flip_mode);
        ImVec4 pm_flip_color;
        if (pm_flip_state.flip_mode == DxgiBypassMode::kComposed) {
            pm_flip_color = ui::colors::FLIP_COMPOSED;
        } else if (pm_flip_state.flip_mode == DxgiBypassMode::kOverlay
                   || pm_flip_state.flip_mode == DxgiBypassMode::kIndependentFlip) {
            pm_flip_color = ui::colors::FLIP_INDEPENDENT;
        } else {
            pm_flip_color = ui::colors::FLIP_UNKNOWN;
        }
        ImGui::TextColored(pm_flip_color, "  %s", pm_flip_str);
        if (!pm_flip_state.present_mode_str.empty()) {
            ImGui::Text("  Mode: %s", pm_flip_state.present_mode_str.c_str());
        }
        if (!pm_flip_state.debug_info.empty()) {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "  Info: %s", pm_flip_state.debug_info.c_str());
        }
        LONGLONG now_ns = utils::get_now_ns();
        LONGLONG age_ns = now_ns - static_cast<LONGLONG>(pm_flip_state.last_update_time);
        double age_ms = static_cast<double>(age_ns) / 1000000.0;
        if (age_ms < 1000.0) {
            ImGui::TextColored(ui::colors::TEXT_SUCCESS, "  Age: %.1f ms", age_ms);
        } else {
            ImGui::TextColored(ui::colors::TEXT_WARNING, "  Age: %.1f s (stale)", age_ms / 1000.0);
        }
    } else {
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "  No flip state data available");
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "  Waiting for a PresentMode-like ETW property/event");
        if (!pm_debug_info.last_present_mode_value.empty()) {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "  Last PresentMode-like: %s",
                               pm_debug_info.last_present_mode_value.c_str());
        }
    }

    ImGui::Spacing();
    HWND game_hwnd = g_last_swapchain_hwnd.load();
    if (game_hwnd != nullptr && IsWindow(game_hwnd)) {
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ui::colors::TEXT_LABEL, "Layer Information (Game HWND: 0x%p):", game_hwnd);
        std::vector<presentmon::PresentMonSurfaceCompatibilitySummary> surfaces;
        presentmon::g_presentMonManager.GetRecentFlipCompatibilitySurfaces(surfaces, 3600000);
        bool found_layer = false;
        for (const auto& surface : surfaces) {
            if (surface.hwnd == reinterpret_cast<uint64_t>(game_hwnd)) {
                found_layer = true;
                ImGui::Indent();
                ImGui::Text("Surface LUID: 0x%llX", surface.surface_luid);
                ImGui::Text("Surface Size: %ux%u", surface.surface_width, surface.surface_height);
                if (surface.pixel_format != 0) {
                    ImGui::Text("Pixel Format: 0x%X", surface.pixel_format);
                }
                if (surface.color_space != 0) {
                    ImGui::Text("Color Space: 0x%X", surface.color_space);
                }
                ImGui::Spacing();
                ImGui::TextColored(ui::colors::TEXT_LABEL, "Flip Compatibility:");
                if (surface.is_direct_flip_compatible) {
                    ImGui::TextColored(ui::colors::TEXT_SUCCESS, "  " ICON_FK_OK " Direct Flip Compatible");
                } else {
                    ImGui::TextColored(ui::colors::TEXT_DIMMED, "  " ICON_FK_CANCEL " Direct Flip Compatible");
                }
                if (surface.is_advanced_direct_flip_compatible) {
                    ImGui::TextColored(ui::colors::TEXT_SUCCESS, "  " ICON_FK_OK " Advanced Direct Flip Compatible");
                } else {
                    ImGui::TextColored(ui::colors::TEXT_DIMMED, "  " ICON_FK_CANCEL " Advanced Direct Flip Compatible");
                }
                if (surface.is_overlay_compatible) {
                    ImGui::TextColored(ui::colors::TEXT_SUCCESS, "  " ICON_FK_OK " Overlay Compatible");
                } else {
                    ImGui::TextColored(ui::colors::TEXT_DIMMED, "  " ICON_FK_CANCEL " Overlay Compatible");
                }
                if (surface.is_overlay_required) {
                    ImGui::TextColored(ui::colors::TEXT_WARNING, "  " ICON_FK_WARNING " Overlay Required");
                }
                if (surface.no_overlapping_content) {
                    ImGui::TextColored(ui::colors::TEXT_SUCCESS, "  " ICON_FK_OK " No Overlapping Content");
                } else {
                    ImGui::TextColored(ui::colors::TEXT_DIMMED, "  " ICON_FK_CANCEL " No Overlapping Content");
                }
                if (surface.last_update_time_ns > 0) {
                    LONGLONG now_ns = utils::get_now_ns();
                    LONGLONG age_ns = now_ns - static_cast<LONGLONG>(surface.last_update_time_ns);
                    double age_ms = static_cast<double>(age_ns) / 1000000.0;
                    ImGui::Spacing();
                    if (age_ms < 1000.0) {
                        ImGui::TextColored(ui::colors::TEXT_SUCCESS, "Last Update: %.1f ms ago", age_ms);
                    } else {
                        ImGui::TextColored(ui::colors::TEXT_WARNING, "Last Update: %.1f s ago", age_ms / 1000.0);
                    }
                }
                if (surface.count > 0) {
                    ImGui::Text("Event Count: %llu", surface.count);
                }
                ImGui::Unindent();
                break;
            }
        }
        if (!found_layer) {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "  No layer information found for this HWND");
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "  Waiting for PresentMon events...");
            if (!surfaces.empty()) {
                ImGui::TextColored(ui::colors::TEXT_DIMMED, "  (%zu surfaces tracked, none match)", surfaces.size());
            }
        }
    } else {
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "Layer Information: Game window not available");
    }

    ImGui::Spacing();
    ImGui::TextColored(ui::colors::TEXT_LABEL, "PresentMon Debug Info:");
    ImGui::Text("  Thread Status: %s", pm_debug_info.thread_status.c_str());
    if (pm_debug_info.is_running) {
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::TEXT_SUCCESS, ICON_FK_OK);
    } else {
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::TEXT_ERROR, ICON_FK_CANCEL);
    }
    if (!pm_debug_info.etw_session_name.empty()) {
        ImGui::Text("  ETW Session: %s [%s]", pm_debug_info.etw_session_status.c_str(),
                    pm_debug_info.etw_session_name.c_str());
    } else {
        ImGui::Text("  ETW Session: %s", pm_debug_info.etw_session_status.c_str());
    }
    if (pm_debug_info.etw_session_active) {
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::TEXT_SUCCESS, ICON_FK_OK);
    } else {
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::TEXT_WARNING, ICON_FK_WARNING);
    }
    if (pm_debug_info.events_processed > 0) {
        ImGui::Text("  Events Processed: %llu", pm_debug_info.events_processed);
        if (pm_debug_info.events_lost > 0) {
            ImGui::TextColored(ui::colors::TEXT_WARNING, "  Events Lost: %llu", pm_debug_info.events_lost);
        }
        if (pm_debug_info.last_event_time > 0) {
            LONGLONG now_ns = utils::get_now_ns();
            LONGLONG age_ns = now_ns - static_cast<LONGLONG>(pm_debug_info.last_event_time);
            double age_ms = static_cast<double>(age_ns) / 1000000.0;
            if (age_ms < 1000.0) {
                ImGui::TextColored(ui::colors::TEXT_SUCCESS, "  Last Event: %.1f ms ago", age_ms);
            } else {
                ImGui::TextColored(ui::colors::TEXT_WARNING, "  Last Event: %.1f s ago", age_ms / 1000.0);
            }
        }
    } else {
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "  Events Processed: 0 (ETW not active)");
    }
    if (!pm_debug_info.last_error.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ui::colors::TEXT_ERROR, "  Error: %s", pm_debug_info.last_error.c_str());
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ui::colors::TEXT_LABEL, "Troubleshooting:");
    if (!pm_debug_info.is_running) {
        ImGui::BulletText("PresentMon thread is not running");
        ImGui::BulletText("Check Advanced tab -> Enable PresentMon ETW Tracing");
    } else if (!pm_debug_info.etw_session_active) {
        ImGui::BulletText("ETW session is not active");
        ImGui::BulletText("You may need admin or Performance Log Users group membership");
    } else if (pm_debug_info.events_processed == 0) {
        ImGui::BulletText("No events processed yet");
        ImGui::BulletText("ETW session may need time to initialize");
    } else if (pm_debug_info.events_lost > 0) {
        ImGui::BulletText("Events are being lost - ETW buffer may be too small");
        ImGui::BulletText("Check Windows Event Viewer for ETW errors");
    } else {
        ImGui::BulletText("PresentMon appears to be working correctly");
    }
}

static void DrawDisplaySettings_VSyncAndTearing_SwapchainTooltip(const VSyncTearingTooltipContext& ctx) {
    const auto& desc = *ctx.desc;
    const DxgiBypassMode flip_state = ctx.flip_state;
    const char* flip_state_str = ctx.flip_state_str.c_str();

    ImGui::TextColored(ui::colors::TEXT_LABEL, "Swapchain Information:");
    ImGui::Separator();
    ImGui::Text("Present Mode: %s", ctx.present_mode_name.c_str());
    ImGui::Text("Present Mode ID: %u", desc.present_mode);
    ImGui::Text("Status: %s", flip_state_str);

    HWND game_window = display_commanderhooks::GetGameWindow();
    if (game_window != nullptr && IsWindow(game_window)) {
        ImGui::Separator();
        ImGui::TextColored(ui::colors::TEXT_LABEL, "Window Information (Debug):");
        RECT window_rect = {};
        RECT client_rect = {};
        if (GetWindowRect(game_window, &window_rect) && GetClientRect(game_window, &client_rect)) {
            ImGui::Text("Window Rect: (%ld, %ld) to (%ld, %ld)", window_rect.left, window_rect.top, window_rect.right,
                        window_rect.bottom);
            ImGui::Text("Window Size: %ld x %ld", window_rect.right - window_rect.left,
                        window_rect.bottom - window_rect.top);
            ImGui::Text("Client Rect: (%ld, %ld) to (%ld, %ld)", client_rect.left, client_rect.top, client_rect.right,
                        client_rect.bottom);
            ImGui::Text("Client Size: %ld x %ld", client_rect.right - client_rect.left,
                        client_rect.bottom - client_rect.top);
        }
        LONG_PTR style = GetWindowLongPtrW(game_window, GWL_STYLE);
        LONG_PTR ex_style = GetWindowLongPtrW(game_window, GWL_EXSTYLE);
        ImGui::Text("Window Style: 0x%08lX", static_cast<unsigned long>(style));
        ImGui::Text("Window ExStyle: 0x%08lX", static_cast<unsigned long>(ex_style));
        bool is_popup = (style & WS_POPUP) != 0;
        bool is_child = (style & WS_CHILD) != 0;
        bool has_caption = (style & WS_CAPTION) != 0;
        bool has_border = (style & WS_BORDER) != 0;
        bool is_layered = (ex_style & WS_EX_LAYERED) != 0;
        bool is_topmost = (ex_style & WS_EX_TOPMOST) != 0;
        bool is_transparent = (ex_style & WS_EX_TRANSPARENT) != 0;
        ImGui::Text("  WS_POPUP: %s", is_popup ? "Yes" : "No");
        ImGui::Text("  WS_CHILD: %s", is_child ? "Yes" : "No");
        ImGui::Text("  WS_CAPTION: %s", has_caption ? "Yes" : "No");
        ImGui::Text("  WS_BORDER: %s", has_border ? "Yes" : "No");
        ImGui::Text("  WS_EX_LAYERED: %s", is_layered ? "Yes" : "No");
        ImGui::Text("  WS_EX_TOPMOST: %s", is_topmost ? "Yes" : "No");
        ImGui::Text("  WS_EX_TRANSPARENT: %s", is_transparent ? "Yes" : "No");
        ImGui::Separator();
        ImGui::TextColored(ui::colors::TEXT_LABEL, "Size Comparison:");
        ImGui::Text("Backbuffer: %ux%u", desc.back_buffer.texture.width, desc.back_buffer.texture.height);
        if (GetWindowRect(game_window, &window_rect)) {
            long window_width = window_rect.right - window_rect.left;
            long window_height = window_rect.bottom - window_rect.top;
            ImGui::Text("Window: %ldx%ld", window_width, window_height);
            bool size_matches = (static_cast<long>(desc.back_buffer.texture.width) == window_width
                                 && static_cast<long>(desc.back_buffer.texture.height) == window_height);
            if (size_matches) {
                ImGui::TextColored(ui::colors::TEXT_SUCCESS, "  Sizes match");
            } else {
                ImGui::TextColored(ui::colors::TEXT_WARNING, "  Sizes differ (may cause Composed Flip)");
            }
        }
        ImGui::Separator();
        ImGui::TextColored(ui::colors::TEXT_LABEL, "Display Information:");
        HMONITOR monitor = MonitorFromWindow(game_window, MONITOR_DEFAULTTONEAREST);
        if (monitor != nullptr) {
            MONITORINFOEXW monitor_info = {};
            monitor_info.cbSize = sizeof(MONITORINFOEXW);
            if (GetMonitorInfoW(monitor, &monitor_info)) {
                ImGui::Text("Monitor Rect: (%ld, %ld) to (%ld, %ld)", monitor_info.rcMonitor.left,
                            monitor_info.rcMonitor.top, monitor_info.rcMonitor.right, monitor_info.rcMonitor.bottom);
                long monitor_width = monitor_info.rcMonitor.right - monitor_info.rcMonitor.left;
                long monitor_height = monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top;
                ImGui::Text("Monitor Size: %ld x %ld", monitor_width, monitor_height);
                if (GetWindowRect(game_window, &window_rect)) {
                    bool covers_monitor = (window_rect.left == monitor_info.rcMonitor.left
                                           && window_rect.top == monitor_info.rcMonitor.top
                                           && window_rect.right == monitor_info.rcMonitor.right
                                           && window_rect.bottom == monitor_info.rcMonitor.bottom);
                    if (covers_monitor) {
                        ImGui::TextColored(ui::colors::TEXT_SUCCESS, "  Window covers entire monitor");
                    } else {
                        ImGui::TextColored(ui::colors::TEXT_WARNING, "  Window does not cover entire monitor");
                    }
                }
            }
        }
    }

    if (flip_state == DxgiBypassMode::kComposed) {
        ImGui::Separator();
        ImGui::TextColored(ui::colors::FLIP_COMPOSED,
                           "  - Composed Flip (Red): Desktop Window Manager composition mode");
        ImGui::Text("    Higher latency, not ideal for gaming");
        ImGui::Spacing();
        ImGui::TextColored(ui::colors::TEXT_LABEL, "Why Composed Flip?");
        ImGui::BulletText("Fullscreen: No (Borderless windowed mode)");
        ImGui::BulletText("DWM composition required for windowed mode");
        ImGui::BulletText("Independent Flip requires True Fullscreen Exclusive (FSE)");
        ImGui::Spacing();
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "To achieve Independent Flip:");
        ImGui::BulletText("Enable True Fullscreen Exclusive in game settings");
        ImGui::BulletText("Or use borderless fullscreen with exact resolution match");
        ImGui::BulletText("Ensure no overlays or DWM effects are active");
    } else if (flip_state == DxgiBypassMode::kOverlay) {
        ImGui::TextColored(ui::colors::FLIP_INDEPENDENT,
                           "  - MPO Independent Flip (Green): Modern hardware overlay plane");
        ImGui::Text("    Best performance and lowest latency");
    } else if (flip_state == DxgiBypassMode::kIndependentFlip) {
        ImGui::TextColored(ui::colors::FLIP_INDEPENDENT, "  - Independent Flip (Green): Legacy direct flip mode");
        ImGui::Text("    Good performance and low latency");
    } else if (flip_state == DxgiBypassMode::kQueryFailedSwapchainNull) {
        ImGui::TextColored(ui::colors::TEXT_ERROR, "  - Query Failed: Swapchain is null");
        ImGui::Text("    Cannot determine flip state - swapchain not available");
    } else if (flip_state == DxgiBypassMode::kQueryFailedNoMedia) {
        if (GetModuleHandleA("sl.interposer.dll") != nullptr) {
            ImGui::TextColored(ui::colors::TEXT_ERROR,
                               ICON_FK_WARNING "  - Streamline Interposer detected - Flip State Query not supported");
            ImGui::Text("    Cannot determine flip state - call after at least one Present");
        } else {
            ImGui::TextColored(ui::colors::TEXT_ERROR, "  • Query Failed: GetFrameStatisticsMedia failed");
            ImGui::Text("    Cannot determine flip state - call after at least one Present");
        }
    } else if (flip_state == DxgiBypassMode::kQueryFailedNoStats) {
        ImGui::TextColored(ui::colors::TEXT_ERROR, "  • Query Failed: GetFrameStatisticsMedia failed");
        ImGui::Text("    Cannot determine flip state - call after at least one Present");
    } else if (flip_state == DxgiBypassMode::kQueryFailedNoSwapchain1) {
        ImGui::TextColored(ui::colors::TEXT_ERROR, "  • Query Failed: IDXGISwapChain1 not available");
        ImGui::Text("    Cannot determine flip state - SwapChain1 interface not supported");
    } else if (flip_state == DxgiBypassMode::kUnset) {
        ImGui::TextColored(ui::colors::FLIP_UNKNOWN, "  • Flip state not yet queried");
        ImGui::Text("    Initial state - will be determined on first query");
    } else {
        ImGui::TextColored(ui::colors::FLIP_UNKNOWN, "  • Flip state not yet determined");
        ImGui::Text("    Wait for a few frames to render");
    }

    ImGui::Text("Back Buffer Count: %u", desc.back_buffer_count);
    ImGui::Text("Back Buffer Size: %ux%u", desc.back_buffer.texture.width, desc.back_buffer.texture.height);
    const char* format_name = "Unknown";
    switch (desc.back_buffer.texture.format) {
        case reshade::api::format::r10g10b10a2_unorm:  format_name = "R10G10B10A2_UNORM (HDR 10-bit)"; break;
        case reshade::api::format::r16g16b16a16_float: format_name = "R16G16B16A16_FLOAT (HDR 16-bit)"; break;
        case reshade::api::format::r8g8b8a8_unorm:     format_name = "R8G8B8A8_UNORM (SDR 8-bit)"; break;
        case reshade::api::format::b8g8r8a8_unorm:     format_name = "B8G8R8A8_UNORM (SDR 8-bit)"; break;
        default:                                       format_name = "Unknown Format"; break;
    }
    ImGui::Text("Back Buffer Format: %s", format_name);
    ImGui::Text("Sync Interval: %u", desc.sync_interval);
    ImGui::Text("Fullscreen: %s", desc.fullscreen_state ? "Yes" : "No");
    if (desc.fullscreen_state && desc.fullscreen_refresh_rate > 0) {
        ImGui::Text("Refresh Rate: %.2f Hz", desc.fullscreen_refresh_rate);
    }

    ImGui::Separator();
    ImGui::Spacing();
    g_rendering_ui_section.store("ui:tab:main_new:presentmon", std::memory_order_release);
    if (ImGui::CollapsingHeader("PresentMon ETW Flip State & Debug Info", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        DrawDisplaySettings_VSyncAndTearing_PresentMonETWSubsection();
        ImGui::Unindent();
    }

    if (desc.present_flags != 0) {
        ImGui::Text("Device Creation Flags: 0x%X", desc.present_flags);
        ImGui::Text("Flags:");
        if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) {
            ImGui::Text("  • ALLOW_TEARING (VRR/G-Sync)");
        }
        if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) {
            ImGui::Text("  • FRAME_LATENCY_WAITABLE_OBJECT");
        }
        if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_DISPLAY_ONLY) {
            ImGui::Text("  • DISPLAY_ONLY");
        }
        if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_RESTRICTED_CONTENT) {
            ImGui::Text("  • RESTRICTED_CONTENT");
        }
    }
}

/// Draws PresentMon ON/OFF status line. When PresentMon is ON, also draws surface LUID,
/// flip-from-surface, and surface tooltip when available (all APIs: DX9, DXGI, Vulkan, OpenGL).
static void DrawDisplaySettings_VSyncAndTearing_PresentMonStatusLine() {
    if (settings::g_advancedTabSettings.enable_presentmon_tracing.GetValue()
        && presentmon::g_presentMonManager.IsRunning()) {
        ImGui::TextColored(ui::colors::TEXT_SUCCESS, "PresentMon: ON");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("PresentMon ETW tracing is active and monitoring presentation events.");
        }
        HWND game_hwnd = g_last_swapchain_hwnd.load();
        if (game_hwnd != nullptr && IsWindow(game_hwnd)) {
            std::vector<presentmon::PresentMonSurfaceCompatibilitySummary> surfaces;
            presentmon::g_presentMonManager.GetRecentFlipCompatibilitySurfaces(surfaces, 3600000);
            const presentmon::PresentMonSurfaceCompatibilitySummary* found_surface = nullptr;
            for (const auto& surface : surfaces) {
                if (surface.hwnd == reinterpret_cast<uint64_t>(game_hwnd)) {
                    found_surface = &surface;
                    break;
                }
            }
            if (found_surface != nullptr) {
                DxgiBypassMode determined_flip_mode = DxgiBypassMode::kComposed;
                if (found_surface->is_overlay_compatible
                    && (found_surface->is_overlay_required || found_surface->no_overlapping_content)) {
                    determined_flip_mode = DxgiBypassMode::kOverlay;
                } else if (found_surface->is_advanced_direct_flip_compatible) {
                    determined_flip_mode = DxgiBypassMode::kIndependentFlip;
                } else if (found_surface->is_direct_flip_compatible) {
                    determined_flip_mode = DxgiBypassMode::kIndependentFlip;
                } else {
                    determined_flip_mode = DxgiBypassMode::kComposed;
                }
                const char* flip_str = DxgiBypassModeToString(determined_flip_mode);
                ImVec4 flip_color;
                if (determined_flip_mode == DxgiBypassMode::kComposed) {
                    flip_color = ui::colors::FLIP_COMPOSED;
                } else if (determined_flip_mode == DxgiBypassMode::kOverlay
                           || determined_flip_mode == DxgiBypassMode::kIndependentFlip) {
                    flip_color = ui::colors::FLIP_INDEPENDENT;
                } else {
                    flip_color = ui::colors::FLIP_UNKNOWN;
                }
                ImGui::SameLine();
                ImGui::TextColored(ui::colors::TEXT_DIMMED, " | ");
                ImGui::SameLine();
                ImGui::TextColored(ui::colors::TEXT_LABEL, "Surface: 0x%llX", found_surface->surface_luid);
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextColored(ui::colors::TEXT_LABEL, "PresentMon Surface Information:");
                    ImGui::Separator();
                    ImGui::Text("Surface LUID: 0x%llX", found_surface->surface_luid);
                    ImGui::Text("Surface Size: %ux%u", found_surface->surface_width, found_surface->surface_height);
                    if (found_surface->pixel_format != 0) {
                        ImGui::Text("Pixel Format: 0x%X", found_surface->pixel_format);
                    }
                    if (found_surface->flags != 0) {
                        ImGui::Text("Flags: 0x%X", found_surface->flags);
                    }
                    if (found_surface->color_space != 0) {
                        ImGui::Text("Color Space: 0x%X", found_surface->color_space);
                    }
                    ImGui::Separator();
                    ImGui::TextColored(ui::colors::TEXT_LABEL, "Surface Delays:");
                    if (found_surface->last_update_time_ns > 0) {
                        LONGLONG now_ns = utils::get_now_ns();
                        LONGLONG age_ns = now_ns - static_cast<LONGLONG>(found_surface->last_update_time_ns);
                        double age_ms = static_cast<double>(age_ns) / 1000000.0;
                        if (age_ms < 1000.0) {
                            ImGui::TextColored(ui::colors::TEXT_SUCCESS, "Last Update: %.1f ms ago", age_ms);
                        } else {
                            ImGui::TextColored(ui::colors::TEXT_WARNING, "Last Update: %.1f s ago", age_ms / 1000.0);
                        }
                    } else {
                        ImGui::TextColored(ui::colors::TEXT_DIMMED, "Last Update: Unknown");
                    }
                    if (found_surface->count > 0) {
                        ImGui::Text("Event Count: %llu", found_surface->count);
                        if (found_surface->count > 1 && found_surface->last_update_time_ns > 0) {
                            double avg_delay_ms = static_cast<double>(found_surface->last_update_time_ns) / 1000000.0
                                                  / static_cast<double>(found_surface->count);
                            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Avg Delay: ~%.2f ms", avg_delay_ms);
                        }
                    }
                    ImGui::Separator();
                    ImGui::TextColored(ui::colors::TEXT_LABEL, "Flip Compatibility:");
                    if (found_surface->is_direct_flip_compatible) {
                        ImGui::TextColored(ui::colors::TEXT_SUCCESS, "  " ICON_FK_OK " Direct Flip Compatible");
                    } else {
                        ImGui::TextColored(ui::colors::TEXT_DIMMED, "  " ICON_FK_CANCEL " Direct Flip Compatible");
                    }
                    if (found_surface->is_advanced_direct_flip_compatible) {
                        ImGui::TextColored(ui::colors::TEXT_SUCCESS,
                                           "  " ICON_FK_OK " Advanced Direct Flip Compatible");
                    } else {
                        ImGui::TextColored(ui::colors::TEXT_DIMMED,
                                           "  " ICON_FK_CANCEL " Advanced Direct Flip Compatible");
                    }
                    if (found_surface->is_overlay_compatible) {
                        ImGui::TextColored(ui::colors::TEXT_SUCCESS, "  " ICON_FK_OK " Overlay Compatible");
                    } else {
                        ImGui::TextColored(ui::colors::TEXT_DIMMED, "  " ICON_FK_CANCEL " Overlay Compatible");
                    }
                    if (found_surface->is_overlay_required) {
                        ImGui::TextColored(ui::colors::TEXT_WARNING, "  " ICON_FK_WARNING " Overlay Required");
                    }
                    if (found_surface->no_overlapping_content) {
                        ImGui::TextColored(ui::colors::TEXT_SUCCESS, "  " ICON_FK_OK " No Overlapping Content");
                    } else {
                        ImGui::TextColored(ui::colors::TEXT_DIMMED, "  " ICON_FK_CANCEL " No Overlapping Content");
                    }
                    ImGui::Separator();
                    ImGui::TextColored(ui::colors::TEXT_LABEL, "Flip State (from surface):");
                    ImGui::TextColored(flip_color, "Mode: %s", flip_str);
                    ImGui::EndTooltip();
                }
                ImGui::SameLine();
                ImGui::TextColored(ui::colors::TEXT_DIMMED, " | ");
                ImGui::SameLine();
                ImGui::TextColored(flip_color, "Flip: %s", flip_str);
            } else {
                ImGui::SameLine();
                ImGui::TextColored(ui::colors::TEXT_DIMMED, " | ");
                ImGui::SameLine();
                ImGui::TextColored(ui::colors::TEXT_DIMMED, "Surface: nullptr");
            }
        } else {
            ImGui::SameLine();
            ImGui::TextColored(ui::colors::TEXT_DIMMED, " | ");
            ImGui::SameLine();
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "HWND: nullptr");
        }
    } else {
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "PresentMon: OFF (not enabled by default)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextColored(ui::colors::TEXT_LABEL, "PresentMon: OFF");
            ImGui::Separator();
            ImGui::Text("To enable PresentMon ETW tracing:");
            ImGui::BulletText("Go to the Advanced tab");
            ImGui::BulletText("Enable 'Enable PresentMon ETW Tracing'");
            ImGui::BulletText("PresentMon will start automatically");
            ImGui::Separator();
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "PresentMon provides detailed flip mode");
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "and surface compatibility information.");
            ImGui::EndTooltip();
        }
    }
}

static bool DrawDisplaySettings_VSyncAndTearing_PresentModeLine(VSyncTearingTooltipContext* out_ctx) {
    auto desc_ptr = g_last_swapchain_desc.load();
    if (!desc_ptr) {
        return false;
    }
    const auto& desc = *desc_ptr;
    int current_api = g_last_reshade_device_api.load();
    bool is_d3d9 = current_api == static_cast<int>(reshade::api::device_api::d3d9);
    bool is_dxgi = current_api == static_cast<int>(reshade::api::device_api::d3d10)
                   || current_api == static_cast<int>(reshade::api::device_api::d3d11)
                   || current_api == static_cast<int>(reshade::api::device_api::d3d12);

    ImGui::TextColored(ui::colors::TEXT_LABEL, "Current Present Mode:");
    ImGui::SameLine();
    ImVec4 present_mode_color = ui::colors::TEXT_DIMMED;
    std::string present_mode_name = "Unknown";

    if (is_d3d9) {
        if (desc.present_mode == D3DSWAPEFFECT_FLIPEX) {
            present_mode_name = "FLIPEX (Flip Model)";
            present_mode_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
        } else if (desc.present_mode == D3DSWAPEFFECT_DISCARD) {
            present_mode_name = "DISCARD (Traditional)";
            present_mode_color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
        } else if (desc.present_mode == D3DSWAPEFFECT_FLIP) {
            present_mode_name = "FLIP (Traditional)";
            present_mode_color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
        } else if (desc.present_mode == D3DSWAPEFFECT_COPY) {
            present_mode_name = "COPY (Traditional)";
            present_mode_color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
        } else if (desc.present_mode == D3DSWAPEFFECT_OVERLAY) {
            present_mode_name = "OVERLAY (Traditional)";
            present_mode_color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
        } else {
            present_mode_name = "Unknown";
            present_mode_color = ui::colors::TEXT_ERROR;
        }
        if (desc.fullscreen_state) {
            present_mode_name = present_mode_name + "(FSE)";
        }
        DxgiBypassMode flip_state = GetFlipStateForAPI(current_api);
        const char* flip_state_str = "Unknown";
        switch (flip_state) {
            case DxgiBypassMode::kUnset:                    flip_state_str = "Unset"; break;
            case DxgiBypassMode::kComposed:                 flip_state_str = "Composed"; break;
            case DxgiBypassMode::kOverlay:                  flip_state_str = "MPO iFlip"; break;
            case DxgiBypassMode::kIndependentFlip:          flip_state_str = "iFlip"; break;
            case DxgiBypassMode::kQueryFailedSwapchainNull: flip_state_str = "Query Failed: Null"; break;
            case DxgiBypassMode::kQueryFailedNoMedia:       flip_state_str = "Query Failed: No Media"; break;
            case DxgiBypassMode::kQueryFailedNoSwapchain1:  flip_state_str = "Query Failed: No Swapchain1"; break;
            case DxgiBypassMode::kQueryFailedNoStats:       flip_state_str = "Query Failed: No Stats"; break;
            case DxgiBypassMode::kUnknown:
            default:
                flip_state_str = (desc.present_mode == DXGI_SWAP_EFFECT_FLIP_DISCARD
                                  || desc.present_mode == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL)
                                     ? "Unknown"
                                     : "Unavailable";
                break;
        }
        ImGui::TextColored(present_mode_color, "%s", present_mode_name.c_str());
        if (flip_state != DxgiBypassMode::kQueryFailedNoMedia) {
            ImGui::SameLine();
            ImGui::TextColored(ui::colors::TEXT_DIMMED, " | ");
            ImGui::SameLine();
            ImGui::TextColored(
                flip_state == DxgiBypassMode::kComposed ? ui::colors::FLIP_COMPOSED
                : (flip_state == DxgiBypassMode::kOverlay || flip_state == DxgiBypassMode::kIndependentFlip)
                    ? ui::colors::FLIP_INDEPENDENT
                : (flip_state == DxgiBypassMode::kQueryFailedSwapchainNull
                   || flip_state == DxgiBypassMode::kQueryFailedNoSwapchain1
                   || flip_state == DxgiBypassMode::kQueryFailedNoMedia
                   || flip_state == DxgiBypassMode::kQueryFailedNoStats)
                    ? ui::colors::TEXT_ERROR
                    : ui::colors::FLIP_UNKNOWN,
                "Status: %s", flip_state_str);
        }
        bool status_hovered = ImGui::IsItemHovered();
        static DWORD last_discord_check = 0;
        DWORD current_time = GetTickCount();
        static bool discord_overlay_visible = false;
        if (current_time - last_discord_check > 1000) {
            discord_overlay_visible = display_commander::utils::IsWindowWithTitleVisible(L"Discord Overlay");
            last_discord_check = current_time;
        }
        if (discord_overlay_visible) {
            ImGui::SameLine();
            ui::colors::PushIconColor(ui::colors::ICON_WARNING);
            ImGui::Text(ICON_FK_WARNING " Discord Overlay");
            ui::colors::PopIconColor();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Discord Overlay is visible and may prevent MPO iFlip.\n"
                    "It can prevent Independent Flip mode and increase latency.\n"
                    "Consider disabling it or setting AllowWindowedMode=true in Special-K.");
            }
        }
        DrawDisplaySettings_VSyncAndTearing_PresentMonStatusLine();
        if (out_ctx) {
            out_ctx->desc = desc_ptr.get();
            out_ctx->flip_state = flip_state;
            out_ctx->flip_state_str = flip_state_str;
            out_ctx->present_mode_name = std::move(present_mode_name);
        }
        return status_hovered;
    }

    if (is_dxgi) {
        if (desc.present_mode == DXGI_SWAP_EFFECT_FLIP_DISCARD) {
            present_mode_name = "FLIP_DISCARD (Flip Model)";
            present_mode_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
        } else if (desc.present_mode == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL) {
            present_mode_name = "FLIP_SEQUENTIAL (Flip Model)";
            present_mode_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
        } else if (desc.present_mode == DXGI_SWAP_EFFECT_DISCARD) {
            present_mode_name = "DISCARD (Traditional)";
            present_mode_color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
        } else if (desc.present_mode == DXGI_SWAP_EFFECT_SEQUENTIAL) {
            present_mode_name = "SEQUENTIAL (Traditional)";
            present_mode_color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
        } else {
            present_mode_name = "Unknown";
            present_mode_color = ui::colors::TEXT_ERROR;
        }
        ImGui::TextColored(present_mode_color, "%s", present_mode_name.c_str());
        DxgiBypassMode flip_state = GetFlipStateForAPI(current_api);
        const char* flip_state_str = "Unknown";
        switch (flip_state) {
            case DxgiBypassMode::kUnset:                    flip_state_str = "Unset"; break;
            case DxgiBypassMode::kComposed:                 flip_state_str = "Composed"; break;
            case DxgiBypassMode::kOverlay:                  flip_state_str = "MPO iFlip"; break;
            case DxgiBypassMode::kIndependentFlip:          flip_state_str = "iFlip"; break;
            case DxgiBypassMode::kQueryFailedSwapchainNull: flip_state_str = "Query Failed: Null"; break;
            case DxgiBypassMode::kQueryFailedNoMedia:
                flip_state_str =
                    (GetModuleHandleA("sl.interposer.dll") != nullptr) ? "(not implemented)" : "Query Failed: No Media";
                break;
            case DxgiBypassMode::kQueryFailedNoSwapchain1: flip_state_str = "Query Failed: No Swapchain1"; break;
            case DxgiBypassMode::kQueryFailedNoStats:      flip_state_str = "Query Failed: No Stats"; break;
            case DxgiBypassMode::kUnknown:
            default:
                flip_state_str = (desc.present_mode == DXGI_SWAP_EFFECT_FLIP_DISCARD
                                  || desc.present_mode == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL)
                                     ? "Unknown"
                                     : "Unavailable";
                break;
        }
        bool status_hovered = false;
        if (flip_state != DxgiBypassMode::kQueryFailedNoMedia) {
            ImGui::SameLine();
            ImGui::TextColored(ui::colors::TEXT_DIMMED, " | ");
            ImGui::SameLine();
            ImGui::TextColored(
                flip_state == DxgiBypassMode::kComposed ? ui::colors::FLIP_COMPOSED
                : (flip_state == DxgiBypassMode::kOverlay || flip_state == DxgiBypassMode::kIndependentFlip)
                    ? ui::colors::FLIP_INDEPENDENT
                : (flip_state == DxgiBypassMode::kQueryFailedSwapchainNull
                   || flip_state == DxgiBypassMode::kQueryFailedNoSwapchain1
                   || flip_state == DxgiBypassMode::kQueryFailedNoMedia
                   || flip_state == DxgiBypassMode::kQueryFailedNoStats)
                    ? ui::colors::TEXT_ERROR
                    : ui::colors::FLIP_UNKNOWN,
                "Status: %s", flip_state_str);
            status_hovered = ImGui::IsItemHovered();
        }

        DrawDisplaySettings_VSyncAndTearing_PresentMonStatusLine();

        if (out_ctx) {
            out_ctx->desc = desc_ptr.get();
            out_ctx->flip_state = flip_state;
            out_ctx->flip_state_str = flip_state_str;
            out_ctx->present_mode_name = std::move(present_mode_name);
        }
        return status_hovered;
    }

    // Unsupported API (Vulkan, OpenGL, etc.)
    present_mode_name = "Non-DXGI";
    ImGui::TextColored(present_mode_color, "%s", present_mode_name.c_str());
    DrawDisplaySettings_VSyncAndTearing_PresentMonStatusLine();
    if (out_ctx) {
        out_ctx->desc = desc_ptr.get();
        out_ctx->flip_state = DxgiBypassMode::kUnset;
        out_ctx->flip_state_str = "Unknown";
        out_ctx->present_mode_name = std::move(present_mode_name);
    }
    return false;
}

void DrawDisplaySettings_VSyncAndTearing() {
    DrawDisplaySettings_VSyncAndTearing_FpsSliders();
    ImGui::Spacing();

    g_rendering_ui_section.store("ui:tab:main_new:vsync_tearing", std::memory_order_release);
    if (ImGui::CollapsingHeader("VSync & Tearing", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawDisplaySettings_VSyncAndTearing_Checkboxes();

        VSyncTearingTooltipContext tooltip_ctx;
        bool status_hovered = DrawDisplaySettings_VSyncAndTearing_PresentModeLine(&tooltip_ctx);
        if (status_hovered && tooltip_ctx.desc != nullptr) {
            ImGui::BeginTooltip();
            DrawDisplaySettings_VSyncAndTearing_SwapchainTooltip(tooltip_ctx);
            ImGui::EndTooltip();
        }

        if (!g_last_swapchain_desc.load()) {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "No swapchain information available");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "No game detected or swapchain not yet created.\nThis information will appear once a game is "
                    "running.");
            }
        }
    }
}

void DrawDisplaySettings(reshade::api::effect_runtime* runtime) {
    assert(runtime != nullptr);
    DrawDisplaySettings_DisplayAndTarget();
    DrawDisplaySettings_WindowModeAndApply();
    DrawDisplaySettings_FpsLimiterMode();
    DrawDisplaySettings_FpsAndBackground();
    DrawDisplaySettings_VSyncAndTearing();

    {
        const DLSSGSummary dlss_summary = GetDLSSGSummary();
        // Show DLSS Information section if any DLSS feature was active at least once or any DLSS DLL is loaded
        const bool show_dlss_section = dlss_summary.any_dlss_was_active_once || dlss_summary.any_dlss_dll_loaded;
        g_rendering_ui_section.store("ui:tab:main_new:dlss_info", std::memory_order_release);
        if (show_dlss_section && ImGui::CollapsingHeader("DLSS Information", ImGuiTreeNodeFlags_None)) {
            ImGui::Indent();
            if (!AreNGXParameterVTableHooksInstalled()) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                                   ICON_FK_WARNING " NGX Parameter vtable hooks were never installed.");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "This is usually caused by ReShade loading Display Commander too late (e.g. _nvngx.dll was "
                        "already loaded). "
                        "Recommendation: use Display Commander as dxgi.dll/d3d12.dll/d3d11.dll/version.dll and ReShade "
                        "as Reshade64.dll so our hooks are active before NGX loads. "
                        "Parameter overrides and DLSS preset controls may not apply until then.");
                }
            }
            DrawDLSSInfo(dlss_summary);

            // Button to simulate WM_SIZE to force game to resize and recreate DLSS feature
            {
                HWND hwnd = g_last_swapchain_hwnd.load();
                const bool can_send = (hwnd != nullptr && IsWindow(hwnd));
                if (!can_send) {
                    ImGui::BeginDisabled();
                }
                ui::colors::PushIconColor(ui::colors::ICON_ACTION);
                if (ImGui::Button("Send WM_SIZE (force resize / recreate DLSS)")) {
                    RECT client_rect = {};
                    if (GetClientRect(hwnd, &client_rect)) {
                        const int w = client_rect.right - client_rect.left;
                        const int h = client_rect.bottom - client_rect.top;
                        if (w > 0 && h > 0) {
                            // sleep 100ms then post the full size
                            LogInfo("Posted WM_SIZE w-1,h-1 then will post %dx%d after short delay", w, h);
                            std::thread([hwnd, w, h]() {
                                PostMessage(hwnd, WM_SIZE, SIZE_RESTORED,
                                            MAKELPARAM(static_cast<UINT>(w - 1), static_cast<UINT>(h - 1)));
                                Sleep(100);
                                if (IsWindow(hwnd)) {
                                    PostMessage(hwnd, WM_SIZE, SIZE_RESTORED,
                                                MAKELPARAM(static_cast<UINT>(w), static_cast<UINT>(h)));
                                    LogInfo("Posted WM_SIZE %dx%d to game window", w, h);
                                }
                            }).detach();
                        }
                    }
                }
                ui::colors::PopIconColor();
                if (!can_send) {
                    ImGui::EndDisabled();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Sends two WM_SIZE messages: first with -1,-1, then after a short delay with the current "
                        "client size. Use this to force the game to process a resize and recreate the DLSS feature.");
                }

                // Second button: actually resize window to quarter then back (triggers real WM_SIZE from system)
                if (ImGui::Button("Resize window to quarter then restore")) {
                    RECT window_rect = {};
                    if (GetWindowRect(hwnd, &window_rect)) {
                        const int x = window_rect.left;
                        const int y = window_rect.top;
                        const int ww = window_rect.right - window_rect.left;
                        const int wh = window_rect.bottom - window_rect.top;
                        if (ww > 0 && wh > 0) {
                            std::thread([hwnd, x, y, ww, wh]() {
                                if (!IsWindow(hwnd)) {
                                    return;
                                }
                                SetWindowPos(hwnd, nullptr, x, y, ww - 1, wh - 1, SWP_NOZORDER);
                                Sleep(100);
                                if (IsWindow(hwnd)) {
                                    SetWindowPos(hwnd, nullptr, x, y, ww, wh, SWP_NOZORDER);
                                    LogInfo("Resize window: quarter then restored to %dx%d", ww, wh);
                                }
                            }).detach();
                        }
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Actually resizes the game window to quarter size (half width, half height), waits 150 ms, "
                        "then restores the previous size. The system sends real WM_SIZE messages, which can force "
                        "the game to recreate the swap chain and DLSS feature.");
                }
            }

            {
                // DLSS internal resolution scale: 0 = no override, (0,1] = scale render size (OutWidth/OutHeight =
                // Width/Height * scale)
                // Apply only on release (IsItemDeactivatedAfterEdit) so dragging the slider doesn't apply
                // immediately and close the UI. Use static so we don't re-init from GetValue() each frame.
                static float s_dlss_scale_ui = -1.f;
                if (s_dlss_scale_ui < 0.f) {
                    s_dlss_scale_ui = settings::g_swapchainTabSettings.dlss_internal_resolution_scale.GetValue();
                }
                ImGui::SetNextItemWidth(120.0f);
                ImGui::SliderFloat("Internal resolution scale (WIP Experimental)", &s_dlss_scale_ui, 0.0f, 1.0f,
                                   "%.2f");
                if (!ImGui::IsItemActive() && !ImGui::IsItemDeactivatedAfterEdit()) {
                    s_dlss_scale_ui = settings::g_swapchainTabSettings.dlss_internal_resolution_scale.GetValue();
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    settings::g_swapchainTabSettings.dlss_internal_resolution_scale.SetValue(s_dlss_scale_ui);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Scale DLSS internal render resolution. 0 = no override. e.g. 0.5 = half width/height "
                        "(OutWidth = "
                        "Width * 0.5, OutHeight = Height * 0.5).");
                }
            }

            // DLSS Quality Preset override (Performance, Balanced, Quality, etc. - not render preset A/B/C) - shown
            // even without experimental so users can see and override the preset
            {
                static const char* dlss_quality_preset_items[] = {
                    "Game Default", "Performance", "Balanced", "Quality", "Ultra Performance", "Ultra Quality", "DLAA"};
                std::string current_quality = settings::g_swapchainTabSettings.dlss_quality_preset_override.GetValue();
                int current_quality_index = 0;
                for (int i = 0; i < 7; ++i) {
                    if (current_quality == dlss_quality_preset_items[i]) {
                        current_quality_index = i;
                        break;
                    }
                }
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::Combo("DLSS Quality Preset Override", &current_quality_index, dlss_quality_preset_items,
                                 7)) {
                    settings::g_swapchainTabSettings.dlss_quality_preset_override.SetValue(
                        dlss_quality_preset_items[current_quality_index]);
                    ResetNGXPresetInitialization();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Override DLSS quality preset (Performance, Balanced, Quality, etc.). Game Default = no "
                        "override. This is the quality mode, not the render preset (A, B, C).");
                }
            }

            // DLSS override: per-DLL checkbox + subfolder selector (Display Commander\dlss_override\<subfolder>)
            bool dlss_override_enabled = settings::g_streamlineTabSettings.dlss_override_enabled.GetValue();
            if (ImGui::Checkbox("Use DLSS override", &dlss_override_enabled)) {
                settings::g_streamlineTabSettings.dlss_override_enabled.SetValue(dlss_override_enabled);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Load DLSS DLLs from Display Commander\\dlss_override subfolders. Each DLL has its own checkbox "
                    "and subfolder.");
            }
            if (dlss_override_enabled) {
                std::vector<std::string> subfolders = GetDlssOverrideSubfolderNames();
                auto draw_dll_row = [&subfolders](const char* label, bool* p_check,
                                                  ui::new_ui::StringSetting& subfolder_setting, int dll_index) {
                    ImGui::Checkbox(label, p_check);
                    std::string current_sub = subfolder_setting.GetValue();
                    int current_index = -1;
                    if (!current_sub.empty()) {
                        for (size_t i = 0; i < subfolders.size(); ++i) {
                            if (subfolders[i] == current_sub) {
                                current_index = static_cast<int>(i);
                                break;
                            }
                        }
                    }
                    const char* combo_label = (current_index >= 0)
                                                  ? subfolders[static_cast<size_t>(current_index)].c_str()
                                              : current_sub.empty() ? "(root folder)"
                                                                    : current_sub.c_str();
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(140.0f);
                    if (ImGui::BeginCombo((std::string("##dlss_sub_") + std::to_string(dll_index)).c_str(),
                                          combo_label)) {
                        if (ImGui::Selectable("(root folder)", current_sub.empty())) {
                            subfolder_setting.SetValue("");
                        }
                        for (size_t i = 0; i < subfolders.size(); ++i) {
                            bool selected = (current_index == static_cast<int>(i));
                            if (ImGui::Selectable(subfolders[i].c_str(), selected)) {
                                subfolder_setting.SetValue(subfolders[i]);
                            }
                        }
                        ImGui::EndCombo();
                    }
                    {
                        std::string effective_folder = GetEffectiveDefaultDlssOverrideFolder(current_sub).string();
                        DlssOverrideDllStatus st = GetDlssOverrideFolderDllStatus(effective_folder, (dll_index == 0),
                                                                                  (dll_index == 1), (dll_index == 2));
                        if (st.dlls.size() > static_cast<size_t>(dll_index)) {
                            const DlssOverrideDllEntry& e = st.dlls[dll_index];
                            ImGui::SameLine();
                            if (e.present) {
                                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s", e.version.c_str());
                            } else {
                                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.0f, 1.0f), "Missing");
                            }
                        }
                    }
                };
                bool dlss_on = settings::g_streamlineTabSettings.dlss_override_dlss.GetValue();
                bool dlss_fg_on = settings::g_streamlineTabSettings.dlss_override_dlss_fg.GetValue();
                bool dlss_rr_on = settings::g_streamlineTabSettings.dlss_override_dlss_rr.GetValue();
                draw_dll_row("nvngx_dlss.dll (DLSS)##main", &dlss_on,
                             settings::g_streamlineTabSettings.dlss_override_subfolder, 0);
                if (dlss_on != settings::g_streamlineTabSettings.dlss_override_dlss.GetValue()) {
                    settings::g_streamlineTabSettings.dlss_override_dlss.SetValue(dlss_on);
                }
                draw_dll_row("nvngx_dlssd.dll (D = denoiser / RR)##main", &dlss_rr_on,
                             settings::g_streamlineTabSettings.dlss_override_subfolder_dlssd, 1);
                if (dlss_rr_on != settings::g_streamlineTabSettings.dlss_override_dlss_rr.GetValue()) {
                    settings::g_streamlineTabSettings.dlss_override_dlss_rr.SetValue(dlss_rr_on);
                }
                draw_dll_row("nvngx_dlssg.dll (G = generation / FG)##main", &dlss_fg_on,
                             settings::g_streamlineTabSettings.dlss_override_subfolder_dlssg, 2);
                if (dlss_fg_on != settings::g_streamlineTabSettings.dlss_override_dlss_fg.GetValue()) {
                    settings::g_streamlineTabSettings.dlss_override_dlss_fg.SetValue(dlss_fg_on);
                }
                // Add Folder
                static char dlss_add_folder_buf[128] = "";
                ImGui::SetNextItemWidth(120.0f);
                ImGui::InputTextWithHint("##dlss_add_folder", "e.g. 310.5.2", dlss_add_folder_buf,
                                         sizeof(dlss_add_folder_buf));
                ImGui::SameLine();
                if (ImGui::Button("Add Folder")) {
                    std::string name(dlss_add_folder_buf);
                    std::string err;
                    if (CreateDlssOverrideSubfolder(name, &err)) {
                        dlss_add_folder_buf[0] = '\0';
                    } else if (!err.empty()) {
                        LogError("DLSS override Add Folder: %s", err.c_str());
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Create subfolder under Display Commander\\dlss_override.");
                }
            }
            ImGui::SameLine();
            ui::colors::PushIconColor(ui::colors::ICON_ACTION);
            if (ImGui::Button(ICON_FK_FOLDER_OPEN " Open DLSS override folder")) {
                std::string folder_to_open = GetDefaultDlssOverrideFolder().string();
                std::thread([folder_to_open]() {
                    std::error_code ec;
                    std::filesystem::create_directories(folder_to_open, ec);
                    if (ec) {
                        LogError("Failed to create DLSS override folder: %s (%s)", folder_to_open.c_str(),
                                 ec.message().c_str());
                        return;
                    }
                    HINSTANCE result =
                        ShellExecuteA(nullptr, "explore", folder_to_open.c_str(), nullptr, nullptr, SW_SHOW);
                    if (reinterpret_cast<intptr_t>(result) <= 32) {
                        LogError("Failed to open DLSS override folder: %s (Error: %ld)", folder_to_open.c_str(),
                                 reinterpret_cast<intptr_t>(result));
                    }
                }).detach();
            }
            ui::colors::PopIconColor();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Open the folder where you can place custom DLSS DLLs (created if missing).");
            }

            ImGui::Unindent();
        }
    }
}

// Returns a short label for an audio channel (L, R, C, LFE, etc.) for display in per-channel volume/VU UI.
static const char* GetAudioChannelLabel(unsigned int channel_index, unsigned int channel_count) {
    static const char* stereo[] = {"L", "R"};
    static const char* five_one[] = {"L", "R", "C", "LFE", "RL", "RR"};
    static const char* seven_one[] = {"L", "R", "C", "LFE", "RL", "RR", "SL", "SR"};
    static char generic_buf[16];
    if (channel_count == 1 && channel_index == 0) return "M";
    if (channel_count == 2 && channel_index < 2) return stereo[channel_index];
    if (channel_count == 6 && channel_index < 6) return five_one[channel_index];
    if (channel_count == 8 && channel_index < 8) return seven_one[channel_index];
    (void)std::snprintf(generic_buf, sizeof(generic_buf), "Ch%u", channel_index);
    return generic_buf;
}

void DrawOverlayVUBars(bool show_tooltips) {
    unsigned int meter_count = 0;
    if (!::GetAudioMeterChannelCount(&meter_count) || meter_count == 0) {
        return;
    }
    static std::vector<float> s_overlay_vu_peaks;
    static std::vector<float> s_overlay_vu_smoothed;
    if (s_overlay_vu_peaks.size() < meter_count) {
        s_overlay_vu_peaks.resize(meter_count);
        s_overlay_vu_smoothed.resize(meter_count, 0.0f);
    }
    unsigned int effective_meter_count = meter_count;
    if (::GetAudioMeterPeakValues(meter_count, s_overlay_vu_peaks.data())) {
        // use meter_count as-is
    } else if (meter_count > 6 && ::GetAudioMeterPeakValues(6, s_overlay_vu_peaks.data())) {
        effective_meter_count = 6;
    } else if (meter_count > 2 && ::GetAudioMeterPeakValues(2, s_overlay_vu_peaks.data())) {
        effective_meter_count = 2;
    } else {
        return;
    }
    const float decay = 0.85f;
    for (unsigned int i = 0; i < effective_meter_count; ++i) {
        float p = s_overlay_vu_peaks[i];
        float s = s_overlay_vu_smoothed[i];
        s_overlay_vu_smoothed[i] = (p > s) ? p : (s * decay);
    }
    const float bar_height = 48.0f;
    const float bar_width = 10.0f;
    const float gap = 3.0f;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float total_width = (static_cast<float>(effective_meter_count) * (bar_width + gap)) - gap;
    for (unsigned int i = 0; i < effective_meter_count; ++i) {
        const float level = (std::min)(1.0f, s_overlay_vu_smoothed[i]);
        const float x = cursor.x + (static_cast<float>(i) * (bar_width + gap));
        const ImVec2 bg_min(x, cursor.y);
        const ImVec2 bg_max(x + bar_width, cursor.y + bar_height);
        const float fill_h = level * bar_height;
        const ImVec2 fill_min(x, cursor.y + bar_height - fill_h);
        const ImVec2 fill_max(x + bar_width, cursor.y + bar_height);
        draw_list->AddRectFilled(bg_min, bg_max, IM_COL32(35, 35, 35, 255));
        draw_list->AddRect(bg_min, bg_max, IM_COL32(60, 60, 60, 255), 0.0f, 0, 1.0f);
        draw_list->AddRectFilled(fill_min, fill_max, IM_COL32(80, 180, 80, 255));
    }
    ImGui::Dummy(ImVec2(total_width, bar_height));
    const float label_y = cursor.y + bar_height + 2.0f;
    const float line_height = ImGui::GetTextLineHeightWithSpacing();
    for (unsigned int i = 0; i < effective_meter_count; ++i) {
        const char* ch_label = GetAudioChannelLabel(i, effective_meter_count);
        const float level = (std::min)(1.0f, s_overlay_vu_smoothed[i]);
        char raw_buf[32];
        (void)std::snprintf(raw_buf, sizeof(raw_buf), "%s %.1f%%", ch_label, level * 100.0f);
        const float bar_center_x = cursor.x + (static_cast<float>(i) * (bar_width + gap)) + (bar_width * 0.5f);
        const float text_w = ImGui::CalcTextSize(raw_buf).x;
        ImGui::SetCursorScreenPos(ImVec2(bar_center_x - (text_w * 0.5f), label_y));
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "%s", raw_buf);
    }
    if (show_tooltips && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Per-channel peak level (default output device).");
    }
    ImGui::SetCursorScreenPos(ImVec2(cursor.x, label_y + line_height));
    ImGui::Dummy(ImVec2(total_width, line_height));
}

void DrawAudioSettings() {
    g_rendering_ui_section.store("ui:tab:main_new:audio:entry", std::memory_order_release);
    // Default output device format info (channel config, Hz, bits, format, extension, device name)
    g_rendering_ui_section.store("ui:tab:main_new:audio:device_info", std::memory_order_release);
    AudioDeviceFormatInfo device_info;
    if (GetDefaultAudioDeviceFormatInfo(&device_info)
        && (device_info.channel_count > 0 || device_info.sample_rate_hz > 0)) {
        const char* ext_str =
            device_info.format_extension_utf8.empty() ? "—" : device_info.format_extension_utf8.c_str();
        const char* name_str =
            device_info.device_friendly_name_utf8.empty() ? nullptr : device_info.device_friendly_name_utf8.c_str();
        if (name_str && *name_str) {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Device: %s", name_str);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Default render endpoint. Extension/codec (Dolby, DTS, PCM, etc.) shown on next line.\n\nRaw: %s",
                    device_info.raw_format_utf8.empty() ? "(none)" : device_info.raw_format_utf8.c_str());
            }
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Format: %s, %u Hz, %u-bit, extension: %s",
                               device_info.channel_config_utf8.empty() ? "—" : device_info.channel_config_utf8.c_str(),
                               device_info.sample_rate_hz, device_info.bits_per_sample, ext_str);
        } else {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Device: %s, %u Hz, %u-bit, extension: %s",
                               device_info.channel_config_utf8.empty() ? "—" : device_info.channel_config_utf8.c_str(),
                               device_info.sample_rate_hz, device_info.bits_per_sample, ext_str);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Source: Default output device mix format from WASAPI (IAudioClient::GetMixFormat).\n"
                "Extension: stream/codec type (e.g. PCM, Float, Dolby AC3, DTS). Device name shows endpoint (e.g. "
                "Dolby Atmos).\n\n"
                "Raw: %s",
                device_info.raw_format_utf8.empty() ? "(none)" : device_info.raw_format_utf8.c_str());
        }
        ImGui::Spacing();
    }

    g_rendering_ui_section.store("ui:tab:main_new:audio:game_volume", std::memory_order_release);
    // Audio Volume slider
    float volume = s_audio_volume_percent.load();
    if (ImGui::SliderFloat("Game Volume (%)", &volume, 0.0f, 100.0f, "%.0f%%")) {
        s_audio_volume_percent.store(volume);

        // Apply immediately only if Auto-apply is enabled
        if (settings::g_mainTabSettings.audio_volume_auto_apply.GetValue()) {
            if (::SetVolumeForCurrentProcess(volume)) {
                std::ostringstream oss;
                oss << "Game volume changed to " << static_cast<int>(volume) << "%";
                LogInfo(oss.str().c_str());
            } else {
                std::ostringstream oss;
                oss << "Failed to set game volume to " << static_cast<int>(volume) << "%";
                LogWarn(oss.str().c_str());
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Game audio volume control (0-100%%). When at 100%%, volume adjustments will affect system volume "
            "instead.");
    }
    // Auto-apply checkbox next to Audio Volume
    ImGui::SameLine();
    if (CheckboxSetting(settings::g_mainTabSettings.audio_volume_auto_apply, "Auto-apply##audio_volume")) {
        // No immediate action required; stored for consistency with other UI
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Auto-apply volume changes when adjusting the slider.");
    }

    g_rendering_ui_section.store("ui:tab:main_new:audio:system_volume", std::memory_order_release);
    // System Volume slider (controls system master volume directly)
    float system_volume = 0.0f;
    if (::GetSystemVolume(&system_volume)) {
        s_system_volume_percent.store(system_volume);
    } else {
        system_volume = s_system_volume_percent.load();
    }
    if (ImGui::SliderFloat("System Volume (%)", &system_volume, 0.0f, 100.0f, "%.0f%%")) {
        s_system_volume_percent.store(system_volume);
        if (!::SetSystemVolume(system_volume)) {
            std::ostringstream oss;
            oss << "Failed to set system volume to " << static_cast<int>(system_volume) << "%";
            LogWarn(oss.str().c_str());
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "System master volume control (0-100%%). This adjusts the Windows system volume for the default output "
            "device.\n"
            "Note: System volume may also be adjusted automatically when game volume is at 100%% and you increase it.");
    }

    g_rendering_ui_section.store("ui:tab:main_new:audio:mute", std::memory_order_release);
    // Audio Mute checkbox
    bool audio_mute = s_audio_mute.load();
    if (ImGui::Checkbox("Mute", &audio_mute)) {
        settings::g_mainTabSettings.audio_mute.SetValue(audio_mute);

        // Apply mute/unmute immediately
        if (::SetMuteForCurrentProcess(audio_mute)) {
            ::g_muted_applied.store(audio_mute);
            std::ostringstream oss;
            oss << "Audio " << (audio_mute ? "muted" : "unmuted") << " successfully";
            LogInfo(oss.str().c_str());
        } else {
            std::ostringstream oss;
            oss << "Failed to " << (audio_mute ? "mute" : "unmute") << " audio";
            LogWarn(oss.str().c_str());
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Manually mute/unmute audio.");
    }

    g_rendering_ui_section.store("ui:tab:main_new:audio:vu_peaks", std::memory_order_release);
    // Fetch per-channel VU peak data once (default render endpoint); reused for per-channel bars and VU strip below.
    // Some endpoints (e.g. Dolby Atmos PCM 7.1) report 8 channels but GetChannelsPeakValues(8) fails; fallback to 6
    // or 2.
    static std::vector<float> s_vu_peaks;
    static std::vector<float> s_vu_smoothed;
    unsigned int meter_count = 0;
    unsigned int effective_meter_count = 0;
    if (::GetAudioMeterChannelCount(&meter_count) && meter_count > 0) {
        effective_meter_count = meter_count;
        if (s_vu_peaks.size() < meter_count) {
            s_vu_peaks.resize(meter_count);
            s_vu_smoothed.resize(meter_count, 0.0f);
        }
        if (::GetAudioMeterPeakValues(meter_count, s_vu_peaks.data())) {
            const float decay = 0.85f;
            for (unsigned int i = 0; i < meter_count; ++i) {
                float p = s_vu_peaks[i];
                float s = s_vu_smoothed[i];
                s_vu_smoothed[i] = (p > s) ? p : (s * decay);
            }
        } else if (meter_count > 6 && ::GetAudioMeterPeakValues(6, s_vu_peaks.data())) {
            effective_meter_count = 6;
            const float decay = 0.85f;
            for (unsigned int i = 0; i < 6; ++i) {
                float p = s_vu_peaks[i];
                float s = s_vu_smoothed[i];
                s_vu_smoothed[i] = (p > s) ? p : (s * decay);
            }
        } else if (meter_count > 2 && ::GetAudioMeterPeakValues(2, s_vu_peaks.data())) {
            effective_meter_count = 2;
            const float decay = 0.85f;
            for (unsigned int i = 0; i < 2; ++i) {
                float p = s_vu_peaks[i];
                float s = s_vu_smoothed[i];
                s_vu_smoothed[i] = (p > s) ? p : (s * decay);
            }
        }
    }

    if (!g_using_wine.load(std::memory_order_acquire)) {
        g_rendering_ui_section.store("ui:tab:main_new:audio:per_channel_volume", std::memory_order_release);
        // Per-channel volume (when session supports IChannelAudioVolume; L/R for stereo, L/R/C/LFE/RL/RR for 5.1, etc.)
        // Each channel row shows: small VU bar (when available) + volume slider.
        unsigned int channel_count = 0;
        const bool have_channel_volume = ::GetChannelVolumeCountForCurrentProcess(&channel_count) && channel_count >= 1;
        if (have_channel_volume) {
            std::vector<float> channel_vols;
            if (::GetAllChannelVolumesForCurrentProcess(&channel_vols) && channel_vols.size() == channel_count) {
                if (ImGui::TreeNodeEx("Per-channel volume", ImGuiTreeNodeFlags_DefaultOpen)) {
                    const float row_vu_width = 14.0f;
                    const float row_vu_height = 32.0f;
                    for (unsigned int ch = 0; ch < channel_count; ++ch) {
                        float pct = channel_vols[ch] * 100.0f;
                        const char* label = GetAudioChannelLabel(ch, channel_count);
                        // Per-channel VU bar (graphical) + raw value next to the slider when we have meter data
                        if (ch < effective_meter_count && ch < s_vu_smoothed.size()) {
                            const float level = (std::min)(1.0f, s_vu_smoothed[ch]);
                            ImDrawList* draw_list = ImGui::GetWindowDrawList();
                            const ImVec2 pos = ImGui::GetCursorScreenPos();
                            const ImVec2 bg_min(pos.x, pos.y);
                            const ImVec2 bg_max(pos.x + row_vu_width, pos.y + row_vu_height);
                            const float fill_h = level * row_vu_height;
                            const ImVec2 fill_min(pos.x, pos.y + row_vu_height - fill_h);
                            const ImVec2 fill_max(pos.x + row_vu_width, pos.y + row_vu_height);
                            draw_list->AddRectFilled(bg_min, bg_max, IM_COL32(40, 40, 40, 255));
                            draw_list->AddRectFilled(fill_min, fill_max, IM_COL32(80, 180, 80, 255));
                            ImGui::Dummy(ImVec2(row_vu_width + 4.0f, row_vu_height));
                            ImGui::SameLine(0.0f, 0.0f);
                            ImGui::TextColored(ui::colors::TEXT_DIMMED, "%.1f%%", level * 100.0f);
                            ImGui::SameLine(0.0f, 6.0f);
                        }
                        char slider_id[32];
                        (void)std::snprintf(slider_id, sizeof(slider_id), "%s (%%)##ch%u", label, ch);
                        if (ImGui::SliderFloat(slider_id, &pct, 0.0f, 100.0f, "%.0f%%")) {
                            if (::SetChannelVolumeForCurrentProcess(ch, pct / 100.0f)) {
                                LogInfo("Channel %u volume set", ch);
                            }
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Volume for channel %u (%s), game audio session.", ch, label);
                        }
                    }
                    ImGui::TreePop();
                }
            }
        } else if (device_info.channel_count >= 6) {
            ImGui::TextColored(ui::colors::TEXT_DIMMED,
                               "Per-channel volume is not available for this output (e.g. Dolby Atmos PCM 7.1). "
                               "Switch Windows sound output to PCM 5.1 or Stereo for per-channel control.");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "IChannelAudioVolume is not exposed by the game audio session on some outputs (e.g. Dolby Atmos).");
            }
        }
    }

    g_rendering_ui_section.store("ui:tab:main_new:audio:vu_strip", std::memory_order_release);
    // Per-channel VU meter strip: graphical representation with labels (default render endpoint; mixed output)
    if (effective_meter_count > 0 && effective_meter_count <= s_vu_smoothed.size()) {
        const float bar_height = 288.0f;
        const float bar_width = 72.0f;
        const float gap = 24.0f;
        const float label_height = ImGui::GetTextLineHeight();
        ImGui::Spacing();
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "Level (output)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Per-channel peak level (default output device, mixed).");
        }
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const float total_width = (static_cast<float>(effective_meter_count) * (bar_width + gap)) - gap;
        for (unsigned int i = 0; i < effective_meter_count; ++i) {
            const float level = (std::min)(1.0f, s_vu_smoothed[i]);
            const float x = cursor.x + (static_cast<float>(i) * (bar_width + gap));
            const ImVec2 bg_min(x, cursor.y);
            const ImVec2 bg_max(x + bar_width, cursor.y + bar_height);
            const float fill_h = level * bar_height;
            const ImVec2 fill_min(x, cursor.y + bar_height - fill_h);
            const ImVec2 fill_max(x + bar_width, cursor.y + bar_height);
            draw_list->AddRectFilled(bg_min, bg_max, IM_COL32(35, 35, 35, 255));
            draw_list->AddRect(bg_min, bg_max, IM_COL32(60, 60, 60, 255), 0.0f, 0, 1.0f);
            draw_list->AddRectFilled(fill_min, fill_max, IM_COL32(80, 180, 80, 255));
        }
        ImGui::Dummy(ImVec2(total_width, bar_height));
        // Channel labels and raw values centered under each bar
        const float label_y = cursor.y + bar_height + 2.0f;
        const float line_height = ImGui::GetTextLineHeightWithSpacing();
        for (unsigned int i = 0; i < effective_meter_count; ++i) {
            const char* ch_label = GetAudioChannelLabel(i, effective_meter_count);
            const float bar_center_x = cursor.x + (static_cast<float>(i) * (bar_width + gap)) + (bar_width * 0.5f);
            const float label_w = ImGui::CalcTextSize(ch_label).x;
            ImGui::SetCursorScreenPos(ImVec2(bar_center_x - (label_w * 0.5f), label_y));
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "%s", ch_label);
            const float level = (std::min)(1.0f, s_vu_smoothed[i]);
            char raw_buf[32];
            (void)std::snprintf(raw_buf, sizeof(raw_buf), "%.1f%%", level * 100.0f);
            const float raw_w = ImGui::CalcTextSize(raw_buf).x;
            ImGui::SetCursorScreenPos(ImVec2(bar_center_x - (raw_w * 0.5f), label_y + label_height + 2.0f));
            ImGui::TextColored(ui::colors::TEXT_SUBTLE, "%s", raw_buf);
        }
        ImGui::SetCursorScreenPos(ImVec2(cursor.x, label_y + label_height + 2.0f + line_height));
        ImGui::Dummy(ImVec2(total_width, label_height + 2.0f + line_height));
    }
    g_rendering_ui_section.store("ui:tab:main_new:audio:mute_in_bg", std::memory_order_release);
    // Mute in Background checkbox (disabled if Mute is ON)
    bool mute_in_bg = s_mute_in_background.load();
    if (s_audio_mute.load()) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Checkbox("Mute In Background", &mute_in_bg)) {
        settings::g_mainTabSettings.mute_in_background.SetValue(mute_in_bg);
        settings::g_mainTabSettings.mute_in_background_if_other_audio.SetValue(false);

        // Reset applied flag so the monitor thread reapplies desired state
        ::g_muted_applied.store(false);
        // Also apply the current mute state immediately if manual mute is off
        if (!s_audio_mute.load()) {
            HWND hwnd = g_last_swapchain_hwnd.load();
            // Use actual focus state for background audio (not spoofed)
            bool want_mute = (mute_in_bg && hwnd != nullptr && GetForegroundWindow() != hwnd);
            if (::SetMuteForCurrentProcess(want_mute)) {
                ::g_muted_applied.store(want_mute);
                std::ostringstream oss;
                oss << "Background mute " << (mute_in_bg ? "enabled" : "disabled");
                LogInfo(oss.str().c_str());
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Mute the game's audio when it is not the foreground window.");
    }
    if (s_audio_mute.load()) {
        ImGui::EndDisabled();
    }
    g_rendering_ui_section.store("ui:tab:main_new:audio:mute_in_bg_if_other", std::memory_order_release);
    // Mute in Background only if other app plays audio
    bool mute_in_bg_if_other = s_mute_in_background_if_other_audio.load();
    if (s_audio_mute.load()) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Checkbox("Mute In Background (only if other app has audio)", &mute_in_bg_if_other)) {
        settings::g_mainTabSettings.mute_in_background_if_other_audio.SetValue(mute_in_bg_if_other);
        settings::g_mainTabSettings.mute_in_background.SetValue(false);
        ::g_muted_applied.store(false);
        if (!s_audio_mute.load()) {
            HWND hwnd = g_last_swapchain_hwnd.load();
            bool is_background = (hwnd != nullptr && GetForegroundWindow() != hwnd);
            bool want_mute = (mute_in_bg_if_other && is_background && ::IsOtherAppPlayingAudio());
            if (::SetMuteForCurrentProcess(want_mute)) {
                ::g_muted_applied.store(want_mute);
                std::ostringstream oss;
                oss << "Background mute (if other audio) " << (mute_in_bg_if_other ? "enabled" : "disabled");
                LogInfo(oss.str().c_str());
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Mute only if app is background AND another app outputs audio.");
    }
    if (s_audio_mute.load()) {
        ImGui::EndDisabled();
    }

    ImGui::Separator();

    g_rendering_ui_section.store("ui:tab:main_new:audio:output_device", std::memory_order_release);
    // Audio output device selector (per-application routing)
    ImGui::Text("Output Device");

    static std::vector<std::string> s_audio_device_names;
    static std::vector<std::wstring> s_audio_device_ids;
    static int s_selected_audio_device_index = 0;  // 0 = System Default, 1..N = devices
    static bool s_audio_devices_initialized = false;

    // Refresh device list on first use or when user clicks refresh
    auto refresh_audio_devices = []() {
        s_audio_device_names.clear();
        s_audio_device_ids.clear();
        s_selected_audio_device_index = 0;

        std::wstring current_device_id;
        if (GetAudioOutputDevices(s_audio_device_names, s_audio_device_ids, current_device_id)) {
            // Determine current selection: empty = system default, otherwise match by short device id
            if (current_device_id.empty()) {
                s_selected_audio_device_index = 0;
            } else {
                int matched = 0;
                for (size_t i = 0; i < s_audio_device_ids.size(); ++i) {
                    if (s_audio_device_ids[i] == current_device_id) {
                        matched = static_cast<int>(i) + 1;
                        break;
                    }
                }
                s_selected_audio_device_index = matched;
            }
        }
    };

    if (!s_audio_devices_initialized) {
        refresh_audio_devices();
        s_audio_devices_initialized = true;
    }

    const char* current_label = "System Default";
    if (s_selected_audio_device_index > 0
        && static_cast<size_t>(s_selected_audio_device_index - 1) < s_audio_device_names.size()) {
        current_label = s_audio_device_names[s_selected_audio_device_index - 1].c_str();
    }

    g_rendering_ui_section.store("ui:tab:main_new:audio:output_device_combo", std::memory_order_release);
    if (ImGui::BeginCombo("##AudioOutputDevice", current_label)) {
        bool selection_changed = false;

        // Option 0: System Default (no per-app override)
        bool selected_default = (s_selected_audio_device_index == 0);
        if (ImGui::Selectable("System Default (use Windows setting)", selected_default)) {
            if (SetAudioOutputDeviceForCurrentProcess(L"")) {
                s_selected_audio_device_index = 0;
                selection_changed = true;
            }
        }
        if (selected_default) {
            ImGui::SetItemDefaultFocus();
        }

        // Actual devices
        for (int i = 0; i < static_cast<int>(s_audio_device_names.size()); ++i) {
            bool selected = (s_selected_audio_device_index == i + 1);
            if (ImGui::Selectable(s_audio_device_names[i].c_str(), selected)) {
                if (i >= 0 && static_cast<size_t>(i) < s_audio_device_ids.size()) {
                    if (SetAudioOutputDeviceForCurrentProcess(s_audio_device_ids[i])) {
                        s_selected_audio_device_index = i + 1;
                        selection_changed = true;
                    }
                }
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::EndCombo();

        if (selection_changed) {
            // No additional state to sync; Windows persists per-process routing itself.
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Select which audio output device this game should use.\n"
            "Uses Windows per-application audio routing (similar to 'App volume and device preferences').");
    }

    g_rendering_ui_section.store("ui:tab:main_new:audio:refresh_devices", std::memory_order_release);
    ImGui::SameLine();
    if (ImGui::Button("Refresh Devices")) {
        refresh_audio_devices();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Re-scan active audio output devices (use after plugging/unplugging audio hardware).");
    }
}

void DrawWindowControls() {
    HWND hwnd = g_last_swapchain_hwnd.load();
    if (hwnd == nullptr) {
        LogWarn("Maximize Window: no window handle available");
        return;
    }
    // Window Control Buttons (Minimize, Restore, and Maximize side by side)
    ImGui::BeginGroup();

    // Minimize Window Button
    ui::colors::PushIconColor(ui::colors::ICON_ACTION);
    if (ImGui::Button(ICON_FK_MINUS " Minimize Window")) {
        HWND hwnd = g_last_swapchain_hwnd.load();
        std::thread([hwnd]() {
            LogDebug("Minimize Window button pressed (bg thread)");
            ShowWindow(hwnd, SW_MINIMIZE);
        }).detach();
    }
    ui::colors::PopIconColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Minimize the current game window.");
    }

    ImGui::SameLine();

    // Open Game Folder Button
    ui::colors::PushIconColor(ui::colors::ICON_ACTION);
    if (ImGui::Button(ICON_FK_FOLDER_OPEN " Open Game Folder")) {
        std::thread([]() {
            LogDebug("Open Game Folder button pressed (bg thread)");

            // Get current process executable path
            char process_path[MAX_PATH];
            DWORD path_length = GetModuleFileNameA(nullptr, process_path, MAX_PATH);

            if (path_length == 0) {
                LogError("Failed to get current process path for folder opening");
                return;
            }

            // Get the parent directory of the executable
            std::string full_path(process_path);
            size_t last_slash = full_path.find_last_of("\\/");

            if (last_slash == std::string::npos) {
                LogError("Invalid process path format: %s", full_path.c_str());
                return;
            }

            std::string game_folder = full_path.substr(0, last_slash);
            LogInfo("Opening game folder: %s", game_folder.c_str());

            // Open the folder in Windows Explorer
            HINSTANCE result = ShellExecuteA(nullptr, "explore", game_folder.c_str(), nullptr, nullptr, SW_SHOW);

            if (reinterpret_cast<intptr_t>(result) <= 32) {
                LogError("Failed to open game folder: %s (Error: %ld)", game_folder.c_str(),
                         reinterpret_cast<intptr_t>(result));
            } else {
                LogInfo("Successfully opened game folder: %s", game_folder.c_str());
            }
        }).detach();
    }
    ui::colors::PopIconColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Open the game's installation folder in Windows Explorer.");
    }

    ImGui::SameLine();

    // Restore Window Button
    ui::colors::PushIconColor(ui::colors::ICON_ACTION);
    if (ImGui::Button(ICON_FK_UNDO " Restore Window")) {
        std::thread([hwnd]() {
            LogDebug("Restore Window button pressed (bg thread)");
            ShowWindow(hwnd, SW_RESTORE);
            display_commanderhooks::SendFakeActivationMessages(hwnd);
        }).detach();
    }
    ui::colors::PopIconColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Restore the minimized game window.");
    }

    ImGui::SameLine();

    // Open DisplayCommander.log Button
    ui::colors::PushIconColor(ui::colors::ICON_ACTION);
    if (ImGui::Button(ICON_FK_FILE " DisplayCommander.log")) {
        std::thread([]() {
            LogDebug("Open DisplayCommander.log button pressed (bg thread)");

            // Get current process executable path
            char process_path[MAX_PATH];
            DWORD path_length = GetModuleFileNameA(nullptr, process_path, MAX_PATH);

            if (path_length == 0) {
                LogError("Failed to get current process path for log file opening");
                return;
            }

            // Get the parent directory of the executable
            std::string full_path(process_path);
            size_t last_slash = full_path.find_last_of("\\/");

            if (last_slash == std::string::npos) {
                LogError("Invalid process path format: %s", full_path.c_str());
                return;
            }

            std::string log_path = full_path.substr(0, last_slash) + "\\DisplayCommander.log";
            LogInfo("Opening DisplayCommander.log: %s", log_path.c_str());

            // Open the log file with default text editor
            HINSTANCE result = ShellExecuteA(nullptr, "open", log_path.c_str(), nullptr, nullptr, SW_SHOW);

            if (reinterpret_cast<intptr_t>(result) <= 32) {
                LogError("Failed to open DisplayCommander.log: %s (Error: %ld)", log_path.c_str(),
                         reinterpret_cast<intptr_t>(result));
            } else {
                LogInfo("Successfully opened DisplayCommander.log: %s", log_path.c_str());
            }
        }).detach();
    }
    ui::colors::PopIconColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Open DisplayCommander.log in the default text editor.");
    }

    ImGui::SameLine();

    // Open reshade.log Button
    ui::colors::PushIconColor(ui::colors::ICON_ACTION);
    if (ImGui::Button(ICON_FK_FILE " reshade.log")) {
        std::thread([]() {
            LogDebug("Open reshade.log button pressed (bg thread)");

            // Get current process executable path
            char process_path[MAX_PATH];
            DWORD path_length = GetModuleFileNameA(nullptr, process_path, MAX_PATH);

            if (path_length == 0) {
                LogError("Failed to get current process path for log file opening");
                return;
            }

            // Get the parent directory of the executable
            std::string full_path(process_path);
            size_t last_slash = full_path.find_last_of("\\/");

            if (last_slash == std::string::npos) {
                LogError("Invalid process path format: %s", full_path.c_str());
                return;
            }

            std::string log_path = full_path.substr(0, last_slash) + "\\reshade.log";
            LogInfo("Opening reshade.log: %s", log_path.c_str());

            // Open the log file with default text editor
            HINSTANCE result = ShellExecuteA(nullptr, "open", log_path.c_str(), nullptr, nullptr, SW_SHOW);

            if (reinterpret_cast<intptr_t>(result) <= 32) {
                LogError("Failed to open reshade.log: %s (Error: %ld)", log_path.c_str(),
                         reinterpret_cast<intptr_t>(result));
            } else {
                LogInfo("Successfully opened reshade.log: %s", log_path.c_str());
            }
        }).detach();
    }
    ui::colors::PopIconColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Open reshade.log in the default text editor.");
    }

#if 0

    ImGui::SameLine();
    // Maximize Window Button
    ui::colors::PushIconColor(ui::colors::ICON_POSITIVE);
    if (ImGui::Button(ICON_FK_PLUS " Maximize Window")) {
        std::thread([hwnd]() {
            LogDebug("Maximize Window button pressed (bg thread)");

            // Switch to fullscreen mode to maximize the window
            s_window_mode.store(WindowMode::kFullscreen);
            settings::g_mainTabSettings.window_mode.SetValue(static_cast<int>(WindowMode::kFullscreen));

            // Apply the window changes using the existing UI system
            ApplyWindowChange(hwnd, "maximize_button");

            LogInfo("Window maximized to current monitor size");
        }).detach();
    }
    ui::colors::PopIconColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Maximize the window to fill the current monitor.");
    }
#endif
    ImGui::EndGroup();
}

void DrawImportantInfo() {
    // Test Overlay Control
    {
        bool show_test_overlay = settings::g_mainTabSettings.show_test_overlay.GetValue();
        if (ImGui::Checkbox(ICON_FK_SEARCH " Show Overlay", &show_test_overlay)) {
            settings::g_mainTabSettings.show_test_overlay.SetValue(show_test_overlay);
            LogInfo("Performance overlay %s", show_test_overlay ? "enabled" : "disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Shows a performance monitoring widget in the main ReShade overlay with frame time graph, "
                "FPS counter, and other performance metrics. Demonstrates reshade_overlay event usage.");
        }
        ImGui::SameLine();

        // Show Labels Control
        bool show_labels = settings::g_mainTabSettings.show_labels.GetValue();
        if (ImGui::Checkbox("Show labels", &show_labels)) {
            settings::g_mainTabSettings.show_labels.SetValue(show_labels);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows text labels (like 'fps:', 'lat:', etc.) before values in the overlay.");
        }

        // Separator
        ImGui::Separator();

        // Grid layout for overlay display checkboxes (4 columns)
        ImGui::Columns(4, "overlay_checkboxes", false);

        // Show Playtime Control
        bool show_playtime = settings::g_mainTabSettings.show_playtime.GetValue();
        if (ImGui::Checkbox("Playtime", &show_playtime)) {
            settings::g_mainTabSettings.show_playtime.SetValue(show_playtime);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows total playtime (time from game start) in the performance overlay.");
        }
        ImGui::NextColumn();

        // Show FPS Counter
        bool show_fps_counter = settings::g_mainTabSettings.show_fps_counter.GetValue();
        if (ImGui::Checkbox("FPS Counter", &show_fps_counter)) {
            settings::g_mainTabSettings.show_fps_counter.SetValue(show_fps_counter);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows the current FPS counter in the main ReShade overlay.");
        }
        ImGui::NextColumn();

        // Show Native FPS
        bool show_native_fps = settings::g_mainTabSettings.show_native_fps.GetValue();
        if (ImGui::Checkbox("Native FPS", &show_native_fps)) {
            settings::g_mainTabSettings.show_native_fps.SetValue(show_native_fps);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Shows native FPS (calculated from native Reflex sleep calls) alongside regular FPS in format: XX.X / "
                "YY.Y fps");
        }
        ImGui::NextColumn();

        // Show Flip Status
        bool show_flip_status = settings::g_mainTabSettings.show_flip_status.GetValue();
        if (ImGui::Checkbox("Flip Status", &show_flip_status)) {
            settings::g_mainTabSettings.show_flip_status.SetValue(show_flip_status);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Shows the DXGI flip mode status (Composed, Independent Flip, MPO Overlay) in the performance "
                "overlay.");
        }
        ImGui::NextColumn();

        {
            // Show VRR Status
            bool show_vrr_status = settings::g_mainTabSettings.show_vrr_status.GetValue();
            if (ImGui::Checkbox("VRR Status", &show_vrr_status)) {
                settings::g_mainTabSettings.show_vrr_status.SetValue(show_vrr_status);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Shows whether Variable Refresh Rate (VRR) is active in the performance overlay.");
            }
            ImGui::NextColumn();

            // Actual refresh rate (NVAPI Adaptive Sync) - replaces old "Refresh rate" in overlay
            bool show_actual_refresh_rate = settings::g_mainTabSettings.show_actual_refresh_rate.GetValue();
            if (ImGui::Checkbox("Refresh rate", &show_actual_refresh_rate)) {
                settings::g_mainTabSettings.show_actual_refresh_rate.SetValue(show_actual_refresh_rate);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Shows actual refresh rate in the performance overlay (NvAPI_DISP_GetAdaptiveSyncData). "
                    "Also feeds the refresh rate time graph when \"Refresh rate time graph\" is on.");
            }
            ImGui::NextColumn();

            // VRR Debug Mode
            bool vrr_debug_mode = settings::g_mainTabSettings.vrr_debug_mode.GetValue();
            if (ImGui::Checkbox("VRR Debug Mode", &vrr_debug_mode)) {
                settings::g_mainTabSettings.vrr_debug_mode.SetValue(vrr_debug_mode);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Shows detailed VRR debugging parameters (Fixed Hz, Threshold, Samples, etc.) in the performance "
                    "overlay.");
            }
            ImGui::NextColumn();
        }

        if (display_commander::nvapi::IsNvapiActualRefreshRateMonitoringActive()
            && display_commander::nvapi::IsNvapiGetAdaptiveSyncDataFailingRepeatedly()) {
            ImGui::Columns(1);
            ImGui::TextColored(
                ui::colors::TEXT_WARNING,
                "NvAPI_DISP_GetAdaptiveSyncData is failing repeatedly (e.g. driver/display may not support it). "
                "Refresh rate and refresh rate time graph may show no data.");
            ImGui::Columns(4, "overlay_checkboxes", false);
        }

        // Show Cpu busy
        bool show_cpu_usage = settings::g_mainTabSettings.show_cpu_usage.GetValue();
        if (ImGui::Checkbox("Cpu busy", &show_cpu_usage)) {
            settings::g_mainTabSettings.show_cpu_usage.SetValue(show_cpu_usage);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "100%% minus the %% of frame time the FPS limiter spends sleeping. "
                "Not actual CPU usage: measures how much headroom the game has. 100%% = CPU limited.");
        }
        ImGui::NextColumn();

        // Show Cpu FPS (current FPS / cpu busy %)
        bool show_cpu_fps = settings::g_mainTabSettings.show_cpu_fps.GetValue();
        if (ImGui::Checkbox("Cpu FPS", &show_cpu_fps)) {
            settings::g_mainTabSettings.show_cpu_fps.SetValue(show_cpu_fps);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Current FPS / (cpu busy %%). Theoretical FPS if CPU were 100%% busy. "
                "E.g. 100 fps at 50%% busy = 200 cpu fps.");
        }
        ImGui::NextColumn();

        // Show DLSS-FG Mode
        bool show_fg_mode = settings::g_mainTabSettings.show_fg_mode.GetValue();
        if (ImGui::Checkbox("FG Mode", &show_fg_mode)) {
            settings::g_mainTabSettings.show_fg_mode.SetValue(show_fg_mode);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows DLSS Frame Generation mode (OFF / 2x / 3x / 4x) in the performance overlay.");
        }
        ImGui::NextColumn();

        // Show DLSS Internal Resolution
        bool show_dlss_internal_resolution = settings::g_mainTabSettings.show_dlss_internal_resolution.GetValue();
        if (ImGui::Checkbox("DLSS Res", &show_dlss_internal_resolution)) {
            settings::g_mainTabSettings.show_dlss_internal_resolution.SetValue(show_dlss_internal_resolution);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows DLSS internal resolution (e.g., 1920x1080) in the performance overlay.");
        }
        ImGui::NextColumn();

        // Show VRAM (used / budget) in overlay - DXGI adapter memory budget
        bool show_overlay_vram = settings::g_mainTabSettings.show_overlay_vram.GetValue();
        if (ImGui::Checkbox("VRAM", &show_overlay_vram)) {
            settings::g_mainTabSettings.show_overlay_vram.SetValue(show_overlay_vram);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows GPU video memory used / budget (MiB) in the performance overlay (DXGI adapter).");
        }
        ImGui::NextColumn();

        // Show DLSS Status
        bool show_dlss_status = settings::g_mainTabSettings.show_dlss_status.GetValue();
        if (ImGui::Checkbox("DLSS Status", &show_dlss_status)) {
            settings::g_mainTabSettings.show_dlss_status.SetValue(show_dlss_status);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows DLSS on/off status in the performance overlay.");
        }
        ImGui::NextColumn();

        // Show DLSS Quality Preset
        bool show_dlss_quality_preset = settings::g_mainTabSettings.show_dlss_quality_preset.GetValue();
        if (ImGui::Checkbox("DLSS Quality Preset", &show_dlss_quality_preset)) {
            settings::g_mainTabSettings.show_dlss_quality_preset.SetValue(show_dlss_quality_preset);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Shows DLSS quality preset (Performance, Balanced, Quality, Ultra Performance, Ultra Quality, DLAA) in "
                "the performance overlay.");
        }
        ImGui::NextColumn();

        // Show DLSS Render Preset
        bool show_dlss_render_preset = settings::g_mainTabSettings.show_dlss_render_preset.GetValue();
        if (ImGui::Checkbox("DLSS Render Preset", &show_dlss_render_preset)) {
            settings::g_mainTabSettings.show_dlss_render_preset.SetValue(show_dlss_render_preset);
            ResetNGXPresetInitialization();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Shows DLSS render preset (A, B, C, D, E, etc.) for the current quality mode in the performance "
                "overlay.");
        }
        ImGui::NextColumn();

        // Show Stopwatch Control
        bool show_stopwatch = settings::g_mainTabSettings.show_stopwatch.GetValue();
        if (ImGui::Checkbox("Stopwatch", &show_stopwatch)) {
            settings::g_mainTabSettings.show_stopwatch.SetValue(show_stopwatch);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows a stopwatch in the performance overlay. Use Ctrl+S to start/reset.");
        }
        ImGui::NextColumn();

        // Show VU bars (per-channel level) in overlay
        bool show_overlay_vu_bars = settings::g_mainTabSettings.show_overlay_vu_bars.GetValue();
        if (ImGui::Checkbox("VU bars", &show_overlay_vu_bars)) {
            settings::g_mainTabSettings.show_overlay_vu_bars.SetValue(show_overlay_vu_bars);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows per-channel audio level (VU) bars in the performance overlay.");
        }
        ImGui::NextColumn();

        // GPU Measurement Enable/Disable Control
        bool gpu_measurement = settings::g_mainTabSettings.gpu_measurement_enabled.GetValue() != 0;
        if (ImGui::Checkbox("Show latency", &gpu_measurement)) {
            settings::g_mainTabSettings.gpu_measurement_enabled.SetValue(gpu_measurement ? 1 : 0);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Measures time from Present call to GPU completion using fences.\n"
                "Requires D3D11 with Windows 10+ or D3D12.\n"
                "Shows as 'GPU Duration' in the timing metrics below.");
        }
        ImGui::NextColumn();

        // Show Clock Control
        bool show_clock = settings::g_mainTabSettings.show_clock.GetValue();
        if (ImGui::Checkbox("Show clock", &show_clock)) {
            settings::g_mainTabSettings.show_clock.SetValue(show_clock);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows the current time (HH:MM:SS) in the overlay.");
        }
        ImGui::NextColumn();

        // Show Frame Time Graph Control
        bool show_frame_time_graph = settings::g_mainTabSettings.show_frame_time_graph.GetValue();
        if (ImGui::Checkbox("Show frame time graph", &show_frame_time_graph)) {
            settings::g_mainTabSettings.show_frame_time_graph.SetValue(show_frame_time_graph);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows a graph of frame times in the overlay.");
        }
        ImGui::NextColumn();

        // Show Frame Time Stats Control
        bool show_frame_time_stats = settings::g_mainTabSettings.show_frame_time_stats.GetValue();
        if (ImGui::Checkbox("Show frame time stats", &show_frame_time_stats)) {
            settings::g_mainTabSettings.show_frame_time_stats.SetValue(show_frame_time_stats);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows frame time statistics (avg, deviation, min, max) in the overlay.");
        }
        ImGui::NextColumn();

        // Show Native Frame Time Graph Control
        bool show_native_frame_time_graph = settings::g_mainTabSettings.show_native_frame_time_graph.GetValue();
        if (ImGui::Checkbox("Show native frame time graph", &show_native_frame_time_graph)) {
            settings::g_mainTabSettings.show_native_frame_time_graph.SetValue(show_native_frame_time_graph);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Shows a graph of native frame times (frames shown to display via native swapchain Present) in the "
                "overlay.\nOnly available when limit real frames is enabled.");
        }
        ImGui::NextColumn();

        // Show Frame Timeline Bar Control
        bool show_frame_timeline_bar = settings::g_mainTabSettings.show_frame_timeline_bar.GetValue();
        if (ImGui::Checkbox("Show frame timeline bar", &show_frame_timeline_bar)) {
            settings::g_mainTabSettings.show_frame_timeline_bar.SetValue(show_frame_timeline_bar);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Shows a compact frame timeline in the overlay (Simulation, Render Submit, Present, etc. as bars). "
                "Updates every 1 s.");
        }
        ImGui::NextColumn();

        // Refresh Rate Time Graph Control
        bool show_refresh_rate_frame_times = settings::g_mainTabSettings.show_refresh_rate_frame_times.GetValue();
        // Warning: about this introducing heartbeat issue
        if (ImGui::Checkbox("Refresh rate time graph" ICON_FK_WARNING, &show_refresh_rate_frame_times)) {
            settings::g_mainTabSettings.show_refresh_rate_frame_times.SetValue(show_refresh_rate_frame_times);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Shows a graph of actual refresh rate frame times (NVAPI Adaptive Sync) in the overlay. "
                "Requires NVAPI and a resolved display.\n"
                "WARNING: This may introduces a heartbeat issue, with frame time spike once a second.");
        }
        ImGui::NextColumn();

        // Show Refresh Rate Time Stats Control
        bool show_refresh_rate_frame_time_stats =
            settings::g_mainTabSettings.show_refresh_rate_frame_time_stats.GetValue();
        if (ImGui::Checkbox("Refresh rate time stats", &show_refresh_rate_frame_time_stats)) {
            settings::g_mainTabSettings.show_refresh_rate_frame_time_stats.SetValue(show_refresh_rate_frame_time_stats);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows refresh rate time statistics (avg, deviation, min, max) in the overlay.");
        }
        {
            ImGui::NextColumn();

            // Show Volume Control (experimental feature)
            {
                bool show_volume = settings::g_experimentalTabSettings.show_volume.GetValue();
                if (ImGui::Checkbox("Show volume", &show_volume)) {
                    settings::g_experimentalTabSettings.show_volume.SetValue(show_volume);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Shows the current audio volume percentage in the overlay.");
                }
            }
        }

        ImGui::Columns(1);  // Reset to single column
        if (show_refresh_rate_frame_times || settings::g_mainTabSettings.show_actual_refresh_rate.GetValue()) {
            if (SliderIntSetting(settings::g_mainTabSettings.refresh_rate_monitor_poll_ms, "Refresh poll (ms)",
                                 "%d ms")) {
                // Setting is automatically saved by SliderIntSetting
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Polling interval for the actual refresh rate monitoring thread when the time graph is enabled. "
                    "Lower values update the graph more frequently but use more CPU. When the time graph is off, "
                    "polling defaults to 1 s and this setting is not used.");
            }
        }

        ImGui::Spacing();
        // Overlay background transparency slider
        if (SliderFloatSetting(settings::g_mainTabSettings.overlay_background_alpha, "Overlay Background Transparency",
                               "%.2f")) {
            // Setting is automatically saved by SliderFloatSetting
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Controls the transparency of the overlay background. 0.0 = fully transparent, 1.0 = fully opaque.");
        }
        // Overlay chart transparency slider
        if (SliderFloatSetting(settings::g_mainTabSettings.overlay_chart_alpha, "Frame Chart Transparency", "%.2f")) {
            // Setting is automatically saved by SliderFloatSetting
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Controls the transparency of the frame time and refresh rate chart backgrounds. 0.0 = fully "
                "transparent, 1.0 = fully opaque. Chart lines remain fully visible.");
        }
        // Overlay graph scale slider
        if (SliderFloatSetting(settings::g_mainTabSettings.overlay_graph_scale, "Graph Size Scale", "%.1fx")) {
            // Setting is automatically saved by SliderFloatSetting
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Controls the size of the frame time and refresh rate graphs in the overlay. "
                "1.0x = default size (300x60px), 4.0x = maximum size (1200x240px).");
        }
        // Overlay graph max scale slider
        if (SliderFloatSetting(settings::g_mainTabSettings.overlay_graph_max_scale, "Graph Max Value Scale", "%.1fx")) {
            // Setting is automatically saved by SliderFloatSetting
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Controls the maximum Y-axis value for the frame time and refresh rate graphs. "
                "The graph will scale from 0ms to (average frame time × this multiplier). "
                "Lower values (2x-4x) show more detail for normal frame times. "
                "Higher values (6x-10x) accommodate frame time spikes without clipping.");
        }
        // Overlay vertical spacing slider
        if (SliderFloatSetting(settings::g_mainTabSettings.overlay_vertical_spacing, "Overlay Vertical Spacing",
                               "%.0f px")) {
            // Setting is automatically saved by SliderFloatSetting
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Adds vertical spacing to the performance overlay position. "
                "Useful to prevent overlap with stream overlay text. "
                "Positive values move the overlay down, negative values move it up.");
        }
        // Overlay horizontal spacing slider
        if (SliderFloatSetting(settings::g_mainTabSettings.overlay_horizontal_spacing, "Overlay Horizontal Spacing",
                               "%.0f px")) {
            // Setting is automatically saved by SliderFloatSetting
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Adds horizontal spacing to the performance overlay position. "
                "Useful to prevent overlap with stream overlay text. "
                "Positive values move the overlay to the right, negative values move it to the left.");
        }
    }

    ImGui::Spacing();

    // FPS Counter with 1% Low and 0.1% Low over past 60s (computed in background)
    {
        std::string local_text;
        auto shared_text = ::g_perf_text_shared.load();
        if (shared_text) {
            local_text = *shared_text;
        }
        ImGui::TextUnformatted(local_text.c_str());
        ui::colors::PushIconColor(ui::colors::ICON_ACTION);
        if (ImGui::Button(ICON_FK_REFRESH " Reset Stats")) {
            ::g_perf_reset_requested.store(true, std::memory_order_release);
        }
        ui::colors::PopIconColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Reset FPS/frametime statistics. Metrics are computed since reset.");
        }
    }

    ImGui::Spacing();

    // Frame Time Graph Section (see docs/UI_STYLE_GUIDE.md for depth/indent rules)
    // Depth 1: Nested subsection with indentation and distinct colors
    ImGui::Indent();  // Indent nested header
    g_rendering_ui_section.store("ui:tab:main_new:frame_time_graph", std::memory_order_release);
    ui::colors::PushNestedHeaderColors();  // Apply distinct colors for nested header
    if (ImGui::CollapsingHeader("Frame Time Graph", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();  // Indent content inside subsection
        // Display GPU fence status/failure reason
        if (settings::g_mainTabSettings.gpu_measurement_enabled.GetValue() != 0) {
            const char* failure_reason = ::g_gpu_fence_failure_reason.load();
            if (failure_reason != nullptr) {
                ImGui::Indent();
                ui::colors::PushIconColor(ui::colors::ICON_ERROR);
                ImGui::TextUnformatted(ICON_FK_WARNING);
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::TextColored(ui::colors::TEXT_ERROR, "GPU Fence Failed: %s", failure_reason);
                ImGui::Unindent();
            } else {
                ImGui::Indent();
                ui::colors::PushIconColor(ui::colors::ICON_SUCCESS);
                ImGui::TextUnformatted(ICON_FK_OK);
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::TextColored(ui::colors::TEXT_SUCCESS, "GPU Fence Active");
                ImGui::Unindent();
            }
        }

        ImGui::Spacing();

        DrawFrameTimeGraph();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Frame timeline bar (Simulation | Render Submit | ReShade | Present | Sleep | GPU)
        DrawFrameTimelineBar();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Native Frame Time Graph
        ImGui::Text("Native Frame Time Graph");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Shows frame times for frames actually displayed via native swapchain Present when limit real frames "
                "is enabled.");
        }
        ImGui::Spacing();

        DrawNativeFrameTimeGraph();

        ImGui::Spacing();

        std::ostringstream oss;

        // Present Duration Display
        oss.str("");
        oss.clear();
        oss << "Present Duration: " << std::fixed << std::setprecision(3)
            << (1.0 * ::g_present_duration_ns.load() / utils::NS_TO_MS) << " ms";
        ImGui::TextUnformatted(oss.str().c_str());
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::TEXT_VALUE, "(smoothed)");

        // Frame Duration Display
        oss.str("");
        oss.clear();
        oss << "Frame Duration: " << std::fixed << std::setprecision(3)
            << (1.0 * ::g_frame_time_ns.load() / utils::NS_TO_MS) << " ms";
        ImGui::TextUnformatted(oss.str().c_str());
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::TEXT_VALUE, "(smoothed)");

        // GPU Duration Display (only show if measurement is enabled and has data)
        if (settings::g_mainTabSettings.gpu_measurement_enabled.GetValue() != 0 && ::g_gpu_duration_ns.load() > 0) {
            oss.str("");
            oss.clear();
            oss << "GPU Duration: " << std::fixed << std::setprecision(3)
                << (1.0 * ::g_gpu_duration_ns.load() / utils::NS_TO_MS) << " ms";
            ImGui::TextUnformatted(oss.str().c_str());
            ImGui::SameLine();
            ImGui::TextColored(ui::colors::TEXT_VALUE, "(smoothed)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Time from Present call to GPU completion (D3D11 only, requires Windows 10+)");
            }

            // Sim-to-Display Latency (only show if we have valid measurement)
            if (::g_sim_to_display_latency_ns.load() > 0) {
                oss.str("");
                oss.clear();
                oss << "Sim-to-Display Latency: " << std::fixed << std::setprecision(3)
                    << (1.0 * ::g_sim_to_display_latency_ns.load() / utils::NS_TO_MS) << " ms";
                ImGui::TextUnformatted(oss.str().c_str());
                ImGui::SameLine();
                ImGui::TextColored(ui::colors::TEXT_VALUE, "(smoothed)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Time from simulation start to frame displayed (includes GPU work and present)");
                }

                // GPU Late Time (how much later GPU finishes compared to Present)
                oss.str("");
                oss.clear();
                oss << "GPU Late Time: " << std::fixed << std::setprecision(3)
                    << (1.0 * ::g_gpu_late_time_ns.load() / utils::NS_TO_MS) << " ms";
                ImGui::TextUnformatted(oss.str().c_str());
                ImGui::SameLine();
                ImGui::TextColored(ui::colors::TEXT_VALUE, "(smoothed)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "How much later GPU completion finishes compared to Present\n0 ms = GPU finished before "
                        "Present\n>0 ms = GPU finished after Present (GPU is late)");
                }
            }
        }

        oss.str("");
        oss.clear();
        oss << "Simulation Duration: " << std::fixed << std::setprecision(3)
            << (1.0 * ::g_simulation_duration_ns.load() / utils::NS_TO_MS) << " ms";
        ImGui::TextUnformatted(oss.str().c_str());
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::TEXT_VALUE, "(smoothed)");

        // Reshade Overhead Display
        oss.str("");
        oss.clear();
        oss << "Render Submit Duration: " << std::fixed << std::setprecision(3)
            << (1.0 * ::g_render_submit_duration_ns.load() / utils::NS_TO_MS) << " ms";
        ImGui::TextUnformatted(oss.str().c_str());
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::TEXT_VALUE, "(smoothed)");

        // Reshade Overhead Display
        oss.str("");
        oss.clear();
        oss << "Reshade Overhead Duration: " << std::fixed << std::setprecision(3)
            << ((1.0 * ::g_reshade_overhead_duration_ns.load() - ::fps_sleep_before_on_present_ns.load()
                 - ::fps_sleep_after_on_present_ns.load())
                / utils::NS_TO_MS)
            << " ms";
        ImGui::TextUnformatted(oss.str().c_str());
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::TEXT_VALUE, "(smoothed)");

        oss.str("");
        oss.clear();
        oss << "FPS Limiter Sleep Duration (before onPresent): " << std::fixed << std::setprecision(3)
            << (1.0 * ::fps_sleep_before_on_present_ns.load() / utils::NS_TO_MS) << " ms";
        ImGui::TextUnformatted(oss.str().c_str());
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::TEXT_VALUE, "(smoothed)");

        // FPS Limiter Start Duration Display
        oss.str("");
        oss.clear();
        oss << "FPS Limiter Sleep Duration (after onPresent): " << std::fixed << std::setprecision(3)
            << (1.0 * ::fps_sleep_after_on_present_ns.load() / utils::NS_TO_MS) << " ms";
        ImGui::TextUnformatted(oss.str().c_str());
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::TEXT_VALUE, "(smoothed)");

        // Simulation Start to Present Latency Display
        oss.str("");
        oss.clear();
        // Calculate latency: frame_time - sleep duration after onPresent
        float current_fps = 0.0f;
        const uint32_t count = ::g_perf_ring.GetCount();
        if (count > 0) {
            const ::PerfSample& last_sample = ::g_perf_ring.GetSample(0);
            current_fps = 1.0f / last_sample.dt;
        }

        if (current_fps > 0.0f) {
            float frame_time_ms = 1000.0f / current_fps;
            float sleep_duration_ms = static_cast<float>(::fps_sleep_after_on_present_ns.load()) / utils::NS_TO_MS;
            float latency_ms = frame_time_ms - sleep_duration_ms;

            static double sim_start_to_present_latency_ms = 0.0;
            sim_start_to_present_latency_ms = (sim_start_to_present_latency_ms * 0.99 + latency_ms * 0.01);
            oss << "Sim Start to Present Latency: " << std::fixed << std::setprecision(3)
                << sim_start_to_present_latency_ms << " ms";
            ImGui::TextUnformatted(oss.str().c_str());
            ImGui::SameLine();
            ImGui::TextColored(ui::colors::TEXT_HIGHLIGHT, "(frame_time - sleep_duration)");
        }

        // Flip State Display (renamed from DXGI Composition)
        const char* flip_state_str = "Unknown";
        int current_api = g_last_reshade_device_api.load();
        DxgiBypassMode flip_state = GetFlipStateForAPI(current_api);

        switch (flip_state) {
            case DxgiBypassMode::kUnset:                    flip_state_str = "Unset"; break;
            case DxgiBypassMode::kComposed:                 flip_state_str = "Composed Flip"; break;
            case DxgiBypassMode::kOverlay:                  flip_state_str = "MPO Independent Flip"; break;
            case DxgiBypassMode::kIndependentFlip:          flip_state_str = "Legacy Independent Flip"; break;
            case DxgiBypassMode::kQueryFailedSwapchainNull: flip_state_str = "Query Failed: Swapchain Null"; break;
            case DxgiBypassMode::kQueryFailedNoMedia:       flip_state_str = "Query Failed: No Media Interface"; break;
            case DxgiBypassMode::kQueryFailedNoSwapchain1:  flip_state_str = "Query Failed: No Swapchain1"; break;
            case DxgiBypassMode::kQueryFailedNoStats:       flip_state_str = "Query Failed: No Statistics"; break;
            case DxgiBypassMode::kUnknown:
            default:                                        flip_state_str = "Unknown"; break;
        }

        oss.str("");
        oss.clear();
        oss << "Status: " << flip_state_str;

        // Color code based on flip state
        if (flip_state == DxgiBypassMode::kComposed) {
            // Composed Flip - Red
            ImGui::TextColored(ui::colors::FLIP_COMPOSED, "%s", oss.str().c_str());
        } else if (flip_state == DxgiBypassMode::kOverlay || flip_state == DxgiBypassMode::kIndependentFlip) {
            // Independent Flip modes - Green
            ImGui::TextColored(ui::colors::FLIP_INDEPENDENT, "%s", oss.str().c_str());
        } else if (flip_state == DxgiBypassMode::kQueryFailedSwapchainNull
                   || flip_state == DxgiBypassMode::kQueryFailedNoSwapchain1
                   || flip_state == DxgiBypassMode::kQueryFailedNoMedia
                   || flip_state == DxgiBypassMode::kQueryFailedNoStats) {
            // Query Failed - Red
            ImGui::TextColored(ui::colors::TEXT_ERROR, "%s", oss.str().c_str());
        } else {
            // Unknown/Unset - Yellow
            ImGui::TextColored(ui::colors::FLIP_UNKNOWN, "%s", oss.str().c_str());
        }
    }

    ImGui::Spacing();

    g_rendering_ui_section.store("ui:tab:main_new:refresh_rate_monitor", std::memory_order_release);
    // Refresh Rate Monitor Section (NvAPI_DISP_GetAdaptiveSyncData)
    if (ImGui::CollapsingHeader("Refresh Rate Monitor", ImGuiTreeNodeFlags_None)) {
        bool is_monitoring = display_commander::nvapi::IsNvapiActualRefreshRateMonitoringActive();

        if (ImGui::Button(is_monitoring ? ICON_FK_CANCEL " Stop Monitoring" : ICON_FK_PLUS " Start Monitoring")) {
            if (is_monitoring) {
                display_commander::nvapi::StopNvapiActualRefreshRateMonitoring();
            } else {
                display_commander::nvapi::StartNvapiActualRefreshRateMonitoring();
            }
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Measures actual display refresh rate via NvAPI_DISP_GetAdaptiveSyncData (flip count/timestamp).\n"
                "Requires NVAPI and a resolved display. Shows the real refresh rate which may differ\n"
                "from the configured rate due to VRR, power management, or other factors.");
        }

        ImGui::SameLine();

        // Status display
        const char* status_str = is_monitoring ? "Active" : "Inactive";
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "Status: %s", status_str);

        if (is_monitoring && display_commander::nvapi::IsNvapiGetAdaptiveSyncDataFailingRepeatedly()) {
            ImGui::Spacing();
            ImGui::TextColored(
                ui::colors::TEXT_WARNING,
                "NvAPI_DISP_GetAdaptiveSyncData is failing repeatedly (driver/display may not support it).");
        }

        // Display DXGI output device name
        if (g_got_device_name.load()) {
            auto device_name_ptr = g_dxgi_output_device_name.load();
            if (device_name_ptr != nullptr) {
                ImGui::Spacing();
                ImGui::Text("DXGI Output Device:");
                ImGui::SameLine();
                ImGui::TextColored(ui::colors::TEXT_HIGHLIGHT, "%ls", device_name_ptr->c_str());
            } else {
                ImGui::Spacing();
                ImGui::TextColored(ui::colors::TEXT_DIMMED, "DXGI Output Device: Not available");
            }
        } else {
            ImGui::Spacing();
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "DXGI Output Device: Not detected yet");
        }

        // Refresh rate data from NVAPI actual refresh rate monitor (recent samples)
        double current_hz = display_commander::nvapi::GetNvapiActualRefreshRateHz();
        size_t sample_count = 0;
        double min_hz = 0.0;
        double max_hz = 0.0;
        double sum_hz = 0.0;
        display_commander::nvapi::ForEachNvapiActualRefreshRateSample([&](double rate_hz) {
            if (rate_hz > 0.0) {
                if (sample_count == 0) {
                    min_hz = max_hz = rate_hz;
                } else {
                    min_hz = (std::min)(min_hz, rate_hz);
                    max_hz = (std::max)(max_hz, rate_hz);
                }
                sum_hz += rate_hz;
                ++sample_count;
            }
        });
        double avg_hz = (sample_count > 0) ? (sum_hz / static_cast<double>(sample_count)) : 0.0;

        if (sample_count > 0) {
            ImGui::Spacing();

            // Current refresh rate (large, prominent display)
            ImGui::Text("Measured Refresh Rate:");
            ImGui::SameLine();
            ImGui::TextColored(ui::colors::TEXT_HIGHLIGHT, "%.1f Hz", current_hz > 0.0 ? current_hz : avg_hz);

            // Detailed statistics
            ImGui::Indent();
            ImGui::Text("Current: %.1f Hz", current_hz > 0.0 ? current_hz : avg_hz);
            ImGui::Text("Min: %.1f Hz", min_hz);
            ImGui::Text("Max: %.1f Hz", max_hz);
            ImGui::Text("Samples: %zu", sample_count);
            ImGui::Unindent();

            // VRR detection hint
            if (max_hz > min_hz + 1.0) {
                ImGui::Spacing();
                ui::colors::PushIconColor(ui::colors::ICON_SUCCESS);
                ImGui::TextUnformatted(ICON_FK_OK);
                ui::colors::PopIconColor();
                ImGui::SameLine();
                ImGui::TextColored(ui::colors::TEXT_SUCCESS, "Variable Refresh Rate (VRR) detected");
            }
        } else if (is_monitoring) {
            ImGui::Spacing();
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Collecting data...");
        } else {
            ImGui::Spacing();
            ImGui::TextColored(ui::colors::TEXT_DIMMED,
                               "No refresh rate data (start monitoring or enable overlay refresh rate).");
        }
        ImGui::Unindent();  // Unindent content
    }
    ui::colors::PopNestedHeaderColors();  // Restore default header colors
    ImGui::Unindent();                    // Unindent nested header section
}

void DrawAdhdMultiMonitorControls(bool hasBlackCurtainSetting) {
    // Check if multiple monitors are available
    bool hasMultipleMonitors = adhd_multi_monitor::api::HasMultipleMonitors();

    if (!hasMultipleMonitors) {
        return;
    }
    if (hasBlackCurtainSetting) ImGui::SameLine();

    // Main ADHD mode checkbox
    bool adhdEnabled = settings::g_mainTabSettings.adhd_multi_monitor_enabled.GetValue();
    if (ImGui::Checkbox("ADHD Multi-Monitor Mode", &adhdEnabled)) {
        settings::g_mainTabSettings.adhd_multi_monitor_enabled.SetValue(adhdEnabled);
        // adhd_multi_monitor::api::SetEnabled(adhdEnabled);
        LogInfo("ADHD Multi-Monitor Mode %s", adhdEnabled ? "enabled" : "disabled");
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Covers secondary monitors with a black window to reduce distractions while playing this game.");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Similar to Special-K's ADHD Multi-Monitor Mode.\nThe black background window will automatically position "
            "itself to cover all monitors except the one where your game is running.");
    }
}

}  // namespace ui::new_ui
