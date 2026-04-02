#include "main_new_tab.hpp"
#include "../../addon.hpp"
#include "../../adhd_multi_monitor/adhd_simple_api.hpp"
#include "../../config/default_overrides.hpp"
#include "../../config/default_settings_file.hpp"
#include "../../config/display_commander_config.hpp"
#include "../../display/hdr_control.hpp"
#include "../../dlss/dlss_indicator_manager.hpp"
#include "../../dxgi/vram_info.hpp"
#include "../../globals.hpp"
#include "../../hooks/loadlibrary_hooks.hpp"
#include "../../hooks/nvidia/ngx_hooks.hpp"
#include "../../hooks/nvidia/nvapi_hooks.hpp"
#include "../../hooks/present_traffic_tracking.hpp"
#include "../../hooks/vulkan/nvlowlatencyvk_hooks.hpp"
#include "../../hooks/vulkan/vulkan_loader_hooks.hpp"
#include "../../hooks/windows_hooks/api_hooks.hpp"
#include "../../hooks/windows_hooks/window_proc_hooks.hpp"
#include "../../hooks/windows_hooks/windows_message_hooks.hpp"
#include "../../latency/reflex_provider.hpp"
#include "../../latent_sync/latent_sync_limiter.hpp"
#include "../../latent_sync/refresh_rate_monitor_integration.hpp"
#include "../../modules/module_registry.hpp"
#include "../../nvapi/gpu_dynamic_utilization.hpp"
#include "../../nvapi/nvapi_actual_refresh_rate_monitor.hpp"
#include "../../nvapi/nvapi_init.hpp"
#include "../forkawesome.h"
#include "../ui_colors.hpp"
#include "../../settings/advanced_tab_settings.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../settings/streamline_tab_settings.hpp"
#include "../../settings/swapchain_tab_settings.hpp"
#include "../../swapchain_events.hpp"
#include "../../utils.hpp"
#include "../../utils/d3d9_api_version.hpp"
#include "../../utils/dc_load_path.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/perf_measurement.hpp"
#include "../../utils/reshade_load_path.hpp"
#include "../../widgets/resolution_widget/resolution_widget.hpp"
#include "new_ui_tabs.hpp"
#include "settings_wrapper.hpp"
#include "controls/main_tab_optional_panels.hpp"
#include "controls/performance_overlay/important_info.hpp"
#include "../../utils/detour_call_tracker.hpp"
#include "../../version.hpp"

#include "imgui.h"

#include <d3d9.h>
#include <d3d9types.h>
#include <dxgi.h>
#include <minwindef.h>
#include <psapi.h>
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
#include <cstring>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// Minimum CPU cores that can be selected (excludes 1-5)
static constexpr int MIN_CPU_CORES_SELECTABLE = 6;
// Width for FPS limit sliders and FPS Limiter Mode combo in Main tab
static constexpr float kFpsLimiterItemWidth = 600.0f;

namespace ui::new_ui {

namespace {

// Flag to indicate a restart is required after changing VSync/tearing options
std::atomic<bool> s_restart_needed_vsync_tearing{false};

// ReShade ADDON LoadFromDllMain: read once per process after g_reshade_module is set (avoids ini parse every frame).
std::atomic<bool> s_load_from_dll_main_fetched{false};
std::atomic<int32_t> s_load_from_dll_main_value{0};

/** Horizontal offset from row start to the control after a leading checkbox (ImGui checkbox square + SameLine gap). */
float GetMainTabCheckboxColumnGutter(display_commander::ui::IImGuiWrapper& imgui) {
    const ImGuiStyle& st = imgui.GetStyle();
    const float frame_h = imgui.GetTextLineHeight() + st.FramePadding.y * 2.f;
    return frame_h + st.ItemInnerSpacing.x;
}

/** Dummy + SameLine so combos/sliders align with the FPS Limit / Background FPS Limit label column.
 *  When `compensate_for_parent_indent` is true, subtract one ImGui indent level (Advanced block under
 *  DrawDisplaySettings_FpsLimiter is wrapped in Indent() vs the FPS slider rows). */
void PushFpsLimiterSliderColumnAlign(display_commander::ui::IImGuiWrapper& imgui, float checkbox_column_gutter,
                                     bool compensate_for_parent_indent = false) {
    float g = checkbox_column_gutter;
    if (compensate_for_parent_indent) {
        const float ind = imgui.GetStyle().IndentSpacing;
        g = (g > ind) ? (g - ind) : 0.0f;
    }
    if (g > 0.0f) {
        imgui.Dummy(ImVec2(g, imgui.GetTextLineHeight()));
        imgui.SameLine(0.0f, 0.0f);
    }
}

// Helper function to check if injected Reflex is active
bool DidNativeReflexSleepRecently(uint64_t now_ns) {
    auto last_injected_call = g_nvapi_last_sleep_timestamp_ns.load();
    return last_injected_call > 0 && (now_ns - last_injected_call) < utils::SEC_TO_NS;  // 1s in nanoseconds
}

// Draw native Reflex (NvLL VK) status on the same line: Status OK/FAIL + tooltip (from NvLL_VK_Sleep_Detour /
// NvLL_VK_SetLatencyMarker_Detour). Only draws when AreNvLowLatencyVkHooksInstalled(). Call after Reflex combo;
// uses SameLine so it appears next to the previous widget. Layout matches DrawDxgiNativeReflexStatusOnSameLine.
void DrawNvllNativeReflexStatusOnSameLine(display_commander::ui::IImGuiWrapper& imgui) {
    if (!AreNvLowLatencyVkHooksInstalled()) {
        return;
    }
    if (IsInjectedReflexEnabled()) {
        return;
    }
    uint64_t hook_counts[static_cast<std::size_t>(NvllVkHook::Count)] = {};
    GetNvllVkHookCallCounts(hook_counts, static_cast<std::size_t>(NvllVkHook::Count));
    const uint64_t sleep_count = hook_counts[static_cast<std::size_t>(NvllVkHook::Sleep)];
    const uint64_t marker_count = hook_counts[static_cast<std::size_t>(NvllVkHook::SetLatencyMarker)];

    uint64_t marker_by_type[kNvllVkMarkerTypeCount] = {};
    GetNvLowLatencyVkMarkerCountsByType(marker_by_type, kNvllVkMarkerTypeCount);

    const bool status_ok = (sleep_count > 0 && marker_count > 0);

    imgui.SameLine();
    if (status_ok) {
        imgui.TextColored(ui::colors::ICON_SUCCESS, "Status: OK");
    } else {
        imgui.TextColored(ui::colors::ICON_ERROR, "Status: FAIL");
    }
    if (imgui.IsItemHovered()) {
        std::ostringstream tt;
        tt << "OK - Game implements native Reflex correctly.\n"
           << "FAIL - Game didn't implement native Reflex correctly, needs fixes.\n"
           << "Sleep: " << sleep_count << "\n"
           << "Markers total: " << marker_count << "\n\n"
           << "Count (by marker type):\n";
        for (size_t i = 0; i < kNvllVkMarkerTypeCount; ++i) {
            tt << "  " << GetNvLowLatencyVkMarkerTypeName(static_cast<int>(i)) << ": ";
            if (marker_by_type[i] != 0) {
                tt << marker_by_type[i];
            } else {
                tt << "—";
            }
            tt << "\n";
        }
        imgui.SetTooltipEx("%s", tt.str().c_str());
    }
}

// Number of frames (g_global_frame_id) to consider "recent" for DXGI native Reflex status OK.
constexpr uint64_t kDxgiNativeReflexStatusFrameWindow = 50;

// Draw native Reflex (DXGI/D3D) status on the same line: Sleep count + marker count (from NvAPI_D3D_Sleep_Detour /
// NvAPI_D3D_SetLatencyMarker_Detour). Only draws when device is D3D11/D3D12 and Reflex is available. Call after Reflex
// combo; uses SameLine so it appears next to the previous widget. Shown when NvLL (Vulkan) status is not shown.
// Status OK when all 6 markers and Sleep were seen within the last kDxgiNativeReflexStatusFrameWindow frames.
void DrawDxgiNativeReflexStatusOnSameLine(display_commander::ui::IImGuiWrapper& imgui) {
    if (IsInjectedReflexEnabled()) {
        return;
    }
    const reshade::api::device_api api = g_last_reshade_device_api.load();
    if (api != reshade::api::device_api::d3d11 && api != reshade::api::device_api::d3d12) {
        return;
    }
    if (!IsReflexAvailable()) {
        return;
    }
    const uint32_t sleep_count = g_nvapi_event_counters[NVAPI_EVENT_D3D_SLEEP].load();
    const uint32_t marker_count = g_nvapi_event_counters[NVAPI_EVENT_D3D_SET_LATENCY_MARKER].load();
    const uint64_t current_frame = g_global_frame_id.load(std::memory_order_relaxed);
    const uint64_t cutoff_frame = (current_frame >= kDxgiNativeReflexStatusFrameWindow)
                                      ? (current_frame - kDxgiNativeReflexStatusFrameWindow)
                                      : 0;

    uint64_t last_frame_by_type[static_cast<size_t>(kLatencyMarkerTypeCountFirstSix)] = {};
    int latest_type = -1;
    uint64_t latest_frame = 0;
    bool all_markers_within_window = true;
    for (size_t i = 0; i < static_cast<size_t>(kLatencyMarkerTypeCountFirstSix); ++i) {
        last_frame_by_type[i] = g_nvapi_d3d_last_global_frame_id_by_marker_type[i].load();
        if (last_frame_by_type[i] != 0 && last_frame_by_type[i] >= latest_frame) {
            latest_frame = last_frame_by_type[i];
            latest_type = static_cast<int>(i);
        }
        if (last_frame_by_type[i] == 0 || last_frame_by_type[i] < cutoff_frame) {
            all_markers_within_window = false;
        }
    }
    const uint64_t last_sleep_frame = g_nvapi_d3d_last_sleep_global_frame_id.load();
    const bool sleep_within_window = (last_sleep_frame != 0 && last_sleep_frame >= cutoff_frame);
    const bool status_ok = all_markers_within_window && sleep_within_window;

    imgui.SameLine();
    if (status_ok) {
        imgui.TextColored(ui::colors::ICON_SUCCESS, "Status: OK");
    } else {
        imgui.TextColored(ui::colors::ICON_ERROR, "Status: FAIL");
    }
    if (imgui.IsItemHovered()) {
        std::ostringstream tt;
        tt << "OK - Game implements native Reflex correctly.\n"
           << "FAIL - Game didn't implement native Reflex correctly, needs fixes.\n"
           << "Sleep: " << sleep_count << "\n"
           << "Markers total: " << marker_count << "\n\n"
           << "Last frame (by marker type):\n";
        for (size_t i = 0; i < static_cast<size_t>(kLatencyMarkerTypeCountFirstSix); ++i) {
            tt << "  " << GetNvLowLatencyVkMarkerTypeName(static_cast<int>(i)) << ": ";
            if (last_frame_by_type[i] != 0) {
                tt << "#" << last_frame_by_type[i];
            } else {
                tt << "—";
            }
            tt << "\n";
        }
        imgui.SetTooltipEx("%s", tt.str().c_str());
    }
}

// Draw DXGI overlay subsection (show DXGI VRR status, show DXGI refresh rate). Uses RefreshRateMonitor when
// enable_dxgi_refresh_rate_vrr_detection is on in Advanced tab. Checkboxes are disabled when that setting is off.
void DrawDxgiOverlaySubsection(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Columns(1);
    imgui.Separator();
    imgui.TextUnformatted("DXGI");
    imgui.Columns(4, "overlay_checkboxes", false);

    const bool dxgi_detection_enabled =
        settings::g_advancedTabSettings.enable_dxgi_refresh_rate_vrr_detection.GetValue();
    if (!dxgi_detection_enabled) {
        imgui.BeginDisabled();
    }

    bool show_dxgi_vrr_status = settings::g_mainTabSettings.show_dxgi_vrr_status.GetValue();
    if (imgui.Checkbox("Show DXGI VRR status", &show_dxgi_vrr_status)) {
        settings::g_mainTabSettings.show_dxgi_vrr_status.SetValue(show_dxgi_vrr_status);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Shows DXGI-based VRR status in the performance overlay (RefreshRateMonitor heuristic: rate spread / "
            "samples below threshold). Enable \"DXGI refresh rate / VRR detection\" in Advanced tab for data.");
    }
    imgui.NextColumn();

    bool show_dxgi_refresh_rate = settings::g_mainTabSettings.show_dxgi_refresh_rate.GetValue();
    if (imgui.Checkbox("Show DXGI refresh rate", &show_dxgi_refresh_rate)) {
        settings::g_mainTabSettings.show_dxgi_refresh_rate.SetValue(show_dxgi_refresh_rate);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Shows DXGI refresh rate (Hz) in the performance overlay from swap chain GetFrameStatistics. "
            "Enable \"DXGI refresh rate / VRR detection\" in Advanced tab for data.");
    }
    imgui.NextColumn();

    if (!dxgi_detection_enabled) {
        imgui.EndDisabled();
    }

    imgui.Columns(1);

    // Show current DXGI VRR status and refresh rate on the main tab when detection is enabled
    if (dxgi_detection_enabled) {
        dxgi::fps_limiter::RefreshRateStats dxgi_stats = dxgi::fps_limiter::GetRefreshRateStats();
        double dxgi_hz = dxgi::fps_limiter::GetSmoothedRefreshRate();
        if (dxgi_stats.is_valid || dxgi_hz > 0.0) {
            imgui.Spacing();
            if (dxgi_stats.is_valid) {
                if (dxgi_stats.all_last_20_within_1s && dxgi_stats.samples_below_threshold_last_10s >= 2) {
                    imgui.TextColored(ui::colors::TEXT_SUCCESS, "DXGI VRR: On");
                } else {
                    imgui.TextColored(ui::colors::TEXT_DIMMED, "DXGI VRR: Off");
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
void DrawNvapiStatsOverlaySubsection(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Columns(1);  // Reset to single column
    imgui.Separator();
    imgui.TextWrapped(
        "NVAPI stats (NVIDIA only). These options may cause occasional hiccups; not available on Intel/AMD, "
        "Linux, or 32-bit builds.");
    imgui.Columns(4, "overlay_checkboxes", false);

    const bool nvapi_initialized = nvapi::EnsureNvApiInitialized();
    const bool nvapi_stats_available = nvapi_initialized && is_64_bit();
    if (!nvapi_stats_available) {
        imgui.BeginDisabled();
    }

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

    bool show_actual_refresh_rate = settings::g_mainTabSettings.show_actual_refresh_rate.GetValue();
    if (imgui.Checkbox("Refresh rate" ICON_FK_WARNING, &show_actual_refresh_rate)) {
        settings::g_mainTabSettings.show_actual_refresh_rate.SetValue(show_actual_refresh_rate);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Shows actual refresh rate in the performance overlay (NvAPI_DISP_GetAdaptiveSyncData). "
            "Also feeds the refresh rate time graph when \"Refresh rate time graph\" is on. "
            "WARNING: May cause a heartbeat/hitch (frame time spike). Uses NVAPI (NVIDIA only).");
    }
    imgui.NextColumn();
    imgui.NextColumn();

    bool show_refresh_rate_frame_times = settings::g_mainTabSettings.show_refresh_rate_frame_times.GetValue();
    if (imgui.Checkbox("Refresh rate time graph" ICON_FK_WARNING, &show_refresh_rate_frame_times)) {
        settings::g_mainTabSettings.show_refresh_rate_frame_times.SetValue(show_refresh_rate_frame_times);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Shows a graph of actual refresh rate frame times (NVAPI Adaptive Sync) in the overlay. "
            "Requires NVAPI and a resolved display.\n"
            "WARNING: May cause a heartbeat/hitch (frame time spike). Uses NVAPI (NVIDIA only).");
    }
    imgui.NextColumn();

    bool show_refresh_rate_frame_time_stats = settings::g_mainTabSettings.show_refresh_rate_frame_time_stats.GetValue();
    if (imgui.Checkbox("Refresh rate time stats", &show_refresh_rate_frame_time_stats)) {
        settings::g_mainTabSettings.show_refresh_rate_frame_time_stats.SetValue(show_refresh_rate_frame_time_stats);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Shows refresh rate time statistics (avg, deviation, min, max) in the overlay. "
            "Uses NVAPI (NVIDIA only; may cause occasional hiccups).");
    }
    imgui.NextColumn();

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

    bool show_nvapi_latency_stats = settings::g_mainTabSettings.show_nvapi_latency_stats.GetValue();
    if (imgui.Checkbox("Latency PCL(AV)", &show_nvapi_latency_stats)) {
        settings::g_mainTabSettings.show_nvapi_latency_stats.SetValue(show_nvapi_latency_stats);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Shows NVIDIA Reflex NVAPI latency stats (PC latency and GPU frame time) in the performance overlay.\n"
            "Requires a D3D11/D3D12 device with Reflex latency reporting available. Uses NvAPI_D3D_GetLatency, "
            "which may add minor overhead when enabled.");
    }

    if (display_commander::nvapi::IsNvapiActualRefreshRateMonitoringActive()
        && display_commander::nvapi::IsNvapiGetAdaptiveSyncDataFailingRepeatedly()) {
        imgui.Columns(1);
        imgui.TextColored(
            ui::colors::TEXT_WARNING,
            "NvAPI_DISP_GetAdaptiveSyncData is failing repeatedly (e.g. driver/display may not support it). "
            "Refresh rate and refresh rate time graph may show no data.");
        imgui.Columns(4, "overlay_checkboxes", false);
    }

    imgui.Columns(1);  // Reset to single column
    if (settings::g_mainTabSettings.show_refresh_rate_frame_times.GetValue()
        || settings::g_mainTabSettings.show_actual_refresh_rate.GetValue()) {
        if (SliderIntSetting(settings::g_mainTabSettings.refresh_rate_monitor_poll_ms, "Refresh poll (ms)", "%d ms",
                             imgui)) {
            // Setting is automatically saved by SliderIntSetting
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Polling interval for the actual refresh rate monitoring thread when the time graph is enabled. "
                "Lower values update the graph more frequently but use more CPU. When the time graph is off, "
                "polling defaults to 1 s and this setting is not used.");
        }
    }

