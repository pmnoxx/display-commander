#include "main_new_tab.hpp"
#include "../../addon.hpp"
#include "../../adhd_multi_monitor/adhd_simple_api.hpp"
#include "../../audio/audio_management.hpp"
#include "../../latent_sync/latent_sync_limiter.hpp"
#include "../../settings/developer_tab_settings.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../widgets/resolution_widget/resolution_widget.hpp"
#include "globals.hpp"
#include "utils/timing.hpp"
#include "version.hpp"

#include <imgui.h>
#include <minwindef.h>

#include <atomic>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <thread>

namespace ui::new_ui {

namespace {
// Flag to indicate a restart is required after changing VSync/tearing options
std::atomic<bool> s_restart_needed_vsync_tearing{false};
}  // anonymous namespace

void InitMainNewTab() {
    static bool settings_loaded_once = false;
    if (!settings_loaded_once) {
        // Ensure developer settings (including continuous monitoring) are loaded so UI reflects saved state
        settings::g_developerTabSettings.LoadAll();
        settings::g_mainTabSettings.LoadSettings();
        s_window_mode = static_cast<WindowMode>(settings::g_mainTabSettings.window_mode.GetValue());
        s_aspect_index = static_cast<AspectRatioType>(settings::g_mainTabSettings.aspect_index.GetValue());
        s_window_alignment = static_cast<WindowAlignment>(settings::g_mainTabSettings.alignment.GetValue());
        // FPS limits are now automatically synced via FloatSettingRef
        s_audio_mute.store(settings::g_mainTabSettings.audio_mute.GetValue());
        s_mute_in_background.store(settings::g_mainTabSettings.mute_in_background.GetValue());
        s_mute_in_background_if_other_audio.store(
            settings::g_mainTabSettings.mute_in_background_if_other_audio.GetValue());
        s_no_present_in_background.store(settings::g_mainTabSettings.no_present_in_background.GetValue());
        // VSync & Tearing - all automatically synced via BoolSettingRef

        // Update input blocking system with loaded settings
        // Input blocking is now handled by Windows message hooks

        // Apply ADHD Multi-Monitor Mode settings
        adhd_multi_monitor::api::SetEnabled(settings::g_mainTabSettings.adhd_multi_monitor_enabled.GetValue());

        settings_loaded_once = true;

        // FPS limiter mode
        s_fps_limiter_mode.store(static_cast<FpsLimiterMode>(settings::g_mainTabSettings.fps_limiter_mode.GetValue()));
        // FPS limiter injection timing
        s_fps_limiter_injection.store(settings::g_mainTabSettings.fps_limiter_injection.GetValue());
        // Scanline offset and VBlank Sync Divisor are now automatically synced via IntSettingRef

        // Initialize resolution widget
        display_commander::widgets::resolution_widget::InitializeResolutionWidget();
    }
}

void DrawMainNewTab() {
    // Load saved settings once and sync legacy globals

    // Version and build information at the top
    ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "=== Display Commander ===");
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Version: %s", DISPLAY_COMMANDER_VERSION_STRING);
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Build: %s %s", DISPLAY_COMMANDER_BUILD_DATE,
                       DISPLAY_COMMANDER_BUILD_TIME);
    ImGui::Separator();

    ImGui::Text("Main Tab - Basic Settings");
    ImGui::Separator();

    // Display Settings Section
    DrawDisplaySettings();

    ImGui::Spacing();
    ImGui::Separator();

    // Monitor/Display Resolution Settings Section
    DrawMonitorDisplaySettings();

    ImGui::Spacing();
    ImGui::Separator();

    // Audio Settings Section
    DrawAudioSettings();

    ImGui::Spacing();
    ImGui::Separator();

    // Input Blocking (Background) Section
    {
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Input Control (Background) ===");
        bool block_any_in_background = settings::g_mainTabSettings.block_input_in_background.GetValue();
        if (ImGui::Checkbox("Block Input in Background", &block_any_in_background)) {
            settings::g_mainTabSettings.block_input_in_background.SetValue(block_any_in_background);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Blocks mouse, keyboard, and cursor warping while the game window is not focused.");
        }

        // Input blocking without Reshade (handled by Windows message hooks)
        bool block_without_reshade = settings::g_mainTabSettings.block_input_without_reshade.GetValue();
        if (ImGui::Checkbox("Block input without Reshade (Uses Windows message hooks)", &block_without_reshade)) {
            settings::g_mainTabSettings.block_input_without_reshade.SetValue(block_without_reshade);
            // No need to call update function - the message hooks check the setting directly
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Uses Windows message hooks to block input independently of Reshade's system. Required "
                "for future gamepad remapping features.");
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Screensaver Control Section
    {
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Screensaver Control ===");
        if (ComboSettingEnumRefWrapper(settings::g_mainTabSettings.screensaver_mode, "Screensaver Mode")) {
            LogInfo("Screensaver mode changed to %d", settings::g_mainTabSettings.screensaver_mode.GetValue());
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Controls screensaver behavior while the game is running:\n\n"
                "• Default (no change): Preserves original game behavior\n"
                "• Disable when Focused: Disables screensaver when game window is focused\n"
                "• Disable: Always disables screensaver while game is running\n\n"
                "Note: This feature requires the screensaver implementation to be active.");
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Window Controls Section
    DrawWindowControls();

    ImGui::Spacing();
    ImGui::Separator();

    DrawImportantInfo();
}

void DrawQuickResolutionChanger() {
    // Quick-set buttons based on current monitor refresh rate
    {
        double refresh_hz;
        {
            auto window_state = ::g_window_state.load();
            refresh_hz = window_state->current_monitor_refresh_rate.ToHz();
        }
        int y = static_cast<int>(std::round(refresh_hz));
        if (y > 0) {
            bool first = true;
            const float selected_epsilon = 0.0001f;
            // Add No Limit button at the beginning
            {
                bool selected =
                    (std::fabs(settings::g_mainTabSettings.fps_limit.GetValue() - 0.0f) <= selected_epsilon);
                if (selected) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.60f, 0.20f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.70f, 0.20f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.50f, 0.10f, 1.0f));
                }
                if (ImGui::Button("No Limit")) {
                    settings::g_mainTabSettings.fps_limit.SetValue(0.0f);
                }
                if (selected) {
                    ImGui::PopStyleColor(3);
                }
            }
            first = false;
            for (int x = 1; x <= 15; ++x) {
                if (y % x == 0) {
                    int candidate_rounded = y / x;
                    float candidate_precise = refresh_hz / x;
                    if (candidate_rounded >= 30) {
                        if (!first) ImGui::SameLine();
                        first = false;
                        std::string label = std::to_string(candidate_rounded);
                        {
                            bool selected =
                                (std::fabs(settings::g_mainTabSettings.fps_limit.GetValue() - candidate_precise)
                                 <= selected_epsilon);
                            if (selected) {
                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.60f, 0.20f, 1.0f));
                                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.70f, 0.20f, 1.0f));
                                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.50f, 0.10f, 1.0f));
                            }
                            if (ImGui::Button(label.c_str())) {
                                float target_fps = candidate_precise;
                                settings::g_mainTabSettings.fps_limit.SetValue(target_fps);
                            }
                            if (selected) {
                                ImGui::PopStyleColor(3);
                            }
                            // Add tooltip showing the precise calculation
                            if (ImGui::IsItemHovered()) {
                                std::ostringstream tooltip_oss;
                                tooltip_oss.setf(std::ios::fixed);
                                tooltip_oss << std::setprecision(3);
                                tooltip_oss << "FPS = " << refresh_hz << " ÷ " << x << " = " << candidate_precise
                                            << " FPS\n\n";
                                tooltip_oss << "Creates a smooth frame rate that divides evenly\n";
                                tooltip_oss << "into the monitor's refresh rate.";
                                ImGui::SetTooltip("%s", tooltip_oss.str().c_str());
                            }
                        }
                    }
                }
            }
            // Add Gsync Cap button at the end
            if (!first) {
                ImGui::SameLine();
            }

            {
                // Gsync formula: refresh_hz - (refresh_hz * refresh_hz / 3600)
                double gsync_target = refresh_hz - (refresh_hz * refresh_hz / 3600.0);
                float precise_target = static_cast<float>(gsync_target);
                if (precise_target < 1.0f) precise_target = 1.0f;
                bool selected =
                    (std::fabs(settings::g_mainTabSettings.fps_limit.GetValue() - precise_target) <= selected_epsilon);

                if (selected) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.60f, 0.20f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.70f, 0.20f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.50f, 0.10f, 1.0f));
                }
                if (ImGui::Button("Gsync Cap")) {
                    double precise_target = gsync_target;  // do not round on apply
                    float target_fps = static_cast<float>(precise_target < 1.0 ? 1.0 : precise_target);
                    settings::g_mainTabSettings.fps_limit.SetValue(target_fps);
                }
                if (selected) {
                    ImGui::PopStyleColor(3);
                }
                // Add tooltip explaining the Gsync formula
                if (ImGui::IsItemHovered()) {
                    std::ostringstream tooltip_oss;
                    tooltip_oss.setf(std::ios::fixed);
                    tooltip_oss << std::setprecision(3);
                    tooltip_oss << "Gsync Cap: FPS = " << refresh_hz << " - (" << refresh_hz << "² / 3600)\n";
                    tooltip_oss << "= " << refresh_hz << " - " << (refresh_hz * refresh_hz / 3600.0) << " = "
                                << gsync_target << " FPS\n\n";
                    tooltip_oss << "Creates a ~0.3ms frame time buffer to optimize latency\n";
                    tooltip_oss << "and prevent tearing, similar to NVIDIA Reflex Low Latency Mode.";
                    ImGui::SetTooltip("%s", tooltip_oss.str().c_str());
                }
            }
        }
    }
}

