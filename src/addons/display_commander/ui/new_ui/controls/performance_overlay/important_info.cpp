// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
// Headers <Display Commander>
#include "performance_overlay_internal.hpp"
#include "hooks/nvidia/ngx_hooks.hpp"
#include "latent_sync/refresh_rate_monitor_integration.hpp"
#include "nvapi/gpu_dynamic_utilization.hpp"
#include "nvapi/nvapi_init.hpp"
#include "swapchain_events.hpp"
#include "ui/forkawesome.h"
#include "../../settings_wrapper.hpp"
#include "utils.hpp"

namespace ui::new_ui {

namespace {

static void DrawImportantInfo_OverlayControls(display_commander::ui::IImGuiWrapper& imgui);
// Draw DXGI overlay subsection (show DXGI VRR status, show DXGI refresh rate). Uses RefreshRateMonitor when
// enable_dxgi_refresh_rate_vrr_detection is on (Debug DXGI refresh tab in -DebugTabs builds, or config). Checkboxes are
// disabled when that setting is off.
static void DrawDxgiOverlaySubsection(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Columns(1);
    imgui.Separator();
    imgui.TextUnformatted("Refresh Rate");
    imgui.Columns(4, "overlay_checkboxes", false);

    const bool dxgi_detection_enabled =
        settings::g_advancedTabSettings.enable_dxgi_refresh_rate_vrr_detection.GetValue();
    if (!dxgi_detection_enabled) {
        imgui.BeginDisabled();
    }

    bool show_dxgi_vrr_status = settings::g_mainTabSettings.show_dxgi_vrr_status.GetValue();
    if (imgui.Checkbox("VRR status", &show_dxgi_vrr_status)) {
        settings::g_mainTabSettings.show_dxgi_vrr_status.SetValue(show_dxgi_vrr_status);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Shows DXGI-based VRR status in the performance overlay (RefreshRateMonitor heuristic: rate spread / "
            "samples below threshold). Enable \"DXGI refresh rate / VRR detection\" in the Debug DXGI refresh tab "
            "(-DebugTabs build) or via addon config for data.");
    }
    imgui.NextColumn();

    bool show_dxgi_refresh_rate = settings::g_mainTabSettings.show_dxgi_refresh_rate.GetValue();
    if (imgui.Checkbox("Refresh Rate", &show_dxgi_refresh_rate)) {
        settings::g_mainTabSettings.show_dxgi_refresh_rate.SetValue(show_dxgi_refresh_rate);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Shows DXGI refresh rate (Hz) in the performance overlay from swap chain GetFrameStatistics. "
            "Enable \"DXGI refresh rate / VRR detection\" in the Debug DXGI refresh tab (-DebugTabs build) or via addon "
            "config for data.");
    }
    imgui.NextColumn();

    if (!dxgi_detection_enabled) {
        imgui.EndDisabled();
    }

    imgui.Columns(1);

    if (dxgi_detection_enabled) {
        dxgi::fps_limiter::RefreshRateStats dxgi_stats = dxgi::fps_limiter::GetRefreshRateStats();
        double dxgi_hz = dxgi::fps_limiter::GetSmoothedRefreshRate();
        if (dxgi_stats.is_valid || dxgi_hz > 0.0) {
            imgui.Spacing();
            if (dxgi_stats.is_valid) {
                if (dxgi_stats.all_last_20_within_1s && dxgi_stats.samples_below_threshold_last_10s >= 2) {
                    imgui.TextColored(ui::colors::TEXT_SUCCESS, "VRR: On");
                } else {
                    imgui.TextColored(ui::colors::TEXT_DIMMED, "VRR: Off");
                }
                imgui.SameLine(0.0f, imgui.GetStyle().ItemInnerSpacing.x * 2.0f);
            }
            if (dxgi_hz > 0.0) {
                imgui.Text("Refresh rate: %.1f Hz (min %.1f / max %.1f)", dxgi_hz, dxgi_stats.min_rate,
                           dxgi_stats.max_rate);
            } else {
                imgui.TextColored(ui::colors::TEXT_DIMMED, "Refresh rate: -- Hz");
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Current DXGI refresh rate from swap chain. Enable \"Show DXGI VRR status\" / \"Show DXGI refresh "
                    "rate\" above to show in overlay.");
            }
        }
    }
}

