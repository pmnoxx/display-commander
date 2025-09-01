#include "main_new_tab.hpp"
#include <atomic>
#include "globals.hpp"
#include "main_new_tab_settings.hpp"
#include "developer_new_tab_settings.hpp"
#include "../../addon.hpp"
#include "../../audio/audio_management.hpp"
#include <minwindef.h>
#include <sstream>
#include <thread>
#include <atomic>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include "../ui_display_tab.hpp"
#include <deps/imgui/imgui.h>
#include "../../latent_sync/latent_sync_limiter.hpp"
#include "../../utils/timing.hpp"


namespace ui::new_ui {

// Flag to indicate a restart is required after changing VSync/tearing options
static std::atomic<bool> s_restart_needed_vsync_tearing{false};

void InitMainNewTab() {

    static bool settings_loaded_once = false;
    if (!settings_loaded_once) {
        // Ensure developer settings (including continuous monitoring) are loaded so UI reflects saved state
        g_developerTabSettings.LoadAll();
        g_main_new_tab_settings.LoadSettings();
        s_window_mode = g_main_new_tab_settings.window_mode.GetValue();
        {
            int idx = g_main_new_tab_settings.window_width.GetValue();
            idx = (std::max)(idx, 0);
            int max_idx = 7;
            idx = (std::min)(idx, max_idx);
            s_windowed_width.store((idx == 0) ? GetCurrentMonitorWidth()
                                          : WIDTH_OPTIONS[idx]);
        }
        {
            int idx = g_main_new_tab_settings.window_height.GetValue();
            idx = (std::max)(idx, 0);
            int max_idx = 7;
            idx = (std::min)(idx, max_idx);
            s_windowed_height.store((idx == 0) ? GetCurrentMonitorHeight()
                                           : HEIGHT_OPTIONS[idx]);
        }
        s_aspect_index = g_main_new_tab_settings.aspect_index.GetValue();
        s_target_monitor_index.store(g_main_new_tab_settings.target_monitor_index.GetValue());
        s_background_feature_enabled.store(g_main_new_tab_settings.background_feature.GetValue());
        s_move_to_zero_if_out = g_main_new_tab_settings.alignment.GetValue();
        // FPS limits are now automatically synced via FloatSettingRef
        s_audio_volume_percent.store(g_main_new_tab_settings.audio_volume_percent.GetValue());
        s_audio_mute.store(g_main_new_tab_settings.audio_mute.GetValue());
        s_mute_in_background.store(g_main_new_tab_settings.mute_in_background.GetValue());
        s_mute_in_background_if_other_audio.store(g_main_new_tab_settings.mute_in_background_if_other_audio.GetValue());
        s_no_present_in_background.store(g_main_new_tab_settings.no_present_in_background.GetValue());
        // VSync & Tearing - all automatically synced via BoolSettingRef
        s_block_input_in_background.store(g_main_new_tab_settings.block_input_in_background.GetValue());
        settings_loaded_once = true;

        // FPS limiter mode
        s_fps_limiter_mode.store(g_main_new_tab_settings.fps_limiter_mode.GetValue());
        // FPS limiter injection timing
        s_fps_limiter_injection.store(g_main_new_tab_settings.fps_limiter_injection.GetValue());
        // Scanline offset
        s_scanline_offset.store(g_main_new_tab_settings.scanline_offset.GetValue());
        // VBlank Sync Divisor
        s_vblank_sync_divisor.store(g_main_new_tab_settings.vblank_sync_divisor.GetValue());

        // If manual Audio Mute is OFF, proactively unmute on startup
        if (!s_audio_mute.load()) {
            if (::SetMuteForCurrentProcess(false)) {
                ::g_muted_applied.store(false);
                LogInfo("Audio unmuted at startup (Audio Mute is OFF)");
            }
        }
    }
}

void DrawMainNewTab() {
    // Load saved settings once and sync legacy globals
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
        bool block_any_in_background =
            g_main_new_tab_settings.block_input_in_background.GetValue();
        if (ImGui::Checkbox("Block Input in Background", &block_any_in_background)) {
            g_main_new_tab_settings.block_input_in_background.SetValue(block_any_in_background);
            s_block_input_in_background.store(block_any_in_background);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Blocks mouse, keyboard, and cursor warping while the game window is not focused.");
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
                bool selected = (std::fabs(g_main_new_tab_settings.fps_limit.GetValue() - 0.0f) <= selected_epsilon);
                if (selected) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.60f, 0.20f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.70f, 0.20f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.50f, 0.10f, 1.0f));
                }
                if (ImGui::Button("No Limit")) {
                    g_main_new_tab_settings.fps_limit.SetValue(0.0f);
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
                            bool selected = (std::fabs(g_main_new_tab_settings.fps_limit.GetValue() - candidate_precise) <= selected_epsilon);
                            if (selected) {
                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.60f, 0.20f, 1.0f));
                                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.70f, 0.20f, 1.0f));
                                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.50f, 0.10f, 1.0f));
                            }
                            if (ImGui::Button(label.c_str())) {
                                float target_fps = candidate_precise;
                                g_main_new_tab_settings.fps_limit.SetValue(target_fps);
                            }
                            if (selected) {
                                ImGui::PopStyleColor(3);
                            }
                            // Add tooltip showing the precise calculation
                            if (ImGui::IsItemHovered()) {
                                std::ostringstream tooltip_oss;
                                tooltip_oss.setf(std::ios::fixed);
                                tooltip_oss << std::setprecision(3);
                                tooltip_oss << "FPS = " << refresh_hz << " ÷ " << x << " = " << candidate_precise << " FPS\n\n";
                                tooltip_oss << "Creates a smooth frame rate that divides evenly\n";
                                tooltip_oss << "into the monitor's refresh rate.";
                                ImGui::SetTooltip("%s", tooltip_oss.str().c_str());
                            }
                        }
                    }
                }
            }
            // Add Gsync Cap button at the end
            if (!first) ImGui::SameLine();
            {
                // Gsync formula: refresh_hz - (refresh_hz * refresh_hz / 3600)
                double gsync_target = refresh_hz - (refresh_hz * refresh_hz / 3600.0);
                float precise_target = static_cast<float>(gsync_target);
                if (precise_target < 1.0f) precise_target = 1.0f;
                bool selected = (std::fabs(g_main_new_tab_settings.fps_limit.GetValue() - precise_target) <= selected_epsilon);

                if (selected) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.60f, 0.20f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.70f, 0.20f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.50f, 0.10f, 1.0f));
                }
                if (ImGui::Button("Gsync Cap")) {
                    double precise_target = gsync_target; // do not round on apply
                    float target_fps = static_cast<float>(precise_target < 1.0 ? 1.0 : precise_target);
                    g_main_new_tab_settings.fps_limit.SetValue(target_fps);
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
                    tooltip_oss << "= " << refresh_hz << " - " << (refresh_hz * refresh_hz / 3600.0) << " = " << gsync_target << " FPS\n\n";
                    tooltip_oss << "Creates a ~0.3ms frame time buffer to optimize latency\n";
                    tooltip_oss << "and prevent tearing, similar to NVIDIA Reflex Low Latency Mode.";
                    ImGui::SetTooltip("%s", tooltip_oss.str().c_str());
                }
            }

        }
    }
}


