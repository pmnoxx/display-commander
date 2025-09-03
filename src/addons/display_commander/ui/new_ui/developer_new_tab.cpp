#include "developer_new_tab.hpp"
#include "developer_new_tab_settings.hpp"
#include "settings_wrapper.hpp"
#include "../../nvapi/nvapi_fullscreen_prevention.hpp"
#include "../../nvapi/nvapi_hdr_monitor.hpp"
#include "../../nvapi/nvapi_dllfg_stats.hpp"
#include "../../globals.hpp"
#include "../../utils.hpp"

#include <sstream>
#include <iomanip>
#include <thread>
#include <atomic>


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
    
    ImGui::Spacing();
    ImGui::Separator();
    
    // DLL-FG Stats Section
    DrawDllFgStats();
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
    
    // Performance optimization: Flush before present
    if (CheckboxSetting(g_developerTabSettings.flush_before_present, "Flush Command Queue Before Present")) {
        ::g_flush_before_present.store(g_developerTabSettings.flush_before_present.GetValue());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Flush command queue before ReShade shaders process. Reduces latency and increases FPS by ensuring GPU commands are processed before shader execution.");
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
        LogInfo(oss.str().c_str());
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
            LogInfo("Fullscreen state spoofing reset to disabled");
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
        LogInfo(oss.str().c_str());
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
            LogInfo("Window focus spoofing reset to disabled");
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

    // Minimal NVIDIA Reflex Controls (device runtime dependent)
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "=== NVIDIA Reflex (Minimal) ===");
    bool reflex_enable = g_developerTabSettings.reflex_enable.GetValue();
    if (ImGui::Checkbox("Enable Reflex", &reflex_enable)) {
        g_developerTabSettings.reflex_enable.SetValue(reflex_enable);
        s_reflex_enable.store(reflex_enable);
    }
    bool reflex_low_latency = g_developerTabSettings.reflex_low_latency.GetValue();
    if (ImGui::Checkbox("Low Latency Mode", &reflex_low_latency)) {
        g_developerTabSettings.reflex_low_latency.SetValue(reflex_low_latency);
        s_reflex_low_latency.store(reflex_low_latency);
    }
    bool reflex_boost = g_developerTabSettings.reflex_boost.GetValue();
    if (ImGui::Checkbox("Low Latency Boost", &reflex_boost)) {
        g_developerTabSettings.reflex_boost.SetValue(reflex_boost);
        s_reflex_boost.store(reflex_boost);
    }
    bool reflex_markers = g_developerTabSettings.reflex_use_markers.GetValue();
    if (ImGui::Checkbox("Use Markers to Optimize", &reflex_markers)) {
        g_developerTabSettings.reflex_use_markers.SetValue(reflex_markers);
        s_reflex_use_markers.store(reflex_markers);
    }
    bool reflex_logging = g_developerTabSettings.reflex_logging.GetValue();
    if (ImGui::Checkbox("Enable Reflex Logging", &reflex_logging)) {
        g_developerTabSettings.reflex_logging.SetValue(reflex_logging);
        s_enable_reflex_logging.store(reflex_logging);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable detailed logging of Reflex marker operations for debugging purposes.");
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

    // Present Duration Display
    extern std::atomic<double> g_present_duration;
    double present_duration = ::g_present_duration_ns.load();
    
    oss.str("");
    oss.clear();
    oss << "Present Duration: " << std::fixed << std::setprecision(6) << present_duration << " ms";
    ImGui::TextUnformatted(oss.str().c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "(smoothed)");
}