void DrawDisplaySettings() {
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "=== Display Settings ===");
    {
        // Target Display dropdown
        // Use device ID-based approach for better reliability

        auto display_info = display_cache::g_displayCache.GetDisplayInfoForUI();
        std::vector<const char*> monitor_c_labels;
        monitor_c_labels.reserve(display_info.size());
        for (const auto& info : display_info) {
            monitor_c_labels.push_back(info.display_label.c_str());
        }

        // Find current selection by device ID
        std::string current_device_id = settings::g_mainTabSettings.selected_extended_display_device_id.GetValue();
        int selected_index = 0; // Default to first display
        for (size_t i = 0; i < display_info.size(); ++i) {
            if (display_info[i].extended_device_id == current_device_id) {
                selected_index = static_cast<int>(i);
                break;
            }
        }

        if (ImGui::Combo("Target Display", &selected_index, monitor_c_labels.data(),
                         static_cast<int>(monitor_c_labels.size()))) {
            if (selected_index >= 0 && selected_index < static_cast<int>(display_info.size())) {
                // Store the device ID
                std::string new_device_id = display_info[selected_index].extended_device_id;
                settings::g_mainTabSettings.selected_extended_display_device_id.SetValue(new_device_id);

                LogInfo("Target monitor changed to device ID: %s", new_device_id.c_str());
            }
        }
        if (ImGui::IsItemHovered()) {
            // Get the saved game window display device ID for tooltip
            std::string saved_device_id = settings::g_mainTabSettings.game_window_display_device_id.GetValue();
            std::string tooltip_text =
                "Choose which monitor to apply size/pos to. The monitor corresponding to the "
                "game window is automatically selected.";
            if (!saved_device_id.empty() && saved_device_id != "No Window" && saved_device_id != "No Monitor"
                && saved_device_id != "Monitor Info Failed") {
                tooltip_text += "\n\nGame window is on: " + saved_device_id;
            }
            ImGui::SetTooltip("%s", tooltip_text.c_str());
        }
    }

    // Window Mode dropdown (with persistent setting)
    if (ComboSettingEnumRefWrapper(settings::g_mainTabSettings.window_mode, "Window Mode")) {
        WindowMode old_mode = s_window_mode.load();
        s_window_mode = static_cast<WindowMode>(settings::g_mainTabSettings.window_mode.GetValue());

        // Don't apply changes immediately - let the normal window management system handle it
        // This prevents crashes when changing modes during gameplay

        std::ostringstream oss;
        oss << "Window mode changed from " << static_cast<int>(old_mode) << " to "
            << settings::g_mainTabSettings.window_mode.GetValue();
        LogInfo(oss.str().c_str());
    }
    // Auto-apply (continuous monitoring) checkbox next to Window Mode
    ImGui::SameLine();
    if (CheckboxSetting(settings::g_developerTabSettings.continuous_monitoring, "Auto-apply")) {
        s_continuous_monitoring_enabled.store(settings::g_developerTabSettings.continuous_monitoring.GetValue());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Auto-apply window mode changes and continuously keep window size/position in sync.");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Choose the window mode: Borderless Windowed with aspect ratio or Borderless Fullscreen.");
    }

    // Aspect Ratio dropdown (only shown in Aspect Ratio mode)
    if (s_window_mode.load() == WindowMode::kAspectRatio) {
        if (ComboSettingWrapper(settings::g_mainTabSettings.aspect_index, "Aspect Ratio")) {
            s_aspect_index = static_cast<AspectRatioType>(settings::g_mainTabSettings.aspect_index.GetValue());
            LogInfo("Aspect ratio changed");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Choose the aspect ratio for window resizing.");
        }
    }
    if (s_window_mode.load() == WindowMode::kAspectRatio) {
        // Width dropdown for aspect ratio mode
        if (ComboSettingRefWrapper(settings::g_mainTabSettings.window_aspect_width, "Window Width")) {
            s_aspect_width.store(settings::g_mainTabSettings.window_aspect_width.GetValue());
            LogInfo("Window width for aspect mode setting changed to: %d", s_aspect_width.load());
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Choose the width for the aspect ratio window. 'Display Width' uses the current monitor width.");
        }
    }

    // Window Alignment dropdown (only shown in Aspect Ratio mode)
    if (s_window_mode.load() == WindowMode::kAspectRatio) {
        if (ComboSettingWrapper(settings::g_mainTabSettings.alignment, "Alignment")) {
            s_window_alignment = static_cast<WindowAlignment>(settings::g_mainTabSettings.alignment.GetValue());
            LogInfo("Window alignment changed");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Choose how to align the window when repositioning is needed. 0=Center, 1=Top Left, "
                "2=Top Right, 3=Bottom Left, 4=Bottom Right.");
        }
    }
    // Background Black Curtain checkbox (only shown in Aspect Ratio mode)
    if (s_window_mode.load() == WindowMode::kAspectRatio) {
        if (CheckboxSetting(settings::g_mainTabSettings.background_feature, "Background Black Curtain")) {
            LogInfo("Background black curtain setting changed");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Creates a black background behind the game window when it doesn't cover the full screen.");
        }
    }

    // ADHD Multi-Monitor Mode controls
    DrawAdhdMultiMonitorControls(s_window_mode.load() == WindowMode::kAspectRatio);

    // Apply Changes button
    if (ImGui::Button("Apply Changes")) {
        // Force immediate application of window changes
        ::g_init_apply_generation.fetch_add(1);
        LogInfo("Apply Changes button clicked - forcing immediate window update");
        std::ostringstream oss;
        // All global settings on this tab are handled by the settings wrapper
        oss << "Apply Changes button clicked - forcing immediate window update";
        LogInfo(oss.str().c_str());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Apply the current window size and position settings immediately.");
    }

    ImGui::Spacing();

    // FPS Limiter Mode
    {
        if (ComboSettingWrapper(settings::g_mainTabSettings.fps_limiter_mode, "FPS Limiter Mode")) {
            s_fps_limiter_mode.store(
                static_cast<FpsLimiterMode>(settings::g_mainTabSettings.fps_limiter_mode.GetValue()));
            FpsLimiterMode mode = s_fps_limiter_mode.load();
            if (mode == FpsLimiterMode::kNone) {
                LogInfo("FPS Limiter: None (no limiting)");
            } else if (mode == FpsLimiterMode::kCustom) {
                LogInfo("FPS Limiter: Custom (Sleep/Spin) for VRR");
            } else if (mode == FpsLimiterMode::kLatentSync) {
                LogInfo("FPS Limiter: VBlank Scanline Sync for VSYNC-OFF or without VRR");
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Choose limiter: Custom sleep/spin or Latent Sync pacing");
        }
    }

    // FPS Limiter Injection Timing
    {
        int current_injection = settings::g_mainTabSettings.fps_limiter_injection.GetValue();
        int temp_injection = current_injection;
        if (ImGui::SliderInt("FPS Limiter Injection", &temp_injection, 0, 2, "%d")) {
            settings::g_mainTabSettings.fps_limiter_injection.SetValue(temp_injection);
            s_fps_limiter_injection.store(temp_injection);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Choose when to inject FPS limiter: 0=OnPresentFlags (recommended), "
                "1=OnPresentUpdateBefore2, 2=OnPresentUpdateBefore");
        }

        // Show current injection timing info
        const char* injection_labels[] = {"OnPresentFlags (Recommended)", "OnPresentUpdateBefore2",
                                          "OnPresentUpdateBefore"};
        if (temp_injection >= 0 && temp_injection < 3) {
            ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "Current: %s", injection_labels[temp_injection]);
        }
    }

    // Present Pacing Delay slider (persisted)
    {
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "Present Pacing Delay:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Improves frame timing consistency");

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                           "Adds a small delay after present to smooth frame pacing and reduce stuttering");

        float current_delay = settings::g_mainTabSettings.present_pacing_delay_percentage.GetValue();
        if (SliderFloatSetting(settings::g_mainTabSettings.present_pacing_delay_percentage,
                               "Present Pacing Delay (manual fine-tuning is needed for now)", "%.1f%%")) {
            // The setting is automatically synced via FloatSettingRef
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Present Pacing Delay: Adds delay to starting next frame.\n\n"
                "How it reduces latency:\n"
                "• Allow for more time for CPU to process input.\n"
                "• Lower values provide more consistent frame timing.\n"
                "• Higher values provide lower latency but slightly less consistent timing.\n"
                "Range: 0%% to 100%%. Default: 0%% (1 frame time delay).\n"
                "Manual fine-tuning required.");
        }
    }

    // Latent Sync Mode (only visible if Latent Sync limiter is selected)
    if (s_fps_limiter_mode.load() == FpsLimiterMode::kLatentSync) {
        // Scanline Offset (only visible if scanline mode is selected)
        int current_offset = settings::g_mainTabSettings.scanline_offset.GetValue();
        int temp_offset = current_offset;
        if (ImGui::SliderInt("Scanline Offset", &temp_offset, -1000, 1000, "%d")) {
            settings::g_mainTabSettings.scanline_offset.SetValue(temp_offset);
            s_scanline_offset.store(temp_offset);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Scanline offset for latent sync (-1000 to 1000). This defines the offset from the "
                "threshold where frame pacing is active.");
        }

        // VBlank Sync Divisor (only visible if latent sync mode is selected)
        int current_divisor = settings::g_mainTabSettings.vblank_sync_divisor.GetValue();
        int temp_divisor = current_divisor;
        if (ImGui::SliderInt("VBlank Sync Divisor", &temp_divisor, 0, 8, "%d")) {
            settings::g_mainTabSettings.vblank_sync_divisor.SetValue(temp_divisor);
            s_vblank_sync_divisor.store(temp_divisor);
        }
        if (ImGui::IsItemHovered()) {
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
                if (div == 1)
                    tooltip_oss << " (Full Refresh)";
                else if (div == 2)
                    tooltip_oss << " (Half Refresh)";
                else
                    tooltip_oss << " (1/" << div << " Refresh)";
                tooltip_oss << "\n";
            }
            tooltip_oss << "\n0 = Disabled, higher values reduce effective frame rate for smoother frame pacing.";
            ImGui::SetTooltip("%s", tooltip_oss.str().c_str());
        }

        // VBlank Monitor Status (only visible if latent sync is enabled and FPS limit > 0)
        if (s_fps_limiter_mode.load() == FpsLimiterMode::kLatentSync) {
            if (dxgi::latent_sync::g_latentSyncManager) {
                auto& latent = dxgi::latent_sync::g_latentSyncManager->GetLatentLimiter();
                if (latent.IsVBlankMonitoringActive()) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✁EVBlank Monitor: ACTIVE");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "VBlank monitoring thread is running and collecting scanline data for frame pacing.");
                    }

                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "  refresh time: %.3fms",
                                       1.0 * dxgi::fps_limiter::ns_per_refresh.load() / utils::NS_TO_MS);
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "  total_height: %llu",
                                       dxgi::fps_limiter::g_latent_sync_total_height.load());
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "  active_height: %llu",
                                       dxgi::fps_limiter::g_latent_sync_active_height.load());
                } else {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "⚠ VBlank Monitor: STARTING...");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "VBlank monitoring is enabled but the monitoring thread is still starting up.");
                    }
                }
            }
        }
    }

    ImGui::Spacing();

    // FPS Limit slider (persisted)

    bool fps_limit_enabled = s_fps_limiter_mode.load() != FpsLimiterMode::kNone || s_reflex_enable.load();


    {
        if (!fps_limit_enabled) {
            ImGui::BeginDisabled();
        }

        float current_value = settings::g_mainTabSettings.fps_limit.GetValue();
        const char* fmt = (current_value > 0.0f) ? "%.3f FPS" : "No Limit";
        if (SliderFloatSetting(settings::g_mainTabSettings.fps_limit, "FPS Limit", fmt)) {}

        if (!fps_limit_enabled) {
            ImGui::EndDisabled();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Set FPS limit for the game (0 = no limit). Now uses the new Custom FPS Limiter system.");
    }

    // No Render in Background checkbox
    {
        bool no_render_in_bg = settings::g_mainTabSettings.no_render_in_background.GetValue();
        if (ImGui::Checkbox("No Render in Background", &no_render_in_bg)) {
            settings::g_mainTabSettings.no_render_in_background.SetValue(no_render_in_bg);
            // The setting is automatically synced via BoolSettingRef
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Skip rendering draw calls when the game window is not in the foreground. This can save "
                "GPU power and reduce background processing.");
        }
    }

    // No Present in Background checkbox
    {
        bool no_present_in_bg = settings::g_mainTabSettings.no_present_in_background.GetValue();
        if (ImGui::Checkbox("No Present in Background", &no_present_in_bg)) {
            settings::g_mainTabSettings.no_present_in_background.SetValue(no_present_in_bg);
            // The setting is automatically synced via BoolSettingRef
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Skip ReShade's on_present processing when the game window is not in the foreground. "
                "This can save GPU power and reduce background processing.");
        }
    }

    // VSync & Tearing controls
    {
        if (!fps_limit_enabled) {
            ImGui::BeginDisabled();
        }
        DrawQuickResolutionChanger();

        if (!fps_limit_enabled) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "=== VSync & Tearing ===");

        bool vs_on = settings::g_mainTabSettings.force_vsync_on.GetValue();
        if (ImGui::Checkbox("Force VSync ON", &vs_on)) {
            s_restart_needed_vsync_tearing.store(true);
            // Mutual exclusion
            if (vs_on) {
                settings::g_mainTabSettings.force_vsync_off.SetValue(false);
                // s_force_vsync_off is automatically synced via BoolSettingRef
            }
            settings::g_mainTabSettings.force_vsync_on.SetValue(vs_on);
            // s_force_vsync_on is automatically synced via BoolSettingRef
            LogInfo(vs_on ? "Force VSync ON enabled" : "Force VSync ON disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Forces sync interval = 1 (requires restart).");
        }

        ImGui::SameLine();

        bool vs_off = settings::g_mainTabSettings.force_vsync_off.GetValue();
        if (ImGui::Checkbox("Force VSync OFF", &vs_off)) {
            s_restart_needed_vsync_tearing.store(true);
            // Mutual exclusion
            if (vs_off) {
                settings::g_mainTabSettings.force_vsync_on.SetValue(false);
            }
            settings::g_mainTabSettings.force_vsync_off.SetValue(vs_off);
            // s_force_vsync_off is automatically synced via BoolSettingRef
            LogInfo(vs_off ? "Force VSync OFF enabled" : "Force VSync OFF disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Forces sync interval = 0 (requires restart).");
        }

        ImGui::SameLine();

        bool prevent_t = settings::g_mainTabSettings.prevent_tearing.GetValue();
        if (ImGui::Checkbox("Prevent Tearing", &prevent_t)) {
            settings::g_mainTabSettings.prevent_tearing.SetValue(prevent_t);
            // s_prevent_tearing is automatically synced via BoolSettingRef
            LogInfo(prevent_t ? "Prevent Tearing enabled (tearing flags will be cleared)" : "Prevent Tearing disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Prevents tearing by clearing DXGI tearing flags and preferring sync.");
        }

        // Display restart-required notice if flagged
        if (s_restart_needed_vsync_tearing.load()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Game restart required to apply VSync/tearing changes.");
        }
    }

    // Background FPS Limit slider (persisted)
    {
        if (!fps_limit_enabled) {
            ImGui::BeginDisabled();
        }

        float current_bg = settings::g_mainTabSettings.fps_limit_background.GetValue();
        const char* fmt_bg = (current_bg > 0.0f) ? "%.0f FPS" : "No Limit";
        if (SliderFloatSetting(settings::g_mainTabSettings.fps_limit_background, "Background FPS Limit", fmt_bg)) {}

        if (!fps_limit_enabled) {
            ImGui::EndDisabled();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "FPS cap when the game window is not in the foreground. Now uses the new Custom FPS Limiter system.");
    }
}