// Draw NVAPI stats subsection (5 checkboxes + warning + refresh poll slider). Whole subsection is disabled when NVAPI
// is not initialized. (Optional NVAPI overlay stats remain 64-bit build only via is_64_bit().)
static void DrawNvapiStatsOverlaySubsection(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Columns(1);
    imgui.Separator();
    imgui.TextWrapped(
        "NVIDIA API stats");
    imgui.Columns(4, "overlay_checkboxes", false);

    const bool nvapi_initialized = nvapi::EnsureNvApiInitialized();
    const bool nvapi_stats_available = nvapi_initialized && is_64_bit();
    if (!nvapi_stats_available) {
        imgui.BeginDisabled();
    }

    #if 0
    bool show_vrr_status = settings::g_mainTabSettings.show_vrr_status.GetValue();
    if (imgui.Checkbox("VRR Status", &show_vrr_status)) {
        settings::g_mainTabSettings.show_vrr_status.SetValue(show_vrr_status);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Shows whether Variable Refresh Rate (VRR) is active in the performance overlay. "
            "Uses NVAPI (NVIDIA only; may cause occasional hiccups).");
    }
    imgui.NextColumn();

    bool vrr_debug_mode = settings::g_mainTabSettings.vrr_debug_mode.GetValue();
    if (imgui.Checkbox("VRR Debug Mode", &vrr_debug_mode)) {
        settings::g_mainTabSettings.vrr_debug_mode.SetValue(vrr_debug_mode);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Shows detailed VRR debugging in the performance overlay: Fixed Hz, Threshold, Samples, and NVAPI "
            "fields from NvAPI_Disp_GetVRRInfo (NV_GET_VRR_INFO):\n"
            "  enabled: VRR is enabled for the display (driver/app has enabled it).\n"
            "  req: VRR has been requested (e.g. by the application or swap chain).\n"
            "  poss: The display and link support VRR (capability).\n"
            "  in_mode: The display is currently operating in VRR mode (authoritative hardware state).\n"
            "Uses NVAPI (NVIDIA only; may cause occasional hiccups).");
    }
    imgui.NextColumn();
    #endif

    bool show_overlay_nvapi_gpu_util = settings::g_mainTabSettings.show_overlay_nvapi_gpu_util.GetValue();
    if (imgui.Checkbox("GPU util", &show_overlay_nvapi_gpu_util)) {
        settings::g_mainTabSettings.show_overlay_nvapi_gpu_util.SetValue(show_overlay_nvapi_gpu_util);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Shows NVIDIA GPU engine busy %% in the performance overlay (NvAPI_GPU_GetDynamicPstatesInfoEx; "
            "driver-reported ~1 s rolling average). Uses the first enumerated physical GPU. "
            "Uses NVAPI (NVIDIA only; may cause occasional hiccups).");
    }
    imgui.NextColumn();

   /*/ bool show_nvapi_latency_stats = settings::g_mainTabSettings.show_nvapi_latency_stats.GetValue();
    if (imgui.Checkbox("Latency PCL(AV)", &show_nvapi_latency_stats)) {
        settings::g_mainTabSettings.show_nvapi_latency_stats.SetValue(show_nvapi_latency_stats);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Shows NVIDIA Reflex NVAPI latency stats (PC latency and GPU frame time) in the performance overlay.\n"
            "Requires a D3D11/D3D12 device with Reflex latency reporting available. Uses NvAPI_D3D_GetLatency, "
            "which may add minor overhead when enabled.");
    }*/

    imgui.Columns(1);

    if (!nvapi_stats_available) {
        imgui.EndDisabled();
    }
}

static void DrawImportantInfo_FpsCounterAndReset(display_commander::ui::IImGuiWrapper& imgui);
static void DrawImportantInfo_FrameTimeGraphContent(display_commander::ui::IImGuiWrapper& imgui);
static void DrawImportantInfo_FpsCounterAndReset(display_commander::ui::IImGuiWrapper& imgui) {
    std::string local_text;
    auto shared_text = ::g_perf_text_shared.load();
    if (shared_text) {
        local_text = *shared_text;
    }
    imgui.TextUnformatted(local_text.c_str());
    imgui.PushStyleColor(ImGuiCol_Text, ui::colors::ICON_ACTION);
    if (imgui.Button(ICON_FK_REFRESH " Reset Stats")) {
        ::g_perf_reset_requested.store(true, std::memory_order_release);
    }
    imgui.PopStyleColor();
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Reset FPS/frametime statistics. Metrics are computed since reset.");
    }
}

