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
#include "../ui_display_tab.hpp"

// Global variable declaration
extern std::unique_ptr<renodx::dxgi::fps_limiter::CustomFpsLimiterManager> renodx::dxgi::fps_limiter::g_customFpsLimiterManager;

// External constants for width and height options
extern const int WIDTH_OPTIONS[];
extern const int HEIGHT_OPTIONS[];

// External global variables
extern float s_background_feature_enabled;

namespace renodx::ui::new_ui {

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
        s_reflex_enabled = g_main_new_tab_settings.reflex_enabled.GetValue() ? 1.0f : 0.0f;
        settings_loaded_once = true;
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
    
    // Window Controls Section
    DrawWindowControls();
    
    ImGui::Spacing();
    ImGui::Separator();
    
    // Basic Reflex Settings Section
    DrawBasicReflexSettings();

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
    std::vector<std::string> monitor_labels = MakeMonitorLabels();
    std::vector<const char*> monitor_c_labels;
    monitor_c_labels.reserve(monitor_labels.size());
    for (const auto& label : monitor_labels) {
        monitor_c_labels.push_back(label.c_str());
    }
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
    if (ImGui::SliderFloat("FPS Limit", &fps_limit, 0.0f, 240.0f, fps_limit > 0.0f ? "%.0f FPS" : "No Limit")) {
        s_fps_limit = fps_limit;
        
        if (fps_limit > 0.0f) {
            // Custom FPS Limiter is always enabled, just initialize if needed
            if (renodx::dxgi::fps_limiter::g_customFpsLimiterManager && renodx::dxgi::fps_limiter::g_customFpsLimiterManager->InitializeCustomFpsLimiterSystem()) {
                LogWarn("Custom FPS Limiter system auto-initialized");
            } else {
                LogWarn("Failed to initialize Custom FPS Limiter system");
                return;
            }
            
            // Update the custom FPS limiter
            if (renodx::dxgi::fps_limiter::g_customFpsLimiterManager) {
                auto& limiter = renodx::dxgi::fps_limiter::g_customFpsLimiterManager->GetFpsLimiter();
                limiter.SetTargetFps(fps_limit);
                limiter.SetEnabled(true);
                
                std::ostringstream oss;
                oss << "FPS limit applied: " << static_cast<int>(fps_limit) << " FPS (via Custom FPS Limiter)";
                LogInfo(oss.str().c_str());
            }
        } else {
            // FPS limit set to 0, disable the limiter
            if (renodx::dxgi::fps_limiter::g_customFpsLimiterManager) {
                auto& limiter = renodx::dxgi::fps_limiter::g_customFpsLimiterManager->GetFpsLimiter();
                limiter.SetEnabled(false);
                LogInfo("FPS limit removed (no limit)");
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Set FPS limit for the game (0 = no limit). Now uses the new Custom FPS Limiter system.");
    }
    
    // Background FPS Limit slider
    float fps_limit_bg = s_fps_limit_background;
    if (ImGui::SliderFloat("Background FPS Limit", &fps_limit_bg, 0.0f, 240.0f, fps_limit_bg > 0.0f ? "%.0f FPS" : "No Limit")) {
        s_fps_limit_background = fps_limit_bg;
        
        if (fps_limit_bg > 0.0f) {
            // Custom FPS Limiter is always enabled, just initialize if needed
            if (renodx::dxgi::fps_limiter::g_customFpsLimiterManager && renodx::dxgi::fps_limiter::g_customFpsLimiterManager->InitializeCustomFpsLimiterSystem()) {
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
                if (renodx::dxgi::fps_limiter::g_customFpsLimiterManager) {
                    auto& limiter = renodx::dxgi::fps_limiter::g_customFpsLimiterManager->GetFpsLimiter();
                    limiter.SetTargetFps(fps_limit_bg);
                    limiter.SetEnabled(true);
                    
                    std::ostringstream oss;
                    oss << "Background FPS limit applied immediately: " << static_cast<int>(fps_limit_bg) << " FPS (via Custom FPS Limiter)";
                    LogInfo(oss.str().c_str());
                }
            }
        } else {
            // Background FPS limit set to 0, disable the limiter
            if (renodx::dxgi::fps_limiter::g_customFpsLimiterManager) {
                auto& limiter = renodx::dxgi::fps_limiter::g_customFpsLimiterManager->GetFpsLimiter();
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

void DrawBasicReflexSettings() {
    ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "=== Basic Reflex Settings ===");
    
    // Enable NVIDIA Reflex checkbox (persistent)
    if (CheckboxSetting(g_main_new_tab_settings.reflex_enabled, "Enable NVIDIA Reflex")) {
        s_reflex_enabled = g_main_new_tab_settings.reflex_enabled.GetValue() ? 1.0f : 0.0f;
        std::ostringstream oss;
        oss << "NVIDIA Reflex " << (g_main_new_tab_settings.reflex_enabled.GetValue() ? "enabled" : "disabled");
        LogInfo(oss.str().c_str());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable NVIDIA Reflex for reduced latency. This setting appears in the basic tab for easy access.");
    }
}

void DrawImportantInfo() {
    ImGui::Spacing();
    ImGui::Separator();
    
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.8f, 1.0f), "=== Important Information ===");
    
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

} // namespace renodx::ui::new_ui
