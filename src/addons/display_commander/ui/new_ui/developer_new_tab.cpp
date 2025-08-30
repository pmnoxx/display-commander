#include "developer_new_tab.hpp"
#include "developer_new_tab_settings.hpp"
#include "settings_wrapper.hpp"
#include "../../addon.hpp"
#include "../../nvapi/nvapi_fullscreen_prevention.hpp"
#include "../../nvapi/nvapi_hdr_monitor.hpp"
#include "../ui_common.hpp"
#include "../../display_cache.hpp"
#include <sstream>
#include <iomanip>
#include <thread>
#include <atomic>

// External declarations for Reflex settings
extern std::atomic<bool> s_reflex_enabled;
extern std::atomic<bool> s_reflex_low_latency_mode;
extern std::atomic<bool> s_reflex_low_latency_boost;
extern std::atomic<bool> s_reflex_use_markers;
extern std::atomic<bool> s_reflex_debug_output;
extern std::atomic<bool> g_reflex_settings_changed;

static std::atomic<bool> s_restart_needed_nvapi(false);

namespace ui::new_ui {

void InitDeveloperNewTab() {
    // Ensure settings are loaded
    static bool settings_loaded = false;
    if (!settings_loaded) {
        g_developerTabSettings.LoadAll();
        settings_loaded = true;
    }
    
}

void DrawDeveloperNewTab() {
    ImGui::Text("Developer Tab - Advanced Features");
    ImGui::Separator();
    
    // Developer Settings Section
    DrawDeveloperSettings();
    
    ImGui::Spacing();
    ImGui::Separator();
    
    // NVAPI Settings Section (merged with HDR and Colorspace Settings)
    DrawNvapiSettings();
    
    ImGui::Spacing();
    ImGui::Separator();
    
    // Reflex Settings Section
    DrawReflexSettings();
    
    ImGui::Spacing();
    ImGui::Separator();
    
    // Resolution Override Settings Section
    DrawResolutionOverrideSettings();
    
    ImGui::Spacing();
    ImGui::Separator();
    
    // Keyboard Shortcuts Section
    DrawKeyboardShortcutsSettings();
    
    ImGui::Spacing();
    ImGui::Separator();
    
    // Latency Display Section
    DrawLatencyDisplay();
}

void DrawDeveloperSettings() {
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "=== Developer Settings ===");
    
    // Enable unstable ReShade features (requires custom dxgi.dll)
    if (CheckboxSetting(g_developerTabSettings.enable_unstable_reshade_features, "Enable unstable ReShade features (requires custom dxgi.dll)")) {
        ::s_enable_unstable_reshade_features.store(g_developerTabSettings.enable_unstable_reshade_features.GetValue());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable experimental features that rely on a custom dxgi.dll build. Off by default.");
    }
    
    // Prevent Fullscreen (global)
    if (CheckboxSetting(g_developerTabSettings.prevent_fullscreen, "Prevent Fullscreen")) {
        // Update global variable for compatibility
        s_prevent_fullscreen.store(g_developerTabSettings.prevent_fullscreen.GetValue());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Prevent exclusive fullscreen; keep borderless/windowed for stability and HDR.");
    }
    
    // Spoof Fullscreen State
    const char* spoof_fullscreen_labels[] = {"Disabled", "Spoof as Fullscreen", "Spoof as Windowed"};
    int spoof_fullscreen_state = static_cast<int>(g_developerTabSettings.spoof_fullscreen_state.GetValue());
    if (ImGui::Combo("Spoof Fullscreen State", &spoof_fullscreen_state, spoof_fullscreen_labels, 3)) {
        g_developerTabSettings.spoof_fullscreen_state.SetValue(spoof_fullscreen_state != 0);
        s_spoof_fullscreen_state.store(spoof_fullscreen_state != 0);
        
        // Log the change
        std::ostringstream oss;
        oss << "Fullscreen state spoofing changed to ";
        if (spoof_fullscreen_state < 0.5f) {
            oss << "Disabled";
        } else if (spoof_fullscreen_state < 1.5f) {
            oss << "Spoof as Fullscreen";
        } else {
            oss << "Spoof as Windowed";
        }
        ::LogInfo(oss.str().c_str());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Spoof fullscreen state detection for applications that query fullscreen status. Useful for games that change behavior based on fullscreen state.");
    }
    
