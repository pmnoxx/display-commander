// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "display_settings_internal.hpp"

namespace ui::new_ui {

namespace {

// Width for FPS limit sliders and FPS Limiter Mode combo in Main tab
static constexpr float kFpsLimiterItemWidth = 600.0f;

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
    (void)latest_type;
    (void)latest_frame;
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

}  // namespace

static void DrawDisplaySettings_FpsLimiterAdvanced(display_commander::ui::IImGuiWrapper& imgui,
                                                  float fps_limiter_checkbox_column_gutter);
static void DrawDisplaySettings_FpsLimiterOnPresentSync(display_commander::ui::IImGuiWrapper& imgui,
                                                        const std::function<void()>& drawPclStatsCheckbox,
                                                        float fps_limiter_checkbox_column_gutter);
static void DrawDisplaySettings_FpsLimiterReflex(display_commander::ui::IImGuiWrapper& imgui,
                                                 const std::function<void()>& drawPclStatsCheckbox);
static void DrawDisplaySettings_FpsLimiterLatentSync(display_commander::ui::IImGuiWrapper& imgui);

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
                        tooltip_oss << "FPS = " << refresh_hz << " ÷ " << x << " = " << candidate_precise
                                    << " FPS\n\n";
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

            const float debug_label_width =
                (std::min)(280.0f, (std::max)(180.0f, imgui.GetContentRegionAvail().x * 0.45f));
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
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Controls the balance between display latency and input latency.\n\n"
                    "Available in 12.5%% steps:\n"
                    "100%% Display / 0%% Input: Prioritizes consistent frame timing (better frame timing at cost "
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
                    imgui.Text("Frame Start: %lld ns (%.3f ms ago)", frame_start_ns, time_since_start_ns / 1'000'000.0f);
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
            auto use_reflex_markers_as_fps_limiter = settings::g_mainTabSettings.use_reflex_markers_as_fps_limiter.GetValue();
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
                LogInfo("Use Reflex markers as FPS limiter %s",
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
                if (settings::g_mainTabSettings.reflex_fps_limiter_max_queued_frames.GetValue() > 0) imgui.BeginDisabled();
                if (CheckboxSetting(settings::g_mainTabSettings.native_pacing_sim_start_only, "Native frame pacing", imgui)) {
                    LogInfo("Native pacing sim start only %s",
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
                if (settings::g_mainTabSettings.reflex_fps_limiter_max_queued_frames.GetValue() > 0) imgui.EndDisabled();
                imgui.Unindent();
                imgui.Unindent();
            }
        }
    }

    // Experimental Safe Mode fps limiter (only visible if OnPresentSync mode is selected)
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
                    (void)hwnd;
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

    // Limit Real Frames indicator
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

        if (IsNativeReflexActive()) {
            imgui.TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ICON_FK_OK " Native Reflex: ACTIVE Limit Real Frames: ON");
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
            imgui.TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ICON_FK_OK " Injected Reflex: ACTIVE Limit Real Frames: %s",
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
                imgui.TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                                  ICON_FK_WARNING
                                  " Warning: Both native and injected Reflex are active - this may cause conflicts! (FIXME)");
            }

            drawPclStatsCheckbox();
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
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Scanline offset for latent sync (-1000 to 1000). This defines the offset from the "
            "threshold where frame pacing is active.");
    }

    // VBlank Sync Divisor (only visible if latent sync mode is selected)
    if (SliderIntSetting(settings::g_mainTabSettings.vblank_sync_divisor,
                         "VBlank Sync Divisor (controls FPS limit as fraction of monitor refresh rate)", "%d", imgui)) {
    }
    if (imgui.IsItemHovered()) {
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
    (void)fps_limit_enabled;

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
                            "Reflex sleep were observed in the last 1 s.\nReflex sleep time: %.2f ms (rolling average).",
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

    // Reflex combo: always visible; which setting is used depends on FPS Limiter Mode (and applies even when checkbox off)
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
        DrawDisplaySettings_FpsLimiterOnPresentSync(imgui, DrawPclStatsCheckbox, fps_limiter_checkbox_column_gutter);
    }

    if (current_item == static_cast<int>(FpsLimiterMode::kReflex)) {
        DrawDisplaySettings_FpsLimiterReflex(imgui, DrawPclStatsCheckbox);
    }

    // Latent Sync Mode
    if (s_fps_limiter_mode.load() == FpsLimiterMode::kLatentSync) {
        DrawDisplaySettings_FpsLimiterLatentSync(imgui);
    }
}

}  // namespace ui::new_ui