static void DrawImportantInfo_FrameTimeGraphContent(display_commander::ui::IImGuiWrapper& imgui) {
    if (settings::g_mainTabSettings.gpu_measurement_enabled.GetValue() != 0) {
        const char* failure_reason = ::g_gpu_fence_failure_reason.load();
        if (failure_reason != nullptr) {
            imgui.Indent();
            imgui.PushStyleColor(ImGuiCol_Text, ui::colors::ICON_ERROR);
            imgui.TextUnformatted(ICON_FK_WARNING);
            imgui.PopStyleColor();
            imgui.SameLine();
            imgui.TextColored(ui::colors::TEXT_ERROR, "GPU Fence Failed: %s", failure_reason);
            imgui.Unindent();
        } else {
            imgui.Indent();
            imgui.PushStyleColor(ImGuiCol_Text, ui::colors::ICON_SUCCESS);
            imgui.TextUnformatted(ICON_FK_OK);
            imgui.PopStyleColor();
            imgui.SameLine();
            imgui.TextColored(ui::colors::TEXT_SUCCESS, "GPU Fence Active");
            imgui.Unindent();
        }
    }

    imgui.Spacing();
    DrawFrameTimeGraph(imgui);

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    DrawFrameTimelineBar(imgui);

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    imgui.Text("Native Frame Time Graph");
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Shows frame times for frames actually displayed via native swapchain Present when limit real frames "
            "is enabled.");
    }
    imgui.Spacing();

    DrawNativeFrameTimeGraph(imgui);

    imgui.Spacing();

    std::ostringstream oss;

    oss << "Present Duration: " << std::fixed << std::setprecision(3)
        << (1.0 * ::g_present_duration_ns.load() / utils::NS_TO_MS) << " ms";
    imgui.TextUnformatted(oss.str().c_str());
    imgui.SameLine();
    imgui.TextColored(ui::colors::TEXT_VALUE, "(smoothed)");

    oss.str("");
    oss.clear();
    oss << "Frame Duration: " << std::fixed << std::setprecision(3)
        << (1.0 * ::g_frame_time_ns.load() / utils::NS_TO_MS) << " ms";
    imgui.TextUnformatted(oss.str().c_str());
    imgui.SameLine();
    imgui.TextColored(ui::colors::TEXT_VALUE, "(smoothed)");

    if (settings::g_mainTabSettings.gpu_measurement_enabled.GetValue() != 0 && ::g_gpu_duration_ns.load() > 0) {
        oss.str("");
        oss.clear();
        oss << "GPU Duration: " << std::fixed << std::setprecision(3)
            << (1.0 * ::g_gpu_duration_ns.load() / utils::NS_TO_MS) << " ms";
        imgui.TextUnformatted(oss.str().c_str());
        imgui.SameLine();
        imgui.TextColored(ui::colors::TEXT_VALUE, "(smoothed)");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Time from Present call to GPU completion (D3D11 only, requires Windows 10+)");
        }

        if (::g_sim_to_display_latency_ns.load() > 0) {
            oss.str("");
            oss.clear();
            oss << "Sim-to-Display Latency: " << std::fixed << std::setprecision(3)
                << (1.0 * ::g_sim_to_display_latency_ns.load() / utils::NS_TO_MS) << " ms";
            imgui.TextUnformatted(oss.str().c_str());
            imgui.SameLine();
            imgui.TextColored(ui::colors::TEXT_VALUE, "(smoothed)");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Time from simulation start to frame displayed (includes GPU work and present)");
            }

            oss.str("");
            oss.clear();
            oss << "GPU Late Time: " << std::fixed << std::setprecision(3)
                << (1.0 * ::g_gpu_late_time_ns.load() / utils::NS_TO_MS) << " ms";
            imgui.TextUnformatted(oss.str().c_str());
            imgui.SameLine();
            imgui.TextColored(ui::colors::TEXT_VALUE, "(smoothed)");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "How much later GPU completion finishes compared to Present\n0 ms = GPU finished before "
                    "Present\n>0 ms = GPU finished after Present (GPU is late)");
            }
        }
    }

    oss.str("");
    oss.clear();
    oss << "Simulation Duration: " << std::fixed << std::setprecision(3)
        << (1.0 * ::g_simulation_duration_ns.load() / utils::NS_TO_MS) << " ms";
    imgui.TextUnformatted(oss.str().c_str());
    imgui.SameLine();
    imgui.TextColored(ui::colors::TEXT_VALUE, "(smoothed)");

    oss.str("");
    oss.clear();
    oss << "Render Submit Duration: " << std::fixed << std::setprecision(3)
        << (1.0 * ::g_render_submit_duration_ns.load() / utils::NS_TO_MS) << " ms";
    imgui.TextUnformatted(oss.str().c_str());
    imgui.SameLine();
    imgui.TextColored(ui::colors::TEXT_VALUE, "(smoothed)");

    oss.str("");
    oss.clear();
    oss << "Reshade Overhead Duration: " << std::fixed << std::setprecision(3)
        << ((1.0 * ::g_reshade_overhead_duration_ns.load() - ::fps_sleep_before_on_present_ns.load()
             - ::fps_sleep_after_on_present_ns.load())
            / utils::NS_TO_MS)
        << " ms";
    imgui.TextUnformatted(oss.str().c_str());
    imgui.SameLine();
    imgui.TextColored(ui::colors::TEXT_VALUE, "(smoothed)");

    oss.str("");
    oss.clear();
    oss << "FPS Limiter Sleep Duration (before onPresent): " << std::fixed << std::setprecision(3)
        << (1.0 * ::fps_sleep_before_on_present_ns.load() / utils::NS_TO_MS) << " ms";
    imgui.TextUnformatted(oss.str().c_str());
    imgui.SameLine();
    imgui.TextColored(ui::colors::TEXT_VALUE, "(smoothed)");

    oss.str("");
    oss.clear();
    oss << "FPS Limiter Sleep Duration (after onPresent): " << std::fixed << std::setprecision(3)
        << (1.0 * ::fps_sleep_after_on_present_ns.load() / utils::NS_TO_MS) << " ms";
    imgui.TextUnformatted(oss.str().c_str());
    imgui.SameLine();
    imgui.TextColored(ui::colors::TEXT_VALUE, "(smoothed)");

    oss.str("");
    oss.clear();
    float current_fps = 0.0f;
    const uint32_t head = ::g_perf_ring.GetHead();
    const uint32_t count = ::g_perf_ring.GetCountFromHead(head);
    if (count > 0) {
        const ::PerfSample last_sample = ::g_perf_ring.GetSampleWithHead(0, head);
        if (last_sample.dt > 0.0f) current_fps = 1.0f / last_sample.dt;
    }

    if (current_fps > 0.0f) {
        float frame_time_ms = 1000.0f / current_fps;
        float sleep_duration_ms = static_cast<float>(::fps_sleep_after_on_present_ns.load()) / utils::NS_TO_MS;
        float latency_ms = frame_time_ms - sleep_duration_ms;

        static double sim_start_to_present_latency_ms = 0.0;
        sim_start_to_present_latency_ms = (sim_start_to_present_latency_ms * 0.99 + latency_ms * 0.01);
        oss << "Sim Start to Present Latency: " << std::fixed << std::setprecision(3) << sim_start_to_present_latency_ms
            << " ms";
        imgui.TextUnformatted(oss.str().c_str());
        imgui.SameLine();
        imgui.TextColored(ui::colors::TEXT_HIGHLIGHT, "(frame_time - sleep_duration)");
    }
}