    if (!nvapi_stats_available) {
        imgui.EndDisabled();
    }
}

}  // anonymous namespace

// Performance overlay / graphs / timeline implementations were extracted into:
// `ui/new_ui/controls/performance_overlay/*.cpp`

// Draw DLSS indicator section (registry toggle + DLSS-FG text level). Shown at top of DLSS Control when active.
static void DrawDLSSInfo_IndicatorSection(display_commander::ui::IImGuiWrapper& imgui) {
    if (imgui.TreeNodeEx("DLSS indicator (Registry)", ImGuiTreeNodeFlags_None)) {
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Show DLSS on-screen indicator in games. Writes NVIDIA registry; may require restart. Admin if apply "
                "fails.");
        }
        bool reg_enabled = dlss::DlssIndicatorManager::IsDlssIndicatorEnabled();
        imgui.TextColored(reg_enabled ? ui::colors::TEXT_SUCCESS : ui::colors::TEXT_DIMMED, "DLSS indicator: %s",
                          reg_enabled ? "On" : "Off");
        if (imgui.Checkbox("Enable DLSS indicator through Registry##MainTab", &reg_enabled)) {
            LogInfo("DLSS Indicator: %s", reg_enabled ? "enabled" : "disabled");
            if (!dlss::DlssIndicatorManager::SetDlssIndicatorEnabled(reg_enabled)) {
                LogInfo("DLSS Indicator: Apply to registry failed (run as admin if needed).");
            }
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Show DLSS on-screen indicator (resolution/version) in games. Writes NVIDIA registry; may require "
                "restart. Admin needed if apply fails.");
        }

        const char* dlssg_indicator_items[] = {"Off", "Minimal", "Detailed"};
        int dlssg_indicator_current = static_cast<int>(dlss::DlssIndicatorManager::GetDlssgIndicatorTextLevel());
        if (dlssg_indicator_current < 0 || dlssg_indicator_current > 2) {
            dlssg_indicator_current = 0;
        }
        if (imgui.Combo("DLSS-FG indicator text##MainTab", &dlssg_indicator_current, dlssg_indicator_items,
                        static_cast<int>(sizeof(dlssg_indicator_items) / sizeof(dlssg_indicator_items[0])))) {
            DWORD level = static_cast<DWORD>(dlssg_indicator_current);
            if (!dlss::DlssIndicatorManager::SetDlssgIndicatorTextLevel(level)) {
                LogInfo("DLSSG_IndicatorText: Apply to registry failed (run as admin).");
            }
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "DLSS-FG on-screen indicator text level (registry DLSSG_IndicatorText). Off / Minimal / Detailed. "
                "May require restart. Admin needed if apply fails.");
        }
        imgui.TreePop();
    }
}

// Draw DLSS information (same format as performance overlay). Caller must pass pre-fetched summary.
void DrawDLSSInfo(display_commander::ui::IImGuiWrapper& imgui, const DLSSGSummary& dlssg_summary) {
    (void)imgui;
    CALL_GUARD_NO_TS();
    const bool any_dlss_active =
        dlssg_summary.dlss_active || dlssg_summary.dlss_g_active || dlssg_summary.ray_reconstruction_active;

    DrawDLSSInfo_IndicatorSection(imgui);

    // Tracked DLSS modules (from OnModuleLoaded: nvngx_dlss/dlssg/dlssd.dll or .bin identified as such)
    {
        auto path_dlss = GetDlssTrackedPath(DlssTrackedKind::DLSS);
        auto path_dlssg = GetDlssTrackedPath(DlssTrackedKind::DLSSG);
        auto path_dlssd = GetDlssTrackedPath(DlssTrackedKind::DLSSD);
        if (path_dlss.has_value() || path_dlssg.has_value() || path_dlssd.has_value()) {
            if (imgui.TreeNodeEx("DLSS module paths (tracked)", ImGuiTreeNodeFlags_None)) {
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx(
                        "Paths from OnModuleLoaded (DLL name or .bin identified as DLSS/DLSS-G/DLSS-D).");
                }
                if (path_dlss.has_value()) {
                    imgui.Text("nvngx_dlss.dll: %s", path_dlss->c_str());
                } else {
                    imgui.TextColored(ui::colors::TEXT_DIMMED, "nvngx_dlss.dll: (not tracked)");
                }
                if (path_dlssg.has_value()) {
                    imgui.Text("nvngx_dlssg.dll: %s", path_dlssg->c_str());
                } else {
                    imgui.TextColored(ui::colors::TEXT_DIMMED, "nvngx_dlssg.dll: (not tracked)");
                }
                if (path_dlssd.has_value()) {
                    imgui.Text("nvngx_dlssd.dll: %s", path_dlssd->c_str());
                } else {
                    imgui.TextColored(ui::colors::TEXT_DIMMED, "nvngx_dlssd.dll: (not tracked)");
                }
                imgui.TreePop();
            }
        }
    }

    // CreateFeature seen (NGX/Streamline): whether we observed CreateFeature for each feature, or loaded too late
    if (imgui.TreeNodeEx("CreateFeature seen (tracked)", ImGuiTreeNodeFlags_None)) {
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Whether our NGX/Streamline hooks observed CreateFeature for each feature. \"Loaded too late\" means "
                "the game created the feature before we hooked.");
        }
        const bool dlss_seen = g_dlss_was_active_once.load();
        const bool dlss_late = dlssg_summary.dlss_active && !dlss_seen;
        if (dlss_seen) {
            imgui.TextColored(ui::colors::TEXT_SUCCESS, "DLSS: CreateFeature seen");
        } else if (dlss_late) {
            imgui.TextColored(ui::colors::TEXT_WARNING, "DLSS: Loaded too late (CreateFeature not seen)");
        } else {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS: Not seen");
        }
        const bool dlssfg_seen = g_dlssg_was_active_once.load();
        const bool dlssfg_late = dlssg_summary.dlss_g_active && !dlssfg_seen;
        if (dlssfg_seen) {
            imgui.TextColored(ui::colors::TEXT_SUCCESS, "DLSS FG: CreateFeature seen");
        } else if (dlssfg_late) {
            imgui.TextColored(ui::colors::TEXT_WARNING, "DLSS FG: Loaded too late (CreateFeature not seen)");
        } else {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS FG: Not seen");
        }
        const bool dlssrr_seen = g_ray_reconstruction_was_active_once.load();
        const bool dlssrr_late = dlssg_summary.ray_reconstruction_active && !dlssrr_seen;
        if (dlssrr_seen) {
            imgui.TextColored(ui::colors::TEXT_SUCCESS, "DLSS-RR: CreateFeature seen");
        } else if (dlssrr_late) {
            imgui.TextColored(ui::colors::TEXT_WARNING, "DLSS-RR: Loaded too late (CreateFeature not seen)");
        } else {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS-RR: Not seen");
        }
        imgui.TreePop();
    }

    // FG Mode
    if (any_dlss_active
        && (dlssg_summary.fg_mode == "2x" || dlssg_summary.fg_mode == "3x" || dlssg_summary.fg_mode == "4x")) {
        imgui.Text("FG: %s", dlssg_summary.fg_mode.c_str());
    } else {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "FG: OFF");
    }

    // DLSS Internal Resolution (same format as performance overlay: internal -> output -> backbuffer)
    if (any_dlss_active && dlssg_summary.internal_resolution != "N/A") {
        std::string res_text = dlssg_summary.internal_resolution;
        const int bb_w = g_game_render_width.load();
        const int bb_h = g_game_render_height.load();
        if (bb_w > 0 && bb_h > 0) {
            res_text += " -> " + std::to_string(bb_w) + "x" + std::to_string(bb_h);
        }
        imgui.Text("DLSS Internal->Output: %s", res_text.c_str());
    } else {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS Internal->Output: N/A");
    }

    // DLSS Status
    if (any_dlss_active) {
        std::string status_text = "DLSS: On";
        if (dlssg_summary.ray_reconstruction_active) {
            status_text += " (RR)";
        } else if (dlssg_summary.dlss_g_active) {
            status_text += " (DLSS-G)";
        }
        imgui.TextColored(ui::colors::TEXT_SUCCESS, "%s", status_text.c_str());
    } else {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS: Off");
    }

    // DLSS Quality Preset
    if (any_dlss_active && dlssg_summary.quality_preset != "N/A") {
        imgui.Text("DLSS Quality: %s", dlssg_summary.quality_preset.c_str());
    } else {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS Quality: N/A");
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
            imgui.Text("DLSS Render: %s", render_preset_letter.c_str());
        } else {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS Render: N/A");
        }
    } else {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS Render: N/A");
    }

    // DLSS Render Preset override (same settings as Swapchain tab)
    if (any_dlss_active) {
        bool preset_override_enabled = settings::g_swapchainTabSettings.dlss_preset_override_enabled.GetValue();
        if (imgui.Checkbox("Enable DLSS Preset Override##MainTab", &preset_override_enabled)) {
            settings::g_swapchainTabSettings.dlss_preset_override_enabled.SetValue(preset_override_enabled);
            ResetNGXPresetInitialization();
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Override DLSS presets at runtime (Game Default / DLSS Default / Preset A, B, C, etc.). Same as "
                "Swapchain tab.");
        }

        if (g_dlss_from_nvidia_app_bin.load()) {
            imgui.TextColored(
                ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                "NVIDIA App DLSS override detected (.bin). Version and presets are controlled by the NVIDIA app.");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "DLSS was loaded from a .bin bundle (Streamline/NVIDIA App). Preset override may have limited "
                    "effect.");
            }
        }

        if (settings::g_swapchainTabSettings.dlss_preset_override_enabled.GetValue()) {
            const bool rr_active = dlssg_summary.ray_reconstruction_active;

            std::vector<std::string> sr_preset_options = GetDLSSPresetOptions(dlssg_summary.supported_dlss_presets);
            std::vector<const char*> sr_preset_cstrs;
            sr_preset_cstrs.reserve(sr_preset_options.size());
            for (const auto& option : sr_preset_options) {
                sr_preset_cstrs.push_back(option.c_str());
            }
            std::string sr_current_value = settings::g_swapchainTabSettings.dlss_sr_preset_override.GetValue();
            int sr_current_selection = 0;
            for (size_t i = 0; i < sr_preset_options.size(); ++i) {
                if (sr_current_value == sr_preset_options[i]) {
                    sr_current_selection = static_cast<int>(i);
                    break;
                }
            }
            const char* sr_label = rr_active ? "SR Preset##MainTabSRPreset" : "SR Preset (active)##MainTabSRPreset";
            imgui.SetNextItemWidth(250.0f);
            if (imgui.Combo(sr_label, &sr_current_selection, sr_preset_cstrs.data(),
                            static_cast<int>(sr_preset_cstrs.size()))) {
                settings::g_swapchainTabSettings.dlss_sr_preset_override.SetValue(sr_preset_options[sr_current_selection]);
                ResetNGXPresetInitialization();
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Preset: Game Default = no override, DLSS Default = 0, Preset A/B/C... = 1/2/3...");
            }

            std::vector<std::string> rr_preset_options =
                GetDLSSPresetOptions(dlssg_summary.supported_dlss_rr_presets);
            std::vector<const char*> rr_preset_cstrs;
            rr_preset_cstrs.reserve(rr_preset_options.size());
            for (const auto& option : rr_preset_options) {
                rr_preset_cstrs.push_back(option.c_str());
            }
            std::string rr_current_value = settings::g_swapchainTabSettings.dlss_rr_preset_override.GetValue();
            int rr_current_selection = 0;
            for (size_t i = 0; i < rr_preset_options.size(); ++i) {
                if (rr_current_value == rr_preset_options[i]) {
                    rr_current_selection = static_cast<int>(i);
                    break;
                }
            }
            const char* rr_label = rr_active ? "RR Preset (active)##MainTabRRPreset" : "RR Preset##MainTabRRPreset";
            imgui.SetNextItemWidth(250.0f);
            if (imgui.Combo(rr_label, &rr_current_selection, rr_preset_cstrs.data(),
                            static_cast<int>(rr_preset_cstrs.size()))) {
                settings::g_swapchainTabSettings.dlss_rr_preset_override.SetValue(rr_preset_options[rr_current_selection]);
                ResetNGXPresetInitialization();
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Preset: Game Default = no override, DLSS Default = 0, Preset A/B/C... = 1/2/3...");
            }
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
            imgui.Text("Create.Flags: %d (%s)", create_flags_val, create_flags_list.c_str());
        } else {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "Create.Flags: N/A");
        }
    } else {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "Create.Flags: N/A");
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
        imgui.Text("Auto Exposure: %s", dlssg_summary.auto_exposure.c_str());
        const char* ae_items[] = {"Game Default", "Force Off", "Force On"};
        int ae_idx = 0;
        if (ae_current == "Force Off") {
            ae_idx = 1;
        } else if (ae_current == "Force On") {
            ae_idx = 2;
        }
        imgui.SetNextItemWidth(imgui.CalcTextSize("Force On").x + (imgui.GetStyle().FramePadding.x * 2.0f) + 20.0f);
        if (imgui.Combo("Auto Exposure Override##DLSS", &ae_idx, ae_items, 3)) {
            settings::g_swapchainTabSettings.dlss_forced_auto_exposure.SetValue(ae_items[ae_idx]);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Override DLSS auto-exposure. Takes effect when DLSS feature is (re)created.\n"
                "See Create.Flags field for current DLSS.Feature.Create.Flags value and decoded bits.");
        }
        if (original_auto_exposure_setting != ae_current) {
            imgui.TextColored(ui::colors::TEXT_WARNING, "Restart required for change to take effect.");
        }
    } else {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "Auto Exposure: N/A");
    }

    // DLSS DLL Versions
    imgui.Spacing();
    if (dlssg_summary.dlss_dll_version != "N/A") {
        imgui.TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "DLSS DLL: %s", dlssg_summary.dlss_dll_version.c_str());
        if (dlssg_summary.supported_dlss_presets != "N/A") {
            imgui.SameLine();
            imgui.TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), " [%s]", dlssg_summary.supported_dlss_presets.c_str());
        }
    } else {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS DLL: N/A");
    }

    if (dlssg_summary.dlssg_dll_version != "N/A") {
        imgui.TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "DLSS-G DLL: %s", dlssg_summary.dlssg_dll_version.c_str());
    } else {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS-G DLL: N/A");
    }

    if (dlssg_summary.dlssd_dll_version != "N/A" && dlssg_summary.dlssd_dll_version != "Not loaded") {
        imgui.TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "DLSS-D DLL: %s", dlssg_summary.dlssd_dll_version.c_str());
    } else {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS-D DLL: N/A");
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
            imgui.TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                              ICON_FK_WARNING
                              " Override not applied for: %s. Restart game with override enabled before launch.",
                              not_applied.c_str());
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "The game loaded these DLLs before our hooks were active. Enable override and restart the game to "
                    "use override versions.");
            }
        }
    }
}

// Performance overlay / graphs implementations were extracted into:
// `ui/new_ui/controls/performance_overlay/*.cpp`

void InitMainNewTab() {
    CALL_GUARD_NO_TS();
    static bool settings_loaded_once = false;
    if (!settings_loaded_once) {
        // Settings already loaded at startup
        settings::g_mainTabSettings.LoadSettings();
        s_aspect_index = static_cast<AspectRatioType>(settings::g_mainTabSettings.aspect_index.GetValue());
        s_window_alignment = static_cast<WindowAlignment>(settings::g_mainTabSettings.alignment.GetValue());
        // FPS limits are now automatically synced via FloatSettingRef
        // Audio mute settings are automatically synced via BoolSettingRef
        // Background mute settings are automatically synced via BoolSettingRef
        // VSync & Tearing - all automatically synced via BoolSettingRef

        // Apply loaded mute state immediately if manual mute is enabled
        // Apply mute setting to the audio system
     /*   if (settings::g_mainTabSettings.audio_mute.GetValue()) {
            if (::SetMuteForCurrentProcess(true)) {
                ::g_muted_applied.store(true);
                LogInfo("Audio mute state loaded and applied from settings");
            } else {
                LogWarn("Failed to apply loaded mute state");
            }
        }*/

        // Update input blocking system with loaded settings
        // Input blocking is now handled by Windows message hooks

        settings_loaded_once = true;

        // FPS limiter: enabled checkbox + mode (0=OnPresentSync, 1=Reflex, 2=LatentSync; clamp to 0-2)
        s_fps_limiter_enabled.store(settings::g_mainTabSettings.fps_limiter_enabled.GetValue());
        int mode_val = settings::g_mainTabSettings.fps_limiter_mode.GetValue();
        if (mode_val < 0 || mode_val > 2) {
            mode_val = (mode_val < 0) ? 0 : 2;
            settings::g_mainTabSettings.fps_limiter_mode.SetValue(mode_val);
        }
        s_fps_limiter_mode.store(static_cast<FpsLimiterMode>(mode_val));
        // Scanline offset and VBlank Sync Divisor are now automatically synced via IntSettingRef

        // Initialize resolution widget
        display_commander::widgets::resolution_widget::InitializeResolutionWidget();

        // Log level is read directly from settings::g_mainTabSettings.log_level when needed (GetMinLogLevel()).
    }
}


