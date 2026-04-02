// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
// Headers <Display Commander>
#include "performance_overlay_internal.hpp"
#include "dxgi/vram_info.hpp"
#include "hooks/nvidia/ngx_hooks.hpp"
#include "latency/reflex_provider.hpp"
#include "latent_sync/refresh_rate_monitor_integration.hpp"
#include "modules/module_registry.hpp"
#include "nvapi/nvapi_actual_refresh_rate_monitor.hpp"
#include "nvapi/gpu_dynamic_utilization.hpp"
#include "nvapi/vrr_status.hpp"
#include "swapchain_events.hpp"
#include "utils.hpp"

// Libraries <Windows.h>
#include <Windows.h>

// Libraries <Windows>
#include <psapi.h>

namespace ui::new_ui {

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
    bool show_fps_counter = settings::g_mainTabSettings.show_fps_counter.GetValue();
    bool show_vrr_status = settings::g_mainTabSettings.show_vrr_status.GetValue();
    bool show_actual_refresh_rate = settings::g_mainTabSettings.show_actual_refresh_rate.GetValue();
    bool show_flip_status = settings::g_mainTabSettings.show_flip_status.GetValue();
    bool show_volume = settings::g_experimentalTabSettings.show_volume.GetValue();
    bool show_overlay_vu_bars = settings::g_mainTabSettings.show_overlay_vu_bars.GetValue();
    bool show_gpu_measurement = (settings::g_mainTabSettings.gpu_measurement_enabled.GetValue() != 0);
    bool show_frame_time_graph = settings::g_mainTabSettings.show_frame_time_graph.GetValue();
    bool show_native_frame_time_graph = settings::g_mainTabSettings.show_native_frame_time_graph.GetValue();
    bool show_cpu_usage = settings::g_mainTabSettings.show_cpu_usage.GetValue();
    bool show_cpu_fps = settings::g_mainTabSettings.show_cpu_fps.GetValue();
    bool show_overlay_nvapi_gpu_util = settings::g_mainTabSettings.show_overlay_nvapi_gpu_util.GetValue();
    bool show_nvapi_latency_stats = settings::g_mainTabSettings.show_nvapi_latency_stats.GetValue();
    bool show_fg_mode = settings::g_mainTabSettings.show_fg_mode.GetValue();
    bool show_dlss_internal_resolution = settings::g_mainTabSettings.show_dlss_internal_resolution.GetValue();
    bool show_dlss_status = settings::g_mainTabSettings.show_dlss_status.GetValue();
    bool show_dlss_quality_preset = settings::g_mainTabSettings.show_dlss_quality_preset.GetValue();
    bool show_dlss_render_preset = settings::g_mainTabSettings.show_dlss_render_preset.GetValue();
    bool show_fps_limiter_src = settings::g_mainTabSettings.show_fps_limiter_src.GetValue();
    bool show_overlay_vram = settings::g_mainTabSettings.show_overlay_vram.GetValue();
    bool show_dxgi_vrr_status = settings::g_mainTabSettings.show_dxgi_vrr_status.GetValue();
    bool show_dxgi_refresh_rate = settings::g_mainTabSettings.show_dxgi_refresh_rate.GetValue();

