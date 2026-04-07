#include "advanced_tab.hpp"
#include "../../features/dpi/dpi_management.hpp"
#include "../../globals.hpp"
#include "../../hooks/vulkan/nvlowlatencyvk_hooks.hpp"
#include "../../latency/reflex_provider.hpp"
#include "../forkawesome.h"
#include "../ui_colors.hpp"
#include "../../settings/advanced_tab_settings.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../swapchain_events.hpp"
#include "../../ui/imgui_wrapper_base.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/timing.hpp"
#include "settings_wrapper.hpp"

#include <atomic>

#include <windows.h>

namespace ui::new_ui {

using namespace display_commander::ui;

void DrawFeaturesEnabledByDefault(display_commander::ui::IImGuiWrapper& imgui);
void DrawAdvancedTabSettingsSection(display_commander::ui::GraphicsApi api,
                                    display_commander::ui::IImGuiWrapper& imgui);
void DrawGlobalSettingsSection(display_commander::ui::IImGuiWrapper& imgui);
void DrawNvapiSettings(display_commander::ui::GraphicsApi api, display_commander::ui::IImGuiWrapper& imgui);

void InitAdvancedTab() {
    // Ensure settings are loaded
    static bool settings_loaded = false;
    if (!settings_loaded) {
        // Settings already loaded at startup
        settings_loaded = true;
    }
}

void DrawAdvancedTab(display_commander::ui::GraphicsApi api, display_commander::ui::IImGuiWrapper& imgui) {
    // Global settings (stored in Display Commander folder, shared across all games)
    if (imgui.CollapsingHeader("Global settings", wrapper_flags::TreeNodeFlags_None)) {
        DrawGlobalSettingsSection(imgui);
    }

    // Advanced Settings Section
    if (imgui.CollapsingHeader("Advanced Settings", wrapper_flags::TreeNodeFlags_None)) {
        DrawAdvancedTabSettingsSection(api, imgui);
    }

    // NVAPI Settings Section - only show if game is in NVAPI game list
    DrawNvapiSettings(api, imgui);

}

void DrawDcServiceStatusIndicators(display_commander::ui::IImGuiWrapper& imgui, bool include_version_in_tooltip) {
    (void)imgui;
    (void)include_version_in_tooltip;
}

void DrawDcServiceIndicatorsOnLine(display_commander::ui::IImGuiWrapper& imgui, bool include_version_in_tooltip) {
    (void)imgui;
    (void)include_version_in_tooltip;
}

void DrawDcServiceSection(display_commander::ui::IImGuiWrapper& imgui) {
    (void)imgui;
}

void DrawGlobalSettingsSection(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Indent();

    // Windows Gaming Input suppression globally (stored in global_overrides.toml; overrides per-game value)
    if (CheckboxSetting(settings::g_advancedTabSettings.suppress_wgi_globally,
                        "Enable Windows Gaming Input suppression globally", imgui)) {
        LogInfo("Suppress WGI globally changed to: %s",
                settings::g_advancedTabSettings.suppress_wgi_globally.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "When enabled, Windows Gaming Input is suppressed for all games (same effect as the per-game checkbox "
            "in the Controller tab, but applied everywhere). Stored in the Display Commander folder "
            "(global_overrides.toml, same location as hotkeys.toml). Overrides the per-game value even when the game config has it. "
            "Restart each game to apply.");
    }
    imgui.SameLine();
    imgui.TextColored(::ui::colors::TEXT_WARNING, ICON_FK_WARNING);
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Suppressing Windows Gaming Input may break networking in some games.");
    }

    imgui.Unindent();
}

void DrawAdvancedTabSettingsSection(display_commander::ui::GraphicsApi api, display_commander::ui::IImGuiWrapper& imgui) {
    (void)api;
    imgui.Indent();

    // Suppress Window Changes setting
    if (CheckboxSetting(settings::g_advancedTabSettings.suppress_window_changes, "Suppress Window Changes", imgui)) {
        LogInfo("Suppress Window Changes setting changed to: %s",
                settings::g_advancedTabSettings.suppress_window_changes.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Suppresses automatic window position, size, and style changes from continuous monitoring.\n"
            "When enabled, ApplyWindowChange will not be called automatically.\n"
            "This is a compatibility feature for cases where automatic window management causes issues.\n\n"
            "Default: disabled (window changes are applied automatically).");
    }

    imgui.Unindent();
}

