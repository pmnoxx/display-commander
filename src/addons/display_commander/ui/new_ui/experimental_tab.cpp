#include "experimental_tab.hpp"
#include "../../globals.hpp"
#include "../../hooks/loadlibrary_hooks.hpp"
#include "../../hooks/system/timeslowdown_hooks.hpp"
#include "../../latency/reflex_provider.hpp"
#include "../forkawesome.h"
#include "../ui_colors.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../ui/imgui_wrapper_base.hpp"
#include "../../ui/imgui_wrapper_reshade.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/timing.hpp"
#include "../imgui_wrapper_reshade.hpp"
#include "main_new_tab.hpp"
#include "settings_wrapper.hpp"

#include <reshade.hpp>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace ui::new_ui {

namespace {

struct ReflexLatencyDebugSnapshot {
    bool query_attempted = false;
    bool has_data = false;
    uint64_t min_non_zero_ns = 0;
    std::vector<ReflexProvider::NvapiLatencyFrame> frames;
};

uint64_t GetMinNonZeroNs(const std::vector<ReflexProvider::NvapiLatencyFrame>& frames) {
    uint64_t min_ns = 0;
    for (const auto& frame : frames) {
        const uint64_t values[] = {frame.input_sample_time_ns,
                                   frame.sim_start_time_ns,
                                   frame.sim_end_time_ns,
                                   frame.render_submit_start_time_ns,
                                   frame.render_submit_end_time_ns,
                                   frame.present_start_time_ns,
                                   frame.present_end_time_ns,
                                   frame.driver_start_time_ns,
                                   frame.driver_end_time_ns,
                                   frame.os_render_queue_start_time_ns,
                                   frame.os_render_queue_end_time_ns,
                                   frame.gpu_render_start_time_ns,
                                   frame.gpu_render_end_time_ns};
        for (const uint64_t value : values) {
            if (value == 0) {
                continue;
            }
            min_ns = (min_ns == 0) ? value : (std::min)(min_ns, value);
        }
    }
    return min_ns;
}

void DrawRelativeNsOrNa(display_commander::ui::IImGuiWrapper& imgui, uint64_t value_ns, uint64_t min_non_zero_ns) {
    if (value_ns == 0 || min_non_zero_ns == 0 || value_ns < min_non_zero_ns) {
        imgui.TextUnformatted("N/A");
        return;
    }
    imgui.Text("%llu", static_cast<unsigned long long>(value_ns - min_non_zero_ns));
}

void RefreshReflexLatencySnapshot(ReflexLatencyDebugSnapshot& snapshot) {
    snapshot.query_attempted = true;
    snapshot.has_data = false;
    snapshot.min_non_zero_ns = 0;
    snapshot.frames.clear();

    if (!g_reflexProvider) {
        return;
    }

    std::vector<ReflexProvider::NvapiLatencyFrame> frames;
    if (!g_reflexProvider->GetRecentLatencyFrames(frames, 10)) {
        return;
    }

    snapshot.frames = std::move(frames);
    snapshot.min_non_zero_ns = GetMinNonZeroNs(snapshot.frames);
    snapshot.has_data = !snapshot.frames.empty();
}

void DrawReflexLatencyLastFramesSection(display_commander::ui::IImGuiWrapper& imgui) {
    if (!imgui.CollapsingHeader("Reflex Last 10 Frames (GetLatencyMetrics)",
                                display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
        return;
    }

    static ReflexLatencyDebugSnapshot s_snapshot{};
    if (!s_snapshot.query_attempted) {
        RefreshReflexLatencySnapshot(s_snapshot);
    }

    imgui.Indent();
    if (imgui.Button("Refresh")) {
        RefreshReflexLatencySnapshot(s_snapshot);
    }
    imgui.SameLine();
    imgui.TextColored(ui::colors::TEXT_DIMMED, "Values are relative to min non-zero timestamp (ns).");

    if (!g_reflexProvider || !g_reflexProvider->IsInitialized()) {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "Reflex provider is not initialized.");
        imgui.Unindent();
        return;
    }

    if (!s_snapshot.has_data) {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "No latency frame data available.");
        imgui.Unindent();
        return;
    }