void DrawAdvancedSettings(display_commander::ui::IImGuiWrapper& imgui) {
    (void)imgui;
    CALL_GUARD_NO_TS();
    // Advanced Settings Control
    {
        bool advanced_settings = settings::g_mainTabSettings.advanced_settings_enabled.GetValue();
        if (imgui.Checkbox(ICON_FK_FILE_CODE " Show All Tabs", &advanced_settings)) {
            settings::g_mainTabSettings.advanced_settings_enabled.SetValue(advanced_settings);
            LogInfo("Advanced settings %s", advanced_settings ? "enabled" : "disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Enable advanced settings to show advanced tabs (Advanced, Debug, etc.).\n"
                "When disabled, advanced tabs will be hidden to simplify the interface.");
        }
    }

    imgui.Spacing();

    // Logging Level Control
    if (ComboSettingEnumWrapper(settings::g_mainTabSettings.log_level, "Logging Level", imgui)) {
        // Always log the level change (using LogCurrentLogLevel which uses LogError)
        LogCurrentLogLevel();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Controls the minimum log level to display:\n\n"
            "- Error Only: Only error messages\n"
            "- Warning: Errors and warnings\n"
            "- Info: Errors, warnings, and info messages\n"
            "- Debug (Everything): All log messages (default)");
    }

    imgui.Spacing();

    // Individual Tab Visibility Settings
    imgui.Text("Show Individual Tabs:");
    imgui.Indent();

    if (ui::new_ui::g_tab_manager.HasTab("games")) {
        if (CheckboxSetting(settings::g_mainTabSettings.show_games_tab, "Show Games Tab", imgui)) {
            LogInfo("Show Games tab %s",
                    settings::g_mainTabSettings.show_games_tab.GetValue() ? "enabled" : "disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Show the Games tab (per-game display and profile settings).");
        }
    }

    if (ui::new_ui::g_tab_manager.HasTab("hotkeys")) {
        if (CheckboxSetting(settings::g_mainTabSettings.show_hotkeys_tab, "Show Hotkeys Tab", imgui)) {
            LogInfo("Show Hotkeys tab %s",
                    settings::g_mainTabSettings.show_hotkeys_tab.GetValue() ? "enabled" : "disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Show the Hotkeys tab (keyboard shortcuts for volume, overlay, window actions, etc.).");
        }
    }

    if (ui::new_ui::g_tab_manager.HasTab("advanced")) {
        if (CheckboxSetting(settings::g_mainTabSettings.show_advanced_tab, "Show Advanced Tab", imgui)) {
            LogInfo("Show Advanced tab %s",
                    settings::g_mainTabSettings.show_advanced_tab.GetValue() ? "enabled" : "disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Shows the Advanced tab even when 'Show All Tabs' is disabled.");
        }
    }

    if (ui::new_ui::g_tab_manager.HasTab("controller")) {
        if (CheckboxSetting(settings::g_mainTabSettings.show_controller_tab, "Show Controller Tab", imgui)) {
            LogInfo("Show Controller tab %s",
                    settings::g_mainTabSettings.show_controller_tab.GetValue() ? "enabled" : "disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Shows the Controller tab (XInput monitoring and remapping) even when 'Show All Tabs' is disabled.");
        }
    }

    if (ui::new_ui::g_tab_manager.HasTab("reshade")) {
        if (CheckboxSetting(settings::g_mainTabSettings.show_reshade_tab, "Show ReShade Tab", imgui)) {
            LogInfo("Show ReShade tab %s",
                    settings::g_mainTabSettings.show_reshade_tab.GetValue() ? "enabled" : "disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Shows the ReShade/Addons tab even when 'Show All Tabs' is disabled.");
        }
    }

    const std::vector<modules::ModuleDescriptor> modules_list = modules::GetModules();
    if (!modules_list.empty()) {
        imgui.Spacing();
        imgui.Text("Module Features:");
        imgui.Indent();
        for (const modules::ModuleDescriptor& module : modules_list) {
            bool enabled = module.enabled;
            const std::string checkbox_label = "Enable " + module.display_name;
            if (imgui.Checkbox(checkbox_label.c_str(), &enabled)) {
                modules::SetModuleEnabled(module.id, enabled);
                LogInfo("Module '%s' %s", module.id.c_str(), enabled ? "enabled" : "disabled");
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("%s", module.description.c_str());
            }

            if (enabled) {
                imgui.SameLine();
                bool show_overlay = module.show_in_overlay;
                const std::string overlay_label = "Overlay##" + module.id;
                if (imgui.Checkbox(overlay_label.c_str(), &show_overlay)) {
                    modules::SetModuleOverlayEnabled(module.id, show_overlay);
                    LogInfo("Module '%s' overlay %s", module.id.c_str(), show_overlay ? "enabled" : "disabled");
                }
            }

            modules::DrawModuleMainTabInlineById(module.id, imgui, nullptr);
        }
        imgui.Unindent();
    }

    imgui.Unindent();

    imgui.Spacing();

    DrawMainTabOptionalPanelsAdvancedSettingsUi(imgui);

    imgui.Spacing();
}

display_commander::ui::GraphicsApi GetGraphicsApiFromRuntime(reshade::api::effect_runtime* runtime) {
    if (!runtime) return display_commander::ui::GraphicsApi::Unknown;
    auto rapi = runtime->get_device()->get_api();
    switch (rapi) {
        case reshade::api::device_api::d3d9:   return display_commander::ui::GraphicsApi::D3D9;
        case reshade::api::device_api::d3d10:  return display_commander::ui::GraphicsApi::D3D10;
        case reshade::api::device_api::d3d11:  return display_commander::ui::GraphicsApi::D3D11;
        case reshade::api::device_api::d3d12:  return display_commander::ui::GraphicsApi::D3D12;
        case reshade::api::device_api::opengl: return display_commander::ui::GraphicsApi::OpenGL;
        case reshade::api::device_api::vulkan: return display_commander::ui::GraphicsApi::Vulkan;
        default:                               return display_commander::ui::GraphicsApi::Unknown;
    }
}

display_commander::ui::GraphicsApi GetGraphicsApiFromLastDeviceApi() {
    if (!(g_reshade_module != nullptr)) return display_commander::ui::GraphicsApi::Unknown;
    const reshade::api::device_api api = g_last_reshade_device_api.load();
    switch (api) {
        case reshade::api::device_api::d3d9:   return display_commander::ui::GraphicsApi::D3D9;
        case reshade::api::device_api::d3d10:  return display_commander::ui::GraphicsApi::D3D10;
        case reshade::api::device_api::d3d11:  return display_commander::ui::GraphicsApi::D3D11;
        case reshade::api::device_api::d3d12:  return display_commander::ui::GraphicsApi::D3D12;
        case reshade::api::device_api::opengl: return display_commander::ui::GraphicsApi::OpenGL;
        case reshade::api::device_api::vulkan: return display_commander::ui::GraphicsApi::Vulkan;
        default:                               return display_commander::ui::GraphicsApi::Unknown;
    }
}


void DrawMainNewTab(display_commander::ui::GraphicsApi api, display_commander::ui::IImGuiWrapper& imgui,
                    reshade::api::effect_runtime* runtime) {
    CALL_GUARD_NO_TS();
    RefreshReShadeModuleIfNeeded();
    // Load saved settings once and sync legacy globals
    g_rendering_ui_section.store("ui:tab:main_new:entry", std::memory_order_release);

    // Game default overrides info banner (when overrides are in use for this exe)
    if (display_commander::config::HasActiveOverrides()) {
        g_rendering_ui_section.store("ui:tab:main_new:default_overrides", std::memory_order_release);
        imgui.Spacing();
        imgui.TextColored(ImVec4(0.4f, 0.6f, 0.9f, 1.0f),
                          ICON_FK_FILE " Game default overrides are in use for this game.");
        if (imgui.IsItemHovered()) {
            std::string tooltip = "Active overrides (per-exe defaults):\n";
            for (const auto& e : display_commander::config::GetActiveOverrideEntries()) {
                std::string value_display = (e.value == "1") ? "On" : (e.value == "0") ? "Off" : e.value;
                tooltip += "  - " + e.display_name + " = " + value_display + "\n";
            }
            tooltip += "\nClick \"Apply to config\" to save these to DisplayCommander.ini.";
            imgui.SetTooltipEx("%s", tooltip.c_str());
        }
        imgui.SameLine();
        if (imgui.Button("Apply to config")) {
            display_commander::config::ApplyDefaultOverridesToConfig();
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Save the current override values to this game's DisplayCommander.ini.");
        }
        imgui.Spacing();
    }

    // Config save failure warning at the top
    g_rendering_ui_section.store("ui:tab:main_new:warnings:config", std::memory_order_release);
    auto config_save_failure_path = g_config_save_failure_path.load();
    if (config_save_failure_path != nullptr && !config_save_failure_path->empty()) {
        imgui.Spacing();
        imgui.TextColored(ui::colors::TEXT_ERROR, ICON_FK_WARNING " Error: Failed to save config to %s",
                          config_save_failure_path->c_str());
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("The configuration file could not be saved. Check file permissions and disk space.");
        }
        imgui.Spacing();
    }

    g_rendering_ui_section.store("ui:tab:main_new:warnings:load_from_dll", std::memory_order_release);
    // LoadFromDllMain warning (config read once; requires restart to pick up ini changes)
    int32_t load_from_dll_main_value = 0;
    if (g_reshade_module != nullptr) {
        if (!s_load_from_dll_main_fetched.load(std::memory_order_acquire)) {
            int32_t v = 0;
            const bool ok = reshade::get_config_value(nullptr, "ADDON", "LoadFromDllMain", v);
            s_load_from_dll_main_value.store(ok ? v : 0, std::memory_order_relaxed);
            s_load_from_dll_main_fetched.store(true, std::memory_order_release);
        }
        load_from_dll_main_value = s_load_from_dll_main_value.load(std::memory_order_relaxed);
    }
    if (load_from_dll_main_value == 1) {
        imgui.Spacing();
        imgui.TextColored(ui::colors::TEXT_WARNING,
                          ICON_FK_WARNING " WARNING: LoadFromDllMain is set to 1 in ReShade configuration");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "LoadFromDllMain=1 can cause compatibility issues with some games and addons. "
                "Consider disabling it in the Advanced tab or ReShade.ini if you experience problems.");
        }
        imgui.Spacing();
    }

    g_rendering_ui_section.store("ui:tab:main_new:warnings:multi_version", std::memory_order_release);
    // Multiple Display Commander versions warning
    auto other_dc_version = g_other_dc_version_detected.load();
    if (other_dc_version != nullptr && !other_dc_version->empty()) {
        imgui.Spacing();
        imgui.TextColored(ui::colors::TEXT_ERROR,
                          ICON_FK_WARNING " ERROR: Multiple Display Commander versions detected!");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Another Display Commander instance (v%s) is loaded in this process. "
                "This may cause conflicts and unexpected behavior. Please ensure only one version is loaded.",
                other_dc_version->c_str());
        }
        imgui.SameLine();
        imgui.TextColored(ui::colors::TEXT_ERROR, "Other version: v%s", other_dc_version->c_str());
        imgui.Spacing();
    }

    g_rendering_ui_section.store("ui:tab:main_new:warnings:multi_swapchain", std::memory_order_release);
    // Multiple swapchains (ReShade runtimes) warning — skip when RenoDX is loaded (expected multiple runtimes)
    const size_t runtime_count = GetReShadeRuntimeCount();
    if (runtime_count > 1 && !g_is_renodx_loaded.load(std::memory_order_relaxed)) {
        imgui.Spacing();
        imgui.TextColored(ui::colors::TEXT_WARNING,
                          ICON_FK_WARNING " WARNING: Multiple swapchains detected (%zu ReShade runtimes)",
                          runtime_count);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "More than one swapchain/runtime is active. Some features may target only the first runtime. "
                "This can happen with multi-window or multi-context games.");
        }
        imgui.Spacing();
    }

    g_rendering_ui_section.store("ui:tab:main_new:version_build", std::memory_order_release);
    // Version and build information at the top
    {
        imgui.TextColored(ui::colors::TEXT_DEFAULT, "Version: %s", DISPLAY_COMMANDER_VERSION_STRING);

        // Display current graphics API with feature level/version
        const reshade::api::device_api api = g_last_reshade_device_api.load();
        imgui.SameLine();
        if (api != static_cast<reshade::api::device_api>(0)) {
            uint32_t api_version = g_last_api_version.load();

            if (api == reshade::api::device_api::d3d9 && s_d3d9e_upgrade_successful.load()) {
                api_version =
                    static_cast<uint32_t>(display_commander::D3D9ApiVersion::D3D9Ex);  // due to reshade's bug.
            }

            // Display API with version/feature level and bitness
#ifdef _WIN64
            const char* bitness_label = "64-bit";
#else
            const char* bitness_label = "32-bit";
#endif
            if (api_version != 0) {
                std::string api_string = GetDeviceApiVersionString(api, api_version);
                imgui.TextColored(ui::colors::TEXT_LABEL, "| %s: %s", bitness_label, api_string.c_str());
            } else {
                imgui.TextColored(ui::colors::TEXT_LABEL, "| %s: %s", bitness_label, GetDeviceApiString(api));
            }
        } else {
#ifdef _WIN64
            const char* bitness_label = "64-bit";
#else
            const char* bitness_label = "32-bit";
#endif
            imgui.TextColored(ui::colors::TEXT_LABEL, "| %s", bitness_label);
        }

        // Ko-fi support button
        //imgui.SameLine();
        //imgui.TextColored(ui::colors::TEXT_LABEL, "Support the project:");
        imgui.PushStyleColor(ImGuiCol_Text, ui::colors::ICON_SPECIAL);
        if (imgui.Button(ICON_FK_PLUS " Buy me a coffee on Ko-fi")) {
            ShellExecuteA(nullptr, "open", "https://ko-fi.com/pmnox", nullptr, nullptr, SW_SHOW);
        }
        imgui.PopStyleColor();
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Support Display Commander development with a coffee!");
        }
    }

    // Display Settings Section
    g_rendering_ui_section.store("ui:tab:main_new:display_settings", std::memory_order_release);
    ui::colors::PushHeaderColors(&imgui);
    const bool display_settings_open = imgui.CollapsingHeader("Display Settings", ImGuiTreeNodeFlags_DefaultOpen);
    ui::colors::PopCollapsingHeaderColors(&imgui);
    if (display_settings_open) {
        imgui.Indent();
        DrawDisplaySettings(api, imgui, runtime);
        imgui.Unindent();
    }

    imgui.Spacing();

    // Monitor/Display Resolution Settings Section
    g_rendering_ui_section.store("ui:tab:main_new:resolution", std::memory_order_release);
    ui::colors::PushHeaderColors(&imgui);
    const bool resolution_control_open = imgui.CollapsingHeader("Resolution Control", ImGuiTreeNodeFlags_None);
    ui::colors::PopCollapsingHeaderColors(&imgui);
    if (resolution_control_open) {
        imgui.Indent();
        display_commander::widgets::resolution_widget::DrawResolutionWidget(imgui);
        imgui.Unindent();
    }

    imgui.Spacing();

    g_rendering_ui_section.store("ui:tab:main_new:window_control", std::memory_order_release);
    ui::colors::PushHeaderColors(&imgui);
    const bool window_control_open = imgui.CollapsingHeader("Window Control", ImGuiTreeNodeFlags_None);
    ui::colors::PopCollapsingHeaderColors(&imgui);
    if (window_control_open) {
        imgui.Indent();

        CheckboxSetting(settings::g_advancedTabSettings.prevent_always_on_top, "Prevent Always On Top", imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Prevents windows from becoming always on top, even if they are moved or resized.");
        }

        imgui.SameLine();
        CheckboxSetting(settings::g_advancedTabSettings.prevent_minimize, "Prevent Minimize", imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Prevents the game window from being minimized (e.g. via taskbar or system menu).");
        }


        // Continue rendering in background (also labeled "Fake Fullscreen" for discoverability)
        static bool continue_rendering_changed = false;
        if (CheckboxSetting(settings::g_advancedTabSettings.continue_rendering,
                            "Continue Rendering in Background (Fake Fullscreen)", imgui)) {
            continue_rendering_changed = true;
            bool new_value = settings::g_advancedTabSettings.continue_rendering.GetValue();
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
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Prevent games from pausing or reducing performance when alt-tabbed. Blocks window focus "
                "messages so the game keeps running in the background. Also called fake fullscreen.");
        }

        /*
        if (display_commanderhooks::g_wgi_state.wgi_called.load() && continue_rendering_changed) {
            imgui.TextColored(ui::colors::TEXT_WARNING,
                              ICON_FK_WARNING " Game restart may be required for changes to take full effect.");
        }
        imgui.Spacing();
*/
        // Prevent display sleep & screensaver mode
        if (ComboSettingEnumWrapper(settings::g_mainTabSettings.screensaver_mode, "Prevent display sleep & screensaver",
                                    imgui, 320.f)) {
            LogInfo("Prevent display sleep & screensaver mode changed to %d",
                    settings::g_mainTabSettings.screensaver_mode.GetValue());
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Controls display sleep and screensaver while the game is running:\n\n"
                "- Default (no change): Preserves original game behavior\n"
                "- Disable when Focused: Prevents display sleep & screensaver when game window is focused\n"
                "- Disable: Always prevents display sleep & screensaver while game is running\n\n"
                "Note: Enable \"Prevent display sleep & screensaver\" in the Advanced tab for this to take effect.");
        }

        // Windows taskbar visibility
        if (ComboSettingEnumWrapper(settings::g_mainTabSettings.taskbar_hide_mode, "Auto-hide Windows taskbar", imgui,
                                    320.f)) {
            LogInfo("Auto-hide Windows taskbar mode changed to %d",
                    settings::g_mainTabSettings.taskbar_hide_mode.GetValue());
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Controls Windows taskbar visibility (main and secondary monitors):\n\n"
                "- No changes: Do not hide the taskbar\n"
                "- In foreground: Hide taskbar while game is in foreground, show when in background\n"
                "- Always: Always hide the taskbar while the game is running.\n\n"
                "Taskbar is restored when the addon unloads.");
        }

        imgui.Unindent();
    }

    imgui.Spacing();

    g_rendering_ui_section.store("ui:tab:main_new:performance_overlay", std::memory_order_release);
    ui::colors::PushHeaderColors(&imgui);
    const bool performance_overlay_open = imgui.CollapsingHeader("Performance Overlay", ImGuiTreeNodeFlags_None);
    ui::colors::PopCollapsingHeaderColors(&imgui);
    if (performance_overlay_open) {
        imgui.Indent();
        DrawImportantInfo(imgui);
        imgui.Unindent();
    }
    imgui.Spacing();
    g_rendering_ui_section.store("ui:tab:main_new:advanced_settings", std::memory_order_release);
    ui::colors::PushHeaderColors(&imgui);
    const bool advanced_settings_open = imgui.CollapsingHeader("Features", ImGuiTreeNodeFlags_None);
    ui::colors::PopCollapsingHeaderColors(&imgui);
    if (advanced_settings_open) {
        imgui.Indent();
        DrawAdvancedSettings(imgui);
        imgui.Unindent();
    }

    DrawMainTabOptionalPanelsInOrder(api, imgui, runtime);
}