static void DrawImportantInfo_OverlayControls(display_commander::ui::IImGuiWrapper& imgui) {
    // Test Overlay Control
    {
        bool show_performance_overlay = settings::g_mainTabSettings.show_performance_overlay.GetValue();
        if (imgui.Checkbox(ICON_FK_SEARCH " Show performance overlay", &show_performance_overlay)) {
            settings::g_mainTabSettings.show_performance_overlay.SetValue(show_performance_overlay);
            LogInfo("Performance overlay %s", show_performance_overlay ? "enabled" : "disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Shows a performance monitoring widget in the main ReShade overlay with frame time graph, "
                "FPS counter, and other performance metrics. Demonstrates reshade_overlay event usage.");
        }

        (void)RadioSettingEnumWrapper(
            settings::g_mainTabSettings.overlay_label_mode, "Overlay labels",
            "How metric names appear in the performance overlay: no prefix, short tokens, or full phrases. Values "
            "use a second column when labels are on.",
            imgui, RadioSettingLayout::Horizontal);

        imgui.Separator();

        imgui.TextUnformatted("FPS & core display");
        imgui.Columns(4, "overlay_checkboxes", false);

        bool show_playtime = settings::g_mainTabSettings.show_playtime.GetValue();
        if (imgui.Checkbox("Playtime", &show_playtime)) {
            settings::g_mainTabSettings.show_playtime.SetValue(show_playtime);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Shows total playtime (time from game start).");
        }
        imgui.NextColumn();

        bool show_fps_counter = settings::g_mainTabSettings.show_fps_counter.GetValue();
        if (imgui.Checkbox("Present FPS", &show_fps_counter)) {
            settings::g_mainTabSettings.show_fps_counter.SetValue(show_fps_counter);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Shows the FPS counter representing frames actually displayed on the screen.");
        }
        imgui.NextColumn();

        bool show_fps_limiter_late_frames_pct = settings::g_mainTabSettings.show_fps_limiter_late_frames_pct.GetValue();
        if (imgui.Checkbox("Late frames %", &show_fps_limiter_late_frames_pct)) {
            settings::g_mainTabSettings.show_fps_limiter_late_frames_pct.SetValue(show_fps_limiter_late_frames_pct);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Shows the percentage of recent frames where OnPresentSync FPS limiter started late. "
                "Computed from 0.1 second buckets (30s history), showing only the last 5 seconds.");
        }
        imgui.NextColumn();

        bool show_native_fps = settings::g_mainTabSettings.show_native_fps.GetValue();
        const bool native_reflex_active = IsNativeReflexActive();
        if (!native_reflex_active) {
            imgui.BeginDisabled();
        }
        if (imgui.Checkbox("Native FPS", &show_native_fps)) {
            settings::g_mainTabSettings.show_native_fps.SetValue(show_native_fps);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Shows the fps counter representing frames rendered by the game.");
        }
        if (!native_reflex_active) {
            imgui.EndDisabled();
        }
        imgui.NextColumn();

        bool show_overlay_resolution = settings::g_mainTabSettings.show_overlay_resolution.GetValue();
        if (imgui.Checkbox("Resolution", &show_overlay_resolution)) {
            settings::g_mainTabSettings.show_overlay_resolution.SetValue(show_overlay_resolution);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Shows render resolution in the performance overlay. When DLSS is active, shows internal render size "
                "and swapchain/backbuffer (e.g. 1920x1080->3840x2160). When DLSS is off, shows the tracked "
                "backbuffer size only.");
        }
        imgui.NextColumn();

        bool show_flip_status = settings::g_mainTabSettings.show_flip_status.GetValue();
        if (imgui.Checkbox("Presentation model", &show_flip_status)) {
            settings::g_mainTabSettings.show_flip_status.SetValue(show_flip_status);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Shows current swapchain presentation model in the performance overlay "
                "(Flip/BitBlt for DXGI, FLIPEX for D3D9, Vulkan/OpenGL present mode where available).");
        }
        imgui.NextColumn();