    if (imgui.BeginTable("##reflex_last_10_frames", 15,
                         display_commander::ui::wrapper_flags::TableFlags_RowBg
                             | display_commander::ui::wrapper_flags::TableFlags_Borders
                             | display_commander::ui::wrapper_flags::TableFlags_SizingFixedFit
                             | display_commander::ui::wrapper_flags::TableFlags_ScrollX
                             | display_commander::ui::wrapper_flags::TableFlags_ScrollY,
                         ImVec2{0.0f, 260.0f})) {
        imgui.TableSetupColumn("FrameID");
        imgui.TableSetupColumn("Input");
        imgui.TableSetupColumn("SimStart");
        imgui.TableSetupColumn("SimEnd");
        imgui.TableSetupColumn("RenderStart");
        imgui.TableSetupColumn("RenderEnd");
        imgui.TableSetupColumn("PresentStart");
        imgui.TableSetupColumn("PresentEnd");
        imgui.TableSetupColumn("DriverStart");
        imgui.TableSetupColumn("DriverEnd");
        imgui.TableSetupColumn("OSQStart");
        imgui.TableSetupColumn("OSQEnd");
        imgui.TableSetupColumn("GPUStart");
        imgui.TableSetupColumn("GPUEnd");
        imgui.TableSetupColumn("GPU(us)");
        imgui.TableHeadersRow();

        for (const auto& frame : s_snapshot.frames) {
            imgui.TableNextRow();
            imgui.TableSetColumnIndex(0);
            imgui.Text("%llu", static_cast<unsigned long long>(frame.frame_id));
            imgui.TableSetColumnIndex(1);
            DrawRelativeNsOrNa(imgui, frame.input_sample_time_ns, s_snapshot.min_non_zero_ns);
            imgui.TableSetColumnIndex(2);
            DrawRelativeNsOrNa(imgui, frame.sim_start_time_ns, s_snapshot.min_non_zero_ns);
            imgui.TableSetColumnIndex(3);
            DrawRelativeNsOrNa(imgui, frame.sim_end_time_ns, s_snapshot.min_non_zero_ns);
            imgui.TableSetColumnIndex(4);
            DrawRelativeNsOrNa(imgui, frame.render_submit_start_time_ns, s_snapshot.min_non_zero_ns);
            imgui.TableSetColumnIndex(5);
            DrawRelativeNsOrNa(imgui, frame.render_submit_end_time_ns, s_snapshot.min_non_zero_ns);
            imgui.TableSetColumnIndex(6);
            DrawRelativeNsOrNa(imgui, frame.present_start_time_ns, s_snapshot.min_non_zero_ns);
            imgui.TableSetColumnIndex(7);
            DrawRelativeNsOrNa(imgui, frame.present_end_time_ns, s_snapshot.min_non_zero_ns);
            imgui.TableSetColumnIndex(8);
            DrawRelativeNsOrNa(imgui, frame.driver_start_time_ns, s_snapshot.min_non_zero_ns);
            imgui.TableSetColumnIndex(9);
            DrawRelativeNsOrNa(imgui, frame.driver_end_time_ns, s_snapshot.min_non_zero_ns);
            imgui.TableSetColumnIndex(10);
            DrawRelativeNsOrNa(imgui, frame.os_render_queue_start_time_ns, s_snapshot.min_non_zero_ns);
            imgui.TableSetColumnIndex(11);
            DrawRelativeNsOrNa(imgui, frame.os_render_queue_end_time_ns, s_snapshot.min_non_zero_ns);
            imgui.TableSetColumnIndex(12);
            DrawRelativeNsOrNa(imgui, frame.gpu_render_start_time_ns, s_snapshot.min_non_zero_ns);
            imgui.TableSetColumnIndex(13);
            DrawRelativeNsOrNa(imgui, frame.gpu_render_end_time_ns, s_snapshot.min_non_zero_ns);
            imgui.TableSetColumnIndex(14);
            if (frame.gpu_frame_time_us == 0) {
                imgui.TextUnformatted("N/A");
            } else {
                imgui.Text("%u", frame.gpu_frame_time_us);
            }
        }

        imgui.EndTable();
    }
    imgui.Unindent();
}

}  // namespace