void DrawQuickFpsLimitChanger(display_commander::ui::IImGuiWrapper& imgui) {
    (void)imgui;
    CALL_GUARD_NO_TS();
    const float selected_epsilon = 0.002f;
    auto window_state = ::g_window_state.load();
    double refresh_hz = window_state ? window_state->current_monitor_refresh_rate.ToHz() : 0.0;
    int y = static_cast<int>(std::round(refresh_hz));

    if (y <= 0) {
        // Refresh rate unknown: show fixed presets only (no fallback)
        const float presets[] = {0.0f, 30.0f, 60.0f, 120.0f, 144.0f};
        const char* labels[] = {"No Limit", "30", "60", "120", "144"};
        for (size_t i = 0; i < sizeof(presets) / sizeof(presets[0]); ++i) {
            if (i > 0) imgui.SameLine();
            bool selected =
                (std::fabs(settings::g_mainTabSettings.fps_limit.GetValue() - presets[i]) <= selected_epsilon);
            if (selected) ui::colors::PushSelectedButtonColors(&imgui);
            if (imgui.Button(labels[i])) {
                settings::g_mainTabSettings.fps_limit.SetValue(presets[i]);
            }
            if (selected) ui::colors::PopSelectedButtonColors(&imgui);
        }
        // Reflex rate not detected error
        imgui.TextColored(ui::colors::TEXT_DIMMED, "Reflex rate not detected: TODO FIXME");
        return;
    }

    // Quick-set buttons based on current monitor refresh rate
    {
        bool first = true;
        // Add No Limit button at the beginning
        if (enabled_experimental_features) {
            bool selected = (std::fabs(settings::g_mainTabSettings.fps_limit.GetValue() - 0.0f) <= selected_epsilon);
            if (selected) ui::colors::PushSelectedButtonColors(&imgui);
            if (imgui.Button("No Limit")) {
                settings::g_mainTabSettings.fps_limit.SetValue(0.0f);
            }
            if (selected) ui::colors::PopSelectedButtonColors(&imgui);
            first = false;
        }
        for (int x = 1; x <= 15; ++x) {
            int candidate_rounded = static_cast<int>(std::round(refresh_hz / x));
            float candidate_precise = static_cast<float>(refresh_hz / x);
            constexpr int k_quick_fps_min = 40;
            const bool above_min = (candidate_rounded >= k_quick_fps_min);
            if (above_min) {
                if (!first) imgui.SameLine();
                first = false;
                std::string label = std::to_string(candidate_rounded);
                {
                    bool selected = (std::fabs(settings::g_mainTabSettings.fps_limit.GetValue() - candidate_precise)
                                     <= selected_epsilon);
                    if (selected) ui::colors::PushSelectedButtonColors(&imgui);
                    if (imgui.Button(label.c_str())) {
                        float target_fps = candidate_precise;
                        settings::g_mainTabSettings.fps_limit.SetValue(target_fps);
                    }
                    if (selected) ui::colors::PopSelectedButtonColors(&imgui);
                    // Add tooltip showing the precise calculation
                    if (imgui.IsItemHovered()) {
                        std::ostringstream tooltip_oss;
                        tooltip_oss.setf(std::ios::fixed);
                        tooltip_oss << std::setprecision(3);
                        tooltip_oss << "FPS = " << refresh_hz << " ÷ " << x << " = " << candidate_precise << " FPS\n\n";
                        tooltip_oss << "Creates a smooth frame rate that divides evenly\n";
                        tooltip_oss << "into the monitor's refresh rate.";
                        imgui.SetTooltipEx("%s", tooltip_oss.str().c_str());
                    }
                }
            }
        }
        // Add Gsync Cap button at the end
        if (!first) {
            imgui.SameLine();
        }

        {
            // Gsync formula: 3600 × refresh / (refresh + 3600). Apply ×0.995 only when Reflex is enabled.
            const double raw_cap = 3600.0 * refresh_hz / (refresh_hz + 3600.0);
            const bool reflex_enabled = ShouldReflexBeEnabled() && ShouldReflexLowLatencyBeEnabled();
            const double gsync_target = reflex_enabled ? (raw_cap * 0.995) : raw_cap;
            float precise_target = static_cast<float>(gsync_target);
            if (precise_target < 1.0f) precise_target = 1.0f;
            bool selected =
                (std::fabs(settings::g_mainTabSettings.fps_limit.GetValue() - precise_target) <= selected_epsilon);

            if (selected) ui::colors::PushSelectedButtonColors(&imgui);
            if (imgui.Button("VRR Cap")) {
                double precise_target_val = gsync_target;  // do not round on apply
                float target_fps = static_cast<float>(precise_target_val < 1.0 ? 1.0 : precise_target_val);
                settings::g_mainTabSettings.fps_limit.SetValue(target_fps);
            }
            if (selected) ui::colors::PopSelectedButtonColors(&imgui);
            // Add tooltip explaining the Gsync formula
            if (imgui.IsItemHovered()) {
                std::ostringstream tooltip_oss;
                tooltip_oss.setf(std::ios::fixed);
                tooltip_oss << std::setprecision(3);
                tooltip_oss << "Gsync Cap: FPS = 3600 × " << refresh_hz << " / (" << refresh_hz << " + 3600)\n";
                tooltip_oss << "= " << raw_cap << " FPS";
                if (reflex_enabled) {
                    tooltip_oss << " × 0.995 = " << gsync_target << " FPS";
                }
                tooltip_oss << "\n\n";
                tooltip_oss << "Creates a ~0.3ms frame time buffer to optimize latency\n";
                tooltip_oss << "and prevent tearing, similar to NVIDIA Reflex Low Latency Mode.";
                if (reflex_enabled) {
                    tooltip_oss << "\n(×0.995 applied because Reflex limiter is enabled.)";
                }
                imgui.SetTooltipEx("%s", tooltip_oss.str().c_str());
            }
        }
    }
}

void DrawDisplaySettings_DisplayAndTarget(display_commander::ui::IImGuiWrapper& imgui,
                                          reshade::api::effect_runtime* runtime) {
    (void)imgui;
    CALL_GUARD_NO_TS();
    {
        // Refresh target display from config so hotkey changes (Win+Left/Win+Right) are visible on the UI thread
        settings::g_mainTabSettings.selected_extended_display_device_id.Load();
        settings::g_mainTabSettings.target_extended_display_device_id.Load();

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

        // Backbuffer size: from runtime when available, else from game render size
        uint32_t backbuffer_w = 0;
        uint32_t backbuffer_h = 0;
        reshade::api::format backbuffer_format = reshade::api::format::unknown;
        if (runtime != nullptr) {
            runtime->get_screenshot_width_and_height(&backbuffer_w, &backbuffer_h);
            reshade::api::device* device = runtime->get_device();
            if (device != nullptr) {
                reshade::api::resource bb = runtime->get_back_buffer(0);
                if (bb != 0) {
                    backbuffer_format = device->get_resource_desc(bb).texture.format;
                }
            }
            if (backbuffer_format == reshade::api::format::unknown) {
                auto desc_ptr = g_last_swapchain_desc_post.load();
                if (desc_ptr != nullptr) {
                    backbuffer_format = desc_ptr->back_buffer.texture.format;
                }
            }
        }
        if (backbuffer_w == 0 || backbuffer_h == 0) {
            backbuffer_w = static_cast<uint32_t>(g_game_render_width.load());
            backbuffer_h = static_cast<uint32_t>(g_game_render_height.load());
            if (backbuffer_w == 0 || backbuffer_h == 0) {
                auto desc_ptr = g_last_swapchain_desc_post.load();
                if (desc_ptr != nullptr) {
                    backbuffer_w = desc_ptr->back_buffer.texture.width;
                    backbuffer_h = desc_ptr->back_buffer.texture.height;
                    backbuffer_format = desc_ptr->back_buffer.texture.format;
                }
            } else {
                auto desc_ptr = g_last_swapchain_desc_post.load();
                if (desc_ptr != nullptr) {
                    backbuffer_format = desc_ptr->back_buffer.texture.format;
                }
            }
        }

        if (backbuffer_w > 0 && backbuffer_h > 0) {
            imgui.TextColored(ui::colors::TEXT_LABEL, "Render resolution:");
            imgui.SameLine();
            imgui.Text("%ux%u", static_cast<unsigned>(backbuffer_w), static_cast<unsigned>(backbuffer_h));

            // Bit depth from runtime or swapchain desc (optional, in parens)
            const char* bit_depth_str = nullptr;
            switch (backbuffer_format) {
                case reshade::api::format::r8g8b8a8_unorm:
                case reshade::api::format::b8g8r8a8_unorm:     bit_depth_str = "8-bit"; break;
                case reshade::api::format::r10g10b10a2_unorm:  bit_depth_str = "10-bit"; break;
                case reshade::api::format::r16g16b16a16_float: bit_depth_str = "16-bit"; break;
                default:                                       break;
            }
            if (bit_depth_str != nullptr) {
                imgui.SameLine();
                imgui.TextColored(ui::colors::TEXT_DIMMED, " (%s)", bit_depth_str);
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
            imgui.SameLine();
            if (refresh_hz > 0.0) {
                imgui.TextColored(ui::colors::TEXT_LABEL, "Refresh rate:");
                imgui.SameLine();
                imgui.Text("%.1f Hz", refresh_hz);
            } else {
                imgui.TextColored(ui::colors::TEXT_LABEL, "Refresh rate:");
                imgui.SameLine();
                imgui.TextColored(ui::colors::TEXT_DIMMED, "—");
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
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
                imgui.TextColored(ui::colors::TEXT_LABEL, "VRAM:");
                imgui.SameLine();
                imgui.Text("%llu / %llu MiB", static_cast<unsigned long long>(used_mib),
                           static_cast<unsigned long long>(total_mib));
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx("GPU video memory used / budget (DXGI adapter memory budget).");
                }
            } else {
                imgui.TextColored(ui::colors::TEXT_LABEL, "VRAM:");
                imgui.SameLine();
                imgui.TextColored(ui::colors::TEXT_DIMMED, "N/A");
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx("VRAM unavailable (DXGI adapter or budget query failed).");
                }
            }

            // RAM (system memory) on the same line: X(Y)/Z = system used (current process used) / total
            imgui.SameLine();
            MEMORYSTATUSEX mem_status = {};
            mem_status.dwLength = sizeof(mem_status);
            if (GlobalMemoryStatusEx(&mem_status) != 0 && mem_status.ullTotalPhys > 0) {
                const uint64_t ram_used = mem_status.ullTotalPhys - mem_status.ullAvailPhys;
                const uint64_t ram_used_mib = ram_used / (1024ULL * 1024ULL);
                const uint64_t ram_total_mib = mem_status.ullTotalPhys / (1024ULL * 1024ULL);
                PROCESS_MEMORY_COUNTERS pmc = {};
                pmc.cb = sizeof(pmc);
                const bool have_process = (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)) != 0);
                const uint64_t process_mib = have_process ? (pmc.WorkingSetSize / (1024ULL * 1024ULL)) : 0;
                imgui.TextColored(ui::colors::TEXT_LABEL, "RAM:");
                imgui.SameLine();
                if (have_process) {
                    imgui.Text("%llu (%llu) / %llu MiB", static_cast<unsigned long long>(ram_used_mib),
                               static_cast<unsigned long long>(process_mib),
                               static_cast<unsigned long long>(ram_total_mib));
                } else {
                    imgui.Text("%llu / %llu MiB", static_cast<unsigned long long>(ram_used_mib),
                               static_cast<unsigned long long>(ram_total_mib));
                }
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx(
                        "System RAM in use (this app working set) / total (GlobalMemoryStatusEx, "
                        "GetProcessMemoryInfo).");
                }
            } else {
                imgui.TextColored(ui::colors::TEXT_LABEL, "RAM:");
                imgui.SameLine();
                imgui.TextColored(ui::colors::TEXT_DIMMED, "N/A");
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx("System memory info unavailable.");
                }
            }
        }

        // Target Display dropdown (left-aligned with Render resolution / VRAM; flat frame — similar to DC folder buttons)
        std::vector<const char*> monitor_c_labels;
        monitor_c_labels.reserve(display_info.size());
        for (const auto& info : display_info) {
            monitor_c_labels.push_back(info.display_label.c_str());
        }

        float preview_text_w = imgui.CalcTextSize("—").x;
        for (const char* lbl : monitor_c_labels) {
            preview_text_w = (std::max)(preview_text_w, imgui.CalcTextSize(lbl).x);
        }
        const ImGuiStyle& st = imgui.GetStyle();
        const float combo_ctrl_w =
            preview_text_w + (st.FramePadding.x * 2.f) + st.ItemInnerSpacing.x + imgui.GetTextLineHeight() + 4.f;

        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        const ImVec4 frame_bg_clear(0.f, 0.f, 0.f, 0.f);
        imgui.PushStyleColor(ImGuiCol_FrameBg, frame_bg_clear);
        imgui.PushStyleColor(ImGuiCol_FrameBgHovered, frame_bg_clear);
        imgui.PushStyleColor(ImGuiCol_FrameBgActive, frame_bg_clear);

        PushFpsLimiterSliderColumnAlign(imgui, GetMainTabCheckboxColumnGutter(imgui));
        imgui.BeginGroup();
        imgui.SetNextItemWidth(600.f); //   combo_ctrl_w);

        static bool s_target_display_changed = false;
        if (imgui.Combo("##TargetDisplay", &selected_index, monitor_c_labels.data(),
                        static_cast<int>(monitor_c_labels.size()))) {
            if (selected_index >= 0 && selected_index < static_cast<int>(display_info.size())) {
                s_target_display_changed = true;
                // Store extended device ID so Win+Left/Right and window management use the same value.
                std::string new_device_id = display_info[selected_index].extended_device_id;
                settings::g_mainTabSettings.selected_extended_display_device_id.SetValue(new_device_id);
                settings::g_mainTabSettings.target_extended_display_device_id.SetValue(new_device_id);

                LogInfo("Target monitor changed to device ID: %s", new_device_id.c_str());
            }
        }
        const bool target_display_combo_hovered = imgui.IsItemHovered();
        imgui.SameLine(0.f, st.ItemInnerSpacing.x);
        imgui.TextColored(ui::colors::TEXT_LABEL, "Target Display");
        const bool target_display_label_hovered = imgui.IsItemHovered();
        imgui.EndGroup();

        imgui.PopStyleColor(3);
        ImGui::PopStyleVar(2);
        if (target_display_combo_hovered || target_display_label_hovered) {
            // Get the saved game window display device ID for tooltip
            std::string saved_device_id = settings::g_mainTabSettings.game_window_extended_display_device_id.GetValue();
            std::string tooltip_text =
                "Choose which monitor to apply size/pos to. The monitor corresponding to the "
                "game window is automatically selected.";
            if (!saved_device_id.empty() && saved_device_id != "No Window" && saved_device_id != "No Monitor"
                && saved_device_id != "Monitor Info Failed") {
                tooltip_text += "\n\nGame window is on: " + saved_device_id;
            }
            imgui.SetTooltipEx("%s", tooltip_text.c_str());
        }
        // Warn if mode does not resize; moving to another display isn't implemented in those modes
        const WindowMode mode = GetCurrentWindowMode();
        if (s_target_display_changed
            && (mode == WindowMode::kNoChanges || mode == WindowMode::kPreventFullscreenNoResize)) {
            imgui.TextColored(ui::colors::TEXT_WARNING, ICON_FK_WARNING
                              "Warning: Moving to another display isn't implemented in this window mode.");
        }
    }
}

void DrawDisplaySettings_WindowModeAndApply(display_commander::ui::IImGuiWrapper& imgui) {
    (void)imgui;
    CALL_GUARD_NO_TS();
    // Window Mode dropdown (with persistent setting)
    static bool was_ever_in_no_changes_mode = false;
    if (static_cast<WindowMode>(settings::g_mainTabSettings.window_mode.GetValue()) == WindowMode::kNoChanges) {
        was_ever_in_no_changes_mode = true;
    }

    PushFpsLimiterSliderColumnAlign(imgui, GetMainTabCheckboxColumnGutter(imgui));
    if (ComboSettingEnumWrapper(settings::g_mainTabSettings.window_mode, "Window Mode", imgui, 600.f,
                               &ui::colors::TEXT_LABEL)) {
        // Don't apply changes immediately - let the normal window management system handle it
        // This prevents crashes when changing modes during gameplay
        LogInfo("Window mode changed to %d", settings::g_mainTabSettings.window_mode.GetValue());
    }
    // Warn about restart may be needed for preventing fullscreen
    if (was_ever_in_no_changes_mode
        && static_cast<WindowMode>(settings::g_mainTabSettings.window_mode.GetValue()) != WindowMode::kNoChanges) {
        imgui.TextColored(ui::colors::TEXT_WARNING,
                          ICON_FK_WARNING "Warning: Restart may be needed for preventing fullscreen.");
    }

    // Aspect Ratio dropdown (only shown in Aspect Ratio mode)
    if (GetCurrentWindowMode() == WindowMode::kAspectRatio) {
        if (ComboSettingWrapper(settings::g_mainTabSettings.aspect_index, "Aspect Ratio", imgui)) {
            s_aspect_index = static_cast<AspectRatioType>(settings::g_mainTabSettings.aspect_index.GetValue());
            LogInfo("Aspect ratio changed");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Choose the aspect ratio for window resizing.");
        }
    }
    if (GetCurrentWindowMode() == WindowMode::kAspectRatio) {
        // Width dropdown for aspect ratio mode
        if (ComboSettingWrapper(settings::g_mainTabSettings.window_aspect_width, "Window Width", imgui)) {
            LogInfo("Window width for aspect mode setting changed to: %d",
                    settings::g_mainTabSettings.window_aspect_width.GetValue());
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Choose the width for the aspect ratio window. 'Display Width' uses the current monitor width.");
        }
    }

    // Window Alignment dropdown (only shown in Aspect Ratio mode)
    if (GetCurrentWindowMode() == WindowMode::kAspectRatio) {
        if (ComboSettingWrapper(settings::g_mainTabSettings.alignment, "Alignment", imgui)) {
            s_window_alignment = static_cast<WindowAlignment>(settings::g_mainTabSettings.alignment.GetValue());
            LogInfo("Window alignment changed");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Choose how to align the window when repositioning is needed. 0=Center, 1=Top Left, "
                "2=Top Right, 3=Bottom Left, 4=Bottom Right.");
        }
    }
    // Black curtain (game / other displays) controls
    DrawAdhdMultiMonitorControls(imgui);

    // Apply Changes button
    imgui.PushStyleColor(ImGuiCol_Text, ui::colors::ICON_SUCCESS);
    if (imgui.Button(ICON_FK_OK " Apply Changes")) {
        LogInfo("Apply Changes button clicked - forcing immediate window update");
        std::ostringstream oss;
        // All global settings on this tab are handled by the settings wrapper
        oss << "Apply Changes button clicked - forcing immediate window update";
        LogInfo(oss.str().c_str());
    }
    imgui.PopStyleColor();
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Apply the current window size and position settings immediately.");
    }
}