void DrawAudioSettings() {
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Audio Settings ===");

    // Audio Volume slider
    float volume = s_audio_volume_percent.load();
    if (ImGui::SliderFloat("Audio Volume (%)", &volume, 0.0f, 100.0f, "%.0f%%")) {
        s_audio_volume_percent.store(volume);

        // Apply immediately only if Auto-apply is enabled
        if (settings::g_mainTabSettings.audio_volume_auto_apply.GetValue()) {
            if (::SetVolumeForCurrentProcess(volume)) {
                std::ostringstream oss;
                oss << "Audio volume changed to " << static_cast<int>(volume) << "%";
                LogInfo(oss.str().c_str());
            } else {
                std::ostringstream oss;
                oss << "Failed to set audio volume to " << static_cast<int>(volume) << "%";
                LogWarn(oss.str().c_str());
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Master audio volume control (0-100%%)");
    }
    // Auto-apply checkbox next to Audio Volume
    ImGui::SameLine();
    if (CheckboxSetting(settings::g_mainTabSettings.audio_volume_auto_apply, "Auto-apply##audio_volume")) {
        // No immediate action required; stored for consistency with other UI
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Auto-apply volume changes when adjusting the slider.");
    }

    // Audio Mute checkbox
    bool audio_mute = s_audio_mute.load();
    if (ImGui::Checkbox("Audio Mute", &audio_mute)) {
        settings::g_mainTabSettings.audio_mute.SetValue(audio_mute);

        // Apply mute/unmute immediately
        if (::SetMuteForCurrentProcess(audio_mute)) {
            ::g_muted_applied.store(audio_mute);
            std::ostringstream oss;
            oss << "Audio " << (audio_mute ? "muted" : "unmuted") << " successfully";
            LogInfo(oss.str().c_str());
        } else {
            std::ostringstream oss;
            oss << "Failed to " << (audio_mute ? "mute" : "unmute") << " audio";
            LogWarn(oss.str().c_str());
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Manually mute/unmute audio.");
    }

    // Mute in Background checkbox (disabled if Mute is ON)
    bool mute_in_bg = s_mute_in_background.load();
    if (s_audio_mute.load()) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Checkbox("Mute In Background", &mute_in_bg)) {
        settings::g_mainTabSettings.mute_in_background.SetValue(mute_in_bg);
        settings::g_mainTabSettings.mute_in_background_if_other_audio.SetValue(false);

        // Reset applied flag so the monitor thread reapplies desired state
        ::g_muted_applied.store(false);
        // Also apply the current mute state immediately if manual mute is off
        if (!s_audio_mute.load()) {
            HWND hwnd = g_last_swapchain_hwnd.load();
            // Use actual focus state for background audio (not spoofed)
            bool want_mute = (mute_in_bg && hwnd != nullptr && GetForegroundWindow() != hwnd);
            if (::SetMuteForCurrentProcess(want_mute)) {
                ::g_muted_applied.store(want_mute);
                std::ostringstream oss;
                oss << "Background mute " << (mute_in_bg ? "enabled" : "disabled");
                LogInfo(oss.str().c_str());
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Mute the game's audio when it is not the foreground window.");
    }
    if (s_audio_mute.load()) {
        ImGui::EndDisabled();
    }

    // Mute in Background only if other app plays audio
    bool mute_in_bg_if_other = s_mute_in_background_if_other_audio.load();
    if (s_audio_mute.load()) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Checkbox("Mute In Background (only if other app has audio)", &mute_in_bg_if_other)) {
        settings::g_mainTabSettings.mute_in_background_if_other_audio.SetValue(mute_in_bg_if_other);
        settings::g_mainTabSettings.mute_in_background.SetValue(false);
        ::g_muted_applied.store(false);
        if (!s_audio_mute.load()) {
            HWND hwnd = g_last_swapchain_hwnd.load();
            bool is_background = (hwnd != nullptr && GetForegroundWindow() != hwnd);
            bool want_mute = (mute_in_bg_if_other && is_background && ::IsOtherAppPlayingAudio());
            if (::SetMuteForCurrentProcess(want_mute)) {
                ::g_muted_applied.store(want_mute);
                std::ostringstream oss;
                oss << "Background mute (if other audio) " << (mute_in_bg_if_other ? "enabled" : "disabled");
                LogInfo(oss.str().c_str());
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Mute only if app is background AND another app outputs audio.");
    }
    if (s_audio_mute.load()) {
        ImGui::EndDisabled();
    }
}

void DrawWindowControls() {
    HWND hwnd = g_last_swapchain_hwnd.load();
    if (hwnd == nullptr) {
        LogWarn("Maximize Window: no window handle available");
        return;
    }

    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.8f, 1.0f), "=== Window Controls ===");

    // Window Control Buttons (Minimize, Restore, and Maximize side by side)
    ImGui::BeginGroup();

    // Minimize Window Button
    if (ImGui::Button("Minimize Window")) {
        HWND hwnd = g_last_swapchain_hwnd.load();
        std::thread([hwnd]() {
            LogDebug("Minimize Window button pressed (bg thread)");
            ShowWindow(hwnd, SW_MINIMIZE);
        }).detach();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Minimize the current game window.");
    }

    ImGui::SameLine();

    // Restore Window Button
    if (ImGui::Button("Restore Window")) {
        std::thread([hwnd]() {
            LogDebug("Restore Window button pressed (bg thread)");
            ShowWindow(hwnd, SW_RESTORE);
        }).detach();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Restore the minimized game window.");
    }

    ImGui::SameLine();

    // Maximize Window Button
    if (ImGui::Button("Maximize Window")) {
        std::thread([hwnd]() {
            LogDebug("Maximize Window button pressed (bg thread)");

            // Switch to fullscreen mode to maximize the window
            s_window_mode.store(WindowMode::kFullscreen);
            settings::g_mainTabSettings.window_mode.SetValue(static_cast<int>(WindowMode::kFullscreen));

            // Apply the window changes using the existing UI system
            ApplyWindowChange(hwnd, "maximize_button");

            LogInfo("Window maximized to current monitor size");
        }).detach();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Maximize the window to fill the current monitor.");
    }

    ImGui::EndGroup();
}

void DrawMonitorDisplaySettings() {
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Monitor & Display Resolution ===");

    // Resolution Widget
    if (ImGui::CollapsingHeader("Resolution Control", ImGuiTreeNodeFlags_DefaultOpen)) {
        display_commander::widgets::resolution_widget::DrawResolutionWidget();
    }
}

void DrawImportantInfo() {
    ImGui::Spacing();
    ImGui::Separator();

    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.8f, 1.0f), "=== Important Information ===");

    // Test Overlay Control
    {
        bool show_test_overlay = settings::g_mainTabSettings.show_test_overlay.GetValue();
        if (ImGui::Checkbox("Show Test Overlay (reshade_overlay demo)", &show_test_overlay)) {
            settings::g_mainTabSettings.show_test_overlay.SetValue(show_test_overlay);
            LogInfo("Test overlay %s", show_test_overlay ? "enabled" : "disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Shows a test widget using the reshade_overlay event to demonstrate the difference "
                "between register_overlay and reshade_overlay approaches.");
        }
    }

    ImGui::Spacing();

    // FPS Counter with 1% Low and 0.1% Low over past 60s (computed in background)
    {
        std::string local_text;
        auto shared_text = ::g_perf_text_shared.load();
        if (shared_text) {
            local_text = *shared_text;
        }
        ImGui::TextUnformatted(local_text.c_str());
        if (ImGui::Button("Reset Stats")) {
            ::g_perf_reset_requested.store(true, std::memory_order_release);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Reset FPS/frametime statistics. Metrics are computed since reset.");
        }
    }
    std::ostringstream oss;

    // Present Duration Display
    oss.str("");
    oss.clear();
    oss << "Present Duration: " << std::fixed << std::setprecision(3)
        << (1.0 * ::g_present_duration_ns.load() / utils::NS_TO_MS) << " ms";
    ImGui::TextUnformatted(oss.str().c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "(smoothed)");

    oss.str("");
    oss.clear();
    oss << "Simulation Duration: " << std::fixed << std::setprecision(3)
        << (1.0 * ::g_simulation_duration_ns.load() / utils::NS_TO_MS) << " ms";
    ImGui::TextUnformatted(oss.str().c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "(smoothed)");

    // Reshade Overhead Display
    oss.str("");
    oss.clear();
    oss << "Render Submit Duration: " << std::fixed << std::setprecision(3)
        << (1.0 * ::g_render_submit_duration_ns.load() / utils::NS_TO_MS) << " ms";
    ImGui::TextUnformatted(oss.str().c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "(smoothed)");

    // Reshade Overhead Display
    oss.str("");
    oss.clear();
    oss << "Reshade Overhead Duration: " << std::fixed << std::setprecision(3)
        << ((1.0 * ::g_reshade_overhead_duration_ns.load() - ::fps_sleep_before_on_present_ns.load()
             - ::fps_sleep_after_on_present_ns.load())
            / utils::NS_TO_MS)
        << " ms";
    ImGui::TextUnformatted(oss.str().c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "(smoothed)");

    oss.str("");
    oss.clear();
    oss << "FPS Limiter Sleep Duration (before onPresent): " << std::fixed << std::setprecision(3)
        << (1.0 * ::fps_sleep_before_on_present_ns.load() / utils::NS_TO_MS) << " ms";
    ImGui::TextUnformatted(oss.str().c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "(smoothed)");

    // FPS Limiter Start Duration Display
    oss.str("");
    oss.clear();
    oss << "FPS Limiter Sleep Duration (after onPresent): " << std::fixed << std::setprecision(3)
        << (1.0 * ::fps_sleep_after_on_present_ns.load() / utils::NS_TO_MS) << " ms";
    ImGui::TextUnformatted(oss.str().c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "(smoothed)");

    // Simulation Start to Present Latency Display
    oss.str("");
    oss.clear();
    // Calculate latency: frame_time - sleep duration after onPresent
    float current_fps = 0.0f;
    const uint32_t head = ::g_perf_ring_head.load(std::memory_order_acquire);
    if (head > 0) {
        const uint32_t last_idx = (head - 1) & (::kPerfRingCapacity - 1);
        const ::PerfSample& last_sample = ::g_perf_ring[last_idx];
        current_fps = last_sample.fps;
    }

    if (current_fps > 0.0f) {
        float frame_time_ms = 1000.0f / current_fps;
        float sleep_duration_ms = static_cast<float>(::fps_sleep_after_on_present_ns.load()) / utils::NS_TO_MS;
        float latency_ms = frame_time_ms - sleep_duration_ms;

        static double sim_start_to_present_latency_ms = 0.0;
        sim_start_to_present_latency_ms = (sim_start_to_present_latency_ms * 0.99 + latency_ms * 0.01);
        oss << "Sim Start to Present Latency: " << std::fixed << std::setprecision(3) << sim_start_to_present_latency_ms
            << " ms";
        ImGui::TextUnformatted(oss.str().c_str());
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "(frame_time - sleep_duration)");
    }

    // Flip State Display (renamed from DXGI Composition)
    const char* flip_state_str = "Unknown";
    int flip_state_case = static_cast<int>(::s_dxgi_composition_state);
    switch (flip_state_case) {
        case 1:  flip_state_str = "Composed Flip"; break;
        case 2:  flip_state_str = "MPO Independent Flip"; break;
        case 3:  flip_state_str = "Legacy Independent Flip"; break;
        default: flip_state_str = "Unknown"; break;
    }

    oss.str("");
    oss.clear();
    oss << "Flip State: " << flip_state_str;

    // Color code based on flip state
    if (flip_state_case == 1) {
        // Composed Flip - Red
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.8f, 1.0f), "%s", oss.str().c_str());
    } else if (flip_state_case == 2 || flip_state_case == 3) {
        // Independent Flip modes - Green
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "%s", oss.str().c_str());
    } else {
        // Unknown - Yellow
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.8f, 1.0f), "%s", oss.str().c_str());
    }
}