    if (settings::g_mainTabSettings.show_clock.GetValue()) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        imgui.Text("%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    }

    if (settings::g_mainTabSettings.show_playtime.GetValue()) {
        LONGLONG game_start_time_ns = g_game_start_time_ns.load();
        if (game_start_time_ns > 0) {
            LONGLONG now_ns = utils::get_now_ns();
            LONGLONG playtime_ns = now_ns - game_start_time_ns;
            double playtime_seconds = static_cast<double>(playtime_ns) / static_cast<double>(utils::SEC_TO_NS);

            int hours = static_cast<int>(playtime_seconds / 3600.0);
            int minutes = static_cast<int>((playtime_seconds - (hours * 3600.0)) / 60.0);
            int seconds = static_cast<int>(playtime_seconds - (hours * 3600.0) - (minutes * 60.0));
            int milliseconds = static_cast<int>((playtime_seconds - static_cast<int>(playtime_seconds)) * 1000.0);
            (void)milliseconds;

            if (settings::g_mainTabSettings.show_labels.GetValue()) {
                imgui.Text("%02d:%02d:%02d", hours, minutes, seconds);
            } else {
                imgui.Text("%02d:%02d:%02d", hours, minutes, seconds);
            }

            if (imgui.IsItemHovered() && show_tooltips) {
                imgui.SetTooltipEx("Playtime: Time elapsed since game start");
            }
        }
    }

    if (show_fps_counter) {
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
            auto average_fps = sample_count / total_time;

            bool show_native_fps = settings::g_mainTabSettings.show_native_fps.GetValue();
            if (show_native_fps) {
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
                    if (settings::g_mainTabSettings.show_labels.GetValue()) {
                        imgui.Text("%.1f / %.1f fps", native_fps, average_fps);
                    } else {
                        imgui.Text("%.1f / %.1f", native_fps, average_fps);
                    }
                } else {
                    if (settings::g_mainTabSettings.show_labels.GetValue()) {
                        imgui.Text("%.1f fps", average_fps);
                    } else {
                        imgui.Text("%.1f", average_fps);
                    }
                }
            } else {
                if (settings::g_mainTabSettings.show_labels.GetValue()) {
                    imgui.Text("%.1f fps", average_fps);
                } else {
                    imgui.Text("%.1f", average_fps);
                }
            }
        }
    }

    if (show_actual_refresh_rate) {
        static double s_smoothed_actual_hz = 0.0;
        constexpr double k_alpha = 0.02;
        double actual_hz = display_commander::nvapi::GetNvapiActualRefreshRateHz();
        if (actual_hz > 0.0) {
            s_smoothed_actual_hz = k_alpha * actual_hz + (1.0 - k_alpha) * s_smoothed_actual_hz;
            imgui.Text("%.1f Hz", s_smoothed_actual_hz);
            if (imgui.IsItemHovered() && show_tooltips) {
                imgui.SetTooltipEx("Actual refresh rate from NvAPI_DISP_GetAdaptiveSyncData (flip count/timestamp).");
            }
        } else {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "Actual: -- Hz");
            if (imgui.IsItemHovered() && show_tooltips) {
                imgui.SetTooltipEx("Waiting for NVAPI display or samples.");
            }
        }
    }

    {
        bool show_vrr_debug_mode = settings::g_mainTabSettings.vrr_debug_mode.GetValue();

        if (show_vrr_status || show_vrr_debug_mode) {
            perf_measurement::ScopedTimer overlay_show_vrr_status_timer(perf_measurement::Metric::OverlayShowVrrStatus);
            static dxgi::fps_limiter::RefreshRateStats cached_stats{};
            static LONGLONG last_valid_sample_ns = 0;

            bool cached_nvapi_ok = vrr_status::cached_nvapi_ok.load();
            std::shared_ptr<nvapi::VrrStatus> cached_nvapi_vrr = vrr_status::cached_nvapi_vrr.load();

            LONGLONG now_ns = utils::get_now_ns();

            auto shared_stats = g_cached_refresh_rate_stats.load();
            if (shared_stats && shared_stats->is_valid && shared_stats->sample_count > 0) {
                cached_stats = *shared_stats;
                last_valid_sample_ns = now_ns;
            }

            const LONGLONG sample_timeout_ns = 1000 * utils::NS_TO_MS;
            bool has_recent_sample = (now_ns - last_valid_sample_ns) < sample_timeout_ns;

            if (show_vrr_status) {
                if (cached_nvapi_ok && cached_nvapi_vrr) {
                    if (cached_nvapi_vrr->is_display_in_vrr_mode && cached_nvapi_vrr->is_vrr_enabled) {
                        imgui.TextColored(ui::colors::TEXT_SUCCESS, "VRR: On");
                    } else if (cached_nvapi_vrr->is_display_in_vrr_mode) {
                        imgui.TextColored(ui::colors::TEXT_WARNING, "VRR: Capable");
                    } else if (cached_nvapi_vrr->is_vrr_requested) {
                        imgui.TextColored(ui::colors::TEXT_WARNING, "VRR: Requested");
                    } else {
                        imgui.TextColored(ui::colors::TEXT_DIMMED, "VRR: Off");
                    }
                } else {
                    if (cached_stats.all_last_20_within_1s && cached_stats.samples_below_threshold_last_10s >= 2) {
                        imgui.TextColored(ui::colors::TEXT_SUCCESS, "VRR: On");
                    } else {
                        imgui.TextColored(ui::colors::TEXT_DIMMED, "VRR: NO NVAPI");
                    }
                }
            }

            if (show_vrr_debug_mode && has_recent_sample && cached_stats.is_valid) {
                imgui.TextColored(ui::colors::TEXT_DIMMED, "  Fixed: %.2f Hz", cached_stats.fixed_refresh_hz);
                imgui.TextColored(ui::colors::TEXT_DIMMED, "  Threshold: %.2f Hz", cached_stats.threshold_hz);
                imgui.TextColored(ui::colors::TEXT_DIMMED, "  Total samples (10s): %u",
                                  cached_stats.total_samples_last_10s);
                imgui.TextColored(ui::colors::TEXT_DIMMED, "  Below threshold: %u",
                                  cached_stats.samples_below_threshold_last_10s);
                imgui.TextColored(ui::colors::TEXT_DIMMED, "  Last 20 within 1s: %s",
                                  cached_stats.all_last_20_within_1s ? "Yes" : "No");
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
    }

    if (show_dxgi_vrr_status || show_dxgi_refresh_rate) {
        dxgi::fps_limiter::RefreshRateStats dxgi_stats = dxgi::fps_limiter::GetRefreshRateStats();
        if (show_dxgi_vrr_status) {
            if (dxgi_stats.is_valid && dxgi_stats.all_last_20_within_1s
                && dxgi_stats.samples_below_threshold_last_10s >= 2) {
                imgui.TextColored(ui::colors::TEXT_SUCCESS, "DXGI VRR: On");
            } else if (dxgi_stats.is_valid) {
                imgui.TextColored(ui::colors::TEXT_DIMMED, "DXGI VRR: Off");
            } else {
                imgui.TextColored(ui::colors::TEXT_DIMMED, "DXGI VRR: --");
            }
            if (imgui.IsItemHovered() && show_tooltips) {
                imgui.SetTooltipEx(
                    "DXGI-based VRR heuristic (RefreshRateMonitor). Enable DXGI refresh rate / VRR detection in "
                    "Advanced tab.");
            }
        }
        if (show_dxgi_refresh_rate) {
            double dxgi_hz = dxgi::fps_limiter::GetSmoothedRefreshRate();
            if (dxgi_hz > 0.0) {
                if (settings::g_mainTabSettings.show_labels.GetValue()) {
                    imgui.Text("DXGI refresh rate: %.1f Hz", dxgi_hz);
                } else {
                    imgui.Text("%.1f Hz", dxgi_hz);
                }
            } else {
                imgui.TextColored(ui::colors::TEXT_DIMMED, "DXGI refresh rate: -- Hz");
            }
            if (imgui.IsItemHovered() && show_tooltips) {
                imgui.SetTooltipEx(
                    "From swap chain GetFrameStatistics (RefreshRateMonitor). Enable DXGI refresh rate / VRR detection "
                    "in Advanced tab.");
            }
        }
    }

    if (show_overlay_vram) {
        uint64_t vram_used = 0;
        uint64_t vram_total = 0;
        if (display_commander::dxgi::GetVramInfo(&vram_used, &vram_total) && vram_total > 0) {
            const uint64_t used_mib = vram_used / (1024ULL * 1024ULL);
            const uint64_t total_mib = vram_total / (1024ULL * 1024ULL);
            if (settings::g_mainTabSettings.show_labels.GetValue()) {
                imgui.Text("VRAM: %llu / %llu MiB", static_cast<unsigned long long>(used_mib),
                           static_cast<unsigned long long>(total_mib));
            } else {
                imgui.Text("%llu / %llu MiB", static_cast<unsigned long long>(used_mib),
                           static_cast<unsigned long long>(total_mib));
            }
            if (imgui.IsItemHovered() && show_tooltips) {
                imgui.SetTooltipEx("GPU video memory used / budget (DXGI adapter memory budget).");
            }
        } else {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "VRAM: N/A");
            if (imgui.IsItemHovered() && show_tooltips) {
                imgui.SetTooltipEx("VRAM unavailable (DXGI adapter or budget query failed).");
            }
        }

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
            if (settings::g_mainTabSettings.show_labels.GetValue()) {
                if (have_process) {
                    imgui.Text("RAM: %llu (%llu) / %llu MiB", static_cast<unsigned long long>(ram_used_mib),
                               static_cast<unsigned long long>(process_mib),
                               static_cast<unsigned long long>(ram_total_mib));
                } else {
                    imgui.Text("RAM: %llu / %llu MiB", static_cast<unsigned long long>(ram_used_mib),
                               static_cast<unsigned long long>(ram_total_mib));
                }
            } else {
                if (have_process) {
                    imgui.Text("%llu (%llu) / %llu MiB", static_cast<unsigned long long>(ram_used_mib),
                               static_cast<unsigned long long>(process_mib),
                               static_cast<unsigned long long>(ram_total_mib));
                } else {
                    imgui.Text("%llu / %llu MiB", static_cast<unsigned long long>(ram_used_mib),
                               static_cast<unsigned long long>(ram_total_mib));
                }
            }
            if (imgui.IsItemHovered() && show_tooltips) {
                imgui.SetTooltipEx(
                    "System RAM in use (this app working set) / total (GlobalMemoryStatusEx, GetProcessMemoryInfo).");
            }
        } else {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "RAM: N/A");
            if (imgui.IsItemHovered() && show_tooltips) {
                imgui.SetTooltipEx("System memory info unavailable.");
            }
        }
    }

    if (show_fg_mode || show_dlss_internal_resolution || show_dlss_status || show_dlss_quality_preset
        || show_dlss_render_preset) {
        const DLSSGSummaryLite dlss_lite = GetDLSSGSummaryLite();
        const bool any_dlss_active = dlss_lite.any_dlss_active;

        int fg_mode = 0;

        int dllssg_mode = -1;
        int enable_interp = -1;
        g_ngx_parameters.get_as_int("DLSSG.Mode", dllssg_mode);
        g_ngx_parameters.get_as_int("DLSSG.EnableInterp", enable_interp);

        bool is_fg_enabled = (dllssg_mode != -1 ? dllssg_mode >= 1 : enable_interp == 1);

        if (show_fg_mode) {
            int num_frames_actually_presented;
            (void)num_frames_actually_presented;
            if (is_fg_enabled) {
                unsigned int multi_frame_count;
                if (g_ngx_parameters.get_as_uint("DLSSG.MultiFrameCount", multi_frame_count)) {
                    fg_mode = static_cast<int>(multi_frame_count) + 1;
                }
            }
        }

        std::string internal_resolution = "N/A";
        std::string output_resolution = "N/A";
        if (show_dlss_internal_resolution) {
            unsigned int internal_width, internal_height, output_width, output_height;
            bool has_internal_width = g_ngx_parameters.get_as_uint("DLSS.Render.Subrect.Dimensions.Width", internal_width);
            bool has_internal_height =
                g_ngx_parameters.get_as_uint("DLSS.Render.Subrect.Dimensions.Height", internal_height);
            bool has_output_width = g_ngx_parameters.get_as_uint("Width", output_width);
            bool has_output_height = g_ngx_parameters.get_as_uint("Height", output_height);

            if (has_internal_width && has_internal_height && internal_width > 0 && internal_height > 0) {
                internal_resolution = std::to_string(internal_width) + "x" + std::to_string(internal_height);
            }
            if (has_output_width && has_output_height) {
                output_resolution = std::to_string(output_width) + "x" + std::to_string(output_height);
            }
        }

        std::string quality_preset = "N/A";
        if (show_dlss_quality_preset || show_dlss_render_preset) {
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

        if (show_fg_mode) {
            if (any_dlss_active && fg_mode >= 2) {
                imgui.Text("FG: %sx", fg_mode);
            } else {
                imgui.TextColored(ui::colors::TEXT_DIMMED, "FG: OFF");
            }
        }

        if (show_dlss_internal_resolution) {
            if (any_dlss_active && internal_resolution != "N/A") {
                std::string res_text = internal_resolution;
                const int bb_w = g_game_render_width.load();
                const int bb_h = g_game_render_height.load();
                if (bb_w > 0 && bb_h > 0) {
                    res_text += " -> " + std::to_string(bb_w) + "x" + std::to_string(bb_h);
                }
                if (settings::g_mainTabSettings.show_labels.GetValue()) {
                    imgui.Text("DLSS Internal->Output: %s", res_text.c_str());
                } else {
                    imgui.Text("%s", res_text.c_str());
                }
            } else {
                imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS Internal->Output: N/A");
            }
        }

        if (show_dlss_status) {
            if (any_dlss_active) {
                std::string status_text;
                if (settings::g_mainTabSettings.show_labels.GetValue()) {
                    status_text = "DLSS: On";
                } else {
                    status_text = "DLSS On";
                }

                if (dlss_lite.ray_reconstruction_active) {
                    status_text += " (RR)";
                } else if (dlss_lite.dlss_g_active) {
                    status_text += " (DLSS-G)";
                }

                imgui.TextColored(ui::colors::TEXT_SUCCESS, "%s", status_text.c_str());
            } else {
                if (settings::g_mainTabSettings.show_labels.GetValue()) {
                    imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS: Off");
                } else {
                    imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS Off");
                }
            }
        }

        if (show_dlss_quality_preset) {
            if (any_dlss_active && quality_preset != "N/A") {
                if (settings::g_mainTabSettings.show_labels.GetValue()) {
                    imgui.Text("DLSS Quality: %s", quality_preset.c_str());
                } else {
                    imgui.Text("%s", quality_preset.c_str());
                }
            } else {
                imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS Quality: N/A");
            }
        }

        if (show_dlss_render_preset) {
            if (any_dlss_active) {
                DLSSModelProfile model_profile = GetDLSSModelProfile();
                if (model_profile.is_valid) {
                    std::string current_quality = quality_preset;
                    int render_preset_value = 0;

                    if (dlss_lite.ray_reconstruction_active) {
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

                    if (settings::g_mainTabSettings.show_labels.GetValue()) {
                        imgui.Text("DLSS Render: %s", render_preset_letter.c_str());
                    } else {
                        imgui.Text("%s", render_preset_letter.c_str());
                    }
                } else {
                    imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS Render: N/A");
                }
            } else {
                imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS Render: N/A");
            }
        }
    }

    if (show_volume) {
        perf_measurement::ScopedTimer overlay_show_volume_timer(perf_measurement::Metric::OverlayShowVolume);
        float current_volume = s_game_volume_percent.load();
        float system_volume = s_system_volume_percent.load();

        bool is_muted = g_muted_applied.load();

        if (settings::g_mainTabSettings.show_labels.GetValue()) {
            if (is_muted) {
                imgui.Text("%.0f%% vol / %.0f%% sys muted", current_volume, system_volume);
            } else {
                imgui.Text("%.0f%% vol / %.0f%% sys", current_volume, system_volume);
            }
        } else {
            if (is_muted) {
                imgui.Text("%.0f%% / %.0f%% muted", current_volume, system_volume);
            } else {
                imgui.Text("%.0f%% / %.0f%%", current_volume, system_volume);
            }
        }
        if (imgui.IsItemHovered() && show_tooltips) {
            if (is_muted) {
                imgui.SetTooltipEx("Game Volume: %.0f%% | System Volume: %.0f%% (Muted)", current_volume,
                                   system_volume);
            } else {
                imgui.SetTooltipEx("Game Volume: %.0f%% | System Volume: %.0f%%", current_volume, system_volume);
            }
        }
    }

    if (show_gpu_measurement) {
        LONGLONG latency_ns = ::g_sim_to_display_latency_ns.load();
        if (latency_ns > 0) {
            double latency_ms = (1.0 * latency_ns / utils::NS_TO_MS);
            if (settings::g_mainTabSettings.show_labels.GetValue()) {
                imgui.Text("%.1f ms lat", latency_ms);
            } else {
                imgui.Text("%.1f", latency_ms);
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

            if (settings::g_mainTabSettings.show_labels.GetValue()) {
                imgui.Text("%.1f%% cpu busy (max: %.1f%%)", displayed_cpu_usage, max_cpu_usage);
            } else {
                imgui.Text("%.1f%% (max: %.1f%%)", displayed_cpu_usage, max_cpu_usage);
            }
        }
    }

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
            if (settings::g_mainTabSettings.show_labels.GetValue()) {
                imgui.Text("%.1f%% GPU (NV)", displayed_nv_gpu_util);
            } else {
                imgui.Text("%.1f%%", displayed_nv_gpu_util);
            }
            if (imgui.IsItemHovered() && show_tooltips) {
                imgui.SetTooltipEx(
                    "NVIDIA GPU engine utilization from NvAPI_GPU_GetDynamicPstatesInfoEx (~1 s rolling average, "
                    "first physical GPU).");
            }
        }
    }

    if (show_nvapi_latency_stats) {
        if (g_reflexProvider) {
            ReflexProvider::NvapiLatencyMetrics metrics{};
            if (g_reflexProvider->GetLatencyMetrics(metrics)) {
                if (settings::g_mainTabSettings.show_labels.GetValue()) {
                    imgui.Text("PCL (NVAPI): %.1f ms", metrics.pc_latency_ms + metrics.gpu_frame_time_ms / 2.0);
                } else {
                    imgui.Text("%.1f ms / %.1f ms", metrics.pc_latency_ms, metrics.gpu_frame_time_ms);
                }
                if (imgui.IsItemHovered() && show_tooltips) {
                    imgui.SetTooltipEx(
                        "PC latency from NVAPI Reflex (input sample to GPU render end) and GPU frame time.\n"
                        "FrameID: %llu",
                        static_cast<unsigned long long>(metrics.frame_id));
                }
            }
        }
    }

    if (show_fps_limiter_src) {
        const char* src_name = GetChosenFpsLimiterSiteName();
        if (settings::g_mainTabSettings.show_labels.GetValue()) {
            imgui.Text("FPS limiter source: %s", src_name);
        } else {
            imgui.Text("%s", src_name);
        }
    }

    if (show_cpu_fps) {
        LONGLONG cpu_time_ns =
            ::g_frame_time_ns.load() - fps_sleep_after_on_present_ns.load() - fps_sleep_before_on_present_ns.load();
        LONGLONG frame_time_ns = ::g_frame_time_ns.load();

        double current_fps = 0.0;
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
            current_fps = sample_count / total_time;
        }

        if (current_fps > 0.0 && cpu_time_ns > 0 && frame_time_ns > 0) {
            double cpu_busy_percent = (static_cast<double>(cpu_time_ns) / static_cast<double>(frame_time_ns)) * 100.0;
            if (cpu_busy_percent < 0.0) cpu_busy_percent = 0.0;
            if (cpu_busy_percent > 100.0) cpu_busy_percent = 100.0;

            if (cpu_busy_percent > 0.0) {
                double cpu_fps_raw = current_fps / (cpu_busy_percent / 100.0);
                if (cpu_fps_raw > 9999.0) cpu_fps_raw = 9999.0;
                static double s_smoothed_cpu_fps = 0.0;
                static double s_displayed_cpu_fps = 0.0;
                static LONGLONG s_cpu_fps_last_display_ns = 0;
                constexpr double k_cpu_fps_alpha = 0.01;
                s_smoothed_cpu_fps = k_cpu_fps_alpha * cpu_fps_raw + (1.0 - k_cpu_fps_alpha) * s_smoothed_cpu_fps;
                LONGLONG now_ns = utils::get_now_ns();
                const LONGLONG k_cpu_fps_display_interval_ns =
                    static_cast<LONGLONG>(0.2 * static_cast<double>(utils::SEC_TO_NS));
                if (now_ns - s_cpu_fps_last_display_ns >= k_cpu_fps_display_interval_ns) {
                    s_cpu_fps_last_display_ns = now_ns;
                    s_displayed_cpu_fps = s_smoothed_cpu_fps;
                }
                double cpu_fps = s_displayed_cpu_fps;
                if (settings::g_mainTabSettings.show_labels.GetValue()) {
                    imgui.Text("%.1f cpu fps", cpu_fps);
                } else {
                    imgui.Text("%.1f", cpu_fps);
                }
            }
        }
    }

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
                    if (settings::g_mainTabSettings.show_labels.GetValue()) {
                        if (is_muted) {
                            imgui.Text("%.0f%% vol muted", volume_value);
                        } else {
                            imgui.Text("%.0f%% vol", volume_value);
                        }
                    } else {
                        if (is_muted) {
                            imgui.Text("%.0f%% muted", volume_value);
                        } else {
                            imgui.Text("%.0f%%", volume_value);
                        }
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

    if (show_frame_time_graph) {
        ui::new_ui::DrawFrameTimeGraphOverlay(imgui, show_tooltips);
    }

    if (show_native_frame_time_graph) {
        ui::new_ui::DrawNativeFrameTimeGraphOverlay(imgui, show_tooltips);
    }

    if (settings::g_mainTabSettings.show_frame_timeline_bar.GetValue()) {
        ui::new_ui::DrawFrameTimelineBarOverlay(imgui, show_tooltips);
    }

    if (settings::g_mainTabSettings.show_refresh_rate_frame_times.GetValue()) {
        ui::new_ui::DrawRefreshRateFrameTimesGraph(imgui, show_tooltips);
    }

    modules::DrawEnabledModulesInOverlay(imgui);
}

}  // namespace ui::new_ui