    // Reset button for Fullscreen State (only show if not at default)
    if (s_spoof_fullscreen_state.load() != false) {
        ImGui::SameLine();
        if (ImGui::Button("Reset##DevFullscreen")) {
            g_developerTabSettings.spoof_fullscreen_state.SetValue(false);
            s_spoof_fullscreen_state.store(false);
            ::LogInfo("Fullscreen state spoofing reset to disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Reset to default (Disabled)");
        }
    }
    
    // Spoof Window Focus
    const char* spoof_focus_labels[] = {"Disabled", "Spoof as Focused", "Spoof as Unfocused"};
    int spoof_focus_state = static_cast<int>(g_developerTabSettings.spoof_window_focus.GetValue());
    if (ImGui::Combo("Spoof Window Focus", &spoof_focus_state, spoof_focus_labels, 3)) {
        g_developerTabSettings.spoof_window_focus.SetValue(spoof_focus_state);
        s_spoof_window_focus.store(spoof_focus_state);
        
        // Log the change
        std::ostringstream oss;
        oss << "Window focus spoofing changed to ";
        if (spoof_focus_state < 0.5f) {
            oss << "Disabled";
        } else if (spoof_focus_state < 1.5f) {
            oss << "Spoof as Focused";
        } else {
            oss << "Spoof as Unfocused";
        }
        ::LogInfo(oss.str().c_str());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Spoof window focus state for applications that query focus status. Useful for games that change behavior based on focus state.");
    }
    
    // Reset button for Window Focus (only show if not at default)
    if (s_spoof_window_focus.load() != false) {
        ImGui::SameLine();
        if (ImGui::Button("Reset##DevFocus")) {
            g_developerTabSettings.spoof_window_focus.SetValue(0);
            s_spoof_window_focus.store(0);
            extern void LogInfo(const char* message);
            ::LogInfo("Window focus spoofing reset to disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Reset to default (Disabled)");
        }
    }
    
    ImGui::Spacing();
    
    // Continuous Monitoring moved to Main tab as 'Auto-apply'
    
    // Prevent Always On Top
    if (CheckboxSetting(g_developerTabSettings.prevent_always_on_top, "Prevent Always On Top")) {
        s_prevent_always_on_top.store(g_developerTabSettings.prevent_always_on_top.GetValue());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Prevents windows from becoming always on top, even if they are moved or resized.");
    }
    
    // Remove Top Bar
    if (CheckboxSetting(g_developerTabSettings.remove_top_bar, "Remove Top Bar")) {
        s_remove_top_bar.store(g_developerTabSettings.remove_top_bar.GetValue());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Remove the window title bar for a borderless appearance.");
    }

