// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
// Headers <Display Commander>
#include "performance_overlay_internal.hpp"
#include "utils.hpp"

namespace ui::new_ui {

void DrawNativeFrameTimeGraph(display_commander::ui::IImGuiWrapper& imgui) {
    (void)imgui;
    CALL_GUARD_NO_TS();
    const uint32_t head = ::g_native_frame_time_ring.GetHead();
    const uint32_t count = ::g_native_frame_time_ring.GetCountFromHead(head);

    if (count == 0) {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "No native frame time data available yet...");
        return;
    }

    static std::vector<float> frame_times;
    frame_times.clear();
    const uint32_t samples_to_collect = min(count, 300u);
    frame_times.reserve(samples_to_collect);

    for (uint32_t i = 0; i < samples_to_collect; ++i) {
        const ::PerfSample sample = ::g_native_frame_time_ring.GetSampleWithHead(i, head);
        if (sample.dt > 0.0f) {
            frame_times.push_back(1000.0 * sample.dt);
        }
    }

    if (frame_times.empty()) {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "No valid native frame time data available...");
        return;
    }

    float min_frame_time = *std::ranges::min_element(frame_times);
    float max_frame_time = *std::ranges::max_element(frame_times);
    float avg_frame_time = 0.0f;
    for (float ft : frame_times) {
        avg_frame_time += ft;
    }
    avg_frame_time /= static_cast<float>(frame_times.size());

    float avg_fps = (avg_frame_time > 0.0f) ? (1000.0f / avg_frame_time) : 0.0f;

    imgui.Text("Min: %.2f ms | Max: %.2f ms | Avg: %.2f ms | FPS(avg): %.1f", min_frame_time, max_frame_time,
               avg_frame_time, avg_fps);

    std::string overlay_text = "Native Frame Time: " + std::to_string(frame_times.back()).substr(0, 4) + " ms";

    ImVec2 graph_size = ImVec2(-1.0f, 200.0f);
    float scale_min = 0.0f;
    float scale_max = avg_frame_time * 4.f;

    imgui.PlotLines("Native Frame Time (ms)", frame_times.data(), static_cast<int>(frame_times.size()), 0,
                    overlay_text.c_str(), scale_min, scale_max, graph_size);

    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Native frame time graph showing frames actually shown to display via native swapchain Present.\n"
            "Lower values = higher FPS, smoother gameplay.\n"
            "Spikes indicate frame drops or stuttering.");
    }
}

void DrawNativeFrameTimeGraphOverlay(display_commander::ui::IImGuiWrapper& imgui, bool show_tooltips) {
    (void)imgui;
    CALL_GUARD_NO_TS();
    if (perf_measurement::IsSuppressionEnabled()
        && perf_measurement::IsMetricSuppressed(perf_measurement::Metric::Overlay)) {
        return;
    }

    perf_measurement::ScopedTimer perf_timer(perf_measurement::Metric::Overlay);

    const uint32_t head = ::g_native_frame_time_ring.GetHead();
    const uint32_t count = ::g_native_frame_time_ring.GetCountFromHead(head);

    if (count == 0) {
        return;
    }
    const uint32_t samples_to_display = min(count, 256u);

    static std::vector<float> frame_times;
    frame_times.clear();
    frame_times.reserve(samples_to_display);

    for (uint32_t i = 0; i < samples_to_display; ++i) {
        const ::PerfSample sample = ::g_native_frame_time_ring.GetSampleWithHead(i, head);
        if (sample.dt > 0.0f) {
            frame_times.push_back(1000.0 * sample.dt);
        }
    }

    if (frame_times.empty()) {
        return;
    }

    float max_frame_time = *std::ranges::max_element(frame_times);
    float avg_frame_time = 0.0f;
    for (float ft : frame_times) {
        avg_frame_time += ft;
    }
    avg_frame_time /= static_cast<float>(frame_times.size());

    float graph_scale = settings::g_mainTabSettings.overlay_graph_scale.GetValue();
    ImVec2 graph_size = ImVec2(300.0f * graph_scale, 60.0f * graph_scale);
    float scale_min = 0.0f;
    float max_scale = settings::g_mainTabSettings.overlay_graph_max_scale.GetValue();
    float scale_max = avg_frame_time * max_scale;

    float chart_alpha = settings::g_mainTabSettings.overlay_chart_alpha.GetValue();
    ImVec4 bg_color = imgui.GetStyle().Colors[ImGuiCol_FrameBg];
    bg_color.w *= chart_alpha;

    imgui.PushStyleColor(ImGuiCol_FrameBg, bg_color);

    imgui.PlotLines("##NativeFrameTime", frame_times.data(), static_cast<int>(frame_times.size()), 0, nullptr,
                    scale_min, scale_max, graph_size);

    imgui.PopStyleColor();

    if (imgui.IsItemHovered() && show_tooltips) {
        imgui.SetTooltipEx("Native frame time graph (last 256 frames)\nAvg: %.2f ms | Max: %.2f ms", avg_frame_time,
                           max_frame_time);
    }
}

}  // namespace ui::new_ui