#if !defined(DC_LITE)
        const bool present_mon_etw_on = settings::g_mainTabSettings.present_mon_etw_enabled.GetValue();
        if (!present_mon_etw_on) {
            imgui.BeginDisabled();
        }
        bool show_overlay_presentmon_flip = settings::g_mainTabSettings.show_overlay_presentmon_flip.GetValue();
        if (imgui.Checkbox("Flip state", &show_overlay_presentmon_flip)) {
            settings::g_mainTabSettings.show_overlay_presentmon_flip.SetValue(show_overlay_presentmon_flip);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Shows Win32k ETW composition/flip classification in the performance overlay (same source as Flip state "
                "under VSync & Tearing). This is not the same as Presentation model, which reads the swapchain API. "
                "Enable PresentMon ETW (flip state) under Display Settings → VSync & Tearing for live data.");
        }
        if (!present_mon_etw_on) {
            imgui.EndDisabled();
        }
        imgui.NextColumn();
#endif

        imgui.Columns(1);
        imgui.Separator();
        #if 0
        imgui.TextUnformatted("CPU / limiter");
        imgui.Columns(4, "overlay_checkboxes", false);
        bool show_cpu_usage = settings::g_mainTabSettings.show_cpu_usage.GetValue();
        if (imgui.Checkbox("Cpu busy", &show_cpu_usage)) {
            settings::g_mainTabSettings.show_cpu_usage.SetValue(show_cpu_usage);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "100%% minus the %% of frame time the FPS limiter spends sleeping. "
                "Not actual CPU usage: measures how much headroom the game has. 100%% = CPU limited.");
        }
        imgui.NextColumn();
        bool show_fps_limiter_src = settings::g_mainTabSettings.show_fps_limiter_src.GetValue();
        if (imgui.Checkbox("FPS limiter", &show_fps_limiter_src)) {
            settings::g_mainTabSettings.show_fps_limiter_src.SetValue(show_fps_limiter_src);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Shows which hook is currently applying the FPS limiter in the performance overlay (same text as "
                "FPS limiter source on the Main tab).");
        }
        imgui.NextColumn();

        imgui.Columns(1);
        imgui.Separator();
        #endif
        imgui.TextUnformatted("DLSS / NGX");
        imgui.Columns(4, "overlay_checkboxes", false);

        const DLSSGSummary dlss_overlay_summary = GetDLSSGSummary();
        const bool dlss_ngx_seen =
            dlss_overlay_summary.any_dlss_was_active_once || dlss_overlay_summary.any_dlss_dll_loaded;
        if (!dlss_ngx_seen) {
            imgui.BeginDisabled();
        }

        bool show_fg_mode = settings::g_mainTabSettings.show_fg_mode.GetValue();
        if (imgui.Checkbox("FG Mode", &show_fg_mode)) {
            settings::g_mainTabSettings.show_fg_mode.SetValue(show_fg_mode);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Shows DLSS Frame Generation mode (OFF / 2x / 3x / 4x) in the performance overlay.%s",
                               dlss_ngx_seen ? "" : " Requires a game that uses DLSS/NGX.");
        }
        imgui.NextColumn();

        bool show_dlss_status = settings::g_mainTabSettings.show_dlss_status.GetValue();
        if (imgui.Checkbox("DLSS Status", &show_dlss_status)) {
            settings::g_mainTabSettings.show_dlss_status.SetValue(show_dlss_status);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Shows DLSS on/off status in the performance overlay.");
        }
        imgui.NextColumn();

        bool show_dlss_quality_preset = settings::g_mainTabSettings.show_dlss_quality_preset.GetValue();
        imgui.SetNextItemWidth(300.0f);
        if (imgui.Checkbox("DLSS Quality Preset", &show_dlss_quality_preset)) {
            settings::g_mainTabSettings.show_dlss_quality_preset.SetValue(show_dlss_quality_preset);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Shows DLSS quality preset (Performance, Balanced, Quality, Ultra Performance, Ultra Quality, DLAA) in "
                "the performance overlay.");
        }
        imgui.NextColumn();

        if (!dlss_ngx_seen) {
            imgui.EndDisabled();
        }

