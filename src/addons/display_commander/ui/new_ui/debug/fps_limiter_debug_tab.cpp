// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "fps_limiter_debug_tab.hpp"
#include "../../../globals.hpp"
#include "../../../swapchain_events.hpp"
#include "../../../utils/timing.hpp"

// Libraries <ReShade> / <imgui>
#include <imgui.h>

// Libraries <standard C++>
#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <string>

namespace ui::new_ui::debug {

namespace {

std::string FgModeHumanReadable(int fg_mode) {
    if (fg_mode == kDlssGFgModeOff) {
        return "OFF (FPS limiter does not divide by FG)";
    }
    if (fg_mode == kDlssGFgModeActiveUnknown) {
        return "FG on, multiplier unknown (no DLSSG.MultiFrameCount)";
    }
    if (fg_mode >= 2) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%dx (capped FPS / present-delay logic uses this N)", fg_mode);
        return std::string(buf);
    }
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%d (unexpected)", fg_mode);
    return std::string(buf);
}

const char* FpsLimiterModeLabel(FpsLimiterMode m) {
    switch (m) {
        case FpsLimiterMode::kOnPresentSync:
            return "OnPresentSync";
        case FpsLimiterMode::kReflex:
            return "Reflex";
        default:
            return "?";
    }
}

void DrawRatesRow(display_commander::ui::IImGuiWrapper& imgui, const char* label, double calls_per_sec) {
    imgui.TableNextRow();
    imgui.TableNextColumn();
    imgui.TextUnformatted(label);
    imgui.TableNextColumn();
    if (calls_per_sec >= 0.0) {
        imgui.Text("%.1f /s", calls_per_sec);
    } else {
        imgui.TextUnformatted("…");
    }
}

}  // namespace