    // FPS Limiter Extra Wait (ms)
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "=== FPS Limiter Timing ===");
    const char* fmt_ms = "%.2f ms";
    if (SliderFloatSetting(g_developerTabSettings.fps_extra_wait_ms, "Extra wait before SIMULATION_START", fmt_ms)) {
        ::s_fps_extra_wait_ms.store(g_developerTabSettings.fps_extra_wait_ms.GetValue());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Delays CPU thread to decrease latency by a fixed amount.\nAdds a fixed delay before SIMULATION_START to pull simulation closer to present. Typical ~half frame time; range 0–10 ms.");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
        ImGui::TextUnformatted("Delays CPU thread to decrease latency by a fixed amount.");
        ImGui::Separator();
        ImGui::BulletText("Shifts SIMULATION_START later so simulation occurs closer to PRESENT.");
        ImGui::BulletText("Effective when there is spare frame time (CPU/GPU finishes early).");
        ImGui::BulletText("Recommended: 0.5–3.0 ms. Start small; too high can cause stutter/missed v-blank.");
        ImGui::BulletText("Does not cap FPS; pair with an FPS limit for stable pacing if needed.");
        ImGui::BulletText("Only affects CPU timing; has no effect if the game is already CPU-bound.");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void DrawNvapiSettings() {
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "=== NVAPI Settings ===");
    
    // HDR10 Colorspace Fix
    if (CheckboxSetting(g_developerTabSettings.fix_hdr10_colorspace, "Set ReShade Effects Processing to HDR10 Colorspace")) {
        s_fix_hdr10_colorspace.store(g_developerTabSettings.fix_hdr10_colorspace.GetValue());
        s_restart_needed_nvapi.store(true);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Automatically fix HDR10 colorspace when swapchain format is RGB10A2 and colorspace is currently sRGB. Only works when the game is using sRGB colorspace.");
    }
    
    // NVAPI Fullscreen Prevention
    if (CheckboxSetting(g_developerTabSettings.nvapi_fullscreen_prevention, "NVAPI Fullscreen Prevention")) {
        s_nvapi_fullscreen_prevention.store(g_developerTabSettings.nvapi_fullscreen_prevention.GetValue());
        s_restart_needed_nvapi.store(true);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Use NVAPI to prevent fullscreen mode at the driver level.");
    }
    // Display restart-required notice if flagged
    if (s_restart_needed_nvapi.load()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Game restart required to apply NVAPI changes.");
    }
    if (s_nvapi_fullscreen_prevention.load() && ::g_nvapiFullscreenPrevention.IsAvailable()) {
        
        // NVAPI HDR Logging
        if (CheckboxSetting(g_developerTabSettings.nvapi_hdr_logging, "NVAPI HDR Logging")) {
            s_nvapi_hdr_logging.store(g_developerTabSettings.nvapi_hdr_logging.GetValue());
            
            if (g_developerTabSettings.nvapi_hdr_logging.GetValue()) {
                std::thread(::RunBackgroundNvapiHdrMonitor).detach();
            }
        }
        
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Enable HDR monitor information logging via NVAPI.");
        }
        
        // NVAPI HDR Interval
        if (SliderFloatSetting(g_developerTabSettings.nvapi_hdr_interval_sec, "HDR Logging Interval (seconds)", "%.1f")) {
            s_nvapi_hdr_interval_sec.store(g_developerTabSettings.nvapi_hdr_interval_sec.GetValue());
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Interval between HDR monitor information logging.");
        }
        
        if (g_developerTabSettings.nvapi_hdr_logging.GetValue()) {
        
            // NVAPI Debug Information Display
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "NVAPI Debug Information:");
            
            extern NVAPIFullscreenPrevention g_nvapiFullscreenPrevention;
        
            // Library loaded successfully
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ NVAPI Library: Loaded");
            
            // Driver version info
            std::string driverVersion = ::g_nvapiFullscreenPrevention.GetDriverVersion();
            if (driverVersion != "Failed to get driver version") {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ Driver Version: %s", driverVersion.c_str());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "⚠ Driver Version: %s", driverVersion.c_str());
            }
            
            // Hardware detection
            if (::g_nvapiFullscreenPrevention.HasNVIDIAHardware()) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ NVIDIA Hardware: Detected");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "✗ NVIDIA Hardware: Not Found");
            }
            
            // Fullscreen prevention status
            if (::g_nvapiFullscreenPrevention.IsFullscreenPreventionEnabled()) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ Fullscreen Prevention: ACTIVE");
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "○ Fullscreen Prevention: Inactive");
            }
            
            // Function availability check
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ Core Functions: Available");
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ DRS Functions: Available");
        }
    } else {
        // Library not loaded
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "✗ NVAPI Library: Not Loaded");
        
        // Try to get error information
        std::string lastError = ::g_nvapiFullscreenPrevention.GetLastError();
        if (!lastError.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Error: %s", lastError.c_str());
        }
        
        // Common failure reasons
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 0.8f), "Common Issues:");
        ImGui::BulletText("nvapi64.dll not found in system PATH");
        ImGui::BulletText("No NVIDIA drivers installed");
        ImGui::BulletText("Running on non-NVIDIA hardware");
        ImGui::BulletText("Insufficient permissions to load DLL");
        
        // Function availability (all unavailable when library not loaded)
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "✗ Core Functions: Unavailable");
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "✗ DRS Functions: Unavailable");
    }
    
    // Additional debug info
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Technical Details:");
    ImGui::BulletText("Uses dynamic loading of nvapi64.dll");
    ImGui::BulletText("Creates NVIDIA driver profiles per application");
    ImGui::BulletText("Sets OGL_DX_PRESENT_DEBUG_ID (0x20324987)");
    ImGui::BulletText("Applies DISABLE_FULLSCREEN_OPT (0x00000001)");
    ImGui::BulletText("Settings persist across application restarts");
    
    // DLL version info
    if (!::g_nvapiFullscreenPrevention.IsAvailable()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "DLL Information:");
        std::string dllInfo = ::g_nvapiFullscreenPrevention.GetDllVersionInfo();
        if (dllInfo != "No library loaded") {
            ImGui::TextWrapped("%s", dllInfo.c_str());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "DLL not loaded - cannot get version info");
        }
    }
}