#if !defined(DC_LITE)
        bool show_driver_dlss_sr_preset = settings::g_mainTabSettings.show_driver_dlss_sr_preset.GetValue();
        if (imgui.Checkbox("SR preset", &show_driver_dlss_sr_preset)) {
            settings::g_mainTabSettings.show_driver_dlss_sr_preset.SetValue(show_driver_dlss_sr_preset);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "One overlay line for **DLSS-SR render preset**: NVIDIA **driver profile** (DRS) when it overrides the "
                "default, otherwise your **Display Commander** combo value (or **Preset override off**). Tooltip lists "
                "both. Driver data is read once per session until you use **Refresh driver preset info** in DLSS Control "
                "or restart.");
        }
        imgui.NextColumn();

        bool show_driver_dlss_rr_preset = settings::g_mainTabSettings.show_driver_dlss_rr_preset.GetValue();
        if (imgui.Checkbox("RR preset", &show_driver_dlss_rr_preset)) {
            settings::g_mainTabSettings.show_driver_dlss_rr_preset.SetValue(show_driver_dlss_rr_preset);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "One overlay line for **DLSS-RR render preset**: driver profile (DRS) when it overrides, else your DC "
                "combo (or **Preset override off**). Tooltip lists both. Driver data is cached until Refresh in DLSS "
                "Control or restart.");
        }
        imgui.NextColumn();
