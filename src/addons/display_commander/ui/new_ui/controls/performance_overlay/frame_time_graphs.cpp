// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
// Headers <Display Commander>
#include "performance_overlay_internal.hpp"
#include "utils.hpp"

namespace ui::new_ui {

void DrawFrameTimeGraph(display_commander::ui::IImGuiWrapper& imgui) {
    (void)imgui;
    CALL_GUARD_NO_TS();
    const uint32_t head = ::g_perf_ring.GetHead();
    const uint32_t count = ::g_perf_ring.GetCountFromHead(head);

    if (count == 0) {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "No frame time data available yet...");
        return;
    }

    static std::vector<float> frame_times;
    frame_times.clear();
    const uint32_t samples_to_collect = min(count, 300u);
    frame_times.reserve(samples_to_collect);

    for (uint32_t i = 0; i < samples_to_collect; ++i) {
        const ::PerfSample sample = ::g_perf_ring.GetSampleWithHead(i, head);
        if (sample.dt > 0.0f) {
            frame_times.push_back(sample.dt);
        }
    }

    if (frame_times.empty()) {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "No valid frame time data available...");
        return;
    }

    float min_frame_time = *std::ranges::min_element(frame_times);
    float max_frame_time = *std::ranges::max_element(frame_times);
    float avg_frame_time = 0.0f;
    for (float ft : frame_times) {
        avg_frame_time += ft;
    }
    avg_frame_time /= static_cast<float>(frame_times.size());

    float avg_fps = (avg_frame_time > 0.0f) ? (1.0f / avg_frame_time) : 0.0f;

    imgui.Text("Min: %.2f ms | Max: %.2f ms | Avg: %.2f ms | FPS(avg): %.1f", min_frame_time, max_frame_time,
               avg_frame_time, avg_fps);

    std::string overlay_text = "Frame Time: " + std::to_string(frame_times.back()).substr(0, 4) + " ms";

    ImVec2 graph_size = ImVec2(-1.0f, 200.0f);
    float scale_min = 0.0f;
    float scale_max = avg_frame_time * 4.f;

    imgui.PlotLines("Frame Time (ms)", frame_times.data(), static_cast<int>(frame_times.size()), 0,
                    overlay_text.c_str(), scale_min, scale_max, graph_size);

    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Frame time graph showing recent frame times in milliseconds.\n"
            "Lower values = higher FPS, smoother gameplay.\n"
            "Spikes indicate frame drops or stuttering.");
    }

    imgui.Spacing();
    imgui.Text("Frame Time Mode:");
    imgui.SameLine();

    int current_mode = static_cast<int>(settings::g_mainTabSettings.frame_time_mode.GetValue());
    const char* mode_items[] = {"Present-to-Present", "Frame Begin-to-Frame Begin", "Display Timing (GPU Completion)"};

    if (imgui.Combo("##frame_time_mode", &current_mode, mode_items, 3)) {
        settings::g_mainTabSettings.frame_time_mode.SetValue(current_mode);
        LogInfo("Frame time mode changed to: %s", mode_items[current_mode]);
    }

    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Select which timing events to record for the frame time graph:\n"
            "- Present-to-Present: Records time between Present calls\n"
            "- Frame Begin-to-Frame Begin: Records time between frame begin events\n"
            "- Display Timing: Records when frames are actually displayed (based on GPU completion)\n"
            "  Note: Display Timing requires GPU measurement to be enabled");
    }
}

void DrawFrameTimeGraphOverlay(display_commander::ui::IImGuiWrapper& imgui, bool show_tooltips) {
    (void)imgui;
    CALL_GUARD_NO_TS();
    if (perf_measurement::IsSuppressionEnabled()
        && perf_measurement::IsMetricSuppressed(perf_measurement::Metric::Overlay)) {
        return;
    }

    perf_measurement::ScopedTimer perf_timer(perf_measurement::Metric::Overlay);

    const uint32_t head = ::g_perf_ring.GetHead();
    const uint32_t count = ::g_perf_ring.GetCountFromHead(head);

    if (count == 0) {
        return;
    }
    const uint32_t samples_to_display = min(count, 256u);

    static std::vector<float> frame_times;
    frame_times.clear();
    frame_times.reserve(samples_to_display);

    for (uint32_t i = 0; i < samples_to_display; ++i) {
        const ::PerfSample sample = ::g_perf_ring.GetSampleWithHead(i, head);
        frame_times.push_back(1000.0 * sample.dt);
    }

    if (frame_times.empty()) {
        return;
    }

    float min_frame_time = *std::ranges::min_element(frame_times);
    float max_frame_time = *std::ranges::max_element(frame_times);
    float avg_frame_time = 0.0f;
    for (float ft : frame_times) {
        avg_frame_time += ft;
    }
    avg_frame_time /= static_cast<float>(frame_times.size());

    float variance = 0.0f;
    for (float ft : frame_times) {
        float diff = ft - avg_frame_time;
        variance += diff * diff;
    }
    variance /= static_cast<float>(frame_times.size());
    float std_deviation = std::sqrt(variance);

    float graph_scale = settings::g_mainTabSettings.overlay_graph_scale.GetValue();
    ImVec2 graph_size = ImVec2(300.0f * graph_scale, 60.0f * graph_scale);
    float scale_min = 0.0f;
    float max_scale = settings::g_mainTabSettings.overlay_graph_max_scale.GetValue();
    float scale_max = avg_frame_time * max_scale;

    float chart_alpha = settings::g_mainTabSettings.overlay_chart_alpha.GetValue();
    ImVec4 bg_color = imgui.GetStyle().Colors[ImGuiCol_FrameBg];
    bg_color.w *= chart_alpha;

    imgui.PushStyleColor(ImGuiCol_FrameBg, bg_color);

    imgui.PlotLines("##FrameTime", frame_times.data(), static_cast<int>(frame_times.size()), 0, nullptr,
                    scale_min, scale_max, graph_size);

    imgui.PopStyleColor();

    bool show_frame_time_stats = settings::g_mainTabSettings.show_frame_time_stats.GetValue();
    if (show_frame_time_stats) {
        imgui.Text("Avg: %.2f ms | Dev: %.2f ms | Min: %.2f ms | Max: %.2f ms", avg_frame_time, std_deviation,
                   min_frame_time, max_frame_time);
    }

    if (imgui.IsItemHovered() && show_tooltips) {
        imgui.SetTooltipEx("Frame time graph (last 256 frames)\nAvg: %.2f ms | Max: %.2f ms", avg_frame_time,
                           max_frame_time);
    }
}

}  // namespace ui::new_ui