void DrawNvapiSettings(display_commander::ui::GraphicsApi api, display_commander::ui::IImGuiWrapper& imgui) {
    uint64_t now_ns = utils::get_now_ns();

    // Minimal NVIDIA Reflex Controls (device runtime dependent); only when Reflex is available (64-bit + native or
    // NVAPI init)
    if (imgui.CollapsingHeader("NVIDIA Reflex (Minimal)", wrapper_flags::TreeNodeFlags_None)) {
        imgui.Indent();
        // Native Reflex Status Indicator
        bool is_native_reflex_active = IsNativeReflexActive();
        if (is_native_reflex_active) {
            imgui.TextColored(::ui::colors::ICON_SUCCESS, ICON_FK_OK " Native Reflex: ACTIVE Limit Real Frames: ON");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("The game has native Reflex support and is actively using it. ");
            }
        } else {
            imgui.TextColored(::ui::colors::TEXT_DIMMED,
                              ICON_FK_MINUS " Native Reflex: INACTIVE Limit Real Frames: OFF");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("No native Reflex activity detected. ");
            }
        }
        bool reflex_auto_configure = settings::g_advancedTabSettings.reflex_auto_configure.GetValue();
        bool reflex_delay_first_500_frames = settings::g_advancedTabSettings.reflex_delay_first_500_frames.GetValue();
        bool reflex_use_markers = settings::g_advancedTabSettings.reflex_use_markers.GetValue();
        bool reflex_generate_markers = settings::g_advancedTabSettings.reflex_generate_markers.GetValue();
        bool reflex_enable_sleep = settings::g_advancedTabSettings.reflex_enable_sleep.GetValue();

        // Reflex enable / low latency / boost are derived from Main tab FPS limiter mode (onpresent_reflex_mode,
        // reflex_limiter_reflex_mode, reflex_disabled_limiter_mode). Shown as read-only Yes/No.
        const bool reflex_enabled = ShouldReflexBeEnabled();
        const bool reflex_low_latency = ShouldReflexLowLatencyBeEnabled();
        const bool reflex_boost = ShouldReflexBoostBeEnabled();
        imgui.Text("Reflex: %s", reflex_enabled ? "Yes" : "No");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Derived from Main tab FPS Limiter Mode and Reflex combo (OnPresent / Reflex / Disabled).");
        }
        imgui.Text("Low Latency: %s", reflex_low_latency ? "Yes" : "No");
        imgui.Text("Boost: %s", reflex_boost ? "Yes" : "No");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Configure in Main tab under FPS Limiter Mode (Reflex combo).");
        }

        /*
        if (imgui.Checkbox("Delay Reflex for first 500 frames", &reflex_delay_first_500_frames)) {
            settings::g_advancedTabSettings.reflex_delay_first_500_frames.SetValue(reflex_delay_first_500_frames);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "When enabled, NVIDIA Reflex integration will not be activated\n"
                "until after the first 500 frames of the game (g_global_frame_id >= 500),\n"
                "even if Reflex (from Main tab) or auto-configure would normally turn it on.");
        }

        if (imgui.Checkbox("Auto Configure Reflex", &reflex_auto_configure)) {
            settings::g_advancedTabSettings.reflex_auto_configure.SetValue(reflex_auto_configure);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Automatically configure Reflex settings on startup");
        }
        if (reflex_auto_configure) {
            imgui.Text("Auto-configure is handled by continuous monitoring");
        }
        if (reflex_enabled) {
            if (reflex_auto_configure) {
                imgui.BeginDisabled();
            }
            if (imgui.Checkbox("Use Reflex Markers", &reflex_use_markers)) {
                settings::g_advancedTabSettings.reflex_use_markers.SetValue(reflex_use_markers);
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Tell NVIDIA Reflex to use markers for optimization");
            }

            if (imgui.Checkbox("Generate Reflex Markers", &reflex_generate_markers)) {
                settings::g_advancedTabSettings.reflex_generate_markers.SetValue(reflex_generate_markers);
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Generate markers in the frame timeline for latency measurement");
            }
            // Warning about enabling Reflex when game already has it
            if (is_native_reflex_active && settings::g_advancedTabSettings.reflex_generate_markers.GetValue()) {
                imgui.SameLine();
                imgui.TextColored(::ui::colors::ICON_WARNING, ICON_FK_WARNING
                                  " Warning: Do not enable 'Generate Reflex Markers' if the game already has built-in "
                                  "Reflex support!");
            }

            if (imgui.Checkbox("Enable Reflex Sleep Mode", &reflex_enable_sleep)) {
                settings::g_advancedTabSettings.reflex_enable_sleep.SetValue(reflex_enable_sleep);
            }
            if (is_native_reflex_active && settings::g_advancedTabSettings.reflex_enable_sleep.GetValue()) {
                imgui.SameLine();
                imgui.TextColored(::ui::colors::ICON_WARNING, ICON_FK_WARNING
                                  " Warning: Do not enable 'Enable Reflex Sleep Mode' if the game already has "
                                  "built-in Reflex support!");
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Enable Reflex sleep mode calls (disabled by default for safety).");
            }
            if (reflex_auto_configure) {
                imgui.EndDisabled();
            }
            bool reflex_logging = settings::g_advancedTabSettings.reflex_logging.GetValue();
            if (imgui.Checkbox("Enable Reflex Logging", &reflex_logging)) {
                settings::g_advancedTabSettings.reflex_logging.SetValue(reflex_logging);
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Enable detailed logging of Reflex marker operations for debugging purposes.");
            }
        }
*/
        // Reflex Sleep Status Section
        imgui.Spacing();
        imgui.Separator();
        imgui.Spacing();

        if (imgui.CollapsingHeader("Reflex Sleep Status", wrapper_flags::TreeNodeFlags_None)) {
            // Try to get sleep status from latency manager
            NV_GET_SLEEP_STATUS_PARAMS sleep_status = {};
            sleep_status.version = NV_GET_SLEEP_STATUS_PARAMS_VER;

            bool status_available = false;
            SleepStatusUnavailableReason unavailable_reason = SleepStatusUnavailableReason::kNone;

            if (!g_reflexProvider) {
                unavailable_reason = SleepStatusUnavailableReason::kNoReflex;
            } else if (!g_reflexProvider->IsInitialized()) {
                unavailable_reason = SleepStatusUnavailableReason::kReflexNotInitialized;
            } else {
                status_available = g_reflexProvider->GetSleepStatus(&sleep_status, &unavailable_reason);
            }

            if (status_available) {
                imgui.TextColored(ImVec4{0.8f, 0.8f, 0.8f, 1.0f}, "Current Reflex Status:");
                imgui.Indent();

                // Low Latency Mode status
                bool low_latency_enabled = (sleep_status.bLowLatencyMode == NV_TRUE);
                imgui.TextColored(low_latency_enabled ? ::ui::colors::ICON_SUCCESS : ::ui::colors::TEXT_DIMMED,
                                  "Low Latency Mode: %s", low_latency_enabled ? "ENABLED" : "DISABLED");
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx(
                        "Indicates whether NVIDIA Reflex Low Latency Mode is currently active in the driver.");
                }

                // Fullscreen VRR status
                bool fs_vrr = (sleep_status.bFsVrr == NV_TRUE);
                imgui.Text("Fullscreen VRR: %s", fs_vrr ? "ENABLED" : "DISABLED");
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx(
                        "Indicates if fullscreen GSYNC or GSYNC Compatible mode is active (valid only when app is in "
                        "foreground).");
                }

                // Control Panel VSYNC Override
                bool cpl_vsync_on = (sleep_status.bCplVsyncOn == NV_TRUE);
                imgui.Text("Control Panel VSYNC Override: %s", cpl_vsync_on ? "ON" : "OFF");
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx("Indicates if NVIDIA Control Panel is overriding VSYNC settings.");
                }

                // Sleep interval
                if (sleep_status.sleepIntervalUs > 0) {
                    float fps_limit = 1000000.0f / static_cast<float>(sleep_status.sleepIntervalUs);
                    imgui.Text("Sleep Interval: %u us (%.2f FPS limit)", sleep_status.sleepIntervalUs, fps_limit);
                } else {
                    imgui.Text("Sleep Interval: Not set");
                }
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx("Latest sleep interval in microseconds (inverse of FPS limit).");
                }

                // Game Sleep status
                bool use_game_sleep = (sleep_status.bUseGameSleep == NV_TRUE);
                imgui.Text("Game Sleep Calls: %s", use_game_sleep ? "ACTIVE" : "INACTIVE");
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx("Indicates if NvAPI_D3D_Sleep() is being called by the game or addon.");
                }

                imgui.Unindent();
            } else {
                imgui.TextColored(::ui::colors::TEXT_DIMMED, "Sleep status not available: %s",
                                  SleepStatusUnavailableReasonToString(unavailable_reason));
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx(
                        "Sleep status requires an initialized DirectX 11/12 device and NVIDIA GPU with Reflex "
                        "support.");
                }
            }

            // NvLL VK (Vulkan Reflex) params when NvLowLatencyVk hooks are active
            if (AreNvLowLatencyVkHooksInstalled()) {
                imgui.Spacing();
                imgui.TextColored(ImVec4{0.8f, 0.8f, 0.8f, 1.0f}, "NvLL VK (Vulkan Reflex) SetSleepMode:");
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx(
                        "When NvLowLatencyVk hooks are installed, we re-apply SleepMode on SIMULATION_START.\n"
                        "'Last applied' is what we sent to the driver; 'Game tried to set' is what the game passed.");
                }
                imgui.Indent();
                NvLLVkSleepModeParamsView last_applied = {};
                GetNvLowLatencyVkLastAppliedSleepModeParams(&last_applied);
                if (last_applied.has_value) {
                    imgui.TextColored(::ui::colors::ICON_SUCCESS, "Last applied (via SetSleepMode_Original):");
                    imgui.Text("  Low Latency: %s  Boost: %s  Min interval: %u us",
                               last_applied.low_latency ? "Yes" : "No", last_applied.boost ? "Yes" : "No",
                               last_applied.minimum_interval_us);
                    if (last_applied.minimum_interval_us > 0) {
                        float fps = 1000000.0f / last_applied.minimum_interval_us;
                        imgui.Text("  Target FPS: %.1f", fps);
                    }
                } else {
                    imgui.TextColored(ImVec4{0.6f, 0.6f, 0.6f, 1.0f}, "Last applied: (none yet)");
                }
                NvLLVkSleepModeParamsView game_params = {};
                GetNvLowLatencyVkGameSleepModeParams(&game_params);
                if (game_params.has_value) {
                    imgui.TextColored(ImVec4{0.8f, 0.8f, 0.8f, 1.0f}, "Game tried to set (NvLL_VK_SetSleepMode):");
                    imgui.Text("  Low Latency: %s  Boost: %s  Min interval: %u us",
                               game_params.low_latency ? "Yes" : "No", game_params.boost ? "Yes" : "No",
                               game_params.minimum_interval_us);
                    if (game_params.minimum_interval_us > 0) {
                        float fps = 1000000.0f / game_params.minimum_interval_us;
                        imgui.Text("  Target FPS: %.1f", fps);
                    }
                } else {
                    imgui.TextColored(ImVec4{0.6f, 0.6f, 0.6f, 1.0f}, "Game tried to set: (none yet)");
                }
                imgui.Unindent();
            }
        }

        // Reflex Debug Counters Section
        imgui.Spacing();
        imgui.Separator();
        imgui.Spacing();

        if (imgui.CollapsingHeader("Reflex Debug Counters", wrapper_flags::TreeNodeFlags_None)) {
            extern std::atomic<uint32_t> g_reflex_sleep_count;
            extern std::atomic<uint32_t> g_reflex_apply_sleep_mode_count;
            extern std::atomic<LONGLONG> g_reflex_sleep_duration_ns;
            extern std::atomic<uint32_t> g_reflex_marker_simulation_start_count;
            extern std::atomic<uint32_t> g_reflex_marker_simulation_end_count;
            extern std::atomic<uint32_t> g_reflex_marker_rendersubmit_start_count;
            extern std::atomic<uint32_t> g_reflex_marker_rendersubmit_end_count;
            extern std::atomic<uint32_t> g_reflex_marker_present_start_count;
            extern std::atomic<uint32_t> g_reflex_marker_present_end_count;
            extern std::atomic<uint32_t> g_reflex_marker_input_sample_count;

            uint32_t sleep_count = ::g_reflex_sleep_count.load();
            uint32_t apply_sleep_mode_count = ::g_reflex_apply_sleep_mode_count.load();
            LONGLONG sleep_duration_ns = ::g_reflex_sleep_duration_ns.load();
            uint32_t sim_start_count = ::g_reflex_marker_simulation_start_count.load();
            uint32_t sim_end_count = ::g_reflex_marker_simulation_end_count.load();
            uint32_t render_start_count = ::g_reflex_marker_rendersubmit_start_count.load();
            uint32_t render_end_count = ::g_reflex_marker_rendersubmit_end_count.load();
            uint32_t present_start_count = ::g_reflex_marker_present_start_count.load();
            uint32_t present_end_count = ::g_reflex_marker_present_end_count.load();
            uint32_t input_sample_count = ::g_reflex_marker_input_sample_count.load();

            uint32_t total_marker_count = sim_start_count + sim_end_count + render_start_count + render_end_count
                                          + present_start_count + present_end_count + input_sample_count;

            imgui.TextColored(ImVec4{0.8f, 0.8f, 0.8f, 1.0f}, "Reflex API Call Counters:");
            imgui.Indent();
            imgui.Text("Sleep calls: %u", sleep_count);
            if (sleep_count > 0) {
                double sleep_duration_ms = sleep_duration_ns / 1000000.0;
                imgui.Text("Avg Sleep Duration: %.3f ms", sleep_duration_ms);
            }
            imgui.Text("ApplySleepMode calls: %u", apply_sleep_mode_count);
            imgui.Text("Total SetMarker calls: %u", total_marker_count);
            imgui.Unindent();

            imgui.Spacing();
            imgui.TextColored(ImVec4{0.8f, 0.8f, 0.8f, 1.0f}, "Individual Marker Type Counts:");
            imgui.Indent();
            imgui.Text("SIMULATION_START: %u", sim_start_count);
            imgui.Text("SIMULATION_END: %u", sim_end_count);
            imgui.Text("RENDERSUBMIT_START: %u", render_start_count);
            imgui.Text("RENDERSUBMIT_END: %u", render_end_count);
            imgui.Text("PRESENT_START: %u", present_start_count);
            imgui.Text("PRESENT_END: %u", present_end_count);
            imgui.Text("INPUT_SAMPLE: %u", input_sample_count);
            imgui.Unindent();

            imgui.Spacing();
            imgui.TextColored(ImVec4{0.6f, 0.6f, 0.6f, 1.0f}, "These counters help debug Reflex FPS limiter issues.");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Marker counts show which specific markers are being set:\n"
                    "- SIMULATION_START/END: Frame simulation markers\n"
                    "- RENDERSUBMIT_START/END: GPU submission markers\n"
                    "- PRESENT_START/END: Present call markers\n"
                    "- INPUT_SAMPLE: Input sampling markers\n\n"
                    "If all marker counts are 0, Reflex markers are not being set.\n"
                    "If Sleep calls are 0, the Reflex sleep mode is not being called.\n"
                    "If ApplySleepMode calls are 0, the Reflex configuration is not being applied.");
            }

            imgui.Spacing();
            imgui.Separator();
            imgui.Spacing();

            // Native Reflex Counters
            uint32_t native_sleep_count = ::g_nvapi_event_counters[NVAPI_EVENT_D3D_SLEEP].load();
            uint32_t native_set_sleep_mode_count = ::g_nvapi_event_counters[NVAPI_EVENT_D3D_SET_SLEEP_MODE].load();
            uint32_t native_set_latency_marker_count =
                ::g_nvapi_event_counters[NVAPI_EVENT_D3D_SET_LATENCY_MARKER].load();
            uint32_t native_get_latency_count = ::g_nvapi_event_counters[NVAPI_EVENT_D3D_GET_LATENCY].load();
            uint32_t native_get_sleep_status_count = ::g_nvapi_event_counters[NVAPI_EVENT_D3D_GET_SLEEP_STATUS].load();
            LONGLONG native_sleep_ns = ::g_sleep_reflex_native_ns.load();
            LONGLONG native_sleep_ns_smooth = ::g_sleep_reflex_native_ns_smooth.load();

            imgui.TextColored(ImVec4{0.8f, 0.8f, 0.8f, 1.0f}, "Native Reflex API Call Counters:");
            imgui.Indent();
            imgui.Text("NvAPI_D3D_Sleep calls: %u", native_sleep_count);
            if (native_sleep_count > 0 && native_sleep_ns_smooth > 0) {
                double native_calls_per_second = 1000000000.0 / static_cast<double>(native_sleep_ns_smooth);
                imgui.Text("Native Sleep Rate: %.2f times/sec (%.1f ms interval)", native_calls_per_second,
                           native_sleep_ns_smooth / 1000000.0);
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx("Smoothed interval using rolling average. Raw: %.1f ms",
                                       native_sleep_ns > 0 ? native_sleep_ns / 1000000.0 : 0.0);
                }
            }
            imgui.Text("NvAPI_D3D_SetSleepMode calls: %u", native_set_sleep_mode_count);
            imgui.Text("NvAPI_D3D_SetLatencyMarker calls: %u", native_set_latency_marker_count);
            imgui.Text("NvAPI_D3D_GetLatency calls: %u", native_get_latency_count);
            imgui.Text("NvAPI_D3D_GetSleepStatus calls: %u", native_get_sleep_status_count);
            imgui.Unindent();

            imgui.Spacing();
            imgui.TextColored(ImVec4{0.6f, 0.6f, 0.6f, 1.0f},
                              "These counters track native Reflex API calls from the game.");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Native Reflex counters show when the game itself calls NVAPI Reflex functions:\n"
                    "- NvAPI_D3D_Sleep: Game's sleep calls for frame pacing\n"
                    "- NvAPI_D3D_SetSleepMode: Game's Reflex configuration calls\n"
                    "- NvAPI_D3D_SetLatencyMarker: Game's latency marker calls\n"
                    "- NvAPI_D3D_GetLatency: Game's latency query calls\n"
                    "- NvAPI_D3D_GetSleepStatus: Game's sleep status query calls\n\n"
                    "If all counts are 0, the game is not using native Reflex.\n"
                    "If counts are increasing, the game has native Reflex support.");
            }

            if (imgui.Button("Reset Counters")) {
                // Reset injected Reflex counters
                ::g_reflex_sleep_count.store(0);
                ::g_reflex_apply_sleep_mode_count.store(0);
                ::g_reflex_sleep_duration_ns.store(0);
                ::g_reflex_marker_simulation_start_count.store(0);
                ::g_reflex_marker_simulation_end_count.store(0);
                ::g_reflex_marker_rendersubmit_start_count.store(0);
                ::g_reflex_marker_rendersubmit_end_count.store(0);
                ::g_reflex_marker_present_start_count.store(0);
                ::g_reflex_marker_present_end_count.store(0);
                ::g_reflex_marker_input_sample_count.store(0);

                // Reset native Reflex counters
                ::g_nvapi_event_counters[NVAPI_EVENT_D3D_SLEEP].store(0);
                ::g_nvapi_event_counters[NVAPI_EVENT_D3D_SET_SLEEP_MODE].store(0);
                ::g_nvapi_event_counters[NVAPI_EVENT_D3D_SET_LATENCY_MARKER].store(0);
                ::g_nvapi_event_counters[NVAPI_EVENT_D3D_GET_LATENCY].store(0);
                ::g_nvapi_event_counters[NVAPI_EVENT_D3D_GET_SLEEP_STATUS].store(0);
                ::g_sleep_reflex_native_ns.store(0);
                ::g_sleep_reflex_native_ns_smooth.store(0);
                ::g_nvapi_d3d_last_sleep_global_frame_id.store(0);
                for (size_t i = 0; i < static_cast<size_t>(::kLatencyMarkerTypeCountFirstSix); ++i) {
                    ::g_nvapi_d3d_last_global_frame_id_by_marker_type[i].store(0);
                }
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Reset all Reflex debug counters to zero.");
            }
        }
        imgui.Unindent();
    }

}

}  // namespace ui::new_ui