// Initialize experimental tab
void InitExperimentalTab() {
    LogInfo("InitExperimentalTab() - Settings already loaded at startup");

    // Apply the loaded settings to the actual hook system
    // This ensures the hook system matches the UI settings
    LogInfo("InitExperimentalTab() - Applying loaded timer hook settings to hook system");
    display_commanderhooks::SetTimerHookTypeById(
        display_commanderhooks::TimerHookIdentifier::QueryPerformanceCounter,
        static_cast<display_commanderhooks::TimerHookType>(
            settings::g_experimentalTabSettings.query_performance_counter_hook.GetValue()));
    display_commanderhooks::SetTimerHookTypeById(
        display_commanderhooks::TimerHookIdentifier::GetTickCount,
        static_cast<display_commanderhooks::TimerHookType>(
            settings::g_experimentalTabSettings.get_tick_count_hook.GetValue()));
    display_commanderhooks::SetTimerHookTypeById(
        display_commanderhooks::TimerHookIdentifier::GetTickCount64,
        static_cast<display_commanderhooks::TimerHookType>(
            settings::g_experimentalTabSettings.get_tick_count64_hook.GetValue()));
    display_commanderhooks::SetTimerHookTypeById(
        display_commanderhooks::TimerHookIdentifier::TimeGetTime,
        static_cast<display_commanderhooks::TimerHookType>(
            settings::g_experimentalTabSettings.time_get_time_hook.GetValue()));
    display_commanderhooks::SetTimerHookTypeById(
        display_commanderhooks::TimerHookIdentifier::GetSystemTime,
        static_cast<display_commanderhooks::TimerHookType>(
            settings::g_experimentalTabSettings.get_system_time_hook.GetValue()));
    display_commanderhooks::SetTimerHookTypeById(
        display_commanderhooks::TimerHookIdentifier::GetSystemTimeAsFileTime,
        static_cast<display_commanderhooks::TimerHookType>(
            settings::g_experimentalTabSettings.get_system_time_as_file_time_hook.GetValue()));
    display_commanderhooks::SetTimerHookTypeById(
        display_commanderhooks::TimerHookIdentifier::GetSystemTimePreciseAsFileTime,
        static_cast<display_commanderhooks::TimerHookType>(
            settings::g_experimentalTabSettings.get_system_time_precise_as_file_time_hook.GetValue()));
    display_commanderhooks::SetTimerHookTypeById(
        display_commanderhooks::TimerHookIdentifier::GetLocalTime,
        static_cast<display_commanderhooks::TimerHookType>(
            settings::g_experimentalTabSettings.get_local_time_hook.GetValue()));
    display_commanderhooks::SetTimerHookTypeById(
        display_commanderhooks::TimerHookIdentifier::NtQuerySystemTime,
        static_cast<display_commanderhooks::TimerHookType>(
            settings::g_experimentalTabSettings.nt_query_system_time_hook.GetValue()));

    // Apply DirectInput hook suppression setting
    s_suppress_dinput_hooks.store(settings::g_experimentalTabSettings.suppress_dinput_hooks.GetValue());

    LogInfo("InitExperimentalTab() - Experimental tab settings loaded and applied to hook system");
}

void DrawExperimentalTab(display_commander::ui::IImGuiWrapper& imgui, reshade::api::effect_runtime* /* runtime */) {
    if (!imgui.BeginTabBar("ExperimentalSubTabs", 0)) {
        return;
    }

    if (imgui.BeginTabItem("Features", nullptr, 0)) {
        imgui.Text("Experimental Tab - Advanced Features");
        imgui.TextColored(ui::colors::TEXT_DIMMED, "Time Slowdown controls are now in the dedicated module tab.");
        imgui.Separator();

        imgui.EndTabItem();
    }

    if (imgui.BeginTabItem("Debug", nullptr, 0)) {
        DrawReflexLatencyLastFramesSection(imgui);
        imgui.EndTabItem();
    }

    imgui.EndTabBar();
}

void CleanupExperimentalTab() {}

}  // namespace ui::new_ui