#endif

        imgui.Columns(1);
        imgui.Separator();
        imgui.TextUnformatted("GPU & memory");
        imgui.Columns(4, "overlay_checkboxes", false);

        bool show_overlay_vram = settings::g_mainTabSettings.show_overlay_vram.GetValue();
        if (imgui.Checkbox("VRAM", &show_overlay_vram)) {
            settings::g_mainTabSettings.show_overlay_vram.SetValue(show_overlay_vram);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Shows GPU video memory used / budget (MiB) in the performance overlay (DXGI adapter).");
        }
        imgui.NextColumn();

        bool show_overlay_ram = settings::g_mainTabSettings.show_overlay_ram.GetValue();
        if (imgui.Checkbox("RAM", &show_overlay_ram)) {
            settings::g_mainTabSettings.show_overlay_ram.SetValue(show_overlay_ram);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Shows system RAM in use and this process working set (MiB) in the performance overlay "
                "(GlobalMemoryStatusEx, GetProcessMemoryInfo).");
        }
        imgui.NextColumn();

        const bool smooth_motion_latency = g_smooth_motion_dll_loaded.load(std::memory_order_relaxed);
        if (smooth_motion_latency) {
            imgui.BeginDisabled();
        }
        bool gpu_measurement = settings::g_mainTabSettings.gpu_measurement_enabled.GetValue() != 0;
        if (imgui.Checkbox("Show latency", &gpu_measurement)) {
            settings::g_mainTabSettings.gpu_measurement_enabled.SetValue(gpu_measurement ? 1 : 0);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Measures time from Present call to GPU completion using fences.\n"
                "Requires D3D11 with Windows 10+ or D3D12.\n"
                "Shows as 'GPU Duration' in the timing metrics below.");
        }
        if (smooth_motion_latency) {
            imgui.EndDisabled();
            imgui.SameLine();
            imgui.TextColored(ui::colors::TEXT_WARNING, "(Disabled due to Smooth Motion)");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "nvpresent DLL is loaded; GPU completion measurement is suppressed while Smooth Motion is active.");
            }
        }
        imgui.NextColumn();

        imgui.Columns(1);
        bool show_animation_error_nvapi = settings::g_mainTabSettings.show_overlay_nvapi_latency_jitter_abs.GetValue();
        if (imgui.Checkbox("Animation error", &show_animation_error_nvapi)) {
            settings::g_mainTabSettings.show_overlay_nvapi_latency_jitter_abs.SetValue(show_animation_error_nvapi);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Computes animation error from frame-to-frame average changes in the NVAPI Reflex latency estimate "
                "(overlay line). Requires Reflex latency reporting (NvAPI_D3D_GetLatency); not Intel PresentMon "
                "MsAnimationError.");
        }

        imgui.Separator();
        imgui.TextUnformatted("Misc");
        imgui.Columns(4, "overlay_checkboxes", false);

        bool show_clock = settings::g_mainTabSettings.show_clock.GetValue();
        if (imgui.Checkbox("Show clock", &show_clock)) {
            settings::g_mainTabSettings.show_clock.SetValue(show_clock);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Shows the current time (HH:MM:SS) in the overlay.");
        }
        imgui.NextColumn();
#if 0
        bool show_volume = settings::g_experimentalTabSettings.show_volume.GetValue();
        if (imgui.Checkbox("Show volume", &show_volume)) {
            settings::g_experimentalTabSettings.show_volume.SetValue(show_volume);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Shows the current audio volume percentage in the overlay.");
        }
        imgui.NextColumn();
