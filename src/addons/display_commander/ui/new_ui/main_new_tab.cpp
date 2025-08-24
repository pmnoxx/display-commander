#include "main_new_tab.hpp"
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

// Global variable declaration
extern std::unique_ptr<dxgi::fps_limiter::CustomFpsLimiterManager> dxgi::fps_limiter::g_customFpsLimiterManager;

// External constants for width and height options
extern const int WIDTH_OPTIONS[];
extern const int HEIGHT_OPTIONS[];

// External global variables
extern float s_background_feature_enabled;

namespace ui::new_ui {

// Cache for monitor information to avoid expensive operations every frame
static std::vector<std::string> s_cached_monitor_labels;
static std::vector<const char*> s_cached_monitor_c_labels;
static std::chrono::steady_clock::time_point s_last_monitor_cache_update = std::chrono::steady_clock::now();
static const std::chrono::milliseconds MONITOR_CACHE_TTL = std::chrono::milliseconds(1000); // Update cache every 1 second

void InitMainNewTab() {

    static bool settings_loaded_once = false;
    if (!settings_loaded_once) {
        // Ensure developer settings (including continuous monitoring) are loaded so UI reflects saved state
        g_developerTabSettings.LoadAll();
        g_main_new_tab_settings.LoadSettings();
        s_window_mode = static_cast<float>(g_main_new_tab_settings.window_mode.GetValue());
        {
            int idx = g_main_new_tab_settings.window_width.GetValue();
            idx = (std::max)(idx, 0);
            int max_idx = 7;
            idx = (std::min)(idx, max_idx);
            s_windowed_width = (idx == 0) ? static_cast<float>(GetCurrentMonitorWidth())
                                          : static_cast<float>(WIDTH_OPTIONS[idx]);
        }
        {
            int idx = g_main_new_tab_settings.window_height.GetValue();
            idx = (std::max)(idx, 0);
            int max_idx = 7;
            idx = (std::min)(idx, max_idx);
            s_windowed_height = (idx == 0) ? static_cast<float>(GetCurrentMonitorHeight())
                                           : static_cast<float>(HEIGHT_OPTIONS[idx]);
        }
        s_aspect_index = static_cast<float>(g_main_new_tab_settings.aspect_index.GetValue());
        s_target_monitor_index = static_cast<float>(g_main_new_tab_settings.target_monitor_index.GetValue());
        s_background_feature_enabled = g_main_new_tab_settings.background_feature.GetValue() ? 1.0f : 0.0f;
        s_move_to_zero_if_out = static_cast<float>(g_main_new_tab_settings.alignment.GetValue());
        s_fps_limit = g_main_new_tab_settings.fps_limit.GetValue();
        s_fps_limit_background = g_main_new_tab_settings.fps_limit_background.GetValue();
        s_audio_volume_percent = g_main_new_tab_settings.audio_volume_percent.GetValue();
        s_audio_mute = g_main_new_tab_settings.audio_mute.GetValue() ? 1.0f : 0.0f;
        s_mute_in_background = g_main_new_tab_settings.mute_in_background.GetValue() ? 1.0f : 0.0f;
        s_mute_in_background_if_other_audio = g_main_new_tab_settings.mute_in_background_if_other_audio.GetValue() ? 1.0f : 0.0f;
     
        s_block_mouse_in_background = g_main_new_tab_settings.block_mouse_in_background.GetValue() ? 1.0f : 0.0f;
        s_block_keyboard_in_background = g_main_new_tab_settings.block_keyboard_in_background.GetValue() ? 1.0f : 0.0f;
        s_block_mouse_cursor_warping_in_background = g_main_new_tab_settings.block_mouse_cursor_warping_in_background.GetValue() ? 1.0f : 0.0f;
        settings_loaded_once = true;

        // If manual Audio Mute is OFF, proactively unmute on startup
        if (s_audio_mute < 0.5f) {
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
        const bool unstable_enabled = (::s_enable_unstable_reshade_features >= 0.5f);
        if (!unstable_enabled) ImGui::BeginDisabled();
        bool bm = g_main_new_tab_settings.block_mouse_in_background.GetValue();
        if (ImGui::Checkbox("Block Mouse Input in Background", &bm)) {
            g_main_new_tab_settings.block_mouse_in_background.SetValue(bm);
            s_block_mouse_in_background = bm ? 1.0f : 0.0f;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("When enabled, mouse input is blocked while the game window is not focused.");
        }

        bool bk = g_main_new_tab_settings.block_keyboard_in_background.GetValue();
        if (ImGui::Checkbox("Block Keyboard Input in Background", &bk)) {
            g_main_new_tab_settings.block_keyboard_in_background.SetValue(bk);
            s_block_keyboard_in_background = bk ? 1.0f : 0.0f;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("When enabled, keyboard input is blocked while the game window is not focused.");
        }

        bool bw = g_main_new_tab_settings.block_mouse_cursor_warping_in_background.GetValue();
        if (ImGui::Checkbox("Block Mouse Cursor Warping in Background", &bw)) {
            g_main_new_tab_settings.block_mouse_cursor_warping_in_background.SetValue(bw);
            s_block_mouse_cursor_warping_in_background = bw ? 1.0f : 0.0f;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("When enabled, cursor warping is blocked while the game window is not focused.");
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
        s_continuous_monitoring_enabled = g_developerTabSettings.continuous_monitoring.GetValue() ? 1.0f : 0.0f;
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
        ::g_monitor_labels_lock.lock();
        monitor_labels_local = ::g_monitor_labels; // copy to avoid lifetime issues
        ::g_monitor_labels_lock.unlock();
        monitor_c_labels.reserve(monitor_labels_local.size());
        for (const auto& label : monitor_labels_local) {
            monitor_c_labels.push_back(label.c_str());
        }
    }
    /*
    // Fallback: if cache empty, build once synchronously (first-time UI open)
    if (monitor_c_labels.empty()) {
        auto labels = MakeMonitorLabels();
        ::g_monitor_labels_lock.lock();
        ::g_monitor_labels = labels;
        monitor_labels_local = ::g_monitor_labels;
        ::g_monitor_labels_lock.unlock();
        monitor_c_labels.clear();
        monitor_c_labels.reserve(monitor_labels_local.size());
        for (const auto& label : monitor_labels_local) {
            monitor_c_labels.push_back(label.c_str());
        }
        ::g_monitor_labels_need_update.store(false);
    }*/
    int monitor_index = static_cast<int>(s_target_monitor_index);
    if (ImGui::Combo("Target Monitor", &monitor_index, monitor_c_labels.data(), static_cast<int>(monitor_c_labels.size()))) {
        s_target_monitor_index = static_cast<float>(monitor_index);
        g_main_new_tab_settings.target_monitor_index.SetValue(monitor_index);
        LogInfo("Target monitor changed");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Choose which monitor to apply size/pos to. 'Auto' uses the current window monitor.");
    }
    
    // Background Black Curtain checkbox (only shown in Borderless Windowed modes)
    if (s_window_mode < 1.5f) {
        if (CheckboxSetting(g_main_new_tab_settings.background_feature, "Background Black Curtain")) {
            s_background_feature_enabled = g_main_new_tab_settings.background_feature.GetValue() ? 1.0f : 0.0f;
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
        extern std::atomic<uint64_t> g_init_apply_generation;
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
    
    // FPS Limit slider
    float fps_limit = s_fps_limit;
    if (ImGui::SliderFloat("FPS Limit", &fps_limit, 0.0f, 240.0f, fps_limit > 0.0f ? "%.3f FPS" : "No Limit")) {
        s_fps_limit = fps_limit;
        
        if (fps_limit > 0.0f) {
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
                limiter.SetTargetFps(fps_limit);
                limiter.SetEnabled(true);
                
                std::ostringstream oss;
                oss.setf(std::ios::fixed);
                oss << std::setprecision(3);
                oss << "FPS limit applied: " << fps_limit << " FPS (via Custom FPS Limiter)";
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
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Set FPS limit for the game (0 = no limit). Now uses the new Custom FPS Limiter system.");
    }
    
    // Quick-set buttons based on current monitor refresh rate
    {
        double refresh_hz = ::g_window_state.current_monitor_refresh_rate.ToHz();
        int y = static_cast<int>(std::round(refresh_hz));
        if (y > 0) {
            bool first = true;
            // Add No Limit button at the beginning
            if (ImGui::Button("No Limit")) {
                s_fps_limit = 0.0f;
                if (dxgi::fps_limiter::g_customFpsLimiterManager) {
                    auto& limiter = dxgi::fps_limiter::g_customFpsLimiterManager->GetFpsLimiter();
                    limiter.SetEnabled(false);
                    LogInfo("FPS limit removed (no limit)");
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
                        if (ImGui::Button(label.c_str())) {
                            float target_fps = candidate_precise;
                            s_fps_limit = target_fps;
                            
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
                    }
                }
            }
            // Add 95% button at the end
            if (!first) ImGui::SameLine();
            if (ImGui::Button("95%")) {
                double precise_target = refresh_hz * 0.95; // do not round on apply
                float target_fps = static_cast<float>(precise_target < 1.0 ? 1.0 : precise_target);
                s_fps_limit = target_fps;
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
                        oss << "FPS limit applied: " << target_fps << " FPS (95% of refresh via Custom FPS Limiter)";
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
        }
    }
    
    // Background FPS Limit slider
    float fps_limit_bg = s_fps_limit_background;
    if (ImGui::SliderFloat("Background FPS Limit", &fps_limit_bg, 0.0f, 240.0f, fps_limit_bg > 0.0f ? "%.0f FPS" : "No Limit")) {
        s_fps_limit_background = fps_limit_bg;
        
        if (fps_limit_bg > 0.0f) {
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
            
            if (is_background && fps_limit_bg >= 0.f) {
                if (dxgi::fps_limiter::g_customFpsLimiterManager) {
                    auto& limiter = dxgi::fps_limiter::g_customFpsLimiterManager->GetFpsLimiter();
                    limiter.SetTargetFps(fps_limit_bg);
                    limiter.SetEnabled(true);
                    
                    std::ostringstream oss;
                    oss << "Background FPS limit applied immediately: " << static_cast<int>(fps_limit_bg) << " FPS (via Custom FPS Limiter)";
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
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("FPS cap when the game window is not in the foreground. Now uses the new Custom FPS Limiter system.");
    }
}

void DrawAudioSettings() {
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Audio Settings ===");
    
    // Audio Volume slider
    float volume = s_audio_volume_percent;
    if (ImGui::SliderFloat("Audio Volume (%)", &volume, 0.0f, 100.0f, "%.0f%%")) {
        s_audio_volume_percent = volume;
        
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
    bool audio_mute = (s_audio_mute >= 0.5f);
    if (ImGui::Checkbox("Audio Mute", &audio_mute)) {
        s_audio_mute = audio_mute ? 1.0f : 0.0f;
        
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
    bool mute_in_bg = (s_mute_in_background >= 0.5f);
    if (s_audio_mute >= 0.5f) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Checkbox("Mute In Background", &mute_in_bg)) {
        s_mute_in_background = mute_in_bg ? 1.0f : 0.0f;
        
        // Reset applied flag so the monitor thread reapplies desired state
        ::g_muted_applied.store(false);
        // Also apply the current mute state immediately if manual mute is off
        if (s_audio_mute < 0.5f) {
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
    if (s_audio_mute >= 0.5f) {
        ImGui::EndDisabled();
    }

    // Mute in Background only if other app plays audio
    bool mute_in_bg_if_other = (s_mute_in_background_if_other_audio >= 0.5f);
    if (s_audio_mute >= 0.5f) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Checkbox("Mute In Background (only if other app has audio)", &mute_in_bg_if_other)) {
        s_mute_in_background_if_other_audio = mute_in_bg_if_other ? 1.0f : 0.0f;
        g_main_new_tab_settings.mute_in_background_if_other_audio.SetValue(mute_in_bg_if_other);
        ::g_muted_applied.store(false);
        if (s_audio_mute < 0.5f) {
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
    if (s_audio_mute >= 0.5f) {
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
    extern std::atomic<float> g_pcl_av_latency_ms;
    float pcl_latency = ::g_pcl_av_latency_ms.load();
    
    std::ostringstream oss;
    oss << "PCL AV Latency: " << std::fixed << std::setprecision(2) << pcl_latency << " ms";
    ImGui::TextUnformatted(oss.str().c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "(30-frame avg)");
    
    // Reflex Status Display
    extern std::atomic<bool> g_reflex_active;
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
    extern float s_dxgi_composition_state;
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
