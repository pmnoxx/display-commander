// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
// Headers <Display Commander>
#include "performance_overlay_internal.hpp"
#include "dxgi/vram_info.hpp"
#if !defined(DC_LITE)
#include "features/nvidia_profile_inspector/nvidia_profile_inspector.hpp"
#endif
#include "globals.hpp"
#include "hooks/nvidia/ngx_hooks.hpp"
#include "latency/reflex_provider.hpp"
#include "latent_sync/refresh_rate_monitor_integration.hpp"
#include "modules/module_registry.hpp"
#include "nvapi/gpu_dynamic_utilization.hpp"
#include "nvapi/vrr_status.hpp"
#include "settings/swapchain_tab_settings.hpp"
#include "swapchain_events.hpp"
#include "utils.hpp"

// Libraries <ReShade / ImGui>
#include <imgui.h>

// Libraries <Standard C++>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#if !defined(DC_LITE)
#include <memory>
#endif

// Libraries <Windows.h>
#include <Windows.h>

// Libraries <Windows>
#include <psapi.h>

namespace ui::new_ui {

namespace {

// Overlay label column (col 0): None = no label (value only in col 1). Short = compact token. Full = readable phrase.
// Examples: Time / Local time, Play / Playtime, Present / Present FPS, NV VRR / VRR (NVAPI), Hz / Measured refresh
// (DXGI), VRR / VRR (estimate) — swap-chain heuristic row; tooltips name DXGI/GetFrameStatistics for advanced users.
const char* OverlayCol0Label(OverlayLabelMode m, const char* short_s, const char* full_s) {
    if (m == OverlayLabelMode::kNone) return nullptr;
    if (m == OverlayLabelMode::kShort) return short_s;
    return full_s;
}

void OverlayScalarTableBegin(display_commander::ui::IImGuiWrapper& imgui) {
    const int table_flags = static_cast<int>(ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX
                                             | ImGuiTableFlags_NoPadOuterX);
    imgui.BeginTable("##dc_perf_scalar", 2, table_flags);
}

void OverlayTableRow_TextUnformatted(display_commander::ui::IImGuiWrapper& imgui, OverlayLabelMode m,
                                     const char* short_lbl, const char* full_lbl, const char* value_utf8,
                                     bool show_tooltips = false, const char* tooltip_ex = nullptr) {
    imgui.TableNextRow();
    imgui.TableNextColumn();
    const char* lb = OverlayCol0Label(m, short_lbl, full_lbl);
    if (lb != nullptr) {
        imgui.TextUnformatted(lb);
    }
    imgui.TableNextColumn();
    imgui.TextUnformatted(value_utf8);
    if (show_tooltips && tooltip_ex != nullptr && imgui.IsItemHovered()) {
        imgui.SetTooltipEx("%s", tooltip_ex);
    }
}

void OverlayTableRow_Text(display_commander::ui::IImGuiWrapper& imgui, OverlayLabelMode m, const char* short_lbl,
                          const char* full_lbl, bool show_tooltips, const char* tooltip_ex, const char* fmt, ...) {
    char buf[320];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';
    OverlayTableRow_TextUnformatted(imgui, m, short_lbl, full_lbl, buf, show_tooltips, tooltip_ex);
}

void OverlayTableRow_TextColoredV(display_commander::ui::IImGuiWrapper& imgui, OverlayLabelMode m,
                                  const char* short_lbl, const char* full_lbl, const ImVec4& col, bool show_tooltips,
                                  const char* tooltip_ex, const char* fmt, va_list ap) {
    char buf[320];
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    buf[sizeof(buf) - 1] = '\0';
    imgui.TableNextRow();
    imgui.TableNextColumn();
    const char* lb = OverlayCol0Label(m, short_lbl, full_lbl);
    if (lb != nullptr) {
        imgui.TextUnformatted(lb);
    }
    imgui.TableNextColumn();
    imgui.TextColored(col, "%s", buf);
    if (show_tooltips && tooltip_ex != nullptr && imgui.IsItemHovered()) {
        imgui.SetTooltipEx("%s", tooltip_ex);
    }
}

void OverlayTableRow_TextColored(display_commander::ui::IImGuiWrapper& imgui, OverlayLabelMode m,
                                 const char* short_lbl, const char* full_lbl, const ImVec4& col, bool show_tooltips,
                                 const char* tooltip_ex, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    OverlayTableRow_TextColoredV(imgui, m, short_lbl, full_lbl, col, show_tooltips, tooltip_ex, fmt, ap);
    va_end(ap);
}

void DrawOverlayGpuMemoryTable(display_commander::ui::IImGuiWrapper& imgui, OverlayLabelMode label_mode,
                               bool show_tooltips, bool show_overlay_nvapi_gpu_util, bool show_overlay_vram,
                               bool show_overlay_ram) {
    const bool table_any = show_overlay_nvapi_gpu_util || show_overlay_vram || show_overlay_ram;
    if (!table_any) {
        return;
    }
    OverlayScalarTableBegin(imgui);
    if (show_overlay_nvapi_gpu_util) {
        nvapi::RequestGpuDynamicUtilizationFromOverlay(true);
        unsigned gpu_pct = 0;
        if (nvapi::GetCachedGpuDynamicUtilizationPercent(gpu_pct)) {
            const double raw = static_cast<double>(gpu_pct);
            static double smoothed_nv_gpu_util = 0.0;
            static double displayed_nv_gpu_util = 0.0;
            static LONGLONG s_nv_gpu_util_last_display_ns = 0;
            constexpr double k_nv_gpu_alpha = 0.1;
            smoothed_nv_gpu_util = (1.0 - k_nv_gpu_alpha) * smoothed_nv_gpu_util + k_nv_gpu_alpha * raw;
            const LONGLONG now_ns_nv = utils::get_now_ns();
            constexpr LONGLONG k_nv_gpu_display_interval_ns =
                static_cast<LONGLONG>(0.2 * static_cast<double>(utils::SEC_TO_NS));
            if (now_ns_nv - s_nv_gpu_util_last_display_ns >= k_nv_gpu_display_interval_ns) {
                s_nv_gpu_util_last_display_ns = now_ns_nv;
                displayed_nv_gpu_util = smoothed_nv_gpu_util;
            }
            OverlayTableRow_Text(
                imgui, label_mode, "GPU", "GPU usage", show_tooltips,
                "NVIDIA GPU engine utilization from NvAPI_GPU_GetDynamicPstatesInfoEx (~1 s rolling average, "
                "first physical GPU).",
                "%.1f%%", displayed_nv_gpu_util);
        }
    }
    if (show_overlay_vram) {
        uint64_t vram_used = 0;
        uint64_t vram_total = 0;
        if (display_commander::dxgi::GetVramInfo(&vram_used, &vram_total) && vram_total > 0) {
            const uint64_t used_mib = vram_used / (1024ULL * 1024ULL);
            const uint64_t total_mib = vram_total / (1024ULL * 1024ULL);
            char buf[96];
            (void)snprintf(buf, sizeof(buf), "%llu / %llu MiB", static_cast<unsigned long long>(used_mib),
                           static_cast<unsigned long long>(total_mib));
            OverlayTableRow_TextUnformatted(imgui, label_mode, "VRAM", "VRAM", buf, show_tooltips,
                                            "GPU video memory used / budget (DXGI adapter memory budget).");
        } else {
            OverlayTableRow_TextColored(imgui, label_mode, "VRAM", "VRAM", ui::colors::TEXT_DIMMED, show_tooltips,
                                        "VRAM unavailable (DXGI adapter or budget query failed).", "%s", "N/A");
        }
    }
    if (show_overlay_ram) {
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
            char buf[128];
            if (have_process) {
                (void)snprintf(buf, sizeof(buf), "%llu (%llu) / %llu MiB",
                               static_cast<unsigned long long>(ram_used_mib),
                               static_cast<unsigned long long>(process_mib),
                               static_cast<unsigned long long>(ram_total_mib));
            } else {
                (void)snprintf(buf, sizeof(buf), "%llu / %llu MiB", static_cast<unsigned long long>(ram_used_mib),
                               static_cast<unsigned long long>(ram_total_mib));
            }
            OverlayTableRow_TextUnformatted(
                imgui, label_mode, "RAM", "RAM", buf, show_tooltips,
                "System RAM in use (this app working set) / total (GlobalMemoryStatusEx, GetProcessMemoryInfo).");
        } else {
            OverlayTableRow_TextColored(imgui, label_mode, "RAM", "RAM", ui::colors::TEXT_DIMMED, show_tooltips,
                                        "System memory info unavailable.", "%s", "N/A");
        }
    }
    imgui.EndTable();
}

}  // namespace