static void DrawDisplaySettings_FpsLimiterAdvanced(display_commander::ui::IImGuiWrapper& imgui,
                                                   float fps_limiter_checkbox_column_gutter);
static void DrawDisplaySettings_FpsLimiterOnPresentSync(display_commander::ui::IImGuiWrapper& imgui,
                                                      const std::function<void()>& drawPclStatsCheckbox,
                                                      float fps_limiter_checkbox_column_gutter);
static void DrawDisplaySettings_FpsLimiterReflex(display_commander::ui::IImGuiWrapper& imgui,
                                                 const std::function<void()>& drawPclStatsCheckbox);
static void DrawDisplaySettings_FpsLimiterLatentSync(display_commander::ui::IImGuiWrapper& imgui);

void DrawDisplaySettings_FpsLimiter(display_commander::ui::IImGuiWrapper& imgui) {
    (void)imgui;
    CALL_GUARD_NO_TS();
    imgui.Spacing();

    const char* mode_items[] = {"Default", "NVIDIA Reflex (DX11/DX12 only, Vulkan requires native reflex)",
                                "Sync to Display Refresh Rate (fraction of monitor refresh rate) Non-VRR"};

    int current_item = settings::g_mainTabSettings.fps_limiter_mode.GetValue();
    if (current_item < 0 || current_item > 2) {
        current_item = (current_item < 0) ? 0 : 2;
        settings::g_mainTabSettings.fps_limiter_mode.SetValue(current_item);
        s_fps_limiter_mode.store(static_cast<FpsLimiterMode>(current_item));
    }
    int prev_item = current_item;

    bool enabled = settings::g_mainTabSettings.fps_limiter_enabled.GetValue();
    bool fps_limit_enabled =
        (enabled && s_fps_limiter_mode.load() != FpsLimiterMode::kLatentSync) || ShouldReflexBeEnabled();
    const auto get_fps_limiter_control_width = [&imgui]() -> float {
        // Keep controls stable for fixed-width clients while avoiding overflow on narrower layouts.
        const float avail = imgui.GetContentRegionAvail().x;
        return (std::min)(kFpsLimiterItemWidth, (std::max)(260.0f, avail));
    };

    const float fps_limiter_checkbox_column_gutter = GetMainTabCheckboxColumnGutter(imgui);
    // (enable checkbox) fps limit slider
    if (imgui.Checkbox("##FPS limiter", &enabled)) {
        settings::g_mainTabSettings.fps_limiter_enabled.SetValue(enabled);
        s_fps_limiter_enabled.store(enabled);
        LogInfo("FPS Limiter: %s", enabled ? "enabled" : "disabled (no limiting)");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("When checked, the selected mode is active. When unchecked, no FPS limiting.");
    }
    imgui.SameLine();
    if (!fps_limit_enabled) {
        imgui.BeginDisabled();
    }
    float current_value = settings::g_mainTabSettings.fps_limit.GetValue();
    const char* fmt = (current_value > 0.0f) ? "%.3f FPS" : "No Limit";
    imgui.SetNextItemWidth(get_fps_limiter_control_width());
    if (SliderFloatSetting(settings::g_mainTabSettings.fps_limit, "FPS Limit", fmt, imgui)) {
    }
    float cur_limit = settings::g_mainTabSettings.fps_limit.GetValue();
    if (cur_limit > 0.0f && cur_limit < 10.0f) {
        settings::g_mainTabSettings.fps_limit.SetValue(0.0f);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Set FPS limit for the game (0 = no limit). Now uses the new Custom FPS Limiter system.");
    }
    if (!fps_limit_enabled) {
        imgui.EndDisabled();
    }

    if (enabled) {
        DrawQuickFpsLimitChanger(imgui);
    }

    // (enable background checkbox) background fps limiter slider
    {
        bool background_fps_enabled = settings::g_mainTabSettings.background_fps_enabled.GetValue();
        if (imgui.Checkbox("##Background FPS", &background_fps_enabled)) {
            settings::g_mainTabSettings.background_fps_enabled.SetValue(background_fps_enabled);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "When enabled, cap FPS when the game window is in the background. Slider sets the limit (default 60).");
        }
        imgui.SameLine();
        if (fps_limit_enabled && !settings::g_mainTabSettings.background_fps_enabled.GetValue()) {
            imgui.BeginDisabled();
        }
        float current_bg = settings::g_mainTabSettings.fps_limit_background.GetValue();
        const char* fmt_bg = (current_bg > 0.0f) ? "%.0f FPS" : "No Limit";
        imgui.SetNextItemWidth(get_fps_limiter_control_width());
        if (SliderFloatSetting(settings::g_mainTabSettings.fps_limit_background, "Background FPS Limit", fmt_bg,
                               imgui)) {
        }
        if (fps_limit_enabled && !settings::g_mainTabSettings.background_fps_enabled.GetValue()) {
            imgui.EndDisabled();
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "When enabled, caps FPS to the limit above when the game window is not in the foreground. Uses the "
                "Custom FPS Limiter.");
        }
    }

    // (fps limiter mode selection) — align with slider rows (checkbox + SameLine offset above)
    PushFpsLimiterSliderColumnAlign(imgui, fps_limiter_checkbox_column_gutter);
    if (!enabled) {
        imgui.BeginDisabled();
    }
    imgui.SetNextItemWidth(get_fps_limiter_control_width());
    if (imgui.Combo("FPS Limiter Mode", &current_item, mode_items, 3)) {
        settings::g_mainTabSettings.fps_limiter_mode.SetValue(current_item);
        s_fps_limiter_mode.store(static_cast<FpsLimiterMode>(current_item));
        FpsLimiterMode mode = s_fps_limiter_mode.load();
        if (mode == FpsLimiterMode::kReflex) {
            LogInfo("FPS Limiter: Reflex");
            settings::g_advancedTabSettings.reflex_auto_configure.SetValue(true);
        } else if (mode == FpsLimiterMode::kOnPresentSync) {
            LogInfo("FPS Limiter: OnPresent Frame Synchronizer");
        } else if (mode == FpsLimiterMode::kLatentSync) {
            LogInfo("FPS Limiter: VBlank Scanline Sync for VSYNC-OFF or without VRR");
        }

        if (mode == FpsLimiterMode::kReflex && prev_item != static_cast<int>(FpsLimiterMode::kReflex)) {
            settings::g_advancedTabSettings.reflex_auto_configure.SetValue(false);
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Choose limiter mode (when FPS limiter is enabled):\n"
            "Default - Various presets.\n"
            "Reflex - NVIDIA Reflex library.\n"
            "Sync to Display Refresh Rate - synchronizes frame display time to the monitor refresh rate\n"
            "\n"
            " FPS limiter source: %s",
            GetChosenFpsLimiterSiteName());
    }

    if (!enabled) {
        imgui.EndDisabled();
    }

    // Subheader for advanced FPS limiter settings
    //imgui.Spacing();
    //imgui.TextColored(ui::colors::TEXT_DIMMED, "Advanced FPS limiter settings");
    imgui.Indent();
    DrawDisplaySettings_FpsLimiterAdvanced(imgui, fps_limiter_checkbox_column_gutter);
    imgui.Unindent();

    // After Reflex / advanced FPS UI so FPS Limiter Mode sits next to Reflex without a debug header in between.
    if (enabled_experimental_features) {
        if (!enabled) {
            imgui.BeginDisabled();
        }
        ui::colors::PushHeader2Colors(&imgui);
        const bool fps_limiter_debug_open =
            imgui.CollapsingHeader("FPS Limiter Debug", display_commander::ui::wrapper_flags::TreeNodeFlags_None);
        ui::colors::PopCollapsingHeaderColors(&imgui);
        if (fps_limiter_debug_open) {
            const uint64_t now_ns = static_cast<uint64_t>(utils::get_now_ns());
            const uint8_t chosen = g_chosen_fps_limiter_site.load(std::memory_order_relaxed);
            size_t active_sites = 0;
            size_t recent_sites = 0;

            for (size_t i = 0; i < kFpsLimiterCallSiteCount; i++) {
                const uint64_t last_ts = g_fps_limiter_last_timestamp_ns[i].load(std::memory_order_relaxed);
                const bool called_recently =
                    (last_ts != 0 && (now_ns - last_ts) <= static_cast<uint64_t>(utils::SEC_TO_NS));
                const bool is_active = (chosen != kFpsLimiterChosenUnset && static_cast<size_t>(chosen) == i);
                if (is_active) {
                    active_sites++;
                }
                if (called_recently) {
                    recent_sites++;
                }
            }

            const float debug_label_width = (std::min)(280.0f, (std::max)(180.0f, imgui.GetContentRegionAvail().x * 0.45f));
            imgui.Columns(2, "FpsLimiterDebugSummary", false);
            imgui.SetColumnWidth(0, debug_label_width);

            imgui.Text("Active call sites:");
            imgui.NextColumn();
            imgui.Text("%zu / %zu", active_sites, static_cast<size_t>(kFpsLimiterCallSiteCount));
            imgui.NextColumn();

            imgui.Text("Recent call sites (<=1s):");
            imgui.NextColumn();
            imgui.Text("%zu / %zu", recent_sites, static_cast<size_t>(kFpsLimiterCallSiteCount));
            imgui.NextColumn();
            imgui.Columns(1);

            imgui.Separator();
            imgui.TextUnformatted("Call site activity:");

            imgui.Columns(2, "FpsLimiterDebugRows", false);
            imgui.SetColumnWidth(0, debug_label_width);
            for (size_t i = 0; i < kFpsLimiterCallSiteCount; i++) {
                const char* name = FpsLimiterSiteDisplayName(static_cast<FpsLimiterCallSite>(i));
                const uint64_t last_ts = g_fps_limiter_last_timestamp_ns[i].load(std::memory_order_relaxed);
                const bool called_recently =
                    (last_ts != 0 && (now_ns - last_ts) <= static_cast<uint64_t>(utils::SEC_TO_NS));
                const bool is_active = (chosen != kFpsLimiterChosenUnset && static_cast<size_t>(chosen) == i);

                const char* status = "-";
                ImVec4 status_color = ui::colors::TEXT_DIMMED;
                if (is_active) {
                    status = "Active";
                    status_color = ui::colors::ICON_SUCCESS;
                } else if (called_recently) {
                    status = "OK";
                    status_color = ui::colors::TEXT_SUCCESS;
                }

                imgui.Text("%s", name);
                imgui.NextColumn();
                imgui.TextColored(status_color, "%s", status);
                if (last_ts != 0) {
                    const double age_ms = static_cast<double>(now_ns - last_ts) / static_cast<double>(utils::NS_TO_MS);
                    imgui.SameLine();
                    imgui.TextColored(ui::colors::TEXT_DIMMED, "(%.1f ms ago)", age_ms);
                }
                imgui.NextColumn();
            }
            imgui.Columns(1);
        }
        if (!enabled) {
            imgui.EndDisabled();
        }
    }
}