void DrawFpsLimiterDebugTab(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.TextWrapped(
        "GetDLSSGSummaryLite() is sampled when this tab draws; target FPS and fg_mode-at-pre are updated inside "
        "HandleFpsLimiterPre each present. If fg_mode is 0 but effective target is half of native, the limiter path "
        "may still be pacing for a lower cap (compare modes and call rates). DEBUG_TABS / bd.ps1 -DebugTabs.");
    imgui.Spacing();

    imgui.Separator();
    imgui.Spacing();
    imgui.TextUnformatted("NVAPI marker mapping (runtime only)");
    imgui.TextUnformatted("Affects NVAPI FPS limiter mapping only. Resets on restart.");

    auto draw_marker_input = [&](const char* label, std::atomic<int>& marker_value) {
        int value = marker_value.load(std::memory_order_relaxed);
        if (ImGui::InputInt(label, &value, 1, 1)) {
            const int min_marker = 0;
            const int max_marker = static_cast<int>(kLatencyMarkerTypeCount) - 1;
            value = (std::max)(min_marker, (std::min)(value, max_marker));
            marker_value.store(value, std::memory_order_relaxed);
        }
    };

    draw_marker_input("simulation_start marker", g_nvapi_marker_simulation_start);
    draw_marker_input("present_start marker", g_nvapi_marker_present_start);
    draw_marker_input("present_end marker", g_nvapi_marker_present_end);

    if (imgui.Button("Reset NVAPI marker defaults")) {
        ResetNvapiReflexMarkerTypesForFpsLimiter();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Restores defaults used before runtime configurability:\n"
            "simulation_start=SIMULATION_START, present_start=PRESENT_START-1, present_end=PRESENT_END-2.");
    }

    const ReflexMarkerTypes marker_snapshot = GetNvapiReflexMarkerTypesForFpsLimiter();
    const bool has_duplicate_markers = marker_snapshot.simulation_start == marker_snapshot.present_start
                                       || marker_snapshot.simulation_start == marker_snapshot.present_end
                                       || marker_snapshot.present_start == marker_snapshot.present_end;
    if (has_duplicate_markers) {
        imgui.TextUnformatted("Warning: duplicate marker IDs can reduce pacing accuracy.");
    }
    imgui.Spacing();

    const DLSSGSummaryLite lite = GetDLSSGSummaryLite();
    const int fg_at_last_pre = g_fps_limiter_debug_getlite_fg_mode.load(std::memory_order_relaxed);
    const std::string lite_fg_str = FgModeHumanReadable(lite.fg_mode);
    const std::string pre_fg_str = FgModeHumanReadable(fg_at_last_pre);

    constexpr int kCols = 2;
    if (imgui.BeginTable("fps_limiter_dlssg_lite", kCols, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        imgui.TableSetupColumn("Field");
        imgui.TableSetupColumn("Value");
        imgui.TableHeadersRow();

        imgui.TableNextRow();
        imgui.TableNextColumn();
        imgui.TextUnformatted("any_dlss_active");
        imgui.TableNextColumn();
        imgui.Text("%s", lite.any_dlss_active ? "true" : "false");

        imgui.TableNextRow();
        imgui.TableNextColumn();
        imgui.TextUnformatted("dlss_active");
        imgui.TableNextColumn();
        imgui.Text("%s", lite.dlss_active ? "true" : "false");

        imgui.TableNextRow();
        imgui.TableNextColumn();
        imgui.TextUnformatted("dlss_g_active");
        imgui.TableNextColumn();
        imgui.Text("%s", lite.dlss_g_active ? "true" : "false");

        imgui.TableNextRow();
        imgui.TableNextColumn();
        imgui.TextUnformatted("ray_reconstruction_active");
        imgui.TableNextColumn();
        imgui.Text("%s", lite.ray_reconstruction_active ? "true" : "false");

        imgui.TableNextRow();
        imgui.TableNextColumn();
        imgui.TextUnformatted("fg_mode (this tab draw)");
        imgui.TableNextColumn();
        imgui.Text("%d — %s", lite.fg_mode, lite_fg_str.c_str());

        imgui.TableNextRow();
        imgui.TableNextColumn();
        imgui.TextUnformatted("fg_mode (last HandleFpsLimiterPre)");
        imgui.TableNextColumn();
        imgui.Text("%d — %s", fg_at_last_pre, pre_fg_str.c_str());

        imgui.EndTable();
    }

    imgui.Spacing();
    imgui.TextUnformatted("FPS limiter state");
    if (imgui.BeginTable("fps_limiter_state", kCols, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        imgui.TableSetupColumn("Field");
        imgui.TableSetupColumn("Value");
        imgui.TableHeadersRow();

        const float native_stored = g_fps_limiter_debug_target_fps_native.load(std::memory_order_relaxed);
        const float effective_stored = g_fps_limiter_debug_target_fps_effective.load(std::memory_order_relaxed);
        const float target_now = GetTargetFps();
        const bool aware =
            g_fps_limiter_debug_frame_generation_aware.load(std::memory_order_relaxed) != 0;

        imgui.TableNextRow();
        imgui.TableNextColumn();
        imgui.TextUnformatted("s_fps_limiter_enabled");
        imgui.TableNextColumn();
        imgui.Text("%s", s_fps_limiter_enabled.load(std::memory_order_relaxed) ? "true" : "false");

        imgui.TableNextRow();
        imgui.TableNextColumn();
        imgui.TextUnformatted("s_fps_limiter_mode");
        imgui.TableNextColumn();
        imgui.TextUnformatted(FpsLimiterModeLabel(s_fps_limiter_mode.load(std::memory_order_relaxed)));

        imgui.TableNextRow();
        imgui.TableNextColumn();
        imgui.TextUnformatted("chosen FPS site");
        imgui.TableNextColumn();
        imgui.TextUnformatted(GetChosenFpsLimiterSiteName());

        imgui.TableNextRow();
        imgui.TableNextColumn();
        imgui.TextUnformatted("GetTargetFps() now");
        imgui.TableNextColumn();
        imgui.Text("%.3f (settings / background)", static_cast<double>(target_now));

        imgui.TableNextRow();
        imgui.TableNextColumn();
        imgui.TextUnformatted("target_fps_native (last pre)");
        imgui.TableNextColumn();
        imgui.Text("%.3f", static_cast<double>(native_stored));

        imgui.TableNextRow();
        imgui.TableNextColumn();
        imgui.TextUnformatted("target_fps_effective (last pre)");
        imgui.TableNextColumn();
        imgui.Text("%.3f (after FG divide if aware)", static_cast<double>(effective_stored));

        imgui.TableNextRow();
        imgui.TableNextColumn();
        imgui.TextUnformatted("frame_generation_aware (last pre)");
        imgui.TableNextColumn();
        imgui.Text("%s", aware ? "true" : "false");

        imgui.EndTable();
    }

    imgui.Spacing();
    imgui.TextUnformatted("OnPresentSync pacing (last frame; ns from limiter)");
    if (imgui.BeginTable("fps_limiter_sleep", kCols, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        imgui.TableSetupColumn("Field");
        imgui.TableSetupColumn("Value");
        imgui.TableHeadersRow();

        const LONGLONG ft_ns = g_onpresent_sync_frame_time_ns.load(std::memory_order_relaxed);
        const LONGLONG pre_ns = g_onpresent_sync_pre_sleep_ns.load(std::memory_order_relaxed);
        const LONGLONG post_ns = g_onpresent_sync_post_sleep_ns.load(std::memory_order_relaxed);
        const float bias = g_onpresent_sync_delay_bias.load(std::memory_order_relaxed);

        imgui.TableNextRow();
        imgui.TableNextColumn();
        imgui.TextUnformatted("frame_time_ns");
        imgui.TableNextColumn();
        imgui.Text("%lld (%.3f ms)", static_cast<long long>(ft_ns), static_cast<double>(ft_ns) * 1e-6);

        imgui.TableNextRow();
        imgui.TableNextColumn();
        imgui.TextUnformatted("pre_present sleep (actual)");
        imgui.TableNextColumn();
        imgui.Text("%lld (%.3f ms)", static_cast<long long>(pre_ns), static_cast<double>(pre_ns) * 1e-6);

        imgui.TableNextRow();
        imgui.TableNextColumn();
        imgui.TextUnformatted("post_present sleep (actual)");
        imgui.TableNextColumn();
        imgui.Text("%lld (%.3f ms)", static_cast<long long>(post_ns), static_cast<double>(post_ns) * 1e-6);

        imgui.TableNextRow();
        imgui.TableNextColumn();
        imgui.TextUnformatted("delay_bias");
        imgui.TableNextColumn();
        imgui.Text("%.4f", static_cast<double>(bias));

        imgui.EndTable();
    }

    imgui.Spacing();
    imgui.TextUnformatted("Call rate (~0.5s sliding window)");

    static uint64_t s_last_pre = 0;
    static uint64_t s_last_active = 0;
    static uint64_t s_last_post = 0;
    static LONGLONG s_last_sample_ns = 0;
    static double s_rate_pre = -1.0;
    static double s_rate_active = -1.0;
    static double s_rate_post = -1.0;

    const LONGLONG now_ns = utils::get_now_ns();
    const uint64_t pre_total = g_fps_limiter_debug_pre_entry_count.load(std::memory_order_relaxed);
    const uint64_t active_total = g_fps_limiter_debug_pre_active_count.load(std::memory_order_relaxed);
    const uint64_t post_total = g_fps_limiter_debug_post_entry_count.load(std::memory_order_relaxed);

    if (s_last_sample_ns == 0) {
        s_last_sample_ns = now_ns;
        s_last_pre = pre_total;
        s_last_active = active_total;
        s_last_post = post_total;
    } else {
        const LONGLONG dt_ns = now_ns - s_last_sample_ns;
        constexpr LONGLONG kWindowNs = 500'000'000LL;
        if (dt_ns >= kWindowNs) {
            const double dt_s = static_cast<double>(dt_ns) * 1e-9;
            if (dt_s > 0.0) {
                s_rate_pre = static_cast<double>(pre_total - s_last_pre) / dt_s;
                s_rate_active = static_cast<double>(active_total - s_last_active) / dt_s;
                s_rate_post = static_cast<double>(post_total - s_last_post) / dt_s;
            }
            s_last_sample_ns = now_ns;
            s_last_pre = pre_total;
            s_last_active = active_total;
            s_last_post = post_total;
        }
    }

    if (imgui.BeginTable("fps_limiter_rates", kCols, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        imgui.TableSetupColumn("Counter");
        imgui.TableSetupColumn("Rate");
        imgui.TableHeadersRow();
        DrawRatesRow(imgui, "HandleFpsLimiterPre entries", s_rate_pre);
        DrawRatesRow(imgui, "HandleFpsLimiterPre active body", s_rate_active);
        DrawRatesRow(imgui, "HandleFpsLimiterPost (after warmup)", s_rate_post);
        imgui.EndTable();
    }

    imgui.Spacing();
    imgui.TextWrapped(
        "Totals: pre=%" PRIu64 " active=%" PRIu64 " post=%" PRIu64 " (raw fetch_add counts).",
        static_cast<unsigned long long>(pre_total), static_cast<unsigned long long>(active_total),
        static_cast<unsigned long long>(post_total));
}

}  // namespace ui::new_ui::debug