void DrawAdhdMultiMonitorControls(bool hasBlackCurtainSetting) {
    // Check if multiple monitors are available
    bool hasMultipleMonitors = adhd_multi_monitor::api::HasMultipleMonitors();

    if (!hasMultipleMonitors) {
        return;
    }
    if (hasBlackCurtainSetting) ImGui::SameLine();

    // Main ADHD mode checkbox
    bool adhdEnabled = settings::g_mainTabSettings.adhd_multi_monitor_enabled.GetValue();
    if (ImGui::Checkbox("ADHD Multi-Monitor Mode", &adhdEnabled)) {
        settings::g_mainTabSettings.adhd_multi_monitor_enabled.SetValue(adhdEnabled);
        adhd_multi_monitor::api::SetEnabled(adhdEnabled);
        LogInfo("ADHD Multi-Monitor Mode %s", adhdEnabled ? "enabled" : "disabled");
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Covers secondary monitors with a black window to reduce distractions while playing this game.");
    }

    // Focus disengagement is always enabled (no UI control needed)
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "• Automatically disengages on Alt-Tab");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "ADHD mode will automatically disengage whenever you Alt-Tab, regardless of which monitor "
            "the new application is on.");
    }

    // Additional information
    ImGui::Spacing();
    ImGui::TextColored(
        ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
        "This feature helps reduce distractions by covering secondary monitors with a black background.");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Similar to Special-K's ADHD Multi-Monitor Mode.\nThe black background window will automatically position "
            "itself to cover all monitors except the one where your game is running.");
    }
}

}  // namespace ui::new_ui