static void DrawDisplaySettings_FpsLimiterOnPresentSync(display_commander::ui::IImGuiWrapper& imgui,
                                                        const std::function<void()>& drawPclStatsCheckbox,
                                                        float fps_limiter_checkbox_column_gutter) {
    // Reflex combo is always shown in Advanced FPS limiter settings (unified for all modes)
    if (!::IsNativeFramePacingInSync()) {
        // Check if we're running on D3D9 and show warning
        const reshade::api::device_api current_api = g_last_reshade_device_api.load();
        if (current_api == reshade::api::device_api::d3d9) {
            imgui.TextColored(ui::colors::TEXT_WARNING,
                              ICON_FK_WARNING " Warning: Reflex does not work with Direct3D 9");
        }
        drawPclStatsCheckbox();

        // Low Latency Ratio Selector (Experimental WIP placeholder)
        auto display_input_ratio = !(::IsNativeFramePacingInSync() && GetEffectiveNativePacingSimStartOnly());

        if (display_input_ratio) {
            imgui.Spacing();
            if (ComboSettingWrapper(settings::g_mainTabSettings.onpresent_sync_low_latency_ratio,
                                    "Display / Input Ratio", imgui, 600.f)) {
                // Setting is automatically saved via ComboSettingWrapper
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
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
            imgui.SameLine();
            static bool show_delay_bias_debug = false;
            if (imgui.SmallButton("[Debug]")) {
                show_delay_bias_debug = !show_delay_bias_debug;
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Show delay_bias debug information");
            }

            // Debug Info Window
            if (show_delay_bias_debug) {
                imgui.Begin("Delay Bias Debug Info", &show_delay_bias_debug, ImGuiWindowFlags_AlwaysAutoResize);

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
                imgui.TextColored(ui::colors::TEXT_HIGHLIGHT, "Ratio Settings:");
                imgui.Text("Ratio Index: %d", ratio_index);
                float display_pct = (1.0f - delay_bias) * 100.0f;
                float input_pct = delay_bias * 100.0f;
                imgui.Text("Delay Bias: %.3f (%.1f%% Display / %.1f%% Input)", delay_bias, display_pct, input_pct);

                imgui.Spacing();
                imgui.TextColored(ui::colors::TEXT_HIGHLIGHT, "Frame Timing:");
                if (frame_time_ns > 0) {
                    float frame_time_ms = frame_time_ns / 1'000'000.0f;
                    float target_fps = 1000.0f / frame_time_ms;
                    imgui.Text("Frame Time: %.3f ms (%.1f FPS)", frame_time_ms, target_fps);
                } else {
                    imgui.TextColored(ui::colors::TEXT_WARNING, "Frame Time: Not set (FPS limiter disabled?)");
                }

                imgui.Spacing();
                imgui.TextColored(ui::colors::TEXT_HIGHLIGHT, "Sleep Times:");
                if (pre_sleep_ns > 0) {
                    imgui.Text("Pre-Sleep: %.3f ms", pre_sleep_ns / 1'000'000.0f);
                } else {
                    imgui.Text("Pre-Sleep: 0 ms");
                }
                if (post_sleep_ns > 0) {
                    imgui.Text("Post-Sleep: %.3f ms", post_sleep_ns / 1'000'000.0f);
                } else {
                    imgui.Text("Post-Sleep: 0 ms");
                }
                if (late_ns != 0) {
                    imgui.TextColored(ui::colors::TEXT_WARNING, "Late Amount: %.3f ms", late_ns / 1'000'000.0f);
                } else {
                    imgui.Text("Late Amount: 0 ms");
                }

                imgui.Spacing();
                imgui.TextColored(ui::colors::TEXT_HIGHLIGHT, "Frame Timing (Raw):");
                if (last_frame_end_ns > 0) {
                    LONGLONG now_ns = utils::get_now_ns();
                    LONGLONG time_since_last_frame_ns = now_ns - last_frame_end_ns;
                    imgui.Text("Last Frame End: %lld ns (%.3f ms ago)", last_frame_end_ns,
                               time_since_last_frame_ns / 1'000'000.0f);
                } else {
                    imgui.Text("Last Frame End: Not set (first frame?)");
                }
                if (frame_start_ns > 0) {
                    LONGLONG now_ns = utils::get_now_ns();
                    LONGLONG time_since_start_ns = now_ns - frame_start_ns;
                    imgui.Text("Frame Start: %lld ns (%.3f ms ago)", frame_start_ns,
                               time_since_start_ns / 1'000'000.0f);
                } else {
                    imgui.Text("Frame Start: Not set");
                }

                imgui.End();
            }
        }
    } else {
    // FPS limiter presets (only visible if OnPresentSync mode is selected and in sync)
        const int raw = settings::g_mainTabSettings.native_reflex_fps_preset.GetValue();
        if (raw < 0 || raw > static_cast<int>(FpsLimiterPreset::kCustom)) {
            settings::g_mainTabSettings.native_reflex_fps_preset.SetValue(FpsLimiterPreset::kDCPaceLockQ2);
        }
        FpsLimiterPreset preset = settings::g_mainTabSettings.native_reflex_fps_preset.GetEnumValue();

        PushFpsLimiterSliderColumnAlign(imgui, fps_limiter_checkbox_column_gutter, true);
        imgui.SetNextItemWidth(500.f);
        if (ComboSettingEnumWrapper(settings::g_mainTabSettings.native_reflex_fps_preset, "FPS limiter preset", imgui,
                                    600.f)) {
            const FpsLimiterPreset new_preset = settings::g_mainTabSettings.native_reflex_fps_preset.GetEnumValue();
            LogInfo("FPS limiter preset changed to %d", static_cast<int>(new_preset));
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Quick presets for FPS limiter when the game has native Reflex. DCPaceLock (q=1–3) is Display "
                "Commander’s pacing for frame generation with lower latency. Custom allows manual configuration.");
        }

        const bool show_custom_options = (preset == FpsLimiterPreset::kCustom);
        if (show_custom_options) {
            auto use_reflex_markers_as_fps_limiter =
                settings::g_mainTabSettings.use_reflex_markers_as_fps_limiter.GetValue();
            if (use_reflex_markers_as_fps_limiter) imgui.BeginDisabled();
            {
                if (CheckboxSetting(settings::g_mainTabSettings.use_streamline_proxy_fps_limiter,
                                    "Use Streamline proxy for FPS limiter", imgui)) {
                    LogInfo("Use Streamline proxy for FPS limiter %s",
                            settings::g_mainTabSettings.use_streamline_proxy_fps_limiter.GetValue() ? "enabled"
                                                                                                    : "disabled");
                }
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx(
                        "When enabled, FPS limiter runs on the Streamline proxy swap chain (Present/Present1).\n"
                        "Use when the game presents through Streamline's proxy (e.g. DLSS-G).");
                }
            }
            if (use_reflex_markers_as_fps_limiter) imgui.EndDisabled();
            if (CheckboxSetting(settings::g_mainTabSettings.use_reflex_markers_as_fps_limiter,
                                "Use Reflex Latency Markers as fps limiter", imgui)) {
                LogInfo(
                    "Use Reflex markers as FPS limiter %s",
                    settings::g_mainTabSettings.use_reflex_markers_as_fps_limiter.GetValue() ? "enabled" : "disabled");
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "When enabled with Frame Generation (DLSS-G) active, limits native (real) frame rate.\n"
                    "Experimental; may improve frame pacing with FG.");
            }
            {
                imgui.Indent();
                if (ComboSettingWrapper(settings::g_mainTabSettings.reflex_fps_limiter_max_queued_frames,
                                        "Max queued frames", imgui, 400.f)) {
                    LogInfo("Max queued frames changed");
                }
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx(
                        "Max frames to queue when using Reflex markers as FPS limiter. Game default = no limit; 1–6 = "
                        "limit.");
                }
                if (settings::g_mainTabSettings.reflex_fps_limiter_max_queued_frames.GetValue() > 0)
                    imgui.BeginDisabled();
                if (CheckboxSetting(settings::g_mainTabSettings.native_pacing_sim_start_only, "Native frame pacing",
                                    imgui)) {
                    LogInfo(
                        "Native pacing sim start only %s",
                        settings::g_mainTabSettings.native_pacing_sim_start_only.GetValue() ? "enabled" : "disabled");
                }
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx(
                        "When enabled, native frame pacing uses SIMULATION_START instead of PRESENT_END.\n"
                        "Matches Special-K behavior (pacing on simulation thread rather than render thread).");
                }
                imgui.Indent();
                if (CheckboxSetting(settings::g_mainTabSettings.delay_present_start_after_sim_enabled,
                                    "Schedule present start N frame times after simulation start", imgui)) {
                    LogInfo("Schedule present start after Sim Start %s",
                            settings::g_mainTabSettings.delay_present_start_after_sim_enabled.GetValue() ? "enabled"
                                                                                                         : "disabled");
                }
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx(
                        "When enabled, PRESENT_START is scheduled for (SIMULATION_START + N frame times).\n"
                        "Improves frame pacing when using native frame pacing. Use the slider to set N (0 = no delay, "
                        "1 = one frame, 0.5 = half frame, etc.).");
                }
                imgui.SameLine();
                imgui.SetNextItemWidth(400.f);
                if (SliderFloatSetting(settings::g_mainTabSettings.delay_present_start_frames, "Delay (frames)", "%.2f",
                                       imgui)) {
                }
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx("Frames to delay PRESENT_START after SIMULATION_START (0–2). 0 = no delay.");
                }
                if (settings::g_mainTabSettings.reflex_fps_limiter_max_queued_frames.GetValue() > 0)
                    imgui.EndDisabled();
                imgui.Unindent();
                imgui.Unindent();
            }
        }
    }

    // Experimental Safe Mode fps limiter (only visible if OnPresentSync mode is selected)
    // Shown when Custom FPS limiter preset, or when outside native reflex block (safe mode is not preset-controlled)
    const FpsLimiterPreset fps_limiter_preset = settings::g_mainTabSettings.native_reflex_fps_preset.GetEnumValue();
    const bool show_safe_mode = !::IsNativeFramePacingInSync() || fps_limiter_preset == FpsLimiterPreset::kCustom;
    if (show_safe_mode
        && CheckboxSetting(settings::g_mainTabSettings.safe_mode_fps_limiter, "Safe Mode fps limiter", imgui)) {
        LogInfo("Safe Mode fps limiter %s",
                settings::g_mainTabSettings.safe_mode_fps_limiter.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Uses a safer FPS limiting path with reduced risk of stutter or instability.\n"
            "Experimental; may have slightly higher latency than the default limiter.");
    }

    // ReShade runtime list (when multiple runtimes exist): select which runtime to use for DC features
    {
        const size_t runtime_count = GetReShadeRuntimeCount();
        if (runtime_count >= 2) {
            settings::g_mainTabSettings.selected_reshade_runtime_index.SetMax(static_cast<int>(runtime_count) - 1);
            int current_index = settings::g_mainTabSettings.selected_reshade_runtime_index.GetValue();
            if (current_index < 0 || static_cast<size_t>(current_index) >= runtime_count) {
                current_index = 0;
                settings::g_mainTabSettings.selected_reshade_runtime_index.SetValue(0);
            }

            std::vector<std::string> runtime_labels;
            runtime_labels.reserve(runtime_count);
            EnumerateReShadeRuntimes(
                [](size_t index, reshade::api::effect_runtime* rt, void* user_data) {
                    auto* labels = static_cast<std::vector<std::string>*>(user_data);
                    const char* api_str = "?";
                    if (rt && rt->get_device()) {
                        switch (rt->get_device()->get_api()) {
                            case reshade::api::device_api::d3d9:   api_str = "D3D9"; break;
                            case reshade::api::device_api::d3d10:  api_str = "D3D10"; break;
                            case reshade::api::device_api::d3d11:  api_str = "D3D11"; break;
                            case reshade::api::device_api::d3d12:  api_str = "D3D12"; break;
                            case reshade::api::device_api::opengl: api_str = "OpenGL"; break;
                            case reshade::api::device_api::vulkan: api_str = "Vulkan"; break;
                            default:                               break;
                        }
                    }
                    HWND hwnd = rt ? static_cast<HWND>(rt->get_hwnd()) : nullptr;
                    char buf[128];
                    if (index == 0) {
                        snprintf(buf, sizeof(buf), "%zu: %s", index + 1, api_str);
                    }
                    labels->emplace_back(buf);
                    return false;  // continue
                },
                &runtime_labels);

            const char* current_label =
                (current_index >= 0 && static_cast<size_t>(current_index) < runtime_labels.size())
                    ? runtime_labels[current_index].c_str()
                    : "Runtime 0 (first)";
            PushFpsLimiterSliderColumnAlign(imgui, fps_limiter_checkbox_column_gutter, true);
            imgui.SetNextItemWidth(600.f);
            if (imgui.BeginCombo("ReShade runtime", current_label)) {
                for (size_t i = 0; i < runtime_labels.size(); ++i) {
                    const bool selected = (static_cast<int>(i) == current_index);
                    if (imgui.Selectable(runtime_labels[i].c_str(), selected)) {
                        settings::g_mainTabSettings.selected_reshade_runtime_index.SetValue(static_cast<int>(i));
                        settings::g_mainTabSettings.selected_reshade_runtime_index.Save();
                    }
                    if (selected) {
                        imgui.SetItemDefaultFocus();
                    }
                }
                imgui.EndCombo();
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "When multiple ReShade runtimes (swapchains) exist, select which one Display Commander uses for "
                    "input blocking, Reflex, and other features. 0 = first runtime.");
            }
        }
    }

    // Limit Real Frames indicator (only visible if OnPresentSync mode is selected; shows effective value)
    //if (g_present_update_after2_called.load(std::memory_order_acquire))
    {
        bool limit_real = GetEffectiveLimitRealFrames();
        imgui.TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Limit Real Frames: %s", limit_real ? "ON" : "OFF");
    }
}

static void DrawDisplaySettings_FpsLimiterReflex(display_commander::ui::IImGuiWrapper& imgui,
                                                 const std::function<void()>& drawPclStatsCheckbox) {
    // Check if we're running on D3D9 and show warning
    const reshade::api::device_api current_api = g_last_reshade_device_api.load();
    if (current_api == reshade::api::device_api::d3d9) {
        imgui.TextColored(ui::colors::TEXT_WARNING, ICON_FK_WARNING " Warning: Reflex does not work with Direct3D 9");
    } else {
        uint64_t now_ns = utils::get_now_ns();

        // Show Native Reflex status only when streamline is used
        //if (g_present_update_after2_called.load(std::memory_order_acquire))
        {
            if (IsNativeReflexActive()) {
                imgui.TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                                  ICON_FK_OK " Native Reflex: ACTIVE Limit Real Frames: ON");
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx("The game has native Reflex support and is actively using it. ");
                }
                double native_ns = static_cast<double>(g_sleep_reflex_native_ns_smooth.load());
                double calls_per_second = native_ns <= 0 ? -1 : 1000000000.0 / native_ns;
                imgui.TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Native Reflex: %.2f times/sec (%.1f ms interval)",
                                  calls_per_second, native_ns / 1000000.0);
                if (imgui.IsItemHovered()) {
                    double raw_ns = static_cast<double>(g_sleep_reflex_native_ns.load());
                    imgui.SetTooltipEx("Smoothed interval using rolling average. Raw: %.1f ms", raw_ns / 1000000.0);
                }
            } else {
                bool limit_real = GetEffectiveLimitRealFrames();
                imgui.TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                                  ICON_FK_OK " Injected Reflex: ACTIVE Limit Real Frames: %s",
                                  limit_real ? "ON" : "OFF");
                double injected_ns = static_cast<double>(g_sleep_reflex_injected_ns_smooth.load());
                double calls_per_second = injected_ns <= 0 ? -1 : 1000000000.0 / injected_ns;
                imgui.TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Injected Reflex: %.2f times/sec (%.1f ms interval)",
                                  calls_per_second, injected_ns / 1000000.0);
                if (imgui.IsItemHovered()) {
                    double raw_ns = static_cast<double>(g_sleep_reflex_injected_ns.load());
                    imgui.SetTooltipEx("Smoothed interval using rolling average. Raw: %.1f ms", raw_ns / 1000000.0);
                }

                // Warn if both native and injected reflex are running simultaneously
                if (DidNativeReflexSleepRecently(now_ns)) {
                    imgui.TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), ICON_FK_WARNING
                                      " Warning: Both native and injected Reflex are active - this may cause "
                                      "conflicts! (FIXME)");
                }

                // Reflex-mode: restore Inject Reflex + PCL stats injection UI.
                // This callback is drawn in OnPresentSync mode elsewhere.
                drawPclStatsCheckbox();
            }
        }
    }
    // Suppress Reflex Sleep checkbox
    imgui.Spacing();
    if (CheckboxSetting(settings::g_mainTabSettings.suppress_reflex_sleep, "Suppress Reflex Sleep", imgui)) {
        LogInfo("Suppress Reflex Sleep %s",
                settings::g_mainTabSettings.suppress_reflex_sleep.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Suppresses both native Reflex sleep calls (from the game) and injected Reflex sleep calls.\n"
            "This prevents Reflex from sleeping the CPU, which may help with certain compatibility issues.");
    }
}

static void DrawDisplaySettings_FpsLimiterLatentSync(display_commander::ui::IImGuiWrapper& imgui) {
    // Scanline Offset (only visible if scanline mode is selected)
    if (SliderIntSetting(settings::g_mainTabSettings.scanline_offset, "Scanline Offset", "%d", imgui)) {
        // Setting is automatically saved by SliderIntSetting
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Scanline offset for latent sync (-1000 to 1000). This defines the offset from the "
            "threshold where frame pacing is active.");
    }

    // VBlank Sync Divisor (only visible if latent sync mode is selected)
    if (SliderIntSetting(settings::g_mainTabSettings.vblank_sync_divisor,
                         "VBlank Sync Divisor (controls FPS limit as fraction of monitor refresh rate)", "%d", imgui)) {
        // Setting is automatically saved by SliderIntSetting
    }
    if (imgui.IsItemHovered()) {
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
            if (div == 1) {
                tooltip_oss << " (Full Refresh)";
            } else if (div == 2) {
                tooltip_oss << " (Half Refresh)";
            } else {
                tooltip_oss << " (1/" << div << " Refresh)";
            }
            tooltip_oss << "\n";
        }
        tooltip_oss << "\n0 = Disabled, higher values reduce effective frame rate for smoother frame pacing.";
        imgui.SetTooltipEx("%s", tooltip_oss.str().c_str());
    }

    // VBlank Monitor Status (only visible if latent sync is enabled and FPS limit > 0)
    if (s_fps_limiter_mode.load() == FpsLimiterMode::kLatentSync) {
        if (dxgi::latent_sync::g_latentSyncManager) {
            auto& latent = dxgi::latent_sync::g_latentSyncManager->GetLatentLimiter();
            if (latent.IsVBlankMonitoringActive()) {
                imgui.Spacing();
                imgui.TextColored(ui::colors::STATUS_ACTIVE, "✁EVBlank Monitor: ACTIVE");
                if (imgui.IsItemHovered()) {
                    std::string status = latent.GetVBlankMonitorStatusString();
                    imgui.SetTooltipEx(
                        "VBlank monitoring thread is running and collecting scanline data for frame pacing.\n\n%s",
                        status.c_str());
                }

                imgui.TextColored(ui::colors::STATUS_INACTIVE, "  refresh time: %.3fms",
                                  1.0 * dxgi::fps_limiter::ns_per_refresh.load() / utils::NS_TO_MS);
                imgui.SameLine();
                imgui.TextColored(ui::colors::STATUS_INACTIVE, "  total_height: %llu",
                                  dxgi::fps_limiter::g_latent_sync_total_height.load());
                imgui.SameLine();
                imgui.TextColored(ui::colors::STATUS_INACTIVE, "  active_height: %llu",
                                  dxgi::fps_limiter::g_latent_sync_active_height.load());
            } else {
                imgui.Spacing();
                imgui.TextColored(ui::colors::STATUS_STARTING, ICON_FK_WARNING " VBlank Monitor: STARTING...");
                if (imgui.IsItemHovered()) {
                    std::string status = latent.GetVBlankMonitorStatusString();
                    imgui.SetTooltipEx(
                        "VBlank monitoring is enabled in settings but the thread is not running yet.\n\n"
                        "• %s\n\n"
                        "The thread starts when the FPS limiter runs (i.e. when a frame is presented with "
                        "VBlank Sync Divisor > 0). After start it may briefly wait for Latent Sync mode, "
                        "then bind to the display and collect scanline data for frame pacing.",
                        status.c_str());
                }
            }
        }
    }

    // Limit Real Frames (experimental; checkbox shows effective value, write updates config)
    if (enabled_experimental_features) {
        if (g_present_update_after2_called.load(std::memory_order_acquire)) {
            imgui.Spacing();
            bool limit_real = GetEffectiveLimitRealFrames();
            if (imgui.Checkbox("Limit Real Frames", &limit_real)) {
                settings::g_mainTabSettings.limit_real_frames.SetValue(limit_real);
                LogInfo(limit_real ? "Limit Real Frames enabled" : "Limit Real Frames disabled");
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
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

    // No Render / No Present in Background
    if ((g_reshade_module != nullptr)) {
        imgui.Spacing();
        bool no_render_in_bg = settings::g_mainTabSettings.no_render_in_background.GetValue();
        if (imgui.Checkbox("No Render in Background", &no_render_in_bg)) {
            settings::g_mainTabSettings.no_render_in_background.SetValue(no_render_in_bg);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Skip rendering draw calls when the game window is not in the foreground. This can save "
                "GPU power and reduce background processing.");
        }
        imgui.SameLine();
        bool no_present_in_bg = settings::g_mainTabSettings.no_present_in_background.GetValue();
        if (imgui.Checkbox("No Present in Background", &no_present_in_bg)) {
            settings::g_mainTabSettings.no_present_in_background.SetValue(no_present_in_bg);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Skip ReShade's on_present processing when the game window is not in the foreground. "
                "This can save GPU power and reduce background processing.");
        }
    }
}

