#include "main_new_tab.hpp"
#include "../../renodx/settings.hpp"
#include "../../background_window.hpp"
#include "../../dxgi/custom_fps_limiter_manager.hpp"
#include <atomic>
#include "main_new_tab_settings.hpp"
#include "developer_new_tab_settings.hpp"
#include "../../addon.hpp"
#include "../../audio/audio_management.hpp"
#include "../../dxgi/custom_fps_limiter.hpp"
#include "../../dxgi/custom_fps_limiter_manager.hpp"
#include <sstream>
#include <thread>
#include <atomic>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <vector>
#include <deque>
#include <utility>
#include <chrono>
#include "../ui_display_tab.hpp"


namespace ui::new_ui {

// Cache for monitor information to avoid expensive operations every frame
static std::vector<std::string> s_cached_monitor_labels;
static std::vector<const char*> s_cached_monitor_c_labels;
static std::chrono::steady_clock::time_point s_last_monitor_cache_update = std::chrono::steady_clock::now();
static const std::chrono::milliseconds MONITOR_CACHE_TTL = std::chrono::milliseconds(1000); // Update cache every 1 second

// Flag to indicate a restart is required after changing VSync/tearing options
static bool s_restart_needed_vsync_tearing = false;

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
            s_windowed_width = (idx == 0) ? GetCurrentMonitorWidth()
                                          : WIDTH_OPTIONS[idx];
        }
        {
            int idx = g_main_new_tab_settings.window_height.GetValue();
            idx = (std::max)(idx, 0);
            int max_idx = 7;
            idx = (std::min)(idx, max_idx);
            s_windowed_height = (idx == 0) ? GetCurrentMonitorHeight()
                                           : HEIGHT_OPTIONS[idx];
        }
        s_aspect_index = g_main_new_tab_settings.aspect_index.GetValue();
        s_target_monitor_index.store(g_main_new_tab_settings.target_monitor_index.GetValue());
        s_background_feature_enabled.store(g_main_new_tab_settings.background_feature.GetValue());
        s_move_to_zero_if_out = g_main_new_tab_settings.alignment.GetValue();
        s_fps_limit.store(g_main_new_tab_settings.fps_limit.GetValue());
        s_fps_limit_background.store(g_main_new_tab_settings.fps_limit_background.GetValue());
        s_audio_volume_percent.store(g_main_new_tab_settings.audio_volume_percent.GetValue());
        s_audio_mute.store(g_main_new_tab_settings.audio_mute.GetValue());
        s_mute_in_background.store(g_main_new_tab_settings.mute_in_background.GetValue());
        s_mute_in_background_if_other_audio.store(g_main_new_tab_settings.mute_in_background_if_other_audio.GetValue());
        // VSync & Tearing
        s_force_vsync_on.store(g_main_new_tab_settings.force_vsync_on.GetValue());
        s_force_vsync_off.store(g_main_new_tab_settings.force_vsync_off.GetValue());
        s_allow_tearing.store(g_main_new_tab_settings.allow_tearing.GetValue());
        s_prevent_tearing.store(g_main_new_tab_settings.prevent_tearing.GetValue());
        // Resolve persisted conflicts: Prevent Tearing takes precedence
        if (s_prevent_tearing.load() && s_allow_tearing.load()) {
            g_main_new_tab_settings.allow_tearing.SetValue(false);
            s_allow_tearing.store(false);
        }
        s_block_input_in_background.store(g_main_new_tab_settings.block_input_in_background.GetValue());
        settings_loaded_once = true;

        // If manual Audio Mute is OFF, proactively unmute on startup
        if (!s_audio_mute.load()) {
            if (::SetMuteForCurrentProcess(false)) {
                ::g_muted_applied.store(false);
                LogInfo("Audio unmuted at startup (Audio Mute is OFF)");
            }
        }

        // Apply saved FPS limiter settings on startup
        if (dxgi::fps_limiter::g_customFpsLimiterManager && dxgi::fps_limiter::g_customFpsLimiterManager->InitializeCustomFpsLimiterSystem()) {
            auto& limiter = dxgi::fps_limiter::g_customFpsLimiterManager->GetFpsLimiter();
            if (s_fps_limit.load() > 0.0f) {
                limiter.SetTargetFps(s_fps_limit.load());
                limiter.SetEnabled(true);
                std::ostringstream oss;
                oss.setf(std::ios::fixed);
                oss << std::setprecision(3);
                oss << "FPS limit applied from saved settings: " << s_fps_limit.load() << " FPS";
                LogInfo(oss.str().c_str());
            } else {
                // Ensure disabled if saved value is 0
                limiter.SetEnabled(false);
            }

            // If currently in background and a background limit is saved, apply it
            HWND hwnd = g_last_swapchain_hwnd.load();
            if (hwnd == nullptr) hwnd = GetForegroundWindow();
            const bool is_background = (hwnd != nullptr && GetForegroundWindow() != hwnd);
            if (is_background && s_fps_limit_background > 0.0f) {
                limiter.SetTargetFps(s_fps_limit_background);
                limiter.SetEnabled(true);
                std::ostringstream oss;
                oss << "Background FPS limit applied from saved settings: " << static_cast<int>(s_fps_limit_background) << " FPS";
                LogInfo(oss.str().c_str());
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
        const bool unstable_enabled = ::s_enable_unstable_reshade_features.load();
        if (!unstable_enabled) ImGui::BeginDisabled();
        bool block_any_in_background =
            g_main_new_tab_settings.block_input_in_background.GetValue();
        if (ImGui::Checkbox("Block Input in Background (Custom DLL needed)", &block_any_in_background)) {
            g_main_new_tab_settings.block_input_in_background.SetValue(block_any_in_background);
            s_block_input_in_background.store(block_any_in_background);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Blocks mouse, keyboard, and cursor warping while the game window is not focused.");
        }
        if (!unstable_enabled) ImGui::EndDisabled();
        if (!unstable_enabled) {
            ImGui::TextDisabled("Enable 'unstable ReShade features' in Developer tab to use these.");
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

void DrawDisplaySettings() {
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
            s_windowed_width = (idx == 0) ? static_cast<float>(GetCurrentMonitorWidth())
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
            s_windowed_height = (idx == 0) ? static_cast<float>(GetCurrentMonitorHeight())
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
    
    // FPS Limit slider (persisted)
    {
        float current_value = g_main_new_tab_settings.fps_limit.GetValue();
        const char* fmt = (current_value > 0.0f) ? "%.3f FPS" : "No Limit";
        if (SliderFloatSetting(g_main_new_tab_settings.fps_limit, "FPS Limit", fmt)) {
            s_fps_limit.store(g_main_new_tab_settings.fps_limit.GetValue());
            
            if (s_fps_limit.load() > 0.0f) {
                // Custom FPS Limiter is always enabled, just initialize if needed
                if (dxgi::fps_limiter::g_customFpsLimiterManager && dxgi::fps_limiter::g_customFpsLimiterManager->InitializeCustomFpsLimiterSystem()) {
                    LogWarn("Custom FPS Limiter system auto-initialized");
                } else {
                    LogWarn("Failed to initialize Custom FPS Limiter system");
                    return;
                }
                
                // Update the custom FPS limiter
                if (dxgi::fps_limiter::g_customFpsLimiterManager) {
                    auto& limiter = dxgi::fps_limiter::g_customFpsLimiterManager->GetFpsLimiter();
                    limiter.SetTargetFps(s_fps_limit.load());
                    limiter.SetEnabled(true);
                    
                    std::ostringstream oss;
                    oss.setf(std::ios::fixed);
                    oss << std::setprecision(3);
                    oss << "FPS limit applied: " << s_fps_limit.load() << " FPS (via Custom FPS Limiter)";
                    LogInfo(oss.str().c_str());
                }
            } else {
                // FPS limit set to 0, disable the limiter
                if (dxgi::fps_limiter::g_customFpsLimiterManager) {
                    auto& limiter = dxgi::fps_limiter::g_customFpsLimiterManager->GetFpsLimiter();
                    limiter.SetEnabled(false);
                    LogInfo("FPS limit removed (no limit)");
                }
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Set FPS limit for the game (0 = no limit). Now uses the new Custom FPS Limiter system.");
    }
    
    // VSync & Tearing controls
    {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "=== VSync & Tearing ===");
        bool vsync_tearing_changed = false;

        bool vs_on = g_main_new_tab_settings.force_vsync_on.GetValue();
        if (ImGui::Checkbox("Force VSync ON (Custom DLL needed)", &vs_on)) {
            vsync_tearing_changed = true;
            // Mutual exclusion
            if (vs_on) {
                g_main_new_tab_settings.force_vsync_off.SetValue(false);
                s_force_vsync_off.store(false);
                g_main_new_tab_settings.allow_tearing.SetValue(false);
                s_allow_tearing.store(false);
            }
            g_main_new_tab_settings.force_vsync_on.SetValue(vs_on);
            s_force_vsync_on.store(vs_on);
            LogInfo(vs_on ? "Force VSync ON enabled" : "Force VSync ON disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Forces sync interval = 1 (may require swapchain recreation by the game).");
        }

        ImGui::SameLine();

        bool vs_off = g_main_new_tab_settings.force_vsync_off.GetValue();
        if (ImGui::Checkbox("Force VSync OFF", &vs_off)) {
            vsync_tearing_changed = true;
            // Mutual exclusion
            if (vs_off) {
                g_main_new_tab_settings.force_vsync_on.SetValue(false);
                s_force_vsync_on.store(false);
            }
            g_main_new_tab_settings.force_vsync_off.SetValue(vs_off);
            s_force_vsync_off.store(vs_off);
            LogInfo(vs_off ? "Force VSync OFF enabled" : "Force VSync OFF disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Forces sync interval = 0 (immediate). Use with 'Allow Tearing' for true VSYNC off in windowed.");
        }

        ImGui::SameLine();

        bool allow_t = g_main_new_tab_settings.allow_tearing.GetValue();
        if (ImGui::Checkbox("Allow Tearing (DXGI)", &allow_t)) {
            vsync_tearing_changed = true;
            // Mutual exclusion with Prevent Tearing
            if (allow_t) {
                g_main_new_tab_settings.prevent_tearing.SetValue(false);
                s_prevent_tearing.store(false);
                g_main_new_tab_settings.force_vsync_on.SetValue(false);
                s_force_vsync_on.store(false);
            }
            g_main_new_tab_settings.allow_tearing.SetValue(allow_t);
            s_allow_tearing.store(allow_t);
            LogInfo(allow_t ? "DXGI tearing allowed enabled (flag will be applied on swapchain)" : "DXGI tearing allowed disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Sets DXGI swapchain Allow Tearing flag (Flip Model + OS support required). Applies on swapchain creation.");
        }

        ImGui::SameLine();

        bool prevent_t = g_main_new_tab_settings.prevent_tearing.GetValue();
        if (ImGui::Checkbox("Prevent Tearing", &prevent_t)) {
            vsync_tearing_changed = true;
            // Mutual exclusion with Enable Tearing
            if (prevent_t) {
                g_main_new_tab_settings.allow_tearing.SetValue(false);
                s_allow_tearing.store(false);
            }
            g_main_new_tab_settings.prevent_tearing.SetValue(prevent_t);
            s_prevent_tearing.store(prevent_t);
            LogInfo(prevent_t ? "Prevent Tearing enabled (tearing flags will be cleared)" : "Prevent Tearing disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Prevents tearing by clearing DXGI tearing flags and preferring sync. Mutually exclusive with Enable Tearing.");
        }

        // If any of the VSync/tearing settings changed this frame, mark restart as required
        if (vsync_tearing_changed) {
            s_restart_needed_vsync_tearing = true;
        }

        // Display restart-required notice if flagged
        if (s_restart_needed_vsync_tearing) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Game restart required to apply VSync/tearing changes.");
        }
    }

    // Quick-set buttons based on current monitor refresh rate
    {
        double refresh_hz;
        {
            ::g_window_state_lock.lock();
            refresh_hz = ::g_window_state.current_monitor_refresh_rate.ToHz();
            ::g_window_state_lock.unlock();
        }
        int y = static_cast<int>(std::round(refresh_hz));
        if (y > 0) {
            bool first = true;
            const float selected_epsilon = 0.01f;
            // Add No Limit button at the beginning
            {
                bool selected = (std::fabs(s_fps_limit.load() - 0.0f) <= selected_epsilon);
                if (selected) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.60f, 0.20f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.70f, 0.20f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.50f, 0.10f, 1.0f));
                }
                if (ImGui::Button("No Limit")) {
                s_fps_limit.store(0.0f);
                g_main_new_tab_settings.fps_limit.SetValue(0.0f);
                if (dxgi::fps_limiter::g_customFpsLimiterManager) {
                    auto& limiter = dxgi::fps_limiter::g_customFpsLimiterManager->GetFpsLimiter();
                    limiter.SetEnabled(false);
                    LogInfo("FPS limit removed (no limit)");
                }
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
                            bool selected = (std::fabs(s_fps_limit.load() - candidate_precise) <= selected_epsilon);
                            if (selected) {
                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.60f, 0.20f, 1.0f));
                                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.70f, 0.20f, 1.0f));
                                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.50f, 0.10f, 1.0f));
                            }
                            if (ImGui::Button(label.c_str())) {
                            float target_fps = candidate_precise;
                            s_fps_limit.store(target_fps);
                            g_main_new_tab_settings.fps_limit.SetValue(target_fps);
                            
                            // Ensure limiter system is initialized
                            if (dxgi::fps_limiter::g_customFpsLimiterManager && dxgi::fps_limiter::g_customFpsLimiterManager->InitializeCustomFpsLimiterSystem()) {
                                LogWarn("Custom FPS Limiter system auto-initialized");
                            } else if (!dxgi::fps_limiter::g_customFpsLimiterManager) {
                                LogWarn("Failed to initialize Custom FPS Limiter system");
                            }
                            
                            // Apply the selected FPS limit immediately
                            if (dxgi::fps_limiter::g_customFpsLimiterManager) {
                                auto& limiter = dxgi::fps_limiter::g_customFpsLimiterManager->GetFpsLimiter();
                                limiter.SetTargetFps(target_fps);
                                limiter.SetEnabled(true);
                                
                                std::ostringstream oss;
                                oss.setf(std::ios::fixed);
                                oss << std::setprecision(3);
                                oss << "FPS limit applied: " << target_fps << " FPS (via Custom FPS Limiter)";
                                LogInfo(oss.str().c_str());
                            }
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
            // Add Reflex Cap button at the end
            if (!first) ImGui::SameLine();
            {
                // Reflex formula: refresh_hz - (refresh_hz * refresh_hz / 3600)
                double reflex_target = refresh_hz - (refresh_hz * refresh_hz / 3600.0);
                float precise_target = static_cast<float>(reflex_target);
                if (precise_target < 1.0f) precise_target = 1.0f;
                bool selected = (std::fabs(s_fps_limit.load() - precise_target) <= selected_epsilon);
                if (selected) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.60f, 0.20f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.70f, 0.20f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.50f, 0.10f, 1.0f));
                }
                if (ImGui::Button("Reflex Cap")) {
                double precise_target = reflex_target; // do not round on apply
                float target_fps = static_cast<float>(precise_target < 1.0 ? 1.0 : precise_target);
                s_fps_limit.store(target_fps);
                g_main_new_tab_settings.fps_limit.SetValue(target_fps);
                if (target_fps > 0.0f) {
                    if (dxgi::fps_limiter::g_customFpsLimiterManager && dxgi::fps_limiter::g_customFpsLimiterManager->InitializeCustomFpsLimiterSystem()) {
                        LogWarn("Custom FPS Limiter system auto-initialized");
                    } else if (!dxgi::fps_limiter::g_customFpsLimiterManager) {
                        LogWarn("Failed to initialize Custom FPS Limiter system");
                    }
                    if (dxgi::fps_limiter::g_customFpsLimiterManager) {
                        auto& limiter = dxgi::fps_limiter::g_customFpsLimiterManager->GetFpsLimiter();
                        limiter.SetTargetFps(target_fps);
                        limiter.SetEnabled(true);
                        std::ostringstream oss;
                        oss.setf(std::ios::fixed);
                        oss << std::setprecision(3);
                        oss << "FPS limit applied: " << target_fps << " FPS (Reflex Cap via Custom FPS Limiter)";
                        LogInfo(oss.str().c_str());
                    }
                } else {
                    if (dxgi::fps_limiter::g_customFpsLimiterManager) {
                        auto& limiter = dxgi::fps_limiter::g_customFpsLimiterManager->GetFpsLimiter();
                        limiter.SetEnabled(false);
                        LogInfo("FPS limit removed (no limit)");
                    }
                }
                }
                if (selected) {
                    ImGui::PopStyleColor(3);
                }
                // Add tooltip explaining the Reflex formula
                if (ImGui::IsItemHovered()) {
                    std::ostringstream tooltip_oss;
                    tooltip_oss.setf(std::ios::fixed);
                    tooltip_oss << std::setprecision(3);
                    tooltip_oss << "Reflex Cap: FPS = " << refresh_hz << " - (" << refresh_hz << "² / 3600)\n";
                    tooltip_oss << "= " << refresh_hz << " - " << (refresh_hz * refresh_hz / 3600.0) << " = " << reflex_target << " FPS\n\n";
                    tooltip_oss << "Creates a ~0.3ms frame time buffer to optimize latency\n";
                    tooltip_oss << "and prevent tearing, similar to NVIDIA Reflex Low Latency Mode.";
                    ImGui::SetTooltip("%s", tooltip_oss.str().c_str());
                }
            }
        }
    }
    
    // Background FPS Limit slider (persisted)
    {
        float current_bg = g_main_new_tab_settings.fps_limit_background.GetValue();
        const char* fmt_bg = (current_bg > 0.0f) ? "%.0f FPS" : "No Limit";
        if (SliderFloatSetting(g_main_new_tab_settings.fps_limit_background, "Background FPS Limit", fmt_bg)) {
            s_fps_limit_background.store(g_main_new_tab_settings.fps_limit_background.GetValue());
            
                          if (s_fps_limit_background.load() > 0.0f) {
                // Custom FPS Limiter is always enabled, just initialize if needed
                if (dxgi::fps_limiter::g_customFpsLimiterManager && dxgi::fps_limiter::g_customFpsLimiterManager->InitializeCustomFpsLimiterSystem()) {
                    LogWarn("Custom FPS Limiter system auto-initialized");
                } else {
                    LogWarn("Failed to initialize Custom FPS Limiter system");
                    return;
                }
                
                // Apply background FPS limit immediately if currently in background
                HWND hwnd = g_last_swapchain_hwnd.load();
                if (hwnd == nullptr) hwnd = GetForegroundWindow();
                const bool is_background = (hwnd != nullptr && GetForegroundWindow() != hwnd);
                
                if (is_background && s_fps_limit_background >= 0.f) {
                    if (dxgi::fps_limiter::g_customFpsLimiterManager) {
                        auto& limiter = dxgi::fps_limiter::g_customFpsLimiterManager->GetFpsLimiter();
                        limiter.SetTargetFps(s_fps_limit_background);
                        limiter.SetEnabled(true);
                        
                        std::ostringstream oss;
                        oss << "Background FPS limit applied immediately: " << static_cast<int>(s_fps_limit_background) << " FPS (via Custom FPS Limiter)";
                        LogInfo(oss.str().c_str());
                    }
                }
            } else {
                // Background FPS limit set to 0, disable the limiter
                if (dxgi::fps_limiter::g_customFpsLimiterManager) {
                    auto& limiter = dxgi::fps_limiter::g_customFpsLimiterManager->GetFpsLimiter();
                    limiter.SetEnabled(false);
                    LogInfo("Background FPS limit removed (no limit)");
                }
            }
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
        s_audio_mute.store(audio_mute);
        
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
        s_mute_in_background.store(mute_in_bg);
        
        // Reset applied flag so the monitor thread reapplies desired state
        ::g_muted_applied.store(false);
        // Also apply the current mute state immediately if manual mute is off
        if (!s_audio_mute.load()) {
            HWND hwnd = g_last_swapchain_hwnd.load();
            if (hwnd == nullptr) hwnd = GetForegroundWindow();
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
        s_mute_in_background_if_other_audio.store(mute_in_bg_if_other);
        g_main_new_tab_settings.mute_in_background_if_other_audio.SetValue(mute_in_bg_if_other);
        ::g_muted_applied.store(false);
        if (!s_audio_mute.load()) {
            HWND hwnd = g_last_swapchain_hwnd.load();
            if (hwnd == nullptr) hwnd = GetForegroundWindow();
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
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.8f, 1.0f), "=== Window Controls ===");
    
    // Window Control Buttons (Minimize, Restore, and Maximize side by side)
    ImGui::BeginGroup();
    
    // Minimize Window Button
    if (ImGui::Button("Minimize Window")) {
        std::thread([](){
            HWND hwnd = g_last_swapchain_hwnd.load();
            if (hwnd == nullptr) hwnd = GetForegroundWindow();
            if (hwnd == nullptr) {
                LogWarn("Minimize Window: no window handle available");
                return;
            }
            LogDebug("Minimize Window button pressed (bg thread)");
            if (ShowWindow(hwnd, SW_MINIMIZE)) {
                LogInfo("Window minimized successfully");
            } else {
                DWORD error = GetLastError();
                std::ostringstream oss;
                oss << "Failed to minimize window. Error: " << error;
                LogWarn(oss.str().c_str());
            }
        }).detach();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Minimize the current game window.");
    }
    
    ImGui::SameLine();
    
    // Restore Window Button
    if (ImGui::Button("Restore Window")) {
        std::thread([](){
            HWND hwnd = g_last_swapchain_hwnd.load();
            if (hwnd == nullptr) hwnd = GetForegroundWindow();
            if (hwnd == nullptr) {
                LogWarn("Restore Window: no window handle available");
                return;
            }
            LogDebug("Restore Window button pressed (bg thread)");
            if (ShowWindow(hwnd, SW_RESTORE)) {
                LogInfo("Window restored successfully");
            } else {
                DWORD error = GetLastError();
                std::ostringstream oss;
                oss << "Failed to restore window. Error: " << error;
                LogWarn(oss.str().c_str());
            }
        }).detach();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Restore the minimized game window.");
    }
    
    ImGui::SameLine();
    
    // Maximize Window Button
    if (ImGui::Button("Maximize Window")) {
        std::thread([](){
            HWND hwnd = g_last_swapchain_hwnd.load();
            if (hwnd == nullptr) hwnd = GetForegroundWindow();
            if (hwnd == nullptr) {
                LogWarn("Maximize Window: no window handle available");
                return;
            }
            LogDebug("Maximize Window button pressed (bg thread)");
            
            // Set window dimensions to current monitor size
            s_windowed_width = static_cast<float>(GetCurrentMonitorWidth());
            s_windowed_height = static_cast<float>(GetCurrentMonitorHeight());
            
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
    
    // Initialize the display cache when first accessed
    static bool cache_initialized = false;
    if (!cache_initialized) {
        InitializeDisplayCache();
        cache_initialized = true;
    }
    
    // Dynamic Monitor Settings
    if (ImGui::CollapsingHeader("Dynamic Monitor Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        HandleMonitorSettingsUI();
    }
    
    ImGui::Spacing();
    
    // Current Display Info
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Current Display Info:");
    std::string display_info = GetCurrentDisplayInfoFromCache();
    ImGui::TextWrapped("%s", display_info.c_str());
}

void DrawImportantInfo() {
    ImGui::Spacing();
    ImGui::Separator();
    
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.8f, 1.0f), "=== Important Information ===");
    
    // FPS Counter with 1% Low and 0.1% Low over past 60s (computed in background)
    {
        std::string local_text;
        ::g_perf_text_lock.lock();
        local_text = ::g_perf_text_shared;
        ::g_perf_text_lock.unlock();
        ImGui::TextUnformatted(local_text.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Reset Stats")) {
            ::g_perf_reset_requested.store(true, std::memory_order_release);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Reset FPS/frametime statistics. Metrics are computed since reset.");
        }
    }
    
    // PCL AV Latency Display
    float pcl_latency = ::g_pcl_av_latency_ms.load();
    
    std::ostringstream oss;
    oss << "PCL AV Latency: " << std::fixed << std::setprecision(2) << pcl_latency << " ms";
    ImGui::TextUnformatted(oss.str().c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "(30-frame avg)");
    
    // Reflex Status Display
    bool is_active = ::g_reflex_active.load();
    
    oss.str("");
    oss.clear();
    if (is_active) {
        oss << "Reflex Status: Active";
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "%s", oss.str().c_str());
    } else {
        oss << "Reflex Status: Inactive";
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.8f, 1.0f), "%s", oss.str().c_str());
    }
    
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