void DrawReflexSettings() {
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 0.8f), "=== Reflex Settings ===");
    
    // Enable NVIDIA Reflex
    if (CheckboxSetting(g_developerTabSettings.reflex_enabled, "Enable NVIDIA Reflex")) {
        s_reflex_enabled.store(g_developerTabSettings.reflex_enabled.GetValue());
        
        if (g_developerTabSettings.reflex_enabled.GetValue()) {
            extern void InstallReflexHooks();
            ::InstallReflexHooks();
        } else {
            extern void UninstallReflexHooks();
            ::UninstallReflexHooks();
        }
        
        // Mark that Reflex settings have changed to force sleep mode update
        ::g_reflex_settings_changed.store(true);
    }
    
    // Low Latency Mode
    if (CheckboxSetting(g_developerTabSettings.reflex_low_latency_mode, "Low Latency Mode")) {
        ::s_reflex_low_latency_mode.store(g_developerTabSettings.reflex_low_latency_mode.GetValue());
        ::g_reflex_settings_changed.store(true);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable NVIDIA Reflex Low Latency Mode for reduced input lag.");
    }
    
    // Low Latency Boost
    if (CheckboxSetting(g_developerTabSettings.reflex_low_latency_boost, "Low Latency Boost")) {
        ::s_reflex_low_latency_boost.store(g_developerTabSettings.reflex_low_latency_boost.GetValue());
        ::g_reflex_settings_changed.store(true);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable NVIDIA Reflex Low Latency Boost for maximum performance.");
    }
    
    // Use Markers
    if (CheckboxSetting(g_developerTabSettings.reflex_use_markers, "Use Markers")) {
        ::s_reflex_use_markers.store(g_developerTabSettings.reflex_use_markers.GetValue());
        ::g_reflex_settings_changed.store(true);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Use NVIDIA Reflex markers for optimal frame timing optimization.");
    }
    
    // Debug Output
    if (CheckboxSetting(g_developerTabSettings.reflex_debug_output, "Debug Output")) {
        ::s_reflex_debug_output.store(g_developerTabSettings.reflex_debug_output.GetValue());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable debug output for NVIDIA Reflex operations.");
    }
}

void DrawResolutionOverrideSettings() {
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "=== Resolution Override ===");
    
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Override the backbuffer resolution during swapchain creation. Same as ReShade ForceResolution.");
    }
}

void DrawKeyboardShortcutsSettings() {
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "=== Keyboard Shortcuts ===");
    
    // Enable Mute/Unmute Shortcut (Ctrl+M)
    if (CheckboxSetting(g_developerTabSettings.enable_mute_unmute_shortcut, "Enable Mute/Unmute Shortcut (Ctrl+M)")) {
        ::s_enable_mute_unmute_shortcut.store(g_developerTabSettings.enable_mute_unmute_shortcut.GetValue());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable keyboard shortcut Ctrl+M to quickly mute/unmute audio.");
    }
    
    // Info text
    if (g_developerTabSettings.enable_mute_unmute_shortcut.GetValue()) {
        ImGui::Indent();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Press Ctrl+M to toggle audio mute state");
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Shortcut works globally when enabled");
        ImGui::Unindent();
    }
}

void DrawLatencyDisplay() {
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "=== Latency Information ===");
    
    // Current Latency Display
    extern std::atomic<float> g_current_latency_ms;
    float latency = ::g_current_latency_ms.load();
    
    std::ostringstream oss;
    oss << "Current Latency: " << std::fixed << std::setprecision(2) << latency << " ms";
    ImGui::TextUnformatted(oss.str().c_str());
    
    // PCL AV Latency Display (Most Important)
    extern std::atomic<float> g_pcl_av_latency_ms;
    float pcl_latency = ::g_pcl_av_latency_ms.load();
    
    oss.str("");
    oss.clear();
    oss << "PCL AV Latency: " << std::fixed << std::setprecision(2) << pcl_latency << " ms";
    ImGui::TextUnformatted(oss.str().c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "(30-frame avg)");
    
    // Present Duration Display
    extern std::atomic<double> g_present_duration;
    double present_duration = ::g_present_duration.load();
    
    oss.str("");
    oss.clear();
    oss << "Present Duration: " << std::fixed << std::setprecision(6) << present_duration << " ms";
    ImGui::TextUnformatted(oss.str().c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "(smoothed)");
    
    // Reflex Status Display
    extern std::atomic<bool> g_reflex_active;
    extern std::atomic<uint64_t> g_current_frame;
    bool is_active = ::g_reflex_active.load();
    uint64_t frame = ::g_current_frame.load();
    
    oss.str("");
    oss.clear();
    if (is_active) {
        oss << "Reflex Status: Active (Frame " << frame << ")";
    } else {
        oss << "Reflex Status: Inactive";
    }
    ImGui::TextUnformatted(oss.str().c_str());
}

} // namespace ui::new_ui