static void DrawDisplaySettings_FpsLimiterAdvanced(display_commander::ui::IImGuiWrapper& imgui,
                                                 float fps_limiter_checkbox_column_gutter) {
    (void)imgui;
    CALL_GUARD_NO_TS();

    int current_item = settings::g_mainTabSettings.fps_limiter_mode.GetValue();
    if (current_item < 0 || current_item > 2) {
        current_item = (current_item < 0) ? 0 : 2;
    }
    bool enabled = settings::g_mainTabSettings.fps_limiter_enabled.GetValue();
    bool fps_limit_enabled =
        (enabled && s_fps_limiter_mode.load() != FpsLimiterMode::kLatentSync) || ShouldReflexBeEnabled();

    auto DrawPclStatsCheckbox = [&imgui]() {
        if (CheckboxSetting(settings::g_mainTabSettings.inject_reflex, "Inject Reflex", imgui)) {
            LogInfo("Inject Reflex %s", settings::g_mainTabSettings.inject_reflex.GetValue() ? "enabled" : "disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "When the game has no native Reflex, use the addon's Reflex (sleep + latency markers) for low "
                "latency.");
        }
        if (settings::g_mainTabSettings.inject_reflex.GetValue()) {
            {
                imgui.SameLine();
                const LONGLONG now_ns = utils::get_now_ns();
                const LONGLONG cutoff_ns = now_ns - static_cast<LONGLONG>(utils::SEC_TO_NS);
                static const char* const kReflexMarkerNames[] = {
                    "Sim Start", "Sim End", "Render Submit Start", "Render Submit End", "Present Start", "Present End"};
                bool all_markers_in_1s = true;
                std::string markers_not_sent;
                for (int i = 0; i < 6; ++i) {
                    if (g_injected_reflex_last_marker_time_ns[i].load(std::memory_order_relaxed) < cutoff_ns) {
                        all_markers_in_1s = false;
                        if (!markers_not_sent.empty()) markers_not_sent += ", ";
                        markers_not_sent += kReflexMarkerNames[i];
                    }
                }
                const LONGLONG last_sleep_ns = g_injected_reflex_last_sleep_time_ns.load(std::memory_order_relaxed);
                const bool sleep_in_1s = (last_sleep_ns >= cutoff_ns);
                const bool status_ok = all_markers_in_1s && sleep_in_1s;
                const LONGLONG sleep_duration_ns = g_reflex_sleep_duration_ns.load(std::memory_order_relaxed);
                const double sleep_ms = static_cast<double>(sleep_duration_ns) / static_cast<double>(utils::NS_TO_MS);
                if (status_ok) {
                    imgui.TextColored(ui::colors::ICON_SUCCESS, "Status: OK");
                } else {
                    imgui.TextColored(ui::colors::ICON_ERROR, "Status: FAIL");
                }
                if (imgui.IsItemHovered()) {
                    if (status_ok) {
                        imgui.SetTooltipEx(
                            "OK: All 6 Reflex markers (Sim Start/End, Render Submit Start/End, Present Start/End) and "
                            "Reflex sleep were observed in the last 1 s.\nReflex sleep time: %.2f ms (rolling "
                            "average).",
                            sleep_ms);
                    } else {
                        imgui.SetTooltipEx(
                            "FAILED\n"
                            "Reflex sleep in last 1 s: %s\n"
                            "Markers not sent in last 1 s: %s\n"
                            "Reflex sleep time: %.2f ms (rolling average).",
                            sleep_in_1s ? "yes" : "no", markers_not_sent.c_str(), sleep_ms);
                    }
                }
            }
            imgui.Spacing();
            bool pcl_stats = settings::g_mainTabSettings.pcl_stats_enabled.GetValue();
            if (imgui.Checkbox("PCL stats for injected reflex", &pcl_stats)) {
                settings::g_mainTabSettings.pcl_stats_enabled.SetValue(pcl_stats);
                HWND game_window = display_commanderhooks::GetGameWindow();
                if (game_window != nullptr && pcl_stats) {
                    display_commanderhooks::InstallWindowProcHooks(game_window);
                }
            }
        }
    };

    // Reflex combo: always visible; which setting is used depends on FPS Limiter Mode (and applies even when checkbox
    // off)
    if (IsReflexAvailable()) {
        PushFpsLimiterSliderColumnAlign(imgui, fps_limiter_checkbox_column_gutter, true);
        const FpsLimiterMode mode = static_cast<FpsLimiterMode>(current_item);
        bool combo_changed = false;
        if (mode == FpsLimiterMode::kOnPresentSync) {
            combo_changed =
                ComboSettingEnumWrapper(settings::g_mainTabSettings.onpresent_reflex_mode, "Reflex", imgui, 600.f);
        } else if (mode == FpsLimiterMode::kReflex) {
            combo_changed =
                ComboSettingEnumWrapper(settings::g_mainTabSettings.reflex_limiter_reflex_mode, "Reflex", imgui, 600.f);
        } else {
            combo_changed = ComboSettingEnumWrapper(settings::g_mainTabSettings.reflex_disabled_limiter_mode, "Reflex",
                                                    imgui, 600.f);
        }
        (void)combo_changed;
        if (imgui.IsItemHovered()) {
            const char* context = (mode == FpsLimiterMode::kOnPresentSync) ? "On Present Sync"
                                  : (mode == FpsLimiterMode::kReflex)      ? "Reflex FPS limiter"
                                                                           : "FPS limiter off or LatentSync";
            std::string tooltip =
                std::string("NVIDIA Reflex (used for ") + context + ").\n\n"
                + "Low latency: Enables Reflex Low Latency Mode (default).\n"
                + "Low Latency + boost: Enables both Low Latency and Boost for maximum latency reduction.\n"
                + "Off: Disables both Low Latency and Boost.\n"
                + "Game Defaults: Do not override; use the game's own Reflex settings.";
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
            imgui.SetTooltipEx("%s", tooltip.c_str());
        }
        DrawNvllNativeReflexStatusOnSameLine(imgui);
        DrawDxgiNativeReflexStatusOnSameLine(imgui);
    }

    if (current_item == static_cast<int>(FpsLimiterMode::kOnPresentSync)) {
        DrawDisplaySettings_FpsLimiterOnPresentSync(imgui, DrawPclStatsCheckbox,
                                                    fps_limiter_checkbox_column_gutter);
    }

    if (current_item == static_cast<int>(FpsLimiterMode::kReflex)) {
        DrawDisplaySettings_FpsLimiterReflex(imgui, DrawPclStatsCheckbox);
    }

    // Latent Sync Mode (only visible if Latent Sync limiter is selected)
    if (s_fps_limiter_mode.load() == FpsLimiterMode::kLatentSync) {
        DrawDisplaySettings_FpsLimiterLatentSync(imgui);
    }
}

// Context for VSync & Tearing swapchain debug tooltip (filled by PresentModeLine, consumed by SwapchainTooltip).
// desc_holder keeps the swapchain desc alive for the tooltip duration to avoid use-after-free if
// g_last_swapchain_desc_post is updated (e.g. swapchain recreated) while the tooltip is open.
struct VSyncTearingTooltipContext {
    std::shared_ptr<reshade::api::swapchain_desc> desc_holder;
    const reshade::api::swapchain_desc* desc = nullptr;
    std::string present_mode_name;
};

/// Returns present mode display name for non-DXGI APIs (Vulkan, OpenGL).
/// ReShade: present_mode is VkPresentModeKHR for Vulkan, WGL_SWAP_METHOD_ARB for OpenGL.
static const char* GetPresentModeNameNonDxgi(int device_api_value, uint32_t present_mode) {
    if (device_api_value == static_cast<int>(reshade::api::device_api::vulkan)) {
        switch (present_mode) {
            case 0:  return "IMMEDIATE (Vulkan)";
            case 1:  return "MAILBOX (Vulkan)";
            case 2:  return "FIFO (Vulkan)";
            case 3:  return "FIFO_RELAXED (Vulkan)";
            default: return "VkPresentMode (Vulkan)";
        }
    }
    if (device_api_value == static_cast<int>(reshade::api::device_api::opengl)) {
        return "OpenGL";
    }
    return "Other";
}

static void DrawDisplaySettings_VSyncAndTearing_Checkboxes_Reshade(display_commander::ui::IImGuiWrapper& imgui) {
    CALL_GUARD_NO_TS();
    const reshade::api::device_api current_api_pt = g_last_reshade_device_api.load();
    const bool is_dxgi_pt =
        (current_api_pt == reshade::api::device_api::d3d10 || current_api_pt == reshade::api::device_api::d3d11
         || current_api_pt == reshade::api::device_api::d3d12);
    if (g_reshade_create_swapchain_capture_count.load() > 0) {
        auto desc_ptr_cb = g_last_swapchain_desc_post.load();
        if (is_dxgi_pt) {
            PushFpsLimiterSliderColumnAlign(imgui, GetMainTabCheckboxColumnGutter(imgui), true);
            if (ComboSettingWrapper(settings::g_mainTabSettings.vsync_override, "VSync", imgui, 300.f)) {
                LogInfo("VSync override changed to index %d", settings::g_mainTabSettings.vsync_override.GetValue());
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Override DXGI Present SyncInterval. No override = use game setting. Force ON = VSync every "
                    "frame; 1/2-1/4 = every 2nd-4th vblank (not VRR); FORCED OFF = no VSync. Applied at runtime (no "
                    "restart).");
            }

            imgui.SameLine();
            bool prevent_t = settings::g_mainTabSettings.prevent_tearing.GetValue();
            if (imgui.Checkbox("Prevent Tearing", &prevent_t)) {
                settings::g_mainTabSettings.prevent_tearing.SetValue(prevent_t);
                LogInfo(prevent_t ? "Prevent Tearing enabled (tearing flags will be cleared)"
                                  : "Prevent Tearing disabled");
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Prevents tearing by clearing DXGI tearing flags and preferring sync.");
            }
        } else {
            bool vs_on = settings::g_mainTabSettings.force_vsync_on.GetValue();
            if (imgui.Checkbox("Force VSync ON", &vs_on)) {
                s_restart_needed_vsync_tearing.store(true);
                if (vs_on) {
                    settings::g_mainTabSettings.force_vsync_off.SetValue(false);
                }
                settings::g_mainTabSettings.force_vsync_on.SetValue(vs_on);
                LogInfo(vs_on ? "Force VSync ON enabled" : "Force VSync ON disabled");
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Forces sync interval = 1 (requires restart).");
            }
            imgui.SameLine();

            bool vs_off = settings::g_mainTabSettings.force_vsync_off.GetValue();
            if (imgui.Checkbox("Force VSync OFF", &vs_off)) {
                s_restart_needed_vsync_tearing.store(true);
                if (vs_off) {
                    settings::g_mainTabSettings.force_vsync_on.SetValue(false);
                }
                settings::g_mainTabSettings.force_vsync_off.SetValue(vs_off);
                LogInfo(vs_off ? "Force VSync OFF enabled" : "Force VSync OFF disabled");
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Forces sync interval = 0 (requires restart).");
            }
        }
        if (is_dxgi_pt) {
        } else if (desc_ptr_cb) {
            imgui.SameLine();
            imgui.TextColored(ui::colors::TEXT_DIMMED, "Present mode: %s",
                              GetPresentModeNameNonDxgi(static_cast<int>(current_api_pt), desc_ptr_cb->present_mode));
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Current swapchain present mode (Vulkan: VkPresentModeKHR, OpenGL: WGL). Read-only.");
            }
        }
    } else {
        if ((g_reshade_module != nullptr)) {
            imgui.TextColored(ui::colors::TEXT_WARNING,
                              "VSYNC ON/OFF Prevent Tearing options unavailable due to reshade bug!");
        }
    }

    const reshade::api::device_api current_api = g_last_reshade_device_api.load();
    const bool is_d3d9 = current_api == reshade::api::device_api::d3d9;
    const bool is_dxgi =
        (current_api == reshade::api::device_api::d3d10 || current_api == reshade::api::device_api::d3d11
         || current_api == reshade::api::device_api::d3d12);
    bool enable_flip = settings::g_advancedTabSettings.enable_flip_chain.GetValue();
    bool is_flip = false;
    if (is_dxgi) {
        auto desc_for_flip = g_last_swapchain_desc_post.load();
        if (desc_for_flip
            && (desc_for_flip->present_mode == DXGI_SWAP_EFFECT_FLIP_DISCARD
                || desc_for_flip->present_mode == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL)) {
            is_flip = true;
        }
    }
    static bool has_been_enabled = false;
    has_been_enabled |= is_dxgi && (enable_flip || !is_flip);


    if (has_been_enabled) {
        imgui.SameLine();
        if (imgui.Checkbox("Enable Flip Chain (requires restart)", &enable_flip)) {
            settings::g_advancedTabSettings.enable_flip_chain.SetValue(enable_flip);
            s_restart_needed_vsync_tearing.store(true);
            LogInfo(enable_flip ? "Enable Flip Chain enabled" : "Enable Flip Chain disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Forces games to use flip model swap chains (FLIP_DISCARD) for better performance.\n"
                "This setting requires a game restart to take effect.\n"
                "Only works with DirectX 10/11/12 (DXGI) games.");
        }
    }

    if (is_d3d9) {
        imgui.SameLine();
        bool enable_d9ex_with_flip = settings::g_experimentalTabSettings.d3d9_flipex_enabled.GetValue();
        if (imgui.Checkbox("Enable Flip State (requires restart)", &enable_d9ex_with_flip)) {
            settings::g_experimentalTabSettings.d3d9_flipex_enabled.SetValue(enable_d9ex_with_flip);
            LogInfo(enable_d9ex_with_flip ? "Enable D9EX with Flip Model enabled"
                                          : "Enable D9EX with Flip Model disabled");
        }
    }

    if (s_restart_needed_vsync_tearing.load()) {
        imgui.Spacing();
        imgui.TextColored(ui::colors::TEXT_ERROR, "Game restart required to apply VSync/tearing changes.");
    }

    imgui.Spacing();
}

static void DrawDisplaySettings_VSyncAndTearing_SwapchainTooltip(display_commander::ui::IImGuiWrapper& imgui,
                                                                 const VSyncTearingTooltipContext& ctx) {
    (void)imgui;
    CALL_GUARD_NO_TS();
    if (ctx.desc == nullptr) return;
    const auto& desc = *ctx.desc;
    const reshade::api::device_api api_val = g_last_reshade_device_api.load();

    imgui.TextColored(ui::colors::TEXT_LABEL, "Swapchain Information:");
    imgui.Separator();
    imgui.Text("Present Mode: %s", ctx.present_mode_name.c_str());
    imgui.Text("Present Mode ID: %u", desc.present_mode);
    if (api_val == reshade::api::device_api::vulkan) {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "Vulkan swapchain (VkPresentModeKHR, flags below)");
    } else if (api_val == reshade::api::device_api::opengl) {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "OpenGL swapchain");
    } else if (api_val == reshade::api::device_api::d3d10 || api_val == reshade::api::device_api::d3d11
               || api_val == reshade::api::device_api::d3d12) {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "DXGI swapchain");
    }

    HWND game_window = display_commanderhooks::GetGameWindow();
    if (game_window != nullptr && IsWindow(game_window)) {
        imgui.Separator();
        imgui.TextColored(ui::colors::TEXT_LABEL, "Window Information (Debug):");
        RECT window_rect = {};
        RECT client_rect = {};
        if (GetWindowRect(game_window, &window_rect) && GetClientRect(game_window, &client_rect)) {
            imgui.Text("Window Rect: (%ld, %ld) to (%ld, %ld)", window_rect.left, window_rect.top, window_rect.right,
                       window_rect.bottom);
            imgui.Text("Window Size: %ld x %ld", window_rect.right - window_rect.left,
                       window_rect.bottom - window_rect.top);
            imgui.Text("Client Rect: (%ld, %ld) to (%ld, %ld)", client_rect.left, client_rect.top, client_rect.right,
                       client_rect.bottom);
            imgui.Text("Client Size: %ld x %ld", client_rect.right - client_rect.left,
                       client_rect.bottom - client_rect.top);
        }
        LONG_PTR style = GetWindowLongPtrW(game_window, GWL_STYLE);
        LONG_PTR ex_style = GetWindowLongPtrW(game_window, GWL_EXSTYLE);
        imgui.Text("Window Style: 0x%08lX", static_cast<unsigned long>(style));
        imgui.Text("Window ExStyle: 0x%08lX", static_cast<unsigned long>(ex_style));
        bool is_popup = (style & WS_POPUP) != 0;
        bool is_child = (style & WS_CHILD) != 0;
        bool has_caption = (style & WS_CAPTION) != 0;
        bool has_border = (style & WS_BORDER) != 0;
        bool is_layered = (ex_style & WS_EX_LAYERED) != 0;
        bool is_topmost = (ex_style & WS_EX_TOPMOST) != 0;
        bool is_transparent = (ex_style & WS_EX_TRANSPARENT) != 0;
        imgui.Text("  WS_POPUP: %s", is_popup ? "Yes" : "No");
        imgui.Text("  WS_CHILD: %s", is_child ? "Yes" : "No");
        imgui.Text("  WS_CAPTION: %s", has_caption ? "Yes" : "No");
        imgui.Text("  WS_BORDER: %s", has_border ? "Yes" : "No");
        imgui.Text("  WS_EX_LAYERED: %s", is_layered ? "Yes" : "No");
        imgui.Text("  WS_EX_TOPMOST: %s", is_topmost ? "Yes" : "No");
        imgui.Text("  WS_EX_TRANSPARENT: %s", is_transparent ? "Yes" : "No");
        imgui.Separator();
        imgui.TextColored(ui::colors::TEXT_LABEL, "Size Comparison:");
        imgui.Text("Backbuffer: %ux%u", desc.back_buffer.texture.width, desc.back_buffer.texture.height);
        if (GetWindowRect(game_window, &window_rect)) {
            long window_width = window_rect.right - window_rect.left;
            long window_height = window_rect.bottom - window_rect.top;
            imgui.Text("Window: %ldx%ld", window_width, window_height);
            bool size_matches = (static_cast<long>(desc.back_buffer.texture.width) == window_width
                                 && static_cast<long>(desc.back_buffer.texture.height) == window_height);
            if (size_matches) {
                imgui.TextColored(ui::colors::TEXT_SUCCESS, "  Sizes match");
            } else {
                imgui.TextColored(ui::colors::TEXT_WARNING, "  Sizes differ (may cause Composed Flip)");
            }
        }
        imgui.Separator();
        imgui.TextColored(ui::colors::TEXT_LABEL, "Display Information:");
        HMONITOR monitor = MonitorFromWindow(game_window, MONITOR_DEFAULTTONEAREST);
        if (monitor != nullptr) {
            MONITORINFOEXW monitor_info = {};
            monitor_info.cbSize = sizeof(MONITORINFOEXW);
            if (GetMonitorInfoW(monitor, &monitor_info)) {
                imgui.Text("Monitor Rect: (%ld, %ld) to (%ld, %ld)", monitor_info.rcMonitor.left,
                           monitor_info.rcMonitor.top, monitor_info.rcMonitor.right, monitor_info.rcMonitor.bottom);
                long monitor_width = monitor_info.rcMonitor.right - monitor_info.rcMonitor.left;
                long monitor_height = monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top;
                imgui.Text("Monitor Size: %ld x %ld", monitor_width, monitor_height);
                if (GetWindowRect(game_window, &window_rect)) {
                    bool covers_monitor = (window_rect.left == monitor_info.rcMonitor.left
                                           && window_rect.top == monitor_info.rcMonitor.top
                                           && window_rect.right == monitor_info.rcMonitor.right
                                           && window_rect.bottom == monitor_info.rcMonitor.bottom);
                    if (covers_monitor) {
                        imgui.TextColored(ui::colors::TEXT_SUCCESS, "  Window covers entire monitor");
                    } else {
                        imgui.TextColored(ui::colors::TEXT_WARNING, "  Window does not cover entire monitor");
                    }
                }
            }
        }
    }

    imgui.Text("Back Buffer Count: %u", desc.back_buffer_count);
    imgui.Text("Back Buffer Size: %ux%u", desc.back_buffer.texture.width, desc.back_buffer.texture.height);
    const char* format_name = "Unknown";
    switch (desc.back_buffer.texture.format) {
        case reshade::api::format::r10g10b10a2_unorm:  format_name = "R10G10B10A2_UNORM (HDR 10-bit)"; break;
        case reshade::api::format::r16g16b16a16_float: format_name = "R16G16B16A16_FLOAT (HDR 16-bit)"; break;
        case reshade::api::format::r8g8b8a8_unorm:     format_name = "R8G8B8A8_UNORM (SDR 8-bit)"; break;
        case reshade::api::format::b8g8r8a8_unorm:     format_name = "B8G8R8A8_UNORM (SDR 8-bit)"; break;
        default:                                       format_name = "Unknown Format"; break;
    }
    imgui.Text("Back Buffer Format: %s", format_name);
    imgui.Text("Sync Interval: %u", desc.sync_interval);
    imgui.Text("Fullscreen: %s", desc.fullscreen_state ? "Yes" : "No");
    if (desc.fullscreen_state && desc.fullscreen_refresh_rate > 0) {
        imgui.Text("Refresh Rate: %.2f Hz", desc.fullscreen_refresh_rate);
    }

    imgui.Separator();
    imgui.Spacing();
    // ReShade: present_flags is DXGI_SWAP_CHAIN_FLAG (DXGI), VkSwapchainCreateFlagsKHR (Vulkan), or PFD_* (OpenGL).
    if (desc.present_flags != 0) {
        const reshade::api::device_api api_val = g_last_reshade_device_api.load();
        const bool is_dxgi_flags =
            (api_val == reshade::api::device_api::d3d10 || api_val == reshade::api::device_api::d3d11
             || api_val == reshade::api::device_api::d3d12);
        if (is_dxgi_flags) {
            imgui.Text("Swap chain creation flags (DXGI): 0x%X", desc.present_flags);
            imgui.Text("Flags:");
            if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) {
                imgui.Text("  • ALLOW_TEARING (VRR/G-Sync)");
            }
            if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) {
                imgui.Text("  • FRAME_LATENCY_WAITABLE_OBJECT");
            }
            if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_DISPLAY_ONLY) {
                imgui.Text("  • DISPLAY_ONLY");
            }
            if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_RESTRICTED_CONTENT) {
                imgui.Text("  • RESTRICTED_CONTENT");
            }
        } else if (api_val == reshade::api::device_api::vulkan) {
            imgui.Text("Swapchain creation flags (VkSwapchainCreateFlagsKHR): 0x%X", desc.present_flags);
        } else {
            imgui.Text("Creation flags: 0x%X", desc.present_flags);
        }
    }
}