void DrawDisplaySettings() {
    int current_monitor_width = GetCurrentMonitorWidth();
    int current_monitor_height = GetCurrentMonitorHeight();
    
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "=== Display Settings ===");
    
    // Window Mode dropdown (with persistent setting)
    if (ComboSettingWrapper(g_main_new_tab_settings.window_mode, "Window Mode")) {
        int old_mode = static_cast<int>(s_window_mode);
        s_window_mode = static_cast<float>(g_main_new_tab_settings.window_mode.GetValue());
        
        // Don't apply changes immediately - let the normal window management system handle it
        // This prevents crashes when changing modes during gameplay
        
        std::ostringstream oss;
        oss << "Window mode changed from " << old_mode << " to " << g_main_new_tab_settings.window_mode.GetValue();
        LogInfo(oss.str().c_str());
    }
    // Auto-apply (continuous monitoring) checkbox next to Window Mode
    ImGui::SameLine();
    if (CheckboxSetting(g_developerTabSettings.continuous_monitoring, "Auto-apply")) {
        s_continuous_monitoring_enabled.store(g_developerTabSettings.continuous_monitoring.GetValue());
        if (g_developerTabSettings.continuous_monitoring.GetValue()) {
            ::StartContinuousMonitoring();
        } else {
            ::StopContinuousMonitoring();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Auto-apply window mode changes and continuously keep window size/position in sync.");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Choose the window mode: Borderless Windowed with aspect ratio, Borderless Windowed with custom width/height, or Borderless Fullscreen.");
    }
    
    // Show mode-specific information
    if (s_window_mode < 0.5f) {
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "Mode: Aspect Ratio - Set width below, height calculated automatically");
    } else if (s_window_mode < 1.5f) {
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "Mode: Width/Height - Set custom window dimensions below");
    } else {
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "Mode: Fullscreen - Window will fill entire monitor (no alignment needed)");
    }
    
    // Window Width dropdown (shown in both Aspect Ratio and Width/Height modes)
    if (s_window_mode < 1.5f) {
        if (ComboSettingWrapper(g_main_new_tab_settings.window_width, "Window Width")) {
            int idx = g_main_new_tab_settings.window_width.GetValue();
            idx = (std::max)(idx, 0);
            int max_idx = 7;
            idx = (std::min)(idx, max_idx);
            s_windowed_width = (idx == 0) ? static_cast<float>(current_monitor_width)
                                          : static_cast<float>(WIDTH_OPTIONS[idx]);
            LogInfo("Window width changed");
        }
        if (ImGui::IsItemHovered()) {
            if (s_window_mode < 0.5f) {
                ImGui::SetTooltip("Choose the window width. Height will be calculated automatically based on the selected aspect ratio.");
            } else {
                ImGui::SetTooltip("Choose the window width. 'Current Display' uses the current monitor's width.");
            }
        }
    }
    
    // Window Height dropdown (only shown in Width/Height mode)
    if (s_window_mode >= 0.5f && s_window_mode < 1.5f) {
        if (ComboSettingWrapper(g_main_new_tab_settings.window_height, "Window Height")) {
            int idx = g_main_new_tab_settings.window_height.GetValue();
            idx = (std::max)(idx, 0);
            int max_idx = 7;
            idx = (std::min)(idx, max_idx);
            s_windowed_height = (idx == 0) ? static_cast<float>(current_monitor_height)
                                           : static_cast<float>(HEIGHT_OPTIONS[idx]);
            LogInfo("Window height changed");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Choose the window height. 'Current Display' uses the current monitor's height.");
        }
    }
    
    // Aspect Ratio dropdown (only shown in Aspect Ratio mode)
    if (s_window_mode < 0.5f) {
        if (ComboSettingWrapper(g_main_new_tab_settings.aspect_index, "Aspect Ratio")) {
            s_aspect_index = static_cast<float>(g_main_new_tab_settings.aspect_index.GetValue());
            LogInfo("Aspect ratio changed");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Choose the aspect ratio for window resizing.");
        }
    }
    
    // Target Monitor dropdown
    // Use cached monitor labels updated by continuous monitoring thread

    std::vector<std::string> monitor_labels_local;
    std::vector<const char*> monitor_c_labels;
    {
        auto ptr = ::g_monitor_labels.load(std::memory_order_acquire);
        monitor_labels_local = *ptr; // copy to avoid lifetime issues
        monitor_c_labels.reserve(monitor_labels_local.size());
        for (const auto& label : monitor_labels_local) {
            monitor_c_labels.push_back(label.c_str());
        }
    }
    int monitor_index = s_target_monitor_index.load();
    if (ImGui::Combo("Target Monitor", &monitor_index, monitor_c_labels.data(), static_cast<int>(monitor_c_labels.size()))) {
        s_target_monitor_index.store(monitor_index);
        g_main_new_tab_settings.target_monitor_index.SetValue(monitor_index);
        LogInfo("Target monitor changed");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Choose which monitor to apply size/pos to. 'Auto' uses the current window monitor.");
    }
    
    // Background Black Curtain checkbox (only shown in Borderless Windowed modes)
    if (s_window_mode < 1.5f) {
        if (CheckboxSetting(g_main_new_tab_settings.background_feature, "Background Black Curtain")) {
            s_background_feature_enabled.store(g_main_new_tab_settings.background_feature.GetValue());
            LogInfo("Background black curtain setting changed");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Creates a black background behind the game window when it doesn't cover the full screen.");
        }
    }
    
    // Window Alignment dropdown (only shown in Borderless Windowed modes)
    if (s_window_mode < 1.5f) {
        if (ComboSettingWrapper(g_main_new_tab_settings.alignment, "Alignment")) {
            s_move_to_zero_if_out = static_cast<float>(g_main_new_tab_settings.alignment.GetValue());
            LogInfo("Window alignment changed");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Choose how to align the window when repositioning is needed. 1=Top Left, 2=Top Right, 3=Bottom Left, 4=Bottom Right, 5=Center.");
        }
    }
    
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
        if (ComboSettingWrapper(g_main_new_tab_settings.fps_limiter_mode, "FPS Limiter Mode")) {
            s_fps_limiter_mode.store(g_main_new_tab_settings.fps_limiter_mode.GetValue());
            LogInfo(s_fps_limiter_mode.load() == 0 ? "Custom (Sleep/Spin) for VRR" : "VBlank Scanline Sync for VSYNC-OFF or without VRR");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Choose limiter: Custom sleep/spin or Latent Sync pacing");
        }
        
        // Warning for VBlank Scanline Sync mode when required settings are not enabled
        if (g_main_new_tab_settings.fps_limiter_mode.GetValue() == 1) {
            bool force_vsync_off_enabled = g_main_new_tab_settings.force_vsync_off.GetValue();
            
            if (!force_vsync_off_enabled) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "WARNING: VBlank Scanline Sync only works with VSYNC-OFF");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("VBlank Scanline Sync mode requires VSync to be disabled for optimal frame pacing.");
                }
            }
        }
    }

    // FPS Limiter Injection Timing
    {
        int current_injection = g_main_new_tab_settings.fps_limiter_injection.GetValue();
        int temp_injection = current_injection;
        if (ImGui::SliderInt("FPS Limiter Injection", &temp_injection, 0, 2, "%d")) {
            g_main_new_tab_settings.fps_limiter_injection.SetValue(temp_injection);
            s_fps_limiter_injection.store(temp_injection);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Choose when to inject FPS limiter: 0=OnPresentFlags (recommended), 1=OnPresentUpdateBefore2, 2=OnPresentUpdateBefore");
        }
        
        // Show current injection timing info
        const char* injection_labels[] = {"OnPresentFlags (Recommended)", "OnPresentUpdateBefore2", "OnPresentUpdateBefore"};
        if (temp_injection >= 0 && temp_injection < 3) {
            ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "Current: %s", injection_labels[temp_injection]);
        }
    }

    // Latent Sync Mode (only visible if Latent Sync limiter is selected)
    if (s_fps_limiter_mode.load() == 1) {
        
        // Get current monitor height for dynamic range
        int monitor_height = current_monitor_height;
        int monitor_width = current_monitor_width;
        
        // Update display dimensions in GlobalWindowState
        auto current_state = g_window_state.load();
        if (current_state) {
            auto new_state = std::make_shared<GlobalWindowState>(*current_state);
            new_state->display_width = monitor_width;
            new_state->display_height = monitor_height;
            g_window_state.store(new_state);
        }
        
        // Scanline Offset (only visible if scanline mode is selected)
        int current_offset = g_main_new_tab_settings.scanline_offset.GetValue();
        int temp_offset = current_offset;
        if (ImGui::SliderInt("Scanline Offset", &temp_offset, -1000, 1000, "%d")) {
            g_main_new_tab_settings.scanline_offset.SetValue(temp_offset);
            s_scanline_offset.store(temp_offset);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Scanline offset for latent sync (-1000 to 1000). This defines the offset from the threshold where frame pacing is active.");
        }
        
        // VBlank Sync Divisor (only visible if latent sync mode is selected)
        int current_divisor = g_main_new_tab_settings.vblank_sync_divisor.GetValue();
        int temp_divisor = current_divisor;
        if (ImGui::SliderInt("VBlank Sync Divisor", &temp_divisor, 0, 8, "%d")) {
            g_main_new_tab_settings.vblank_sync_divisor.SetValue(temp_divisor);
            s_vblank_sync_divisor.store(temp_divisor);
        }
        if (ImGui::IsItemHovered()) {
            // Calculate effective refresh rate based on monitor info
            auto window_state = ::g_window_state.load();
            double refresh_hz = 60.0; // default fallback
            if (window_state) {
                refresh_hz = window_state->current_monitor_refresh_rate.ToHz();
            }
            
            std::ostringstream tooltip_oss;
            tooltip_oss << "VBlank Sync Divisor (0-8). Controls frame pacing similar to VSync divisors:\n\n";
            tooltip_oss << "  0 -> No additional wait (Off)\n";
            for (int div = 1; div <= 8; ++div) {
                int effective_fps = static_cast<int>(std::round(refresh_hz / div));
                tooltip_oss << "  " << div << " -> " << effective_fps << " FPS";
                if (div == 1) tooltip_oss << " (Full Refresh)";
                else if (div == 2) tooltip_oss << " (Half Refresh)";
                else tooltip_oss << " (1/" << div << " Refresh)";
                tooltip_oss << "\n";
            }
            tooltip_oss << "\n0 = Disabled, higher values reduce effective frame rate for smoother frame pacing.";
            ImGui::SetTooltip("%s", tooltip_oss.str().c_str());
        }
        
        // VBlank Monitor Status (only visible if latent sync is enabled and FPS limit > 0)
        if (s_fps_limiter_mode.load() == FPS_LIMITER_MODE_LATENT_SYNC) {
            if (dxgi::latent_sync::g_latentSyncManager) {
                auto& latent = dxgi::latent_sync::g_latentSyncManager->GetLatentLimiter();
                if (latent.IsVBlankMonitoringActive()) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ VBlank Monitor: ACTIVE");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("VBlank monitoring thread is running and collecting scanline data for frame pacing.");
                    }
                    
                    // Show VBlank statistics if available
                    uint64_t vblank_count = latent.GetVBlankCount();
                    uint64_t state_change_count = latent.GetStateChangeCount();
                    double vblank_percentage = latent.GetVBlankPercentage();
                    
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "  VBlank Events: %llu", vblank_count);
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "  State Changes: %llu", state_change_count);
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "  VBlank Time: %.1f%%", vblank_percentage);
                } else {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "⚠ VBlank Monitor: STARTING...");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("VBlank monitoring is enabled but the monitoring thread is still starting up.");
                    }
                }
            }
        }
    }

    ImGui::Spacing();

    // FPS Limit slider (persisted)
    {
        float current_value = g_main_new_tab_settings.fps_limit.GetValue();
        const char* fmt = (current_value > 0.0f) ? "%.3f FPS" : "No Limit";
        if (SliderFloatSetting(g_main_new_tab_settings.fps_limit, "FPS Limit", fmt)) {
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Set FPS limit for the game (0 = no limit). Now uses the new Custom FPS Limiter system.");
    }
    
    // No Render in Background checkbox
    {
        bool no_render_in_bg = g_main_new_tab_settings.no_render_in_background.GetValue();
        if (ImGui::Checkbox("No Render in Background", &no_render_in_bg)) {
            g_main_new_tab_settings.no_render_in_background.SetValue(no_render_in_bg);
            // The setting is automatically synced via BoolSettingRef
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Skip rendering draw calls when the game window is not in the foreground. This can save GPU power and reduce background processing.");
        }
    }
    
    // No Present in Background checkbox
    {
        bool no_present_in_bg = g_main_new_tab_settings.no_present_in_background.GetValue();
        if (ImGui::Checkbox("No Present in Background", &no_present_in_bg)) {
            g_main_new_tab_settings.no_present_in_background.SetValue(no_present_in_bg);
            // The setting is automatically synced via BoolSettingRef
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Skip ReShade's on_present processing when the game window is not in the foreground. This can save GPU power and reduce background processing.");
        }
    }
    
    // VSync & Tearing controls
    {
        DrawQuickResolutionChanger();

        ImGui::SameLine();
        
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "=== VSync & Tearing ===");

        bool vs_on = g_main_new_tab_settings.force_vsync_on.GetValue();
        if (ImGui::Checkbox("Force VSync ON", &vs_on)) {
            s_restart_needed_vsync_tearing.store(true);
            // Mutual exclusion
            if (vs_on) {
                g_main_new_tab_settings.force_vsync_off.SetValue(false);
                // s_force_vsync_off is automatically synced via BoolSettingRef
            }
            g_main_new_tab_settings.force_vsync_on.SetValue(vs_on);
            // s_force_vsync_on is automatically synced via BoolSettingRef
            LogInfo(vs_on ? "Force VSync ON enabled" : "Force VSync ON disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Forces sync interval = 1 (requires restart).");
        }

        ImGui::SameLine();

        bool vs_off = g_main_new_tab_settings.force_vsync_off.GetValue();
        if (ImGui::Checkbox("Force VSync OFF", &vs_off)) {
            s_restart_needed_vsync_tearing.store(true);
            // Mutual exclusion
            if (vs_off) {
                g_main_new_tab_settings.force_vsync_on.SetValue(false);
            }
            g_main_new_tab_settings.force_vsync_off.SetValue(vs_off);
            // s_force_vsync_off is automatically synced via BoolSettingRef
            LogInfo(vs_off ? "Force VSync OFF enabled" : "Force VSync OFF disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Forces sync interval = 0 (requires restart).");
        }

        ImGui::SameLine();

        bool prevent_t = g_main_new_tab_settings.prevent_tearing.GetValue();
        if (ImGui::Checkbox("Prevent Tearing", &prevent_t)) {
            g_main_new_tab_settings.prevent_tearing.SetValue(prevent_t);
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
        float current_bg = g_main_new_tab_settings.fps_limit_background.GetValue();
        const char* fmt_bg = (current_bg > 0.0f) ? "%.0f FPS" : "No Limit";
        if (SliderFloatSetting(g_main_new_tab_settings.fps_limit_background, "Background FPS Limit", fmt_bg)) {

        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("FPS cap when the game window is not in the foreground. Now uses the new Custom FPS Limiter system.");
    }
}

void DrawAudioSettings() {
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Audio Settings ===");
    
    // Audio Volume slider
    float volume = s_audio_volume_percent.load();
    if (ImGui::SliderFloat("Audio Volume (%)", &volume, 0.0f, 100.0f, "%.0f%%")) {
        s_audio_volume_percent.store(volume);
        
        // Apply immediately only if Auto-apply is enabled
        if (g_main_new_tab_settings.audio_volume_auto_apply.GetValue()) {
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
    if (CheckboxSetting(g_main_new_tab_settings.audio_volume_auto_apply, "Auto-apply##audio_volume")) {
        // No immediate action required; stored for consistency with other UI
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Auto-apply volume changes when adjusting the slider.");
    }
    
    // Audio Mute checkbox
    bool audio_mute = s_audio_mute.load();
    if (ImGui::Checkbox("Audio Mute", &audio_mute)) {
        g_main_new_tab_settings.audio_mute.SetValue(audio_mute);
        
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
        g_main_new_tab_settings.mute_in_background.SetValue(mute_in_bg);
        g_main_new_tab_settings.mute_in_background_if_other_audio.SetValue(false);
        
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
        g_main_new_tab_settings.mute_in_background_if_other_audio.SetValue(mute_in_bg_if_other);
        g_main_new_tab_settings.mute_in_background.SetValue(false);
        ::g_muted_applied.store(false);
        if (!s_audio_mute.load()) {
            HWND hwnd = g_last_swapchain_hwnd.load();
            bool is_background = (hwnd != nullptr && GetForegroundWindow() != hwnd);
            bool want_mute = (mute_in_bg_if_other && is_background && ::IsOtherAppPlayingAudio());
            if (::SetMuteForCurrentProcess(want_mute)) {
                ::g_muted_applied.store(want_mute);
                std::ostringstream oss; oss << "Background mute (if other audio) " << (mute_in_bg_if_other ? "enabled" : "disabled");
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
    int current_monitor_width = GetCurrentMonitorWidth();
    int current_monitor_height = GetCurrentMonitorHeight();
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
        std::thread([hwnd](){
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
        std::thread([hwnd](){
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
        std::thread([hwnd, current_monitor_width, current_monitor_height](){
            LogDebug("Maximize Window button pressed (bg thread)");
            
            // Set window dimensions to current monitor size
            s_windowed_width.store(static_cast<float>(current_monitor_width));
            s_windowed_height.store(static_cast<float>(current_monitor_height));
            
            // Update the settings to reflect the change
            g_main_new_tab_settings.window_width.SetValue(0); // 0 = Current Display
            g_main_new_tab_settings.window_height.SetValue(0); // 0 = Current Display
            
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
    
    // Dynamic Monitor Settings
    if (ImGui::CollapsingHeader("Dynamic Monitor Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        HandleMonitorSettingsUI();
    }
    
    ImGui::Spacing();
    
    // Current Display Info
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Current Display Info:");
    std::string display_info = GetCurrentDisplayInfo();
    ImGui::TextWrapped("%s", display_info.c_str());
}

void DrawImportantInfo() {
    ImGui::Spacing();
    ImGui::Separator();
    
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.8f, 1.0f), "=== Important Information ===");
    
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
    oss << "Present Duration: " << std::fixed << std::setprecision(3) << (1.0 *::g_present_duration_ns.load() / utils::NS_TO_MS) << " ms";
    ImGui::TextUnformatted(oss.str().c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "(smoothed)");
    
    
    oss.str("");
    oss.clear();
    oss << "Simulation Duration: " << std::fixed << std::setprecision(3) << (1.0 *::g_simulation_duration_ns.load() / utils::NS_TO_MS) << " ms";
    ImGui::TextUnformatted(oss.str().c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "(smoothed)");   
    
    // Reshade Overhead Display
    oss.str("");
    oss.clear();
    oss << "Render Submit Duration: " << std::fixed << std::setprecision(3) << (1.0 *::g_render_submit_duration_ns.load() / utils::NS_TO_MS) << " ms";
    ImGui::TextUnformatted(oss.str().c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "(smoothed)");
    
    // Reshade Overhead Display
    oss.str("");
    oss.clear();
    oss << "Reshade Overhead Duration: " << std::fixed << std::setprecision(3) 
    << ((1.0 *::g_reshade_overhead_duration_ns.load() - ::fps_sleep_before_on_present_ns.load() - ::fps_sleep_after_on_present_ns.load()) / utils::NS_TO_MS) << " ms";
    ImGui::TextUnformatted(oss.str().c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "(smoothed)");


    oss.str("");
    oss.clear();
    oss << "FPS Limiter Sleep Duration (before onPresent): " << std::fixed << std::setprecision(3) << (1.0 *::fps_sleep_before_on_present_ns.load() / utils::NS_TO_MS) << " ms";
    ImGui::TextUnformatted(oss.str().c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "(smoothed)");

    
    // FPS Limiter Start Duration Display
    oss.str("");
    oss.clear();
    oss << "FPS Limiter Sleep Duration (after onPresent): " << std::fixed << std::setprecision(3) << (1.0 *::fps_sleep_after_on_present_ns.load() / utils::NS_TO_MS) << " ms";
    ImGui::TextUnformatted(oss.str().c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "(smoothed)");

    // Flip State Display (renamed from DXGI Composition)
    const char* flip_state_str = "Unknown";
    int flip_state_case = static_cast<int>(::s_dxgi_composition_state);
    switch (flip_state_case) {
        case 1: flip_state_str = "Composed Flip"; break;
        case 2: flip_state_str = "MPO Independent Flip"; break;
        case 3: flip_state_str = "Legacy Independent Flip"; break;
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

} // namespace ui::new_ui