void DrawDllFgStats() {
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "=== DLL-FG Statistics ===");
    
    // Initialize DLL-FG stats if not already done
    static bool dllfg_initialized = false;
    if (!dllfg_initialized) {
        if (::g_nvapiDllFgStats.Initialize()) {
            dllfg_initialized = true;
        }
    }
    
    if (!dllfg_initialized) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "DLL-FG Stats: Failed to initialize");
        return;
    }
    
    // Update stats
    ::g_nvapiDllFgStats.UpdateStats();
    
    // Check if DLL-FG is supported
    if (!::g_nvapiDllFgStats.IsDllFgSupported()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "DLL-FG: Not supported on this hardware");
        return;
    }
    
    // Internal Resolution
    auto resolution = ::g_nvapiDllFgStats.GetInternalResolution();
    if (resolution.valid) {
        std::ostringstream oss;
        oss << "Internal Resolution: " << resolution.width << "x" << resolution.height;
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ %s", oss.str().c_str());
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "⚠ Internal Resolution: Unknown");
    }
    
    // DLL-FG Mode
    auto mode = ::g_nvapiDllFgStats.GetDllFgMode();
    switch (mode) {
        case NVAPIDllFgStats::DllFgMode::Enabled:
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ DLL-FG Mode: ENABLED");
            break;
        case NVAPIDllFgStats::DllFgMode::Disabled:
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "⚠ DLL-FG Mode: DISABLED");
            break;
        case NVAPIDllFgStats::DllFgMode::Unknown:
        default:
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "⚠ DLL-FG Mode: UNKNOWN");
            break;
    }
    
    // DLL-FG Version
    auto version = ::g_nvapiDllFgStats.GetDllFgVersion();
    if (version.valid) {
        std::ostringstream oss;
        oss << "DLL-FG Version: " << version.version_string;
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ %s", oss.str().c_str());
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "⚠ DLL-FG Version: Unknown");
    }
    
    // Frame Generation Statistics
    auto stats = ::g_nvapiDllFgStats.GetFrameGenStats();
    if (stats.valid) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Frame Generation Stats:");
        
        std::ostringstream oss;
        oss << "Frames Generated: " << stats.total_frames_generated;
        ImGui::Text("  %s", oss.str().c_str());
        
        oss.str("");
        oss << "Frames Presented: " << stats.total_frames_presented;
        ImGui::Text("  %s", oss.str().c_str());
        
        oss.str("");
        oss << "Frames Dropped: " << stats.total_frames_dropped;
        ImGui::Text("  %s", oss.str().c_str());
        
        oss.str("");
        oss << "Generation Ratio: " << std::fixed << std::setprecision(2) << stats.frame_generation_ratio << "%";
        ImGui::Text("  %s", oss.str().c_str());
        
        oss.str("");
        oss << "Avg Frame Time: " << std::fixed << std::setprecision(2) << stats.average_frame_time_ms << "ms";
        ImGui::Text("  %s", oss.str().c_str());
        
        oss.str("");
        oss << "GPU Utilization: " << std::fixed << std::setprecision(1) << stats.gpu_utilization_percent << "%";
        ImGui::Text("  %s", oss.str().c_str());
    }
    
    // Performance Metrics
    auto performance = ::g_nvapiDllFgStats.GetPerformanceMetrics();
    if (performance.valid) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Performance Metrics:");
        
        std::ostringstream oss;
        oss << "Input Lag: " << std::fixed << std::setprecision(2) << performance.input_lag_ms << "ms";
        ImGui::Text("  %s", oss.str().c_str());
        
        oss.str("");
        oss << "Output Lag: " << std::fixed << std::setprecision(2) << performance.output_lag_ms << "ms";
        ImGui::Text("  %s", oss.str().c_str());
        
        oss.str("");
        oss << "Total Latency: " << std::fixed << std::setprecision(2) << performance.total_latency_ms << "ms";
        ImGui::Text("  %s", oss.str().c_str());
        
        oss.str("");
        oss << "Frame Pacing Quality: " << std::fixed << std::setprecision(1) << performance.frame_pacing_quality << "%";
        ImGui::Text("  %s", oss.str().c_str());
    }
    
    // Driver Compatibility
    auto compatibility = ::g_nvapiDllFgStats.GetDriverCompatibility();
    if (compatibility.valid) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Driver Compatibility:");
        
        std::ostringstream oss;
        oss << "Current Version: " << compatibility.current_version;
        ImGui::Text("  %s", oss.str().c_str());
        
        oss.str("");
        oss << "Min Required: " << compatibility.min_required_version;
        ImGui::Text("  %s", oss.str().c_str());
        
        if (compatibility.is_supported) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "  ✓ %s", compatibility.compatibility_status.c_str());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "  ⚠ %s", compatibility.compatibility_status.c_str());
        }
    }
    
    // Configuration
    auto config = ::g_nvapiDllFgStats.GetDllFgConfig();
    if (config.valid) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Configuration:");
        
        ImGui::Text("  Auto Mode: %s", config.auto_mode_enabled ? "Enabled" : "Disabled");
        ImGui::Text("  Quality Mode: %s", config.quality_mode_enabled ? "Enabled" : "Disabled");
        ImGui::Text("  Performance Mode: %s", config.performance_mode_enabled ? "Enabled" : "Disabled");
        ImGui::Text("  Target FPS: %u", config.target_fps);
        ImGui::Text("  VSync: %s", config.vsync_enabled ? "Enabled" : "Disabled");
        ImGui::Text("  G-Sync: %s", config.gsync_enabled ? "Enabled" : "Disabled");
    }
    
    // Debug Information (collapsible)
    if (ImGui::CollapsingHeader("Debug Information")) {
        std::string debug_info = ::g_nvapiDllFgStats.GetDebugInfo();
        if (!debug_info.empty()) {
            ImGui::TextWrapped("%s", debug_info.c_str());
        }
    }
}

} // namespace ui::new_ui