static bool DrawDisplaySettings_VSyncAndTearing_PresentModeLine(display_commander::ui::IImGuiWrapper& imgui,
                                                                VSyncTearingTooltipContext* out_ctx) {
    CALL_GUARD_NO_TS();
    auto desc_ptr = g_last_swapchain_desc_post.load();
    if (!desc_ptr) {
        return false;
    }
    CALL_GUARD_NO_TS();
    const auto& desc = *desc_ptr;
    const reshade::api::device_api current_api = g_last_reshade_device_api.load();
    const bool is_d3d9 = current_api == reshade::api::device_api::d3d9;
    const bool is_dxgi =
        (current_api == reshade::api::device_api::d3d10 || current_api == reshade::api::device_api::d3d11
         || current_api == reshade::api::device_api::d3d12);

    PushFpsLimiterSliderColumnAlign(imgui, GetMainTabCheckboxColumnGutter(imgui), true);
    imgui.TextColored(ui::colors::TEXT_LABEL, "Current Present Mode:");
    imgui.SameLine();
    ImVec4 present_mode_color = ui::colors::TEXT_DIMMED;
    std::string present_mode_name = "Unknown";

    if (is_d3d9) {
        CALL_GUARD_NO_TS();
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
        imgui.TextColored(present_mode_color, "%s", present_mode_name.c_str());
        bool status_hovered = imgui.IsItemHovered();
        CALL_GUARD_NO_TS();
        if (out_ctx) {
            out_ctx->desc_holder = desc_ptr;
            out_ctx->desc = desc_ptr.get();
            out_ctx->present_mode_name = std::move(present_mode_name);
        }
        return status_hovered;
    }

    if (is_dxgi) {
        CALL_GUARD_NO_TS();
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
        imgui.TextColored(present_mode_color, "%s", present_mode_name.c_str());
        bool status_hovered = imgui.IsItemHovered();
        if (out_ctx) {
            out_ctx->desc_holder = desc_ptr;
            out_ctx->desc = desc_ptr.get();
            out_ctx->present_mode_name = std::move(present_mode_name);
        }
        return status_hovered;
    }

    // Vulkan, OpenGL, etc.: show present mode (ReShade: VkPresentModeKHR or WGL)
    CALL_GUARD_NO_TS();
    present_mode_name = GetPresentModeNameNonDxgi(static_cast<int>(current_api), desc.present_mode);
    present_mode_color = ui::colors::TEXT_DIMMED;
    imgui.TextColored(present_mode_color, "%s", present_mode_name.c_str());
    bool status_hovered = imgui.IsItemHovered();
    if (out_ctx) {
        out_ctx->desc_holder = desc_ptr;
        out_ctx->desc = desc_ptr.get();
        out_ctx->present_mode_name = std::move(present_mode_name);
    }
    return status_hovered;
}

void DrawDisplaySettings_VSyncAndTearing(display_commander::ui::IImGuiWrapper& imgui) {
    CALL_GUARD_NO_TS();

    g_rendering_ui_section.store("ui:tab:main_new:vsync_tearing", std::memory_order_release);
    ui::colors::PushHeader2Colors(&imgui);
    const bool vsync_tearing_open = imgui.CollapsingHeader("VSync & Tearing", ImGuiTreeNodeFlags_None);
    ui::colors::PopCollapsingHeaderColors(&imgui);
    if (vsync_tearing_open) {
        imgui.Indent();
        DrawDisplaySettings_VSyncAndTearing_Checkboxes_Reshade(imgui);

        VSyncTearingTooltipContext tooltip_ctx;
        bool status_hovered = DrawDisplaySettings_VSyncAndTearing_PresentModeLine(imgui, &tooltip_ctx);
        g_rendering_ui_section.store("ui:tab:main_new:vsync_tearing:present_mode_line", std::memory_order_release);
        if (status_hovered && tooltip_ctx.desc != nullptr) {
            imgui.BeginTooltip();
            DrawDisplaySettings_VSyncAndTearing_SwapchainTooltip(imgui, tooltip_ctx);
            imgui.EndTooltip();
        }
        g_rendering_ui_section.store("ui:tab:main_new:vsync_tearing:swapchain_tooltip", std::memory_order_release);

        if (!g_last_swapchain_desc_post.load()) {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "No swapchain information available");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "No game detected or swapchain not yet created.\nThis information will appear once a game is "
                    "running.");
            }
        }
        imgui.Unindent();
    }
    g_rendering_ui_section.store("ui:tab:main_new:vsync_tearing:end", std::memory_order_release);
}

void MarkRestartNeededVsyncTearing() {
    s_restart_needed_vsync_tearing.store(true);
}


void DrawDisplaySettings(display_commander::ui::GraphicsApi api, display_commander::ui::IImGuiWrapper& imgui,
                         reshade::api::effect_runtime* runtime) {
    (void)api;
    CALL_GUARD_NO_TS();
    DrawDisplaySettings_DisplayAndTarget(imgui, runtime);
    DrawDisplaySettings_WindowModeAndApply(imgui);
    DrawDisplaySettings_FpsLimiter(imgui);

    // Show graphics/API libraries loaded by the host (game), not by Display Commander or ReShade
    if (enabled_experimental_features) {
        /*
        const std::string host_apis = display_commanderhooks::GetHostLoadedGraphicsApisString();
        if (!host_apis.empty()) {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "APIs (loaded by host): %s", host_apis.c_str());
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Graphics/API libraries that the game (or host process) loaded via LoadLibrary.\n"
                    "Excludes loads where the caller was Display Commander or ReShade.");
            }
        }*/
        std::string traffic_apis = display_commanderhooks::GetPresentTrafficApisString();
        const bool smooth_motion_loaded = g_smooth_motion_dll_loaded.load(std::memory_order_relaxed);
        if (smooth_motion_loaded) {
            if (!traffic_apis.empty()) traffic_apis += ", ";
            traffic_apis += "Smooth Motion";
        }
        if (!traffic_apis.empty()) {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "Active APIs: %s", traffic_apis.c_str());
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Graphics APIs where we observed present/swap traffic in the last 1 second (our hooks were "
                    "called).\n"
                    "DXGI = IDXGISwapChain::Present, D3D9 = IDirect3DDevice9::Present, OpenGL32 = wglSwapBuffers, "
                    "DDraw = IDirectDrawSurface::Flip.\n"
                    "Smooth Motion = nvpresent64.dll or nvpresent32.dll is loaded (NVIDIA driver frame generation).");
            }
        }
    }
    DrawDisplaySettings_VSyncAndTearing(imgui);
}

void DrawWindowControlButtons(display_commander::ui::IImGuiWrapper& imgui) {
    (void)imgui;
    CALL_GUARD_NO_TS();
    HWND hwnd = g_last_swapchain_hwnd.load();
    if (hwnd == nullptr) {
        LogWarn("Maximize Window: no window handle available");
        return;
    }
    // Window Control Buttons (Minimize, Restore, and Maximize side by side)
    imgui.BeginGroup();

    // Minimize Window Button
    imgui.PushStyleColor(ImGuiCol_Text, ui::colors::ICON_ACTION);
    if (imgui.Button(ICON_FK_MINUS " Minimize Window")) {
        HWND hwnd = g_last_swapchain_hwnd.load();
        std::thread([hwnd]() {
            LogDebug("Minimize Window button pressed (bg thread)");
            ShowWindow(hwnd, SW_MINIMIZE);
        }).detach();
    }
    imgui.PopStyleColor();
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Minimize the current game window.");
    }


    imgui.SameLine();

    // Close Window Button (graceful close via WM_CLOSE)
    imgui.PushStyleColor(ImGuiCol_Text, ui::colors::ICON_ACTION);
    if (imgui.Button(ICON_FK_CANCEL " Close")) {
        HWND close_hwnd = g_last_swapchain_hwnd.load();
        std::thread([close_hwnd]() {
            LogDebug("Close button pressed (bg thread)");
            PostMessageW(close_hwnd, WM_CLOSE, 0, 0);
        }).detach();
    }
    imgui.PopStyleColor();
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Request graceful close of the game window (sends WM_CLOSE).");
    }

    imgui.SameLine();

    // Open Game Folder Button
    imgui.PushStyleColor(ImGuiCol_Text, ui::colors::ICON_ACTION);
    if (imgui.Button(ICON_FK_FOLDER_OPEN " Open Game Folder")) {
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
    imgui.PopStyleColor();
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Open the game's installation folder in Windows Explorer.");
    }

    imgui.SameLine();

    // Open DisplayCommander.ini (config) Button
    imgui.PushStyleColor(ImGuiCol_Text, ui::colors::ICON_ACTION);
    if (imgui.Button(ICON_FK_FILE " Config")) {
        std::string config_path =
            display_commander::config::DisplayCommanderConfigManager::GetInstance().GetConfigPath();
        if (!config_path.empty()) {
            std::thread([config_path]() {
                LogDebug("Open DisplayCommander.toml button pressed (bg thread)");
                LogInfo("Opening DisplayCommander.ini: %s", config_path.c_str());
                HINSTANCE result = ShellExecuteA(nullptr, "open", config_path.c_str(), nullptr, nullptr, SW_SHOW);
                if (reinterpret_cast<intptr_t>(result) <= 32) {
                    LogError("Failed to open DisplayCommander.ini: %s (Error: %ld)", config_path.c_str(),
                             reinterpret_cast<intptr_t>(result));
                } else {
                    LogInfo("Successfully opened DisplayCommander.ini: %s", config_path.c_str());
                }
            }).detach();
        }
    }
    imgui.PopStyleColor();
    if (imgui.IsItemHovered()) {
        std::string config_path =
            display_commander::config::DisplayCommanderConfigManager::GetInstance().GetConfigPath();
        if (!config_path.empty()) {
            imgui.SetTooltipEx("Open DisplayCommander config in the default text editor.\nFull path: %s",
                               config_path.c_str());
        } else {
            imgui.SetTooltipEx("Open DisplayCommander.ini (config path not available).");
        }
    }

    imgui.SameLine();

    // Open DisplayCommander.log Button
    imgui.PushStyleColor(ImGuiCol_Text, ui::colors::ICON_ACTION);
    if (imgui.Button(ICON_FK_FILE " Log")) {
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
    imgui.PopStyleColor();
    if (imgui.IsItemHovered()) {
        char process_path[MAX_PATH];
        if (GetModuleFileNameA(nullptr, process_path, MAX_PATH) != 0) {
            std::string full_path(process_path);
            size_t last_slash = full_path.find_last_of("\\/");
            if (last_slash != std::string::npos) {
                std::string log_path = full_path.substr(0, last_slash) + "\\DisplayCommander.log";
                imgui.SetTooltipEx("Open DisplayCommander.log in the default text editor.\nFull path: %s",
                                   log_path.c_str());
            } else {
                imgui.SetTooltipEx("Open DisplayCommander.log in the default text editor.");
            }
        } else {
            imgui.SetTooltipEx("Open DisplayCommander.log in the default text editor.");
        }
    }

    if ((g_reshade_module != nullptr)) {
        imgui.SameLine();

        // Open reshade.log Button
        imgui.PushStyleColor(ImGuiCol_Text, ui::colors::ICON_ACTION);
        if (imgui.Button(ICON_FK_FILE " reshade.log")) {
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
        imgui.PopStyleColor();
        if (imgui.IsItemHovered()) {
            char process_path[MAX_PATH];
            if (GetModuleFileNameA(nullptr, process_path, MAX_PATH) != 0) {
                std::string full_path(process_path);
                size_t last_slash = full_path.find_last_of("\\/");
                if (last_slash != std::string::npos) {
                    std::string log_path = full_path.substr(0, last_slash) + "\\reshade.log";
                    imgui.SetTooltipEx("Open reshade.log in the default text editor.\nFull path: %s", log_path.c_str());
                } else {
                    imgui.SetTooltipEx("Open reshade.log in the default text editor.");
                }
            } else {
                imgui.SetTooltipEx("Open reshade.log in the default text editor.");
            }
        }

        imgui.SameLine();

        // Open reshade.ini Button
        imgui.PushStyleColor(ImGuiCol_Text, ui::colors::ICON_ACTION);
        if (imgui.Button(ICON_FK_FILE " reshade.ini")) {
            std::thread([]() {
                LogDebug("Open reshade.ini button pressed (bg thread)");

                char process_path[MAX_PATH];
                DWORD path_length = GetModuleFileNameA(nullptr, process_path, MAX_PATH);

                if (path_length == 0) {
                    LogError("Failed to get current process path for reshade.ini opening");
                    return;
                }

                std::string full_path(process_path);
                size_t last_slash = full_path.find_last_of("\\/");

                if (last_slash == std::string::npos) {
                    LogError("Invalid process path format: %s", full_path.c_str());
                    return;
                }

                std::string ini_path = full_path.substr(0, last_slash) + "\\reshade.ini";
                LogInfo("Opening reshade.ini: %s", ini_path.c_str());

                HINSTANCE result = ShellExecuteA(nullptr, "open", ini_path.c_str(), nullptr, nullptr, SW_SHOW);

                if (reinterpret_cast<intptr_t>(result) <= 32) {
                    LogError("Failed to open reshade.ini: %s (Error: %ld)", ini_path.c_str(),
                             reinterpret_cast<intptr_t>(result));
                } else {
                    LogInfo("Successfully opened reshade.ini: %s", ini_path.c_str());
                }
            }).detach();
        }
        imgui.PopStyleColor();
        if (imgui.IsItemHovered()) {
            char process_path[MAX_PATH];
            if (GetModuleFileNameA(nullptr, process_path, MAX_PATH) != 0) {
                std::string full_path(process_path);
                size_t last_slash = full_path.find_last_of("\\/");
                if (last_slash != std::string::npos) {
                    std::string ini_path = full_path.substr(0, last_slash) + "\\reshade.ini";
                    imgui.SetTooltipEx("Open reshade.ini (ReShade config) in the default text editor.\nFull path: %s",
                                       ini_path.c_str());
                } else {
                    imgui.SetTooltipEx("Open reshade.ini (ReShade config) in the default text editor.");
                }
            } else {
                imgui.SetTooltipEx("Open reshade.ini (ReShade config) in the default text editor.");
            }
        }
    }

    imgui.EndGroup();
}

void DrawAdhdMultiMonitorControls(display_commander::ui::IImGuiWrapper& imgui) {
    CALL_GUARD_NO_TS();
    // Black curtain (game display) is shown even with one monitor; other displays only when multiple monitors
    bool hasMultipleMonitors = adhd_multi_monitor::api::HasMultipleMonitors();

    imgui.BeginGroup();
    // Use CheckboxSetting so the checkbox always reflects the current setting (e.g. when toggled via hotkey)
    if (CheckboxSetting(settings::g_mainTabSettings.adhd_single_monitor_enabled_for_game_display,
                        "Black curtain (game display)", imgui)) {
        LogInfo("Black curtain (game display) %s",
                settings::g_mainTabSettings.adhd_single_monitor_enabled_for_game_display.GetValue() ? "enabled"
                                                                                                    : "disabled");
    }

    if (hasMultipleMonitors) {
        imgui.SameLine();
        if (CheckboxSetting(settings::g_mainTabSettings.adhd_multi_monitor_enabled, "Black curtain (other displays)",
                            imgui)) {
            LogInfo("Black curtain (other displays) %s",
                    settings::g_mainTabSettings.adhd_multi_monitor_enabled.GetValue() ? "enabled" : "disabled");
        }
    }
    imgui.Unindent();
    imgui.EndGroup();
    if (imgui.IsItemHovered()) {
        adhd_multi_monitor::BackgroundWindowDebugInfo info = {};
        adhd_multi_monitor::api::GetBackgroundWindowDebugInfo(&info);
        char buf[384];
        int n = std::snprintf(buf, sizeof(buf),
                              "Black curtain (game display): black window on the game's monitor.\n"
                              "Black curtain (other displays): covers all other monitors.\n\n"
                              "Background window: HWND %p, %s\n"
                              "Position: (%d, %d), Size: %d x %d\n"
                              "Visible: %s",
                              info.hwnd, info.not_null ? "not null" : "null", info.left, info.top, info.width,
                              info.height, info.is_visible ? "yes" : "no");
        if (n > 0 && n < static_cast<int>(sizeof(buf))) {
            imgui.SetTooltipEx("%s", buf);
        } else {
            imgui.SetTooltipEx(
                "Black curtain (game display): black window on the game's monitor.\n"
                "Black curtain (other displays): covers all other monitors.");
        }
    }
}

}  // namespace ui::new_ui