void DrawPerformanceOverlayContent(display_commander::ui::IImGuiWrapper& imgui,
                                   display_commander::ui::GraphicsApi device_api, bool show_tooltips) {
    CALL_GUARD_NO_TS();
    reshade::api::device_api current_api = static_cast<reshade::api::device_api>(0);
    switch (device_api) {
        case display_commander::ui::GraphicsApi::D3D9:   current_api = reshade::api::device_api::d3d9; break;
        case display_commander::ui::GraphicsApi::D3D10:  current_api = reshade::api::device_api::d3d10; break;
        case display_commander::ui::GraphicsApi::D3D11:  current_api = reshade::api::device_api::d3d11; break;
        case display_commander::ui::GraphicsApi::D3D12:  current_api = reshade::api::device_api::d3d12; break;
        case display_commander::ui::GraphicsApi::OpenGL: current_api = reshade::api::device_api::opengl; break;
        case display_commander::ui::GraphicsApi::Vulkan: current_api = reshade::api::device_api::vulkan; break;
        default:                                         break;
    }
    (void)current_api;

    const OverlayLabelMode label_mode = settings::g_mainTabSettings.overlay_label_mode.GetEnumValue();

    bool show_present_fps = settings::g_mainTabSettings.show_fps_counter.GetValue();
    bool show_vrr_status = settings::g_mainTabSettings.show_vrr_status.GetValue();
    bool show_volume = settings::g_experimentalTabSettings.show_volume.GetValue();
    bool show_gpu_measurement = (settings::g_mainTabSettings.gpu_measurement_enabled.GetValue() != 0);
    bool show_frame_time_graph = settings::g_mainTabSettings.show_frame_time_graph.GetValue();
    bool show_native_frame_time_graph = settings::g_mainTabSettings.show_native_frame_time_graph.GetValue();
    bool show_cpu_usage = settings::g_mainTabSettings.show_cpu_usage.GetValue();
    bool show_overlay_nvapi_gpu_util = settings::g_mainTabSettings.show_overlay_nvapi_gpu_util.GetValue();
    bool show_fg_mode = settings::g_mainTabSettings.show_fg_mode.GetValue();
    bool show_overlay_resolution = settings::g_mainTabSettings.show_overlay_resolution.GetValue();
    bool show_dlss_status = settings::g_mainTabSettings.show_dlss_status.GetValue();
    bool show_dlss_quality_preset = settings::g_mainTabSettings.show_dlss_quality_preset.GetValue();
#if !defined(DC_LITE)
    bool show_driver_dlss_sr_preset = settings::g_mainTabSettings.show_driver_dlss_sr_preset.GetValue();
    bool show_driver_dlss_rr_preset = settings::g_mainTabSettings.show_driver_dlss_rr_preset.GetValue();
#else
    const bool show_driver_dlss_sr_preset = false;
    const bool show_driver_dlss_rr_preset = false;
#endif
    bool show_fps_limiter_src = settings::g_mainTabSettings.show_fps_limiter_src.GetValue();
    bool show_overlay_vram = settings::g_mainTabSettings.show_overlay_vram.GetValue();
    bool show_overlay_ram = settings::g_mainTabSettings.show_overlay_ram.GetValue();
    bool show_dxgi_vrr_status = settings::g_mainTabSettings.show_dxgi_vrr_status.GetValue();
    bool show_dxgi_refresh_rate = settings::g_mainTabSettings.show_dxgi_refresh_rate.GetValue();
    bool show_overlay_nvapi_sim_duration = settings::g_mainTabSettings.show_overlay_nvapi_sim_duration.GetValue();
    bool show_overlay_nvapi_sim_end_to_rs_start =
        settings::g_mainTabSettings.show_overlay_nvapi_sim_end_to_rs_start.GetValue();
    bool show_overlay_nvapi_rs_submit_duration =
        settings::g_mainTabSettings.show_overlay_nvapi_rs_submit_duration.GetValue();
    bool show_overlay_nvapi_rs_start_to_present_start =
        settings::g_mainTabSettings.show_overlay_nvapi_rs_start_to_present_start.GetValue();
    bool show_overlay_nvapi_rs_end_to_present_start =
        settings::g_mainTabSettings.show_overlay_nvapi_rs_end_to_present_start.GetValue();
    bool show_overlay_nvapi_present_phase_duration =
        settings::g_mainTabSettings.show_overlay_nvapi_present_phase_duration.GetValue();
    bool show_overlay_nvapi_present_end_to_rs_end =
        settings::g_mainTabSettings.show_overlay_nvapi_present_end_to_rs_end.GetValue();
    bool show_overlay_nvapi_gpu_active_ms = settings::g_mainTabSettings.show_overlay_nvapi_gpu_active_ms.GetValue();
    bool show_overlay_nvapi_latency_jitter_abs =
        settings::g_mainTabSettings.show_overlay_nvapi_latency_jitter_abs.GetValue();
    bool show_overlay_nvapi_setlatencymarker_threads =
        settings::g_mainTabSettings.show_overlay_nvapi_setlatencymarker_threads.GetValue();

    const bool show_clock_setting = settings::g_mainTabSettings.show_clock.GetValue();
    const bool show_playtime_setting = settings::g_mainTabSettings.show_playtime.GetValue();
    const LONGLONG game_start_time_ns_for_playtime = g_game_start_time_ns.load();
    const bool playtime_row_valid = show_playtime_setting && game_start_time_ns_for_playtime > 0;

    // ----- Table: frame rate + limiter (optional clock/playtime rows first) -----
    bool table1_any = show_clock_setting || playtime_row_valid || settings::g_mainTabSettings.show_native_fps.GetValue();
    bool fps_samples_ok = false;
    double average_fps = 0.0;
    if (show_present_fps || settings::g_mainTabSettings.show_native_fps.GetValue()) {
        const uint32_t head = ::g_perf_ring.GetHead();
        const uint32_t count = ::g_perf_ring.GetCountFromHead(head);
        double total_time = 0.0;
        uint32_t sample_count = 0;
        for (uint32_t i = 0; i < count && i < ::kPerfRingCapacity; ++i) {
            const ::PerfSample sample = ::g_perf_ring.GetSampleWithHead(i, head);
            if (sample.dt == 0.0f || total_time >= 1.0) break;
            sample_count++;
            total_time += sample.dt;
        }
        if (sample_count > 0 && total_time >= 1.0) {
            fps_samples_ok = true;
            average_fps = sample_count / total_time;
            table1_any = true;
        }
    }
    if (show_fps_limiter_src) {
        table1_any = true;
    }

    if (table1_any) {
        OverlayScalarTableBegin(imgui);
        if (show_clock_setting) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            char tbuf[16];
            (void)snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d", static_cast<int>(st.wHour),
                           static_cast<int>(st.wMinute), static_cast<int>(st.wSecond));
            OverlayTableRow_TextUnformatted(imgui, label_mode, "Time", "Local time", tbuf);
        }
        if (playtime_row_valid) {
            LONGLONG now_ns_pt = utils::get_now_ns();
            LONGLONG playtime_ns = now_ns_pt - game_start_time_ns_for_playtime;
            double playtime_seconds = static_cast<double>(playtime_ns) / static_cast<double>(utils::SEC_TO_NS);
            int hours = static_cast<int>(playtime_seconds / 3600.0);
            int minutes = static_cast<int>((playtime_seconds - (hours * 3600.0)) / 60.0);
            int seconds = static_cast<int>(playtime_seconds - (hours * 3600.0) - (minutes * 60.0));
            OverlayTableRow_Text(imgui, label_mode, "Play", "Playtime", show_tooltips,
                                 "Playtime: Time elapsed since game start", "%02d:%02d:%02d", hours, minutes, seconds);
        }
        if (fps_samples_ok && show_present_fps) {
            char buf[64];
            (void)snprintf(buf, sizeof(buf), "%.1f fps", average_fps);
            OverlayTableRow_TextUnformatted(imgui, label_mode, "Present", "Present FPS", buf, show_tooltips,
                                            "FPS measured on the present path (ReShade overlay sampling).");
        }
        if (settings::g_mainTabSettings.show_native_fps.GetValue()) {
            char buf[64];
            uint64_t last_sleep_timestamp = ::g_nvapi_last_sleep_timestamp_ns.load();
            uint64_t current_time = utils::get_now_ns();
            bool is_recent =
                (last_sleep_timestamp > 0) && (current_time - last_sleep_timestamp) < (5 * utils::SEC_TO_NS);
            LONGLONG native_sleep_ns_smooth = ::g_sleep_reflex_native_ns_smooth.load();
            double native_fps = 0.0;
            if (is_recent && native_sleep_ns_smooth > 0 && native_sleep_ns_smooth < 1 * utils::SEC_TO_NS) {
                native_fps = static_cast<double>(utils::SEC_TO_NS) / static_cast<double>(native_sleep_ns_smooth);
            }
            if (native_fps > 0.0) {
                (void)snprintf(buf, sizeof(buf), "%.1f fps", native_fps);
                OverlayTableRow_TextUnformatted(
                    imgui, label_mode, "Native", "Native FPS", buf, show_tooltips,
                    "Estimated display-side FPS from native Reflex sleep smoothing (requires active native Reflex).");
            }
        }

        if (show_fps_limiter_src) {
            const char* src_name = GetChosenFpsLimiterSiteName();
            OverlayTableRow_TextUnformatted(imgui, label_mode, "Lim src", "FPS limiter source", src_name, show_tooltips,
                                            "Which hook site is applying the FPS limiter.");
        }
        imgui.EndTable();
    }

    // ----- Refresh / VRR: shared cache for NVAPI table row + debug overlay -----
    static dxgi::fps_limiter::RefreshRateStats cached_vrr_stats{};
    static LONGLONG last_valid_vrr_sample_ns = 0;
    const bool show_vrr_debug_mode = settings::g_mainTabSettings.vrr_debug_mode.GetValue();
    const bool vrr_overlay_cache = show_vrr_status || show_vrr_debug_mode;
    LONGLONG now_ns_vrr = 0;
    bool has_recent_vrr_sample = false;
    if (vrr_overlay_cache) {
        now_ns_vrr = utils::get_now_ns();
        auto shared_stats_vrr = g_cached_refresh_rate_stats.load();
        if (shared_stats_vrr && shared_stats_vrr->is_valid && shared_stats_vrr->sample_count > 0) {
            cached_vrr_stats = *shared_stats_vrr;
            last_valid_vrr_sample_ns = now_ns_vrr;
        }
        const LONGLONG sample_timeout_ns = 1000 * utils::NS_TO_MS;
        has_recent_vrr_sample = (now_ns_vrr - last_valid_vrr_sample_ns) < sample_timeout_ns;
    }

    dxgi::fps_limiter::RefreshRateStats dxgi_stats{};
    const bool need_dxgi_stats = show_dxgi_refresh_rate || show_dxgi_vrr_status;
    if (need_dxgi_stats) {
        dxgi_stats = dxgi::fps_limiter::GetRefreshRateStats();
    }

    // ----- Table: refresh rates (DXGI measured) + VRR summary rows -----
    bool table2_any = show_dxgi_refresh_rate || show_vrr_status || show_dxgi_vrr_status;
    double dxgi_hz_live = 0.0;
    if (show_dxgi_refresh_rate) {
        dxgi_hz_live = dxgi::fps_limiter::GetSmoothedRefreshRate();
    }

    if (table2_any) {
        OverlayScalarTableBegin(imgui);
        if (show_dxgi_refresh_rate) {
            if (dxgi_hz_live > 0.0) {
                OverlayTableRow_Text(
                    imgui, label_mode, "Hz", "Refresh Rate", show_tooltips,
                    "From swap chain GetFrameStatistics (RefreshRateMonitor). Enable DXGI refresh rate / VRR detection "
                    "in the Debug DXGI refresh tab (-DebugTabs build) or via addon config.",
                    "%.1f Hz", dxgi_hz_live);
            } else {
                OverlayTableRow_TextColored(
                    imgui, label_mode, "Hz", "Refresh Rate", ui::colors::TEXT_DIMMED, show_tooltips,
                    "From swap chain GetFrameStatistics (RefreshRateMonitor). Enable DXGI refresh rate / VRR detection "
                    "in the Debug DXGI refresh tab (-DebugTabs build) or via addon config.",
                    "%s", "-- Hz");
            }
        }
        if (show_vrr_status) {
            bool cached_nvapi_ok = vrr_status::cached_nvapi_ok.load();
            std::shared_ptr<nvapi::VrrStatus> cached_nvapi_vrr = vrr_status::cached_nvapi_vrr.load();
            if (cached_nvapi_ok && cached_nvapi_vrr) {
                if (cached_nvapi_vrr->is_display_in_vrr_mode && cached_nvapi_vrr->is_vrr_enabled) {
                    OverlayTableRow_TextColored(imgui, label_mode, "NV VRR", "VRR (NVAPI)", ui::colors::TEXT_SUCCESS,
                                                show_tooltips, nullptr, "%s", "On");
                } else if (cached_nvapi_vrr->is_display_in_vrr_mode) {
                    OverlayTableRow_TextColored(imgui, label_mode, "NV VRR", "VRR (NVAPI)", ui::colors::TEXT_WARNING,
                                                show_tooltips, nullptr, "%s", "Capable");
                } else if (cached_nvapi_vrr->is_vrr_requested) {
                    OverlayTableRow_TextColored(imgui, label_mode, "NV VRR", "VRR (NVAPI)", ui::colors::TEXT_WARNING,
                                                show_tooltips, nullptr, "%s", "Requested");
                } else {
                    OverlayTableRow_TextColored(imgui, label_mode, "NV VRR", "VRR (NVAPI)", ui::colors::TEXT_DIMMED,
                                                show_tooltips, nullptr, "%s", "Off");
                }
            } else {
                if (cached_vrr_stats.all_last_20_within_1s && cached_vrr_stats.samples_below_threshold_last_10s >= 2) {
                    OverlayTableRow_TextColored(imgui, label_mode, "NV VRR", "VRR (NVAPI)", ui::colors::TEXT_SUCCESS,
                                                show_tooltips, nullptr, "%s", "On");
                } else {
                    OverlayTableRow_TextColored(imgui, label_mode, "NV VRR", "VRR (NVAPI)", ui::colors::TEXT_DIMMED,
                                                show_tooltips, nullptr, "%s", "NO NVAPI");
                }
            }
        }
        if (show_dxgi_vrr_status) {
            if (dxgi_stats.is_valid && dxgi_stats.all_last_20_within_1s
                && dxgi_stats.samples_below_threshold_last_10s >= 2) {
                OverlayTableRow_TextColored(imgui, label_mode, "VRR", "VRR", ui::colors::TEXT_SUCCESS,
                                            show_tooltips,
                                            "Heuristic from present timing (RefreshRateMonitor / DXGI). Enable DXGI "
                                            "refresh rate / VRR detection in the Debug DXGI refresh tab (-DebugTabs "
                                            "build) or via addon config.",
                                            "%s", "On");
            } else if (dxgi_stats.is_valid) {
                OverlayTableRow_TextColored(
                    imgui, label_mode, "VRR", "VRR", ui::colors::TEXT_DIMMED, show_tooltips,
                    "Heuristic from present timing (RefreshRateMonitor / DXGI). Enable DXGI refresh rate / VRR "
                    "detection in the Debug DXGI refresh tab (-DebugTabs build) or via addon config.",
                    "%s", "Off");
            } else {
                OverlayTableRow_TextColored(
                    imgui, label_mode, "VRR", "VRR", ui::colors::TEXT_DIMMED, show_tooltips,
                    "Heuristic from present timing (RefreshRateMonitor / DXGI). Enable DXGI refresh rate / VRR "
                    "detection in the Debug DXGI refresh tab (-DebugTabs build) or via addon config.",
                    "%s", "--");
            }
        }
        imgui.EndTable();
    }

    // ----- NVAPI VRR debug (full width, below table) -----
    if (vrr_overlay_cache) {
        perf_measurement::ScopedTimer overlay_show_vrr_status_timer(perf_measurement::Metric::OverlayShowVrrStatus);
        bool cached_nvapi_ok = vrr_status::cached_nvapi_ok.load();
        std::shared_ptr<nvapi::VrrStatus> cached_nvapi_vrr = vrr_status::cached_nvapi_vrr.load();

        if (show_vrr_debug_mode && has_recent_vrr_sample && cached_vrr_stats.is_valid) {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "  Fixed: %.2f Hz", cached_vrr_stats.fixed_refresh_hz);
            imgui.TextColored(ui::colors::TEXT_DIMMED, "  Threshold: %.2f Hz", cached_vrr_stats.threshold_hz);
            imgui.TextColored(ui::colors::TEXT_DIMMED, "  Total samples (10s): %u",
                              cached_vrr_stats.total_samples_last_10s);
            imgui.TextColored(ui::colors::TEXT_DIMMED, "  Below threshold: %u",
                              cached_vrr_stats.samples_below_threshold_last_10s);
            imgui.TextColored(ui::colors::TEXT_DIMMED, "  Last 20 within 1s: %s",
                              cached_vrr_stats.all_last_20_within_1s ? "Yes" : "No");
        }

        if (show_vrr_debug_mode && cached_nvapi_vrr) {
            if (!cached_nvapi_vrr->nvapi_initialized) {
                imgui.TextColored(ui::colors::TEXT_DIMMED, "  NVAPI: Unavailable");
            } else if (!cached_nvapi_vrr->display_id_resolved) {
                imgui.TextColored(ui::colors::TEXT_DIMMED, "  NVAPI: No displayId (st=%d)",
                                  (int)cached_nvapi_vrr->resolve_status);
                if (!cached_nvapi_vrr->nvapi_display_name.empty()) {
                    imgui.TextColored(ui::colors::TEXT_DIMMED, "  NVAPI Name: %s",
                                      cached_nvapi_vrr->nvapi_display_name.c_str());
                }
            } else if (!cached_nvapi_ok) {
                imgui.TextColored(ui::colors::TEXT_DIMMED, "  NVAPI: Query failed (st=%d)",
                                  (int)cached_nvapi_vrr->query_status);
                imgui.TextColored(ui::colors::TEXT_DIMMED, "  NVAPI DisplayId: %u", cached_nvapi_vrr->display_id);
            } else {
                imgui.TextColored(ui::colors::TEXT_DIMMED, "  NVAPI: enabled=%d req=%d poss=%d in_mode=%d",
                                  (int)cached_nvapi_vrr->is_vrr_enabled, (int)cached_nvapi_vrr->is_vrr_requested,
                                  (int)cached_nvapi_vrr->is_vrr_possible,
                                  (int)cached_nvapi_vrr->is_display_in_vrr_mode);
                if (cached_nvapi_vrr->is_display_in_vrr_mode) {
                    imgui.TextColored(ui::colors::TEXT_DIMMED, "  -> Display is in VRR mode (authoritative)");
                } else if (cached_nvapi_vrr->is_vrr_enabled) {
                    imgui.TextColored(ui::colors::TEXT_DIMMED, "  -> VRR enabled (fallback)");
                }
            }
        }
    }

    // ----- Table: GPU + memory -----
    DrawOverlayGpuMemoryTable(imgui, label_mode, show_tooltips, show_overlay_nvapi_gpu_util, show_overlay_vram,
                             show_overlay_ram);

    // ----- DLSS / FG (table) -----
    if (show_fg_mode || show_overlay_resolution || show_dlss_status || show_dlss_quality_preset
        || show_driver_dlss_sr_preset || show_driver_dlss_rr_preset) {
        const DLSSGSummaryLite dlss_lite = GetDLSSGSummaryLite();
        const bool any_dlss_active = dlss_lite.any_dlss_active;
        const int fg_mode = show_fg_mode ? dlss_lite.fg_mode : 0;

        std::string internal_resolution = "N/A";
        if (show_overlay_resolution) {
            unsigned int internal_width, internal_height;
            bool has_internal_width = g_ngx_parameters.get_as_uint("DLSS.Render.Subrect.Dimensions.Width", internal_width);
            bool has_internal_height =
                g_ngx_parameters.get_as_uint("DLSS.Render.Subrect.Dimensions.Height", internal_height);

            if (has_internal_width && has_internal_height && internal_width > 0 && internal_height > 0) {
                internal_resolution = std::to_string(internal_width) + "x" + std::to_string(internal_height);
            }
        }

        std::string quality_preset = "N/A";
        if (show_dlss_quality_preset) {
            unsigned int perf_quality;
            if (g_ngx_parameters.get_as_uint("PerfQualityValue", perf_quality)) {
                switch (static_cast<NVSDK_NGX_PerfQuality_Value>(perf_quality)) {
                    case NVSDK_NGX_PerfQuality_Value_MaxPerf:          quality_preset = "Performance"; break;
                    case NVSDK_NGX_PerfQuality_Value_Balanced:         quality_preset = "Balanced"; break;
                    case NVSDK_NGX_PerfQuality_Value_MaxQuality:       quality_preset = "Quality"; break;
                    case NVSDK_NGX_PerfQuality_Value_UltraPerformance: quality_preset = "Ultra Performance"; break;
                    case NVSDK_NGX_PerfQuality_Value_UltraQuality:     quality_preset = "Ultra Quality"; break;
                    case NVSDK_NGX_PerfQuality_Value_DLAA:             quality_preset = "DLAA"; break;
                    default:                                           quality_preset = "Unknown"; break;
                }
            }
        }

        OverlayScalarTableBegin(imgui);
        if (show_fg_mode) {
            if (any_dlss_active && fg_mode >= 2) {
                OverlayTableRow_Text(imgui, label_mode, "FG", "Frame gen", show_tooltips, nullptr, "%dx", fg_mode);
            } else {
                OverlayTableRow_TextColored(imgui, label_mode, "FG", "Frame gen", ui::colors::TEXT_DIMMED, show_tooltips,
                                            nullptr, "%s", "OFF");
            }
        }
        if (show_overlay_resolution) {
            const int bb_w = g_game_render_width.load();
            const int bb_h = g_game_render_height.load();
            const bool have_bb = (bb_w > 0 && bb_h > 0);
            if (any_dlss_active && internal_resolution != "N/A") {
                std::string res_text = internal_resolution;
                if (have_bb) {
                    res_text += " -> " + std::to_string(bb_w) + "x" + std::to_string(bb_h);
                }
                OverlayTableRow_TextUnformatted(imgui, label_mode, "Res", "Resolution", res_text.c_str());
            } else if (have_bb) {
                const std::string res_text = std::to_string(bb_w) + "x" + std::to_string(bb_h);
                OverlayTableRow_TextUnformatted(imgui, label_mode, "Res", "Resolution", res_text.c_str());
            } else {
                OverlayTableRow_TextColored(
                    imgui, label_mode, "Res", "Resolution", ui::colors::TEXT_DIMMED, show_tooltips,
                    "Resolution unavailable (no DLSS internal size yet and swapchain/backbuffer size not tracked).",
                    "%s", "N/A");
            }
        }
        if (show_dlss_status) {
            if (any_dlss_active) {
                OverlayTableRow_TextColored(imgui, label_mode, "DLSS", "DLSS", ui::colors::TEXT_SUCCESS, show_tooltips,
                                            nullptr, "%s", "On");
            } else {
                OverlayTableRow_TextColored(imgui, label_mode, "DLSS", "DLSS", ui::colors::TEXT_DIMMED, show_tooltips,
                                            nullptr, "%s", "Off");
            }
        }
        if (show_dlss_quality_preset) {
            if (any_dlss_active && quality_preset != "N/A") {
                OverlayTableRow_TextUnformatted(imgui, label_mode, "DLSS Q", "DLSS quality", quality_preset.c_str());
            } else {
                OverlayTableRow_TextColored(imgui, label_mode, "DLSS Q", "DLSS quality", ui::colors::TEXT_DIMMED,
                                            show_tooltips, nullptr, "%s", "N/A");
            }
        }
#if !defined(DC_LITE)
        if (show_driver_dlss_sr_preset || show_driver_dlss_rr_preset) {
            const DLSSGSummary ngx = GetDLSSGSummary();
            const std::string sr_ngx_letter =
                GetNgxDlssDefaultSrRenderPresetLetterFromVersionString(ngx.dlss_dll_version);
            const std::string rr_ngx_letter =
                GetNgxDlssDefaultRrRenderPresetLetterFromVersionString(ngx.dlssd_dll_version);
            const std::shared_ptr<const display_commander::features::nvidia_profile_inspector::DriverDlssRenderPresetSnapshot>
                drv = display_commander::features::nvidia_profile_inspector::GetDriverDlssRenderPresetSnapshot(false);
            const display_commander::features::nvidia_profile_inspector::DriverDlssRenderPresetSnapshot* drv_ptr =
                drv.get();
            const bool dc_preset_on =
                settings::g_swapchainTabSettings.dlss_preset_override_enabled.GetValue();
            const std::string& dc_sr = settings::g_swapchainTabSettings.dlss_sr_preset_override.GetValue();
            const std::string& dc_rr = settings::g_swapchainTabSettings.dlss_rr_preset_override.GetValue();
            if (show_driver_dlss_sr_preset) {
                const auto merged = display_commander::features::nvidia_profile_inspector::MergeDriverAndDcRenderPreset(
                    false, drv_ptr, dc_preset_on, dc_sr);
                const ImVec4 col = merged.warn_color ? ui::colors::TEXT_WARNING : ui::colors::TEXT_DIMMED;
                std::string sr_line = merged.primary;
                if (!sr_ngx_letter.empty()) {
                    sr_line += " (";
                    sr_line += sr_ngx_letter;
                    sr_line += ")";
                }
                std::string sr_tip = merged.tooltip;
                if (!sr_ngx_letter.empty()) {
                    sr_tip += "\nNGX default preset for loaded nvngx_dlss.dll: ";
                    sr_tip += sr_ngx_letter;
                } else {
                    sr_tip += "\nNGX stack-default preset letter for nvngx_dlss.dll is not mapped for this DLL version.";
                }
                OverlayTableRow_TextColored(imgui, label_mode, "SR pr", "SR preset", col, show_tooltips,
                                            sr_tip.c_str(), "%s", sr_line.c_str());
            }
            if (show_driver_dlss_rr_preset) {
                const auto merged = display_commander::features::nvidia_profile_inspector::MergeDriverAndDcRenderPreset(
                    true, drv_ptr, dc_preset_on, dc_rr);
                const ImVec4 col = merged.warn_color ? ui::colors::TEXT_WARNING : ui::colors::TEXT_DIMMED;
                std::string rr_line = merged.primary;
                if (!rr_ngx_letter.empty()) {
                    rr_line += " (";
                    rr_line += rr_ngx_letter;
                    rr_line += ")";
                }
                std::string rr_tip = merged.tooltip;
                if (!rr_ngx_letter.empty()) {
                    rr_tip += "\nNGX default preset for loaded nvngx_dlssd.dll: ";
                    rr_tip += rr_ngx_letter;
                } else {
                    rr_tip += "\nNGX stack-default preset letter for nvngx_dlssd.dll is not mapped for this DLL version.";
                }
                OverlayTableRow_TextColored(imgui, label_mode, "RR pr", "RR preset", col, show_tooltips,
                                            rr_tip.c_str(), "%s", rr_line.c_str());
            }
        }
#endif
        imgui.EndTable();
    }

    // ----- Latency / CPU / volume table (release-style), then module overlay, then NVAPI debug table (matches tab bar) -----
    const bool want_nvapi_reflex_rolling_overlay = show_overlay_nvapi_sim_duration
        || show_overlay_nvapi_sim_end_to_rs_start || show_overlay_nvapi_rs_submit_duration
        || show_overlay_nvapi_rs_start_to_present_start || show_overlay_nvapi_rs_end_to_present_start
        || show_overlay_nvapi_present_phase_duration || show_overlay_nvapi_present_end_to_rs_end
        || show_overlay_nvapi_gpu_active_ms || show_overlay_nvapi_latency_jitter_abs;
    const bool need_nvapi_latency_params =
        g_reflexProvider != nullptr && (show_gpu_measurement || want_nvapi_reflex_rolling_overlay);
    NV_LATENCY_RESULT_PARAMS_V1 nv_latency_params{};
    bool have_nv_latency_params = false;
    if (need_nvapi_latency_params) {
        have_nv_latency_params = g_reflexProvider->GetLatencyParamsV1(nv_latency_params);
    }

    const bool table4_public_any = show_gpu_measurement || show_cpu_usage || show_volume;
    if (table4_public_any) {
        OverlayScalarTableBegin(imgui);

        if (show_gpu_measurement) {
            bool shown = false;
            if (have_nv_latency_params) {
                ReflexProvider::NvapiLatencyMetrics metrics{};
                if (ReflexProvider::MetricsFromLatencyParams(nv_latency_params, metrics)) {
                    shown = true;
                    double pcl_latency_ms_estimate = metrics.pc_latency_ms + metrics.gpu_frame_time_ms / 2.0;
                    const DLSSGSummaryLite dlss_lite = GetDLSSGSummaryLite();
                    const int fg_mode = dlss_lite.fg_mode;
                    if (fg_mode >= 2) {
                        pcl_latency_ms_estimate += metrics.gpu_frame_time_ms * (fg_mode - 1) / (fg_mode * 2.0);
                    }
                    OverlayTableRow_Text(
                        imgui, label_mode, "Lat.", "Latency", show_tooltips,
                        "PC latency from NVAPI Reflex (input sample to GPU render end) and GPU frame time.", "%.1f ms",
                        pcl_latency_ms_estimate);
                }
            }
            if (!shown) {
                LONGLONG latency_ns = ::g_sim_to_display_latency_ns.load();
                if (latency_ns > 0) {
                    double latency_ms = (1.0 * latency_ns / utils::NS_TO_MS);
                    OverlayTableRow_Text(imgui, label_mode, "Lat.", "Latency", show_tooltips, nullptr, "%.1f ms",
                                         latency_ms);
                }
            }
        }

        if (show_cpu_usage) {
            LONGLONG cpu_time_ns =
                ::g_frame_time_ns.load() - fps_sleep_after_on_present_ns.load() - fps_sleep_before_on_present_ns.load();
            LONGLONG frame_time_ns = ::g_frame_time_ns.load();
            if (cpu_time_ns > 0 && frame_time_ns > 0) {
                double cpu_usage_percent = (static_cast<double>(cpu_time_ns) / static_cast<double>(frame_time_ns)) * 100.0;
                if (cpu_usage_percent < 0.0) cpu_usage_percent = 0.0;
                if (cpu_usage_percent > 100.0) cpu_usage_percent = 100.0;
                static double smoothed_cpu_usage = cpu_usage_percent;
                static double displayed_cpu_usage = cpu_usage_percent;
                static LONGLONG s_cpu_busy_last_display_ns = 0;
                const double alpha = 0.05;
                smoothed_cpu_usage = (1.0 - alpha) * smoothed_cpu_usage + alpha * cpu_usage_percent;
                LONGLONG now_ns = utils::get_now_ns();
                const LONGLONG k_cpu_busy_display_interval_ns =
                    static_cast<LONGLONG>(0.2 * static_cast<double>(utils::SEC_TO_NS));
                if (now_ns - s_cpu_busy_last_display_ns >= k_cpu_busy_display_interval_ns) {
                    s_cpu_busy_last_display_ns = now_ns;
                    displayed_cpu_usage = smoothed_cpu_usage;
                }
                static constexpr size_t kCpuUsageHistorySize = 64;
                static double cpu_usage_history[kCpuUsageHistorySize] = {};
                static size_t cpu_usage_history_index = 0;
                static size_t cpu_usage_history_count = 0;
                cpu_usage_history[cpu_usage_history_index] = cpu_usage_percent;
                cpu_usage_history_index = (cpu_usage_history_index + 1) % kCpuUsageHistorySize;
                if (cpu_usage_history_count < kCpuUsageHistorySize) {
                    cpu_usage_history_count++;
                }
                double max_cpu_usage = cpu_usage_percent;
                for (size_t i = 0; i < cpu_usage_history_count; ++i) {
                    max_cpu_usage = (std::max)(max_cpu_usage, cpu_usage_history[i]);
                }
                OverlayTableRow_Text(imgui, label_mode, "CPU%", "CPU busy", show_tooltips, nullptr,
                                     "%.1f%% (max %.1f%%)", displayed_cpu_usage, max_cpu_usage);
            }
        }
        if (show_volume) {
            perf_measurement::ScopedTimer overlay_show_volume_timer(perf_measurement::Metric::OverlayShowVolume);
            float current_volume = s_game_volume_percent.load();
            float system_volume = s_system_volume_percent.load();
            bool is_muted = g_muted_applied.load();
            if (is_muted) {
                OverlayTableRow_Text(imgui, label_mode, "Vol", "Volume", show_tooltips,
                                     "Game volume | system volume (muted).", "%.0f%% / %.0f%% (muted)", current_volume,
                                     system_volume);
            } else {
                OverlayTableRow_Text(imgui, label_mode, "Vol", "Volume", show_tooltips,
                                     "Game volume | system volume.", "%.0f%% / %.0f%%", current_volume, system_volume);
            }
        }

        imgui.EndTable();
    }

    // ----- Ephemeral notifications (full width) -----
    ActionNotification notification = g_action_notification.load();
    if (notification.type != ActionNotificationType::None) {
        LONGLONG now_ns = utils::get_now_ns();
        LONGLONG elapsed_ns = now_ns - notification.timestamp_ns;
        const LONGLONG display_duration_ns = 10 * utils::SEC_TO_NS;

        if (elapsed_ns < display_duration_ns) {
            switch (notification.type) {
                case ActionNotificationType::Volume: {
                    float volume_value = notification.float_value;
                    bool is_muted = g_muted_applied.load();
                    if (label_mode == OverlayLabelMode::kFull) {
                        if (is_muted) {
                            imgui.Text("%.0f%% vol (muted)", volume_value);
                        } else {
                            imgui.Text("%.0f%% vol", volume_value);
                        }
                    } else if (label_mode == OverlayLabelMode::kShort) {
                        imgui.Text(is_muted ? "%.0f%% (muted)" : "%.0f%%", volume_value);
                    } else {
                        imgui.Text("%.0f%%", volume_value);
                    }
                    if (imgui.IsItemHovered() && show_tooltips) {
                        if (is_muted) {
                            imgui.SetTooltipEx("Audio Volume: %.0f%% (Muted)", volume_value);
                        } else {
                            imgui.SetTooltipEx("Audio Volume: %.0f%%", volume_value);
                        }
                    }
                    break;
                }
                case ActionNotificationType::Mute: {
                    bool mute_state = notification.bool_value;
                    imgui.Text("%s", mute_state ? "Muted" : "Unmuted");
                    if (imgui.IsItemHovered() && show_tooltips) {
                        imgui.SetTooltipEx("Audio: %s", mute_state ? "Muted" : "Unmuted");
                    }
                    break;
                }
                case ActionNotificationType::GenericAction: {
                    imgui.Text("%s", notification.action_name);
                    if (imgui.IsItemHovered() && show_tooltips) {
                        imgui.SetTooltipEx("Gamepad Action: %s", notification.action_name);
                    }
                    break;
                }
                default: break;
            }
        } else {
            ActionNotification clear_notification;
            clear_notification.type = ActionNotificationType::None;
            clear_notification.timestamp_ns = 0;
            clear_notification.float_value = 0.0f;
            clear_notification.bool_value = false;
            clear_notification.action_name[0] = '\0';
            g_action_notification.store(clear_notification);
        }
    }

    modules::DrawEnabledModulesInOverlay(imgui);

    if ((want_nvapi_reflex_rolling_overlay && have_nv_latency_params)
        || show_overlay_nvapi_setlatencymarker_threads) {
        OverlayScalarTableBegin(imgui);
        const DLSSGSummaryLite dlss_lite_pm = GetDLSSGSummaryLite();
        const int fg_mode_pm = dlss_lite_pm.fg_mode;
        ReflexProvider::NvapiReflexNewestFrameDerived derived{};
        if (want_nvapi_reflex_rolling_overlay && have_nv_latency_params
            && ReflexProvider::FillNewestFrameDerivedForOverlay(nv_latency_params, fg_mode_pm, derived)) {
            constexpr double k_pm_ema_alpha = 0.02;
            static double s_ema_sim_ms = 0.0;
            static bool s_ema_sim_inited = false;
            static double s_ema_sim_to_rs_ms = 0.0;
            static bool s_ema_sim_to_rs_inited = false;
            static double s_ema_rs_phase_ms = 0.0;
            static bool s_ema_rs_phase_inited = false;
            static double s_ema_rs_start_to_pr_ms = 0.0;
            static bool s_ema_rs_start_to_pr_inited = false;
            static double s_ema_rs_to_pr_ms = 0.0;
            static bool s_ema_rs_to_pr_inited = false;
            static double s_ema_present_phase_ms = 0.0;
            static bool s_ema_present_phase_inited = false;
            static double s_ema_pr_end_to_rs_end_ms = 0.0;
            static bool s_ema_pr_end_to_rs_end_inited = false;
            static double s_ema_gpu_active_ms = 0.0;
            static bool s_ema_gpu_inited = false;
            static double s_ema_jitter_ms = 0.0;
            static bool s_ema_jitter_inited = false;
            static uint64_t s_last_jitter_frame_id = 0;
            static double s_prev_latency_L = 0.0;
            static bool s_have_prev_latency_L = false;

            if (show_overlay_nvapi_sim_duration && derived.sim_duration_valid) {
                s_ema_sim_ms = s_ema_sim_inited ?
                    (1.0 - k_pm_ema_alpha) * s_ema_sim_ms + k_pm_ema_alpha * derived.sim_duration_ms :
                    derived.sim_duration_ms;
                s_ema_sim_inited = true;
                OverlayTableRow_Text(imgui, label_mode, "sim_start -> sim_end", "sim_start -> sim_end", show_tooltips,
                                     "Rolling average, newest frame, NvAPI GetLatency (µs domain).",
                                     "%.2f ms", s_ema_sim_ms);
            }
            if (show_overlay_nvapi_sim_end_to_rs_start && derived.sim_end_to_render_submit_start_valid) {
                s_ema_sim_to_rs_ms = s_ema_sim_to_rs_inited ?
                    (1.0 - k_pm_ema_alpha) * s_ema_sim_to_rs_ms
                        + k_pm_ema_alpha * derived.sim_end_to_render_submit_start_ms :
                    derived.sim_end_to_render_submit_start_ms;
                s_ema_sim_to_rs_inited = true;
                OverlayTableRow_Text(imgui, label_mode, "sim_end -> render_submit_start",
                                     "sim_end -> render_submit_start", show_tooltips,
                                     "Rolling average, newest frame, NvAPI GetLatency (µs domain).",
                                     "%.2f ms", s_ema_sim_to_rs_ms);
            }
            if (show_overlay_nvapi_rs_submit_duration && derived.render_submit_phase_valid) {
                s_ema_rs_phase_ms = s_ema_rs_phase_inited ?
                    (1.0 - k_pm_ema_alpha) * s_ema_rs_phase_ms + k_pm_ema_alpha * derived.render_submit_phase_ms :
                    derived.render_submit_phase_ms;
                s_ema_rs_phase_inited = true;
                OverlayTableRow_Text(imgui, label_mode, "render_submit_start -> render_submit_end",
                                     "render_submit_start -> render_submit_end", show_tooltips,
                                     "Rolling average, newest frame, NvAPI GetLatency (µs domain).",
                                     "%.2f ms", s_ema_rs_phase_ms);
            }
            if (show_overlay_nvapi_rs_start_to_present_start && derived.rs_start_to_present_start_valid) {
                s_ema_rs_start_to_pr_ms = s_ema_rs_start_to_pr_inited ?
                    (1.0 - k_pm_ema_alpha) * s_ema_rs_start_to_pr_ms
                        + k_pm_ema_alpha * derived.rs_start_to_present_start_ms :
                    derived.rs_start_to_present_start_ms;
                s_ema_rs_start_to_pr_inited = true;
                OverlayTableRow_Text(imgui, label_mode, "render_submit_start -> present_start",
                                     "render_submit_start -> present_start", show_tooltips,
                                     "present_start − render_submit_start (µs→ms), signed when both set. Rolling avg, "
                                     "NvAPI GetLatency.",
                                     "%.2f ms", s_ema_rs_start_to_pr_ms);
            }
            if (show_overlay_nvapi_rs_end_to_present_start && derived.rs_end_to_present_start_valid) {
                s_ema_rs_to_pr_ms = s_ema_rs_to_pr_inited ?
                    (1.0 - k_pm_ema_alpha) * s_ema_rs_to_pr_ms
                        + k_pm_ema_alpha * derived.rs_end_to_present_start_ms :
                    derived.rs_end_to_present_start_ms;
                s_ema_rs_to_pr_inited = true;
                OverlayTableRow_Text(imgui, label_mode, "render_submit_end -> present_start",
                                     "render_submit_end -> present_start", show_tooltips,
                                     "present_start − render_submit_end (µs→ms), signed when both set. Rolling avg, "
                                     "NvAPI GetLatency.",
                                     "%.2f ms", s_ema_rs_to_pr_ms);
            }
            if (show_overlay_nvapi_present_phase_duration && derived.present_phase_valid) {
                s_ema_present_phase_ms = s_ema_present_phase_inited ?
                    (1.0 - k_pm_ema_alpha) * s_ema_present_phase_ms
                        + k_pm_ema_alpha * derived.present_phase_ms :
                    derived.present_phase_ms;
                s_ema_present_phase_inited = true;
                OverlayTableRow_Text(imgui, label_mode, "present_start -> present_end", "present_start -> present_end",
                                     show_tooltips,
                                     "Rolling average, newest frame, NvAPI GetLatency (µs domain).",
                                     "%.2f ms", s_ema_present_phase_ms);
            }
            if (show_overlay_nvapi_present_end_to_rs_end && derived.present_end_to_rs_end_valid) {
                s_ema_pr_end_to_rs_end_ms = s_ema_pr_end_to_rs_end_inited ?
                    (1.0 - k_pm_ema_alpha) * s_ema_pr_end_to_rs_end_ms
                        + k_pm_ema_alpha * derived.present_end_to_rs_end_ms :
                    derived.present_end_to_rs_end_ms;
                s_ema_pr_end_to_rs_end_inited = true;
                OverlayTableRow_Text(imgui, label_mode, "present_end -> render_submit_end",
                                     "present_end -> render_submit_end", show_tooltips,
                                     "render_submit_end − present_end (µs→ms), signed when both set. Rolling avg, "
                                     "NvAPI GetLatency.",
                                     "%.2f ms", s_ema_pr_end_to_rs_end_ms);
            }
            if (show_overlay_nvapi_gpu_active_ms && derived.gpu_active_valid) {
                s_ema_gpu_active_ms = s_ema_gpu_inited ?
                    (1.0 - k_pm_ema_alpha) * s_ema_gpu_active_ms + k_pm_ema_alpha * derived.gpu_active_render_ms :
                    derived.gpu_active_render_ms;
                s_ema_gpu_inited = true;
                OverlayTableRow_Text(imgui, label_mode, "GPU act", "Reflex GPU active", show_tooltips,
                                     "NVAPI gpuActiveRenderTimeUs (newest frame): driver-reported GPU busy time "
                                     "excluding idles. Rolling average; not PresentMon MsGPUBusy (ETW).",
                                     "%.2f ms", s_ema_gpu_active_ms);
            }
            if (show_overlay_nvapi_latency_jitter_abs) {
                if (derived.osd_latency_valid && derived.frame_id != s_last_jitter_frame_id) {
                    if (s_have_prev_latency_L) {
                        const double j = std::fabs(derived.osd_latency_estimate_ms - s_prev_latency_L);
                        s_ema_jitter_ms = s_ema_jitter_inited ?
                            (1.0 - k_pm_ema_alpha) * s_ema_jitter_ms + k_pm_ema_alpha * j :
                            j;
                        s_ema_jitter_inited = true;
                    }
                    s_prev_latency_L = derived.osd_latency_estimate_ms;
                    s_have_prev_latency_L = true;
                    s_last_jitter_frame_id = derived.frame_id;
                }
                if (s_ema_jitter_inited) {
                    OverlayTableRow_Text(
                        imgui, label_mode, "AnimErr", "Animation error", show_tooltips,
                        "When frame_id advances: absolute change vs previous overlay latency estimate (same formula "
                        "as Lat. row). Rolling average; not PresentMon MsAnimationError.",
                        "%.2f ms", s_ema_jitter_ms);
                }
            }
        }
        if (show_overlay_nvapi_setlatencymarker_threads) {
            static constexpr const char* k_marker_tid_short[kNvapiSetLatencyMarkerThreadTrackCount] = {
                "m sim_s", "m sim_e", "m rs_s", "m rs_e", "m pr_s", "m pr_e", "m in_sm"};
            static constexpr const char* k_marker_tid_full[kNvapiSetLatencyMarkerThreadTrackCount] = {
                "SetLatencyMarker: sim_start (TID)", "SetLatencyMarker: sim_end (TID)",
                "SetLatencyMarker: render_submit_start (TID)", "SetLatencyMarker: render_submit_end (TID)",
                "SetLatencyMarker: present_start (TID)", "SetLatencyMarker: present_end (TID)",
                "SetLatencyMarker: input_sample (TID)"};
            for (size_t i = 0; i < kNvapiSetLatencyMarkerThreadTrackCount; ++i) {
                const uint32_t tid =
                    g_nvapi_d3d_setlatencymarker_last_thread_id[i].load(std::memory_order_relaxed);
                char value_buf[48];
                if (tid != 0) {
                    std::snprintf(value_buf, sizeof(value_buf), "%u (0x%X)", tid, tid);
                } else {
                    value_buf[0] = '-';
                    value_buf[1] = '-';
                    value_buf[2] = '\0';
                }
                OverlayTableRow_Text(imgui, label_mode, k_marker_tid_short[i], k_marker_tid_full[i], show_tooltips,
                                     "Last thread ID seen in NvAPI_D3D_SetLatencyMarker detour for this marker type "
                                     "(SIMULATION_START..INPUT_SAMPLE).",
                                     "%s", value_buf);
            }
        }
        imgui.EndTable();
    }

    // ----- Frame timing visuals (bottom of overlay) -----
    if (show_frame_time_graph) {
        ui::new_ui::DrawFrameTimeGraphOverlay(imgui, show_tooltips);
    }
    if (show_native_frame_time_graph) {
        ui::new_ui::DrawNativeFrameTimeGraphOverlay(imgui, show_tooltips);
    }
    if (settings::g_mainTabSettings.show_frame_timeline_bar.GetValue()) {
        ui::new_ui::DrawFrameTimelineBarOverlay(imgui, show_tooltips);
    }
}

}  // namespace ui::new_ui