#endif
        DrawNvapiStatsOverlaySubsection(imgui);
        DrawDxgiOverlaySubsection(imgui);

        imgui.Spacing();
        if (SliderFloatSetting(settings::g_mainTabSettings.overlay_background_alpha, "Overlay Background Opacity",
                               "%.2f", imgui)) {
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Controls the overlay background opacity. 0.0 = fully transparent, 1.0 = fully opaque.");
        }
        if (SliderFloatSetting(settings::g_mainTabSettings.overlay_chart_alpha, "Frame Chart Opacity", "%.2f",
                               imgui)) {
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Controls opacity of the frame time and refresh rate chart backgrounds. 0.0 = fully transparent, "
                "1.0 = fully opaque. Chart lines remain fully visible.");
        }
        if (SliderFloatSetting(settings::g_mainTabSettings.overlay_graph_scale, "Graph Size Scale", "%.1fx", imgui)) {
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Controls the size of the frame time and refresh rate graphs in the overlay. "
                "1.0x = default size (300x60px), 4.0x = maximum size (1200x240px).");
        }
        if (SliderFloatSetting(settings::g_mainTabSettings.overlay_graph_max_scale, "Graph Max Value Scale", "%.1fx",
                               imgui)) {
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Controls the maximum Y-axis value for the frame time and refresh rate graphs. "
                "The graph will scale from 0ms to (average frame time × this multiplier). "
                "Lower values (2x-4x) show more detail for normal frame times. "
                "Higher values (6x-10x) accommodate frame time spikes without clipping.");
        }
        if (SliderFloatSetting(settings::g_mainTabSettings.overlay_vertical_spacing, "Overlay Vertical Spacing",
                               "%.0f px", imgui)) {
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Vertical offset of the performance overlay in the ReShade overlay (pixels from the top). "
                "0 aligns the window top edge with the top of the overlay area. "
                "Positive moves down, negative moves up (e.g. to clear other on-screen text).");
        }
        if (SliderFloatSetting(settings::g_mainTabSettings.overlay_horizontal_spacing, "Overlay Horizontal Spacing",
                               "%.0f px", imgui)) {
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Horizontal offset of the performance overlay in the ReShade overlay (pixels from the left). "
                "0 aligns the window left edge with the left of the overlay area. "
                "Positive moves right, negative moves left.");
        }

        imgui.Separator();
        imgui.TextUnformatted("Frame timing & graphs");
        imgui.Columns(4, "overlay_checkboxes", false);

        bool show_frame_time_graph = settings::g_mainTabSettings.show_frame_time_graph.GetValue();
        if (imgui.Checkbox("Show frame time graph", &show_frame_time_graph)) {
            settings::g_mainTabSettings.show_frame_time_graph.SetValue(show_frame_time_graph);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Shows a graph of frame times in the overlay.");
        }
        imgui.NextColumn();

        bool show_frame_time_stats = settings::g_mainTabSettings.show_frame_time_stats.GetValue();
        if (imgui.Checkbox("Show frame time stats", &show_frame_time_stats)) {
            settings::g_mainTabSettings.show_frame_time_stats.SetValue(show_frame_time_stats);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Shows frame time statistics (avg, deviation, min, max) in the overlay.");
        }
        imgui.NextColumn();

        bool show_native_frame_time_graph = settings::g_mainTabSettings.show_native_frame_time_graph.GetValue();
        if (imgui.Checkbox("Show native frame time graph", &show_native_frame_time_graph)) {
            settings::g_mainTabSettings.show_native_frame_time_graph.SetValue(show_native_frame_time_graph);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Shows a graph of native frame times (frames shown to display via native swapchain Present) in the "
                "overlay.\nOnly available when limit real frames is enabled.");
        }
        imgui.NextColumn();

        bool show_frame_timeline_bar = settings::g_mainTabSettings.show_frame_timeline_bar.GetValue();
        if (imgui.Checkbox("Show frame timeline bar", &show_frame_timeline_bar)) {
            settings::g_mainTabSettings.show_frame_timeline_bar.SetValue(show_frame_timeline_bar);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Shows a compact frame timeline in the overlay (Simulation, Render Submit, Present, etc. as bars). "
                "Updates every 1 s.");
        }
        imgui.NextColumn();

        imgui.Columns(1);
    }
}

}  // namespace

void DrawImportantInfo(display_commander::ui::IImGuiWrapper& imgui) {
    (void)imgui;
    CALL_GUARD_NO_TS();
    DrawImportantInfo_OverlayControls(imgui);
    imgui.Spacing();
    DrawImportantInfo_FpsCounterAndReset(imgui);
    imgui.Spacing();

    imgui.Indent();
    g_rendering_ui_section.store("ui:tab:main_new:frame_time_graph", std::memory_order_release);
    ui::colors::PushHeader2Colors(&imgui);
    const bool frame_time_graph_open = imgui.CollapsingHeader("Frame Time Graph", ImGuiTreeNodeFlags_None);
    ui::colors::PopCollapsingHeaderColors(&imgui);
    if (frame_time_graph_open) {
        imgui.Indent();
        DrawImportantInfo_FrameTimeGraphContent(imgui);
        imgui.Unindent();
    }
    imgui.Unindent();
}

}  // namespace ui::new_ui

